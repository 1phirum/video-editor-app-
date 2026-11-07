"""Exercise shared pool limits against a real local HTTP provider, never Google."""
from contextlib import contextmanager
import concurrent.futures
import copy
from email.utils import formatdate
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import http.client
import json
import os
from pathlib import Path
import socket
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, os.environ.get("CUTPRO_POOL_TEST_TOOLS",
                                str(Path(__file__).resolve().parents[1] / "tools")))
import pool_tool
import gemini_transcribe as transcription
import translate_transcript as translation


@contextmanager
def fixture(status=200, result=None, retry_after=None):
    core = pool_tool.PoolCore()
    document = {"strategy": "round-robin", "accounts": [
        {"id": str(i), "status": "ready", "served": 0, "projectId": "fixture-project"}
        for i in range(3)]}
    calls, active, maximum = [], 0, 0
    lock = threading.Lock()

    class Provider(BaseHTTPRequestHandler):
        def log_message(self, *args): pass

        def do_POST(self):
            nonlocal active, maximum
            body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            with lock:
                calls.append((time.monotonic(), self.headers["Authorization"], body))
                active += 1
                maximum = max(maximum, active)
            try:
                time.sleep(0.06)
                reply = (result(self.headers["Authorization"], body) if callable(result) else result) \
                    if result is not None else {"response": {"candidates": []}}
                data = json.dumps(reply).encode()
                self.send_response(status(self.headers["Authorization"], body) if callable(status) else status)
                if retry_after is not None:
                    self.send_header("Retry-After", retry_after)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
            finally:
                with lock:
                    active -= 1

    provider = ThreadingHTTPServer(("127.0.0.1", 0), Provider)
    router = pool_tool._CoreServer(("127.0.0.1", 0), pool_tool._CoreHandler)
    threads = [threading.Thread(target=s.serve_forever, daemon=True) for s in (provider, router)]

    def update(operation):
        with lock:
            operation(document)
            return copy.deepcopy(document)

    def update_account(account_id, operation):
        return update(lambda current: operation(next(a for a in current["accounts"] if a["id"] == account_id)))

    def load():
        with lock:
            return copy.deepcopy(document)

    with patch.object(pool_tool, "CORE", core), \
         patch.object(pool_tool, "CHAT_ORIGIN", "http://%s:%d" % provider.server_address), \
         patch.object(pool_tool, "load_store", side_effect=load), \
         patch.object(pool_tool, "update_store", side_effect=update), \
         patch.object(pool_tool, "update_account", side_effect=update_account), \
         patch.object(pool_tool, "write_state"), patch.object(pool_tool, "emit"), \
         patch.object(pool_tool, "refresh_access_token", side_effect=lambda a: "fixture-" + a["id"]), \
         patch.object(pool_tool, "GLOBAL_REQUEST_INTERVAL_SECONDS", 0.04, create=True), \
         patch.object(pool_tool, "ACCOUNT_REQUEST_INTERVAL_SECONDS", 0.08, create=True), \
         patch.object(pool_tool, "RETRY_BACKOFF_SECONDS", 0.01):
        for thread in threads:
            thread.start()
        try:
            yield core, document, calls, router.server_address, lambda: maximum
        finally:
            for server in (router, provider):
                server.shutdown()
                server.server_close()
            for thread in threads:
                thread.join(timeout=2)


def payload(text="fixture"):
    return {"contents": [{"parts": [{"text": text}]}]}


def post(address, body=None, headers=None):
    client = http.client.HTTPConnection(*address, timeout=5)
    try:
        client.request("POST", "/v1beta/models/gemini-test:generateContent",
                       json.dumps(payload()) if body is None else body, headers=headers or {})
        response = client.getresponse()
        return response.status, json.loads(response.read()), response.getheader("Retry-After")
    finally:
        client.close()


class PoolProtectionTests(unittest.TestCase):
    def test_other_response_rotates_to_enabled_account_and_can_succeed(self):
        def reply(account, body):
            return {"response": {"promptFeedback": {"blockReason": "OTHER"}, "candidates": []}} \
                if account == "Bearer fixture-0" else {"response": {"candidates": []}}
        with fixture(result=reply) as (core, document, calls, address, maximum):
            document["accounts"][2]["enabled"] = False
            self.assertEqual(post(address)[0], 200)
            self.assertEqual([call[1] for call in calls], ["Bearer fixture-0", "Bearer fixture-1"])
            self.assertEqual(core.served_total, 1)
            self.assertEqual(core.active, set())

    def test_disabled_accounts_are_excluded_from_every_routing_strategy(self):
        for strategy in ("round-robin", "least-used", "random"):
            with self.subTest(strategy=strategy), fixture() as (core, document, calls, address, maximum):
                document["strategy"] = strategy
                self.assertEqual(pool_tool.command_set_enabled("0", False), 0)
                for i in range(3):
                    self.assertEqual(post(address, json.dumps(payload(str(i))))[0], 200)
                self.assertTrue(all(call[1] != "Bearer fixture-0" for call in calls))
                pool_tool.command_set_enabled("0", True)
                pool_tool.command_set_enabled("1", False)
                pool_tool.command_set_enabled("2", False)
                self.assertEqual(post(address, json.dumps(payload("enabled again")))[0], 200)
                self.assertEqual(calls[-1][1], "Bearer fixture-0")

    def test_all_disabled_accounts_do_not_send_any_requests(self):
        with fixture() as (core, document, calls, address, maximum):
            for account in document["accounts"]:
                pool_tool.command_set_enabled(account["id"], False)
            self.assertEqual(post(address)[0], 503)
            self.assertEqual(calls, [])
            self.assertEqual(core.active, set())

    def test_disabling_an_assigned_account_reroutes_before_upload(self):
        with fixture() as (core, document, calls, address, maximum):
            assigned, release = threading.Event(), threading.Event()
            serve = core.serve_account
            def held(*args):
                assigned.set()
                if not release.wait(3):
                    raise TimeoutError("fixture assignment timeout")
                return serve(*args)
            with patch.object(core, "serve_account", side_effect=held), \
                    concurrent.futures.ThreadPoolExecutor(max_workers=1) as executor:
                pending = executor.submit(post, address)
                try:
                    self.assertTrue(assigned.wait(3))
                    pool_tool.command_set_enabled("0", False)
                finally:
                    release.set()
                self.assertEqual(pending.result(timeout=3)[0], 200)
            self.assertEqual(len(calls), 1)
            self.assertNotEqual(calls[0][1], "Bearer fixture-0")
            self.assertEqual(post(address, json.dumps(payload("next request")))[0], 200)
            self.assertNotEqual(calls[-1][1], "Bearer fixture-0")

    def test_removing_an_assigned_account_reroutes_before_upload(self):
        with fixture() as (core, document, calls, address, maximum):
            serve = core.serve_account
            def remove_then_serve(*args):
                if args[1]["id"] == "0":
                    pool_tool.command_remove("0")
                return serve(*args)
            with patch.object(core, "serve_account", side_effect=remove_then_serve):
                self.assertEqual(post(address)[0], 200)
            self.assertEqual(len(calls), 1)
            self.assertNotEqual(calls[0][1], "Bearer fixture-0")
            self.assertEqual(core.active, set())

    def test_disable_during_token_refresh_prevents_upload(self):
        with fixture() as (core, document, calls, address, maximum):
            def refresh(account):
                if account["id"] == "0":
                    pool_tool.command_set_enabled("0", False)
                return "fixture-" + account["id"]
            with patch.object(pool_tool, "refresh_access_token", side_effect=refresh):
                self.assertEqual(post(address)[0], 200)
            self.assertEqual(len(calls), 1)
            self.assertNotEqual(calls[0][1], "Bearer fixture-0")

    def test_disable_before_retry_reroutes_without_another_disabled_upload(self):
        with fixture() as (core, document, calls, address, maximum):
            send = pool_tool.urllib.request.urlopen
            rejected = []
            def reject_once(request, *args, **kwargs):
                if request.get_header("Authorization") == "Bearer fixture-0":
                    rejected.append(request)
                    pool_tool.command_set_enabled("0", False)
                    raise pool_tool.urllib.error.HTTPError(request.full_url, 503, "fixture", {}, None)
                return send(request, *args, **kwargs)
            with patch.object(pool_tool.urllib.request, "urlopen", side_effect=reject_once):
                self.assertEqual(post(address)[0], 200)
            self.assertEqual(len(rejected), 1)
            self.assertEqual(len(calls), 1)
            self.assertNotEqual(calls[0][1], "Bearer fixture-0")
            self.assertEqual(core.active, set())

    def test_disabling_after_upload_allows_response_to_finish(self):
        with fixture() as (core, document, calls, address, maximum):
            send = pool_tool.urllib.request.urlopen
            def disable_after_response(request, *args, **kwargs):
                response = send(request, *args, **kwargs)
                pool_tool.command_set_enabled("0", False)
                return response
            with patch.object(pool_tool.urllib.request, "urlopen", side_effect=disable_after_response):
                self.assertEqual(post(address)[0], 200)
            self.assertEqual(len(calls), 1)
            self.assertEqual(calls[0][1], "Bearer fixture-0")
            self.assertFalse(document["accounts"][0]["enabled"])

    def test_router_revision_change_stops_dispatch(self):
        with fixture() as (core, document, calls, address, maximum), \
                patch.object(pool_tool, "worker_revision", return_value="updated"):
            self.assertEqual(post(address)[0], 503)
            self.assertEqual(calls, [])
            self.assertEqual(core.active, set())
            self.assertEqual(core.in_flight, 0)

    def test_health_requires_current_routing_protocol(self):
        with fixture() as (core, document, calls, address, maximum):
            client = http.client.HTTPConnection(*address, timeout=3)
            try:
                client.request("GET", "/health")
                health = json.loads(client.getresponse().read())
            finally:
                client.close()
            self.assertEqual(health["routerProtocol"], pool_tool.ROUTER_PROTOCOL_VERSION)
            self.assertEqual(health["workerRevision"], pool_tool.LOADED_WORKER_REVISION)
            self.assertEqual(health["pid"], os.getpid())
            self.assertTrue(pool_tool.compatible_router(health))
            self.assertFalse(pool_tool.compatible_router({"ok": True}))
            self.assertFalse(pool_tool.compatible_router(dict(health, routerProtocol=0)))
            self.assertFalse(pool_tool.compatible_router(dict(health, ok=False)))
            with patch.object(pool_tool, "POOL_PORT", address[1]), patch.object(pool_tool, "emit") as emit:
                pool_tool.command_state()
                self.assertEqual(emit.call_args.args[0]["core"], "running")
                with patch.object(pool_tool._CoreHandler, "do_GET",
                                  lambda handler: handler._send(200, {"ok": True})):
                    pool_tool.command_state()
                    self.assertEqual(emit.call_args.args[0]["core"], "stopped")
                    self.assertEqual(pool_tool.command_serve(), 4)

    def test_model_picker_only_includes_enabled_accounts(self):
        with fixture() as (core, document, calls, address, maximum), \
                patch.object(pool_tool, "account_models", side_effect=lambda a: (["gemini-" + a["id"]], None)), \
                patch.object(pool_tool, "emit") as emit:
            pool_tool.command_set_enabled("0", False)
            self.assertEqual(pool_tool.command_models(), 0)
            result = emit.call_args.args[0]
            self.assertEqual(result["models"], ["gemini-1", "gemini-2"])
            self.assertEqual(result["checkedCount"], 3)
            self.assertEqual(document["accounts"][0]["availableModels"], ["gemini-0"])
            self.assertFalse(document["accounts"][0]["enabled"])

    def test_duplicate_http_requests_share_one_send_and_cached_result(self):
        with fixture() as (core, document, calls, address, maximum):
            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
                results = list(executor.map(lambda _: post(address), range(8)))
            self.assertTrue(all(item[0] == 200 for item in results))
            self.assertEqual(len(calls), 1)
            self.assertEqual(post(address)[0], 200)
            self.assertEqual(len(calls), 1)
            self.assertEqual(core.in_flight, 0)
            self.assertEqual(core.active, set())
            self.assertEqual(core.served_total, 1)
            self.assertNotIn("fixture", json.dumps(core.activity.snapshot()))

    def test_unique_requests_obey_pool_account_and_concurrency_limits(self):
        with fixture() as (core, document, calls, address, maximum):
            with concurrent.futures.ThreadPoolExecutor(max_workers=10) as executor:
                results = list(executor.map(lambda i: post(address, json.dumps(payload(str(i)))), range(10)))
            self.assertTrue(all(item[0] == 200 for item in results))
            self.assertEqual(len(calls), 10)
            self.assertLessEqual(maximum(), 2)
            self.assertGreaterEqual(maximum(), 2)
            sends = sorted(calls)
            for first, second in zip(sends, sends[1:]):
                self.assertGreaterEqual(second[0] - first[0], 0.03)
            for account in {call[1] for call in calls}:
                stamps = sorted(call[0] for call in calls if call[1] == account)
                for first, second in zip(stamps, stamps[1:]):
                    self.assertGreaterEqual(second - first, 0.07)
            self.assertEqual(core.in_flight, 0)
            self.assertEqual(core.active, set())

    def test_quota_stops_rotation_and_persists_pool_cooldown(self):
        future = formatdate(time.time() + 120, usegmt=True)
        with fixture(status=429, retry_after=future) as (core, document, calls, address, maximum):
            status, body, header = post(address)
            self.assertEqual(status, 429)
            self.assertFalse(body["error"]["retryable"])
            self.assertGreaterEqual(int(header), 118)
            self.assertGreater(document["poolCooldownUntil"], time.time() + 118)
            self.assertEqual(post(address, json.dumps(payload("different")))[0], 429)
            restarted = pool_tool.PoolCore()
            self.assertEqual(restarted.serve_request("gemini-test", payload("after restart"))[0], 429)
            self.assertEqual(len(calls), 1)
            self.assertEqual(core.protection_state()["state"], "cooldown")

    def test_denied_request_does_not_rotate_or_retry(self):
        with fixture(status=403, result={"error": {"message": "fixture permission denied"}}) as (core, document, calls, address, maximum):
            status, body, header = post(address)
            self.assertEqual(status, 403)
            self.assertIn("fixture permission denied", body["error"]["message"])
            self.assertNotIn("old sign-in", body["error"]["message"])
            self.assertEqual(post(address)[0], 403)
            self.assertEqual(len(calls), 1)
            self.assertEqual(document["accounts"][0]["status"], "error")
            self.assertEqual(core.active, set())

    def test_verification_rejection_marks_account_unverified_and_keeps_google_link(self):
        url = "https://accounts.google.com/signin/continue?fixture=verification"
        reply = {"error": {"message": "Please verify your account", "details": [
            {"reason": "VALIDATION_REQUIRED", "metadata": {"validation_url": url}}]}}
        with fixture(status=403, result=reply) as (core, document, calls, address, maximum):
            document["accounts"] = document["accounts"][:1]
            self.assertEqual(post(address)[0], 403)
            self.assertEqual(len(calls), 1)
            self.assertEqual(document["accounts"][0]["status"], "unverified")
            self.assertEqual(document["accounts"][0]["verificationUrl"], url)
            self.assertEqual(core.active, set())

    def test_policy_blocks_are_terminal_even_in_http_success(self):
        for reply in ({"promptFeedback": {"blockReason": "PROHIBITED_CONTENT"}},
                      {"candidates": [{"finishReason": "SAFETY"}]}):
            with self.subTest(reply=reply), fixture(result={"response": reply}) as (core, document, calls, address, maximum):
                status, body, header = post(address)
                self.assertEqual(status, 422)
                self.assertFalse(body["error"]["retryable"])
                self.assertEqual(len(calls), 1)
                self.assertEqual(core.served_total, 0)

    def test_unspecified_block_reason_does_not_reject_valid_response(self):
        reply = {"response": {"promptFeedback": {"blockReason": "BLOCK_REASON_UNSPECIFIED"}, "candidates": []}}
        with fixture(result=reply) as (core, document, calls, address, maximum):
            self.assertEqual(post(address)[0], 200)
            self.assertEqual(core.served_total, 1)

    def test_retry_budget_and_request_identity_stay_on_one_account(self):
        with fixture(status=503) as (core, document, calls, address, maximum):
            document["accounts"] = document["accounts"][:1]
            self.assertEqual(post(address)[0], 503)
            self.assertEqual(len(calls), 3)
            self.assertEqual(len({call[1] for call in calls}), 1)
            self.assertEqual(len({call[2]["requestId"] for call in calls}), 1)
            self.assertEqual(len({call[2]["request"]["sessionId"] for call in calls}), 1)
            self.assertEqual(core.active, set())

    def test_repeated_provider_failure_opens_shared_circuit(self):
        with fixture(status=502) as (core, document, calls, address, maximum):
            document["accounts"] = document["accounts"][:1]
            for i in range(3):
                self.assertEqual(post(address, json.dumps(payload(str(i))))[0], 502)
            self.assertEqual(len(calls), 9)
            self.assertEqual(post(address, json.dumps(payload("fourth")))[0], 429)
            self.assertEqual(len(calls), 9)
            self.assertGreater(document["poolCooldownUntil"], time.time())

    def test_failed_request_waits_for_busy_enabled_account_then_rotates(self):
        def response_status(account, body):
            return 503 if account == "Bearer fixture-0" else 200
        with fixture(status=response_status) as (core, document, calls, address, maximum):
            document["accounts"][2]["enabled"] = False
            core.round_robin = 1
            busy, release, failed = threading.Event(), threading.Event(), threading.Event()
            serve, release_account = core.serve_account, core.release_account
            def held(*args):
                if args[1]["id"] == "1" and not busy.is_set():
                    busy.set()
                    if not release.wait(4):
                        raise TimeoutError("busy fixture not released")
                return serve(*args)
            def freed(account):
                release_account(account)
                if account["id"] == "0":
                    failed.set()
            with patch.object(core, "serve_account", side_effect=held), \
                    patch.object(core, "release_account", side_effect=freed), \
                    concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
                other = executor.submit(post, address, json.dumps(payload("other job")))
                retry = None
                try:
                    self.assertTrue(busy.wait(3))
                    retry = executor.submit(post, address, json.dumps(payload("failed job")))
                    self.assertTrue(failed.wait(3))
                    self.assertFalse(retry.done())
                    self.assertEqual(core.active, {"1"})
                    self.assertEqual(len(calls), 3)
                finally:
                    release.set()
                self.assertEqual(other.result(timeout=3)[0], 200)
                self.assertEqual(retry.result(timeout=3)[0], 200)
            self.assertEqual([call[1] for call in calls], ["Bearer fixture-0"] * 3 + ["Bearer fixture-1"] * 2)
            self.assertEqual(core.active, set())
            self.assertEqual(core.in_flight, 0)

    def test_pacing_deadline_releases_account_without_sending(self):
        with fixture() as (core, document, calls, address, maximum), \
             patch.object(pool_tool, "REQUEST_DEADLINE_SECONDS", 0.05):
            core.next_global_send = time.monotonic() + 10
            self.assertEqual(post(address)[0], 504)
            self.assertEqual(calls, [])
            self.assertEqual(core.active, set())
            self.assertEqual(core.in_flight, 0)

    def test_input_and_queue_guards_do_not_spend_quota(self):
        with fixture() as (core, document, calls, address, maximum):
            for length in ("-1", "bad", "9" * 100):
                with self.subTest(length=length):
                    self.assertIn(post(address, "", {"Content-Length": length})[0], (400, 413))
            with patch.object(pool_tool, "REQUEST_BODY_CAP", 8):
                self.assertEqual(post(address, " " * 9)[0], 413)
            self.assertEqual(post(address, '{"contents": [], "temperature": NaN}')[0], 400)
            self.assertEqual(post(address, headers={"Origin": "https://example.invalid"})[0], 403)
            with core.lock:
                core.in_flight = pool_tool.MAX_PENDING_REQUESTS
            self.assertEqual(post(address)[0], 503)
            self.assertEqual(calls, [])
            with core.lock:
                core.in_flight = 0
            self.assertEqual(post(address)[0], 200)

    def test_slow_connections_are_bounded_without_blocking_accept_loop(self):
        with fixture() as (core, document, calls, address, maximum):
            server = pool_tool._CoreServer(("127.0.0.1", 0), pool_tool._CoreHandler)
            server._connections = threading.BoundedSemaphore(1)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            first = socket.create_connection(server.server_address, timeout=2)
            try:
                first.sendall(b"POST /v1beta/models/gemini-test:generateContent HTTP/1.0\r\n")
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    if not server._connections.acquire(blocking=False):
                        break
                    server._connections.release()
                    time.sleep(0.01)
                else:
                    self.fail("The first handler did not reserve a connection")
                self.assertEqual(post(server.server_address)[0], 503)
                self.assertEqual(calls, [])
                first.close()
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    if server._connections.acquire(blocking=False):
                        server._connections.release()
                        break
                    time.sleep(0.01)
                else:
                    self.fail("The closed connection was not released")
                self.assertEqual(post(server.server_address)[0], 200)
            finally:
                first.close()
                server.shutdown(); server.server_close(); thread.join(timeout=2)

    def test_cache_expiration_and_memory_budget_preserve_distinct_requests(self):
        with fixture() as (core, document, calls, address, maximum), \
             patch.object(pool_tool, "DUPLICATE_CACHE_BYTES", 1):
            self.assertEqual(post(address)[0], 200)
            self.assertEqual(core.duplicates, {})
        with fixture() as (core, document, calls, address, maximum), \
             patch.object(pool_tool, "DUPLICATE_TTL_SECONDS", 0):
            self.assertEqual(post(address)[0], 200)
            self.assertEqual(post(address)[0], 200)
            self.assertEqual(len(calls), 2)

    def test_retry_after_handles_seconds_dates_and_invalid_values(self):
        self.assertEqual(pool_tool.retry_after_seconds("2.5"), 2.5)
        self.assertEqual(pool_tool.retry_after_seconds("0"), 1)
        for value in (None, "bad", "-1", "nan", "inf"):
            self.assertEqual(pool_tool.retry_after_seconds(value), 60)
        self.assertEqual(pool_tool.retry_after_seconds(formatdate(1120, usegmt=True), now=1000), 120)


class WorkerProtectionTests(unittest.TestCase):
    def test_lost_router_response_reuses_result_without_second_upload(self):
        for worker in (transcription, translation):
            text = '[{"start":0,"end":1,"text":"speech"}]' if worker is transcription else "translated"
            reply = {"response": {"candidates": [{"content": {"parts": [{"text": text}]}}]}}
            with self.subTest(worker=worker.__name__), \
                 fixture(result=reply) as (core, document, calls, address, maximum), \
                 tempfile.TemporaryDirectory() as folder, \
                 patch.dict(os.environ, {"CUTPRO_POOL_ENDPOINT": "http://%s:%d" % address, "CUTPRO_POOL_MODE": "1"}), \
                 patch.object(worker.time, "sleep"):
                send = pool_tool._CoreHandler._send
                lost = []
                def disconnect_once(handler, status, value):
                    if status == 200 and not lost:
                        lost.append(True)
                        handler.close_connection = True
                        return
                    send(handler, status, value)
                config = {"model": "gemini-test", "api_keys": []}
                with patch.object(pool_tool._CoreHandler, "_send", disconnect_once):
                    if worker is transcription:
                        audio = Path(folder) / "audio.mp3"
                        audio.write_bytes(b"fixture")
                        self.assertEqual(worker.transcribe_section(config, str(audio), "auto", 6200, 3)[0]["text"], "speech")
                    else:
                        self.assertEqual(worker.gemini_completion(config, "fixture"), "translated")
                self.assertEqual(len(calls), 1)
                self.assertEqual(core.served_total, 1)
                self.assertEqual(core.in_flight, 0)

    def test_optional_recap_preserves_transcript_without_bypassing_rejection(self):
        with fixture() as (core, document, calls, address, maximum), \
             patch.object(core, "serve_request", return_value=pool_tool.pool_error(422, "fixture terminal")) as serve, \
             patch.dict(os.environ, {"CUTPRO_POOL_ENDPOINT": "http://%s:%d" % address, "CUTPRO_POOL_MODE": "1"}), \
             patch.object(transcription, "with_key_fallback", side_effect=AssertionError("must not fall back")):
            segments = [{"start": 0, "end": 1, "text": "speech"}]
            self.assertEqual(transcription.enrich_segments(
                {"model": "gemini-test", "api_keys": ["fixture-key"]}, segments, "en"), segments)
            self.assertEqual(serve.call_count, 1)

    def test_terminal_pool_errors_never_retry_or_fall_back_to_keys(self):
        for status in (400, 401, 403, 422, 429, 500, 502, 503, 504):
            for worker in (transcription, translation):
                with self.subTest(status=status, worker=worker.__name__), \
                     fixture() as (core, document, calls, address, maximum), \
                     patch.object(core, "serve_request", return_value=pool_tool.pool_error(status, "fixture terminal")) as serve, \
                     tempfile.TemporaryDirectory() as folder, \
                     patch.dict(os.environ, {"CUTPRO_POOL_ENDPOINT": "http://%s:%d" % address, "CUTPRO_POOL_MODE": "1"}), \
                     patch.object(worker, "with_key_fallback", side_effect=AssertionError("must not fall back")), \
                     patch.object(worker.time, "sleep") as sleep:
                    config = {"model": "gemini-test", "api_keys": ["fixture-key"]}
                    with self.assertRaises(worker.ProviderRequestError) as failure:
                        if worker is transcription:
                            audio = Path(folder) / "audio.mp3"
                            audio.write_bytes(b"fixture")
                            worker.transcribe_section(config, str(audio), "auto", 6200, 3)
                        else:
                            worker.gemini_completion(config, "fixture")
                    self.assertFalse(failure.exception.retryable)
                    self.assertEqual(serve.call_count, 1)
                    sleep.assert_not_called()


if __name__ == "__main__":
    unittest.main()
