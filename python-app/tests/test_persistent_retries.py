"""Persistent work-unit recovery, with controlled local failures and no Google calls."""
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, os.environ.get("CUTPRO_POOL_TEST_TOOLS",
                                str(Path(__file__).resolve().parents[1] / "tools")))
import persistent_retry
import gemini_transcribe as transcription
import translate_transcript as translation
from test_provider_transport import endpoint


class PersistentRetryTests(unittest.TestCase):
    def test_other_response_retries_identical_audio_until_success(self):
        with endpoint(["other"] * 3 + ["ok"], '[{"start":0,"end":1,"text":"speech"}]') as (url, calls), \
                tempfile.TemporaryDirectory() as folder, \
                patch.object(transcription, "pool_endpoint", return_value=url), \
                patch.object(transcription, "validate_api_fallback") as fallback, \
                patch.object(transcription, "request_json", wraps=transcription.request_json) as request, \
                patch.object(transcription, "emit") as report, \
                patch.object(persistent_retry.time, "sleep"):
            audio = Path(folder) / "audio.mp3"
            audio.write_bytes(b"fixture audio")
            result = transcription.transcribe_audio(
                {"model": "fixture", "api_keys": ["fixture-key"]}, str(audio), "auto", 6200, 3)
        self.assertEqual([s["text"] for s in result], ["speech"])
        self.assertEqual(len(calls), 4)
        self.assertTrue(all(call.args[1] == request.call_args_list[0].args[1] for call in request.call_args_list))
        self.assertTrue(any("waiting" in call.args[0]["message"] for call in report.call_args_list))
        fallback.assert_not_called()

    def test_translation_other_response_retries_until_success(self):
        with endpoint(["other"] * 3 + ["ok"], '["translated"]') as (url, calls), \
                patch.dict(os.environ, {"CUTPRO_POOL_ENDPOINT": url, "CUTPRO_POOL_MODE": "1"}), \
                patch.object(translation, "with_key_fallback") as fallback, \
                patch.object(translation, "report_status") as report, \
                patch.object(persistent_retry.time, "sleep"):
            result = translation.translate_segments(
                {"provider": "gemini", "model": "fixture", "api_keys": ["fixture-key"]}, ["speech"], "es")
        self.assertEqual(result, ["translated"])
        self.assertEqual(len(calls), 4)
        fallback.assert_not_called()
        self.assertTrue(any("waiting" in call.args[0] for call in report.call_args_list))

    def test_explicit_safety_responses_remain_terminal_for_both_workers(self):
        for worker in (transcription, translation):
            with self.subTest(worker=worker.__name__), endpoint(["safety", "ok"]) as (url, calls), \
                    tempfile.TemporaryDirectory() as folder, \
                    patch.dict(os.environ, {"CUTPRO_POOL_ENDPOINT": url, "CUTPRO_POOL_MODE": "1"}), \
                    patch.object(transcription, "pool_endpoint", return_value=url), \
                    patch.object(persistent_retry.time, "sleep") as sleep:
                audio = Path(folder) / "audio.mp3"
                audio.write_bytes(b"fixture audio")
                with self.assertRaises(worker.ProviderRequestError) as failure:
                    if worker is transcription:
                        worker.transcribe_audio({"model": "fixture"}, str(audio), "auto", 6200, 3)
                    else:
                        worker.translate_segments({"provider": "gemini", "model": "fixture"}, ["speech"], "es")
                self.assertEqual(failure.exception.status, 422)
                self.assertEqual(len(calls), 1)
                sleep.assert_not_called()

    def test_explicit_policy_reason_takes_precedence_over_other(self):
        self.assertEqual(persistent_retry.generation_rejection({
            "promptFeedback": {"blockReason": "OTHER"}, "candidates": [{"finishReason": "SAFETY"}]}), ("SAFETY", True))
        self.assertEqual(persistent_retry.generation_rejection({
            "candidates": [{"finishReason": "OTHER", "content": {"parts": []}}]}), ("OTHER", False))

    def test_parallel_retry_status_lines_do_not_interleave(self):
        class SlowOutput(io.StringIO):
            def write(self, value):
                result = super().write(value)
                time.sleep(0.002)
                return result
        output = SlowOutput()
        with patch.object(translation.sys, "stdout", output), ThreadPoolExecutor(max_workers=8) as executor:
            list(executor.map(translation.report_status, ["batch %d" % i for i in range(16)]))
        reports = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(len(reports), 16)
        self.assertEqual({r["message"] for r in reports}, {"batch %d" % i for i in range(16)})

    def test_retries_beyond_previous_limits_and_honors_cooldown(self):
        calls, reports = [], []
        def work():
            calls.append(1)
            if len(calls) <= 12:
                raise transcription.ProviderRequestError("temporary", 429, False, 120)
            return "complete"
        with patch.object(persistent_retry.time, "sleep") as sleep:
            self.assertEqual(persistent_retry.retry_work(work, reports.append, "Transcription", pool=True), "complete")
        self.assertEqual(len(calls), 13)
        self.assertEqual([call.args[0] for call in sleep.call_args_list], [120] * 12)
        self.assertIn("free enabled account", reports[0])
        self.assertIn("succeeded", reports[-1])

    def test_terminal_failures_do_not_loop(self):
        failures = [transcription.ProviderRequestError("missing model", 404),
                    translation.ProviderRequestError("verify account", 403, False),
                    transcription.ProviderRequestError("content declined", 422, False),
                    ValueError("Choose a model"), FileNotFoundError("missing audio")]
        for error in failures:
            with self.subTest(error=error), patch.object(persistent_retry.time, "sleep") as sleep:
                def work():
                    raise error
                with self.assertRaises(type(error)):
                    persistent_retry.retry_work(work, lambda _: None, "work")
                sleep.assert_not_called()

    def test_transcription_recovers_without_falling_back_to_wrong_api_model(self):
        with endpoint(["unavailable"] * 5 + ["ok"], '[{"start":0,"end":1,"text":"speech"}]') as (url, calls), \
                tempfile.TemporaryDirectory() as folder, \
                patch.object(transcription, "pool_endpoint", return_value=url), \
                patch.object(transcription, "validate_api_fallback") as fallback, \
                patch.object(transcription, "emit") as emit, \
                patch.object(persistent_retry.time, "sleep"):
            audio = Path(folder) / "audio.mp3"
            audio.write_bytes(b"fixture audio")
            result = transcription.transcribe_audio(
                {"model": "pool-only-model", "api_keys": ["fixture-key"]}, str(audio), "auto", 6200, 3)
        self.assertEqual([segment["text"] for segment in result], ["speech"])
        self.assertEqual(len(calls), 6)
        fallback.assert_not_called()
        self.assertTrue(any("waiting" in call.args[0]["message"] for call in emit.call_args_list))

    def test_missing_pool_address_is_actionable_instead_of_waiting_forever(self):
        with patch.dict(os.environ, {"CUTPRO_POOL_MODE": "1", "CUTPRO_POOL_ENDPOINT": ""}), \
                patch.object(persistent_retry.time, "sleep") as sleep:
            with self.assertRaisesRegex(ValueError, "Pool address"):
                transcription.transcribe_audio({"model": "fixture"}, "audio.mp3", "auto", 6200, 3)
            with self.assertRaisesRegex(ValueError, "Pool address"):
                translation.translate_segments({"provider": "gemini", "model": "fixture"}, ["speech"], "es")
            sleep.assert_not_called()

    def test_completed_speech_sections_are_not_repeated_or_prepared_again(self):
        calls = Counter()
        def section(config, path, language, max_ms, duration):
            name = Path(path).name
            calls[name] += 1
            if name == "s0001.mp3" and calls[name] < 4:
                raise transcription.ProviderRequestError("temporary", 503, False)
            return [{"start": 5, "end": 6, "text": name}]
        with patch.object(transcription, "transcribe_section", side_effect=section), \
                patch.object(transcription, "run_ffmpeg") as prepare, \
                patch.object(transcription, "audio_duration", return_value=30), \
                patch.object(transcription, "emit"), patch.object(persistent_retry.time, "sleep"):
            result = transcription.transcribe_audio({"ffmpeg": "fixture"}, "audio.mp3", "auto", 6200, 90)
        self.assertEqual(calls, {"s0000.mp3": 1, "s0001.mp3": 4, "s0002.mp3": 1})
        self.assertEqual(prepare.call_count, 3)
        self.assertEqual([s["text"] for s in result], ["s0000.mp3", "s0001.mp3", "s0002.mp3"])

    def test_translation_recovers_after_exhausting_bounded_http_retries(self):
        with endpoint(["unavailable"] * 10 + ["ok"], '["translated"]') as (url, calls), \
                patch.dict(os.environ, {"CUTPRO_POOL_ENDPOINT": url, "CUTPRO_POOL_MODE": "1"}), \
                patch.object(translation, "with_key_fallback") as fallback, \
                patch.object(translation, "report_status") as report, \
                patch.object(persistent_retry.time, "sleep"):
            result = translation.translate_segments(
                {"provider": "gemini", "model": "pool-only-model", "api_keys": ["fixture"]}, ["speech"], "es")
        self.assertEqual(result, ["translated"])
        self.assertEqual(len(calls), 11)
        fallback.assert_not_called()
        self.assertTrue(any("waiting" in call.args[0] for call in report.call_args_list))

    def test_parallel_translation_keeps_completed_batches_and_order(self):
        calls = Counter()
        completed = threading.Event()
        def batch(config, texts, target):
            index = int(texts[0]) // 40
            calls[index] += 1
            if index == 0 and calls[index] == 1:
                if not completed.wait(2):
                    raise AssertionError("other batch did not finish")
                raise translation.ProviderRequestError("temporary", 503, False)
            if index == 1:
                completed.set()
            return ["translated " + text for text in texts]
        texts = [str(i) for i in range(90)]
        with patch.dict(os.environ, {"CUTPRO_POOL_ENDPOINT": "fixture", "CUTPRO_POOL_MODE": "1", "CUTPRO_POOL_PARALLELISM": "2"}), \
                patch.object(translation, "translate_model_batch", side_effect=batch), \
                patch.object(translation, "report_status"), patch.object(persistent_retry.time, "sleep"):
            result = translation.translate_segments({"provider": "gemini"}, texts, "es")
        self.assertEqual(result, ["translated " + text for text in texts])
        self.assertEqual(calls, {0: 2, 1: 1, 2: 1})

    def test_malformed_model_output_is_retryable(self):
        for worker, parse in [(transcription, transcription.parse_segment_array),
                              (translation, translation.parse_json_array)]:
            with self.subTest(worker=worker.__name__):
                try:
                    parse("model wrote prose")
                except Exception as error:
                    self.assertTrue(persistent_retry.failure_policy(error)[0])

    def test_cancel_terminates_a_worker_during_retry_wait(self):
        code = """
import sys
sys.path.insert(0, sys.argv[1])
from persistent_retry import retry_work, ModelOutputError
def work():
    raise ModelOutputError('temporary')
retry_work(work, lambda message: print(message, flush=True), 'Transcription', pool=True)
"""
        tools = str(Path(transcription.__file__).resolve().parent)
        child = subprocess.Popen([sys.executable, "-c", code, tools], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            with ThreadPoolExecutor(max_workers=1) as executor:
                report = executor.submit(child.stdout.readline)
                try:
                    self.assertIn(b"waiting", report.result(timeout=3))
                finally:
                    child.kill()
                child.wait(timeout=2)
            self.assertNotEqual(child.returncode, 0)
        finally:
            if child.poll() is None:
                child.kill()
            child.communicate(timeout=2)


if __name__ == "__main__":
    unittest.main()
