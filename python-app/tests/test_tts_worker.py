import asyncio
import contextlib
import importlib.util
import io
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import AsyncMock, patch

from aiohttp import ClientResponseError


spec = importlib.util.spec_from_file_location(
    'tts_worker', Path(__file__).resolve().parents[1] / 'tools/text_to_speech.py')
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)


class TtsWorkerTests(unittest.IsolatedAsyncioTestCase):
    @unittest.skipUnless(os.name == 'nt', 'Windows move compatibility')
    def test_completed_file_can_be_published_when_replace_reports_cross_volume(self):
        failure = OSError('redirected folder')
        failure.winerror = 17
        with tempfile.TemporaryDirectory() as folder:
            staging = Path(folder) / 'voice.part'
            target = Path(folder) / 'voice.mp3'
            audio = b'complete audio' * 128
            staging.write_bytes(audio)
            with patch.object(worker.os, 'replace', side_effect=failure):
                worker.publish_file(staging, target)
            self.assertEqual(target.read_bytes(), audio)
            self.assertFalse(staging.exists())

    def test_file_publication_keeps_permission_errors_visible(self):
        with patch.object(worker.os, 'replace', side_effect=PermissionError('access denied')):
            with self.assertRaises(PermissionError):
                worker.publish_file(Path('staging'), Path('target'))

    async def test_client_rejection_aborts_once_without_per_cue_retries(self):
        rejection = ClientResponseError(None, (), status=403, message='Forbidden')
        async def reject(*args):
            raise rejection
        synthesize = AsyncMock(side_effect=reject)
        delay = AsyncMock()
        cues = [{'text': f'Cue {i}', 'startMs': i * 2000, 'endMs': i * 2000 + 1000}
                for i in range(100)]
        with tempfile.TemporaryDirectory() as folder, \
                patch.object(worker, 'synthesize', synthesize), \
                patch.object(worker.asyncio, 'sleep', delay):
            with self.assertRaisesRegex(worker.VoiceServiceRejected, 'HTTP 403'):
                await worker.synthesize_segments(cues, 'en-US-JennyNeural', Path(folder), 4)
        self.assertLessEqual(synthesize.await_count, 4)
        delay.assert_not_awaited()

    async def test_transient_failure_retries_then_keeps_the_audio(self):
        attempts = 0
        async def synthesize(text, voice, output):
            nonlocal attempts
            attempts += 1
            if attempts == 1:
                raise TimeoutError('temporary network interruption')
            output.write_bytes(b'x' * 512)
        with tempfile.TemporaryDirectory() as folder, \
                patch.object(worker, 'synthesize', synthesize), \
                patch.object(worker.asyncio, 'sleep', AsyncMock()) as delay:
            target = Path(folder) / 'voice.mp3'
            await worker.synthesize_one('Hello', 'en-US-JennyNeural', target)
            self.assertTrue(worker.usable(target))
        self.assertEqual(attempts, 2)
        delay.assert_awaited_once()

    async def test_dedup_resume_and_streamed_cue_indexes(self):
        async def synthesize(text, voice, output):
            output.write_bytes(b'x' * 512)
        cues = [{'text': 'Repeated line', 'startMs': 0, 'endMs': 1000},
                {'text': 'Other line', 'startMs': 2000, 'endMs': 3000},
                {'text': 'Repeated line', 'startMs': 4000, 'endMs': 5000}]
        with tempfile.TemporaryDirectory() as folder, \
                patch.object(worker, 'synthesize', AsyncMock(side_effect=synthesize)) as synth, \
                contextlib.redirect_stdout(io.StringIO()) as stdout:
            results = await worker.synthesize_segments(cues, 'en-US-JennyNeural', Path(folder), 2)
            self.assertEqual(synth.await_count, 2)
            self.assertEqual([entry['index'] for entry in results], [0, 1, 2])
            self.assertEqual(stdout.getvalue().count('CUE '), 3)
            await worker.synthesize_segments(cues, 'en-US-JennyNeural', Path(folder), 2)
            self.assertEqual(synth.await_count, 2)


if __name__ == '__main__':
    unittest.main()
