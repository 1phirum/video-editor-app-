// The Google account pool ("GodMode Pool"): signed-in Google accounts whose
// own Gemini quota serves transcription and translation, rotated per request.
// All mechanics live in tools/pool_tool.py (stdlib only) - this class drives
// that worker over QProcess, exactly like the other Python workers, and
// exposes the state to QML as the `Pool` singleton.
#ifndef CUTPRO_POOL_MANAGER_H
#define CUTPRO_POOL_MANAGER_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "core/module_api.h"
#if defined(cutpro_pool_EXPORTS)
#define CUTPRO_POOL_API
#else
#define CUTPRO_POOL_API CUTPRO_IMPORTED
#endif

namespace cutpro::pool {

// The loopback endpoint when the pool is on and at least one account is ready,
// or an empty string - callers hand it to the Python workers through
// CUTPRO_POOL_ENDPOINT and the workers fall back to API keys when it is empty.
QString endpointIfReady();

} // namespace cutpro::pool

class CUTPRO_POOL_API PoolManager : public QObject {
  Q_OBJECT
  // Mirrors tools/pool_tool.py state: core, endpoint, strategy, accounts,
  // readyCount, servedTotal.
  Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(bool refreshing READ refreshing NOTIFY refreshingChanged)
  Q_PROPERTY(QString poolMessage READ poolMessage NOTIFY poolMessageChanged)
  Q_PROPERTY(QString messageKind READ messageKind NOTIFY poolMessageChanged)
  Q_PROPERTY(bool signingIn READ signingIn NOTIFY signInChanged)
  Q_PROPERTY(QString authUrl READ authUrl NOTIFY signInChanged)
  Q_PROPERTY(QStringList models READ models NOTIFY modelsChanged)
  Q_PROPERTY(bool checkingModels READ checkingModels NOTIFY modelCheckChanged)
  Q_PROPERTY(QString modelStatus READ modelStatus NOTIFY modelCheckChanged)

public:
  static PoolManager &instance();
  explicit PoolManager(QObject *parent = nullptr);
  ~PoolManager() override;

  QVariantMap state() const { return m_state; }
  bool busy() const { return m_busy; }
  bool refreshing() const { return m_stateTool.state() != QProcess::NotRunning; }
  QString poolMessage() const { return m_message; }
  QString messageKind() const { return m_messageKind; }
  bool signingIn() const { return m_signingIn; }
  QString authUrl() const { return m_authUrl; }
  QStringList models() const { return m_models; }
  bool checkingModels() const { return m_checkingModels; }
  QString modelStatus() const { return m_modelStatus; }

  // Requests a Core start when ready accounts are available.
  Q_INVOKABLE void ensureCore();
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void addAccount();
  Q_INVOKABLE void removeAccount(const QString &accountId);
  Q_INVOKABLE void setAccountEnabled(const QString &accountId, bool enabled);
  Q_INVOKABLE void verifyAll();
  Q_INVOKABLE void verifyAccount(const QString &accountId);
  Q_INVOKABLE void setStrategy(const QString &strategy);
  Q_INVOKABLE void reopenSignIn();
  Q_INVOKABLE void cancelSignIn();
  Q_INVOKABLE void clearMessage();
  Q_INVOKABLE void stopCore();
  Q_INVOKABLE void checkModels();

signals:
  void stateChanged();
  void busyChanged();
  void refreshingChanged();
  void poolMessageChanged();
  void signInChanged();
  void modelsChanged();
  void modelCheckChanged();
  void modelsChecked(bool ok, const QStringList &models);

private:
  QString workerPath() const;
  // Account commands use their own process; polling uses m_stateTool.
  void runTool(const QStringList &arguments);
  void setMessage(const QString &message, const QString &kind = QStringLiteral("info"));
  void finishRun();
  void applyState(const QByteArray &stdoutData);
  void readToolOutput();
  void consumeToolOutput(bool final);
  void startCoreIfReady();
  void readCoreOutput(bool final = false);
  void finishRefresh(int exitCode, QProcess::ExitStatus status);
  void readModelOutput(bool final = false);

  QProcess m_tool;
  QProcess m_stateTool; // background polling never blocks account actions
  QProcess m_core; // the long-lived rotation server
  QProcess m_modelsTool;
  QTimer m_stateTimeout;
  QTimer m_modelsTimeout;
  QByteArray m_stateOutput;
  QByteArray m_toolOutput; // newline-delimited JSON, including partial reads
  QByteArray m_coreOutput;
  QVariantMap m_result;
  QString m_command;
  QString m_authUrl;
  QVariantMap m_state;
  QString m_message;
  QString m_messageKind = QStringLiteral("info");
  QString m_refreshError;
  QStringList m_models;
  QString m_modelStatus;
  QByteArray m_modelsOutput;
  QVariantMap m_modelsResult;
  bool m_checkingModels = false;
  bool m_busy = false;
  bool m_signingIn = false;
  bool m_cancelled = false;
  bool m_autoStartCore = true;
  bool m_stateTimedOut = false;
  quint64 m_stateGeneration = 0;
  quint64 m_refreshGeneration = 0;
  int m_healthMisses = 0;
};

#endif // CUTPRO_POOL_MANAGER_H
