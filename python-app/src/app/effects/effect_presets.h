#pragma once

#include <QVariantList>
#include <QVariantMap>

// The two preset shelves the effects browser offers next to the raw effects.
//
// Both are shaped exactly like an EffectRegistry definition - id, name,
// category, description, mediaType, icon - so the browser draws them with the
// same delegate and counts them the same way. What sets them apart is "kind",
// which tells the browser how to apply the thing:
//
//  - kind "preset": "steps" is a list of {effectId, parameters} that get pushed
//    onto the clip's effect stack in order, through the ordinary
//    Backend::addClipEffect + setClipEffectParameter. Nothing here is a new kind
//    of processing; a preset is a recipe over effects that already exist, which
//    is also why every one of them stays adjustable afterwards.
//
//  - kind "lumetri": "settings" is a map of ColorSettings clip keys applied by
//    Backend::applyClipColorPreset. These are the ones that show up instantly in
//    the program monitor, because the Lumetri chain is graded on the GPU rather
//    than rendered through FFmpeg.
//
// Both lists are static: they cost one build of a QVariantList for the whole
// session and are handed to QML as CONSTANT properties.
class EffectPresets {
public:
  static QVariantList effectPresets();
  static QVariantList lumetriPresets();
};
