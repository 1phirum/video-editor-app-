// Pixel source for the Lumetri Scopes panel.
//
// The scopes measure the *graded* picture the monitor shows, but the grade only
// exists as a GPU shader chain at runtime (see backend_settings.cpp), so there is
// no graded QImage to read on the GPU path. Rather than read the framebuffer back
// - which the native path never does - this grades a small copy of the raw frame
// on the CPU using the same cached correction the software mirror uses. It is a
// documented approximation of the shader chain (lumetri_preview.h), faithful in
// signal shape, which is what a scope reads.

#include "app/core_app/backend.h"

#include "app/lumetri/lumetri_preview.h"

#include <QImage>
#include <QMutexLocker>
#include <QtGlobal>

QImage Backend::scopeSourceImage(int maxEdge) const {
  QImage source = previewFrameImage();
  if (source.isNull())
    return {};

  // A scope reads a sample, not the whole frame: capping the long edge keeps the
  // per-frame grade off the critical path on a 4K monitor. Smooth, because a
  // nearest-neighbour reduction would invent luma/chroma values the source never
  // had and skew the trace.
  if (maxEdge > 0 &&
      (source.width() > maxEdge || source.height() > maxEdge))
    source = source.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio,
                           Qt::SmoothTransformation);

  // A predictable 24-bit layout for the renderer's scanline walk, and a deep copy
  // so grading in place cannot touch the decoder's still-owned frame buffer.
  QImage graded = source.convertToFormat(QImage::Format_RGB888);

  LumetriPreview::Correction correction;
  {
    QMutexLocker locker(&m_previewGradeMutex);
    correction = m_previewGrade;
  }
  if (!correction.identity)
    LumetriPreview::apply(&graded, correction);
  return graded;
}
