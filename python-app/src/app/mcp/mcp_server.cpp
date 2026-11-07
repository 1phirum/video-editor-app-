#include "mcp_server.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaProperty>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTimer>
#include <QThread>
#include <QUrl>
#include <QUuid>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {
constexpr int MaxBody = 2 * 1024 * 1024;
constexpr int MaxHeaders = 16 * 1024;
const QStringList Versions{"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};
bool sameKey(const QByteArray &a, const QByteArray &b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (qsizetype i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  return diff == 0;
}
QJsonObject rpcError(int code, const QString &message, const QJsonValue &id = QJsonValue(QJsonValue::Null)) {
  return {{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
}
QJsonObject stateTool() {
  return {{"name", "cutpro_get_state"}, {"title", "Get editor state"},
    {"description", "Read the current project/playback, or a paginated media, clips, transcript, tracks, effects or jobs section. Lists include stable editor IDs. Times are milliseconds. Poll jobs to observe asynchronous import/export/transcription/translation/TTS; an accepted call is not job completion."},
    {"inputSchema", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
      {"section", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"summary", "media", "clips", "transcript", "tracks", "effects", "jobs"}}}},
      {"offset", QJsonObject{{"type", "integer"}, {"minimum", 0}}},
      {"limit", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}}}}, {"additionalProperties", false}}},
    {"annotations", QJsonObject{{"readOnlyHint", true}, {"destructiveHint", false}, {"idempotentHint", true}, {"openWorldHint", false}}}};
}
QJsonObject propertyTool() {
  return {{"name", "cutpro_set_editor_property"}, {"title", "Set editor property"},
    {"description", "Set an advertised editor property: playback position/state, primary selection, project/sequence name, workspace/layout or caption style. Uses the property's real Qt type; booleans must be JSON booleans and times must be integer milliseconds."},
    {"inputSchema", McpCatalog::propertySchema()}, {"annotations", QJsonObject{
      {"readOnlyHint", false}, {"destructiveHint", true}, {"idempotentHint", true}, {"openWorldHint", false}}}};
}
QJsonObject selectProperties(QObject *object, const QStringList &names) {
  QJsonObject result;
  if (object) for (const auto &name : names) {
    const QVariant value = object->property(name.toLatin1().constData());
    if (value.isValid()) result[name] = QJsonValue::fromVariant(value);
  }
  return result;
}
} // namespace

McpServer::McpServer(QObject *backend, QObject *keyframes, QObject *tts, QObject *parent)
    : QObject(parent), m_backend(backend), m_tts(tts), m_catalog(backend, keyframes, tts) {
  loadKey();
  connect(&m_listener, &QTcpServer::newConnection, this, &McpServer::acceptConnections);
  // Connect by Qt metadata (not cross-DLL member-function pointers). Multiple
  // progress properties share a notify signal: subscribe once, never once/cue.
  const int slotIndex = metaObject()->indexOfSlot("updateJobs()");
  for (QObject *target : {backend, tts}) {
    if (!target) continue;
    QSet<int> signalIndices;
    const QMetaObject *meta = target->metaObject();
    const QStringList jobProperties{"mediaImportInProgress", "timelinePlacementInProgress", "exportInProgress",
      "transcriptionInProgress", "transcriptionBatchJobs", "translationInProgress", "recapInProgress",
      "silenceScanInProgress", "sceneDetectInProgress", "demucsInProgress", "voiceFxBusy", "inProgress"};
    for (const QString &name : jobProperties) {
      const int i = meta->indexOfProperty(name.toLatin1().constData());
      if (i < 0 || !meta->property(i).hasNotifySignal()) continue;
      const QMetaMethod signal = meta->property(i).notifySignal();
      if (!signalIndices.contains(signal.methodIndex())) {
        QObject::connect(target, signal, this, metaObject()->method(slotIndex));
        signalIndices.insert(signal.methodIndex());
      }
    }
  }
}

bool McpServer::loadKey() {
  QString path = qEnvironmentVariable("CUTPRO_MCP_KEY_FILE");
  if (path.isEmpty()) path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
      .filePath("mcp/connection.key");
  QFile file(path);
  QByteArray key;
  if (file.exists()) {
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384) { m_status = "Could not read the MCP connection key."; return false; }
    QByteArray stored = file.readAll();
#ifdef Q_OS_WIN
    const QByteArray magic("CUTPRO-MCP-DPAPI\n");
    if (!stored.startsWith(magic)) { m_status = "Invalid MCP connection key file."; return false; }
    stored.remove(0, magic.size());
    DATA_BLOB input{static_cast<DWORD>(stored.size()), reinterpret_cast<BYTE *>(stored.data())}, output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
      m_status = "Windows could not unlock the MCP connection key."; return false;
    }
    key = QByteArray(reinterpret_cast<char *>(output.pbData), output.cbData);
    LocalFree(output.pbData);
#else
    key = stored;
#endif
  } else {
    QByteArray random(32, Qt::Uninitialized);
    for (int i = 0; i < random.size(); i += 4) {
      const quint32 value = QRandomGenerator::system()->generate();
      memcpy(random.data() + i, &value, 4);
    }
    key = random.toHex(); QByteArray stored = key;
#ifdef Q_OS_WIN
    DATA_BLOB input{static_cast<DWORD>(key.size()), reinterpret_cast<BYTE *>(key.data())}, output{};
    if (!CryptProtectData(&input, L"Cut Pro MCP", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
      m_status = "Windows could not protect the MCP connection key."; return false;
    }
    stored = QByteArray("CUTPRO-MCP-DPAPI\n") + QByteArray(reinterpret_cast<char *>(output.pbData), output.cbData);
    LocalFree(output.pbData);
#endif
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) { m_status = "Could not create the MCP key folder."; return false; }
    QSaveFile saved(path);
    if (!saved.open(QIODevice::WriteOnly) || saved.write(stored) != stored.size() || !saved.commit()) {
      m_status = "Could not save the MCP connection key."; return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  }
  if (key.size() != 64 || QByteArray::fromHex(key).size() != 32) { m_status = "Invalid MCP connection key."; return false; }
  m_key = QString::fromLatin1(key); return true;
}

QString McpServer::endpoint() const { return QStringLiteral("http://127.0.0.1:%1/mcp").arg(port()); }
void McpServer::applySettings(const QVariantMap &settings) {
  setListening(settings.value("mcpEnabled", false).toBool(), settings.value("mcpPort", 20129).toInt());
}
bool McpServer::setListening(bool enabled, int requestedPort) {
  if (requestedPort < 0 || requestedPort > 65535) return false;
  if (running() && enabled && m_port == requestedPort) return true;
  m_listener.close();
  const auto sockets = m_buffers.keys();
  for (QTcpSocket *socket : sockets) socket->abort();
  m_buffers.clear(); m_port = requestedPort;
  if (!enabled) m_status = "Off";
  else if (m_key.isEmpty()) { if (!loadKey()) { emit stateChanged(); return false; } }
  if (enabled) {
    if (!m_listener.listen(QHostAddress::LocalHost, static_cast<quint16>(requestedPort))) {
      m_status = QStringLiteral("Port %1 is unavailable. Choose another port and click OK.").arg(requestedPort);
      emit stateChanged(); return false;
    }
    m_status = "Running";
  }
  emit stateChanged(); return true;
}

void McpServer::acceptConnections() {
  while (m_listener.hasPendingConnections()) {
    QTcpSocket *socket = m_listener.nextPendingConnection();
    socket->setParent(this);
    if (m_buffers.size() >= 16) { socket->disconnectFromHost(); socket->deleteLater(); continue; }
    socket->setReadBufferSize(MaxBody + MaxHeaders + 1);
    m_buffers.insert(socket, {});
    connect(socket, &QTcpSocket::readyRead, this, [this, socket] { consume(socket); });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket] { m_buffers.remove(socket); socket->deleteLater(); });
    QTimer::singleShot(15000, socket, [socket] { socket->abort(); });
  }
}

void McpServer::respond(QTcpSocket *socket, int status, const QByteArray &body) {
  if (!m_buffers.contains(socket)) return;
  m_buffers.remove(socket);
  static const QMap<int, QByteArray> labels{{200,"OK"},{202,"Accepted"},{400,"Bad Request"},{401,"Unauthorized"},
    {403,"Forbidden"},{404,"Not Found"},{405,"Method Not Allowed"},{413,"Content Too Large"},{415,"Unsupported Media Type"}};
  QByteArray header = "HTTP/1.1 " + QByteArray::number(status) + " " + labels.value(status, "Error") +
      "\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
  if (status == 401) header += "WWW-Authenticate: Bearer\r\n";
  if (status == 405) header += "Allow: POST\r\n";
  socket->write(header + "\r\n" + body); socket->disconnectFromHost();
}

void McpServer::consume(QTcpSocket *socket) {
  if (!m_buffers.contains(socket)) return;
  QByteArray &buffer = m_buffers[socket]; buffer += socket->readAll();
  if (buffer.size() > MaxHeaders + MaxBody) { respond(socket, 413); return; }
  const int end = buffer.indexOf("\r\n\r\n");
  if (end < 0) { if (buffer.size() > MaxHeaders) respond(socket, 413); return; }
  if (end > MaxHeaders) { respond(socket, 413); return; }
  const auto lines = buffer.left(end).split('\n');
  const auto start = lines.first().trimmed().split(' ');
  if (start.size() != 3 || start[2] != "HTTP/1.1") { respond(socket, 400); return; }
  QMap<QByteArray,QByteArray> headers;
  for (int i = 1; i < lines.size(); ++i) {
    const auto line = lines[i].trimmed(); const int colon = line.indexOf(':');
    if (colon <= 0) { respond(socket, 400); return; }
    const auto name = line.left(colon).toLower();
    if (headers.contains(name)) { respond(socket, 400); return; }
    headers[name] = line.mid(colon + 1).trimmed();
  }
  const QByteArray suffix = ":" + QByteArray::number(port());
  if (headers.value("host") != "127.0.0.1" + suffix && headers.value("host") != "localhost" + suffix) { respond(socket, 403); return; }
  if (headers.contains("origin") && headers["origin"] != "http://127.0.0.1" + suffix && headers["origin"] != "http://localhost" + suffix) { respond(socket, 403); return; }
  if (!sameKey(headers.value("authorization"), "Bearer " + m_key.toLatin1())) { respond(socket, 401); return; }
  if (start[1] != "/mcp") { respond(socket, 404); return; }
  if (start[0] != "POST") { respond(socket, 405); return; }
  if (headers.contains("transfer-encoding")) { respond(socket, 400); return; }
  bool validLength = false;
  const qint64 length = headers.value("content-length").toLongLong(&validLength);
  if (!validLength || length < 0) { respond(socket, 400); return; }
  if (length > MaxBody) { respond(socket, 413); return; }
  if (!headers.value("content-type").toLower().startsWith("application/json")) { respond(socket, 415); return; }
  if (headers.contains("mcp-protocol-version") && !Versions.contains(QString::fromLatin1(headers["mcp-protocol-version"]))) { respond(socket, 400); return; }
  if (buffer.size() < end + 4 + length) return;
  if (buffer.size() != end + 4 + length) { respond(socket, 400); return; }
  QJsonParseError parse;
  const auto document = QJsonDocument::fromJson(buffer.mid(end + 4, length), &parse);
  if (parse.error != QJsonParseError::NoError) {
    respond(socket, 200, QJsonDocument(rpcError(-32700,"Invalid JSON.")).toJson(QJsonDocument::Compact)); return;
  }
  if (!document.isObject()) {
    respond(socket, 200, QJsonDocument(rpcError(-32600,"Send one JSON-RPC object per request.")).toJson(QJsonDocument::Compact)); return;
  }
  const auto request = document.object();
  if (request.value("jsonrpc") != "2.0" || !request.value("method").isString() ||
      (request.contains("params") && !request.value("params").isObject()) ||
      (request.contains("id") && !request.value("id").isString() && !request.value("id").isDouble())) {
    respond(socket, 200, QJsonDocument(rpcError(-32600,"Invalid JSON-RPC request.")).toJson(QJsonDocument::Compact)); return;
  }
  m_lastSeen = QDateTime::currentDateTimeUtc(); emit activityChanged();
  // Stateless Streamable HTTP: no session IDs or SSE channel. Clients send one
  // request per POST and poll jobs; notifications are accepted without a body.
  if (!request.contains("id")) { respond(socket, 202); return; }
  respond(socket, 200, QJsonDocument(dispatch(request)).toJson(QJsonDocument::Compact));
}

QJsonArray McpServer::editorTools() const {
  QJsonArray tools{stateTool(), propertyTool()};
  for (const auto &action : m_catalog.actions()) tools.append(action.tool);
  return tools;
}

QJsonObject McpServer::editorState(const QJsonObject &arguments) const {
  Q_ASSERT(thread() == QThread::currentThread());
  return getState(arguments);
}

QString McpServer::validateEditorTool(const QString &name, const QJsonObject &arguments) const {
  if (name == "cutpro_get_state") {
    const auto result = getState(arguments);
    return result.value("isError").toBool()
        ? result.value("structuredContent").toObject().value("error").toString() : QString();
  }
  if (name == "cutpro_set_editor_property") {
    const QString propertyName = arguments.value("property").toString();
    if (arguments.size() != 2 || !McpCatalog::writableProperties().contains(propertyName) || !m_backend)
      return QStringLiteral("Unknown editor property.");
    const auto meta = m_backend->metaObject();
    const int index = meta->indexOfProperty(propertyName.toLatin1().constData());
    QVariant value;
    if (index < 0 || !meta->property(index).isWritable() ||
        !McpCatalog::convert(arguments.value("value"), meta->property(index).metaType(), &value))
      return QStringLiteral("Value does not match the editor property's type.");
    return {};
  }
  return m_catalog.validate(name, arguments);
}

QJsonObject McpServer::invokeEditorTool(const QString &name, const QJsonObject &arguments) {
  Q_ASSERT(thread() == QThread::currentThread());
  QJsonObject result;
  if (name == "cutpro_get_state") result = getState(arguments);
  else if (name == "cutpro_set_editor_property") result = writeProperty(arguments);
  else result = m_catalog.invoke(name, arguments);
  if (name != "cutpro_get_state") recordAction(name, result.value("isError").toBool());
  return result;
}

QJsonObject McpServer::dispatch(const QJsonObject &request) {
  const QString method = request.value("method").toString();
  const auto params = request.value("params").toObject(); QJsonObject value;
  if (method == "initialize") {
    const QString proposed = params.value("protocolVersion").toString();
    m_lastClient = params.value("clientInfo").toObject().value("name").toString().left(80);
    // Client names are display metadata, never HTML and never request arguments.
    m_lastClient.replace('\n', ' '); m_lastClient.replace('\r', ' '); emit activityChanged();
    value = {{"protocolVersion", Versions.contains(proposed) ? proposed : Versions.first()},
      {"capabilities", QJsonObject{{"tools", QJsonObject{{"listChanged", false}}}}},
      {"serverInfo", QJsonObject{{"name", "cut-pro"}, {"version", "1.0"}}},
      {"instructions", "Controls the open Cut Pro editor. Read cutpro_get_state for IDs and job progress. Editing actions use existing undo/redo. Import/export/transcription/translation/voice calls start asynchronous work; poll jobs. Millisecond arguments are integers. Keep Cut Pro open and its MCP server enabled."}};
  } else if (method == "ping") value = {};
  else if (method == "tools/list") {
    int offset = 0;
    if (params.contains("cursor")) {
      const QString cursor = params.value("cursor").toString(); bool ok = false;
      offset = cursor.toInt(&ok);
      if (!ok || offset < 0 || offset >= toolCount() || QString::number(offset) != cursor)
        return rpcError(-32602,"Invalid tools cursor.",request.value("id"));
    }
    const QJsonArray tools = editorTools();
    QJsonArray page;
    for (int i = offset; i < qMin(offset + 50, static_cast<int>(tools.size())); ++i) page.append(tools[i]);
    value["tools"] = page;
    if (offset + 50 < tools.size()) value["nextCursor"] = QString::number(offset + 50);
  } else if (method == "tools/call") {
    if (!params.value("name").isString() || (params.contains("arguments") && !params.value("arguments").isObject()))
      return rpcError(-32602,"Provide a tool name and object arguments.",request.value("id"));
    const QString name = params.value("name").toString(); const auto args = params.value("arguments").toObject();
    if (name != "cutpro_get_state" && name != "cutpro_set_editor_property" && !m_catalog.actions().contains(name))
      return rpcError(-32602,"Unknown editing tool.",request.value("id"));
    value = invokeEditorTool(name, args);
  } else return rpcError(-32601,"Method not found.",request.value("id"));
  return {{"jsonrpc", "2.0"}, {"id", request.value("id")}, {"result", value}};
}

QJsonObject McpServer::getState(const QJsonObject &args) const {
  for (auto it=args.begin();it!=args.end();++it)
    if (it.key()!="section" && it.key()!="offset" && it.key()!="limit") return McpCatalog::result({{"error","Unknown state argument."}},true);
  if (args.contains("section") && !args.value("section").isString())
    return McpCatalog::result({{"error","Section must be a string."}},true);
  QVariant converted;
  for (const QString &field : {QStringLiteral("offset"),QStringLiteral("limit")})
    if (args.contains(field) && !McpCatalog::convert(args[field],QMetaType::fromType<int>(),&converted)) return McpCatalog::result({{"error","Pagination must use integers."}},true);
  const int offset = args.value("offset").toInt(0), limit = args.value("limit").toInt(100);
  if (offset < 0 || limit < 1 || limit > 200) return McpCatalog::result({{"error","Use offset >= 0 and limit between 1 and 200."}},true);
  const QString section = args.value("section").toString("summary");
  const QMap<QString,QString> lists{{"media","media"},{"clips","clips"},{"transcript","transcript"},{"tracks","trackStates"},{"effects","effectDefinitions"}};
  if (section == "summary") return McpCatalog::result(selectProperties(m_backend, {
      "projectId", "projectName", "projectFile", "projectLocation", "sequenceId", "sequenceName", "activeWorkspace",
      "hasSequence", "mediaCount", "clipCount", "transcriptCount", "durationMs", "playheadMs", "playing", "dirty",
      "canUndo", "canRedo", "canExport", "selectedClipId", "effectEditSelection", "videoTrackCount", "audioTrackCount", "subtitleTrackCount", "lastError"}));
  if (section == "jobs") return McpCatalog::result({{"jobs",QJsonArray::fromVariantList(jobs())},
      {"lastError",m_backend ? m_backend->property("lastError").toString() : QString()}});
  if (!lists.contains(section) || !m_backend) return McpCatalog::result({{"error","Unknown state section."}},true);
  const QVariantList source = m_backend->property(lists[section].toLatin1().constData()).toList();
  QJsonArray items;
  for (int i=offset;i<qMin(source.size(),static_cast<qsizetype>(offset)+limit);++i) items.append(QJsonValue::fromVariant(source[i]));
  QJsonObject data{{"section",section},{"items",items},{"total",source.size()},{"offset",offset}};
  if (static_cast<qsizetype>(offset)+items.size()<source.size()) data["nextOffset"]=offset+items.size();
  return McpCatalog::result(data);
}

QJsonObject McpServer::writeProperty(const QJsonObject &args) {
  const QString name = args.value("property").toString();
  if (args.size()!=2 || !McpCatalog::writableProperties().contains(name) || !m_backend)
    return McpCatalog::result({{"error","Unknown editor property."}},true);
  const auto meta=m_backend->metaObject(); const int index=meta->indexOfProperty(name.toLatin1().constData());
  if (index<0 || !meta->property(index).isWritable()) return McpCatalog::result({{"error","This editor property is unavailable."}},true);
  const auto property=meta->property(index); QVariant value;
  if (!McpCatalog::convert(args.value("value"),property.metaType(),&value) || !property.write(m_backend,value))
    return McpCatalog::result({{"error","Value does not match the editor property's type."}},true);
  return McpCatalog::result({{"property",name},{"value",QJsonValue::fromVariant(property.read(m_backend))}});
}

QVariantList McpServer::jobs() const {
  QVariantList result;
  auto add=[&](const QString &id,const QString &title,const QString &active,const QString &progress,const QString &status,double scale=1.0) {
    if (!m_backend || !m_backend->property(active.toLatin1().constData()).toBool()) return;
    QVariantMap job{{"id",id},{"title",title},{"status",m_backend->property(status.toLatin1().constData()).toString()}};
    const QVariant p=m_backend->property(progress.toLatin1().constData());
    if (!progress.isEmpty() && p.isValid()) job["progress"]=qBound(0.0,p.toDouble()*scale,1.0);
    else job["indeterminate"]=true;
    if (id=="export") {
      job["speed"]=m_backend->property("exportSpeed"); job["remainingSeconds"]=m_backend->property("exportRemainingSeconds");
      job["framesPerSecond"]=m_backend->property("exportFramesPerSecond"); job["bytesPerSecond"]=m_backend->property("exportBytesPerSecond");
    }
    if(id=="transcription") {job["clips"]=m_backend->property("transcriptionBatchJobs");job["indeterminate"]=m_backend->property("transcriptionIndeterminate");}
    result.append(job);
  };
  add("import","Importing media","mediaImportInProgress","mediaImportProgress","",0.01);
  add("placement","Placing clips","timelinePlacementInProgress","timelinePlacementProgress","timelinePlacementStatus");
  add("export","Exporting video","exportInProgress","exportProgress","exportStatus");
  add("transcription","Transcribing clips","transcriptionInProgress","transcriptionProgress","transcriptionStatus");
  add("translation","Translating subtitles","translationInProgress","","translationStatus");
  add("recap","Generating recap","recapInProgress","","recapStatus");
  add("silence","Analyzing silence","silenceScanInProgress","silenceScanProgress","silenceScanStatus");
  add("scene","Detecting scenes","sceneDetectInProgress","sceneDetectProgress","sceneDetectStatus");
  add("demucs","Separating audio","demucsInProgress","demucsProgress","demucsStatus");
  add("voice","Applying voice effect","voiceFxBusy","","voiceFxStatus");
  if (m_tts && m_tts->property("inProgress").toBool()) {
    QVariantMap job=selectProperties(m_tts,{"progress","status"}).toVariantMap();
    job.insert("id","tts");job.insert("title","Generating voices");result.append(job);
  }
  return result;
}
QVariantList McpServer::toolGroups() const {
  QMap<QString,int> counts; for (const auto &action:m_catalog.actions()) counts[action.group]++;
  QVariantList result;
  for (const QString &name:{QStringLiteral("Projects & media"),QStringLiteral("Timeline & playback"),QStringLiteral("Effects & color"),QStringLiteral("Transcription & translation"),QStringLiteral("Voice & audio"),QStringLiteral("Export & jobs")})
    result.append(QVariantMap{{"name",name},{"count",counts.value(name)}});
  return result;
}
void McpServer::updateJobs() { emit jobsChanged(); }
void McpServer::recordAction(const QString &name,bool failed) {
  // No arguments, response values, headers or credentials in UI/history.
  const QString title=name=="cutpro_set_editor_property"?QStringLiteral("Update editor property"):
    m_catalog.actions().value(name).tool.value("title").toString(name);
  m_activities.prepend(QVariantMap{{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},
    {"title",title},{"tool",name},{"failed",failed},{"status",failed?"Rejected":"Requested"},
    {"time",QDateTime::currentDateTime().toString("hh:mm:ss")}});
  if(m_activities.size()>100)m_activities.removeLast(); emit activityChanged();
}
void McpServer::clearActivity() {m_activities.clear();emit activityChanged();}
bool McpServer::copyConnectionDetails(const QString &kind) {
  auto clipboard=QGuiApplication::clipboard();if(!clipboard||m_key.isEmpty())return false;
  if(kind=="url")clipboard->setText(endpoint());
  else if(kind=="key")clipboard->setText(m_key);
  else if(kind=="config")clipboard->setText(QString::fromUtf8(QJsonDocument(QJsonObject{
      {"mcpServers",QJsonObject{{"cut-pro",QJsonObject{{"url",endpoint()},
        {"headers",QJsonObject{{"Authorization","Bearer "+m_key}}}}}}}}).toJson(QJsonDocument::Indented)));
  else return false;
  return true;
}
bool McpServer::cancelJob(const QString &id) {
  const QMap<QString,QString> cancel{{"import","cutpro_cancelMediaImport"},{"placement","cutpro_cancelTimelinePlacement"},
    {"export","cutpro_cancelExport"},{"transcription","cutpro_cancelTranscription"},{"translation","cutpro_cancelTranslation"},
    {"recap","cutpro_cancelRecap"},{"silence","cutpro_cancelSilenceScan"},{"scene","cutpro_cancelSceneDetect"},
    {"demucs","cutpro_cancelDemucs"},{"tts","cutpro_tts_cancel"}};
  if(!cancel.contains(id))return false;
  const auto result=m_catalog.invoke(cancel[id],{});recordAction(cancel[id],result.value("isError").toBool());
  return !result.value("isError").toBool();
}
