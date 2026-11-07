#pragma once

#include "mcp_catalog.h"
#include <QDateTime>
#include <QJsonArray>
#include <QTcpServer>
#include <QVariantList>

class QTcpSocket;

// Lives with Backend on the GUI thread. Sockets are asynchronous; imported
// media and long jobs delegate to the existing editor workers. This adapter
// must never drive decode/render state or move Backend to a network thread.
class McpServer final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool running READ running NOTIFY stateChanged)
  Q_PROPERTY(int port READ port NOTIFY stateChanged)
  Q_PROPERTY(QString endpoint READ endpoint NOTIFY stateChanged)
  Q_PROPERTY(QString connectionKey READ connectionKey NOTIFY stateChanged)
  Q_PROPERTY(QString status READ status NOTIFY stateChanged)
  Q_PROPERTY(QString lastClient READ lastClient NOTIFY activityChanged)
  Q_PROPERTY(QString lastSeen READ lastSeen NOTIFY activityChanged)
  Q_PROPERTY(QVariantList activities READ activities NOTIFY activityChanged)
  Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
  Q_PROPERTY(QVariantList toolGroups READ toolGroups CONSTANT)
  Q_PROPERTY(int toolCount READ toolCount CONSTANT)
public:
  explicit McpServer(QObject *backend, QObject *keyframes = nullptr,
                     QObject *tts = nullptr, QObject *parent = nullptr);
  bool running() const { return m_listener.isListening(); }
  int port() const { return running() ? m_listener.serverPort() : m_port; }
  QString endpoint() const;
  QString connectionKey() const { return m_key; }
  QString status() const { return m_status; }
  QString lastClient() const { return m_lastClient; }
  QString lastSeen() const { return m_lastSeen.isValid() ? m_lastSeen.toLocalTime().toString("hh:mm:ss") : QString(); }
  QVariantList activities() const { return m_activities; }
  QVariantList jobs() const;
  QVariantList toolGroups() const;
  int toolCount() const { return m_catalog.actions().size() + 2; }
  // In-process assistant adapter, on the GUI thread. These are deliberately
  // not Q_INVOKABLE; external callers still use the authenticated transport.
  QJsonArray editorTools() const;
  QJsonObject editorState(const QJsonObject &arguments = {}) const;
  QString validateEditorTool(const QString &name, const QJsonObject &arguments) const;
  QJsonObject invokeEditorTool(const QString &name, const QJsonObject &arguments);
  void applySettings(const QVariantMap &settings);
  // Public for isolated test/configuration; production uses normalized settings.
  bool setListening(bool enabled, int port);
  Q_INVOKABLE bool copyConnectionDetails(const QString &kind);
  Q_INVOKABLE bool cancelJob(const QString &id);
  Q_INVOKABLE void clearActivity();
signals:
  void stateChanged();
  void activityChanged();
  void jobsChanged();
private slots:
  void updateJobs();
private:
  bool loadKey();
  void acceptConnections();
  void consume(QTcpSocket *socket);
  void respond(QTcpSocket *socket, int status, const QByteArray &body = {});
  QJsonObject dispatch(const QJsonObject &request);
  QJsonObject getState(const QJsonObject &args) const;
  QJsonObject writeProperty(const QJsonObject &args);
  void recordAction(const QString &name, bool failed);
  QPointer<QObject> m_backend;
  QPointer<QObject> m_tts;
  McpCatalog m_catalog;
  QTcpServer m_listener;
  QHash<QTcpSocket *, QByteArray> m_buffers;
  int m_port = 20129;
  QString m_key, m_status = QStringLiteral("Off"), m_lastClient;
  QDateTime m_lastSeen;
  QVariantList m_activities;
};
