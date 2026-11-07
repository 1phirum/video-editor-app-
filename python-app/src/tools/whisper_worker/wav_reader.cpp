#include "wav_reader.h"

#include <QFile>

#include <cstdint>
#include <cstring>

namespace {

// Little-endian reads. The WAV byte order is fixed, so this never depends on the
// host's endianness the way a reinterpret_cast would.
quint32 readU32(const uchar *p) {
  return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) |
         (quint32(p[3]) << 24);
}
quint16 readU16(const uchar *p) {
  return quint16(p[0]) | (quint16(p[1]) << 8);
}

} // namespace

namespace WavReader {

Audio load(const QString &path) {
  Audio audio;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    audio.error = QStringLiteral("could not open %1").arg(path);
    return audio;
  }
  const QByteArray bytes = file.readAll();
  file.close();

  const uchar *data = reinterpret_cast<const uchar *>(bytes.constData());
  const qint64 size = bytes.size();
  if (size < 12 || std::memcmp(data, "RIFF", 4) != 0 ||
      std::memcmp(data + 8, "WAVE", 4) != 0) {
    audio.error = QStringLiteral("not a RIFF/WAVE file");
    return audio;
  }

  quint16 channels = 0;
  quint16 bitsPerSample = 0;
  quint32 sampleRate = 0;
  qint64 dataOffset = -1;
  quint32 dataSize = 0;

  // Walk the chunk list from just past the 12-byte RIFF header. Each chunk is a
  // 4-byte id, a 4-byte little-endian size, then that many bytes, padded to an
  // even boundary.
  qint64 pos = 12;
  while (pos + 8 <= size) {
    const uchar *chunk = data + pos;
    const quint32 chunkSize = readU32(chunk + 4);
    const qint64 body = pos + 8;
    if (std::memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16 &&
        body + 16 <= size) {
      channels = readU16(data + body + 2);
      sampleRate = readU32(data + body + 4);
      bitsPerSample = readU16(data + body + 14);
    } else if (std::memcmp(chunk, "data", 4) == 0) {
      dataOffset = body;
      // A streamed WAV can carry 0xFFFFFFFF or an over-long size; clamp to what
      // is actually present so we never read past the buffer.
      dataSize = quint32(qMin<qint64>(chunkSize, size - body));
    }
    pos = body + chunkSize + (chunkSize & 1); // pad to even
  }

  if (channels == 0 || sampleRate == 0 || bitsPerSample != 16) {
    audio.error = QStringLiteral("unsupported WAV format (need 16-bit PCM)");
    return audio;
  }
  if (dataOffset < 0 || dataSize == 0) {
    audio.error = QStringLiteral("no audio samples in WAV");
    return audio;
  }

  const qint64 totalSamples = qint64(dataSize) / 2; // int16 count
  const qint64 frames = totalSamples / channels;
  audio.sampleRate = int(sampleRate);
  audio.samples.reserve(size_t(frames));

  const uchar *pcm = data + dataOffset;
  for (qint64 frame = 0; frame < frames; ++frame) {
    int accumulator = 0;
    for (quint16 channel = 0; channel < channels; ++channel) {
      const uchar *sample = pcm + (frame * channels + channel) * 2;
      accumulator += qint16(readU16(sample));
    }
    audio.samples.push_back(float(accumulator) /
                            (32768.0f * float(channels)));
  }

  audio.ok = true;
  return audio;
}

} // namespace WavReader
