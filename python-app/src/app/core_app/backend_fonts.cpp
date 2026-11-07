// Caption font library: the one place that knows which fonts a caption can be
// set in and where they come from. Split out of backend.cpp (which had grown too
// large to maintain) so all font logic - startup registration, the built-in
// cloud downloads, and user-imported fonts - lives together.
//
// Fonts are files under AppDataLocation/fonts. Three kinds share that folder:
//   - system      : always-present Khmer faces shipped with the OS/app; not files
//                   we manage, listed for completeness, never removable.
//   - cloud        : fonts fetched on demand from Google Fonts - the three
//                   built-ins (Roboto / Open Sans / Lato) plus anything the user
//                   grabs from the full Google Fonts browser in Settings. A file
//                   appears once downloaded; "google-*.ttf" files are ones pulled
//                   from the browser.
//   - custom       : anything the user imported from their own .ttf/.otf/.ttc.
//
// Registration is via QFontDatabase::addApplicationFont, which is all Qt needs to
// draw the family in both the live monitor and the exported frames (captions are
// Qt-rendered, so no FFmpeg fontfile path is involved).

#include "app/core_app/backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

namespace {

// The three faces the caption UI has always treated as always-available. Kept in
// one place so the picker and the settings page agree on what "system" means.
const QStringList &systemCaptionFonts() {
  static const QStringList fonts{QStringLiteral("Khmer OS System"),
                                 QStringLiteral("Khmer OS Siemreap"),
                                 QStringLiteral("Khmer OS Battambang")};
  return fonts;
}

// The built-in downloadable families and the file each is saved as. The URLs live
// with downloadCaptionFont(); this only needs the family -> filename mapping so a
// startup scan can tell a downloaded cloud font from a user import.
const QStringList &cloudCaptionFonts() {
  static const QStringList fonts{QStringLiteral("Roboto"),
                                 QStringLiteral("Open Sans"),
                                 QStringLiteral("Lato")};
  return fonts;
}

QString cloudFontFileName(const QString &family) {
  return family.toLower().replace(QLatin1Char(' '), QLatin1Char('-')) +
         QStringLiteral(".ttf");
}

// Files pulled from the full Google Fonts browser get this prefix so a startup
// scan can tell them apart from user imports and list them as cloud fonts.
const QString kGoogleFontPrefix = QStringLiteral("google-");

QString googleFontFileName(const QString &family) {
  QString slug = family.toLower();
  slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
               QStringLiteral("-"));
  slug = slug.trimmed();
  while (slug.startsWith(QLatin1Char('-')))
    slug.remove(0, 1);
  while (slug.endsWith(QLatin1Char('-')))
    slug.chop(1);
  return kGoogleFontPrefix + slug + QStringLiteral(".ttf");
}

QString fontLibraryDir() {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
      .filePath(QStringLiteral("fonts"));
}

// Throwaway copies fetched only to render a preview live here, deliberately
// outside fontLibraryDir() so the startup scan never mistakes a previewed font
// for an installed one.
QString fontPreviewDir() {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
      .filePath(QStringLiteral("font-preview"));
}

bool isFontFile(const QString &path) {
  const QString suffix = QFileInfo(path).suffix().toLower();
  return suffix == QLatin1String("ttf") || suffix == QLatin1String("otf") ||
         suffix == QLatin1String("ttc");
}

// FileDialog/FolderDialog hand back file:/// URLs; a bare path is passed through.
QString localPathFromUrl(const QString &maybeUrl) {
  const QUrl url(maybeUrl);
  return url.isLocalFile() ? url.toLocalFile() : maybeUrl;
}

} // namespace

QString Backend::registerFontFile(const QString &path) {
  const int id = QFontDatabase::addApplicationFont(path);
  if (id < 0)
    return QString();
  const QStringList families = QFontDatabase::applicationFontFamilies(id);
  if (families.isEmpty()) {
    QFontDatabase::removeApplicationFont(id);
    return QString();
  }
  m_captionFontFileIds.insert(path, id);
  return families.first();
}

void Backend::loadFontLibrary() {
  const QString root = fontLibraryDir();
  QDir().mkpath(root);

  // The filenames a downloaded cloud font uses, so the scan can classify each
  // file it finds without a second pass.
  QSet<QString> cloudFileNames;
  for (const QString &family : cloudCaptionFonts())
    cloudFileNames.insert(cloudFontFileName(family).toLower());

  const QStringList filters{QStringLiteral("*.ttf"), QStringLiteral("*.otf"),
                            QStringLiteral("*.ttc")};
  const QFileInfoList files =
      QDir(root).entryInfoList(filters, QDir::Files, QDir::Name);
  for (const QFileInfo &info : files) {
    const QString family = registerFontFile(info.absoluteFilePath());
    if (family.isEmpty())
      continue;
    if (info.fileName().startsWith(kGoogleFontPrefix, Qt::CaseInsensitive)) {
      // Pulled from the Google Fonts browser: its real family name is whatever
      // QFontDatabase reported, so list it as an available cloud font.
      if (!m_downloadedCaptionFonts.contains(family))
        m_downloadedCaptionFonts << family;
      m_downloadedFontPaths.insert(family, info.absoluteFilePath());
    } else if (cloudFileNames.contains(info.fileName().toLower())) {
      // A downloaded built-in: match it back to its canonical family name so the
      // picker's "(download)" state clears.
      for (const QString &cloud : cloudCaptionFonts()) {
        if (cloudFontFileName(cloud).compare(info.fileName(),
                                             Qt::CaseInsensitive) == 0) {
          if (!m_downloadedCaptionFonts.contains(cloud))
            m_downloadedCaptionFonts << cloud;
          m_downloadedFontPaths.insert(cloud, info.absoluteFilePath());
          break;
        }
      }
    } else if (!m_customCaptionFonts.contains(family)) {
      m_customCaptionFonts << family;
      m_customCaptionFontPaths.insert(family, info.absoluteFilePath());
    }
  }
  emit captionFontsChanged();
}

QVariantList Backend::captionFontModel() const {
  // The order the dropdown shows: system first (the default Khmer faces), then the
  // cloud fonts, then the user's own imports last.
  QVariantList model;
  auto append = [&model](const QString &family, bool available,
                         bool downloadable, const QString &category) {
    QVariantMap entry;
    entry.insert(QStringLiteral("family"), family);
    entry.insert(QStringLiteral("available"), available);
    entry.insert(QStringLiteral("downloadable"), downloadable);
    entry.insert(QStringLiteral("category"), category);
    model.append(entry);
  };
  for (const QString &family : systemCaptionFonts())
    append(family, true, false, QStringLiteral("system"));
  for (const QString &family : cloudCaptionFonts())
    append(family, m_downloadedCaptionFonts.contains(family), true,
           QStringLiteral("cloud"));
  // Fonts pulled from the Google Fonts browser: available and picker-ready, but
  // not part of the built-in three, so list them after those.
  for (const QString &family : m_downloadedCaptionFonts)
    if (!cloudCaptionFonts().contains(family))
      append(family, true, false, QStringLiteral("cloud"));
  for (const QString &family : m_customCaptionFonts)
    append(family, true, false, QStringLiteral("custom"));
  return model;
}

QVariantList Backend::captionFontCatalog() const {
  // Same three groups the settings page renders; carries whether a row can be
  // downloaded or removed so the page needs no hardcoded knowledge of families.
  QVariantList catalog;
  auto append = [&catalog](const QString &family, const QString &category,
                           bool available, bool removable, bool downloadable) {
    QVariantMap entry;
    entry.insert(QStringLiteral("family"), family);
    entry.insert(QStringLiteral("category"), category);
    entry.insert(QStringLiteral("available"), available);
    entry.insert(QStringLiteral("removable"), removable);
    entry.insert(QStringLiteral("downloadable"), downloadable);
    catalog.append(entry);
  };
  for (const QString &family : systemCaptionFonts())
    append(family, QStringLiteral("system"), true, false, false);
  for (const QString &family : cloudCaptionFonts())
    append(family, QStringLiteral("cloud"),
           m_downloadedCaptionFonts.contains(family), false, true);
  for (const QString &family : m_customCaptionFonts)
    append(family, QStringLiteral("custom"), true, true, false);
  return catalog;
}

int Backend::importCaptionFontFiles(const QStringList &fileUrls) {
  const QString root = fontLibraryDir();
  QDir().mkpath(root);
  int imported = 0;
  bool changed = false;
  for (const QString &raw : fileUrls) {
    const QString source = localPathFromUrl(raw);
    if (source.isEmpty() || !isFontFile(source) || !QFileInfo::exists(source))
      continue;
    // Copy into the library under the original filename, avoiding a clash with a
    // file already there (a re-import or a name collision keeps a numbered copy).
    const QFileInfo srcInfo(source);
    QString dest = QDir(root).filePath(srcInfo.fileName());
    int suffix = 1;
    while (QFileInfo::exists(dest) &&
           QFileInfo(dest).absoluteFilePath() != srcInfo.absoluteFilePath()) {
      dest = QDir(root).filePath(srcInfo.completeBaseName() +
                                 QStringLiteral("-%1.").arg(suffix++) +
                                 srcInfo.suffix());
    }
    if (QFileInfo(dest).absoluteFilePath() != srcInfo.absoluteFilePath()) {
      QFile::remove(dest); // stale partial from a prior failed copy, if any
      if (!QFile::copy(source, dest)) {
        setError(QStringLiteral("Could not import font: %1").arg(srcInfo.fileName()));
        continue;
      }
    }
    const QString family = registerFontFile(dest);
    if (family.isEmpty()) {
      setError(QStringLiteral("Not a usable font: %1").arg(srcInfo.fileName()));
      continue;
    }
    if (!m_customCaptionFonts.contains(family)) {
      m_customCaptionFonts << family;
      m_customCaptionFontPaths.insert(family, dest);
      changed = true;
    }
    ++imported;
  }
  if (changed)
    emit captionFontsChanged();
  return imported;
}

int Backend::importCaptionFontFolder(const QString &folderUrl) {
  const QString folder = localPathFromUrl(folderUrl);
  if (folder.isEmpty() || !QFileInfo(folder).isDir())
    return 0;
  const QStringList filters{QStringLiteral("*.ttf"), QStringLiteral("*.otf"),
                            QStringLiteral("*.ttc")};
  const QFileInfoList files = QDir(folder).entryInfoList(
      filters, QDir::Files, QDir::Name);
  QStringList paths;
  paths.reserve(files.size());
  for (const QFileInfo &info : files)
    paths << info.absoluteFilePath();
  return importCaptionFontFiles(paths);
}

bool Backend::removeCustomCaptionFont(const QString &family) {
  if (!m_customCaptionFonts.contains(family))
    return false;
  const QString path = m_customCaptionFontPaths.value(family);
  if (m_captionFontFileIds.contains(path)) {
    QFontDatabase::removeApplicationFont(m_captionFontFileIds.value(path));
    m_captionFontFileIds.remove(path);
  }
  if (!path.isEmpty())
    QFile::remove(path);
  m_customCaptionFonts.removeAll(family);
  m_customCaptionFontPaths.remove(family);
  // If the caption was set to the font just removed, fall back to the first
  // always-available family so the picker never shows a missing selection.
  if (m_captionStyle.fontFamily == family && !systemCaptionFonts().isEmpty()) {
    m_captionStyle.fontFamily = systemCaptionFonts().first();
    markDirty();
    emit captionStyleChanged();
  }
  emit captionFontsChanged();
  return true;
}

bool Backend::downloadCaptionFont(const QString &family) {
  static const QHash<QString, QUrl> urls{
      {QStringLiteral("Roboto"),
       QUrl(QStringLiteral("https://github.com/google/fonts/raw/main/ofl/"
                           "roboto/Roboto%5Bwght%5D.ttf"))},
      {QStringLiteral("Open Sans"),
       QUrl(QStringLiteral("https://github.com/google/fonts/raw/main/ofl/"
                           "opensans/OpenSans%5Bwght%5D.ttf"))},
      {QStringLiteral("Lato"),
       QUrl(QStringLiteral("https://github.com/google/fonts/raw/main/ofl/lato/"
                           "Lato%5Bwght%5D.ttf"))}};
  if (!urls.contains(family) || m_downloadedCaptionFonts.contains(family))
    return m_downloadedCaptionFonts.contains(family);

  const QString root = fontLibraryDir();
  QDir().mkpath(root);
  const QString path = QDir(root).filePath(cloudFontFileName(family));
  QNetworkReply *reply =
      m_fontNetwork.get(QNetworkRequest(urls.value(family)));
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, path, family]() {
            const QByteArray data =
                reply->error() == QNetworkReply::NoError ? reply->readAll()
                                                         : QByteArray();
            reply->deleteLater();
            if (data.isEmpty()) {
              setError(QStringLiteral("Could not download font: %1").arg(family));
              return;
            }
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) ||
                file.write(data) != data.size()) {
              setError(QStringLiteral("Could not save font: %1").arg(family));
              return;
            }
            file.close();
            if (registerFontFile(path).isEmpty()) {
              setError(
                  QStringLiteral("Downloaded font is invalid: %1").arg(family));
              return;
            }
            m_downloadedCaptionFonts << family;
            m_downloadedFontPaths.insert(family, path);
            emit captionFontsChanged();
          });
  return true;
}

void Backend::fetchGoogleFontsCatalog() {
  // Fetch once and cache: the library is ~1500 families and rarely changes
  // within a session, so re-opening the page reuses what we already pulled.
  if (m_googleFontsLoading || !m_googleFontsCatalog.isEmpty())
    return;
  m_googleFontsLoading = true;
  m_googleFontsError.clear();
  emit googleFontsCatalogChanged();

  // The website's own metadata feed: no API key, lists every family with its
  // category. The response is prefixed with )]}' to defeat JSON hijacking, so we
  // skip to the first brace before parsing.
  QNetworkRequest request(
      QUrl(QStringLiteral("https://fonts.google.com/metadata/fonts")));
  request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
  QNetworkReply *reply = m_fontNetwork.get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    const bool ok = reply->error() == QNetworkReply::NoError;
    QByteArray data = ok ? reply->readAll() : QByteArray();
    const QString netError = reply->errorString();
    reply->deleteLater();
    m_googleFontsLoading = false;
    if (!ok || data.isEmpty()) {
      m_googleFontsError = netError.isEmpty()
                               ? QStringLiteral("Could not reach Google Fonts")
                               : netError;
      emit googleFontsCatalogChanged();
      return;
    }
    const int brace = data.indexOf('{');
    if (brace > 0)
      data = data.mid(brace);
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (doc.isNull() || !doc.isObject()) {
      m_googleFontsError = QStringLiteral("Could not read the Google Fonts list");
      emit googleFontsCatalogChanged();
      return;
    }
    const QJsonArray list =
        doc.object().value(QStringLiteral("familyMetadataList")).toArray();
    QVariantList catalog;
    catalog.reserve(list.size());
    for (const QJsonValue &value : list) {
      const QJsonObject obj = value.toObject();
      const QString family = obj.value(QStringLiteral("family")).toString();
      if (family.isEmpty())
        continue;
      QVariantMap entry;
      entry.insert(QStringLiteral("family"), family);
      entry.insert(QStringLiteral("category"),
                   obj.value(QStringLiteral("category")).toString());
      catalog.append(entry);
    }
    m_googleFontsCatalog = catalog;
    m_googleFontsError.clear();
    emit googleFontsCatalogChanged();
  });
}

bool Backend::downloadGoogleFont(const QString &family) {
  if (family.isEmpty())
    return false;
  // Already have it (built-in, previously downloaded, or user-imported).
  if (m_downloadedCaptionFonts.contains(family) ||
      m_customCaptionFonts.contains(family))
    return true;

  // Ask the CSS2 endpoint for this family with a user-agent that predates woff2,
  // so Google serves a plain .ttf whose URL we can pull out and fetch directly.
  QUrl cssUrl(QStringLiteral("https://fonts.googleapis.com/css2"));
  cssUrl.setQuery(QStringLiteral("family=") +
                  QString(family).replace(QLatin1Char(' '), QLatin1Char('+')));
  QNetworkRequest cssRequest(cssUrl);
  cssRequest.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 5.1)");
  QNetworkReply *cssReply = m_fontNetwork.get(cssRequest);
  connect(cssReply, &QNetworkReply::finished, this, [this, cssReply, family]() {
    const bool ok = cssReply->error() == QNetworkReply::NoError;
    const QByteArray css = ok ? cssReply->readAll() : QByteArray();
    cssReply->deleteLater();
    if (css.isEmpty()) {
      setError(QStringLiteral("Could not find font: %1").arg(family));
      return;
    }
    // Grab the first .ttf source in the stylesheet.
    static const QRegularExpression ttfRe(
        QStringLiteral("url\\((https?://[^)]+\\.ttf)\\)"));
    const QRegularExpressionMatch match =
        ttfRe.match(QString::fromUtf8(css));
    if (!match.hasMatch()) {
      setError(QStringLiteral("No downloadable file for: %1").arg(family));
      return;
    }
    const QUrl fileUrl(match.captured(1));
    const QString root = fontLibraryDir();
    QDir().mkpath(root);
    const QString path = QDir(root).filePath(googleFontFileName(family));
    QNetworkReply *fileReply = m_fontNetwork.get(QNetworkRequest(fileUrl));
    connect(fileReply, &QNetworkReply::finished, this,
            [this, fileReply, path, family]() {
              const QByteArray data =
                  fileReply->error() == QNetworkReply::NoError
                      ? fileReply->readAll()
                      : QByteArray();
              fileReply->deleteLater();
              if (data.isEmpty()) {
                setError(
                    QStringLiteral("Could not download font: %1").arg(family));
                return;
              }
              QFile file(path);
              if (!file.open(QIODevice::WriteOnly) ||
                  file.write(data) != data.size()) {
                setError(QStringLiteral("Could not save font: %1").arg(family));
                return;
              }
              file.close();
              const QString registered = registerFontFile(path);
              if (registered.isEmpty()) {
                QFile::remove(path);
                setError(QStringLiteral("Downloaded font is invalid: %1")
                             .arg(family));
                return;
              }
              // Track under the requested family so the browser's "Installed"
              // state (bound to downloadedCaptionFonts) flips right away.
              if (!m_downloadedCaptionFonts.contains(family))
                m_downloadedCaptionFonts << family;
              m_downloadedFontPaths.insert(family, path);
              emit captionFontsChanged();
            });
  });
  return true;
}

void Backend::previewGoogleFont(const QString &family) {
  if (family.isEmpty())
    return;
  // Nothing to fetch if it is already renderable (installed or previewed).
  if (m_downloadedCaptionFonts.contains(family) ||
      m_customCaptionFonts.contains(family) ||
      m_previewedCaptionFonts.contains(family))
    return;

  // Same CSS2 trick as the download path, but the file lands in the cache and is
  // registered without being installed, so the sample renders and a Download
  // still persists the font properly.
  QUrl cssUrl(QStringLiteral("https://fonts.googleapis.com/css2"));
  cssUrl.setQuery(QStringLiteral("family=") +
                  QString(family).replace(QLatin1Char(' '), QLatin1Char('+')));
  QNetworkRequest cssRequest(cssUrl);
  cssRequest.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 5.1)");
  QNetworkReply *cssReply = m_fontNetwork.get(cssRequest);
  connect(cssReply, &QNetworkReply::finished, this, [this, cssReply, family]() {
    const bool ok = cssReply->error() == QNetworkReply::NoError;
    const QByteArray css = ok ? cssReply->readAll() : QByteArray();
    cssReply->deleteLater();
    if (css.isEmpty())
      return;
    static const QRegularExpression ttfRe(
        QStringLiteral("url\\((https?://[^)]+\\.ttf)\\)"));
    const QRegularExpressionMatch match = ttfRe.match(QString::fromUtf8(css));
    if (!match.hasMatch())
      return;
    const QUrl fileUrl(match.captured(1));
    const QString root = fontPreviewDir();
    QDir().mkpath(root);
    const QString path = QDir(root).filePath(googleFontFileName(family));
    QNetworkReply *fileReply = m_fontNetwork.get(QNetworkRequest(fileUrl));
    connect(fileReply, &QNetworkReply::finished, this,
            [this, fileReply, path, family]() {
              const QByteArray data =
                  fileReply->error() == QNetworkReply::NoError
                      ? fileReply->readAll()
                      : QByteArray();
              fileReply->deleteLater();
              if (data.isEmpty())
                return;
              QFile file(path);
              if (!file.open(QIODevice::WriteOnly) ||
                  file.write(data) != data.size())
                return;
              file.close();
              if (registerFontFile(path).isEmpty()) {
                QFile::remove(path);
                return;
              }
              if (!m_previewedCaptionFonts.contains(family))
                m_previewedCaptionFonts << family;
              emit previewedCaptionFontsChanged();
            });
  });
}

bool Backend::removeDownloadedCaptionFont(const QString &family) {
  if (!m_downloadedCaptionFonts.contains(family))
    return false;
  const QString path = m_downloadedFontPaths.value(family);
  if (m_captionFontFileIds.contains(path)) {
    QFontDatabase::removeApplicationFont(m_captionFontFileIds.value(path));
    m_captionFontFileIds.remove(path);
  }
  if (!path.isEmpty())
    QFile::remove(path);
  m_downloadedCaptionFonts.removeAll(family);
  m_downloadedFontPaths.remove(family);
  // Same safety net as a custom removal: never leave the caption pointing at a
  // family that is no longer registered.
  if (m_captionStyle.fontFamily == family && !systemCaptionFonts().isEmpty()) {
    m_captionStyle.fontFamily = systemCaptionFonts().first();
    markDirty();
    emit captionStyleChanged();
  }
  emit captionFontsChanged();
  return true;
}
