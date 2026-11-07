"""Live translation delivery with local providers and controlled completion order."""
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import translate_transcript as translation


class TranslationProgressTests(unittest.TestCase):
    def test_free_results_stream_before_a_slower_earlier_segment(self):
        release = threading.Event()
        updates = []

        def translate(text, target):
            if text == "first" and not release.wait(2):
                raise ValueError("An earlier segment blocked live delivery")
            return text + " translated"

        def update(offset, values):
            updates.append((offset, values))
            if offset == 1:
                release.set()

        with patch.object(translation, "translate_free", side_effect=translate):
            result = translation.translate_segments(
                {"provider": "free"}, ["first", "second"], "es", update)
        self.assertEqual(updates[0], (1, ["second translated"]))
        self.assertEqual(result, ["first translated", "second translated"])

    def test_parallel_batches_stream_and_schedule_past_a_slow_first_batch(self):
        release = threading.Event()
        updates = []
        texts = [str(i) for i in range(125)]

        def translate(config, batch, target):
            if batch[0] == "0" and not release.wait(2):
                raise ValueError("A slow batch blocked later work")
            return [text + " translated" for text in batch]

        def update(offset, values):
            updates.append((offset, values))
            if offset == 80:
                release.set()

        with patch.dict(os.environ, {"CUTPRO_POOL_ENDPOINT": "http://fixture",
                                    "CUTPRO_POOL_MODE": "1", "CUTPRO_POOL_PARALLELISM": "2"}), \
                patch.object(translation, "translate_model_batch", side_effect=translate):
            result = translation.translate_segments({"provider": "gemini"}, texts, "es", update)
        self.assertEqual(updates[0][0], 40)
        self.assertEqual(sum(len(values) for _, values in updates), 125)
        self.assertEqual(sorted(offset for offset, _ in updates), [0, 40, 80, 120])
        self.assertEqual(result, [text + " translated" for text in texts])

    def test_worker_streams_progress_and_keeps_final_timing_and_metadata(self):
        segments = [{"start": i, "end": i + 1, "text": "source %d" % i,
                     "mediaId": "source", "sourceIndex": i} for i in range(400)]
        output = io.StringIO()

        def translate(config, texts, target, update):
            translated = ["translated %d" % i for i in range(len(texts))]
            update(0, translated[:50])
            self.assertEqual(len(output.getvalue().splitlines()), 1)
            update(50, translated[50:])
            return translated

        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "transcript.json"
            path.write_text(json.dumps({"segments": segments, "target": "es"}), encoding="utf-8")
            with patch.object(translation.sys, "argv", ["worker", str(path)]), \
                    patch.object(translation.sys, "stdout", output), \
                    patch.object(translation, "provider_config", return_value={"provider": "free"}), \
                    patch.object(translation, "translate_segments", side_effect=translate):
                self.assertEqual(translation.main(), 0)
        reports = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(reports[0], {"type": "progress", "offset": 0,
                                      "texts": ["translated %d" % i for i in range(50)]})
        self.assertEqual(reports[-1]["language"], "es")
        self.assertEqual(len(reports[-1]["segments"]), 400)
        self.assertEqual(reports[-1]["segments"][399], {**segments[399], "text": "translated 399"})


if __name__ == "__main__":
    unittest.main()
