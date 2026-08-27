#include "app/effects/clip_effects.h"

#include <QtGlobal>

namespace {
bool assign(QVariantMap *map, const QString &key, const QVariant &value) {
  if (!map || map->value(key) == value)
    return false;
  map->insert(key, value);
  return true;
}
} // namespace

QVariantMap ClipEffects::defaults() {
  return {{"scale", 100.0},        {"positionX", 0.0},
          {"positionY", 0.0},      {"rotation", 0.0},
          {"anchorPointX", 0.5},   {"anchorPointY", 0.5},
          {"uniformScale", true},  {"scaleWidth", 100.0},
          {"scaleHeight", 100.0},  {"antiFlicker", 0.0},
          {"blendMode", QStringLiteral("normal")},
          {"speed", 100.0},
          {"maskType", QStringLiteral("none")},
          {"maskFeather", 0.0},   {"maskOpacity", 100.0},
          {"maskExpansion", 0.0}, {"maskInverted", false},
          {"opacity", 100.0},      {"cropLeft", 0.0},
          {"cropRight", 0.0},      {"cropTop", 0.0},
          {"cropBottom", 0.0},     {"horizontalFlip", false},
          {"verticalFlip", false}, {"blur", 0.0},
          {"volumeDb", 0.0},       {"volumeBypass", false},
          {"channelVolumeLeft", 0.0}, {"channelVolumeRight", 0.0},
          {"balance", 0.0},        {"pan", 0.0},
          {"vocalRemoval", false}, {"noiseReduction", 0.0},
          {"highPassHz", 0.0},     {"lowPassHz", 0.0},
          {"compressor", false}};
}

bool ClipEffects::setValue(QVariantMap *effects, const QString &key,
                           const QVariant &value) {
  if (!effects)
    return false;
  if (key == "horizontalFlip" || key == "verticalFlip" ||
      key == "vocalRemoval" || key == "compressor" ||
      key == "uniformScale" || key == "volumeBypass" ||
      key == "maskInverted")
    return assign(effects, key, value.toBool());

  if (key == "blendMode") {
    static const QStringList modes{
        "normal", "dissolve", "darken", "multiply", "colorBurn",
        "linearBurn", "darkerColor", "lighten", "screen", "colorDodge",
        "linearDodge", "lighterColor", "overlay", "softLight", "hardLight",
        "vividLight", "linearLight", "pinLight", "hardMix", "difference",
        "exclusion", "subtract", "divide", "hue", "saturation", "color",
        "luminosity"};
    const QString mode = value.toString();
    return modes.contains(mode) ? assign(effects, key, mode) : false;
  }
  if (key == "maskType") {
    static const QStringList types{"none", "ellipse", "rectangle", "pen"};
    const QString type = value.toString();
    return types.contains(type) ? assign(effects, key, type) : false;
  }

  const QVariantMap bounds{{"scale", QVariantList{10.0, 400.0}},
                           {"positionX", QVariantList{-100.0, 100.0}},
                           {"positionY", QVariantList{-100.0, 100.0}},
                           {"rotation", QVariantList{-180.0, 180.0}},
                           {"anchorPointX", QVariantList{0.0, 1.0}},
                           {"anchorPointY", QVariantList{0.0, 1.0}},
                           {"scaleWidth", QVariantList{10.0, 600.0}},
                           {"scaleHeight", QVariantList{10.0, 600.0}},
                           {"antiFlicker", QVariantList{0.0, 1.0}},
                           {"speed", QVariantList{1.0, 10000.0}},
                           {"maskFeather", QVariantList{0.0, 250.0}},
                           {"maskOpacity", QVariantList{0.0, 100.0}},
                           {"maskExpansion", QVariantList{-250.0, 250.0}},
                           {"opacity", QVariantList{0.0, 100.0}},
                           {"cropLeft", QVariantList{0.0, 49.0}},
                           {"cropRight", QVariantList{0.0, 49.0}},
                           {"cropTop", QVariantList{0.0, 49.0}},
                           {"cropBottom", QVariantList{0.0, 49.0}},
                           {"blur", QVariantList{0.0, 100.0}},
                           {"volumeDb", QVariantList{-60.0, 12.0}},
                           {"channelVolumeLeft", QVariantList{-60.0, 15.0}},
                           {"channelVolumeRight", QVariantList{-60.0, 15.0}},
                           {"balance", QVariantList{-100.0, 100.0}},
                           {"pan", QVariantList{-1.0, 1.0}},
                           {"noiseReduction", QVariantList{0.0, 100.0}},
                           {"highPassHz", QVariantList{0.0, 1000.0}},
                           {"lowPassHz", QVariantList{0.0, 22000.0}}};
  if (!bounds.contains(key))
    return false;
  const auto range = bounds.value(key).toList();
  return assign(
      effects, key,
      qBound(range.at(0).toDouble(), value.toDouble(), range.at(1).toDouble()));
}
