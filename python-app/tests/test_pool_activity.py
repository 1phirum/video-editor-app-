import concurrent.futures
import http.client
import json
from pathlib import Path
import sys
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from pool_activity import RouterActivity
import pool_tool


class ActivityTests(unittest.TestCase):
    def test_lifecycle_snapshots_are_bounded_and_independent(self):
        events = []
        activity = RouterActivity(events.append)
        request = activity.begin("gemini-test")
        activity.update(request, "sending", "account-a")
        snapshot = activity.snapshot()
        self.assertEqual(snapshot["activeRequests"][0]["stage"], "sending")
        snapshot["activeRequests"][0]["stage"] = "changed"
        self.assertEqual(activity.snapshot()["activeRequests"][0]["stage"], "sending")
        activity.finish(request, 200)
        self.assertEqual(activity.snapshot()["activeRequests"], [])
        self.assertEqual(activity.snapshot()["recentRequests"][0]["stage"], "completed")
        revision = activity.snapshot()["activityRevision"]
        activity.finish(request, 500)
        self.assertEqual(activity.snapshot()["activityRevision"], revision)
        for _ in range(30):
            activity.finish(activity.begin("test"), 502)
        self.assertEqual(len(activity.snapshot()["recentRequests"]), 20)
        self.assertEqual(activity.snapshot()["recentRequests"][0]["stage"], "failed")
        self.assertTrue(all("payload" not in item for item in events))

    def test_concurrent_requests_release_every_slot(self):
        activity = RouterActivity()
        def work(index):
            request = activity.begin("test")
            activity.update(request, "connecting", str(index))
            activity.update(request, "sending", str(index))
            activity.finish(request, 200)
        with concurrent.futures.ThreadPoolExecutor(max_workers=16) as executor:
            list(executor.map(work, range(64)))
        snapshot = activity.snapshot()
        self.assertEqual(snapshot["activeRequests"], [])
        self.assertEqual(len(snapshot["recentRequests"]), 20)
        self.assertEqual(snapshot["activityRevision"], 64 * 4)
        self.assertNotEqual(snapshot["activitySession"], RouterActivity().snapshot()["activitySession"])

    def test_http_failure_releases_request_and_hides_exception_detail(self):
        core = pool_tool.PoolCore()
        server = pool_tool._CoreServer(("127.0.0.1", 0), pool_tool._CoreHandler)
        with patch.object(pool_tool, "CORE", core), \
             patch.object(pool_tool, "emit"), \
             patch.object(core, "serve_request", side_effect=RuntimeError("private-fixture-detail")):
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                client = http.client.HTTPConnection(*server.server_address, timeout=3)
                client.request("POST", "/v1beta/models/gemini-test:generateContent",
                               json.dumps({"contents": []}))
                response = client.getresponse()
                self.assertEqual(response.status, 500)
                self.assertNotIn("private-fixture-detail", response.read().decode())
                client.close()
                self.assertEqual(core.in_flight, 0)
                snapshot = core.activity.snapshot()
                self.assertEqual(snapshot["activeRequests"], [])
                self.assertEqual(snapshot["recentRequests"][0]["stage"], "failed")
            finally:
                server.shutdown(); server.server_close(); thread.join(timeout=2)

    def test_closed_app_event_pipe_does_not_abort_http_or_leak_requests(self):
        core = pool_tool.PoolCore()
        server = pool_tool._CoreServer(("127.0.0.1", 0), pool_tool._CoreHandler)
        handler_errors = []
        server.handle_error = lambda *args: handler_errors.append("handler failed")
        def serve(model, payload, request_id):
            core.activity.update(request_id, "sending", "fixture-account")
            return 200, {"candidates": []}
        with patch.object(pool_tool, "CORE", core), \
             patch.object(pool_tool, "emit", side_effect=BrokenPipeError("closed app pipe")), \
             patch.object(core, "serve_request", side_effect=serve):
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                for _ in range(3):
                    client = http.client.HTTPConnection(*server.server_address, timeout=3)
                    try:
                        client.request("POST", "/v1beta/models/gemini-test:generateContent",
                                       json.dumps({"contents": []}))
                        response = client.getresponse()
                        self.assertEqual(response.status, 200)
                        response.read()
                    finally:
                        client.close()
                self.assertEqual(core.in_flight, 0)
                self.assertEqual(core.activity.snapshot()["activeRequests"], [])
                self.assertEqual(len(core.activity.snapshot()["recentRequests"]), 3)
                self.assertEqual(handler_errors, [])
            finally:
                server.shutdown(); server.server_close(); thread.join(timeout=2)

    def test_http_request_publishes_sending_and_completion(self):
        core = pool_tool.PoolCore()
        document = {"strategy": "round-robin", "accounts": [
            {"id": "fixture-account", "email": "fixture@example.invalid", "status": "ready", "served": 0}]}
        events, sent, release = [], threading.Event(), threading.Event()
        class Response:
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, size=-1):
                sent.set()
                if not release.wait(3):
                    raise TimeoutError("fixture timeout")
                return b'{"response":{"candidates":[]}}'
        def update(account_id, operation):
            operation(document["accounts"][0])
            return document
        server = pool_tool._CoreServer(("127.0.0.1", 0), pool_tool._CoreHandler)
        def call():
            client = http.client.HTTPConnection(*server.server_address, timeout=5)
            client.request("POST", "/v1beta/models/gemini-test:generateContent",
                           json.dumps({"contents": [{"parts": [{"text": "fixture"}]}]}))
            response = client.getresponse()
            status = response.status
            response.read(); client.close()
            return status
        with patch.object(pool_tool, "CORE", core), \
             patch.object(pool_tool, "load_store", return_value=document), \
             patch.object(pool_tool, "update_account", side_effect=update), \
             patch.object(pool_tool, "write_state"), \
             patch.object(pool_tool, "emit", side_effect=events.append), \
             patch.object(pool_tool, "refresh_access_token", return_value="fixture-token"), \
             patch.object(pool_tool.urllib.request, "urlopen", return_value=Response()):
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                with concurrent.futures.ThreadPoolExecutor(max_workers=1) as executor:
                    result = executor.submit(call)
                    self.assertTrue(sent.wait(3))
                    client = http.client.HTTPConnection(*server.server_address, timeout=3)
                    client.request("GET", "/health")
                    health = json.loads(client.getresponse().read())
                    client.close()
                    self.assertEqual(health["inFlight"], 1)
                    self.assertEqual(health["activeRequests"][0]["stage"], "sending")
                    self.assertEqual(health["activeRequests"][0]["accountId"], "fixture-account")
                    release.set()
                    self.assertEqual(result.result(timeout=4), 200)
                self.assertEqual(core.in_flight, 0)
                self.assertEqual(core.active, set())
                self.assertEqual(core.activity.snapshot()["recentRequests"][0]["stage"], "completed")
                self.assertEqual(core.served_total, 1)
                self.assertNotIn("fixture-token", json.dumps(events))
                self.assertNotIn('"text"', json.dumps(events))
            finally:
                release.set(); server.shutdown(); server.server_close(); thread.join(timeout=2)


if __name__ == "__main__":
    unittest.main()
