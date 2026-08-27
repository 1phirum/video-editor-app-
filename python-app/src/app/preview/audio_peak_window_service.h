#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <atomic>

// Waveform detail for one visible window of a source, decoded on demand.
//
// The cached waveform sheet (AudioPeakBuilder) is 1600 columns over the *whole*
// file. That is the right artefact for a clip drawn a few hundred pixels wide,
// and it falls apart as soon as the timeline is zoomed in: on an eight hour
// source one sheet column covers 18 seconds, so a screen showing 20 seconds of
// audio has a single column stretched across it - a flat block, not a waveform.
// This is what CapCut's timeline does differently, and the fix is the same one
// the thumbnails use: ask for the window that is actually on screen.
//
// Two decode strategies, chosen by how much audio the window covers:
//
//  * a short window (a few seconds) is decoded straight through, so every sample
//    in it is folded into a column and the envelope is exact;
//  * a long window is sampled - one seek and one brief decode per column - so the
//    cost is bounded by the number of columns rather than by the span. At one
//    column per second or more, a peak taken from a 60 ms probe and a peak taken
//    over the whole column are the same waveform to the eye.
//
// What is cached is the peak array, not the rendered image: 256 floats is a
// kilobyte, so tens of thousands of windows - every zoom level of a long source,
// several sources deep - stay in memory for the price of a couple of video
// frames. Rendering from peaks is a few hundred microseconds.
class AudioPeakWindowService final {
public:
  struct Window {
    QVector<float> peaks;
    qint64 startMs = 0;
    qint64 spanMs = 0;
    bool fromCache = false;
    bool sampled = false;
    bool cancelled = false;
    QString error;

    bool valid() const { return !peaks.isEmpty(); }
  };

  // Columns per window. 256 over a window drawn ~512 px wide is two pixels per
  // column, which is as fine as the eye reads a waveform bar.
  static constexpr int kDefaultColumns = 256;
  static constexpr int kRenderWidth = 512;
  static constexpr int kRenderHeight = 160;
  // Windows shorter than this are decoded in full instead of sampled.
  static constexpr qint64 kFullDecodeLimitMs = 6000;
  // Audio decoded per column in sampled mode.
  static constexpr int kProbeWindowMs = 60;
  // Ceiling on one window's decode, so a source with a broken index cannot hold
  // a provider thread while the user keeps scrolling.
  static constexpr int kTimeBudgetMs = 3000;
  // The colour AudioPeakBuilder renders the sheet with, so a window fading in
  // over the sheet is the same waveform in the same blue.
  static constexpr quint32 kWaveformRgb = 0x63a4ff;

  static AudioPeakWindowService &instance();
  // False in builds without direct FFmpeg linkage, where the timeline keeps
  // stretching the cached sheet.
  static bool available();

  AudioPeakWindowService();
  ~AudioPeakWindowService();

  // Shared with the thumbnail provider: one token per source, whichever provider
  // asks for it.
  QString tokenFor(const QString &path);
  QString pathForToken(const QString &token) const;

  // Cache-only, never opens a file: safe to call from the GUI thread.
  Window cachedWindow(const QString &path, qint64 startMs, qint64 spanMs,
                      int columns);
  Window window(const QString &path, qint64 startMs, qint64 spanMs, int columns,
                const std::atomic_bool *cancel);

  // Bottom-anchored bars on transparency, the shape the timeline lays out under
  // each clip.
  static QImage render(const QVector<float> &peaks, const QSize &size);

  void forget(const QString &path);
  void clearMemory();
  QVariantMap statistics() const;

private:
  struct Entry {
    QVector<float> peaks;
    quint64 tick = 0;
  };

  QString fingerprintFor(const QString &path);
  QString cacheKey(const QString &fingerprint, qint64 startMs, qint64 spanMs,
                   int columns) const;
  QVector<float> lookup(const QString &key);
  void store(const QString &key, const QVector<float> &peaks);
  bool knownSilent(const QString &path) const;
  void rememberSilent(const QString &path);

  // Around 12 MB of peaks at 256 columns: a whole editing session's worth of
  // windows over several long sources.
  static constexpr int kMaximumWindows = 12288;

  mutable QMutex m_mutex;
  QHash<QString, Entry> m_windows;
  QHash<QString, QString> m_fingerprintByPath;
  QSet<QString> m_silentSources;
  quint64 m_tick = 0;
  // Bumped by forget(), so a reader another thread is holding open on that file
  // is dropped the next time that thread is used instead of serving stale audio.
  std::atomic<quint64> m_generation{1};

  std::atomic<quint64> m_serves{0};
  std::atomic<quint64> m_decodes{0};
  std::atomic<quint64> m_hits{0};
  std::atomic<quint64> m_cancels{0};
  std::atomic<quint64> m_failures{0};
  std::atomic<quint64> m_opens{0};
  std::atomic<quint64> m_reuse{0};

  friend struct AudioWindowReaderAccess;
};
