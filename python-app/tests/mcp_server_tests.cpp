#include <QtTest>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include "app/mcp/mcp_server.h"
#include "app/settings/app_settings.h"

class EditorFixture final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool playing MEMBER playing)
  Q_PROPERTY(qint64 playheadMs MEMBER playhead)
  Q_PROPERTY(QVariantList clips READ clips)
  Q_PROPERTY(QVariantMap appSettings READ secrets)
  Q_PROPERTY(QString lastError MEMBER lastError)
  Q_PROPERTY(bool exportInProgress MEMBER exporting NOTIFY exportChanged)
  Q_PROPERTY(double exportProgress MEMBER exportProgress NOTIFY exportChanged)
  Q_PROPERTY(double exportSpeed MEMBER speed NOTIFY exportChanged)
  Q_PROPERTY(qint64 exportRemainingSeconds MEMBER remaining NOTIFY exportChanged)
public:
  bool playing=false, exporting=false; qint64 playhead=0, remaining=12;
  double exportProgress=0.64, speed=1.6; QString lastError; QVariant received;
  QVariantList clips() const { QVariantList result;for(int i=0;i<240;++i)result.append(QVariantMap{{"id",QString::number(i)}});return result; }
  QVariantMap secrets() const {return {{"translationApiKeys","fixture-secret-must-not-be-exposed"}};}
  Q_INVOKABLE bool newProject(const QString &name=QStringLiteral("Untitled"),const QString &location=QString(),bool createSequence=true) {received=QVariantMap{{"name",name},{"location",location},{"sequence",createSequence}};return true;}
  Q_INVOKABLE QString addClip(const QString &mediaId,qint64 startMs=-1,const QString &track=QString()) {received=QVariantMap{{"mediaId",mediaId},{"startMs",startMs},{"track",track}};return "clip-created";}
  Q_INVOKABLE QVariantMap setClipEffectParameter(const QString &clipId,const QString &instanceId,const QString &parameterId,const QVariant &value) {received=value;return {{"clipId",clipId},{"instanceId",instanceId},{"parameterId",parameterId},{"value",value}};}
  Q_INVOKABLE void importMediaAsync(const QStringList &paths,bool copyIntoProject=false) {received=QVariantMap{{"paths",paths},{"copy",copyIntoProject}};}
  Q_INVOKABLE bool removeClip(const QString &clipId) {Q_UNUSED(clipId);lastError="Fixture operation rejected";return false;}
  Q_INVOKABLE void cancelExport() {exporting=false;emit exportChanged();}
  Q_INVOKABLE bool applyAppSettings(const QVariantMap &) {return true;}
  Q_INVOKABLE bool installRequirement(const QString &) {return true;}
  Q_INVOKABLE bool trackLocked(const QString &track) const {Q_UNUSED(track);return false;}
signals:
  void exportChanged();
};

class McpTests final : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
  struct Reply {int status=0;QByteArray body;QJsonObject json()const{return QJsonDocument::fromJson(body).object();}};
  Reply send(McpServer &server,QJsonObject request,QMap<QByteArray,QByteArray> headers={},bool authorize=true,const QByteArray &verb="POST") {
    QNetworkAccessManager manager;QNetworkRequest query(QUrl(server.endpoint()));
    query.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    if(authorize)query.setRawHeader("Authorization","Bearer "+server.connectionKey().toLatin1());
    for(auto it=headers.begin();it!=headers.end();++it)query.setRawHeader(it.key(),it.value());
    QNetworkReply *reply=manager.sendCustomRequest(query,verb,QJsonDocument(request).toJson(QJsonDocument::Compact));
    QEventLoop loop;QTimer timer;timer.setSingleShot(true);connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);timer.start(5000);loop.exec();
    return {reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),reply->readAll()};
  }
  QJsonObject request(const QString &method,QJsonObject params={}) {return {{"jsonrpc","2.0"},{"id",1},{"method",method},{"params",params}};}
  QJsonObject tool(McpServer &server,const QString &name,QJsonObject args={}) {return send(server,request("tools/call",{{"name",name},{"arguments",args}})).json().value("result").toObject();}
private slots:
  void initTestCase(){QVERIFY(temp.isValid());qputenv("CUTPRO_MCP_KEY_FILE",temp.filePath("key").toUtf8());}
  void defaultsAreOffAndPortIsBounded(){const auto defaults=AppSettings::defaults();QVERIFY(!defaults.value("mcpEnabled").toBool());QCOMPARE(defaults.value("mcpPort").toInt(),20129);QCOMPARE(AppSettings::normalized({{"mcpPort",90000}}).value("mcpPort").toInt(),65535);QCOMPARE(AppSettings::normalized({{"mcpPort",-1}}).value("mcpPort").toInt(),1024);}
  void initializationNotificationsAndTransport(){EditorFixture backend;McpServer server(&backend);QVERIFY(server.setListening(true,0));auto r=send(server,request("initialize",{{"protocolVersion","2025-11-25"},{"clientInfo",QJsonObject{{"name","MCP fixture"}}}}));QCOMPARE(r.status,200);QCOMPARE(r.json()["result"].toObject()["protocolVersion"].toString(),"2025-11-25");QCOMPARE(server.lastClient(),"MCP fixture");QVERIFY(!server.lastSeen().isEmpty());auto notification=request("notifications/initialized");notification.remove("id");r=send(server,notification);QCOMPARE(r.status,202);QVERIFY(r.body.isEmpty());QCOMPARE(send(server,request("ping")).status,200);QCOMPARE(send(server,{}, {},true,"GET").status,405);QVERIFY(server.setListening(false,0));QVERIFY(!server.running());}
  void authenticationHostOriginAndVersion(){EditorFixture backend;McpServer server(&backend);QVERIFY(server.setListening(true,0));QCOMPARE(send(server,request("ping"),{},false).status,401);QCOMPARE(send(server,request("ping"),{{"Authorization","Bearer wrong"}}).status,401);QCOMPARE(send(server,request("ping"),{{"Host","example.invalid"}}).status,403);QCOMPARE(send(server,request("ping"),{{"Origin","https://example.invalid"}}).status,403);QCOMPARE(send(server,request("ping"),{{"Origin","null"}}).status,403);QCOMPARE(send(server,request("ping"),{{"MCP-Protocol-Version","wrong"}}).status,400);QCOMPARE(send(server,request("ping"),{{"Origin",("http://127.0.0.1:"+QString::number(server.port())).toLatin1()}}).status,200);}
  void typedDiscoveryAndArguments(){EditorFixture backend;McpServer server(&backend);QVERIFY(server.setListening(true,0));const auto list=send(server,request("tools/list")).json()["result"].toObject()["tools"].toArray();QStringList names;for(const auto &value:list)names.append(value.toObject()["name"].toString());QVERIFY(names.contains("cutpro_newProject"));QVERIFY(names.contains("cutpro_importMediaAsync"));QVERIFY(!names.contains("cutpro_applyAppSettings"));QVERIFY(!names.contains("cutpro_installRequirement"));QVERIFY(!tool(server,"cutpro_newProject")["isError"].toBool());QCOMPARE(backend.received.toMap().value("name").toString(),"Untitled");QVERIFY(!tool(server,"cutpro_newProject",{{"name","Example"},{"location",""},{"createSequence",false}})["isError"].toBool());QVERIFY(!backend.received.toMap().value("sequence").toBool());QVERIFY(tool(server,"cutpro_newProject",{{"name",12}})["isError"].toBool());QVERIFY(tool(server,"cutpro_newProject",{{"name","x"},{"unknown",true}})["isError"].toBool());QVERIFY(!tool(server,"cutpro_addClip",{{"mediaId","media-1"},{"startMs",1234},{"track","V1"}})["isError"].toBool());QCOMPARE(backend.received.toMap().value("startMs").toLongLong(),1234);QVERIFY(tool(server,"cutpro_addClip",{{"mediaId","m"},{"startMs",1.5}})["isError"].toBool());QVERIFY(!tool(server,"cutpro_importMediaAsync",{{"paths",QJsonArray{"a.mp4","b.mp4"}}})["isError"].toBool());QCOMPARE(backend.received.toMap().value("paths").toStringList().size(),2);QVERIFY(tool(server,"cutpro_importMediaAsync",{{"paths",QJsonArray{12}}})["isError"].toBool());const auto variant=tool(server,"cutpro_setClipEffectParameter",{{"clipId","c"},{"instanceId","i"},{"parameterId","p"},{"value",QJsonObject{{"x",0.5}}}});QVERIFY(!variant["isError"].toBool());QCOMPARE(backend.received.toMap().value("x").toDouble(),0.5);QVERIFY(tool(server,"cutpro_removeClip",{{"clipId","c"}})["isError"].toBool());}
  void statePaginationAndPropertiesDoNotExposeSecrets(){EditorFixture backend;McpServer server(&backend);QVERIFY(server.setListening(true,0));const auto state=tool(server,"cutpro_get_state",{{"section","clips"},{"offset",200},{"limit",30}})["structuredContent"].toObject();QCOMPARE(state["total"].toInt(),240);QCOMPARE(state["items"].toArray().size(),30);QCOMPARE(state["nextOffset"].toInt(),230);QVERIFY(tool(server,"cutpro_get_state",{{"section","clips"},{"limit",201}})["isError"].toBool());QVERIFY(tool(server,"cutpro_get_state",{{"section","appSettings"}})["isError"].toBool());QVERIFY(!QJsonDocument(tool(server,"cutpro_get_state")).toJson().contains("fixture-secret"));QVERIFY(!tool(server,"cutpro_set_editor_property",{{"property","playing"},{"value",true}})["isError"].toBool());QVERIFY(backend.playing);QVERIFY(tool(server,"cutpro_set_editor_property",{{"property","playing"},{"value","true"}})["isError"].toBool());QVERIFY(!tool(server,"cutpro_set_editor_property",{{"property","playheadMs"},{"value",700}})["isError"].toBool());QCOMPARE(backend.playhead,700);QVERIFY(tool(server,"cutpro_set_editor_property",{{"property","appSettings"},{"value",QJsonObject{}}})["isError"].toBool());QVERIFY(!QJsonDocument::fromVariant(server.activities()).toJson().contains("fixture-secret"));}
  void falseReadOnlyResultsAreValues(){EditorFixture backend;McpServer server(&backend);QVERIFY(server.setListening(true,0));const auto result=tool(server,"cutpro_trackLocked",{{"track","V1"}});QVERIFY(!result["isError"].toBool());QCOMPARE(result["structuredContent"].toObject()["value"].toBool(),false);}
  void keyPersistenceJobSignalsAndPortConflict(){EditorFixture backend;McpServer server(&backend);QVERIFY(server.setListening(true,0));const auto key=server.connectionKey();McpServer other(&backend);QCOMPARE(other.connectionKey(),key);QVERIFY(!other.setListening(true,server.port()));QVERIFY(!other.running());QSignalSpy spy(&server,&McpServer::jobsChanged);backend.exporting=true;emit backend.exportChanged();QCOMPARE(spy.size(),1);const auto jobs=server.jobs();QCOMPARE(jobs.size(),1);QCOMPARE(jobs[0].toMap().value("progress").toDouble(),0.64);QCOMPARE(jobs[0].toMap().value("speed").toDouble(),1.6);QVERIFY(server.cancelJob("export"));QVERIFY(server.jobs().isEmpty());QVERIFY(!server.cancelJob("unknown"));}
  void fragmentedRequestsAndBodyLimit(){EditorFixture backend;McpServer server(&backend);QVERIFY(server.setListening(true,0));QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,server.port());QVERIFY(socket.waitForConnected(1000));const auto body=QJsonDocument(request("ping")).toJson(QJsonDocument::Compact);const QByteArray head="POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:"+QByteArray::number(server.port())+"\r\nAuthorization: Bearer "+server.connectionKey().toLatin1()+"\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n";socket.write(head.left(35));QTest::qWait(10);socket.write(head.mid(35)+body.left(3));QTest::qWait(10);socket.write(body.mid(3));QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable()>0,3000);QByteArray response=socket.readAll();QVERIFY(response.startsWith("HTTP/1.1 200"));QTcpSocket large;large.connectToHost(QHostAddress::LocalHost,server.port());QVERIFY(large.waitForConnected(1000));QByteArray oversized=head;oversized.replace("Content-Length: "+QByteArray::number(body.size()),"Content-Length: 99999999");large.write(oversized);QTRY_VERIFY_WITH_TIMEOUT(large.bytesAvailable()>0,3000);QVERIFY(large.readAll().startsWith("HTTP/1.1 413"));}
  void disablingClosesUnfinishedRequests(){EditorFixture backend;McpServer server(&backend);QVERIFY(server.setListening(true,0));QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,server.port());QVERIFY(socket.waitForConnected(1000));socket.write("POST /mcp HTTP/1.1\r\n");QTest::qWait(20);QVERIFY(server.setListening(false,0));QTRY_COMPARE_WITH_TIMEOUT(socket.state(),QAbstractSocket::UnconnectedState,3000);}
};

int main(int argc,char **argv){QGuiApplication app(argc,argv);McpTests test;return QTest::qExec(&test,argc,argv);}
#include "mcp_server_tests.moc"
