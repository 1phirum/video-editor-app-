#pragma once

#include "ai_message_model.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariantList>

class McpServer;
class QNetworkReply;

// Launcher-side orchestration. Pool credentials stay in the existing router;
// all editor calls stay on the Backend/McpServer GUI thread.
class AiAssistant final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QAbstractItemModel *messages READ messages CONSTANT)
  Q_PROPERTY(QVariantList actions READ actions NOTIFY actionsChanged)
  Q_PROPERTY(QVariantList toolGroups READ toolGroups CONSTANT)
  Q_PROPERTY(QStringList models READ models NOTIFY resourcesChanged)
  Q_PROPERTY(QString model READ model WRITE setModel NOTIFY resourcesChanged)
  Q_PROPERTY(bool ready READ ready NOTIFY resourcesChanged)
  Q_PROPERTY(QString resourceStatus READ resourceStatus NOTIFY resourcesChanged)
  Q_PROPERTY(QString state READ state NOTIFY stateChanged)
  Q_PROPERTY(QString status READ status NOTIFY stateChanged)
  Q_PROPERTY(QString error READ error NOTIFY stateChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
  Q_PROPERTY(bool awaitingReview READ awaitingReview NOTIFY stateChanged)
  Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
  Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY stateChanged)
  Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY contextChanged)
  Q_PROPERTY(QString contextLabel READ contextLabel NOTIFY contextChanged)
  Q_PROPERTY(int selectionCount READ selectionCount NOTIFY contextChanged)
  Q_PROPERTY(bool hasMedia READ hasMedia NOTIFY contextChanged)
public:
  AiAssistant(QObject *backend, McpServer *editor, QObject *pool, QObject *parent = nullptr);
  ~AiAssistant() override;
  QAbstractItemModel *messages() { return &m_messages; }
  QVariantList actions() const;
  QVariantList toolGroups() const;
  QStringList models() const { return m_models; }
  QString model() const { return m_model; }
  void setModel(const QString &model);
  bool ready() const;
  QString resourceStatus() const;
  QString state() const { return m_state; }
  QString status() const { return m_status; }
  QString error() const { return m_error; }
  bool busy() const;
  bool awaitingReview() const { return m_state == "review"; }
  double progress() const { return m_progress; }
  QString mode() const { return m_mode; }
  void setMode(const QString &mode);
  QString scope() const { return m_scope; }
  void setScope(const QString &scope);
  QString contextLabel() const;
  int selectionCount() const;
  bool hasMedia() const { return m_backend && m_backend->property("mediaCount").toInt() > 0; }
  Q_INVOKABLE bool send(const QString &text);
  Q_INVOKABLE void applyPlan();
  Q_INVOKABLE void stop();
  Q_INVOKABLE void newConversation();
  Q_INVOKABLE void refreshResources();
  Q_INVOKABLE void undoLastEdit();
signals:
  void resourcesChanged();
  void stateChanged();
  void actionsChanged();
  void contextChanged();
private slots:
  void updateResources();
  void onContextChanged();
  void onJobsChanged();
  void onBackendFailure(const QString &title, const QString &reason);
  void onBackendError();
private:
  struct Action {
    QString tool, label, wrapper, status = "pending";
    QJsonObject arguments;
    QString targets;
  };
  void setState(const QString &state, const QString &status);
  void fail(const QString &message);
  void requestModel();
  void receiveModel(const QJsonObject &response);
  void executeNext();
  void finishAction();
  void appendResponses();
  QJsonArray availableTools() const;
  QJsonArray declarations() const;
  QJsonObject context() const;
  QUrl poolEndpoint() const;
  QStringList selectedIds() const;
  QString validateAction(const QString &name, const QJsonObject &arguments) const;
  QString actionTargets(const QJsonObject &arguments) const;
  bool sameProject() const;
  bool readOnly(const QString &name) const;
  void addResponse(const QString &wrapper, const QJsonObject &result);
  QPointer<QObject> m_backend, m_pool;
  QPointer<McpServer> m_editor;
  AiMessageModel m_messages;
  QNetworkAccessManager m_network;
  QPointer<QNetworkReply> m_reply;
  QTimer m_jobTimer;
  QByteArray m_replyData;
  QJsonArray m_contents, m_responses;
  QVector<Action> m_actions;
  QSet<QString> m_ownedJobs;
  QStringList m_models, m_selectedIds;
  QString m_model, m_state = "idle", m_status, m_error;
  QString m_mode = "Edit", m_scope = "sequence";
  QString m_projectId, m_sequenceId, m_jobError, m_errorBeforeJob;
  double m_progress = -1;
  quint64 m_generation = 0, m_revision = 0, m_plannedRevision = 0;
  int m_round = 0, m_calls = 0, m_actionIndex = 0;
  bool m_invoking = false;
};
