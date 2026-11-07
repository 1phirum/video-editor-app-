// PoolManager drives the stdlib pool worker and exposes its state to QML.
#include "app/core_app/pool_manager.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "app/media/python_runtime.h"

namespace {
QVariantMap parseJson(const QByteArray &data) {
  const QJsonDocument document = QJsonDocument::fromJson(data);
  return document.isObject() ? document.object().toVariantMap() : QVariantMap{};
}

std::unique_ptr<QSettings> settingsStore() {
  const QString testFile = qEnvironmentVariable("CUTPRO_SETTINGS_FILE");
  if (!testFile.isEmpty())
    return std::make_unique<QSettings>(testFile, QSettings::IniFormat);
  return std::make_unique<QSettings>();
}

QString poolInterpreter() {
  const QString configured = settingsStore()
                                 ->value(QStringLiteral("Preferences/pythonExecutable"))
                                 .toString().trimmed();
  QString interpreter = PythonRuntime::executable(
      configured.isEmpty() ? QStringLiteral("python") : configured);
  // pythonw may leave sys.stdout/sys.stderr unset even with redirected pipes.
  // Use its console sibling for JSON IPC; QProcess hides its console below.
  const QFileInfo info(interpreter);
  if (info.fileName().compare(QStringLiteral("pythonw.exe"), Qt::CaseInsensitive) == 0) {
    const QString console = info.dir().filePath(QStringLiteral("python.exe"));
    if (QFileInfo::exists(console))
      interpreter = console;
  }
  return interpreter;
}

void configureProcess(QProcess &process) {
  process.setProcessChannelMode(QProcess::SeparateChannels);
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
  environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
  process.setProcessEnvironment(environment);
#ifdef Q_OS_WIN
  process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
    args->flags |= CREATE_NO_WINDOW;
    args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
    args->startupInfo->wShowWindow = SW_HIDE;
  });
#endif
}
} // namespace

namespace cutpro::pool {
QString endpointIfReady() {
  const QVariantMap state = PoolManager::instance().state();
  if (state.value("core").toString() != QStringLiteral("running") ||
      state.value("readyCount").toInt() <= 0)
    return {};
  return QStringLiteral("http://%1").arg(state.value("endpoint").toString());
}
} // namespace cutpro::pool

PoolManager &PoolManager::instance() {
  static PoolManager manager;
  return manager;
}

PoolManager::PoolManager(QObject *parent) : QObject(parent) {
  configureProcess(m_tool);
  configureProcess(m_stateTool);
  configureProcess(m_core);
  configureProcess(m_modelsTool);
  m_modelsTimeout.setSingleShot(true);
  m_modelsTimeout.setInterval(300000);
  connect(&m_modelsTimeout, &QTimer::timeout, this, [this] {
    m_modelStatus = tr("The model check timed out. Try checking again.");
    m_modelsTool.kill();
  });
  connect(&m_modelsTool, &QProcess::readyReadStandardOutput, this, [this] { readModelOutput(); });
  connect(&m_modelsTool, &QProcess::readyReadStandardError, this, [this] { m_modelsTool.readAllStandardError(); });
  connect(&m_modelsTool, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart)
      return;
    m_modelsTimeout.stop();
    m_checkingModels = false;
    m_modelStatus = tr("The model checker could not start. Check Python in Settings.");
    emit modelCheckChanged();
    emit modelsChecked(false, {});
  });
  connect(&m_modelsTool, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
    m_modelsTimeout.stop();
    readModelOutput(true);
    QStringList found;
    for (const auto &value : m_modelsResult.value("models").toList())
      found.append(value.toString());
    const bool ok = code == 0 && status == QProcess::NormalExit && !found.isEmpty();
    if (ok) {
      m_models = found;
      emit modelsChanged();
      m_modelStatus = tr("%1 models found across %2 of %3 accounts.")
                          .arg(found.size()).arg(m_modelsResult.value("checkedCount").toInt())
                          .arg(m_modelsResult.value("accountCount").toInt());
    } else if (m_modelsResult.contains("error")) {
      m_modelStatus = m_modelsResult.value("error").toString();
    } else if (status == QProcess::NormalExit) {
      m_modelStatus = m_modelsResult.value("checkedCount").toInt() > 0
                          ? tr("Models checked. Enable an account to use its models.")
                          : tr("No models could be checked. See the account details below.");
    }
    const QVariantList errors = m_modelsResult.value("errors").toList();
    for (const auto &error : errors) {
      const auto detail = error.toMap();
      m_modelStatus += QStringLiteral("\n%1: %2").arg(detail.value("email").toString(), detail.value("message").toString());
    }
    m_checkingModels = false;
    ++m_stateGeneration;
    emit modelCheckChanged();
    emit modelsChecked(ok, found);
    refresh();
  });
  connect(&m_stateTool, &QProcess::stateChanged, this, [this](QProcess::ProcessState) {
    emit refreshingChanged();
  });
  m_stateTimeout.setSingleShot(true);
  m_stateTimeout.setInterval(5000);
  connect(&m_stateTimeout, &QTimer::timeout, this, [this] {
    m_stateTimedOut = true;
    m_stateTool.kill();
  });
  connect(&m_stateTool, &QProcess::readyReadStandardOutput, this, [this] {
    m_stateOutput.append(m_stateTool.readAllStandardOutput());
  });
  connect(&m_stateTool, &QProcess::readyReadStandardError, this, [this] {
    m_stateTool.readAllStandardError();
  });
  connect(&m_stateTool, &QProcess::finished, this, &PoolManager::finishRefresh);
  connect(&m_stateTool, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart)
      return;
    m_stateTimeout.stop();
    m_refreshError = tr("The pool worker could not be started. Check the Python executable in Settings.");
    setMessage(m_refreshError, QStringLiteral("error"));
  });
  connect(&m_tool, &QProcess::readyReadStandardOutput, this, &PoolManager::readToolOutput);
  // Drain stderr without displaying it: the worker returns actionable JSON errors.
  connect(&m_tool, &QProcess::readyReadStandardError, this, [this] {
    m_tool.readAllStandardError();
  });
  connect(&m_tool, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart)
      return;
    setMessage(tr("The pool worker could not be started. Check the Python executable in Settings."),
               QStringLiteral("error"));
    finishRun();
  });
  connect(&m_tool, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
    readToolOutput();
    consumeToolOutput(true);
    const bool cancelled = m_cancelled;
    const QVariantMap result = m_result;
    if (!cancelled) {
      if (result.contains("error"))
        setMessage(result.value("error").toString(), QStringLiteral("error"));
      else if (status != QProcess::NormalExit || exitCode != 0 || result.isEmpty())
        setMessage(tr("The pool worker stopped without a result. Check Python in Settings and try again."),
                   QStringLiteral("error"));
      else if (result.contains("added")) {
        m_autoStartCore = true;
        setMessage(result.value("status").toString() == QStringLiteral("ready")
                       ? tr("Account added. Antigravity access is verified.")
                       : tr("Account connected. Open Accounts to finish Antigravity setup or recheck access."),
                   result.value("status").toString() == QStringLiteral("ready") ? QStringLiteral("success") : QStringLiteral("info"));
      } else if (result.contains("removed"))
        setMessage(tr("Account removed from the pool."), QStringLiteral("success"));
      else if (result.contains("updated"))
        setMessage(result.value("enabled").toBool() ? tr("Account enabled for new requests.")
                                                  : tr("Account disabled. Current requests can finish."),
                   QStringLiteral("success"));
      else if (result.contains("verified"))
        setMessage(tr("Verification finished. Check each account's status for details."), QStringLiteral("success"));
    }
    finishRun(); // release the command before starting a state refresh
    refresh();
  });
  connect(&m_core, &QProcess::started, this, [this] {
    QTimer::singleShot(400, this, &PoolManager::refresh);
  });
  connect(&m_core, &QProcess::readyReadStandardOutput, this, [this] {
    readCoreOutput();
  });
  connect(&m_core, &QProcess::readyReadStandardError, this, [this] {
    m_core.readAllStandardError();
  });
  connect(&m_core, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart)
      return; // Crashes and an intentional stop are handled by finished.
    m_autoStartCore = false;
    setMessage(tr("The pool router could not start. Check Python in Settings."), QStringLiteral("error"));
  });
  connect(&m_core, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
    readCoreOutput(true);
    // Invalidate an in-progress health poll and stop sending indicators now,
    // rather than waiting for the next Python status check to complete.
    ++m_stateGeneration;
    m_state["core"] = "stopped";
    m_state["activeRequests"] = QVariantList{};
    m_state["inFlight"] = 0;
    auto accounts = m_state.value("accounts").toList();
    for (auto &value : accounts) {
      auto account = value.toMap();
      account["inFlight"] = 0;
      value = account;
    }
    m_state["accounts"] = accounts;
    emit stateChanged();
    if (m_autoStartCore && (code != 0 || status != QProcess::NormalExit)) {
      m_autoStartCore = false;
      setMessage(tr("The pool router stopped unexpectedly. Start it again to retry."), QStringLiteral("error"));
    }
    refresh();
  });
  QTimer::singleShot(0, this, &PoolManager::refresh);
}

PoolManager::~PoolManager() {
  m_stateTimeout.stop();
  m_modelsTimeout.stop();
  for (QProcess *process : {&m_tool, &m_stateTool, &m_core, &m_modelsTool}) {
    disconnect(process, nullptr, this, nullptr);
    if (process->state() != QProcess::NotRunning) {
      process->kill();
      process->waitForFinished(2000);
    }
  }
}

QString PoolManager::workerPath() const {
  return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tools/pool_tool.py"));
}

void PoolManager::setMessage(const QString &message, const QString &kind) {
  if (m_message == message && m_messageKind == kind)
    return;
  m_message = message;
  m_messageKind = kind;
  emit poolMessageChanged();
}

void PoolManager::clearMessage() { setMessage({}); }

void PoolManager::finishRun() {
  if (m_signingIn || !m_authUrl.isEmpty()) {
    m_signingIn = false;
    m_authUrl.clear();
    emit signInChanged();
  }
  if (m_busy) {
    m_busy = false;
    emit busyChanged();
  }
}

void PoolManager::applyState(const QByteArray &stdoutData) {
  QVariantMap parsed = parseJson(stdoutData);
  if (!parsed.contains("accounts") || parsed.contains("error"))
    return;
  // A poll that began before a pushed request event must not erase that event.
  if (m_state.value("core").toString() == "running" &&
      parsed.value("core").toString() == "running" &&
      parsed.value("activitySession") == m_state.value("activitySession") &&
      parsed.value("activityRevision").toLongLong() < m_state.value("activityRevision").toLongLong()) {
    for (const QString &key : {QString("activeRequests"), QString("recentRequests"),
                              QString("activityRevision"), QString("inFlight"), QString("servedTotal"),
                              QString("protection")})
      parsed[key] = m_state.value(key);
    auto accounts = parsed.value("accounts").toList();
    const auto previous = m_state.value("accounts").toList();
    for (auto &value : accounts) {
      auto account = value.toMap();
      for (const auto &old : previous)
        if (old.toMap().value("id") == account.value("id"))
          account["inFlight"] = old.toMap().value("inFlight");
      value = account;
    }
    parsed["accounts"] = accounts;
  }
  // One missed loopback probe must not make a healthy Core flash as stopped.
  // A second consecutive miss confirms that it is actually unavailable.
  if (parsed.value("core").toString() == QStringLiteral("running")) {
    m_healthMisses = 0;
  } else if (m_state.value("core").toString() == QStringLiteral("running") &&
             m_autoStartCore && ++m_healthMisses < 2) {
    parsed.insert(QStringLiteral("core"), QStringLiteral("running"));
  }
  if (m_state == parsed)
    return;
  m_state = parsed;
  QSet<QString> names;
  for (const auto &value : parsed.value("accounts").toList()) {
    const auto account = value.toMap();
    if (!account.value("enabled", true).toBool() ||
        account.value("status").toString() != QStringLiteral("ready"))
      continue;
    for (const auto &model : account.value("availableModels").toList())
      names.insert(model.toString());
  }
  QStringList cached(names.begin(), names.end());
  cached.sort();
  if (m_models != cached) {
    m_models = cached;
    emit modelsChanged();
  }
  emit stateChanged();
}

void PoolManager::readCoreOutput(bool final) {
  m_coreOutput += m_core.readAllStandardOutput();
  while (!m_coreOutput.isEmpty()) {
    const auto newline = m_coreOutput.indexOf('\n');
    if (newline < 0 && !final)
      break;
    const auto line = newline < 0 ? m_coreOutput : m_coreOutput.left(newline);
    m_coreOutput.remove(0, newline < 0 ? m_coreOutput.size() : newline + 1);
    const auto result = parseJson(line.trimmed());
    if (result.contains("error")) {
      m_autoStartCore = false;
      setMessage(result.value("error").toString(), QStringLiteral("error"));
      continue;
    }
    if (result.value("event").toString() != "activity")
      continue;
    const auto activity = result.value("activity").toMap();
    if (activity.value("activitySession") == m_state.value("activitySession") &&
        activity.value("activityRevision").toLongLong() < m_state.value("activityRevision").toLongLong())
      continue;
    m_healthMisses = 0;
    const auto active = activity.value("activeRequests").toList();
    QSet<QString> busyAccounts;
    for (const auto &request : active)
      busyAccounts.insert(request.toMap().value("accountId").toString());
    auto accounts = m_state.value("accounts").toList();
    for (auto &value : accounts) {
      auto account = value.toMap();
      account["inFlight"] = busyAccounts.contains(account.value("id").toString()) ? 1 : 0;
      value = account;
    }
    m_state["accounts"] = accounts;
    m_state["core"] = "running";
    m_state["activeRequests"] = active;
    m_state["recentRequests"] = activity.value("recentRequests");
    m_state["activityRevision"] = activity.value("activityRevision");
    m_state["activitySession"] = activity.value("activitySession");
    m_state["protection"] = activity.value("protection");
    m_state["inFlight"] = active.size();
    m_state["servedTotal"] = qMax(m_state.value("servedTotal").toLongLong(), activity.value("servedTotal").toLongLong());
    emit stateChanged();
  }
  if (m_coreOutput.size() > 262144)
    m_coreOutput.clear();
}

void PoolManager::readModelOutput(bool final) {
  m_modelsOutput.append(m_modelsTool.readAllStandardOutput());
  while (!m_modelsOutput.isEmpty()) {
    const qsizetype newline = m_modelsOutput.indexOf('\n');
    if (newline < 0 && !final)
      return;
    const auto line = newline < 0 ? m_modelsOutput : m_modelsOutput.left(newline);
    m_modelsOutput.remove(0, newline < 0 ? m_modelsOutput.size() : newline + 1);
    const auto parsed = parseJson(line.trimmed());
    if (parsed.contains("modelCheck")) {
      m_modelStatus = tr("Checking account models: %1/%2…")
                          .arg(parsed.value("completed").toInt()).arg(parsed.value("total").toInt());
      emit modelCheckChanged();
    } else if (!parsed.isEmpty()) {
      m_modelsResult = parsed;
    }
  }
}

void PoolManager::checkModels() {
  if (m_checkingModels || m_busy)
    return;
  if (m_state.value("accounts").toList().isEmpty()) {
    m_modelStatus = tr("Add a Google account before checking pool models.");
    emit modelCheckChanged();
    return;
  }
  m_modelsOutput.clear();
  m_modelsResult.clear();
  m_checkingModels = true;
  m_modelStatus = tr("Checking models across all connected Google accounts…");
  ++m_stateGeneration;
  emit modelCheckChanged();
  m_modelsTool.start(poolInterpreter(), {QStringLiteral("-u"), workerPath(), QStringLiteral("models")});
  m_modelsTimeout.start();
}

void PoolManager::finishRefresh(int exitCode, QProcess::ExitStatus status) {
  m_stateTimeout.stop();
  m_stateOutput.append(m_stateTool.readAllStandardOutput());
  const bool stale = m_refreshGeneration != m_stateGeneration;
  if (!m_busy && !stale) {
    const QVariantMap result = parseJson(m_stateOutput.trimmed());
    QString error;
    if (m_stateTimedOut)
      error = tr("The pool status check timed out. Account actions are still available.");
    else if (result.contains("error"))
      error = result.value("error").toString();
    else if (status != QProcess::NormalExit || exitCode != 0 || !result.contains("accounts"))
      error = tr("The pool status could not be read. Check Python in Settings.");
    else {
      if (!m_refreshError.isEmpty() && m_message == m_refreshError)
        clearMessage();
      m_refreshError.clear();
      applyState(m_stateOutput.trimmed());
      startCoreIfReady();
    }
    if (!error.isEmpty()) {
      m_refreshError = error;
      setMessage(error, QStringLiteral("error"));
    }
  }
  if (stale && !m_busy)
    QTimer::singleShot(0, this, &PoolManager::refresh);
}

void PoolManager::readToolOutput() {
  m_toolOutput.append(m_tool.readAllStandardOutput());
  consumeToolOutput(false);
}

void PoolManager::consumeToolOutput(bool final) {
  while (!m_toolOutput.isEmpty()) {
    const qsizetype newline = m_toolOutput.indexOf('\n');
    if (newline < 0 && !final)
      return;
    const QByteArray line = newline < 0 ? m_toolOutput : m_toolOutput.left(newline);
    m_toolOutput.remove(0, newline < 0 ? m_toolOutput.size() : newline + 1);
    const QVariantMap parsed = parseJson(line.trimmed());
    if (parsed.isEmpty())
      continue;
    if (parsed.contains("authUrl")) {
      if (!m_signingIn || !m_authUrl.isEmpty())
        continue;
      const QUrl url(parsed.value("authUrl").toString());
      if (url.scheme() != QStringLiteral("https") || url.host() != QStringLiteral("accounts.google.com")) {
        setMessage(tr("The sign-in worker returned an invalid Google address."), QStringLiteral("error"));
        m_tool.kill();
        continue;
      }
      m_authUrl = url.toString();
      emit signInChanged();
      reopenSignIn();
    } else {
      m_result = parsed;
    }
  }
}

void PoolManager::runTool(const QStringList &arguments) {
  if (m_busy)
    return;
  const QString interpreter = poolInterpreter();
  const QString worker = workerPath();
  if (interpreter.isEmpty() || !QFileInfo::exists(worker)) {
    setMessage(tr("The pool worker or Python is missing. Check the installation and Python in Settings."),
               QStringLiteral("error"));
    return;
  }
  m_toolOutput.clear();
  m_result.clear();
  m_command = arguments.value(0);
  ++m_stateGeneration; // discard snapshots that began before this mutation
  m_cancelled = false;
  m_busy = true;
  emit busyChanged();
  if (m_command == QStringLiteral("add")) {
    m_signingIn = true;
    m_authUrl.clear();
    emit signInChanged();
    setMessage(tr("Preparing Google sign-in…"));
  } else {
    clearMessage();
  }
  m_tool.start(interpreter, QStringList{QStringLiteral("-u"), worker} + arguments);
}

void PoolManager::refresh() {
  if (m_busy)
    return;
  if (m_stateTool.state() != QProcess::NotRunning) {
    // Coalesce timer ticks instead of building a queue of overdue polls.
    return;
  }
  const QString interpreter = poolInterpreter();
  if (interpreter.isEmpty() || !QFileInfo::exists(workerPath())) {
    setMessage(tr("The pool worker or Python is missing. Check the installation and Python in Settings."),
               QStringLiteral("error"));
    return;
  }
  m_stateOutput.clear();
  m_stateTimedOut = false;
  m_refreshGeneration = m_stateGeneration;
  m_stateTool.start(interpreter, {QStringLiteral("-u"), workerPath(), QStringLiteral("state")});
  m_stateTimeout.start();
}
void PoolManager::addAccount() { runTool({QStringLiteral("add")}); }
void PoolManager::removeAccount(const QString &accountId) { runTool({QStringLiteral("remove"), accountId}); }
void PoolManager::setAccountEnabled(const QString &accountId, bool enabled) {
  runTool({QStringLiteral("set-enabled"), accountId, enabled ? QStringLiteral("1") : QStringLiteral("0")});
}
void PoolManager::verifyAll() { runTool({QStringLiteral("verify")}); }
void PoolManager::verifyAccount(const QString &accountId) { runTool({QStringLiteral("verify"), accountId}); }
void PoolManager::setStrategy(const QString &strategy) { runTool({QStringLiteral("set-strategy"), strategy}); }

void PoolManager::reopenSignIn() {
  if (!m_signingIn || m_authUrl.isEmpty())
    return;
  // Windows opens the user's default browser and existing browser profile.
  if (QDesktopServices::openUrl(QUrl(m_authUrl)))
    setMessage(tr("Complete Google sign-in in your browser. You can reopen it below."));
  else
    setMessage(tr("Windows could not open your browser. Use Open browser again, or copy the sign-in link."),
               QStringLiteral("error"));
}

void PoolManager::cancelSignIn() {
  if (!m_signingIn)
    return;
  m_cancelled = true;
  setMessage(tr("Google sign-in cancelled. You can add an account whenever you're ready."));
  m_tool.kill(); // finished clears the busy flag and the one-use auth URL
}

void PoolManager::ensureCore() {
  m_autoStartCore = true;
  if (m_state.isEmpty())
    refresh();
  else
    startCoreIfReady();
}

void PoolManager::startCoreIfReady() {
  if (!m_autoStartCore || m_state.value("core").toString() == QStringLiteral("running") ||
      m_state.value("readyCount").toInt() <= 0 || m_core.state() != QProcess::NotRunning)
    return;
  const QString interpreter = poolInterpreter();
  if (interpreter.isEmpty() || !QFileInfo::exists(workerPath()))
    return;
  m_coreOutput.clear();
  m_state["activityRevision"] = 0;
  m_state["activitySession"] = QString();
  m_core.start(interpreter, {QStringLiteral("-u"), workerPath(), QStringLiteral("serve")});
}

void PoolManager::stopCore() {
  if (m_core.state() == QProcess::NotRunning) {
    setMessage(tr("This router was started by another app. Stop it from that app."));
    return;
  }
  m_autoStartCore = false;
  m_core.kill();
  setMessage(tr("Pool router stopped."));
}
