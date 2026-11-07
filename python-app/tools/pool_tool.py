#!/usr/bin/env python3
"""Google account pool for Cut Pro's Gemini workers ("GodMode Pool").

Ported from the Electron app's pool (decolua/9router's Antigravity protocol):
signed-in Google accounts whose own Gemini quota serves transcription and
translation, rotated per request. Deliberately standard-library only - the
app's shipped Python runtime has no pip packages beyond edge-tts, so every
mechanism here is urllib / http.server / ctypes.

Subcommands (newline-delimited JSON on stdout):

  pool_tool.py state                 accounts + counters + strategy
  pool_tool.py add                   emits authUrl, then waits for Google sign-in
  pool_tool.py remove ACCOUNT_ID     drop one account
  pool_tool.py set-enabled ID 0|1    exclude/include an account in routing
  pool_tool.py verify [ACCOUNT_ID]   test Antigravity generation access
  pool_tool.py set-strategy NAME     round-robin | least-used | random
  pool_tool.py serve                 run the loopback rotation server (the Core)

The Core listens on 127.0.0.1 only and speaks the same request shape as the
public Gemini API, so the workers call it like any other endpoint:

  POST http://127.0.0.1:20128/v1beta/models/<model>:generateContent

Each request is forwarded to Google's Antigravity (Code Assist) endpoint under
one ready account, chosen by the configured strategy. Requests share bounded
queues, pacing and duplicate suppression. Quota responses pause the whole pool;
authorization and policy rejections are never retried through another account.
Refresh tokens never leave this machine: they live DPAPI-encrypted in
%APPDATA%/CutPro/pool-accounts.bin.
"""
import base64
import contextlib
import ctypes
import hashlib
import http.client
import json
import math
import os
import random
import re
import secrets
import socket
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from concurrent.futures import ThreadPoolExecutor, as_completed
from email.utils import parsedate_to_datetime
# The Windows embeddable runtime's ._pth excludes the launched script's folder.
# Resolve our bundled sibling explicitly, for both console and GUI launches.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pool_activity import RouterActivity
from persistent_retry import generation_rejection

CREATE_NO_WINDOW = 0x08000000 if os.name == "nt" else 0
_OUTPUT_LOCK = threading.Lock()

DATA_DIR = os.path.join(
    os.environ.get("APPDATA") or os.path.expanduser("~"),
    "CutPro")
ACCOUNTS_PATH = os.path.join(DATA_DIR, "pool-accounts.bin")
STATE_PATH = os.path.join(DATA_DIR, "pool-state.json")
POOL_PORT = int(os.environ.get("CUTPRO_POOL_PORT", "20128"))
MAX_ACCOUNTS = 20

# Antigravity (Code Assist) endpoints. Chat traffic uses the "daily" host;
# project discovery stays on the prod host - the daily host rejects it.
CHAT_ORIGIN = "https://daily-cloudcode-pa.googleapis.com"
CODE_ASSIST_ORIGIN = "https://cloudcode-pa.googleapis.com"
AUTH_ENDPOINT = "https://accounts.google.com/o/oauth2/v2/auth"
TOKEN_ENDPOINT = "https://oauth2.googleapis.com/token"
USERINFO_ENDPOINT = "https://www.googleapis.com/oauth2/v1/userinfo"
AG_SCOPES = " ".join((
    "https://www.googleapis.com/auth/cloud-platform",
    "https://www.googleapis.com/auth/userinfo.email",
    "https://www.googleapis.com/auth/userinfo.profile",
    "https://www.googleapis.com/auth/cclog",
    "https://www.googleapis.com/auth/experimentsandconfigs",
))
# Antigravity Hub's public installed-app client and its desktop fingerprint -
# Google provisions companion projects only for this exact identity, and extra
# headers (X-Goog-Api-Client and friends) make it silently refuse.
ANTIGRAVITY_CLIENT_ID = "1071006060591-tmhssin2h21lcre235vtolojh4g403ep.apps.googleusercontent.com"
ANTIGRAVITY_CLIENT_SECRET = os.environ.get("CUTPRO_ANTIGRAVITY_CLIENT_SECRET", "")
GEMINI_CLI_CLIENT_ID = "681255809395-oo8ft2oprdrnp9e3aqf6av3hmdib135j.apps.googleusercontent.com"
GEMINI_CLI_CLIENT_SECRET = os.environ.get("CUTPRO_GEMINI_CLI_CLIENT_SECRET", "")
ANTIGRAVITY_USER_AGENT = "antigravity/hub/2.24.0 darwin/arm64"

STRATEGIES = ("round-robin", "least-used", "random")
REQUEST_BODY_CAP = 16_000_000
RESPONSE_BODY_CAP = 12_000_000
REQUEST_DEADLINE_SECONDS = 240.0
UPSTREAM_TIMEOUT_SECONDS = 90.0
RETRYABLE_ATTEMPTS = 3
RETRY_BACKOFF_SECONDS = 0.6
MAX_ACTIVE_REQUESTS = 2
MAX_PENDING_REQUESTS = 32
GLOBAL_REQUEST_INTERVAL_SECONDS = 2.0
ACCOUNT_REQUEST_INTERVAL_SECONDS = 6.0
DUPLICATE_TTL_SECONDS = 30.0
DUPLICATE_CACHE_BYTES = 16_000_000
CIRCUIT_FAILURE_LIMIT = 3
CIRCUIT_COOLDOWN_SECONDS = 60.0


def pool_error(status, message, retry_after=0):
    # The Core owns upstream retries. Workers must not multiply that budget or
    # bypass a rejection by falling back to separately configured API keys.
    error = {"code": status, "message": message, "retryable": False}
    if status in (408, 429, 500, 502, 503, 504):
        # A new work-unit attempt must let this request's cached failure expire.
        retry_after = max(retry_after, DUPLICATE_TTL_SECONDS)
    if retry_after > 0:
        error["retryAfterSeconds"] = math.ceil(retry_after)
    return status, {"error": error}


def retry_after_seconds(value, now=None):
    now = time.time() if now is None else now
    try:
        seconds = float(value)
        if math.isfinite(seconds) and seconds >= 0:
            return max(1.0, seconds)
    except (TypeError, ValueError):
        pass
    try:
        return max(1.0, parsedate_to_datetime(value).timestamp() - now)
    except (TypeError, ValueError, OverflowError):
        return 60.0


def reject_json_constant(value):
    raise ValueError("Non-finite JSON numbers are not supported.")


def note(message):
    sys.stderr.write(str(message) + "\n")
    sys.stderr.flush()


def emit(payload):
    with _OUTPUT_LOCK:
        sys.stdout.write(json.dumps(payload, ensure_ascii=False) + "\n")
        sys.stdout.flush()


# --- DPAPI-encrypted account store (per-user, no dependencies) ---------------

class DATA_BLOB(ctypes.Structure):
    _fields_ = [("cbData", ctypes.c_uint), ("pbData", ctypes.POINTER(ctypes.c_char))]


def _dpapi_call(utility, data):
    """CryptProtectData / CryptUnprotectData over one buffer, current user."""
    buffer = ctypes.create_string_buffer(data, len(data))
    blob_in = DATA_BLOB(len(data), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_char)))
    blob_out = DATA_BLOB()
    try:
        if not utility(ctypes.byref(blob_in), None, None, None, None, 0,
                       ctypes.byref(blob_out)):
            raise OSError("DPAPI call failed")
        try:
            return ctypes.string_at(blob_out.pbData, blob_out.cbData)
        finally:
            ctypes.windll.kernel32.LocalFree(blob_out.pbData)
    finally:
        pass


def dpapi_protect(data):
    return _dpapi_call(ctypes.windll.crypt32.CryptProtectData, data)


def dpapi_unprotect(data):
    return _dpapi_call(ctypes.windll.crypt32.CryptUnprotectData, data)


def load_store():
    """The decrypted document, or a fresh one. Tamper fails closed."""
    try:
        with open(ACCOUNTS_PATH, "rb") as handle:
            document = json.loads(dpapi_unprotect(handle.read()).decode("utf-8"))
    except FileNotFoundError:
        return {"version": 1, "strategy": "round-robin", "accounts": []}
    except Exception as error:
        raise RuntimeError("The pool account store could not be read: %s" % error)
    if not isinstance(document, dict) or document.get("version") != 1:
        raise RuntimeError("The pool account store is unreadable.")
    document.setdefault("strategy", "round-robin")
    document["accounts"] = [account for account in document.get("accounts", [])
                            if isinstance(account, dict) and account.get("id")]
    return document


def save_store(document):
    os.makedirs(DATA_DIR, exist_ok=True)
    blob = dpapi_protect(json.dumps(document).encode("utf-8"))
    handle, temp_path = tempfile.mkstemp(dir=DATA_DIR, suffix=".tmp")
    try:
        with os.fdopen(handle, "wb") as stream:
            stream.write(blob)
        os.replace(temp_path, ACCOUNTS_PATH)
    finally:
        if os.path.exists(temp_path):
            os.remove(temp_path)


_STORE_THREAD_LOCK = threading.RLock()


@contextlib.contextmanager
def store_lock():
    """Serialize short store updates across Core threads and GUI workers."""
    os.makedirs(DATA_DIR, exist_ok=True)
    with _STORE_THREAD_LOCK, open(os.path.join(DATA_DIR, "pool-accounts.lock"), "a+b") as handle:
        if os.name == "nt":
            import msvcrt
            handle.seek(0, os.SEEK_END)
            if handle.tell() == 0:
                handle.write(b"0")
                handle.flush()
            deadline = time.monotonic() + 15
            while True:
                try:
                    handle.seek(0)
                    msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
                    break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise RuntimeError("The pool account store is busy. Try again.")
                    time.sleep(0.05)
            try:
                yield
            finally:
                handle.seek(0)
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX)
            try:
                yield
            finally:
                fcntl.flock(handle.fileno(), fcntl.LOCK_UN)


def update_store(operation):
    with store_lock():
        document = load_store()
        operation(document)
        save_store(document)
        return document


def update_account(account_id, operation):
    def edit(document):
        account = next((a for a in document["accounts"] if a["id"] == account_id), None)
        if account is not None:
            operation(account)
    return update_store(edit)


def public_accounts(document):
    accounts = []
    for account in document.get("accounts", []):
        accounts.append({
            "id": account["id"],
            "email": account.get("email", ""),
            "enabled": bool(account.get("enabled", True)),
            "tier": account.get("tier", "unknown"),
            "verificationUrl": safe_verification_url(account.get("verificationUrl")) if account.get("verificationUrl") else "",
            "status": ("ready" if account.get("status") == "cooldown"
                       and 0 < account.get("cooldownUntil", 0) <= time.time()
                       else account.get("status", "unverified")),
            "statusDetail": ("" if account.get("status") == "cooldown"
                             and 0 < account.get("cooldownUntil", 0) <= time.time()
                             else account.get("statusDetail", "")),
            "served": account.get("served", 0),
            "lastUsedAt": account.get("lastUsedAt", 0),
            "addedAt": account.get("addedAt", 0),
            "availableModels": account.get("availableModels", []),
            "modelsCheckedAt": account.get("modelsCheckedAt", 0),
            "modelError": account.get("modelError", ""),
        })
    return accounts


def write_state(core, document=None):
    """A snapshot the QML side polls, so it never talks to a live process."""
    try:
        document = document or load_store()
        os.makedirs(DATA_DIR, exist_ok=True)
        state = {
            "core": core,
            "endpoint": "127.0.0.1:%d" % POOL_PORT,
            "strategy": document.get("strategy", "round-robin"),
            "accounts": public_accounts(document),
            "readyCount": sum(1 for a in public_accounts(document)
                              if a["enabled"] and a["status"] == "ready"),
            "servedTotal": sum(a["served"] for a in public_accounts(document)),
            "updatedAt": int(time.time()),
        }
        with open(STATE_PATH, "w", encoding="utf-8") as handle:
            json.dump(state, handle)
    except Exception as error:
        note("pool: state write failed: %s" % error)


# --- Google sign-in + token maintenance --------------------------------------

ANTIGRAVITY_SETUP_URL = "https://antigravity.google/"


def safe_verification_url(value):
    if isinstance(value, str):
        try:
            parsed = urllib.parse.urlparse(value)
            if (parsed.scheme == "https" and parsed.hostname in ("accounts.google.com", "antigravity.google")
                    and not parsed.username and not parsed.password and parsed.port in (None, 443)):
                return value
        except ValueError:
            pass
    return ANTIGRAVITY_SETUP_URL


class GoogleRequestError(RuntimeError):
    def __init__(self, status, error):
        error = error if isinstance(error, dict) else {}
        message = str(error.get("message") or "Google rejected the request.")
        self.status = status
        self.verification_required = any(text in message.lower() for text in (
            "verify your account", "verification required", "validation_required"))
        urls = []
        def inspect(value):
            if isinstance(value, dict):
                for key, item in value.items():
                    if key in ("validation_url", "validationUrl", "verification_url", "verificationUrl") and isinstance(item, str):
                        urls.append(item)
                        self.verification_required = True
                    if key == "reason" and item in ("VALIDATION_REQUIRED", "ACCOUNT_VERIFICATION_REQUIRED"):
                        self.verification_required = True
                    inspect(item)
            elif isinstance(value, list):
                for item in value:
                    inspect(item)
        inspect(error.get("details", []))
        urls += re.findall(r'https://[^\s<>"\']+', message)
        self.verification_url = next((url for url in urls if safe_verification_url(url) == url), ANTIGRAVITY_SETUP_URL)
        super().__init__("Google request failed (%d): %s" % (status, message[:200]))


def verification_failure(error):
    if isinstance(error, GoogleRequestError) and error.verification_required:
        return {"status": "unverified", "verificationUrl": error.verification_url,
                "statusDetail": "Finish Google verification and Antigravity setup with this email, then recheck access."}
    if isinstance(error, GoogleRequestError) and error.status == 429:
        return {"status": "cooldown", "verificationUrl": "", "cooldownUntil": time.time() + 60,
                "statusDetail": "Google quota was reached. Recheck access after the cooldown."}
    return {"status": "error", "verificationUrl": "", "statusDetail": str(error)[:200]}


def http_json(url, payload=None, headers=None, timeout=30):
    data = json.dumps(payload).encode("utf-8") if payload is not None else None
    request = urllib.request.Request(url, data=data, method="POST" if data else "GET")
    request.add_header("Content-Type", "application/json")
    for key, value in (headers or {}).items():
        request.add_header(key, value)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        try:
            detail = json.loads(error.read(16000).decode("utf-8", errors="replace")).get("error", {})
        except (ValueError, AttributeError):
            detail = {}
        raise GoogleRequestError(error.code, detail) from error
    except (urllib.error.URLError, TimeoutError) as error:
        raise RuntimeError("Google connection failed: %s" % error) from error


def refresh_access_token(account):
    """A valid access token, refreshed (and cached in memory) as needed."""
    cached = _TOKEN_CACHE.get(account["id"])
    if cached and cached[0] > time.time() + 60:
        return cached[1]
    issuer = account.get("issuer", "antigravity")
    client_id = (GEMINI_CLI_CLIENT_ID if issuer == "gemini-cli"
                 else ANTIGRAVITY_CLIENT_ID)
    client_secret = (GEMINI_CLI_CLIENT_SECRET if issuer == "gemini-cli"
                     else ANTIGRAVITY_CLIENT_SECRET)
    body = urllib.parse.urlencode({
        "client_id": client_id, "client_secret": client_secret,
        "refresh_token": account["refreshToken"],
        "grant_type": "refresh_token",
    }).encode("utf-8")
    request = urllib.request.Request(TOKEN_ENDPOINT, data=body, method="POST")
    request.add_header("Content-Type", "application/x-www-form-urlencoded")
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            tokens = json.loads(response.read().decode("utf-8"))
    except Exception as error:
        raise RuntimeError("This account's Google session has expired. Remove "
                           "the account and sign in again.") from error
    token = tokens.get("access_token")
    if not token:
        raise RuntimeError("Google did not return an access token.")
    lifetime = float(tokens.get("expires_in", 3600))
    _TOKEN_CACHE[account["id"]] = (time.time() + lifetime, token)
    return token


_TOKEN_CACHE = {}


def ag_code_assist_headers(token):
    return {
        "Authorization": "Bearer %s" % token,
        "Content-Type": "application/json",
        "User-Agent": ANTIGRAVITY_USER_AGENT,
        "x-request-source": "local",
    }


def detect_profile(token):
    """Companion project + quota tier for an access token (Antigravity identity)."""
    data = http_json(
        CODE_ASSIST_ORIGIN + "/v1internal:loadCodeAssist",
        payload={"metadata": {"ideType": 9, "platform": 5, "pluginType": 2}},
        headers=ag_code_assist_headers(token), timeout=20)
    project = data.get("cloudaicompanionProject")
    if isinstance(project, dict):
        project = project.get("id")
    if not project:
        raise GoogleRequestError(403, {"message": "Antigravity verification required: finish account setup."})
    active_tier = data.get("currentTier") or next((t for t in data.get("allowedTiers") or [] if t.get("isDefault")), {})
    tier_id = str(active_tier.get("id") or "").strip().lower()
    tier = ("paid" if tier_id in ("standard", "standard-tier", "enterprise", "pro")
            else "free" if tier_id in ("free", "free-tier", "legacy-tier") else "unknown")
    return tier, project


def sign_in():
    """The full loopback sign-in: browser consent -> tokens -> stored account."""
    if len(load_store()["accounts"]) >= MAX_ACCOUNTS:
        raise RuntimeError("The pool holds at most %d accounts." % MAX_ACCOUNTS)
    state = secrets.token_hex(8)
    server = ThreadingHTTPServer(("127.0.0.1", 0), _SignInHandler)
    server.state, server.result, server.error = state, None, None
    port = server.server_address[1]
    redirect_uri = "http://127.0.0.1:%d/oauth2callback" % port
    params = urllib.parse.urlencode({
        "client_id": ANTIGRAVITY_CLIENT_ID, "redirect_uri": redirect_uri,
        "response_type": "code", "scope": AG_SCOPES, "state": state,
        "access_type": "offline", "prompt": "consent",
    })
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        # The GUI process opens this URL with QDesktopServices (a pythonw
        # subprocess cannot be relied on to launch the browser itself).
        emit({"authUrl": "%s?%s" % (AUTH_ENDPOINT, params)})
        deadline = time.time() + 300
        while time.time() < deadline and server.result is None and server.error is None:
            time.sleep(0.2)
        if server.error:
            raise RuntimeError(server.error)
        if server.result is None:
            raise RuntimeError("Sign-in timed out. Try again.")
        code = server.result
        body = urllib.parse.urlencode({
            "client_id": ANTIGRAVITY_CLIENT_ID,
            "client_secret": ANTIGRAVITY_CLIENT_SECRET,
            "code": code, "redirect_uri": redirect_uri,
            "grant_type": "authorization_code",
        }).encode("utf-8")
        request = urllib.request.Request(TOKEN_ENDPOINT, data=body, method="POST")
        request.add_header("Content-Type", "application/x-www-form-urlencoded")
        with urllib.request.urlopen(request, timeout=30) as response:
            tokens = json.loads(response.read().decode("utf-8"))
        refresh_token = tokens.get("refresh_token")
        if not refresh_token:
            raise RuntimeError("Google did not grant a long-lived session token.")
        email = (http_json(USERINFO_ENDPOINT + "?alt=json", headers={
            "Authorization": "Bearer %s" % tokens["access_token"],
        }) or {}).get("email") or ""
        account = {
            "id": str(uuid.uuid4()), "email": email,
            "refreshToken": refresh_token, "issuer": "antigravity",
            "tier": "unknown", "projectId": None, "status": "unverified", "enabled": True,
            "statusDetail": "", "served": 0, "addedAt": int(time.time()),
        }
        # Keep the encrypted Google session even when Antigravity requires a
        # separate setup step; the user can finish it and recheck this account.
        account.update(verify_account_access(account, tokens["access_token"]))
        def add(document):
            if any(a.get("email") == email for a in document["accounts"]):
                raise RuntimeError("%s is already in the pool." % email)
            if len(document["accounts"]) >= MAX_ACCOUNTS:
                raise RuntimeError("The pool holds at most %d accounts." % MAX_ACCOUNTS)
            document["accounts"].append(account)
        document = update_store(add)
        write_state("running", document)
        emit({"added": True, "email": email, "tier": account["tier"],
              "status": account["status"]})
        return 0
    finally:
        server.shutdown()
        server.server_close()


class _SignInHandler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        query = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
        if urllib.parse.urlparse(self.path).path != "/oauth2callback":
            self.send_response(404)
            self.end_headers()
            return
        if query.get("state", [""])[0] != self.server.state:
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b"state mismatch")
            return
        if self.server.result is not None or self.server.error is not None:
            self.send_response(409)
            self.end_headers()
            return
        code = (query.get("code") or [""])[0]
        if query.get("error") or not code:
            self.server.error = ("Google sign-in was cancelled or access was denied. Try again."
                                 if query.get("error") else "Google did not return a sign-in code. Try again.")
            self.send_response(400)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(b"<h2>Sign-in was not completed</h2>"
                             b"<p>Return to Cut Pro to try again.</p>")
            return
        self.server.result = code
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()
        self.wfile.write(b"<meta charset='utf-8'><body style='background:#111;"
                         b"color:#eee;font-family:sans-serif;text-align:center;"
                         b"padding-top:18vh'><h2>Sign-in complete</h2>"
                         b"<p style='color:#999'>You can close this tab and "
                         b"return to Cut Pro.</p></body>")


# --- The Core: a loopback server that rotates accounts ------------------------

ROUTER_PROTOCOL_VERSION = 2


def worker_revision():
    try:
        with open(__file__, "rb") as handle:
            return hashlib.sha256(handle.read()).hexdigest()
    except OSError:
        return ""


LOADED_WORKER_REVISION = worker_revision()


def compatible_router(health):
    return (isinstance(health, dict) and health.get("ok") is True
            and health.get("routerProtocol") == ROUTER_PROTOCOL_VERSION)


class _AccountSelectionChanged(Exception):
    pass


class _RouterUpdated(Exception):
    pass


def watch_worker_revision(server, stopped):
    while not stopped.wait(0.5):
        if worker_revision() == LOADED_WORKER_REVISION:
            continue
        # Already dispatched uploads may finish. Queued work and retries check
        # the revision before dispatch and release their reservations.
        with CORE.lock:
            busy = CORE.in_flight > 0
        if not busy:
            server.shutdown()
            return


class PoolCore:
    """One active request per account, shared by every bulk worker."""

    def __init__(self):
        self.in_flight = 0
        self.served_total = 0
        self.last_served = ""
        self.cooldowns = {}
        self.active = set()
        self.round_robin = 0
        self.lock = threading.Lock()
        self.condition = threading.Condition(self.lock)
        self.served_baseline = 0
        self.next_global_send = 0.0
        self.next_account_send = {}
        self.pool_cooldown_until = 0.0
        self.consecutive_failures = 0
        self.duplicates = {}
        self.activity = RouterActivity(self.publish_activity)

    def protection_state(self):
        with self.lock:
            remaining = max(0, math.ceil(self.pool_cooldown_until - time.time()))
        return {"state": "cooldown" if remaining else "active",
                "retryAfterSeconds": remaining, "maxConcurrent": MAX_ACTIVE_REQUESTS,
                "queueLimit": MAX_PENDING_REQUESTS,
                "globalIntervalSeconds": GLOBAL_REQUEST_INTERVAL_SECONDS,
                "accountIntervalSeconds": ACCOUNT_REQUEST_INTERVAL_SECONDS}

    def publish_activity(self, state):
        with self.lock:
            state["servedTotal"] = self.served_baseline + self.served_total
        state["protection"] = self.protection_state()
        emit({"event": "activity", "activity": state})

    def eligible(self, document, tried, model=None):
        now = time.time()
        return [a for a in document.get("accounts", [])
                if a["id"] not in tried
                and a.get("enabled", True)
                and a.get("status") not in ("error", "unverified")
                and max(self.cooldowns.get(a["id"], 0), a.get("cooldownUntil", 0)) <= now
                and (not model or not a.get("availableModels") or model in a["availableModels"])]

    def pick_account(self, document, tried, model=None):
        with self.lock:
            if len(self.active) >= MAX_ACTIVE_REQUESTS:
                return None
            eligible = [a for a in self.eligible(document, tried, model) if a["id"] not in self.active]
            if not eligible:
                return None
            strategy = document.get("strategy", "round-robin")
            if strategy == "random":
                account = random.choice(eligible)
            elif strategy == "least-used":
                account = min(eligible, key=lambda a: (a.get("served", 0), a.get("lastUsedAt", 0)))
            else:
                order = document["accounts"]
                account = eligible[0]
                for step in range(len(order)):
                    candidate = order[(self.round_robin + step) % len(order)]
                    if candidate in eligible:
                        account = candidate
                        self.round_robin = (self.round_robin + step + 1) % len(order)
                        break
            self.active.add(account["id"])
            return account

    def release_account(self, account):
        with self.condition:
            self.active.discard(account["id"])
            self.condition.notify_all()

    def check_dispatch(self, account):
        if worker_revision() != LOADED_WORKER_REVISION:
            raise _RouterUpdated()
        current = next((a for a in load_store()["accounts"] if a["id"] == account["id"]), None)
        if current is None or not current.get("enabled", True):
            raise _AccountSelectionChanged()

    def record_success(self, document, account):
        with self.lock:
            self.served_total += 1
            self.last_served = account["id"]
            self.cooldowns.pop(account["id"], None)
            self.consecutive_failures = 0
        def edit(current):
            current["served"] = current.get("served", 0) + 1
            current["lastUsedAt"] = int(time.time())
            if current.get("status") == "cooldown":
                current.update(status="ready", statusDetail="", cooldownUntil=0)
        write_state("running", update_account(account["id"], edit))

    def record_cooldown(self, document, account, seconds):
        until = time.time() + seconds
        with self.condition:
            self.cooldowns[account["id"]] = until
            self.pool_cooldown_until = max(self.pool_cooldown_until, until)
            self.condition.notify_all()
        def edit(current):
            current["poolCooldownUntil"] = max(current.get("poolCooldownUntil", 0), until)
            for item in current["accounts"]:
                if item["id"] == account["id"]:
                    item.update(status="cooldown", statusDetail="quota, retry in %ds" % math.ceil(seconds),
                                cooldownUntil=until)
        write_state("running", update_store(edit))

    def record_failure(self, status):
        if status not in (408, 500, 502, 503, 504):
            return
        with self.condition:
            self.consecutive_failures += 1
            if self.consecutive_failures < CIRCUIT_FAILURE_LIMIT:
                return
            until = time.time() + CIRCUIT_COOLDOWN_SECONDS
            self.pool_cooldown_until = max(self.pool_cooldown_until, until)
            self.condition.notify_all()
        write_state("running", update_store(lambda current: current.update(
            poolCooldownUntil=max(current.get("poolCooldownUntil", 0), until))))

    def record_error(self, document, account, detail):
        write_state("running", update_account(account["id"], lambda current: current.update(
            status="error", statusDetail=detail[:110])))

    def synthetic_project(self):
        adjective = random.choice(("useful", "bright", "swift", "calm", "bold"))
        noun = random.choice(("fuze", "wave", "spark", "flow", "core"))
        return "%s-%s-%s" % (adjective, noun, uuid.uuid4().hex[:5])

    def serve_request(self, model, payload, request_id=None):
        deadline = time.monotonic() + REQUEST_DEADLINE_SECONDS
        key = hashlib.sha256(json.dumps([model, payload], sort_keys=True,
                                      separators=(",", ":"), allow_nan=False).encode("utf-8")).digest()
        self.activity.update(request_id, "deduplicating")
        with self.condition:
            now = time.monotonic()
            for old_key, item in list(self.duplicates.items()):
                if item["result"] is not None and item["expires"] <= now:
                    del self.duplicates[old_key]
            entry = self.duplicates.get(key)
            if entry is not None:
                while entry["result"] is None:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        return pool_error(504, "Timed out waiting for the matching pool request.")
                    self.condition.wait(remaining)
                return entry["status"], json.loads(entry["result"])
            if len(self.duplicates) >= MAX_PENDING_REQUESTS:
                completed = next((k for k, v in self.duplicates.items() if v["result"] is not None), None)
                if completed is None:
                    return pool_error(503, "The pool request queue is full. Wait for current work to finish.")
                del self.duplicates[completed]
            entry = {"result": None, "expires": 0}
            self.duplicates[key] = entry
        result = pool_error(500, "The pool request failed.")
        try:
            result = self.serve_unique(model, payload, request_id, deadline)
            return result
        finally:
            encoded = json.dumps(result[1]).encode("utf-8")
            with self.condition:
                entry.update(status=result[0], result=encoded,
                             expires=time.monotonic() + DUPLICATE_TTL_SECONDS)
                # Keep only bounded, short-lived results; never retain prompts
                # or audio in activity metadata or on disk.
                size = sum(len(v["result"] or b"") for v in self.duplicates.values())
                for old_key, item in list(self.duplicates.items()):
                    if size <= DUPLICATE_CACHE_BYTES:
                        break
                    if item["result"] is not None:
                        size -= len(item["result"])
                        del self.duplicates[old_key]
                self.condition.notify_all()

    def serve_unique(self, model, payload, request_id, deadline):
        tried = set()
        last = pool_error(503, "No pool account is ready for this model.")
        self.activity.update(request_id, "queued")
        while time.monotonic() < deadline:
            document = load_store()
            with self.lock:
                self.pool_cooldown_until = max(self.pool_cooldown_until,
                                              document.get("poolCooldownUntil", 0))
                remaining = self.pool_cooldown_until - time.time()
            if remaining > 0:
                return pool_error(429, "Pool protection is cooling down. Wait before retrying.", remaining)
            account = self.pick_account(document, tried, model)
            if account is None:
                with self.condition:
                    eligible = self.eligible(document, tried, model)
                    if eligible and (len(self.active) >= MAX_ACTIVE_REQUESTS or
                                     any(a["id"] in self.active for a in eligible)):
                        self.condition.wait(min(0.5, max(0.01, deadline - time.monotonic())))
                        continue
                return last
            self.activity.update(request_id, "connecting", account["id"])
            try:
                last = self.serve_account(document, account, model, payload, request_id, deadline)
                if last[0] not in (408, 500, 502, 503, 504):
                    return last
                tried.add(account["id"])
            except _AccountSelectionChanged:
                self.activity.update(request_id, "queued", "")
                continue
            except _RouterUpdated:
                return pool_error(503, "The pool router was updated. Wait for it to restart, then retry.")
            finally:
                self.release_account(account)
            self.activity.update(request_id, "rotating", "")
            current = load_store()
            with self.lock:
                exhausted = not self.eligible(current, tried, model)
            if exhausted:
                self.record_failure(last[0])
                return last
        return pool_error(504, "The pool request deadline expired while waiting for an account.")

    def wait_for_turn(self, account, deadline, request_id):
        self.activity.update(request_id, "pacing", account["id"])
        with self.condition:
            while time.monotonic() < deadline:
                self.check_dispatch(account)
                remaining = self.pool_cooldown_until - time.time()
                if remaining > 0:
                    return pool_error(429, "Pool protection is cooling down. Wait before retrying.", remaining)
                now = time.monotonic()
                delay = max(self.next_global_send, self.next_account_send.get(account["id"], 0)) - now
                if delay <= 0:
                    self.next_global_send = now + GLOBAL_REQUEST_INTERVAL_SECONDS
                    self.next_account_send[account["id"]] = now + ACCOUNT_REQUEST_INTERVAL_SECONDS
                    return None
                self.condition.wait(min(delay, deadline - now, 0.5))
        return pool_error(504, "The pool request deadline expired while pacing requests.")

    def serve_account(self, document, account, model, payload, request_id=None, deadline=None):
        deadline = deadline if deadline is not None else time.monotonic() + REQUEST_DEADLINE_SECONDS
        self.check_dispatch(account)
        try:
            token = refresh_access_token(account)
        except RuntimeError:
            self.record_error(document, account, "Google session expired. Sign in again.")
            return pool_error(401, "A pool account is no longer authorized. Sign in again.")
        body = {
            "project": account.get("projectId") or self.synthetic_project(),
            "model": model, "userAgent": "antigravity",
            "requestId": "agent/%s/%d/%s/%d" % (
                uuid.uuid4(), int(time.time() * 1000), uuid.uuid4(),
                max(1, len(payload.get("contents", [])) * 2 - 1)),
            "request": dict(payload, sessionId="%s%d" % (uuid.uuid4(), int(time.time() * 1000))),
        }
        last_status, last_message = 503, "No pool account is ready."
        for attempt in range(1, RETRYABLE_ATTEMPTS + 1):
            stopped = self.wait_for_turn(account, deadline, request_id)
            if stopped:
                return stopped
            try:
                request = urllib.request.Request(
                    CHAT_ORIGIN + "/v1internal:generateContent",
                    data=json.dumps(body).encode("utf-8"), method="POST")
                request.add_header("Content-Type", "application/json")
                request.add_header("User-Agent", ANTIGRAVITY_USER_AGENT)
                request.add_header("Authorization", "Bearer %s" % token)
                self.activity.update(request_id, "sending", account["id"])
                self.check_dispatch(account)
                with urllib.request.urlopen(
                        request, timeout=min(UPSTREAM_TIMEOUT_SECONDS, max(0.1, deadline - time.monotonic()))) as response:
                    raw = response.read(RESPONSE_BODY_CAP + 1)
                    if len(raw) > RESPONSE_BODY_CAP:
                        return pool_error(502, "Google returned a response larger than the pool limit.")
                    result = json.loads(raw.decode("utf-8"))
                if not isinstance(result, dict):
                    return pool_error(502, "Google returned an invalid response.")
                result = result.get("response", result)
                if not isinstance(result, dict):
                    return pool_error(502, "Google returned an invalid response.")
                reason, permanent = generation_rejection(result)
                if reason:
                    if permanent:
                        return pool_error(422, "Google declined this content (%s). Review the source and request before retrying." % reason)
                    return pool_error(503, "Google returned no answer (%s). Waiting to retry with a free enabled account." % reason)
                self.record_success(document, account)
                return 200, result
            except urllib.error.HTTPError as error:
                raw_detail = error.read(RESPONSE_BODY_CAP).decode("utf-8", errors="replace")
                try:
                    detail = json.loads(raw_detail).get("error", {}).get("message", "")
                except (ValueError, AttributeError):
                    detail = ""
                status = error.code
                if status == 401 and attempt == 1:
                    _TOKEN_CACHE.pop(account["id"], None)
                    self.check_dispatch(account)
                    try:
                        token = refresh_access_token(account)
                    except RuntimeError:
                        self.record_error(document, account, "Google session expired. Sign in again.")
                        return pool_error(401, "A pool account is no longer authorized. Sign in again.")
                    continue  # forced refresh, same account
                if status == 403:
                    try:
                        failure = GoogleRequestError(status, json.loads(raw_detail).get("error", {}))
                    except (ValueError, AttributeError):
                        failure = GoogleRequestError(status, {"message": detail})
                    if failure.verification_required:
                        changes = verification_failure(failure)
                        write_state("running", update_account(account["id"], lambda current: current.update(changes)))
                        return pool_error(403, changes["statusDetail"])
                    elif account.get("issuer") == "gemini-cli":
                        action = "old sign-in — remove this account and add it again"
                    else:
                        action = "HTTP 403: " + (detail or "Google rejected this account")
                    self.record_error(document, account, action)
                    last_status, last_message = 403, action
                    break
                if status == 401:
                    self.record_error(document, account, "Google session expired — remove this account and add it again")
                    last_status, last_message = 401, "A pool account is no longer authorized. Sign in again."
                    break
                if status == 429:
                    retry_after = error.headers.get("retry-after") if error.headers else None
                    seconds = retry_after_seconds(retry_after)
                    self.record_cooldown(document, account, seconds)
                    return pool_error(429, "Google quota was reached. The pool is cooling down.", seconds)
                if status in (408, 500, 502, 503, 504) and attempt < RETRYABLE_ATTEMPTS:
                    self.activity.update(request_id, "retrying", account["id"])
                    time.sleep(min(RETRY_BACKOFF_SECONDS * (2 ** (attempt - 1)), max(0, deadline - time.monotonic())))
                    continue
                last_status, last_message = status, "HTTP %d" % status
                break
            except (urllib.error.URLError, OSError, http.client.HTTPException, ValueError):
                if attempt >= RETRYABLE_ATTEMPTS:
                    last_status, last_message = 502, "The pool could not reach Google."
                    break
                self.activity.update(request_id, "retrying", account["id"])
                time.sleep(min(RETRY_BACKOFF_SECONDS * (2 ** (attempt - 1)), max(0, deadline - time.monotonic())))
        return pool_error(last_status, last_message)


CORE = PoolCore()


class _CoreHandler(BaseHTTPRequestHandler):
    def setup(self):
        self.request.settimeout(10.0)
        super().setup()

    def log_message(self, *args):
        pass

    def _send(self, status, value):
        if status >= 400 and isinstance(value.get("error"), dict):
            value["error"]["retryable"] = False
        body = json.dumps(value).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        retry_after = value.get("error", {}).get("retryAfterSeconds")
        if retry_after:
            self.send_header("Retry-After", str(retry_after))
        self.end_headers()
        try:
            self.wfile.write(body)
        except OSError:
            # A cancelled desktop job can close its socket after headers.
            # Request/account cleanup has already completed independently.
            self.close_connection = True

    def do_GET(self):
        if urllib.parse.urlparse(self.path).path.rstrip("/") == "/health":
            with CORE.lock:
                health = {"ok": worker_revision() == LOADED_WORKER_REVISION,
                          "routerProtocol": ROUTER_PROTOCOL_VERSION,
                          "workerRevision": LOADED_WORKER_REVISION, "pid": os.getpid(),
                          "served": CORE.served_total,
                          "inFlight": CORE.in_flight, "busyAccounts": list(CORE.active)}
            health.update(CORE.activity.snapshot())
            health["protection"] = CORE.protection_state()
            self._send(200, health)
            return
        self._send(404, {"error": {"code": 404, "message": "Unknown pool route."}})

    def do_POST(self):
        match = re.match(r"^/v1beta/models/([A-Za-z0-9._-]+):generateContent$", self.path)
        if not match:
            self._send(404, {"error": {"code": 404, "message": "Unknown pool route."}})
            return
        # This loopback route is for desktop workers. Cross-origin browser
        # requests must not spend a signed-in account's quota.
        if self.headers.get("Origin") or self.headers.get("Sec-Fetch-Site") == "cross-site":
            self._send(*pool_error(403, "Browser requests are not accepted by the local pool."))
            return
        lengths = self.headers.get_all("Content-Length", [])
        if len(lengths) != 1 or not re.fullmatch(r"[0-9]+", lengths[0]) or self.headers.get("Transfer-Encoding"):
            self._send(*pool_error(400, "A single valid Content-Length is required."))
            return
        if len(lengths[0]) > 10:
            self._send(*pool_error(413, "The request exceeds the pool upload limit."))
            return
        length = int(lengths[0])
        if length > REQUEST_BODY_CAP:
            self._send(*pool_error(413, "The request exceeds the pool upload limit."))
            return
        with CORE.lock:
            admitted = CORE.in_flight < MAX_PENDING_REQUESTS
            if admitted:
                CORE.in_flight += 1
        if not admitted:
            self._send(*pool_error(503, "The pool request queue is full. Wait for current work to finish."))
            return
        request_id = None
        status = 500
        try:
            try:
                raw = self.rfile.read(length)
                payload = json.loads(raw.decode("utf-8"), parse_constant=reject_json_constant)
                if len(raw) != length or not isinstance(payload, dict) or not isinstance(payload.get("contents"), list):
                    raise ValueError("payload")
            except (ValueError, OSError):
                status, result = pool_error(400, "The request does not look like a generateContent call.")
                self._send(status, result)
                return
            request_id = CORE.activity.begin(match.group(1))
            status, result = CORE.serve_request(match.group(1), payload, request_id)
        except Exception:
            result = {"error": {"code": 500, "message": "The pool request failed. Try again."}}
        finally:
            with CORE.lock:
                CORE.in_flight -= 1
            CORE.activity.finish(request_id, status)
        self._send(status, result)


def command_state():
    document = load_store()
    health = {}
    try:
        # Local Core checks must bypass OS/environment proxies. A proxy outage
        # says nothing about whether the service on this machine is running.
        local = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        with local.open("http://127.0.0.1:%d/health" % POOL_PORT, timeout=1.5) as response:
            health = json.loads(response.read().decode("utf-8"))
    except Exception:
        health = {}
    if not compatible_router(health):
        health = {}
    accounts = public_accounts(document)
    for account in accounts:
        account["inFlight"] = int(account["id"] in health.get("busyAccounts", []))
    emit({
        "core": "running" if health.get("ok") else "stopped",
        "endpoint": "127.0.0.1:%d" % POOL_PORT,
        "strategy": document.get("strategy", "round-robin"),
        "accounts": accounts,
        "readyCount": sum(1 for a in accounts if a["enabled"] and a["status"] == "ready"),
        "inFlight": int(health.get("inFlight") or 0),
        "servedTotal": max(sum(a["served"] for a in accounts), int(health.get("served") or 0)),
        "activityRevision": int(health.get("activityRevision") or 0),
        "activitySession": health.get("activitySession", ""),
        "activeRequests": health.get("activeRequests", []),
        "recentRequests": health.get("recentRequests", []),
        "protection": health.get("protection", {}),
    })
    return 0


def command_remove(account_id):
    def remove(document):
        before = len(document["accounts"])
        document["accounts"] = [a for a in document["accounts"] if a["id"] != account_id]
        if len(document["accounts"]) == before:
            raise RuntimeError("Unknown account.")
    document = update_store(remove)
    write_state("running", document)
    emit({"removed": True, "id": account_id})
    return 0


def command_set_enabled(account_id, enabled):
    def edit(document):
        account = next((a for a in document["accounts"] if a["id"] == account_id), None)
        if account is None:
            raise RuntimeError("Unknown account.")
        account["enabled"] = enabled
    document = update_store(edit)
    write_state("running", document)
    emit({"updated": True, "id": account_id, "enabled": enabled})
    return 0


def command_set_strategy(name):
    if name not in STRATEGIES:
        raise RuntimeError("Unknown strategy.")
    document = update_store(lambda current: current.update(strategy=name))
    write_state("running", document)
    emit({"strategy": name})
    return 0


def model_ids(payload):
    result = set()
    models = payload.get("models", {}) if isinstance(payload, dict) else {}
    entries = list(models.items()) if isinstance(models, dict) else enumerate(models) if isinstance(models, list) else []
    for key, value in entries:
        if isinstance(value, dict):
            if value.get("disabled") or value.get("isDisabled") or value.get("isInternal") or value.get("enabled") is False:
                continue
            # The shared picker must also accept the MP3 transcription upload.
            mime_types = value.get("supportedMimeTypes")
            if isinstance(mime_types, dict) and not mime_types.get("audio/mpeg"):
                continue
        candidates = [key, value] if isinstance(value, str) else [key]
        if isinstance(value, dict):
            candidates += [value.get("model"), value.get("id"), value.get("name")]
        for candidate in candidates:
            if not isinstance(candidate, str):
                continue
            candidate = re.sub(r"\([^()]*\)\s*$", "", candidate.removeprefix("models/")).strip().lower()
            if re.fullmatch(r"gemini-[a-z0-9][a-z0-9._-]{0,139}", candidate):
                result.add(candidate)
    return sorted(result)


def account_models(account):
    """Read the live catalogue with the same identity as the Electron pool."""
    if account.get("issuer") == "gemini-cli":
        raise RuntimeError("old sign-in — remove this account and add it again")
    token = refresh_access_token(account)
    project = account.get("projectId")
    if not project:
        _, project = detect_profile(token)
    payload = http_json(CHAT_ORIGIN + "/v1internal:fetchAvailableModels",
                        {"project": project or CORE.synthetic_project()}, {
                            "Authorization": "Bearer %s" % token,
                            "Content-Type": "application/json",
                            "User-Agent": ANTIGRAVITY_USER_AGENT,
                        }, timeout=20)
    models = model_ids(payload)
    if not models:
        raise RuntimeError("Google returned no Gemini models for this account.")
    return models, project


def command_models():
    accounts = load_store()["accounts"]
    if not accounts:
        raise RuntimeError("Add a Google account before checking pool models.")
    found, failures, supported = set(), [], {}
    completed = 0
    with ThreadPoolExecutor(max_workers=min(4, len(accounts))) as executor:
        futures = {executor.submit(account_models, a): a for a in accounts}
        for future in as_completed(futures):
            account = futures[future]
            try:
                models, project = future.result()
                changes = {"availableModels": models, "modelsCheckedAt": int(time.time()), "modelError": ""}
                if project:
                    changes["projectId"] = project
                update_account(account["id"], lambda current: current.update(changes))
                if account.get("enabled", True):
                    for model in models:
                        found.add(model)
                        supported.setdefault(model, []).append(account["id"])
            except Exception as error:
                detail = str(error)
                needs_verification = isinstance(error, GoogleRequestError) and error.verification_required
                if needs_verification:
                    detail = verification_failure(error)["statusDetail"]
                elif account.get("issuer") == "gemini-cli" or "permission" in detail.lower():
                    detail = "old sign-in — remove this account and add it again"
                changes = {"modelError": detail[:200]}
                if needs_verification:
                    changes.update(verification_failure(error))
                elif "(401)" in detail or "(403)" in detail or "old sign-in" in detail or "accounts.google.com" in detail:
                    changes.update(status="error", statusDetail=detail[:110])
                update_account(account["id"], lambda current: current.update(changes))
                failures.append({"id": account["id"], "email": account.get("email", ""), "message": detail[:200]})
            completed += 1
            emit({"modelCheck": True, "completed": completed, "total": len(accounts)})
    emit({"checked": True, "models": sorted(found), "modelAccounts": supported,
          "checkedCount": len(accounts) - len(failures), "accountCount": len(accounts),
          "errors": failures, "accounts": public_accounts(load_store())})
    return 0


def verify_account_access(account, token=None):
    try:
        if account.get("issuer") == "gemini-cli":
            raise RuntimeError("Old sign-in: remove this account and add it again.")
        token = token or refresh_access_token(account)
        tier, project = detect_profile(token)
        models, _ = account_models(dict(account, projectId=project))
        model = next((name for name in ("gemini-2.5-flash-lite", "gemini-2.5-flash", "gemini-3-flash")
                      if name in models), models[0])
        # OAuth and the model catalogue can work before generation is allowed.
        # One tiny text probe checks service access without uploading user media.
        result = http_json(CHAT_ORIGIN + "/v1internal:generateContent", {
            "project": project, "model": model, "userAgent": "antigravity",
            "requestId": "agent/%s" % uuid.uuid4(),
            "request": {"contents": [{"role": "user", "parts": [{"text": "Reply with OK."}]}],
                        "generationConfig": {"maxOutputTokens": 8, "temperature": 0}},
        }, {"Authorization": "Bearer %s" % token, "Content-Type": "application/json",
            "User-Agent": ANTIGRAVITY_USER_AGENT}, timeout=20)
        reply = result.get("response", result) if isinstance(result, dict) else None
        if not isinstance(reply, dict) or not reply.get("candidates"):
            raise RuntimeError("Google returned no generation result. Recheck Antigravity access.")
        return {"tier": tier, "projectId": project, "availableModels": models,
                "modelsCheckedAt": int(time.time()), "modelError": "", "status": "ready",
                "statusDetail": "", "verificationUrl": "", "cooldownUntil": 0,
                "lastVerifiedAt": int(time.time())}
    except Exception as error:
        return verification_failure(error)


def command_verify(account_id=None):
    document = load_store()
    accounts = [a for a in document["accounts"] if account_id is None or a["id"] == account_id]
    if account_id is not None and not accounts:
        raise RuntimeError("Unknown account.")
    for account in accounts:
        changes = verify_account_access(account)
        update_account(account["id"], lambda current: current.update(changes))
    document = load_store()
    write_state("running", document)
    emit({"verified": True, "accounts": public_accounts(document)})
    return 0


class _BusyHandler(_CoreHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(0.1)

    def do_POST(self):
        # Drain a promptly delivered body before closing. Closing a Windows
        # socket with unread data resets it and hides the terminal 503, causing
        # clients to mistake overload for a transport fault and retry.
        length = self.headers.get("Content-Length", "0")
        if re.fullmatch(r"[0-9]{1,10}", length):
            self.rfile.read(min(int(length), REQUEST_BODY_CAP))
        self._send(*pool_error(503, "The local pool is busy. Wait for current work to finish."))

    def do_GET(self):
        self._send(*pool_error(503, "The local pool is busy. Wait for current work to finish."))


class _CoreServer(ThreadingHTTPServer):
    # Windows SO_REUSEADDR permits two servers to bind the same local port.
    # The rotation Core needs a single owner, so never enable address sharing.
    allow_reuse_address = False

    def __init__(self, *args, **kwargs):
        self._connections = threading.BoundedSemaphore(MAX_PENDING_REQUESTS + 8)
        super().__init__(*args, **kwargs)

    def process_request(self, request, client_address):
        if not self._connections.acquire(blocking=False):
            try:
                _BusyHandler(request, client_address, self)
            except OSError:
                pass
            finally:
                self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except Exception:
            self._connections.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self._connections.release()

    def server_bind(self):
        if os.name == "nt":
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        super().server_bind()


def command_serve():
    document = load_store()
    ready = [account for account in document["accounts"]
             if account.get("enabled", True) and account.get("status") == "ready"]
    if not ready:
        emit({"error": "No ready pool account. Add and verify one first."})
        return 4
    try:
        server = _CoreServer(("127.0.0.1", POOL_PORT), _CoreHandler)
    except OSError:
        # A busy port is only another Core if its health route confirms it.
        # Otherwise return a failure so the manager stops retrying in a loop.
        try:
            local = urllib.request.build_opener(urllib.request.ProxyHandler({}))
            with local.open("http://127.0.0.1:%d/health" % POOL_PORT, timeout=1.5) as response:
                health = json.loads(response.read().decode("utf-8"))
            if compatible_router(health):
                emit({"serving": True, "alreadyRunning": True})
                return 0
        except Exception:
            pass
        emit({"error": "Pool Core could not listen on 127.0.0.1:%d. "
                       "The port may be in use by another app." % POOL_PORT})
        return 4
    write_state("running", document)
    note("pool: core listening on 127.0.0.1:%d" % POOL_PORT)
    CORE.served_baseline = sum(a.get("served", 0) for a in document["accounts"])
    CORE.pool_cooldown_until = document.get("poolCooldownUntil", 0)
    stopped = threading.Event()
    watcher = threading.Thread(target=watch_worker_revision, args=(server, stopped), daemon=True)
    watcher.start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stopped.set()
        server.server_close()
        watcher.join(timeout=2)
    write_state("stopped", load_store())
    return 0


def main():
    if len(sys.argv) < 2:
        emit({"error": "usage: pool_tool.py state|add|remove ID|set-enabled ID 0|1|verify|models|"
                       "set-strategy NAME|serve"})
        return 2
    command = sys.argv[1]
    try:
        if command == "state":
            return command_state()
        if command == "add":
            return sign_in()
        if command == "remove" and len(sys.argv) > 2:
            return command_remove(sys.argv[2])
        if command == "set-enabled" and len(sys.argv) == 4 and sys.argv[3] in ("0", "1"):
            return command_set_enabled(sys.argv[2], sys.argv[3] == "1")
        if command == "verify" and len(sys.argv) in (2, 3):
            return command_verify(sys.argv[2] if len(sys.argv) == 3 else None)
        if command == "models":
            return command_models()
        if command == "set-strategy" and len(sys.argv) > 2:
            return command_set_strategy(sys.argv[2])
        if command == "serve":
            return command_serve()
        emit({"error": "Unknown pool command."})
        return 2
    except Exception as error:
        emit({"error": str(error)})
        return 4


if __name__ == "__main__":
    sys.exit(main())
