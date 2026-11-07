#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

class QNetworkReply;

// Self-updater backed by GitHub Releases - no server of our own. It asks the
// repository's "latest release" endpoint what the newest published version is,
// compares it to the version compiled into this build (core::kVersion), and if
// GitHub has a newer one with a Setup .exe attached it exposes it to the UI: the
// top bar shows an update button, and pressing it downloads that installer and
// launches it. The installer's fixed AppId upgrades the existing install in
// place, so there is nothing to uninstall first.
//
// Everything is a property the QML binds to; there is one changed() signal that
// every property's NOTIFY points at, which is enough for a small status object
// the UI only ever reads.
class UpdateChecker : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString state READ state NOTIFY changed)
  Q_PROPERTY(bool available READ available NOTIFY changed)
  Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
  Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
  Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY changed)
  Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY changed)
  Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY changed)
  Q_PROPERTY(QString errorString READ errorString NOTIFY changed)

public:
  explicit UpdateChecker(QObject *parent = nullptr);

  // "idle" | "checking" | "available" | "upToDate" | "downloading" | "ready"
  // | "error". available() is true for the three states that mean "there is a
  // newer build the user can install", which is what the top-bar icon binds to.
  QString state() const { return m_state; }
  bool available() const {
    return m_state == QStringLiteral("available") ||
           m_state == QStringLiteral("downloading") ||
           m_state == QStringLiteral("ready");
  }
  QString currentVersion() const;
  QString latestVersion() const { return m_latestVersion; }
  QString releaseNotes() const { return m_releaseNotes; }
  QString releaseUrl() const { return m_releaseUrl; }
  double downloadProgress() const { return m_downloadProgress; }
  QString errorString() const { return m_errorString; }

  // Ask GitHub for the latest release. Cheap and safe to call more than once;
  // ignored while a check or download is already in flight.
  Q_INVOKABLE void checkForUpdate();
  // Download the setup asset and, once it is on disk, launch it and quit so it
  // can replace the running files.
  Q_INVOKABLE void downloadAndInstall();
  // Fallback: open the release's GitHub page in the browser.
  Q_INVOKABLE void openReleasePage();
  // Hide the button until the next check or restart.
  Q_INVOKABLE void dismiss();

signals:
  void changed();

private:
  void setState(const QString &state);
  void onCheckFinished(QNetworkReply *reply);
  void onDownloadFinished(QNetworkReply *reply);
  void launchInstaller(const QString &installerPath);

  QNetworkAccessManager m_net;
  QString m_state = QStringLiteral("idle");
  QString m_latestVersion;
  QString m_releaseNotes;
  QString m_releaseUrl;
  QString m_downloadUrl;
  QString m_errorString;
  double m_downloadProgress = 0.0;
  bool m_dismissed = false;
};
