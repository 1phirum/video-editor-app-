"""Real loopback HTTP failures at the packaged AI workers' network boundary."""
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import gemini_transcribe as transcription
import translate_transcript as translation


@contextmanager
def endpoint(actions, text="[]"):
    calls = []
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            self.rfile.read(int(self.headers.get("Content-Length", "0")))
            calls.append(self.path)
            action = actions[min(len(calls) - 1, len(actions) - 1)]
            if action == "close":
                self.close_connection = True
                return
            body = json.dumps({"candidates": [{"finishReason": "STOP", "content": {
                "parts": [{"text": text}]}}], "choices": [{"message": {"content": text}}]}).encode()
            if action in ("other", "safety"):
                body = json.dumps({"promptFeedback": {"blockReason": action.upper()},
                                   "candidates": []}).encode()
            if action == "unauthorized":
                body = b'{"error":{"message":"fixture denied"}}'
            if action == "unavailable":
                body = b'{"error":{"message":"pool temporarily unavailable"}}'
            if action == "model-missing":
                body = b'{"error":{"message":"model not found","status":"NOT_FOUND"}}'
            self.send_response({"unauthorized": 401, "unavailable": 503,
                                "model-missing": 404}.get(action, 200))
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body) + (10 if action == "truncate" else 0)))
            self.end_headers()
            self.wfile.write(body)
            self.close_connection = True

        def do_GET(self):
            calls.append(self.path)
            action = actions[min(len(calls) - 1, len(actions) - 1)]
            body = json.dumps({"supportedGenerationMethods": [
                "embedContent" if action == "embedding" else "generateContent"]}).encode()
            if action == "model-missing":
                body = b'{"error":{"message":"model not found","status":"NOT_FOUND"}}'
            self.send_response(404 if action == "model-missing" else 200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield "http://%s:%d" % server.server_address, calls
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


class ProviderTransportTests(unittest.TestCase):
    def test_pool_only_model_keeps_pool_failure_and_never_uploads_to_api_keys(self):
        with endpoint(["unavailable"]) as (pool_url, pool_calls), \
                endpoint(["model-missing"]) as (api_url, api_calls), \
                tempfile.TemporaryDirectory() as folder, \
                patch.object(transcription, "pool_endpoint", return_value=pool_url), \
                patch.object(transcription, "API_ROOT", api_url), \
                patch.object(transcription.time, "sleep"):
            audio = Path(folder) / "audio.mp3"
            audio.write_bytes(b"fixture audio")
            config = {"model": "gemini-3.8-flash-high", "api_keys": ["fixture-key"]}
            with self.assertRaisesRegex(RuntimeError, "pool temporarily unavailable") as failure:
                transcription.transcribe_section(config, str(audio), "auto", 6200, 3)
        self.assertIn("API-key fallback is unavailable for gemini-3.8-flash-high", str(failure.exception))
        self.assertIn("Account Pool", str(failure.exception))
        self.assertNotIn("fixture-key", str(failure.exception))
        self.assertEqual(len(pool_calls), 2)
        self.assertEqual(len(api_calls), 1)
        self.assertNotIn(":generateContent", api_calls[0])

    def test_supported_api_model_can_still_transcribe_after_pool_failure(self):
        with endpoint(["unavailable"]) as (pool_url, pool_calls), \
                endpoint(["model", "ok"], '[{"start":0,"end":1,"text":"speech"}]') as (api_url, api_calls), \
                tempfile.TemporaryDirectory() as folder, \
                patch.object(transcription, "pool_endpoint", return_value=pool_url), \
                patch.object(transcription, "API_ROOT", api_url), \
                patch.object(transcription.time, "sleep"):
            audio = Path(folder) / "audio.mp3"
            audio.write_bytes(b"fixture audio")
            config = {"model": "gemini-2.5-flash", "api_keys": ["fixture-key"]}
            for _ in range(2):
                result = transcription.transcribe_section(config, str(audio), "auto", 6200, 3)
                self.assertEqual([item["text"] for item in result], ["speech"])
        self.assertEqual(len(pool_calls), 4)
        self.assertEqual(len(api_calls), 3)
        self.assertNotIn(":generateContent", api_calls[0])
        self.assertTrue(all(":generateContent" in path for path in api_calls[1:]))

    def test_embedding_model_cannot_be_used_for_api_fallback(self):
        with endpoint(["embedding"]) as (api_url, calls), \
                patch.object(transcription, "API_ROOT", api_url):
            config = {"model": "test", "api_keys": ["fixture-key"]}
            with self.assertRaisesRegex(RuntimeError, "does not support generateContent"):
                transcription.validate_api_fallback(config)
        self.assertEqual(len(calls), 1)
        self.assertNotIn("api_model_checked", config)

    def test_successful_pool_request_does_not_check_api_catalogue(self):
        with endpoint(["ok"], '[{"start":0,"end":1,"text":"speech"}]') as (pool_url, calls), \
                tempfile.TemporaryDirectory() as folder, \
                patch.object(transcription, "pool_endpoint", return_value=pool_url), \
                patch.object(transcription, "validate_api_fallback") as validate:
            audio = Path(folder) / "audio.mp3"
            audio.write_bytes(b"fixture audio")
            result = transcription.transcribe_section(
                {"model": "gemini-3.8-flash-high", "api_keys": ["fixture-key"]},
                str(audio), "auto", 6200, 3)
        self.assertEqual([item["text"] for item in result], ["speech"])
        self.assertEqual(len(calls), 1)
        validate.assert_not_called()

    def test_unavailable_pool_does_not_send_pool_model_to_api_keys(self):
        with endpoint(["model-missing"]) as (api_url, calls), \
                patch.object(transcription, "pool_endpoint", return_value=""), \
                patch.dict(os.environ, {"CUTPRO_POOL_MODE": "1"}), \
                patch.object(transcription, "API_ROOT", api_url):
            with self.assertRaisesRegex(RuntimeError, "Account Pool is not running"):
                transcription.transcribe_section(
                    {"model": "gemini-3.8-flash-high", "api_keys": ["fixture-key"]},
                    "unused.mp3", "auto", 6200, 3)
        self.assertEqual(len(calls), 1)
        self.assertNotIn(":generateContent", calls[0])

    def test_recap_fallback_keeps_transcript_when_api_model_is_unavailable(self):
        segments = [{"start": 0, "end": 1, "text": "speech"}]
        with endpoint(["unavailable"]) as (pool_url, pool_calls), \
                endpoint(["model-missing"]) as (api_url, api_calls), \
                patch.object(transcription, "pool_endpoint", return_value=pool_url), \
                patch.object(transcription, "API_ROOT", api_url):
            result = transcription.enrich_segments(
                {"model": "gemini-3.8-flash-high", "api_keys": ["fixture-key"]},
                segments, "auto")
        self.assertEqual(result, segments)
        self.assertEqual(len(pool_calls), 1)
        self.assertEqual(len(api_calls), 1)
        self.assertNotIn(":generateContent", api_calls[0])

    def test_unavailable_pool_without_keys_keeps_readiness_error(self):
        with patch.object(transcription, "pool_endpoint", return_value=""), \
                patch.dict(os.environ, {"CUTPRO_POOL_MODE": "1"}), \
                patch.object(transcription, "request_json") as request:
            with self.assertRaisesRegex(RuntimeError, "Account Pool is not running"):
                transcription.transcribe_section(
                    {"model": "gemini-3.8-flash-high", "api_keys": []},
                    "unused.mp3", "auto", 6200, 3)
        request.assert_not_called()

    def test_transcription_pool_retries_closed_connection_without_a_key(self):
        with endpoint(["close", "ok"], '[{"start":0,"end":1,"text":"speech"}]') as (url, calls), \
                tempfile.TemporaryDirectory() as folder, \
                patch.object(transcription, "pool_endpoint", return_value=url), \
                patch.object(transcription.time, "sleep"):
            audio = Path(folder) / "audio.mp3"
            audio.write_bytes(b"fixture audio")
            result = transcription.transcribe_section({"model": "test", "api_keys": []},
                str(audio), "auto", 6200, 3)
        self.assertEqual([item["text"] for item in result], ["speech"])
        self.assertEqual(len(calls), 2)

    def test_transcription_api_retries_disconnect_and_incomplete_body_on_same_key(self):
        for failure in ["close", "truncate"]:
            with self.subTest(failure=failure), endpoint([failure, "ok"]) as (url, calls), \
                    tempfile.TemporaryDirectory() as folder, \
                    patch.object(transcription, "API_ROOT", url), \
                    patch.object(transcription.time, "sleep"):
                audio = Path(folder) / "audio.mp3"
                audio.write_bytes(b"fixture")
                config = {"model": "test", "api_keys": ["fixture-first", "fixture-second"]}
                self.assertEqual(transcription.request_transcript(config, str(audio), "auto", 6200), "[]")
                self.assertEqual(len(calls), 2)
                self.assertTrue(all("fixture-first" in path for path in calls))

    def test_translation_pool_retries_closed_connection_without_a_key(self):
        with endpoint(["close", "ok"], "translated") as (url, calls), \
                patch.dict(os.environ, {"CUTPRO_POOL_ENDPOINT": url, "CUTPRO_POOL_MODE": "1"}), \
                patch.object(translation.time, "sleep"):
            self.assertEqual(translation.gemini_completion({"model": "test", "api_keys": []},
                                                          "fixture speech"), "translated")
        self.assertEqual(len(calls), 2)

    def test_translation_api_retries_disconnect_and_incomplete_body(self):
        for failure in ["close", "truncate"]:
            with self.subTest(failure=failure), endpoint([failure, "ok"], "translated") as (url, calls), \
                    patch.object(translation.time, "sleep"):
                config = {"model": "test", "base_url": url, "api_keys": ["fixture-key"]}
                self.assertEqual(translation.openai_completion(config, "fixture speech"), "translated")
                self.assertEqual(len(calls), 2)

    def test_permanent_auth_error_is_not_retried_as_a_network_failure(self):
        for worker, retry in [(transcription, transcription.with_key_fallback),
                              (translation, lambda config, operation: translation.with_key_fallback(
                                  config, lambda key: translation.retry_transient(lambda: operation(key))))]:
            with self.subTest(worker=worker.__name__), endpoint(["unauthorized"]) as (url, calls), \
                    patch.object(worker.time, "sleep") as sleep:
                with self.assertRaisesRegex(RuntimeError, "401"):
                    retry({"api_keys": ["fixture-key"]},
                          lambda key: worker.request_json(url, {"contents": []}))
                self.assertEqual(len(calls), 1)
                sleep.assert_not_called()

    def test_persistent_pool_disconnect_is_bounded_and_keeps_the_real_error(self):
        for worker in [transcription, translation]:
            with self.subTest(worker=worker.__name__), endpoint(["close"]) as (url, calls), \
                    tempfile.TemporaryDirectory() as folder, \
                    patch.dict(os.environ, {"CUTPRO_POOL_ENDPOINT": url, "CUTPRO_POOL_MODE": "1"}), \
                    patch.object(worker.time, "sleep"):
                with self.assertRaisesRegex(RuntimeError, "connection failed"):
                    if worker is transcription:
                        audio = Path(folder) / "audio.mp3"
                        audio.write_bytes(b"fixture")
                        worker.transcribe_section({"model": "test", "api_keys": []},
                                                  str(audio), "auto", 6200, 3)
                    else:
                        worker.gemini_completion({"model": "test", "api_keys": []}, "speech")
                self.assertEqual(len(calls), 2 if worker is transcription else 3)


if __name__ == "__main__":
    unittest.main()
