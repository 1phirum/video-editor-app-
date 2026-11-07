#include "mcp_catalog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaProperty>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <cmath>
#include <exception>

namespace {
QJsonObject schema(QMetaType type) {
  switch (type.id()) {
  case QMetaType::Bool: return {{"type", "boolean"}};
  case QMetaType::Int:
  case QMetaType::LongLong: return {{"type", "integer"}};
  case QMetaType::Double: return {{"type", "number"}};
  case QMetaType::QString: return {{"type", "string"}};
  case QMetaType::QStringList:
    return {{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}};
  case QMetaType::QVariantList: return {{"type", "array"}};
  case QMetaType::QVariantMap: return {{"type", "object"}};
  case QMetaType::QVariant: return {};
  default: return {{"unsupported", true}};
  }
}
QString humanName(QString name) {
  name.replace(QRegularExpression("([a-z])([A-Z])"), "\\1 \\2");
  if (!name.isEmpty()) name[0] = name[0].toUpper();
  return name;
}
bool readAction(const QString &name) {
  return name.startsWith("get") || name.startsWith("is") ||
         name.startsWith("track") || name.startsWith("clipBy") ||
         name.startsWith("mediaBy") || name.startsWith("list") ||
         name.startsWith("keyframesFor") || name.startsWith("keyframeAt") ||
         name.startsWith("instanceChannel") || name.startsWith("interpolated") ||
         name.startsWith("valueAt") || name.startsWith("instanceValue") ||
         name.startsWith("previousTime") || name.startsWith("nextTime") ||
         name == "effectEditTargets" || name == "editableClipIds" ||
         name == "gapAt" || name == "sequenceSourceInfo" ||
         name == "exportEstimate" || name == "suggestedExportPath" ||
         name == "generatedSpeechReport" || name == "speechSpeedForPicture" ||
         name == "transcriptText" || name == "captionFontCatalog" ||
         name == "voicePresets" || name == "clipVoiceId";
}
} // namespace

McpCatalog::McpCatalog(QObject *backend, QObject *keyframes, QObject *tts) {
  add(backend, "cutpro_", "Projects & media", {
      "newProject", "createSequence", "saveProject", "loadProject",
      "listWorkspaceProjects", "createWorkspaceProject", "openWorkspaceProject",
      "importMediaAsync", "cancelMediaImport", "removeMedia", "removeMediaSelection",
      "renameMedia", "mediaById", "clipById", "isTranscribableMedia"});
  add(backend, "cutpro_", "Timeline & playback", {
      "addClip", "addClips", "addMediaToTimeline", "addMediaSelectionToTimeline",
      "beginTimelinePlacement", "cancelTimelinePlacement", "moveClip", "moveClips",
      "splitClip", "trimClipStart", "trimClipEnd", "deleteClipLeft", "deleteClipRight",
      "removeClip", "removeClips", "addTrack", "removeLastTrack", "setTrackMuted",
      "setTrackState", "trackState", "trackLocked", "trackVisible", "trackSolo",
      "trackSyncLocked", "trackTargeted", "trackForRow", "compatibleTrackFor",
      "rippleDeleteClips", "rippleTrimClipEnd", "closeGap", "gapAt", "closeAllGaps",
      "addMarker", "updateMarker", "removeMarker", "editableClipIds",
      "setEffectEditSelection", "effectEditTargets", "applyClipSelectionCommand",
      "setClipSpeed", "setClipEnabled", "toggleClipsEnabled", "setClipFieldOptions",
      "groupClips", "ungroupClips", "linkClips", "unlinkClips", "copyClips", "cutClips",
      "pasteClips", "pasteClipAttributes", "setClipLabel", "undo", "redo"});
  add(backend, "cutpro_", "Effects & color", {
      "addImageOverlay", "applyClipColorPreset", "setColorSetting", "setClipColorSetting",
      "resetClipColorSettings", "setMediaColorSetting", "setClipEffectSetting",
      "resetClipEffectSettings", "addClipEffect", "addTimelineEffect", "removeClipEffect",
      "moveClipEffect", "setClipEffectEnabled", "setClipEffectParameter",
      "resetClipEffectInstance", "setCustomBlurMask", "applyEffectSelectionCommand"});
  add(keyframes, "cutpro_keyframes_", "Effects & color", {
      "addKeyframe", "removeKeyframe", "toggleKeyframing", "interpolatedValue",
      "valueAt", "keyframesFor", "isKeyframed", "previousTime", "nextTime",
      "setInterpolation", "keyframeAt", "instanceChannel", "instanceValueAt"});
  add(backend, "cutpro_", "Transcription & translation", {
      "transcribeMedia", "transcribeMediaBatch", "cancelTranscription", "importSubtitles",
      "exportTranscriptSrt", "exportTranscriptTtml", "updateTranscriptSegment",
      "deleteTranscriptSegment", "deleteTranscriptRange", "addTranscriptToTimeline",
      "removeTranscriptFromTimeline", "addTextClip", "updateTextClip", "translateTranscript",
      "cancelTranslation", "generateRecap", "cancelRecap", "saveRecapText",
      "transcriptText", "replaceTranscriptWithRecap", "captionFontCatalog",
      "setCaptionPositionNormalized", "setCaptionBlurRegionNormalized"});
  add(backend, "cutpro_", "Voice & audio", {
      "generateTimedTextToSpeech", "generatedSpeechReport", "chainGeneratedSpeech",
      "speechSpeedForPicture", "extractClipAudio", "restoreClipAudio", "cancelDemucs",
      "analyzeSilence", "analyzeClipsSilence", "cancelSilenceScan", "applySilenceRemoval",
      "applyClipsSilenceRemoval", "clearSilenceCandidates", "voicePresets",
      "applyVoice", "applyClipsVoice", "revertVoice", "clipVoiceId"});
  // TTS's low-level generateSegments takes executable paths. Keep that private;
  // generateTimedTextToSpeech resolves the real bundled worker and owns import.
  add(tts, "cutpro_tts_", "Voice & audio", {"cancel"});
  add(backend, "cutpro_", "Export & jobs", {
      "startExport", "startExportWithSettings", "cancelExport", "exportEstimate",
      "sequenceSourceInfo", "suggestedExportPath", "detectSceneEdits",
      "detectClipsSceneEdits", "cancelSceneDetect"});
}

void McpCatalog::add(QObject *target, const QString &prefix, const QString &group,
                     const QStringList &names) {
  if (!target) return;
  const QSet<QString> allow(names.cbegin(), names.cend());
  const QMetaObject *meta = target->metaObject();
  for (int i = QObject::staticMetaObject.methodCount(); i < meta->methodCount(); ++i) {
    const QMetaMethod method = meta->method(i);
    const QString name = QString::fromLatin1(method.name());
    if (!allow.contains(name) || method.access() != QMetaMethod::Public ||
        method.methodType() == QMetaMethod::Signal || method.parameterCount() > 10)
      continue;
    if (method.returnMetaType().id() != QMetaType::Void &&
        schema(method.returnMetaType()).contains("unsupported")) continue;
    bool supported = true;
    for (int j = 0; j < method.parameterCount(); ++j)
      if (schema(method.parameterMetaType(j)).contains("unsupported")) supported = false;
    if (!supported) continue;
    auto &action = m_actions[prefix + name];
    action.target = target; action.group = group; action.signatures.append(method);
  }
  for (auto it = m_actions.begin(); it != m_actions.end(); ++it) {
    auto &action = it.value();
    const QMetaMethod *full = &action.signatures.first();
    int minimum = full->parameterCount();
    for (const auto &method : action.signatures) {
      minimum = qMin(minimum, method.parameterCount());
      if (method.parameterCount() > full->parameterCount()) full = &method;
    }
    QJsonObject properties; QJsonArray required;
    const auto arguments = full->parameterNames();
    for (int j = 0; j < full->parameterCount(); ++j) {
      const QString parameter = QString::fromLatin1(arguments.at(j));
      QJsonObject field = schema(full->parameterMetaType(j));
      if (parameter.endsWith("Ms")) field["description"] = "Time in integer milliseconds.";
      if (j >= minimum) field["description"] = "Optional trailing argument. Supply earlier optional arguments when using this one.";
      properties[parameter] = field;
      if (j < minimum) required.append(parameter);
    }
    const QString methodName = QString::fromLatin1(full->name());
    QString description = humanName(methodName) + ". Uses the existing Cut Pro editor operation.";
    if (methodName == "importMediaAsync") description = "Import local video/audio/images asynchronously. Poll cutpro_get_state (jobs or media) until import finishes; then use the returned media IDs.";
    if (methodName == "addImageOverlay") description = "Add an overlay using an existing media ID. Import a file first with cutpro_importMediaAsync; this tool does not synchronously import paths.";
    if (methodName.startsWith("transcribe")) description += " Starts a background job; poll cutpro_get_state jobs/transcript. Use model gemini for the configured account pool; pass an empty model/language to use preferences.";
    if (methodName.startsWith("startExport")) description += " Starts export using the normal app engine and license checks. Poll jobs for progress/speed/remaining time. Completion is asynchronous.";
    const bool read = readAction(methodName);
    action.tool = {{"name", it.key()}, {"title", humanName(methodName)},
      {"description", description}, {"inputSchema", QJsonObject{{"type", "object"},
        {"properties", properties}, {"required", required}, {"additionalProperties", false}}},
      {"annotations", QJsonObject{{"readOnlyHint", read}, {"destructiveHint", !read},
        {"idempotentHint", read}, {"openWorldHint", methodName.contains("ranscri") || methodName.contains("ranslat") || methodName.contains("Speech")}}}};
  }
}

bool McpCatalog::convert(const QJsonValue &value, QMetaType type, QVariant *out) {
  if (value.isUndefined() || value.isNull()) return false;
  const int id = type.id();
  if (id == QMetaType::Bool && !value.isBool()) return false;
  if (id == QMetaType::QString && !value.isString()) return false;
  if ((id == QMetaType::Int || id == QMetaType::LongLong || id == QMetaType::Double) && !value.isDouble()) return false;
  if (id == QMetaType::Int || id == QMetaType::LongLong) {
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        (id == QMetaType::Int && (number < -2147483648.0 || number > 2147483647.0)) ||
        (id == QMetaType::LongLong && std::abs(number) > 9007199254740991.0)) return false;
  }
  if ((id == QMetaType::QStringList || id == QMetaType::QVariantList) && !value.isArray()) return false;
  if (id == QMetaType::QStringList)
    for (const auto &item : value.toArray()) if (!item.isString()) return false;
  if (id == QMetaType::QVariantMap && !value.isObject()) return false;
  *out = value.toVariant();
  return id == QMetaType::QVariant || out->convert(type);
}

QJsonObject McpCatalog::result(const QJsonObject &data, bool error) {
  return {{"content", QJsonArray{QJsonObject{{"type", "text"},
      {"text", QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact))}}}},
      {"structuredContent", data}, {"isError", error}};
}

QString McpCatalog::validate(const QString &name, const QJsonObject &arguments) const {
  const auto found = m_actions.constFind(name);
  if (found == m_actions.cend() || !found->target)
    return QStringLiteral("Unknown editing tool.");
  for (const QMetaMethod &method : found->signatures) {
    if (method.parameterCount() != arguments.size()) continue;
    const auto names = method.parameterNames();
    bool valid = true;
    for (int i = 0; i < names.size(); ++i) {
      QVariant converted;
      if (!convert(arguments.value(QString::fromLatin1(names[i])),
                   method.parameterMetaType(i), &converted)) { valid = false; break; }
    }
    if (valid) return {};
  }
  return QStringLiteral("Arguments do not match an advertised signature. Use exact names/types and supply earlier optional arguments before later ones.");
}

QJsonObject McpCatalog::invoke(const QString &name, const QJsonObject &arguments) const {
  const auto found = m_actions.constFind(name);
  if (found == m_actions.cend() || !found->target)
    return result({{"error", "Unknown editing tool."}}, true);
  const auto &action = found.value();
  Q_ASSERT(action.target->thread() == QThread::currentThread());
  if (name == "cutpro_importMediaAsync" && action.target->property("mediaImportInProgress").toBool())
    return result({{"error", "An import is already running. Wait for it to finish or cancel it before starting another."}}, true);
  if (name == "cutpro_addImageOverlay") {
    const QString mediaId = arguments.value("pathOrMediaId").toString();
    QVariantMap media;
    if (!QMetaObject::invokeMethod(action.target, "mediaById", Qt::DirectConnection,
        Q_RETURN_ARG(QVariantMap, media), Q_ARG(QString, mediaId)) || media.isEmpty())
      return result({{"error", "Import the overlay through cutpro_importMediaAsync first, then provide its media ID."}}, true);
  }
  for (const QMetaMethod &method : action.signatures) {
    if (method.parameterCount() != arguments.size()) continue;
    const auto names = method.parameterNames(); const auto types = method.parameterTypes();
    QVector<QVariant> values(method.parameterCount()); QGenericArgument args[10];
    bool valid = true;
    for (int i = 0; i < names.size(); ++i) {
      if (!convert(arguments.value(QString::fromLatin1(names[i])), method.parameterMetaType(i), &values[i])) { valid = false; break; }
      args[i] = QGenericArgument(types[i].constData(),
          method.parameterMetaType(i).id() == QMetaType::QVariant ? &values[i] : values[i].constData());
    }
    if (!valid) continue;
    const QString previousError = action.target->property("lastError").toString();
    const QMetaType returnType = method.returnMetaType();
    QVariant returned = returnType.id() == QMetaType::Void ? QVariant() : QVariant(returnType);
    const QGenericReturnArgument ret = returnType.id() == QMetaType::Void
      ? QGenericReturnArgument() : QGenericReturnArgument(method.typeName(),
          returnType.id() == QMetaType::QVariant ? &returned : returned.data());
    try {
      if (!method.invoke(action.target, Qt::DirectConnection, ret,
          args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]))
        return result({{"error", "The editor could not invoke this operation."}}, true);
    } catch (const std::exception &) {
      // This is the external-input boundary. Never leak exception contents or
      // leave the TCP request unanswered; the app's normal handlers own detail.
      return result({{"error", "The editing operation raised an error."}}, true);
    } catch (...) {
      return result({{"error", "The editing operation could not complete."}}, true);
    }
    const QString error = action.target->property("lastError").toString();
    const bool read = readAction(QString::fromLatin1(method.name()));
    const bool failed = (!read && returnType.id() == QMetaType::Bool && !returned.toBool()) ||
        (!read && returnType.id() == QMetaType::QString && returned.toString().isEmpty()) ||
        (!error.isEmpty() && error != previousError);
    QJsonObject data{{"tool", name}, {"value", returnType.id() == QMetaType::Void ? QJsonValue(QJsonValue::Null) : QJsonValue::fromVariant(returned)}};
    if (failed) data["error"] = error.isEmpty() || error == previousError ? "The editor rejected the operation. Check the project, IDs and job state." : error;
    return result(data, failed);
  }
  return result({{"error", "Arguments do not match an advertised signature. Use exact names/types and supply earlier optional arguments before later ones."}}, true);
}

QStringList McpCatalog::writableProperties() {
  return {"projectName", "sequenceName", "activeWorkspace", "layoutPreset", "playheadMs", "playing", "selectedClipId", "snappingEnabled",
    "captionFontFamily", "captionFontSize", "captionBoxWidth", "captionTextColor", "captionBold", "captionItalic",
    "captionBackgroundVisible", "captionBackgroundColor", "captionPosition", "captionAlignment", "captionBlurEnabled",
    "captionBlurTrackingEnabled", "captionBlurStrength", "captionBlurPadding", "captionTextEffect", "captionAnimation", "captionAnimationSpeed"};
}
QJsonObject McpCatalog::propertySchema() {
  return {{"type", "object"}, {"properties", QJsonObject{
    {"property", QJsonObject{{"type", "string"}, {"enum", QJsonArray::fromStringList(writableProperties())}}}, {"value", QJsonObject{}}}},
    {"required", QJsonArray{"property", "value"}}, {"additionalProperties", false}};
}
