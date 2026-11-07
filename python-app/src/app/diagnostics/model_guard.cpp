#include "app/diagnostics/model_guard.h"

#include <QDebug>
#include <QList>
#include <QStringList>

#include <algorithm>

ModelGuard::ModelGuard(QObject *parent) : QObject(parent) {}

ModelGuard &ModelGuard::instance() {
  static ModelGuard guard;
  return guard;
}

int ModelGuard::bound(int wanted, int cap, const QString &key) {
  if (cap < 0)
    cap = 0;
  // A model of NaN or of a negative number is a bug of its own; QML turns the
  // first into 0 silently and the second into "no delegates", so neither is
  // visible without this.
  const int asked = wanted > 0 ? wanted : 0;
  const int given = std::min(asked, cap);

  Record &record = m_records[key];
  ++record.calls;
  record.lastWanted = asked;
  record.cap = cap;
  record.peakWanted = std::max(record.peakWanted, asked);

  if (asked > cap) {
    ++record.clamps;
    ++m_totalClamps;
    // First clamp for a key is always reported; after that only an order of
    // magnitude more is, so a ruler that sits slightly over its cap while the
    // user zooms does not fill the log.
    const bool escalated =
        record.reportedAt > 0 && asked >= record.reportedAt * kEscalationFactor;
    if (!record.reported || escalated) {
      record.reported = true;
      record.reportedAt = asked;
      qWarning().noquote()
          << "ModelGuard: clamped" << key << "from" << asked << "to" << cap
          << "- a Repeater model this wide freezes the GUI thread inside "
             "QQuickRepeater::clear(); the expression behind this key is not "
             "bounded by the width it draws into";
    }
  }
  return given;
}

int ModelGuard::note(const QString &key, int count) {
  Record &record = m_records[key];
  ++record.calls;
  record.lastWanted = count;
  record.peakWanted = std::max(record.peakWanted, count);
  return count;
}

QString ModelGuard::report() const {
  struct Row {
    QString key;
    Record record;
  };
  QList<Row> rows;
  rows.reserve(m_records.size());
  for (auto it = m_records.cbegin(); it != m_records.cend(); ++it)
    rows.append({it.key(), it.value()});
  std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
    if (a.record.clamps != b.record.clamps)
      return a.record.clamps > b.record.clamps;
    return a.record.peakWanted > b.record.peakWanted;
  });

  QStringList lines;
  lines << QStringLiteral("guarded models: %1 keys, %2 clamps total")
               .arg(m_records.size())
               .arg(m_totalClamps);
  for (const Row &row : rows) {
    lines << QStringLiteral("  %1  peak %2  cap %3  clamps %4  calls %5")
                 .arg(row.key, -34)
                 .arg(row.record.peakWanted, 8)
                 .arg(row.record.cap, 6)
                 .arg(row.record.clamps, 6)
                 .arg(row.record.calls, 7);
  }
  return lines.join(QLatin1Char('\n'));
}

QVariantMap ModelGuard::statistics() const {
  QVariantMap map;
  map.insert(QStringLiteral("guardKeys"), m_records.size());
  map.insert(QStringLiteral("guardClamps"), m_totalClamps);
  QString worstKey;
  int worstWanted = 0;
  for (auto it = m_records.cbegin(); it != m_records.cend(); ++it) {
    if (it.value().peakWanted > worstWanted) {
      worstWanted = it.value().peakWanted;
      worstKey = it.key();
    }
  }
  map.insert(QStringLiteral("guardWorstKey"), worstKey);
  map.insert(QStringLiteral("guardWorstWanted"), worstWanted);
  return map;
}

void ModelGuard::reset() {
  m_records.clear();
  m_totalClamps = 0;
}
