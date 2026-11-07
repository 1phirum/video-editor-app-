#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QtGlobal>

// FFmpeg progress arrives in arbitrary pipe chunks, separately from diagnostics.
class ExportProgress {
public:
  void reset() {
    m_tail.clear();
    outTimeMs = 0;
    framesPerSecond = speed = bytesPerSecond = 0;
    m_size = m_lastSize = m_lastSampleMs = 0;
    m_clock.start();
  }

  bool consume(const QByteArray &chunk) {
    m_tail += chunk;
    bool published = false;
    qsizetype consumed = 0;
    for (;;) {
      const qsizetype newline = m_tail.indexOf('\n', consumed);
      if (newline < 0)
        break;
      const QByteArray line = m_tail.mid(consumed, newline - consumed).trimmed();
      consumed = newline + 1;
      const qsizetype equals = line.indexOf('=');
      if (equals < 0)
        continue;
      const QByteArray key = line.left(equals);
      const QByteArray value = line.mid(equals + 1);
      bool ok = false;
      if (key == "out_time_us" || key == "out_time_ms") {
        // Both keys carry microseconds, including FFmpeg's historical "ms" key.
        const qint64 micros = value.toLongLong(&ok);
        if (ok)
          outTimeMs = qMax<qint64>(0, micros / 1000);
      } else if (key == "fps") {
        const double parsed = value.toDouble(&ok);
        if (ok && qIsFinite(parsed))
          framesPerSecond = qMax(0.0, parsed);
      } else if (key == "speed") {
        const double parsed = value.endsWith('x')
                                  ? value.chopped(1).toDouble(&ok)
                                  : value.toDouble(&ok);
        if (ok && qIsFinite(parsed))
          speed = qMax(0.0, parsed);
      } else if (key == "total_size") {
        const qint64 parsed = value.toLongLong(&ok);
        if (ok)
          m_size = qMax<qint64>(0, parsed);
      } else if (key == "progress") {
        const qint64 now = m_clock.isValid() ? m_clock.elapsed() : 0;
        if (now > m_lastSampleMs && m_size >= m_lastSize) {
          const double sample = double(m_size - m_lastSize) * 1000 /
                                double(now - m_lastSampleMs);
          bytesPerSecond = bytesPerSecond > 0
                               ? bytesPerSecond * 0.65 + sample * 0.35
                               : sample;
          m_lastSampleMs = now;
          m_lastSize = m_size;
        }
        published = true;
      }
    }
    if (consumed)
      m_tail.remove(0, consumed);
    // Progress keys are small. A malformed stream must not become a log buffer.
    if (m_tail.size() > 8192)
      m_tail = m_tail.right(8192);
    return published;
  }

  qint64 outTimeMs = 0;
  double framesPerSecond = 0;
  double speed = 0;
  double bytesPerSecond = 0;

private:
  QByteArray m_tail;
  QElapsedTimer m_clock;
  qint64 m_size = 0;
  qint64 m_lastSize = 0;
  qint64 m_lastSampleMs = 0;
};
