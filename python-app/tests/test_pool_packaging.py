"""Exercise the real worker launcher with Windows' isolated bundled Python."""
import http.client
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import unittest
import urllib.parse


class BundledPoolTests(unittest.TestCase):
    def setUp(self):
        app = Path(__file__).resolve().parents[1]
        self.python = Path(os.environ.get(
            "CUTPRO_POOL_TEST_PYTHON",
            app / "dist" / "Cut Pro 2026" / "python" / "python.exe"))
        self.worker = Path(os.environ.get("CUTPRO_POOL_TEST_WORKER", app / "tools" / "pool_tool.py"))
        if os.name != "nt" or not self.python.is_file():
            self.skipTest("Set CUTPRO_POOL_TEST_PYTHON to the Windows bundled runtime")
        self.folder = tempfile.TemporaryDirectory(prefix="cutpro-pool-packaging-")
        self.addCleanup(self.folder.cleanup)
        self.env = os.environ.copy()
        self.env["APPDATA"] = str(Path(self.folder.name) / "appdata")
        self.env["LOCALAPPDATA"] = str(Path(self.folder.name) / "localappdata")
        self.env["PYTHONIOENCODING"] = "utf-8"
        self.env.pop("PYTHONPATH", None)

    def test_bundled_runtime_reads_state_without_system_import_paths(self):
        result = subprocess.run(
            [str(self.python), "-u", str(self.worker), "state"],
            cwd=self.folder.name, env=self.env, capture_output=True,
            text=True, encoding="utf-8", timeout=10,
            creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertEqual(result.returncode, 0, result.stderr)
        state = json.loads(result.stdout)
        self.assertIn("accounts", state)
        self.assertEqual(state["accounts"], [])
        self.assertEqual(state["readyCount"], 0)

    def test_running_router_retires_when_worker_file_is_updated(self):
        worker = Path(self.folder.name) / "tools" / "pool_tool.py"
        worker.parent.mkdir()
        shutil.copy2(self.worker, worker)
        shutil.copy2(self.worker.parent / "pool_activity.py", worker.parent / "pool_activity.py")
        shutil.copy2(self.worker.parent / "persistent_retry.py", worker.parent / "persistent_retry.py")
        self.env["CUTPRO_POOL_PORT"] = "0"
        code = '''
from pathlib import Path
import json
import sys
sys.path.insert(0, str(Path(sys.argv[1]).parent))
import pool_tool
pool_tool.save_store({"version": 1, "accounts": [{"id": "fixture", "status": "ready"}]})
original = pool_tool._CoreServer
class Server(original):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        print(json.dumps({"address": self.server_address}), flush=True)
pool_tool._CoreServer = Server
sys.exit(pool_tool.command_serve())
'''
        process = subprocess.Popen(
            [str(self.python), "-u", "-c", code, str(worker)],
            cwd=self.folder.name, env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding="utf-8", creationflags=subprocess.CREATE_NO_WINDOW)
        lines = queue.Queue()
        def read_lines():
            for line in process.stdout:
                lines.put(line)
        reader = threading.Thread(target=read_lines, daemon=True)
        reader.start()
        try:
            address = json.loads(lines.get(timeout=8))["address"]
            client = http.client.HTTPConnection(*address, timeout=3)
            try:
                client.request("GET", "/health")
                health = json.loads(client.getresponse().read())
                self.assertTrue(health["ok"])
                self.assertEqual(health["routerProtocol"], 2)
                self.assertEqual(health["pid"], process.pid)
                self.assertEqual(len(health["workerRevision"]), 64)
            finally:
                client.close()
            with worker.open("a", encoding="utf-8") as handle:
                handle.write("\n# packaging test: updated worker revision\n")
            self.assertEqual(process.wait(timeout=8), 0, process.stderr.read())
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            reader.join(timeout=1)
            process.stdout.close()
            process.stderr.close()

    def test_ai_workers_import_retry_helper_with_isolated_runtime(self):
        for name in ("gemini_transcribe.py", "translate_transcript.py"):
            with self.subTest(worker=name):
                result = subprocess.run(
                    [str(self.python), "-u", str(self.worker.parent / name)],
                    cwd=self.folder.name, env=self.env, capture_output=True,
                    text=True, encoding="utf-8", timeout=10,
                    creationflags=subprocess.CREATE_NO_WINDOW)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stderr, "")
                self.assertIn("error", json.loads(result.stdout))

    def test_state_identifies_accounts_by_full_email_without_credentials(self):
        code = '''
from pathlib import Path
import sys
sys.path.insert(0, str(Path(sys.argv[1]).parent))
import pool_tool
pool_tool.save_store({"version": 1, "accounts": [
    {"id": "one", "email": "phi.first@example.invalid", "status": "ready",
     "refreshToken": "fixture-refresh-token", "accessToken": "fixture-access-token"},
    {"id": "two", "email": "phi.second@example.invalid", "status": "error",
     "statusDetail": "verify this account"}]})
pool_tool.command_state()
'''
        result = subprocess.run(
            [str(self.python), "-u", "-c", code, str(self.worker)],
            cwd=self.folder.name, env=self.env, capture_output=True,
            text=True, encoding="utf-8", timeout=10,
            creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertEqual(result.returncode, 0, result.stderr)
        accounts = json.loads(result.stdout)["accounts"]
        self.assertEqual([account["email"] for account in accounts],
                         ["phi.first@example.invalid", "phi.second@example.invalid"])
        self.assertEqual(accounts[1]["status"], "error")
        self.assertEqual(accounts[1]["statusDetail"], "verify this account")
        self.assertNotIn("fixture-refresh-token", result.stdout)
        self.assertNotIn("fixture-access-token", result.stdout)

    def test_router_keeps_serving_after_real_windows_output_pipe_closes(self):
        code = '''
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(sys.argv[1]).parent))
import pool_tool
def serve(model, payload, request_id):
    pool_tool.CORE.activity.update(request_id, "sending", "fixture-account")
    return 200, {"candidates": []}
pool_tool.CORE.serve_request = serve
server = pool_tool._CoreServer(("127.0.0.1", 0), pool_tool._CoreHandler)
print(json.dumps(server.server_address), flush=True)
server.serve_forever()
'''
        process = subprocess.Popen(
            [str(self.python), "-u", "-c", code, str(self.worker)],
            cwd=self.folder.name, env=self.env,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            text=True, encoding="utf-8", creationflags=subprocess.CREATE_NO_WINDOW)
        lines = queue.Queue()
        reader = threading.Thread(target=lambda: lines.put(process.stdout.readline()), daemon=True)
        reader.start()
        try:
            address = json.loads(lines.get(timeout=8))
            reader.join(timeout=1)
            # Closing the actual read handle reproduces an exited Qt app; this
            # exercises Windows' broken-pipe exception without mocking emit().
            process.stdout.close()
            for _ in range(3):
                client = http.client.HTTPConnection(*address, timeout=3)
                try:
                    client.request("POST", "/v1beta/models/gemini-test:generateContent",
                                   json.dumps({"contents": []}))
                    response = client.getresponse()
                    self.assertEqual(response.status, 200)
                    response.read()
                    client.request("GET", "/health")
                    health = json.loads(client.getresponse().read())
                    self.assertEqual(health["inFlight"], 0)
                    self.assertEqual(health["activeRequests"], [])
                finally:
                    client.close()
            self.assertEqual(len(health["recentRequests"]), 3)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            reader.join(timeout=1)
            process.stdout.close()

    def test_account_selection_persists_and_verification_does_not_enable_it(self):
        def run(*args):
            return subprocess.run([str(self.python), "-u", *args],
                cwd=self.folder.name, env=self.env, capture_output=True,
                text=True, encoding="utf-8", timeout=10,
                creationflags=subprocess.CREATE_NO_WINDOW)
        code = '''
from pathlib import Path
import sys
sys.path.insert(0, str(Path(sys.argv[1]).parent))
import pool_tool
pool_tool.save_store({"version": 1, "accounts": [
    {"id": "one", "email": "one@example.invalid", "status": "ready"},
    {"id": "two", "email": "two@example.invalid", "status": "ready"}]})
'''
        self.assertEqual(run("-c", code, str(self.worker)).returncode, 0)
        result = run(str(self.worker), "state")
        self.assertEqual(json.loads(result.stdout)["readyCount"], 2)
        self.assertEqual(run(str(self.worker), "set-enabled", "one", "0").returncode, 0)
        result = run(str(self.worker), "state")
        state = json.loads(result.stdout)
        self.assertEqual(state["readyCount"], 1)
        self.assertFalse(state["accounts"][0]["enabled"])
        self.assertTrue(state["accounts"][1]["enabled"])
        verify = '''
from pathlib import Path
import sys
sys.path.insert(0, str(Path(sys.argv[1]).parent))
import pool_tool
pool_tool.refresh_access_token = lambda account: "fixture-token"
pool_tool.detect_profile = lambda token: ("free", "fixture-project")
pool_tool.account_models = lambda account: (["gemini-test"], "fixture-project")
pool_tool.http_json = lambda *args, **kwargs: {"response": {"candidates": [{"finishReason": "STOP"}]}}
pool_tool.command_verify()
'''
        self.assertEqual(run("-c", verify, str(self.worker)).returncode, 0)
        result = run(str(self.worker), "state")
        self.assertEqual(json.loads(result.stdout)["readyCount"], 1)
        store = Path(self.env["APPDATA"]) / "CutPro" / "pool-accounts.bin"
        before = store.read_bytes()
        self.assertEqual(run(str(self.worker), "set-enabled", "missing", "1").returncode, 4)
        self.assertEqual(run(str(self.worker), "set-enabled", "one", "invalid").returncode, 2)
        self.assertEqual(store.read_bytes(), before)
        self.assertEqual(run(str(self.worker), "set-enabled", "one", "1").returncode, 0)
        result = run(str(self.worker), "state")
        self.assertEqual(json.loads(result.stdout)["readyCount"], 2)
        for account_id in ("one", "two"):
            self.assertEqual(run(str(self.worker), "set-enabled", account_id, "0").returncode, 0)
        self.assertEqual(run(str(self.worker), "serve").returncode, 4)

    def test_bundled_runtime_starts_signin_and_handles_loopback_cancel(self):
        process = subprocess.Popen(
            [str(self.python), "-u", str(self.worker), "add"],
            cwd=self.folder.name, env=self.env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding="utf-8",
            creationflags=subprocess.CREATE_NO_WINDOW)
        lines = queue.Queue()
        reader = threading.Thread(target=lambda: lines.put(process.stdout.readline()), daemon=True)
        reader.start()
        try:
            first = lines.get(timeout=8)
            if not first:
                self.fail("Sign-in worker exited before returning a browser URL: " + process.stderr.read())
            event = json.loads(first)
            self.assertIn("authUrl", event)
            url = urllib.parse.urlparse(event["authUrl"])
            self.assertEqual(url.scheme, "https")
            self.assertEqual(url.hostname, "accounts.google.com")
            params = urllib.parse.parse_qs(url.query)
            redirect = urllib.parse.urlparse(params["redirect_uri"][0])
            self.assertEqual(redirect.hostname, "127.0.0.1")
            client = http.client.HTTPConnection(redirect.hostname, redirect.port, timeout=3)
            try:
                # A wrong state must be rejected without ending this sign-in.
                client.request("GET", redirect.path + "?state=wrong&error=access_denied")
                response = client.getresponse()
                self.assertEqual(response.status, 400)
                response.read()
                client.request("GET", redirect.path + "?" + urllib.parse.urlencode(
                    {"state": params["state"][0], "error": "access_denied"}))
                response = client.getresponse()
                self.assertEqual(response.status, 400)
                response.read()
            finally:
                client.close()
            output, error = process.communicate(timeout=5)
            self.assertEqual(process.returncode, 4, error)
            result = json.loads(output)
            self.assertIn("cancelled", result["error"])
            self.assertFalse((Path(self.env["APPDATA"]) / "CutPro" / "pool-accounts.bin").exists())
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            reader.join(timeout=1)
            process.stdout.close()
            process.stderr.close()


if __name__ == "__main__":
    unittest.main()
