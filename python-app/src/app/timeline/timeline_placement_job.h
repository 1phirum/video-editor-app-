#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "core/module_api.h"

// Schedules timeline insertion in small event-loop steps so QML can keep
// painting its loading state while a batch of clips is placed.
class CUTPRO_TIMELINE_API TimelinePlacementJob final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool inProgress READ inProgress NOTIFY stateChanged)
  Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
  Q_PROPERTY(QString status READ status NOTIFY stateChanged)

public:
  explicit TimelinePlacementJob(QObject *parent = nullptr);
  bool inProgress() const { return m_inProgress; }
  double progress() const { return m_progress; }
  QString status() const { return m_status; }

  void start(const QVariantList &items);
  void cancel();

signals:
  void stateChanged();
  void stepRequested(const QVariantMap &item);
  void finished(bool success);

private:
  void step();
  void setState(double progress, const QString &status);

  QTimer m_timer;
  QVariantList m_items;
  int m_index = 0;
  bool m_inProgress = false;
  bool m_cancelRequested = false;
  double m_progress = 0.0;
  QString m_status;
};
