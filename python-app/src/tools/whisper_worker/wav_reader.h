#pragma once

#include <QString>

#include <vector>

// Reads the 16 kHz mono PCM s16le WAV the app's prefetcher (and this worker's own
// ffmpeg fallback) writes, into the normalized float samples whisper.cpp wants.
//
// The format is fixed by window_audio_prefetcher.cpp / whisper_transcribe.py's
// extract_window(): `-ac 1 -ar 16000 -c:a pcm_s16le -f wav`. The reader is still
// written defensively - it walks the RIFF chunk list rather than assuming a bare
// 44-byte header - because ffmpeg is free to add a chunk (LIST/fact) ahead of the
// samples and a fixed offset would then read audio as header bytes.
namespace WavReader {

struct Audio {
  std::vector<float> samples; // mono, [-1, 1)
  int sampleRate = 0;
  bool ok = false;
  QString error;
};

// Loads and normalizes. Multi-channel input is downmixed to mono by averaging, so
// a stray stereo WAV still decodes rather than transcribing at half speed.
Audio load(const QString &path);

} // namespace WavReader
