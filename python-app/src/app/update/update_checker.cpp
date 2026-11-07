#include "app/update/update_checker.h"

#include "core/version.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace {
// The public repository we publish releases to. The trailing dash is part of
// the real repo name (github.com/1phirum/video-editor-app-), not a typo.
const QString kLatestReleaseUrl = QStringLiteral(
    "https://api.github.com/repos/1phirum/video-editor-app-/releases/latest");

// "v0.1.3" / "0.1.3-beta" -> {0, 1, 3}. Anything past the first three dotted
// numeric fields (pre-release suffixes, build metadata) is ignored: we only
// order by the release number the installer stamps.
QList<int> parseVersion(const QString &raw) {
  QString v = raw.trimmed();
  if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
    v.remove(0, 1);
  QList<int> parts;
  const QStringList fields = v.split(QLatin1Char('.'));
  for (const QString &field : fields) {
    int digits = 0;
    while (digits < field.size() && field.at(digits).isDigit())
      ++digits;
    if (digits == 0)
      break;
    parts << field.left(digits).toInt();
  }
  return parts;
}

// Returns true when `latest` is a strictly newer version than `current`.
// Missing trailing fields count as zero, so 0.2 > 0.1.9 and 0.1 == 0.1.0.
bool isNewer(const QString &latest, const QString &current) {
  const QList<int> a = parseVersion(latest);
  const QList<int> b = parseVersion(current);
  const int n = qMax(a.size(), b.size());
  for (int i = 0; i < n; ++i) {
    const int x = i < a.size() ? a.at(i) : 0;
    const int y = i < b.size() ? b.at(i) : 0;
    if (x != y)
      return x > y;
  }
  return false;
}
}  // namespace

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent) {}

QString UpdateChecker::currentVersion() const {
  return QString::fromLatin1(core::kVersion);
}

void UpdateChecker::setState(const QString &state) {
  if (m_state == state)
    return;
  m_state = state;
  emit changed();
}

void UpdateChecker::checkForUpdate() {
  // Ignore re-entry while a check or a download is already running; the button
  // and the startup timer can both call this.
  if (m_state == QStringLiteral("checking") ||
      m_state == QStringLiteral("downloading"))
    return;

  m_errorString.clear();
  setState(QStringLiteral("checking"));

  QNetworkRequest req{QUrl(kLatestReleaseUrl)};
  req.setRawHeader("Accept", "application/vnd.github+json");
  req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
  // GitHub rejects requests with no User-Agent outright.
  req.setHeader(QNetworkRequest::UserAgentHeader,
                QStringLiteral("CutPro-Updater/%1").arg(currentVersion()));
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);

  QNetworkReply *reply = m_net.get(req);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply] { onCheckFinished(reply); });
}

void UpdateChecker::onCheckFinished(QNetworkReply *reply) {
  reply->deleteLater();

  if (reply->error() != QNetworkReply::NoError) {
    m_errorString = reply->errorString();
    setState(QStringLiteral("error"));
    return;
  }

  const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
  if (!doc.isObject()) {
    m_errorString = QStringLiteral("Malformed release response from GitHub.");
    setState(QStringLiteral("error"));
    return;
  }
  const QJsonObject obj = doc.object();

  // A draft or a not-yet-published release should never trigger the icon.
  if (obj.value(QStringLiteral("draft")).toBool()) {
    setState(QStringLiteral("upToDate"));
    return;
  }

  const QString tag = obj.value(QStringLiteral("tag_name")).toString();
  m_latestVersion = tag;
  m_releaseNotes = obj.value(QStringLiteral("body")).toString();
  m_releaseUrl = obj.value(QStringLiteral("html_url")).toString();

  // Find the Windows Setup installer among the attached assets.
  m_downloadUrl.clear();
  const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
  for (const QJsonValue &val : assets) {
    const QJsonObject asset = val.toObject();
    const QString name = asset.value(QStringLiteral("name")).toString();
    if (name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
      m_downloadUrl =
          asset.value(QStringLiteral("browser_download_url")).toString();
      break;
    }
  }

  if (m_dismissed || !isNewer(tag, currentVersion())) {
    setState(QStringLiteral("upToDate"));
    return;
  }
  if (m_downloadUrl.isEmpty()) {
    // Newer version exists but has no installer attached: still surface it so
    // the user can open the release page and install by hand.
    m_errorString =
        QStringLiteral("No installer attached to the latest release.");
  }
  setState(QStringLiteral("available"));
}

void UpdateChecker::downloadAndInstall() {
  if (m_state == QStringLiteral("downloading"))
    return;
  if (m_downloadUrl.isEmpty()) {
    // Nothing to fetch - fall back to the release page.
    openReleasePage();
    return;
  }

  m_errorString.clear();
  m_downloadProgress = 0.0;
  setState(QStringLiteral("downloading"));

  QNetworkRequest req{QUrl(m_downloadUrl)};
  req.setHeader(QNetworkRequest::UserAgentHeader,
                QStringLiteral("CutPro-Updater/%1").arg(currentVersion()));
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);

  QNetworkReply *reply = m_net.get(req);
  connect(reply, &QNetworkReply::downloadProgress, this,
          [this](qint64 received, qint64 total) {
            m_downloadProgress = total > 0
                                     ? static_cast<double>(received) / total
                                     : 0.0;
            emit changed();
          });
  connect(reply, &QNetworkReply::finished, this,
          [this, reply] { onDownloadFinished(reply); });
}

void UpdateChecker::onDownloadFinished(QNetworkReply *reply) {
  reply->deleteLater();

  if (reply->error() != QNetworkReply::NoError) {
    m_errorString = reply->errorString();
    setState(QStringLiteral("error"));
    return;
  }

  // Write the installer to a stable, writable location. We keep the release
  // tag in the filename so a stale download from a previous check cannot be
  // launched by mistake.
  const QString dir =
      QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  QString safeTag = m_latestVersion;
  safeTag.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")),
                  QStringLiteral("_"));
  const QString path =
      QDir(dir).filePath(QStringLiteral("CutPro-%1-Setup.exe").arg(safeTag));

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    m_errorString = QStringLiteral("Cannot write installer to %1").arg(path);
    setState(QStringLiteral("error"));
    return;
  }
  file.write(reply->readAll());
  file.close();

  m_downloadProgress = 1.0;
  setState(QStringLiteral("ready"));
  launchInstaller(path);
}

void UpdateChecker::launchInstaller(const QString &installerPath) {
  // Run the setup silently and let it close and relaunch this app around the
  // file replacement. The installer's fixed AppId upgrades the install in
  // place, so no uninstall step is needed.
  const QStringList args{QStringLiteral("/SILENT"),
                         QStringLiteral("/CLOSEAPPLICATIONS"),
                         QStringLiteral("/RESTARTAPPLICATIONS"),
                         QStringLiteral("/NOCANCEL")};
  if (!QProcess::startDetached(installerPath, args)) {
    m_errorString = QStringLiteral("Failed to launch the installer.");
    setState(QStringLiteral("error"));
    return;
  }
  // Give the detached installer a moment to start before we release the files
  // it needs to overwrite.
  QCoreApplication::quit();
}

void UpdateChecker::openReleasePage() {
  const QString url =
      m_releaseUrl.isEmpty()
          ? QStringLiteral("https://github.com/1phirum/video-editor-app-/releases/latest")
          : m_releaseUrl;
  QDesktopServices::openUrl(QUrl(url));
}

void UpdateChecker::dismiss() {
  m_dismissed = true;
  if (available())
    setState(QStringLiteral("upToDate"));
}
