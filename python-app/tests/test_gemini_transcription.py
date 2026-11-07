import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "gemini_worker", Path(__file__).resolve().parents[1] / "tools/gemini_transcribe.py")
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)


class GeminiTranscriptionTests(unittest.TestCase):
    def test_duration_and_verbatim_instructions_reach_the_audio_request(self):
        reply = {"candidates": [{"finishReason": "STOP", "content": {
            "parts": [{"text": "[]"}]}}]}
        with tempfile.TemporaryDirectory() as folder:
            audio = Path(folder) / "speech.mp3"
            audio.write_bytes(b"test audio")
            with patch.object(worker, "request_json", return_value=reply) as request:
                worker.request_transcript({"model": "test", "api_keys": []},
                    str(audio), "auto", 6200, via_pool=True, audio_seconds=23.456)
        prompt = request.call_args.args[1]["contents"][0]["parts"][0]["text"]
        self.assertIn("23.456 seconds", prompt)
        self.assertIn("verbatim", prompt)
        self.assertIn("[inaudible]", prompt)
        self.assertIn("WITHOUT removing", prompt)

    def test_valid_json_cut_off_by_provider_is_rejected(self):
        reply = {"candidates": [{"finishReason": "MAX_TOKENS", "content": {
            "parts": [{"text": '[{"start":0,"end":1,"text":"only the first line"}]'}]}}]}
        with tempfile.TemporaryDirectory() as folder:
            audio = Path(folder) / "speech.mp3"
            audio.write_bytes(b"test")
            with patch.object(worker, "request_json", return_value=reply):
                with self.assertRaisesRegex(RuntimeError, "incomplete"):
                    worker.request_transcript({"model": "test", "api_keys": []},
                        str(audio), "auto", 6200, via_pool=True)

    def test_later_dialogue_outside_actual_clip_is_retried(self):
        invalid = [{"start": 119.23, "end": 121.87, "text": "later dialogue"}]
        valid = [{"start": 79.23, "end": 81.87, "text": "later dialogue"}]
        with patch.object(worker, "pool_endpoint", return_value=""), \
                patch.object(worker, "request_transcript", side_effect=[json.dumps(invalid), json.dumps(valid)]) as request, \
                patch.object(worker.time, "sleep"):
            result = worker.transcribe_section({}, "audio.mp3", "auto", 6200, 113.012)
        self.assertEqual(result, valid)
        self.assertEqual(request.call_count, 2)
        self.assertEqual(request.call_args.kwargs["audio_seconds"], 113.012)

    def test_nonfinite_and_negative_timestamps_are_rejected(self):
        for start, end in [(float("nan"), 1), (0, float("inf")), (-1, 2), (1, 1)]:
            with self.subTest(start=start, end=end):
                with self.assertRaises(RuntimeError):
                    worker.validate_timestamps([{"start": start, "end": end, "text": "speech"}], 30)

    def test_short_utterance_at_the_end_stays_inside_audio(self):
        result = worker.shape_segments([{"start": 29.9, "end": 30, "text": "yes"}], 30, 6200)
        self.assertEqual(result[0]["end"], 30)
        self.assertEqual(result[0]["text"], "yes")

    def test_overlap_owns_whole_utterances_once_and_keeps_final_speech(self):
        # Two copies of each boundary utterance appear in neighboring requests.
        # The midpoint chooses one complete copy instead of emitting both halves.
        sections = [
            [{"start": 28, "end": 32, "text": "across thirty"}],
            [{"start": 1.35, "end": 5.35, "text": "across thirty"},
             {"start": 31.35, "end": 35.35, "text": "across sixty"}],
            [{"start": 1.35, "end": 5.35, "text": "across sixty"}],
            [{"start": 24.35, "end": 25.35, "text": "last words"}],
        ]
        with patch.object(worker, "audio_duration", side_effect=[113, 33.35, 36.7, 36.7, 26.35]) as probe, \
                patch.object(worker, "run_ffmpeg"), \
                patch.object(worker, "transcribe_section", side_effect=sections) as request:
            result = worker.transcribe_audio({"ffmpeg": "ffmpeg"}, "full.mp3", "auto", 6200, 0)
        self.assertEqual(probe.call_count, 5)
        self.assertEqual(request.call_count, 4)
        self.assertEqual([x["text"] for x in result], ["across thirty", "across sixty", "last words"])
        self.assertAlmostEqual(result[0]["start"], 28)
        self.assertAlmostEqual(result[1]["start"], 58)
        self.assertAlmostEqual(result[-1]["end"], 112)

    def test_silent_audio_keeps_real_gaps_without_inventing_segments(self):
        with patch.object(worker, "transcribe_section", return_value=[]):
            self.assertEqual(worker.transcribe_audio({"ffmpeg": "ffmpeg"}, "silence.mp3", "auto", 6200, 20), [])


if __name__ == "__main__":
    unittest.main()
