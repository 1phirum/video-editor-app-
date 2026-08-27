#pragma once

#include <QString>

// Resolves the FFmpeg 9 shared build bundled with Cut Pro before consulting
// PATH. Keeping this in one small runtime helper prevents every backend module
// from carrying its own executable lookup logic.
class FfmpegRuntime final {
public:
  static QString root();
  static QString executable();
  static QString probeExecutable();
};
