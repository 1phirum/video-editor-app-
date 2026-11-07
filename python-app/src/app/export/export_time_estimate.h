#pragma once

#include <QList>
#include <QtGlobal>
#include <cmath>

// Export-wide monotonic samples survive FFmpeg process/chunk resets. Explicit
// time inputs make startup, sustained slowdown and joining deterministic tests.
class ExportTimeEstimate {
public:
  void start(qint64 now, qint64 duration, double previousJoinSpeed = 0) {
    m_started = m_phaseStarted = now;
    m_total = qMax<qint64>(1, duration);
    m_done = 0;
    m_speed = 0;
    m_joinSpeed = qIsFinite(previousJoinSpeed) ? qMax(0.0, previousJoinSpeed) : 0;
    m_finalizing = false;
    m_samples = {{now, 0}};
  }
  void sample(qint64 now, qint64 done) {
    m_done = qMax(m_done, qBound(qint64(0), done, m_total));
    const qint64 elapsed = now - m_phaseStarted;
    if (m_finalizing) {
      if (elapsed >= 1000 && m_done >= 500)
        m_speed = double(m_done) / elapsed;
      return;
    }
    if (now > m_samples.last().wall) m_samples.append({now, m_done});
    while (m_samples.size() > 2 && (now - m_samples[1].wall > 30000 || m_samples.size() > 120))
      m_samples.removeFirst();
    if (now - m_started < 2000 || m_done < 500) return;
    const auto first = m_samples.first();
    const double average = double(m_done) / (now - m_started);
    const double recent = now > first.wall ? double(m_done - first.done) / (now - first.wall) : average;
    m_speed = average * 0.3 + recent * 0.7;
  }
  void beginFinalization(qint64 now) {
    m_finalizing = true;
    m_phaseStarted = now;
    m_done = 0;
    m_speed = m_joinSpeed;
  }
  double speed() const { return m_speed; }
  bool includesFinalization() const { return m_finalizing || m_joinSpeed > 0; }
  qint64 remainingSeconds() const {
    if (!qIsFinite(m_speed) || m_speed <= 0) return -1;
    double remainingMs = (m_total - m_done) / m_speed;
    if (!m_finalizing && m_joinSpeed > 0) remainingMs += m_total / m_joinSpeed;
    return qint64(std::ceil(remainingMs / 1000.0));
  }
  double completedJoinSpeed(qint64 now) const {
    return m_finalizing && now > m_phaseStarted ? double(m_total) / (now - m_phaseStarted) : 0;
  }
private:
  struct Sample { qint64 wall; qint64 done; };
  QList<Sample> m_samples;
  qint64 m_started = 0, m_phaseStarted = 0, m_total = 1, m_done = 0;
  double m_speed = 0, m_joinSpeed = 0;
  bool m_finalizing = false;
};
