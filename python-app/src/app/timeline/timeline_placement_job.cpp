#include "app/timeline/timeline_placement_job.h"

TimelinePlacementJob::TimelinePlacementJob(QObject *parent) : QObject(parent) {
  m_timer.setSingleShot(true);
  connect(&m_timer, &QTimer::timeout, this, &TimelinePlacementJob::step);
}

void TimelinePlacementJob::setState(double progress, const QString &status) {
  m_progress = qBound(0.0, progress, 1.0);
  m_status = status;
  emit stateChanged();
}

void TimelinePlacementJob::start(const QVariantList &items) {
  if (m_inProgress || items.isEmpty())
    return;
  m_items = items;
  m_index = 0;
  m_cancelRequested = false;
  m_inProgress = true;
  setState(0.0, QStringLiteral("Preparing timeline..."));
  emit stateChanged();
  // Placement only mutates project metadata. The drop handler already queues
  // start() until native drag cleanup is complete, so no artificial paint delay
  // is needed here.
  m_timer.start(0);
}

void TimelinePlacementJob::cancel() {
  if (!m_inProgress)
    return;
  m_cancelRequested = true;
  m_timer.stop();
  m_inProgress = false;
  setState(m_progress, QStringLiteral("Timeline placement cancelled"));
  emit finished(false);
}

void TimelinePlacementJob::step() {
  if (!m_inProgress)
    return;
  if (m_cancelRequested || m_index >= m_items.size()) {
    m_inProgress = false;
    setState(1.0, QStringLiteral("Timeline ready"));
    emit finished(true);
    return;
  }

  const QVariantMap item = m_items.at(m_index).toMap();
  emit stepRequested(item);
  ++m_index;
  setState(static_cast<double>(m_index) / m_items.size(),
           QStringLiteral("Placing clip %1 of %2...")
               .arg(m_index)
               .arg(m_items.size()));
  m_timer.start(0);
}
