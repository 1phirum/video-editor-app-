#include <QtTest>

#include "app/settings/app_settings.h"
#include "app/core_app/backend.h"
#include "app/effects/audio_effect_pipeline.h"
#include "app/effects/clip_effects_pipeline.h"
#include "app/lumetri/color_settings.h"
#include "app/lumetri/custom_blur_pipeline.h"
#include "app/effects/effect_registry.h"
#include "app/effects/effect_stack.h"
#include "app/lumetri/lumetri_pipeline.h"
#include "app/media/media_import_queue.h"
#include "app/media/media_path.h"
#include "app/media/media_scan.h"
#include "app/preview/frame_buffer_pool.h"
#include "app/preview/media_preview_generator.h"
#include "app/preview/preview_decode_policy.h"
#include "app/timeline/large_media_policy.h"
#include "app/timeline/long_media_timeline_handler.h"
#include "app/timeline/timeline_clip_model.h"
#include "app/timeline/timeline_placement.h"
#include "app/effects/video_effect_pipeline.h"
#include "app/preview/video_preview_helper.h"
#include "app/subtitles/subtitle_timeline.h"
#include "app/subtitles/subtitle_io.h"

#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <atomic>
#include <memory>

namespace {

// Writes `bytes` of filler so the file is non-empty: the scanner and
// MediaPath::isDecodable both reject zero-byte files, which is the behaviour
// under test elsewhere.
bool writeSampleFile(const QString &path, int bytes = 64) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  return file.write(QByteArray(bytes, 'x')) == bytes;
}

} // namespace

class BackendTests : public QObject {
  Q_OBJECT

private slots:
  void init() { QVERIFY(AppSettings::save(AppSettings::defaults())); }

  void cleanup() { QVERIFY(AppSettings::save(AppSettings::defaults())); }

  void largeMediaUsesDeferredLightweightPresentation() {
    QVariantMap media{{"id", "media-large"},
                      {"name", "eight-hours.mp4"},
                      {"kind", "video"},
                      {"durationMs", 29231740LL},
                      {"sizeBytes", 6281982972LL},
                      {"channels", 2}};

    QVERIFY(LargeMediaPolicy::requiresLightweightHandling(media));
    LargeMediaPolicy::applyPresentationFlags(&media);
    QCOMPARE(media.value("largeMedia").toBool(), true);
    QCOMPARE(media.value("deferMonitorLoad").toBool(), true);
    QCOMPARE(media.value("timelineRenderMode").toString(),
             QString("lightweight"));

    const QVariantMap request =
        LongMediaTimelineHandler::placementRequest(media, 0, "V1");
    QCOMPARE(request.value("lightweightMedia").toBool(), true);
    const QVariantMap clip = LongMediaTimelineHandler::timelineClip(
        media, request, "clip-large");
    QCOMPARE(clip.value("durationMs").toLongLong(), 29231740LL);
    QCOMPARE(clip.value("timelineRenderMode").toString(),
             QString("lightweight"));
    QCOMPARE(clip.value("embeddedAudio").toBool(), true);

    QVariantMap normal{{"kind", "video"},
                       {"durationMs", 120000LL},
                       {"sizeBytes", 50000000LL}};
    QVERIFY(!LargeMediaPolicy::requiresLightweightHandling(normal));
  }

  void largePreviewUsesBoundedDecodeProfile() {
    const auto profile = PreviewDecodePolicy::forSource(
        QStringLiteral("C:/does-not-need-to-exist/large.mp4"), 3840, 2160,
        60.0);
    QVERIFY(profile.lightweight);
    QVERIFY(profile.frameSize.width() <= 540);
    QVERIFY(profile.frameSize.height() <= 540);
    QVERIFY(profile.maximumFrameRate <= 24.0);
    QCOMPARE(profile.decoderThreads, 2);
  }

  void timelineClipModelVirtualizesByViewport() {
    TimelineClipModel model;
    QVariantList clips{
        QVariantMap{{"id", "a"}, {"startMs", 0}, {"durationMs", 1000}},
        QVariantMap{{"id", "b"}, {"startMs", 3600000}, {"durationMs", 1000}}};
    model.setClips(clips);
    model.setViewport(0, 1000);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), TimelineClipModel::ModelDataRole)
                 .toMap()
                 .value("id")
                 .toString(),
             QString("a"));
    model.setViewport(3600000, 3601000);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), TimelineClipModel::ModelDataRole)
                 .toMap()
                 .value("id")
                 .toString(),
             QString("b"));
  }

  void timelineClipModelScrollsWithoutRebuildingDelegates() {
    // A transcript-length subtitle track on a seven-hour sequence: the shape
    // that froze the window.
    QVariantList clips;
    for (int i = 0; i < 12724; ++i)
      clips.append(QVariantMap{{"id", QStringLiteral("c%1").arg(i)},
                               {"startMs", i * 2000},
                               {"durationMs", 2000}});
    TimelineClipModel model;
    model.setClips(clips);
    model.setViewport(0, 5 * 60 * 1000);
    // Bounded by the visible span, not by the sequence duration.
    QVERIFY(model.rowCount() > 0);
    QVERIFY2(model.rowCount() < 400, "Viewport projection is not bounded");

    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

    // Scrolling by less than the gap between two cues cannot change which cues
    // are in the window, and the view is then told nothing whatsoever.
    for (int step = 1; step <= 19; ++step)
      model.setViewport(step * 100, step * 100 + 5 * 60 * 1000);
    QCOMPARE(resets.count(), 0);
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(changed.count(), 0);

    // A ten-minute sweep, a second at a time. Rows enter and leave at the two
    // edges and nothing else moves: no reset, and the total row churn is the
    // number of cues the window actually crossed. A resetting model would
    // instead have destroyed and rebuilt every visible delegate 600 times.
    const auto rowsIn = [](const QSignalSpy &spy) {
      int total = 0;
      for (const QList<QVariant> &call : spy)
        total += call.at(2).toInt() - call.at(1).toInt() + 1;
      return total;
    };
    for (int second = 1; second <= 600; ++second)
      model.setViewport(second * 1000, second * 1000 + 5 * 60 * 1000);
    QCOMPARE(resets.count(), 0);
    QCOMPARE(changed.count(), 0);
    QVERIFY(rowsIn(inserted) > 0);
    QVERIFY2(rowsIn(inserted) < 400, "Scrolling re-created rows it already had");
    QVERIFY2(rowsIn(removed) < 400, "Scrolling dropped rows it still needed");
    QVERIFY(model.rowCount() > 0);
    QVERIFY(model.rowCount() < 400);
    QCOMPARE(model.data(model.index(0, 0), TimelineClipModel::ModelDataRole)
                 .toMap()
                 .value("startMs")
                 .toLongLong(),
             568000LL);

    // Re-publishing the same clips is not a change and must stay silent.
    inserted.clear();
    removed.clear();
    changed.clear();
    model.setClips(clips);
    QCOMPARE(resets.count(), 0);
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(changed.count(), 0);

    // Editing a clip that is on screen refreshes that row in place.
    QVariantMap edited = clips.at(300).toMap();
    edited["durationMs"] = 2500;
    clips[300] = edited;
    model.setClips(clips);
    QCOMPARE(resets.count(), 0);
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(removed.count(), 0);
    QVERIFY(changed.count() > 0);
  }

  void realLongMediaProbeIsBoundedWhenAvailable() {
    const QString path = QStringLiteral(
        "C:/Users/USER/Downloads/Video/He Leveled Up 100x Faster Than Others And Now He Wants to Kill the Gods! - Manhwa Recap.mp4");
    if (!QFileInfo::exists(path))
      QSKIP("Long-media regression source is not available");
    Backend backend;
    QElapsedTimer timer;
    timer.start();
    const QVariantMap media = backend.probeMedia(path, false);
    QVERIFY2(timer.elapsed() < 5000, "Long-media metadata probe exceeded five seconds");
    QCOMPARE(media.value("kind").toString(), QString("video"));
    QVERIFY(media.value("durationMs").toLongLong() > 8LL * 60 * 60 * 1000);
    QCOMPARE(media.value("timelineRenderMode").toString(), QString("lightweight"));
    QVERIFY(media.value("thumbnailUrl").toString().isEmpty());
  }

  void realLongMediaImportAndPlacementStayResponsiveWhenAvailable() {
    const QString path = QStringLiteral(
        "C:/Users/USER/Downloads/Video/He Leveled Up 100x Faster Than Others And Now He Wants to Kill the Gods! - Manhwa Recap.mp4");
    if (!QFileInfo::exists(path))
      QSKIP("Long-media regression source is not available");
    Backend backend;
    QElapsedTimer timer;
    timer.start();
    QCOMPARE(backend.importMedia({path}), 1);
    QVERIFY2(timer.elapsed() < 5000, "Long-media import exceeded five seconds");
    const QVariantMap media = backend.media().constFirst().toMap();
    QCOMPARE(media.value("durationMs").toLongLong(), 29231740LL);

    timer.restart();
    const QStringList clips = backend.addMediaToTimeline(
        media.value("id").toString(), 0, QStringLiteral("V1"));
    QCOMPARE(clips.size(), 1);
    QVERIFY2(timer.elapsed() < 1000, "Long-media placement exceeded one second");
    QCOMPARE(backend.durationMs(), 29231740LL);
    QCOMPARE(backend.selectedClipId(), clips.constFirst());
    QCOMPARE(backend.timelineClipModel()->rowCount(), 1);
  }


  void mediaPathEncodesNonAsciiFolderNamesForFfmpeg() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // A Khmer folder name plus an emoji: neither survives QFile::encodeName()
    // on a Windows ANSI code page, which is what used to reach libavformat as
    // question marks and produced "No such file or directory" for a file that
    // is plainly there.
    const QString folder = dir.filePath(QString::fromUtf8("វីដេអូ 🎬"));
    const QString path = QDir(folder).filePath(QString::fromUtf8("ភាគ ០១.mp4"));
    QVERIFY(writeSampleFile(path));

    const QByteArray url = MediaPath::toFfmpegUrl(path);
    QVERIFY(url.startsWith("file:"));
    const QByteArray name = MediaPath::toFfmpegFileName(path);
    // Round-trips: the bytes libav receives decode back to the same path, which
    // is exactly what its utf8towchar() will do with them.
    QCOMPARE(QString::fromUtf8(name), MediaPath::nativeDecodePath(path));
    QVERIFY(QFileInfo::exists(QString::fromUtf8(name)));

    QString reason;
    QVERIFY(MediaPath::isDecodable(path, &reason));
    QVERIFY(reason.isEmpty());
  }

  void mediaPathRejectsUndecodableSourcesWithAReason() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString reason;

    QVERIFY(!MediaPath::isDecodable(QString(), &reason));
    QVERIFY(!reason.isEmpty());

    QVERIFY(!MediaPath::isDecodable(dir.filePath("missing.mp4"), &reason));
    QVERIFY(reason.contains("missing.mp4"));

    const QString empty = dir.filePath("empty.mp4");
    QVERIFY(writeSampleFile(empty, 0));
    QCOMPARE(QFileInfo(empty).size(), qint64(0));
    QVERIFY(!MediaPath::isDecodable(empty, &reason));
    QVERIFY(reason.contains("empty"));

    QVERIFY(!MediaPath::isDecodable(dir.path(), &reason));
    QVERIFY(!reason.isEmpty());
  }

  void mediaPathUsesExtendedFormOnlyForLongPaths() {
    const QString shortPath = QStringLiteral("C:/media/clip.mp4");
    QVERIFY(!MediaPath::nativeDecodePath(shortPath).startsWith(
        QStringLiteral("\\\\?\\")));

    const QString deep = QStringLiteral("C:/media/") +
                         QString(QStringLiteral("folder-name/")).repeated(30) +
                         QStringLiteral("clip.mp4");
    QVERIFY(deep.length() > MediaPath::kLongPathThreshold);
    const QString native = MediaPath::nativeDecodePath(deep);
#if defined(Q_OS_WIN)
    // Past MAX_PATH the Win32 API needs the extended-length prefix, and libav's
    // file protocol passes the name straight to CreateFileW.
    QVERIFY(native.startsWith(QStringLiteral("\\\\?\\")));
    QVERIFY(!native.contains(QLatin1Char('/')));
#else
    QCOMPARE(native, QDir::cleanPath(deep));
#endif
  }

  void framePoolPadsRowsToTheSwscaleAlignment() {
    // 64 px of RGBA8888 is exactly 256 bytes, already a multiple of 32.
    QCOMPARE(FrameBufferPool::bytesPerLine(64, QImage::Format_RGBA8888),
             qsizetype(256));
    // 65 px is 260 bytes, which swscale would write with unaligned stores; the
    // pool rounds it up to 288 instead.
    QCOMPARE(FrameBufferPool::bytesPerLine(65, QImage::Format_RGBA8888),
             qsizetype(288));
    QCOMPARE(FrameBufferPool::bytesPerLine(65, QImage::Format_RGBA8888) %
                 FrameBufferPool::kRowAlignment,
             qsizetype(0));
    QCOMPARE(FrameBufferPool::bytesPerLine(0, QImage::Format_RGBA8888),
             qsizetype(0));
  }

  void framePoolRecyclesBuffersAndDropsPastTheBudget() {
    const QSize size(64, 64);
    const qint64 frameBytes =
        qint64(FrameBufferPool::bytesPerLine(size.width(),
                                             QImage::Format_RGBA8888)) *
        size.height();
    QCOMPARE(frameBytes, qint64(16384));

    // Room for exactly two frames, which is also kMinimumBuffers: the third
    // concurrent request is the one that must be refused.
    const auto pool = FrameBufferPool::create(frameBytes * 2);
    QCOMPARE(pool->allocatedBytes(), qint64(0));

    QImage first = pool->acquire(size);
    QImage second = pool->acquire(size);
    QVERIFY(!first.isNull());
    QVERIFY(!second.isNull());
    QVERIFY(first.bits() != second.bits());
    QCOMPARE(first.bytesPerLine(), 256);
    QCOMPARE(pool->allocatedBytes(), frameBytes * 2);
    QCOMPARE(pool->liveBuffers(), 2);

    // Budget spent and nothing idle: the decoder is told to drop this frame
    // rather than being handed memory the pool cannot account for.
    QVERIFY(pool->acquire(size).isNull());
    QCOMPARE(pool->allocatedBytes(), frameBytes * 2);

    const uchar *recycled = second.bits();
    second = QImage();
    QCOMPARE(pool->liveBuffers(), 1);
    QCOMPARE(pool->idleBuffers(), 1);
    QCOMPARE(pool->idleBytes(), frameBytes);

    QImage third = pool->acquire(size);
    QVERIFY(!third.isNull());
    // Reused, not reallocated: the whole point of the pool.
    QCOMPARE(third.bits(), recycled);
    QCOMPARE(pool->allocatedBytes(), frameBytes * 2);
    QCOMPARE(pool->idleBuffers(), 0);

    // A geometry change trims the idle buffers so they do not sit on the budget
    // at the wrong size forever.
    first = QImage();
    third = QImage();
    QCOMPARE(pool->liveBuffers(), 0);
    pool->trim();
    QCOMPARE(pool->idleBuffers(), 0);
    QCOMPARE(pool->allocatedBytes(), qint64(0));

    QVERIFY(pool->acquire(QSize(0, 64)).isNull());
    QVERIFY(pool->acquire(QSize(64, -1)).isNull());
  }

  void framePoolKeepsMinimumBuffersWhenTheBudgetIsTiny() {
    // A budget far below one frame must still hand out kMinimumBuffers, or a
    // misconfigured budget would drop every frame and the monitor would stay
    // black forever.
    const auto pool = FrameBufferPool::create(16);
    const QSize size(32, 32);
    const QImage first = pool->acquire(size);
    const QImage second = pool->acquire(size);
    QVERIFY(!first.isNull());
    QVERIFY(!second.isNull());
    QCOMPARE(pool->liveBuffers(), FrameBufferPool::kMinimumBuffers);
    QVERIFY(pool->acquire(size).isNull());
  }

  void framePoolFramesOutliveThePool() {
    QImage frame;
    {
      const auto pool = FrameBufferPool::create(1 << 20);
      frame = pool->acquire(QSize(16, 16));
      QVERIFY(!frame.isNull());
      frame.fill(Qt::red);
    }
    // The release trampoline holds a weak_ptr, so the buffer is freed by the
    // trampoline itself instead of resurrecting a destroyed pool.
    QCOMPARE(frame.pixelColor(4, 4), QColor(Qt::red));
    frame = QImage();
  }

  void timelinePlacementCompletesOnEventLoop() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mediaPath = dir.filePath("placement.png");
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    QVERIFY(media.write("placement-test") > 0);
    media.close();

    Backend backend;
    QCOMPARE(backend.importMedia({mediaPath}), 1);
    const QString mediaId =
        backend.media().first().toMap().value("id").toString();
    QVERIFY(backend.beginTimelinePlacement({mediaId}, 0, "V1"));
    QVERIFY(backend.timelinePlacementInProgress());
    QTRY_VERIFY_WITH_TIMEOUT(!backend.timelinePlacementInProgress(), 2000);
    QCOMPARE(backend.clips().size(), 1);
  }

  void videoPreviewHelperCachesTimelineBoundaries() {
    VideoPreviewHelper helper;
    const QVariantList media{
        QVariantMap{{"id", "media-1"}, {"path", "one.mp4"}, {"kind", "video"}},
        QVariantMap{{"id", "media-2"}, {"path", "two.mp4"}, {"kind", "video"}}};
    const QVariantList clips{
        QVariantMap{{"id", "clip-1"}, {"mediaId", "media-1"},
                    {"kind", "video"}, {"track", "V1"}, {"startMs", 0},
                    {"durationMs", 10000}, {"enabled", true}},
        QVariantMap{{"id", "clip-2"}, {"mediaId", "media-2"},
                    {"kind", "video"}, {"track", "V2"}, {"startMs", 5000},
                    {"durationMs", 2000}, {"enabled", true}},
        QVariantMap{{"id", "subtitle-1"}, {"kind", "subtitle"},
                    {"track", "S1"}, {"startMs", 1000},
                    {"durationMs", 1000}, {"text", "Line"},
                    {"enabled", true}}};
    helper.setTrackStates({QVariantMap{{"id", "V1"}, {"visible", true}},
                           QVariantMap{{"id", "V2"}, {"visible", true}},
                           QVariantMap{{"id", "S1"}, {"visible", true}}});
    helper.setTimeline(clips, media);
    QSignalSpy changes(&helper, &VideoPreviewHelper::activeStateChanged);

    helper.setPosition(500);
    QCOMPARE(helper.activeClipId(), QString("clip-1"));
    const int stableCount = changes.count();
    for (int position = 510; position < 1000; position += 10)
      helper.setPosition(position);
    QCOMPARE(changes.count(), stableCount);

    helper.setPosition(1000);
    QCOMPARE(helper.activeSubtitle().toMap().value("id").toString(),
             QString("subtitle-1"));
    helper.setPosition(5000);
    QCOMPARE(helper.activeClipId(), QString("clip-2"));
    helper.setPosition(7000);
    QCOMPARE(helper.activeClipId(), QString("clip-1"));
  }

  void videoPreviewHelperResolvesOverlappingAudioClips() {
    VideoPreviewHelper helper;
    const QVariantList media{
        QVariantMap{{"id", "video-media"}, {"path", "video.mp4"},
                    {"kind", "video"}},
        QVariantMap{{"id", "tts-media-1"}, {"path", "tts-1.mp3"},
                    {"kind", "audio"}},
        QVariantMap{{"id", "tts-media-2"}, {"path", "tts-2.mp3"},
                    {"kind", "audio"}}};
    const QVariantList clips{
        QVariantMap{{"id", "video-clip"}, {"mediaId", "video-media"},
                    {"kind", "video"}, {"track", "V1"}, {"startMs", 0},
                    {"durationMs", 10000}, {"enabled", true}},
        QVariantMap{{"id", "tts-clip-1"}, {"mediaId", "tts-media-1"},
                    {"kind", "audio"}, {"track", "A1"}, {"startMs", 1000},
                    {"durationMs", 3000}, {"enabled", true}},
        QVariantMap{{"id", "tts-clip-2"}, {"mediaId", "tts-media-2"},
                    {"kind", "audio"}, {"track", "A2"}, {"startMs", 2000},
                    {"durationMs", 3000}, {"enabled", true}}};
    helper.setTrackStates({QVariantMap{{"id", "V1"}, {"visible", true}},
                           QVariantMap{{"id", "A1"}, {"visible", true}},
                           QVariantMap{{"id", "A2"}, {"visible", true}}});
    helper.setTimeline(clips, media);

    helper.setPosition(500);
    QCOMPARE(helper.activeAudioClips().size(), 0);
    helper.setPosition(1500);
    QCOMPARE(helper.activeAudioClips().size(), 1);
    QCOMPARE(helper.activeAudioClips().first().toMap().value("id").toString(),
             QString("tts-clip-1"));
    helper.setPosition(2500);
    QCOMPARE(helper.activeAudioClips().size(), 2);
    helper.setPosition(4500);
    QCOMPARE(helper.activeAudioClips().size(), 1);
    QCOMPARE(helper.activeAudioClips().first().toMap().value("id").toString(),
             QString("tts-clip-2"));

    helper.setTrackStates({QVariantMap{{"id", "V1"}, {"visible", true}},
                           QVariantMap{{"id", "A1"}, {"visible", false}},
                           QVariantMap{{"id", "A2"}, {"visible", true}}});
    helper.setPosition(2500);
    QCOMPARE(helper.activeAudioClips().size(), 1);
    QCOMPARE(helper.activeAudioClips().first().toMap().value("id").toString(),
             QString("tts-clip-2"));
  }

  void subtitleTimelineClampsOverlappingTranscriptWindows() {
    const QVariantList clips = SubtitleTimeline::clipsFromTranscript({
        QVariantMap{{"start", 0.0}, {"end", 2.0}, {"text", "first"}},
        QVariantMap{{"start", 1.5}, {"end", 3.0}, {"text", "second"}},
        QVariantMap{{"start", 3.0}, {"end", 4.0}, {"text", "third"}}});
    QCOMPARE(clips.size(), 3);
    const QVariantMap first = clips.at(0).toMap();
    const QVariantMap second = clips.at(1).toMap();
    const QVariantMap third = clips.at(2).toMap();
    QCOMPARE(first.value("startMs").toLongLong(), 0LL);
    QCOMPARE(first.value("durationMs").toLongLong(), 2000LL);
    QCOMPARE(second.value("startMs").toLongLong(), 2000LL);
    QCOMPARE(second.value("durationMs").toLongLong(), 1000LL);
    QCOMPARE(third.value("startMs").toLongLong(), 3000LL);
  }

  void subtitleImportSeparatesRollingYouTubeWindows() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("rolling.ttml");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    // The shape YouTube actually hands you: a two-line scroll, so each cue's
    // end is the start of the cue two places later. The .720/.879 values are the
    // ones whose doubles sit just below the millisecond they name.
    file.write(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<tt xml:lang=\"en\" xmlns=\"http://www.w3.org/ns/ttml\">\n"
        "<body><div>\n"
        "<p begin=\"00:00:00.240\" end=\"00:00:04.720\">one</p>\n"
        "<p begin=\"00:00:02.000\" end=\"00:00:06.879\">two</p>\n"
        "<p begin=\"00:00:04.720\" end=\"00:00:08.720\">three</p>\n"
        "<p begin=\"00:00:06.879\" end=\"00:00:10.719\">four</p>\n"
        "</div></body></tt>\n");
    file.close();

    QString error;
    QString language;
    const QVariantList cues = SubtitleIO::read(path, &error, &language);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(cues.size(), 4);
    QCOMPARE(language, QString("en"));

    // Every cue but the last is trimmed back to just short of its successor, so
    // no instant of the track belongs to two cues at once.
    const auto ms = [](const QVariant &cue, const char *key) {
      return qRound64(cue.toMap().value(QLatin1String(key)).toDouble() * 1000.0);
    };
    QCOMPARE(ms(cues.at(0), "start"), 240LL);
    QCOMPARE(ms(cues.at(0), "end"), 1999LL);
    QCOMPARE(ms(cues.at(1), "start"), 2000LL);
    QCOMPARE(ms(cues.at(1), "end"), 4719LL);
    QCOMPARE(ms(cues.at(2), "start"), 4720LL);
    QCOMPARE(ms(cues.at(2), "end"), 6878LL);
    // The last cue has no successor to make room for, so it keeps the length it
    // was authored with.
    QCOMPARE(ms(cues.at(3), "start"), 6879LL);
    QCOMPARE(ms(cues.at(3), "end"), 10719LL);
    for (int i = 0; i + 1 < cues.size(); ++i)
      QVERIFY(ms(cues.at(i), "end") < ms(cues.at(i + 1), "start"));

    // And the file that comes back out says the same numbers: the writer rounds
    // to the millisecond it was given rather than truncating toward the one
    // below it.
    const QString out = dir.filePath("rolling.srt");
    QVERIFY2(SubtitleIO::writeSrt(out, cues, &error), qPrintable(error));
    QFile written(out);
    QVERIFY(written.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(written.readAll());
    QVERIFY2(text.contains("00:00:00,240 --> 00:00:01,999"), qPrintable(text));
    QVERIFY2(text.contains("00:00:02,000 --> 00:00:04,719"), qPrintable(text));
    QVERIFY2(text.contains("00:00:04,720 --> 00:00:06,878"), qPrintable(text));
    QVERIFY2(text.contains("00:00:06,879 --> 00:00:10,719"), qPrintable(text));
  }

  void subtitleDeoverlapSparesSimultaneousDialogueOnly() {
    // Two speakers sharing cue 1's start, then ordinary sequential prose. The
    // simultaneous pair keeps its length; the pair after it is still trimmed,
    // which a whole-list precondition could not do.
    QVariantList cues{
        QVariantMap{{"start", 1.0}, {"end", 4.0}, {"text", "speaker one"}},
        QVariantMap{{"start", 1.0}, {"end", 4.0}, {"text", "speaker two"}},
        QVariantMap{{"start", 4.0}, {"end", 8.0}, {"text", "next line"}},
        QVariantMap{{"start", 6.0}, {"end", 9.0}, {"text", "last line"}}};
    QCOMPARE(SubtitleIO::deoverlap(cues), 2);
    const auto ms = [&cues](int i, const char *key) {
      return qRound64(cues.at(i).toMap().value(QLatin1String(key)).toDouble() *
                      1000.0);
    };
    // Cue 0's successor begins at the same moment it does, so there is no room
    // to trim it into and it keeps the length it was authored with.
    QCOMPARE(ms(0, "end"), 4000LL);
    // Cue 1 is still trimmed against cue 2 - the simultaneous pair above does
    // not excuse the rest of the list, which is what a whole-list precondition
    // could not express.
    QCOMPARE(ms(1, "end"), 3999LL);
    QCOMPARE(ms(2, "end"), 5999LL);
    // Nothing follows the last cue, so nothing is taken off it.
    QCOMPARE(ms(3, "end"), 9000LL);
  }

  void effectStackFramework() {
    const QVariantList definitions = EffectRegistry::definitions();
    QVERIFY(definitions.size() >= 12);
    const QVariantMap brightness =
        EffectRegistry::definition("brightness_contrast");
    const QVariantMap blur = EffectRegistry::definition("gaussian_blur");
    const QVariantMap compressor = EffectRegistry::definition("compressor");
    QVERIFY(!brightness.isEmpty());
    QVERIFY(EffectRegistry::supportsClip(brightness, "video", true));
    QVERIFY(!EffectRegistry::supportsClip(brightness, "audio", true));
    QVERIFY(EffectRegistry::supportsClip(compressor, "audio", true));
    QVERIFY(!EffectRegistry::supportsClip(compressor, "video", false));

    QVariantList stack;
    QVERIFY(EffectStack::append(&stack, brightness, "fx-1"));
    QVERIFY(EffectStack::append(&stack, blur, "fx-2"));
    QVERIFY(EffectStack::setParameter(&stack, "fx-1", brightness,
                                      "brightness", 150));
    QCOMPARE(stack.first().toMap()
                 .value("parameters")
                 .toMap()
                 .value("brightness")
                 .toDouble(),
             100.0);
    QVERIFY(EffectStack::move(&stack, "fx-2", -1));
    QCOMPARE(stack.first().toMap().value("id").toString(), QString("fx-2"));
    QVERIFY(EffectStack::setEnabled(&stack, "fx-2", false));
    QVERIFY(EffectStack::reset(&stack, "fx-1", brightness));

    const QString video = VideoEffectPipeline::filters(stack);
    QVERIFY(video.contains("eq=brightness="));
    QVERIFY(!video.contains("gblur="));

    QVariantList audioStack;
    const QVariantMap highPass = EffectRegistry::definition("high_pass");
    const QVariantMap limiter = EffectRegistry::definition("limiter");
    QVERIFY(EffectStack::append(&audioStack, highPass, "audio-1"));
    QVERIFY(EffectStack::append(&audioStack, limiter, "audio-2"));
    const QString audio = AudioEffectPipeline::filters(audioStack, 2);
    QVERIFY(audio.contains("highpass=f="));
    QVERIFY(audio.contains("alimiter=limit="));

    QVERIFY(EffectStack::remove(&stack, "fx-2"));
    QCOMPARE(stack.size(), 1);
    QCOMPARE(EffectStack::normalized(stack).size(), 1);
  }

  void customBlurMaskPipeline() {
    const QVariantMap definition = EffectRegistry::definition("custom_blur");
    QVERIFY(!definition.isEmpty());
    QVariantList stack;
    QVERIFY(EffectStack::append(&stack, definition, "custom-1"));
    QVERIFY(EffectStack::setParameter(&stack, "custom-1", definition,
                                      "amount", 18.0));
    const QVariantMap mask{{"x", 0.12},
                           {"y", 0.23},
                           {"width", 0.44},
                           {"height", 0.31}};
    QVERIFY(EffectStack::setParameter(&stack, "custom-1", definition, "mask",
                                      mask));

    const QVariantList masks = CustomBlurPipeline::enabledMasks(stack);
    QCOMPARE(masks.size(), 1);
    QCOMPARE(masks.first().toMap().value("amount").toDouble(), 18.0);
    QStringList filters;
    const QString output = CustomBlurPipeline::appendFilters(
        &filters, "input", "masked", masks);
    QCOMPARE(output, QString("masked_masked0"));
    QCOMPARE(filters.size(), 3);
    QVERIFY(filters.at(1).contains("crop=w="));
    QVERIFY(filters.at(1).contains("gblur=sigma=18.000"));
    QVERIFY(filters.at(2).contains("overlay=x='main_w*0.120000'"));
  }

  void customBlurBackendEditingAndDefaultExportPath() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mediaPath = dir.filePath("still.png");
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    QVERIFY(media.write("blur-test") > 0);
    media.close();

    Backend backend;
    QVERIFY(backend.newProject("Blur", dir.path(), true, "Masked Scene"));
    QCOMPARE(backend.importMedia({mediaPath}), 1);
    const QString mediaId =
        backend.media().first().toMap().value("id").toString();
    const QString clipId = backend.addClip(mediaId, 0, "V1");
    QVERIFY(!clipId.isEmpty());
    const QString effectId = backend.addClipEffect(clipId, "custom_blur");
    QVERIFY(!effectId.isEmpty());
    QVERIFY(backend.beginCustomBlurMaskEdit(clipId, effectId));
    QCOMPARE(backend.customBlurEditClipId(), clipId);
    QCOMPARE(backend.customBlurEditInstanceId(), effectId);
    QVERIFY(backend.setCustomBlurMask(clipId, effectId, 0.1, 0.2, 0.5, 0.4));

    const QVariantMap clip = backend.selectedClip();
    const QVariantMap savedMask = clip.value("effectStack")
                                      .toList()
                                      .first()
                                      .toMap()
                                      .value("parameters")
                                      .toMap()
                                      .value("mask")
                                      .toMap();
    QCOMPARE(savedMask.value("x").toDouble(), 0.1);
    QCOMPARE(savedMask.value("height").toDouble(), 0.4);
    QVERIFY(backend.suggestedExportPath().contains("Exports"));
    QVERIFY(backend.suggestedExportPath().endsWith("Masked Scene.mp4"));

    backend.endCustomBlurMaskEdit();
    QVERIFY(backend.customBlurEditClipId().isEmpty());
  }

  void expandedEffectRegistryBuildsNativeFilters() {
    QVERIFY(EffectRegistry::definitions().size() >= 29);

    QVariantList videoStack;
    const QStringList videoIds{"color_temperature", "pixelate", "edge_detect",
                               "sepia", "lens_correction", "deinterlace"};
    for (int i = 0; i < videoIds.size(); ++i) {
      const QVariantMap definition = EffectRegistry::definition(videoIds.at(i));
      QVERIFY2(!definition.isEmpty(), qPrintable(videoIds.at(i)));
      QVERIFY(EffectStack::append(&videoStack, definition,
                                  QStringLiteral("video-%1").arg(i)));
    }
    QVERIFY(EffectStack::setParameter(
        &videoStack, "video-4", EffectRegistry::definition("lens_correction"),
        "k1", -12));
    const QString video = VideoEffectPipeline::filters(videoStack);
    QVERIFY(video.contains("colortemperature="));
    QVERIFY(video.contains("pixelize="));
    QVERIFY(video.contains("edgedetect="));
    QVERIFY(video.contains("colorchannelmixer="));
    QVERIFY(video.contains("lenscorrection="));
    QVERIFY(video.contains("yadif="));

    QVariantList audioStack;
    const QStringList audioIds{"bass", "treble", "deesser", "noise_gate",
                               "loudness_normalize", "chorus", "flanger",
                               "stereo_widener"};
    for (int i = 0; i < audioIds.size(); ++i) {
      const QVariantMap definition = EffectRegistry::definition(audioIds.at(i));
      QVERIFY2(!definition.isEmpty(), qPrintable(audioIds.at(i)));
      QVERIFY(EffectStack::append(&audioStack, definition,
                                  QStringLiteral("audio-%1").arg(i)));
    }
    const QString audio = AudioEffectPipeline::filters(audioStack, 2);
    QVERIFY(audio.contains("bass="));
    QVERIFY(audio.contains("treble="));
    QVERIFY(audio.contains("deesser="));
    QVERIFY(audio.contains("agate="));
    QVERIFY(audio.contains("loudnorm="));
    QVERIFY(audio.contains("chorus="));
    QVERIFY(audio.contains("flanger="));
    QVERIFY(audio.contains("stereowiden="));
  }

  void lumetriSectionBypassFlags() {
    QVariantMap settings = ColorSettings::clipDefaults();
    QVERIFY(ColorSettings::setClipValue(&settings, "basicEnabled", false));
    QVERIFY(ColorSettings::setClipValue(&settings, "creativeEnabled", false));
    QVERIFY(ColorSettings::setClipValue(&settings, "curvesEnabled", false));
    QVERIFY(ColorSettings::setClipValue(&settings, "colorWheelsEnabled", false));
    QVERIFY(ColorSettings::setClipValue(&settings, "hslSecondaryEnabled", false));
    QVERIFY(ColorSettings::setClipValue(&settings, "vignetteEnabled", false));
    settings["exposure"] = 2.0;
    settings["temperature"] = 50.0;
    settings["sharpen"] = 50.0;
    settings["masterCurvePoints"] =
        QVariantList{QVariantMap{{"x", 0.0}, {"y", 0.2}},
                     QVariantMap{{"x", 1.0}, {"y", 0.8}}};
    settings["shadowWheelX"] = 50.0;
    settings["hslCorrectionHue"] = 30.0;
    settings["hslBlur"] = 25.0;
    settings["vignette"] = -50.0;
    const QString bypassed = LumetriPipeline::filterForClip(
        QVariantMap{{"lumetri", settings}}, {}, {});
    QVERIFY(bypassed.isEmpty());

    settings["curvesEnabled"] = true;
    settings["rgbCurvesEnabled"] = false;
    settings["hueVsHuePoints"] =
        QVariantList{QVariantMap{{"x", 0.0}, {"y", 0.3}},
                     QVariantMap{{"x", 1.0}, {"y", 0.7}}};
    settings["hueVsHueEnabled"] = false;
    const QString subgroupsBypassed = LumetriPipeline::filterForClip(
        QVariantMap{{"lumetri", settings}}, {}, {});
    QVERIFY(!subgroupsBypassed.contains("curves="));
    QVERIFY(!subgroupsBypassed.contains("hue="));
  }

  void timelinePlacementRules() {
    QCOMPARE(TimelinePlacement::normalizedTrack(" v02 "), QString("V2"));
    QCOMPARE(TimelinePlacement::normalizedTrack("A64"), QString("A64"));
    QVERIFY(TimelinePlacement::normalizedTrack("V0").isEmpty());
    QVERIFY(TimelinePlacement::normalizedTrack("S2").isEmpty());
    QVERIFY(TimelinePlacement::normalizedTrack("video1").isEmpty());

    QCOMPARE(TimelinePlacement::defaultTrackForKind("video"), QString("V1"));
    QCOMPARE(TimelinePlacement::defaultTrackForKind("image"), QString("V1"));
    QCOMPARE(TimelinePlacement::defaultTrackForKind("audio"), QString("A1"));
    QCOMPARE(TimelinePlacement::defaultTrackForKind("subtitle"), QString("S1"));

    QVERIFY(TimelinePlacement::trackAcceptsKind("V3", "video"));
    QVERIFY(TimelinePlacement::trackAcceptsKind("V3", "image"));
    QVERIFY(TimelinePlacement::trackAcceptsKind("A3", "audio"));
    QVERIFY(TimelinePlacement::trackAcceptsKind("S1", "subtitle"));
    QVERIFY(!TimelinePlacement::trackAcceptsKind("A3", "video"));
    QVERIFY(!TimelinePlacement::trackAcceptsKind("V3", "audio"));

    QCOMPARE(TimelinePlacement::shiftedTrack("V2", 2), QString("V4"));
    QCOMPARE(TimelinePlacement::shiftedTrack("A2", -1), QString("A1"));
    QVERIFY(TimelinePlacement::shiftedTrack("V1", -1).isEmpty());
    QVERIFY(TimelinePlacement::shiftedTrack("S1", 1).isEmpty());
  }

  void applicationSettingsPersistAndDriveDefaults() {
    Backend backend;
    QVariantMap settings = backend.appSettings();
    settings["appearanceBrightness"] = 72;
    settings["accentColor"] = QStringLiteral("#29a3a3");
    settings["masterVolume"] = 45;
    settings["defaultImageDurationMs"] = 7000;
    settings["defaultVideoTracks"] = 3;
    settings["defaultAudioTracks"] = 2;
    settings["timelineTrackHeight"] = 84;
    settings["transcriptionModel"] = QStringLiteral("small");
    settings["transcriptionLanguage"] = QStringLiteral("km");
    settings["translationProvider"] = QStringLiteral("gemini");
    settings["translationGeminiModel"] = QStringLiteral("gemini-3.1-flash-lite");
    settings["translationGeminiApiKeys"] =
        QStringLiteral("test-gemini-key-1\ntest-gemini-key-2");
    settings["translationTabitokenModel"] = QStringLiteral("claude-opus-4-8");
    settings["translationTabitokenBaseUrl"] =
        QStringLiteral("https://tabitoken.com/v1");
    settings["translationTabitokenApiKeys"] =
        QStringLiteral("test-tabi-key-1\ntest-tabi-key-2");
    QVERIFY(backend.applyAppSettings(settings));
    QCOMPARE(backend.appSettings().value("appearanceBrightness").toInt(), 72);
    QCOMPARE(backend.appSettings().value("masterVolume").toInt(), 45);

    Backend reloaded;
    QCOMPARE(reloaded.appSettings().value("accentColor").toString(),
             QString("#29a3a3"));
    QCOMPARE(reloaded.appSettings().value("timelineTrackHeight").toInt(), 84);
    QCOMPARE(reloaded.appSettings().value("transcriptionModel").toString(),
             QString("small"));
    QCOMPARE(reloaded.appSettings().value("transcriptionLanguage").toString(),
             QString("km"));
    QCOMPARE(reloaded.appSettings()
                 .value("translationGeminiApiKeys")
                 .toString(),
             QString("test-gemini-key-1\ntest-gemini-key-2"));
    QCOMPARE(reloaded.appSettings()
                 .value("translationTabitokenApiKeys")
                 .toString(),
             QString("test-tabi-key-1\ntest-tabi-key-2"));

    QTemporaryDir projectDir;
    QVERIFY(projectDir.isValid());
    QVERIFY(reloaded.newProject("Settings", projectDir.path()));
    QCOMPARE(reloaded.videoTrackCount(), 3);
    QCOMPARE(reloaded.audioTrackCount(), 2);
    const QString imagePath = projectDir.filePath("still.png");
    QFile image(imagePath);
    QVERIFY(image.open(QIODevice::WriteOnly));
    QVERIFY(image.write("settings-image") > 0);
    image.close();
    QCOMPARE(reloaded.importMedia({imagePath}), 1);
    QVERIFY(!reloaded.addClip(
                         reloaded.media().first().toMap().value("id").toString())
                 .isEmpty());
    QCOMPARE(reloaded.clips().first().toMap().value("durationMs").toLongLong(),
             qint64(7000));

    const QString previewFolder =
        QDir(AppSettings::cacheRoot()).filePath("timeline-previews");
    QVERIFY(QDir().mkpath(previewFolder));
    QFile cacheFile(QDir(previewFolder).filePath("settings-test.cache"));
    QVERIFY(cacheFile.open(QIODevice::WriteOnly));
    QVERIFY(cacheFile.write("cache") > 0);
    cacheFile.close();
    QVERIFY(reloaded.clearMediaCache());
    QVERIFY(!QFileInfo::exists(cacheFile.fileName()));
  }

  void sqliteSnapshotOverridesStaleJsonAndPreservesTranscript() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString subtitlePath = dir.filePath("source.srt");
    QFile subtitle(subtitlePath);
    QVERIFY(subtitle.open(QIODevice::WriteOnly | QIODevice::Text));
    subtitle.write("1\n00:00:01,000 --> 00:00:02,500\nSQLite transcript\n");
    subtitle.close();

    Backend backend;
    QVERIFY(backend.newProject("SQLite Source", dir.path(), true, "Main"));
    QVERIFY(backend.importSubtitles(subtitlePath));
    const QString projectPath = dir.filePath("sqlite-project.cutpro.json");
    QVERIFY2(backend.saveProject(projectPath), qPrintable(backend.lastError()));
    QVERIFY(QFileInfo::exists(projectPath + QStringLiteral(".sqlite")));

    QFile staleFile(projectPath);
    QVERIFY(staleFile.open(QIODevice::ReadOnly));
    QJsonObject stale = QJsonDocument::fromJson(staleFile.readAll()).object();
    staleFile.close();
    stale["projectName"] = QStringLiteral("Stale JSON");
    stale["transcript"] = QJsonArray();
    QVERIFY(staleFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(staleFile.write(QJsonDocument(stale).toJson()) > 0);
    staleFile.close();

    Backend loaded;
    QVERIFY(loaded.loadProject(projectPath));
    QCOMPARE(loaded.projectName(), QString("SQLite Source"));
    QCOMPARE(loaded.transcript().size(), 1);
    QCOMPARE(loaded.transcript().first().toMap().value("text").toString(),
             QString("SQLite transcript"));
  }

  void projectMediaCanBeRenamedWithoutChangingSourceFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mediaPath = dir.filePath("original.png");
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    QVERIFY(media.write("rename-test") > 0);
    media.close();

    Backend backend;
    QCOMPARE(backend.importMedia({mediaPath}), 1);
    const QString mediaId =
        backend.media().first().toMap().value("id").toString();

    QVERIFY(!backend.renameMedia(mediaId, "   "));
    QVERIFY(backend.renameMedia(mediaId, "Opening shot"));
    const QVariantMap renamed = backend.media().first().toMap();
    QCOMPARE(renamed.value("name").toString(), QString("Opening shot"));
    QCOMPARE(renamed.value("path").toString(),
             QFileInfo(mediaPath).absoluteFilePath());
    QVERIFY(QFileInfo::exists(mediaPath));

    backend.undo();
    QCOMPARE(backend.media().first().toMap().value("name").toString(),
             QString("original.png"));
  }

  void projectTimelinePersistence() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString mediaPath = dir.filePath("still.png");
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    QVERIFY(media.write("not-a-real-image") > 0);
    media.close();

    Backend backend;
    QVERIFY(backend.newProject("Demo", dir.path(), true, "Main"));
    QCOMPARE(backend.projectName(), QString("Demo"));
    QCOMPARE(backend.importMedia({mediaPath}), 1);
    QCOMPARE(backend.mediaCount(), 1);

    const QString mediaId =
        backend.media().first().toMap().value("id").toString();
    const QString clipId = backend.addClip(mediaId);
    QVERIFY(!clipId.isEmpty());
    QSignalSpy effectControlsRequested(&backend,
                                       SIGNAL(effectControlsRequested()));
    QVERIFY(effectControlsRequested.isValid());
    const QString brightnessEffect =
        backend.addClipEffect(clipId, "brightness_contrast");
    QVERIFY(!brightnessEffect.isEmpty());
    QCOMPARE(backend.selectedClipId(), clipId);
    QCOMPARE(effectControlsRequested.count(), 1);
    QVERIFY(backend.setClipEffectParameter(clipId, brightnessEffect,
                                           "contrast", 22));
    const QString blurEffect = backend.addClipEffect(clipId, "gaussian_blur");
    QVERIFY(!blurEffect.isEmpty());
    QVERIFY(backend.moveClipEffect(clipId, blurEffect, -1));
    QVERIFY(backend.setClipEffectEnabled(clipId, blurEffect, false));
    QCOMPARE(backend.durationMs(), qint64(5000));
    QVERIFY(backend.canExport());
    QCOMPARE(backend.videoTrackCount(), 1);
    QCOMPARE(backend.audioTrackCount(), 1);

    QVERIFY(backend.setColorSetting("workingColorSpace", "Rec. 2100 HLG"));
    QVERIFY(backend.setColorSetting("autoToneMapMedia", false));
    QVERIFY(backend.setColorSetting("transmitPort", 5000));
    QVERIFY(backend.setClipColorSetting(clipId, "exposure", 1.2));
    QVERIFY(backend.setClipColorSetting(clipId, "temperature", 18));
    QVERIFY(backend.setClipColorSetting(clipId, "curvePreset",
                                        "Increase Contrast"));
    QVERIFY(backend.setClipColorSetting(clipId, "redCurveHighlights", 14));
    QVERIFY(backend.setClipColorSetting(
        clipId, "masterCurvePoints",
        QVariantList{QVariantMap{{"x", 0.0}, {"y", 0.0}},
                     QVariantMap{{"x", 0.4}, {"y", 0.32}},
                     QVariantMap{{"x", 1.0}, {"y", 1.0}}}));
    QVERIFY(backend.setClipEffectSetting(clipId, "scale", 115));
    QVERIFY(backend.setClipEffectSetting(clipId, "positionX", 8));
    QVERIFY(backend.setClipEffectSetting(clipId, "opacity", 92));
    QVERIFY(backend.setMediaColorSetting(mediaId, "useMediaColorSpace", true));
    QVERIFY(backend.setMediaColorSetting(mediaId, "overrideMediaColorSpace",
                                         "Rec. 709"));

    QVERIFY(backend.trimClipStart(clipId, 1000));
    QVariantMap trimmedClip = backend.clips().first().toMap();
    QCOMPARE(trimmedClip.value("startMs").toLongLong(), qint64(1000));
    QCOMPARE(trimmedClip.value("sourceInMs").toLongLong(), qint64(1000));
    QCOMPARE(trimmedClip.value("durationMs").toLongLong(), qint64(4000));
    QVERIFY(backend.trimClipStart(clipId, 0));
    trimmedClip = backend.clips().first().toMap();
    QCOMPARE(trimmedClip.value("startMs").toLongLong(), qint64(0));
    QCOMPARE(trimmedClip.value("sourceInMs").toLongLong(), qint64(0));
    QCOMPARE(trimmedClip.value("durationMs").toLongLong(), qint64(5000));

    QVERIFY(backend.trimClipEnd(clipId, 4000));
    QCOMPARE(backend.clips().first().toMap().value("durationMs").toLongLong(),
             qint64(4000));
    QVERIFY(backend.trimClipEnd(clipId, 5000));
    QCOMPARE(backend.clips().first().toMap().value("durationMs").toLongLong(),
             qint64(5000));
    backend.undo();
    QCOMPARE(backend.clips().first().toMap().value("durationMs").toLongLong(),
             qint64(4000));
    backend.redo();
    QCOMPARE(backend.clips().first().toMap().value("durationMs").toLongLong(),
             qint64(5000));

    QVERIFY(!backend.moveClip(clipId, 0, "A1"));
    QCOMPARE(backend.clips().first().toMap().value("track").toString(),
             QString("V1"));
    QVERIFY(backend.addClip(mediaId, 0, "A1").isEmpty());
    QVERIFY(backend.moveClips({clipId}, 1000, 1));
    QCOMPARE(backend.videoTrackCount(), 2);
    QCOMPARE(backend.clips().first().toMap().value("track").toString(),
             QString("V2"));
    QCOMPARE(backend.clips().first().toMap().value("startMs").toLongLong(),
             qint64(1000));
    QVERIFY(backend.moveClips({clipId}, -1000, -1));
    QCOMPARE(backend.videoTrackCount(), 1);
    QCOMPARE(backend.clips().first().toMap().value("track").toString(),
             QString("V1"));

    backend.setCaptionFontFamily("Noto Sans Khmer");
    backend.setCaptionFontSize(54);
    backend.setCaptionTextColor("#ffcc00");
    backend.setCaptionBackgroundVisible(false);
    backend.setCaptionPosition("top");
    backend.setCaptionAlignment("left");
    backend.setCaptionBlurEnabled(true);
    backend.setCaptionBlurTrackingEnabled(false);
    backend.setCaptionBlurRegionNormalized(0.12, 0.18, 0.55, 0.22);
    backend.setCaptionBlurStrength(46);
    backend.setCaptionBlurPadding(18);
    backend.setCaptionPositionNormalized(0.32, 0.74);

    const QString pruneClipId = backend.addClip(mediaId, 0, "V1");
    QCOMPARE(backend.videoTrackCount(), 1);
    QVERIFY(backend.moveClip(pruneClipId, 0, "V2"));
    QCOMPARE(backend.videoTrackCount(), 2);
    QVERIFY(backend.removeClip(pruneClipId));
    QCOMPARE(backend.videoTrackCount(), 1);

    QVERIFY(backend.moveClips({clipId}, 0, 3));
    QCOMPARE(backend.videoTrackCount(), 4);
    QCOMPARE(backend.clips().first().toMap().value("track").toString(),
             QString("V4"));
    QVERIFY(backend.moveClips({clipId}, 0, -2));
    QCOMPARE(backend.videoTrackCount(), 2);
    QCOMPARE(backend.clips().first().toMap().value("track").toString(),
             QString("V2"));
    QVERIFY(backend.moveClips({clipId}, 0, -1));
    QCOMPARE(backend.videoTrackCount(), 1);

    QCOMPARE(backend.addTrack("video"), QString("V2"));
    QCOMPARE(backend.videoTrackCount(), 2);
    QVERIFY(backend.removeLastTrack("video"));
    QCOMPARE(backend.addTrack("video"), QString("V2"));
    QVERIFY(backend.moveClip(clipId, 0, "V2"));
    QVERIFY(!backend.removeLastTrack("video"));
    QVERIFY(backend.moveClip(clipId, 0, "V1"));
    QCOMPARE(backend.videoTrackCount(), 1);

    QVERIFY(backend.splitClip(clipId, 2000));
    QCOMPARE(backend.clips().size(), 2);
    backend.undo();
    QCOMPARE(backend.clips().size(), 1);
    backend.redo();
    QCOMPARE(backend.clips().size(), 2);

    const QStringList splitIds{
        backend.clips().at(0).toMap().value("id").toString(),
        backend.clips().at(1).toMap().value("id").toString()};
    QVERIFY(backend.removeClips(splitIds));
    QCOMPARE(backend.clips().size(), 0);
    backend.undo();
    QCOMPARE(backend.clips().size(), 2);

    QVERIFY(backend.setTrackMuted("V1", true));
    QVERIFY(backend.mutedTracks().contains("V1"));
    backend.undo();
    QVERIFY(!backend.mutedTracks().contains("V1"));
    backend.redo();
    QVERIFY(backend.mutedTracks().contains("V1"));

    const QString projectPath = dir.filePath("demo.cutpro.json");
    QVERIFY(backend.saveProject(projectPath));
    QVERIFY(!backend.dirty());

    Backend loaded;
    QVERIFY(loaded.loadProject(projectPath));
    const QVariantList loadedStack =
        loaded.clips().first().toMap().value("effectStack").toList();
    QCOMPARE(loadedStack.size(), 2);
    QCOMPARE(loadedStack.first().toMap().value("definitionId").toString(),
             QString("gaussian_blur"));
    QVERIFY(!loadedStack.first().toMap().value("enabled").toBool());
    QCOMPARE(loaded.projectName(), QString("Demo"));
    QCOMPARE(loaded.mediaCount(), 1);
    QCOMPARE(loaded.clips().size(), 2);
    QCOMPARE(loaded.durationMs(), qint64(5000));
    QCOMPARE(loaded.videoTrackCount(), 1);
    QCOMPARE(loaded.audioTrackCount(), 1);
    QVERIFY(loaded.mutedTracks().contains("V1"));
    QCOMPARE(loaded.captionFontFamily(), QString("Noto Sans Khmer"));
    QCOMPARE(loaded.captionFontSize(), 54);
    QCOMPARE(loaded.captionTextColor(), QString("#ffcc00"));
    QVERIFY(!loaded.captionBackgroundVisible());
    QCOMPARE(loaded.captionPosition(), QString("custom"));
    QCOMPARE(loaded.captionAlignment(), QString("left"));
    QCOMPARE(loaded.captionPositionX(), 0.32);
    QCOMPARE(loaded.captionPositionY(), 0.74);
    QVERIFY(loaded.captionBlurEnabled());
    QVERIFY(!loaded.captionBlurTrackingEnabled());
    QCOMPARE(loaded.captionBlurRegionX(), 0.12);
    QCOMPARE(loaded.captionBlurRegionY(), 0.18);
    QCOMPARE(loaded.captionBlurRegionWidth(), 0.55);
    QCOMPARE(loaded.captionBlurRegionHeight(), 0.22);
    QCOMPARE(loaded.captionBlurStrength(), 46);
    QCOMPARE(loaded.captionBlurPadding(), 18);
    QCOMPARE(loaded.colorSettings().value("workingColorSpace").toString(),
             QString("Rec. 2100 HLG"));
    QVERIFY(!loaded.colorSettings().value("autoToneMapMedia").toBool());
    QCOMPARE(loaded.colorSettings().value("transmitPort").toInt(), 5000);
    QCOMPARE(loaded.activeColorClip()
                 .value("lumetri")
                 .toMap()
                 .value("exposure")
                 .toDouble(),
             1.2);
    QCOMPARE(loaded.activeColorClip()
                 .value("lumetri")
                 .toMap()
                 .value("redCurveHighlights")
                 .toDouble(),
             14.0);
    QCOMPARE(loaded.activeColorClip()
                 .value("lumetri")
                 .toMap()
                 .value("masterCurvePoints")
                 .toList()
                 .size(),
             3);
    QCOMPARE(loaded.activeColorClip()
                 .value("effects")
                 .toMap()
                 .value("scale")
                 .toDouble(),
             115.0);
    QCOMPARE(loaded.activeColorMedia()
                 .value("color")
                 .toMap()
                 .value("overrideMediaColorSpace")
                 .toString(),
             QString("Rec. 709"));
  }

  void ffmpegImportAndExport() {
    const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty())
      QSKIP("FFmpeg is not available on PATH");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sourcePath = dir.filePath("source.mp4");
    const QString outputPath = dir.filePath("output.mp4");

    QProcess generator;
    generator.start(ffmpeg, {"-y", "-f", "lavfi", "-i",
                             "testsrc=size=320x180:rate=25:duration=1", "-f",
                             "lavfi", "-i", "sine=frequency=440:duration=1",
                             "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a",
                             "aac", "-ac", "2", "-shortest", sourcePath});
    QVERIFY(generator.waitForFinished(15000));
    QCOMPARE(generator.exitCode(), 0);
    QVERIFY(QFileInfo(sourcePath).size() > 0);

    Backend backend;
    QCOMPARE(backend.importMedia({sourcePath}), 1);
    const QVariantMap item = backend.media().first().toMap();
    QCOMPARE(item.value("kind").toString(), QString("video"));
    QVERIFY(item.value("durationMs").toLongLong() >= 900);
    QVERIFY(item.value("width").toInt() == 320);
    QVERIFY(item.value("height").toInt() == 180);
    const QString thumbnailPath =
        QUrl(item.value("thumbnailUrl").toString()).toLocalFile();
    QVERIFY(!thumbnailPath.isEmpty());
    QVERIFY(QFileInfo(thumbnailPath).size() > 0);
    const QString filmstripPath =
        QUrl(item.value("timelineThumbnailUrl").toString()).toLocalFile();
    const QString waveformPath =
        QUrl(item.value("waveformUrl").toString()).toLocalFile();
    QVERIFY(!filmstripPath.isEmpty());
    QVERIFY(QFileInfo(filmstripPath).size() > 0);
    QVERIFY(!waveformPath.isEmpty());
    QVERIFY(QFileInfo(waveformPath).size() > 0);

    // The timeline draws thumbnails one filmstrip cell at a time, so the cell
    // layout has to travel with the media item. Without it a clip can only
    // stretch the whole sheet across its width.
    QCOMPARE(item.value("filmstripFrames").toInt(),
             MediaPreviewGenerator::kFilmstripFrames);
    QCOMPARE(item.value("filmstripFrameWidth").toInt(),
             MediaPreviewGenerator::kFilmstripFrameWidth);
    QCOMPARE(item.value("filmstripFrameHeight").toInt(),
             MediaPreviewGenerator::kFilmstripFrameHeight);
    const QImage filmstripImage(filmstripPath);
    QVERIFY(!filmstripImage.isNull());
    QCOMPARE(filmstripImage.width(),
             MediaPreviewGenerator::kFilmstripFrames *
                 MediaPreviewGenerator::kFilmstripFrameWidth);
    QCOMPARE(filmstripImage.height(),
             MediaPreviewGenerator::kFilmstripFrameHeight);

    QVERIFY(backend.requestPreviewFrame(sourcePath, 250, 320, 180));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.previewFrameImage().isNull(), 5000);
    QCOMPARE(backend.previewFrameImage().size(), QSize(320, 180));
    QVERIFY(backend.previewFrameUrl().startsWith("image://ffmpeg-preview/"));
    const QString pausedFrameUrl = backend.previewFrameUrl();
    QVERIFY(backend.startPreviewDecode(sourcePath, "video", 0, 700, 320, 180,
                                       25.0, false, 0.0));
    QTRY_VERIFY_WITH_TIMEOUT(backend.previewFrameUrl() != pausedFrameUrl, 5000);
    backend.stopPreviewDecode();

    const QString lutPath = QFINDTESTDATA("data/identity.cube");
    QVERIFY(!lutPath.isEmpty());
    QVERIFY(backend.setMediaColorSetting(item.value("id").toString(),
                                         "inputLut", lutPath));
    QVERIFY(backend.setMediaColorSetting(item.value("id").toString(),
                                         "overrideMediaColorSpace",
                                         "Rec. 2100 PQ"));

    const QStringList timelineClipIds =
        backend.addMediaToTimeline(item.value("id").toString(), 0, "V1");
    QCOMPARE(timelineClipIds.size(), 1);
    const QString colorClipId = timelineClipIds.first();
    const QVariantMap videoClip = backend.clips().first().toMap();
    QCOMPARE(videoClip.value("track").toString(), QString("V1"));
    QVERIFY(videoClip.value("embeddedAudio").toBool());
    QCOMPARE(backend.audioTrackCount(), 1);
    QVERIFY(backend.removeClip(colorClipId));
    QCOMPARE(backend.clips().size(), 0);
    backend.undo();
    QCOMPARE(backend.clips().size(), 1);

    QSignalSpy previewSpy(
        &backend,
        SIGNAL(effectPreviewReady(QString,QString,bool,QString)));
    const QString immediatePreview = backend.requestEffectPreview(
        colorClipId, QStringLiteral("gaussian_blur"), false);
    if (immediatePreview.isEmpty()) {
      QTRY_VERIFY_WITH_TIMEOUT(previewSpy.count() > 0, 15000);
      const QList<QVariant> arguments = previewSpy.takeLast();
      QVERIFY(!arguments.at(3).toString().isEmpty());
      QVERIFY(QFileInfo(QUrl(arguments.at(3).toString()).toLocalFile()).size() >
              0);
    } else {
      QVERIFY(QFileInfo(QUrl(immediatePreview).toLocalFile()).size() > 0);
    }
    previewSpy.clear();
    const QString immediateMotion = backend.requestEffectPreview(
        colorClipId, QStringLiteral("gaussian_blur"), true);
    if (immediateMotion.isEmpty()) {
      QTRY_VERIFY_WITH_TIMEOUT(previewSpy.count() > 0, 15000);
      const QList<QVariant> arguments = previewSpy.takeLast();
      QVERIFY(arguments.at(2).toBool());
      QVERIFY(!arguments.at(3).toString().isEmpty());
      QVERIFY(QFileInfo(QUrl(arguments.at(3).toString()).toLocalFile()).size() >
              0);
    } else {
      QVERIFY(QFileInfo(QUrl(immediateMotion).toLocalFile()).size() > 0);
    }
    QVERIFY(backend.setClipColorSetting(colorClipId, "exposure", 0.7));
    QVERIFY(backend.setClipColorSetting(colorClipId, "temperature", 12));
    QVERIFY(backend.setClipColorSetting(colorClipId, "sharpen", 15));
    QVERIFY(backend.setClipColorSetting(colorClipId, "curvePreset",
                                        "Increase Contrast"));
    QVERIFY(backend.setClipColorSetting(colorClipId, "curveMidtones", 12));
    QVERIFY(backend.setClipColorSetting(colorClipId, "redCurveHighlights", 8));
    QVERIFY(backend.setClipColorSetting(colorClipId, "shadowWheelX", -10));
    QVERIFY(backend.setClipColorSetting(colorClipId, "midtoneWheelY", 7));
    QVERIFY(backend.setClipColorSetting(colorClipId, "hslHueCenter", 28));
    QVERIFY(backend.setClipColorSetting(colorClipId, "hslHueWidth", 55));
    QVERIFY(backend.setClipColorSetting(colorClipId, "hslCorrectionHue", 6));
    QVERIFY(
        backend.setClipColorSetting(colorClipId, "hslCorrectionSaturation", 9));
    QVERIFY(backend.setClipColorSetting(colorClipId, "hslDenoise", 5));
    QVERIFY(backend.setClipColorSetting(colorClipId, "hslBlur", 3));
    QVERIFY(backend.setClipColorSetting(colorClipId, "vignette", -18));
    QVERIFY(backend.setClipColorSetting(colorClipId, "vignetteFeather", 65));
    QVERIFY(backend.setClipColorSetting(
        colorClipId, "masterCurvePoints",
        QVariantList{QVariantMap{{"x", 0.0}, {"y", 0.0}},
                     QVariantMap{{"x", 0.5}, {"y", 0.58}},
                     QVariantMap{{"x", 1.0}, {"y", 1.0}}}));
    QVERIFY(backend.setClipEffectSetting(colorClipId, "scale", 94));
    QVERIFY(backend.setClipEffectSetting(colorClipId, "positionX", 3));
    QVERIFY(backend.setClipEffectSetting(colorClipId, "rotation", 2));
    QVERIFY(backend.setClipEffectSetting(colorClipId, "opacity", 92));
    QVERIFY(backend.setClipEffectSetting(colorClipId, "volumeDb", -2));
    QVERIFY(backend.setClipEffectSetting(colorClipId, "vocalRemoval", true));
    QVERIFY(backend.setClipEffectSetting(colorClipId, "noiseReduction", 4));
    const QString brightnessEffect =
        backend.addClipEffect(colorClipId, "brightness_contrast");
    QVERIFY(!brightnessEffect.isEmpty());
    QVERIFY(backend.setClipEffectParameter(colorClipId, brightnessEffect,
                                           "contrast", 12));
    const QString highPassEffect =
        backend.addClipEffect(colorClipId, "high_pass");
    QVERIFY(!highPassEffect.isEmpty());
    QVERIFY(backend.setClipEffectParameter(colorClipId, highPassEffect,
                                           "frequency", 100));
    const QString mosaicEffect = backend.addClipEffect(colorClipId, "pixelate");
    QVERIFY(!mosaicEffect.isEmpty());
    QVERIFY(backend.setClipEffectParameter(colorClipId, mosaicEffect,
                                           "blockSize", 8));
    const QString customBlurEffect =
        backend.addClipEffect(colorClipId, "custom_blur");
    QVERIFY(!customBlurEffect.isEmpty());
    QVERIFY(backend.setClipEffectParameter(colorClipId, customBlurEffect,
                                           "amount", 10));
    QVERIFY(backend.setCustomBlurMask(colorClipId, customBlurEffect, 0.18,
                                      0.20, 0.36, 0.42));
    // Custom Blur must export from a normal video timeline without requiring
    // subtitle clips to be present.
    const QString noSubtitleOutput = dir.filePath("custom-blur-no-srt.mp4");
    QSignalSpy noSubtitleFinished(&backend,
                                  SIGNAL(exportFinished(bool, QString)));
    QVERIFY(noSubtitleFinished.isValid());
    QVERIFY(backend.startExportWithSettings(
        noSubtitleOutput,
        QVariantMap{{"quality", "low"},
                     {"width", 320},
                     {"height", 180},
                     {"frameRate", 25.0},
                     {"videoBitrateMbps", 2.0},
                     {"audioEnabled", true}}));
    QVERIFY(noSubtitleFinished.wait(20000));
    QVERIFY(noSubtitleFinished.first().at(0).toBool());
    QVERIFY(QFileInfo(noSubtitleOutput).size() > 0);

    const QString selectedFolder = dir.filePath("chosen export folder");
    QVERIFY(QDir().mkpath(selectedFolder));
    QSignalSpy folderFinished(&backend,
                              SIGNAL(exportFinished(bool, QString)));
    QVERIFY(backend.startExportWithSettings(
        QUrl::fromLocalFile(selectedFolder).toString(),
        QVariantMap{{"quality", "low"},
                    {"width", 320},
                    {"height", 180},
                    {"frameRate", 25.0},
                    {"audioEnabled", true}}));
    QVERIFY(folderFinished.wait(20000));
    QVERIFY(folderFinished.first().at(0).toBool());
    const QString folderOutput = folderFinished.first().at(1).toString();
    QCOMPARE(QFileInfo(folderOutput).absolutePath(),
             QDir(selectedFolder).absolutePath());
    QVERIFY(QFileInfo(folderOutput).size() > 0);

    const QString suggested = backend.suggestedExportPath();
    QVERIFY(QFileInfo(suggested).absolutePath().contains("Exports"));

    const QString invalidOutput = dir.filePath("export.unsupported-format");
    QSignalSpy failedExport(&backend,
                            SIGNAL(exportFinished(bool, QString)));
    QVERIFY(backend.startExportWithSettings(
        invalidOutput,
        QVariantMap{{"quality", "low"},
                    {"width", 320},
                    {"height", 180},
                    {"frameRate", 25.0},
                    {"audioEnabled", true}}));
    QVERIFY(failedExport.wait(20000));
    QVERIFY(!failedExport.first().at(0).toBool());
    QVERIFY2(!backend.lastError().isEmpty(),
             "FFmpeg failures must remain visible after progress parsing");
    QCOMPARE(backend.exportStatus(), QString("Export failed"));
    const QString bassEffect = backend.addClipEffect(colorClipId, "bass");
    QVERIFY(!bassEffect.isEmpty());
    QVERIFY(backend.setClipEffectParameter(colorClipId, bassEffect, "gain", 4));
    const qint64 mediaDuration = backend.durationMs();
    const QString subtitlePath = dir.filePath("captions.srt");
    QFile subtitles(subtitlePath);
    QVERIFY(subtitles.open(QIODevice::WriteOnly | QIODevice::Text));
    subtitles.write("1\n00:00:00,100 --> 00:00:02,000\nTest caption\n");
    subtitles.close();
    QVERIFY(backend.importSubtitles(subtitlePath));
    QVERIFY(backend.addTranscriptToTimeline());
    QVERIFY(backend.hasSubtitleClips());
    QCOMPARE(backend.durationMs(), mediaDuration);
    QSignalSpy finished(&backend, SIGNAL(exportFinished(bool, QString)));
    QVERIFY(finished.isValid());
    const QVariantMap exportSettings{
        {"quality", "medium"},     {"width", 640},
        {"height", 360},           {"frameRate", 24.0},
        {"videoBitrateMbps", 3.0}, {"profile", "high"},
        {"encodingSpeed", "fast"}, {"maximumRenderQuality", true},
        {"audioEnabled", true}};
    QVERIFY(backend.startExportWithSettings(outputPath, exportSettings));
    QVERIFY(finished.wait(20000));
    QCOMPARE(finished.first().at(0).toBool(), true);
    QVERIFY(QFileInfo(outputPath).size() > 0);
    const QVariantMap exported = backend.probeMedia(outputPath);
    QCOMPARE(exported.value("width").toInt(), 640);
    QCOMPARE(exported.value("height").toInt(), 360);
    QVERIFY(qAbs(exported.value("frameRate").toDouble() - 24.0) < 0.1);
    QCOMPARE(exported.value("channels").toInt(), 2);
  }

  void lumetriPipelineBuildsNativeFfmpegChain() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString lutPath = dir.filePath("look.cube");
    QFile lut(lutPath);
    QVERIFY(lut.open(QIODevice::WriteOnly | QIODevice::Text));
    lut.write("TITLE \"test\"\nLUT_3D_SIZE 2\n");
    lut.close();

    const QVariantMap clip{
        {"lumetri",
         QVariantMap{{"exposure", 1.0},
                     {"temperature", 20.0},
                     {"curvePreset", "Increase Contrast"},
                     {"curveMidtones", 20.0},
                     {"masterCurvePoints",
                      QVariantList{QVariantMap{{"x", 0.0}, {"y", 0.0}},
                                   QVariantMap{{"x", 0.5}, {"y", 0.62}},
                                   QVariantMap{{"x", 1.0}, {"y", 1.0}}}},
                     {"blueCurveShadows", -12.0},
                     {"shadowWheelX", 10.0},
                     {"highlightWheelY", -8.0},
                     {"hslHueCenter", 30.0},
                     {"hslHueWidth", 50.0},
                     {"hslCorrectionHue", 8.0},
                     {"hslCorrectionSaturation", 12.0},
                     {"hslDenoise", 10.0},
                     {"hslBlur", 5.0},
                     {"vignette", -25.0}}}};
    const QVariantMap media{
        {"color", QVariantMap{{"inputLut", lutPath},
                              {"overrideMediaColorSpace", "Rec. 2100 PQ"}}}};
    const QVariantMap project{{"autoToneMapMedia", true},
                              {"workingColorSpace", "Rec. 709"},
                              {"lutInterpolation", "Trilinear"}};
    const QString chain = LumetriPipeline::filterForClip(clip, media, project);
    QVERIFY(chain.contains("zscale="));
    QVERIFY(chain.contains("tonemap="));
    QVERIFY(chain.contains("lut3d=file="));
    QVERIFY(chain.contains("interp=trilinear"));
    QVERIFY(chain.contains("eq=brightness="));
    QVERIFY(chain.contains("curves=preset=increase_contrast"));
    QVERIFY(chain.contains("curves=master="));
    QVERIFY(chain.contains("curves=b="));
    QVERIFY(chain.contains("colorbalance="));
    QVERIFY(chain.contains("huesaturation="));
    QVERIFY(chain.contains("hqdn3d="));
    QVERIFY(chain.contains("gblur="));
    QVERIFY(chain.contains("vignette="));
  }

  void clipEffectsPipelineBuildsNativeFfmpegChain() {
    const QVariantMap effects{{"scale", 125.0},
                              {"positionX", 12.0},
                              {"rotation", -4.0},
                              {"opacity", 80.0},
                              {"cropLeft", 5.0},
                              {"horizontalFlip", true},
                              {"blur", 15.0},
                              {"volumeDb", -3.0},
                              {"pan", 0.25},
                              {"vocalRemoval", true},
                              {"noiseReduction", 20.0},
                              {"highPassHz", 90.0},
                              {"lowPassHz", 14000.0},
                              {"compressor", true}};
    const QString video = ClipEffectsPipeline::videoFilters(effects);
    QVERIFY(video.contains("crop="));
    QVERIFY(video.contains("scale=iw*"));
    QVERIFY(video.contains("hflip"));
    QVERIFY(video.contains("rotate="));
    QVERIFY(video.contains("colorchannelmixer=aa="));
    QVERIFY(video.contains("gblur="));
    QVERIFY(ClipEffectsPipeline::overlayX(effects).contains("main_w*0.12000"));

    const QString audio = ClipEffectsPipeline::audioFilters(effects, 2);
    QVERIFY(!audio.contains("c0=c0-c1"));
    QVERIFY(audio.contains("stereotools=balance_out="));
    QVERIFY(audio.contains("afftdn="));
    QVERIFY(audio.contains("highpass="));
    QVERIFY(audio.contains("lowpass="));
    QVERIFY(audio.contains("acompressor="));
    QVERIFY(!ClipEffectsPipeline::audioFilters(effects, 1).contains("c0=c0-c1"));
  }

  void timelineEditingFeatures() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mediaPath = dir.filePath("still.png");
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    QVERIFY(media.write("timeline-test") > 0);
    media.close();

    Backend backend;
    QVERIFY(backend.newProject("Timeline", dir.path()));
    QCOMPARE(backend.importMedia({mediaPath}), 1);
    const QString mediaId =
        backend.media().first().toMap().value("id").toString();
    const QString firstId = backend.addClip(mediaId, 0, "V1");
    const QString secondId = backend.addClip(mediaId, 7000, "V1");
    QCOMPARE(backend.addTrack("video"), QString("V2"));
    const QString thirdId = backend.addClip(mediaId, 7000, "V2");
    QVERIFY(!firstId.isEmpty());
    QVERIFY(!secondId.isEmpty());
    QVERIFY(!thirdId.isEmpty());

    const QString markerId = backend.addMarker(6000, "Beat", "#65d46e");
    QVERIFY(!markerId.isEmpty());
    QCOMPARE(backend.snapTime(5920, {}, 120), qint64(6000));
    QVERIFY(backend.updateMarker(markerId, 6100, "Scene", "#f4cf58"));

    QVERIFY(backend.setTrackState("V1", "locked", true));
    QVERIFY(backend.trackLocked("V1"));
    QVERIFY(!backend.moveClip(firstId, 1000, "V1"));
    QVERIFY(backend.setTrackState("V1", "locked", false));
    QVERIFY(backend.rippleTrimClipEnd(firstId, 4000));
    QCOMPARE(backend.clips().at(1).toMap().value("startMs").toLongLong(),
             qint64(6000));
    QCOMPARE(backend.clips().at(2).toMap().value("startMs").toLongLong(),
             qint64(6000));
    QVERIFY(backend.setTrackState("V2", "syncLocked", false));
    QVERIFY(backend.rippleDeleteClips({firstId}));
    QCOMPARE(backend.clips().size(), 2);
    QCOMPARE(backend.clips().first().toMap().value("id").toString(), secondId);
    QCOMPARE(backend.clips().first().toMap().value("startMs").toLongLong(),
             qint64(2000));
    QCOMPARE(backend.clips().at(1).toMap().value("id").toString(), thirdId);
    QCOMPARE(backend.clips().at(1).toMap().value("startMs").toLongLong(),
             qint64(6000));

    QVERIFY(backend.setTrackState("V2", "visible", false));
    QVERIFY(backend.setTrackState("V2", "solo", true));
    QVERIFY(backend.setTrackState("V2", "targeted", false));
    QCOMPARE(backend.videoTrackCount(), 2);

    const QString projectPath = dir.filePath("timeline.cutpro.json");
    QVERIFY(backend.saveProject(projectPath));
    Backend loaded;
    QVERIFY(loaded.loadProject(projectPath));
    QCOMPARE(loaded.videoTrackCount(), 2);
    QCOMPARE(loaded.markers().size(), 1);
    QCOMPARE(loaded.markers().first().toMap().value("name").toString(),
             QString("Scene"));
    QVERIFY(!loaded.trackVisible("V2"));
    QVERIFY(loaded.trackSolo("V2"));
    QVERIFY(!loaded.trackSyncLocked("V2"));
    QVERIFY(!loaded.trackTargeted("V2"));
  }

  void suppliedMediaShortExport() {
    const QString sourcePath = qEnvironmentVariable("CUTPRO_TEST_MEDIA");
    if (sourcePath.isEmpty())
      QSKIP("CUTPRO_TEST_MEDIA is not set");
    QVERIFY(QFileInfo::exists(sourcePath));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Backend backend;
    QCOMPARE(backend.importMedia({sourcePath}), 1);
    const QString mediaId =
        backend.media().first().toMap().value("id").toString();
    const QString clipId = backend.addClip(mediaId);
    QVERIFY(!clipId.isEmpty());
    QVERIFY(backend.splitClip(clipId, 2000));
    const QString tailId = backend.clips().at(1).toMap().value("id").toString();
    QVERIFY(backend.removeClip(tailId));

    const QString outputPath = dir.filePath("supplied-media-export.mp4");
    QSignalSpy finished(&backend, SIGNAL(exportFinished(bool, QString)));
    QVERIFY(backend.startExport(outputPath, "low"));
    QVERIFY(finished.wait(60000));
    QCOMPARE(finished.first().at(0).toBool(), true);
    QVERIFY(QFileInfo(outputPath).size() > 0);
  }

  void pythonWhisperTranscription() {
    const QString sourcePath =
        qEnvironmentVariable("CUTPRO_WHISPER_TEST_MEDIA");
    if (sourcePath.isEmpty())
      QSKIP("CUTPRO_WHISPER_TEST_MEDIA is not set");
    QVERIFY(QFileInfo::exists(sourcePath));

    Backend backend;
    QCOMPARE(backend.importMedia({sourcePath}), 1);
    const QString mediaId =
        backend.media().first().toMap().value("id").toString();
    QVERIFY(backend.transcribeMedia(mediaId));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.transcriptionInProgress(), 60000);
    QVERIFY2(!backend.transcript().isEmpty(),
             qPrintable(backend.transcriptionStatus()));
  }

  void subtitleImportExport() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString inputPath = dir.filePath("input.srt");
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly | QIODevice::Text));
    input.write("1\n00:00:01,250 --> 00:00:03,500\nFirst line\n\n"
                "2\n00:01:04,000 --> 00:01:06,125\nSecond line\n");
    input.close();

    Backend backend;
    QVERIFY(backend.importSubtitles(inputPath));
    QCOMPARE(backend.transcript().size(), 2);
    QCOMPARE(backend.transcript().first().toMap().value("start").toDouble(),
             1.25);
    QVERIFY(backend.updateTranscriptSegment(0, "Edited first line"));
    QCOMPARE(backend.transcript().first().toMap().value("text").toString(),
             QString("Edited first line"));

    const QString outputPath = dir.filePath("output.srt");
    QVERIFY(backend.exportTranscriptSrt(outputPath));
    QFile output(outputPath);
    QVERIFY(output.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString contents = QString::fromUtf8(output.readAll());
    QVERIFY(contents.contains("00:00:01,250 --> 00:00:03,500"));
    QVERIFY(contents.contains("00:01:04,000 --> 00:01:06,125"));
    QVERIFY(contents.contains("Edited first line"));
    QVERIFY(!contents.contains("\nFirst line\n"));

    QVERIFY(backend.addTranscriptToTimeline());
    QVERIFY(backend.hasSubtitleClips());
    QCOMPARE(backend.clips().size(), 2);
    const QVariantMap firstSubtitle = backend.clips().first().toMap();
    QCOMPARE(firstSubtitle.value("kind").toString(), QString("subtitle"));
    QCOMPARE(firstSubtitle.value("track").toString(), QString("S1"));
    QCOMPARE(firstSubtitle.value("startMs").toLongLong(), qint64(1250));
    QCOMPARE(firstSubtitle.value("durationMs").toLongLong(), qint64(2250));
    QCOMPARE(firstSubtitle.value("text").toString(),
             QString("Edited first line"));
    const QString subtitleId = firstSubtitle.value("id").toString();
    QVERIFY(!backend.moveClip(subtitleId, 0, "V1"));

    QVERIFY(backend.deleteClipLeft(subtitleId, 2000));
    QVariantMap editedSubtitle = backend.clips().first().toMap();
    QCOMPARE(editedSubtitle.value("startMs").toLongLong(), qint64(2000));
    QCOMPARE(editedSubtitle.value("sourceInMs").toLongLong(), qint64(750));
    QCOMPARE(editedSubtitle.value("durationMs").toLongLong(), qint64(1500));
    backend.undo();
    editedSubtitle = backend.clips().first().toMap();
    QCOMPARE(editedSubtitle.value("startMs").toLongLong(), qint64(1250));
    QCOMPARE(editedSubtitle.value("durationMs").toLongLong(), qint64(2250));
    backend.redo();
    QCOMPARE(backend.clips().first().toMap().value("startMs").toLongLong(),
             qint64(2000));
    backend.undo();

    QVERIFY(backend.deleteClipRight(subtitleId, 2000));
    editedSubtitle = backend.clips().first().toMap();
    QCOMPARE(editedSubtitle.value("startMs").toLongLong(), qint64(1250));
    QCOMPARE(editedSubtitle.value("durationMs").toLongLong(), qint64(750));
    backend.undo();

    QVERIFY(backend.addTranscriptToTimeline());
    QCOMPARE(backend.clips().size(), 2);
    const QString projectPath = dir.filePath("subtitle-project.cutpro.json");
    QVERIFY(backend.saveProject(projectPath));
    Backend loaded;
    QVERIFY(loaded.loadProject(projectPath));
    QVERIFY(loaded.hasSubtitleClips());
    QCOMPARE(loaded.clips().size(), 2);
    QCOMPARE(loaded.clips().first().toMap().value("track").toString(),
             QString("S1"));
    QVERIFY(!loaded.canExport());

    QVERIFY(backend.removeTranscriptFromTimeline());
    QVERIFY(!backend.hasSubtitleClips());
    QCOMPARE(backend.clips().size(), 0);
    backend.undo();
    QVERIFY(backend.hasSubtitleClips());
    QCOMPARE(backend.clips().size(), 2);
    backend.redo();
    QVERIFY(!backend.hasSubtitleClips());

    QVERIFY(!backend.translateTranscript("th"));
    QVERIFY(backend.lastError().contains("not supported"));
  }

  void mediaScanClassifiesSuffixesForBothScanAndProbe() {
    QCOMPARE(MediaScan::kindForSuffix("MP4"), QString("video"));
    QCOMPARE(MediaScan::kindForSuffix("m2ts"), QString("video"));
    QCOMPARE(MediaScan::kindForSuffix("Flac"), QString("audio"));
    QCOMPARE(MediaScan::kindForSuffix("tiff"), QString("image"));
    QCOMPARE(MediaScan::kindForSuffix("txt"), QString("unknown"));
    QCOMPARE(MediaScan::kindForSuffix(QString()), QString("unknown"));
    QVERIFY(!MediaScan::isSupported(QFileInfo("notes.pdf")));
  }

  void mediaScanNormalizesUrlsAndPlainPaths() {
    const QString path = QStringLiteral("C:/media/a b/clip.mp4");
    QCOMPARE(MediaScan::normalizeInput(QUrl::fromLocalFile(path).toString()),
             path);
    QCOMPARE(MediaScan::normalizeInput(QStringLiteral("  ") + path +
                                       QStringLiteral("  ")),
             path);
    QCOMPARE(MediaScan::normalizeInput(QStringLiteral("C:/media/./a b/../clip.mp4")),
             QStringLiteral("C:/media/clip.mp4"));
    QVERIFY(MediaScan::normalizeInput(QStringLiteral("   ")).isEmpty());
  }

  void mediaScanWalksBreadthFirstAndSkipsUnusableFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir root(dir.path());
    QVERIFY(writeSampleFile(root.filePath("EP10.mp4")));
    QVERIFY(writeSampleFile(root.filePath("EP02.mp4")));
    QVERIFY(writeSampleFile(root.filePath("EP01.mp4")));
    QVERIFY(writeSampleFile(root.filePath("notes.txt")));
    QVERIFY(writeSampleFile(root.filePath("truncated.mov"), 0));
    QVERIFY(writeSampleFile(root.filePath("season 2/EP01.mkv")));
    QVERIFY(writeSampleFile(root.filePath("season 2/audio/theme.flac")));

    const MediaScan::Result scan = MediaScan::expand({dir.path()});
    QVERIFY(!scan.truncated());
    QVERIFY(!scan.cancelled);
    QCOMPARE(scan.files.size(), 5);
    // Breadth-first, and locale-aware inside each directory: the natural
    // episode order survives, and the top level is returned before the
    // subfolders the user did not pick directly.
    QCOMPARE(QFileInfo(scan.files.at(0)).fileName(), QString("EP01.mp4"));
    QCOMPARE(QFileInfo(scan.files.at(1)).fileName(), QString("EP02.mp4"));
    QCOMPARE(QFileInfo(scan.files.at(2)).fileName(), QString("EP10.mp4"));
    QCOMPARE(QFileInfo(scan.files.at(3)).fileName(), QString("EP01.mkv"));
    QCOMPARE(QFileInfo(scan.files.at(4)).fileName(), QString("theme.flac"));
    QCOMPARE(scan.skippedUnsupported, 1);
    QCOMPARE(scan.skippedUnreadable, 1);
    QCOMPARE(scan.directoriesVisited, 3);
    QVERIFY(scan.truncationMessage().isEmpty());
  }

  void mediaScanCollapsesDuplicateSelections() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString clip = QDir(dir.path()).filePath("clip.mp4");
    QVERIFY(writeSampleFile(clip));

    // The same file three ways: the folder, the plain path, and a file:// URL
    // with a redundant "." segment. The old code called removeDuplicates() on
    // the finished list, which collapsed none of these.
    const MediaScan::Result scan = MediaScan::expand(
        {dir.path(), clip,
         QUrl::fromLocalFile(QDir(dir.path()).filePath("./clip.mp4")).toString(),
         QString()});
    QCOMPARE(scan.files.size(), 1);
    QVERIFY(scan.skippedDuplicates >= 1);
  }

  void mediaScanStopsAtItsLimitsAndExplainsWhy() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir root(dir.path());
    for (int index = 0; index < 6; ++index)
      QVERIFY(writeSampleFile(
          root.filePath(QStringLiteral("clip%1.mp4").arg(index, 2, 10, QChar('0')))));

    MediaScan::Limits limits;
    limits.maximumFiles = 3;
    const MediaScan::Result capped = MediaScan::expand({dir.path()}, limits);
    QCOMPARE(capped.files.size(), 3);
    QVERIFY(capped.hitFileLimit);
    QVERIFY(capped.truncated());
    QVERIFY(capped.truncationMessage().contains("file limit"));
    QVERIFY(capped.truncationMessage().contains("first 3"));

    MediaScan::Limits entries;
    entries.maximumEntries = 2;
    const MediaScan::Result clipped = MediaScan::expand({dir.path()}, entries);
    QVERIFY(clipped.hitEntryLimit);
    QVERIFY(clipped.truncationMessage().contains("folder entries"));

    // A depth ceiling of zero means "the folders I picked, nothing below them".
    QVERIFY(writeSampleFile(root.filePath("nested/deep.mp4")));
    MediaScan::Limits shallow;
    shallow.maximumDepth = 0;
    const MediaScan::Result flat = MediaScan::expand({dir.path()}, shallow);
    QVERIFY(flat.hitDepthLimit);
    for (const QString &file : flat.files)
      QCOMPARE(QFileInfo(file).fileName() == QString("deep.mp4"), false);
  }

  void mediaScanStopsWhenCancelled() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeSampleFile(QDir(dir.path()).filePath("clip.mp4")));

    std::atomic_bool cancel{true};
    const MediaScan::Result scan =
        MediaScan::expand({dir.path()}, MediaScan::Limits{}, &cancel);
    QVERIFY(scan.cancelled);
    QVERIFY(scan.files.isEmpty());
  }

  void mediaScanSurvivesADirectoryLoop() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir root(dir.path());
    const QString branch = root.filePath("branch");
    QVERIFY(writeSampleFile(QDir(branch).filePath("clip.mp4")));

    // A junction (or symlink) pointing back at an ancestor used to make the
    // recursive walk run until the process was killed.
    const QString loop = QDir(branch).filePath("loop");
    const bool linked = QFile::link(dir.path(), loop);
    if (!linked)
      QSKIP("This environment does not allow creating directory links");

    MediaScan::Limits limits;
    limits.timeBudgetMs = 4000;
    QElapsedTimer timer;
    timer.start();
    const MediaScan::Result scan = MediaScan::expand({dir.path()}, limits);
    QVERIFY2(timer.elapsed() < 4000, "The scan did not terminate on its own");
    QVERIFY(!scan.hitTimeBudget);
    QCOMPARE(scan.files.size(), 1);
  }

  void mediaImportQueueProbesConcurrentlyAndEmitsInScanOrder() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir root(dir.path());
    QStringList expected;
    for (int index = 0; index < 12; ++index) {
      const QString name = QStringLiteral("clip%1.mp4").arg(index, 2, 10, QChar('0'));
      QVERIFY(writeSampleFile(root.filePath(name)));
      expected << name;
    }

    std::atomic_int active{0};
    std::atomic_int peak{0};
    std::atomic_int probes{0};
    MediaImportQueue queue;
    queue.setMaximumConcurrency(4);
    QCOMPARE(queue.maximumConcurrency(), 4);
    queue.setProber([&active, &peak, &probes](const QString &path) {
      const int running = ++active;
      // Records how many probes overlapped: the old pipeline could only ever
      // reach one.
      int observed = peak.load();
      while (running > observed && !peak.compare_exchange_weak(observed, running))
        ;
      ++probes;
      QThread::msleep(20);
      --active;
      return QVariantMap{{"path", path},
                         {"name", QFileInfo(path).fileName()},
                         {"kind", QStringLiteral("video")},
                         {"durationMs", 1000}};
    });

    QSignalSpy started(&queue, &MediaImportQueue::started);
    QSignalSpy batches(&queue, &MediaImportQueue::itemsReady);
    QSignalSpy done(&queue, &MediaImportQueue::finished);
    QVERIFY(queue.start({{dir.path()}, QString(), MediaScan::Limits{}}));
    QVERIFY(queue.active());
    // Rejected while a run is in flight rather than corrupting the current one.
    QVERIFY(!queue.start({{dir.path()}, QString(), MediaScan::Limits{}}));

    QTRY_VERIFY_WITH_TIMEOUT(done.count() == 1, 15000);
    QCOMPARE(started.count(), 1);
    QCOMPARE(started.constFirst().at(0).toInt(), expected.size());
    QCOMPARE(done.constFirst().at(0).toInt(), expected.size());
    QCOMPARE(done.constFirst().at(1).toInt(), 0);
    QCOMPARE(done.constFirst().at(2).toBool(), false);
    QVERIFY(!queue.active());
    QCOMPARE(queue.percent(), 100);
    QCOMPARE(probes.load(), expected.size());
    QVERIFY2(peak.load() > 1, "Probes never overlapped");
    QVERIFY2(peak.load() <= 4, "More probes ran than the configured concurrency");

    QStringList delivered;
    for (const QList<QVariant> &batch : batches) {
      for (const QVariant &entry : batch.at(0).toList())
        delivered << entry.toMap().value("name").toString();
    }
    // Probes finish out of order; the bin must still fill in scan order.
    QCOMPARE(delivered, expected);
    QVERIFY2(batches.count() < expected.size(),
             "Every file produced its own model reset");
  }

  void mediaImportQueueRejectsDuplicatesAndUnsupportedFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir root(dir.path());
    const QString existing = root.filePath("already-in-bin.mp4");
    const QString fresh = root.filePath("new.mp4");
    const QString bogus = root.filePath("broken.mp4");
    QVERIFY(writeSampleFile(existing));
    QVERIFY(writeSampleFile(fresh));
    QVERIFY(writeSampleFile(bogus));

    MediaImportQueue queue;
    queue.setMaximumConcurrency(2);
    queue.setExistingKeys({MediaPath::duplicateKey(existing)});
    queue.setProber([&bogus](const QString &path) -> QVariantMap {
      if (QFileInfo(path).fileName() == QFileInfo(bogus).fileName())
        return {{"kind", QStringLiteral("unknown")}};
      return {{"path", path},
              {"name", QFileInfo(path).fileName()},
              {"kind", QStringLiteral("video")}};
    });

    QSignalSpy batches(&queue, &MediaImportQueue::itemsReady);
    QSignalSpy warnings(&queue, &MediaImportQueue::warning);
    QSignalSpy done(&queue, &MediaImportQueue::finished);
    // The same file listed twice plus a missing one: neither may reach the
    // prober, and neither may end the run early.
    QVERIFY(queue.start({{fresh, fresh, existing, bogus,
                          root.filePath("gone.mp4")},
                         QString(),
                         MediaScan::Limits{}}));
    QTRY_VERIFY_WITH_TIMEOUT(done.count() == 1, 15000);

    QCOMPARE(done.constFirst().at(0).toInt(), 1);
    QCOMPARE(queue.acceptedCount(), 1);
    QVERIFY(queue.skippedCount() >= 2);
    QStringList delivered;
    for (const QList<QVariant> &batch : batches) {
      for (const QVariant &entry : batch.at(0).toList())
        delivered << entry.toMap().value("name").toString();
    }
    QCOMPARE(delivered, QStringList{QStringLiteral("new.mp4")});
    QVERIFY(!warnings.isEmpty());
  }

  void mediaImportQueueCopiesIntoTheProjectAndCancelsCleanly() {
    QTemporaryDir source;
    QTemporaryDir project;
    QVERIFY(source.isValid());
    QVERIFY(project.isValid());
    const QDir sourceRoot(source.path());
    // Same file name in two folders: the copy must not overwrite one with the
    // other.
    QVERIFY(writeSampleFile(sourceRoot.filePath("a/clip.mp4"), 4096));
    QVERIFY(writeSampleFile(sourceRoot.filePath("b/clip.mp4"), 8192));
    const QString destination = QDir(project.path()).filePath("Media");

    MediaImportQueue queue;
    queue.setMaximumConcurrency(2);
    queue.setProber([](const QString &path) {
      return QVariantMap{{"path", path},
                         {"name", QFileInfo(path).fileName()},
                         {"kind", QStringLiteral("video")}};
    });
    QSignalSpy done(&queue, &MediaImportQueue::finished);
    QVERIFY(queue.start({{source.path()}, destination, MediaScan::Limits{}}));
    QTRY_VERIFY_WITH_TIMEOUT(done.count() == 1, 15000);
    QCOMPARE(done.constFirst().at(0).toInt(), 2);

    const QStringList copied =
        QDir(destination).entryList(QDir::Files, QDir::Name);
    QCOMPARE(copied.size(), 2);
    QVERIFY(copied.contains("clip.mp4"));
    QVERIFY(copied.contains("clip_1.mp4"));
    qint64 total = 0;
    for (const QString &name : copied)
      total += QFileInfo(QDir(destination).filePath(name)).size();
    // Both payloads arrived intact, so the chunked copy is byte-exact.
    QCOMPARE(total, qint64(4096 + 8192));

    QSignalSpy cancelled(&queue, &MediaImportQueue::finished);
    QVERIFY(queue.start({{source.path()}, destination, MediaScan::Limits{}}));
    queue.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(cancelled.count() == 1, 15000);
    QCOMPARE(cancelled.constFirst().at(2).toBool(), true);
    QVERIFY(!queue.active());
    // A cancelled copy removes its partial file instead of leaving a truncated
    // clip in the project media folder.
    QCOMPARE(QDir(destination).entryList(QDir::Files).size(), 2);
  }

  void mediaImportQueueShutdownStopsWorkersWhileTheOwnerIsWhole() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (int index = 0; index < 8; ++index)
      QVERIFY(writeSampleFile(
          QDir(dir.path()).filePath(QStringLiteral("clip%1.mp4").arg(index))));

    std::atomic_int probes{0};
    {
      MediaImportQueue queue;
      queue.setMaximumConcurrency(2);
      queue.setProber([&probes](const QString &path) {
        ++probes;
        QThread::msleep(30);
        return QVariantMap{{"path", path}, {"kind", QStringLiteral("video")}};
      });
      QVERIFY(queue.start({{dir.path()}, QString(), MediaScan::Limits{}}));
      QTest::qWait(60);
      // Mirrors Backend's destructor: the prober captures the owner, so every
      // worker must be stopped before the owner starts tearing itself down.
      queue.shutdown();
      QVERIFY(!queue.active());
    }
    const int settled = probes.load();
    QTest::qWait(150);
    QCOMPARE(probes.load(), settled);
  }
};

QTEST_GUILESS_MAIN(BackendTests)
#include "backend_tests.moc"
