#pragma once

#include <QVariantMap>

// Background project-bin and timeline preview generation for long/large sources.
//
// This is the deferred half of the import: probeMedia() returns immediately with
// no thumbnail for a large source so the bin appears at once, and this job then
// fills in the poster frame, the timeline filmstrip and the waveform from a
// worker thread. Everything it produces is seek-based and disk-cached, so a
// multi-hour clip gets the same thumbnails a short one does instead of being
// left as a flat rectangle.
//
// The job never touches QML or Backend state; it returns a small map of cached
// URLs which the GUI thread merges into the media entry.
class LargeMediaPreviewJob final {
public:
  static QVariantMap generate(const QVariantMap &media);
};
