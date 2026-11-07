#include <QtTest>
#include <QGuiApplication>
#include <QFont>
#include <QFontDatabase>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlIncubationController>
#include <QScopeGuard>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtQml>
#include "app/core_app/backend.h"
#include "app/diagnostics/model_guard.h"
#include "app/mcp/mcp_server.h"
#include "app/ui/app_cursor.h"

// No account store or router is used by a settings UI test.
class EmptyPool final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
  Q_PROPERTY(QStringList models READ models CONSTANT)
  Q_PROPERTY(bool busy READ no CONSTANT)
  Q_PROPERTY(bool checkingModels READ no CONSTANT)
  Q_PROPERTY(bool signingIn READ no CONSTANT)
  Q_PROPERTY(QString modelStatus READ empty CONSTANT)
  Q_PROPERTY(QString authUrl READ empty CONSTANT)
  Q_PROPERTY(QString poolMessage READ empty CONSTANT)
  Q_PROPERTY(QString messageKind READ empty CONSTANT)
public:
  QVariantMap state()const{return m_state;}
  void setState(const QVariantMap &state){m_state=state;emit stateChanged();}
  QStringList models()const{return {};}
  bool no()const{return false;}QString empty()const{return {};}
  Q_INVOKABLE void refresh(){}
  QString lastAccount;
  QString lastVerifiedAccount;
  Q_INVOKABLE void verifyAccount(const QString &id){lastVerifiedAccount=id;}
  bool lastEnabled=true;
  Q_INVOKABLE void setAccountEnabled(const QString &id,bool enabled){
    lastAccount=id;lastEnabled=enabled;
    auto accounts=m_state.value("accounts").toList();
    int ready=0;
    for(auto &value:accounts){
      auto account=value.toMap();
      if(account.value("id").toString()==id)account["enabled"]=enabled;
      if(account.value("enabled",true).toBool() && account.value("status")=="ready")++ready;
      value=account;
    }
    m_state["accounts"]=accounts;m_state["readyCount"]=ready;emit stateChanged();
  }
signals:
  void stateChanged();
private:
  QVariantMap m_state{{"accounts",QVariantList{}},{"readyCount",0},{"coreRunning",false},{"strategy","round_robin"}};
};

class McpUiTests final : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
  std::unique_ptr<Backend> backend;
  std::unique_ptr<McpServer> mcp;
  EmptyPool pool;
  std::unique_ptr<QQmlEngine> engine;
  std::unique_ptr<QObject> dialog;
  QObject *page=nullptr;
  QStringList pageWarnings;
  void draft(const QString &key,const QVariant &value){QVERIFY(QMetaObject::invokeMethod(dialog.get(),"setDraftValue",Q_ARG(QVariant,QVariant(key)),Q_ARG(QVariant,value)));}
  void screenshot(const QString &name){auto window=qobject_cast<QQuickWindow *>(dialog.get());QVERIFY(window);QTest::qWait(150);const QImage image=window->grabWindow();QVERIFY(!image.isNull());const QString dir=qEnvironmentVariable("CUTPRO_MCP_UI_ARTIFACTS");if(!dir.isEmpty()){QDir().mkpath(dir);QVERIFY(image.save(QDir(dir).filePath(name+".png")));}}
private slots:
  void initTestCase(){
    QVERIFY(temp.isValid());qputenv("CUTPRO_SETTINGS_FILE",temp.filePath("settings.ini").toUtf8());qputenv("CUTPRO_MCP_KEY_FILE",temp.filePath("key").toUtf8());qputenv("CUTPRO_CACHE_DIR",temp.filePath("cache").toUtf8());
    backend=std::make_unique<Backend>();mcp=std::make_unique<McpServer>(backend.get(),backend->keyframeEngine(),backend->textToSpeechEngine());
    connect(backend.get(),&Backend::appSettingsChanged,mcp.get(),[this]{mcp->applySettings(backend->appSettings());});
    qmlRegisterSingletonInstance("CutPro",1,0,"Backend",backend.get());qmlRegisterSingletonInstance("CutPro",1,0,"Mcp",mcp.get());qmlRegisterSingletonInstance("CutPro",1,0,"Pool",&pool);
    qmlRegisterSingletonInstance("CutPro",1,0,"ModelGuard",&ModelGuard::instance());
    qmlRegisterUncreatableType<AppCursor>("CutPro",1,0,"AppCursor","Attached cursor properties");
    engine=std::make_unique<QQmlEngine>();
    connect(engine.get(),&QQmlEngine::warnings,this,[this](const QList<QQmlError> &warnings){for(const auto &warning:warnings)if(warning.url().toString().contains("McpSettingsPage"))pageWarnings.append(warning.toString());});
    QQmlComponent component(engine.get(),QUrl::fromLocalFile(QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/settings/SettingsDialog.qml")));
    QVERIFY2(component.isReady(),qPrintable(component.errorString()));dialog.reset(component.create());QVERIFY2(dialog,qPrintable(component.errorString()));
    QVERIFY(QMetaObject::invokeMethod(dialog.get(),"open"));dialog->setProperty("currentCategory",12);page=dialog->findChild<QObject *>("mcpSettingsPage");QVERIFY(page);
  }
  void sidebarIsTextOnlyAndTabsRender(){
    const auto categories=dialog->property("categories").value<QJSValue>().toVariant().toList();QVERIFY(categories.size()==15);QCOMPARE(categories[12].toString(),"MCP");
    const auto window=qobject_cast<QQuickWindow *>(dialog.get());
    QVERIFY(!window->flags().testFlag(Qt::FramelessWindowHint));
    QVERIFY(window->flags().testFlag(Qt::WindowTitleHint));
    QVERIFY(window->flags().testFlag(Qt::WindowCloseButtonHint));
    QCOMPARE(window->title(),QStringLiteral("Settings"));
    QVERIFY(!dialog->findChild<QObject *>("settingsCloseButton"));
    screenshot("mcp-off");page->setProperty("currentTab",1);screenshot("mcp-tools");page->setProperty("currentTab",2);screenshot("mcp-activity-empty");page->setProperty("currentTab",0);
    QVERIFY2(pageWarnings.isEmpty(),qPrintable(pageWarnings.join('\n')));
  }
  void draftsDoNotListenDuringLiveAppearancePreview(){
    QVERIFY(!mcp->running());draft("mcpEnabled",true);QVERIFY(!mcp->running());draft("appearanceBrightness",60);QVERIFY(!mcp->running());QVERIFY(!backend->appSettings().value("mcpEnabled").toBool());QVERIFY(QMetaObject::invokeMethod(dialog.get(),"reject"));QVERIFY(!mcp->running());QCOMPARE(backend->appSettings().value("appearanceBrightness").toInt(),50);
  }
  void commitCancelAndWindowCloseUseRealSettings(){
    QVERIFY(QMetaObject::invokeMethod(dialog.get(),"open"));dialog->setProperty("currentCategory",12);
    QTcpServer available;QVERIFY(available.listen(QHostAddress::LocalHost,0));const int port=available.serverPort();available.close();draft("mcpPort",port);draft("mcpEnabled",true);QVERIFY(QMetaObject::invokeMethod(dialog.get(),"accept"));QVERIFY(mcp->running());QCOMPARE(mcp->port(),port);
    QVERIFY(QMetaObject::invokeMethod(dialog.get(),"open"));dialog->setProperty("currentCategory",12);page->setProperty("currentTab",0);screenshot("mcp-connected-ready");
    draft("mcpEnabled",false);QVERIFY(mcp->running());QVERIFY(QMetaObject::invokeMethod(dialog.get(),"reject"));QVERIFY(mcp->running());
    QVERIFY(QMetaObject::invokeMethod(dialog.get(),"open"));dialog->setProperty("currentCategory",12);draft("mcpEnabled",false);qobject_cast<QQuickWindow *>(dialog.get())->close();QVERIFY(mcp->running());
    QVERIFY(QMetaObject::invokeMethod(dialog.get(),"open"));draft("mcpEnabled",false);QVERIFY(QMetaObject::invokeMethod(dialog.get(),"accept"));QVERIFY(!mcp->running());QVERIFY2(pageWarnings.isEmpty(),qPrintable(pageWarnings.join('\n')));
  }
  void bulkModeToggleIsSharedAndUsesOkAndCancel(){
    QVERIFY(QMetaObject::invokeMethod(dialog.get(),"open"));
    dialog->setProperty("currentCategory",10);
    auto switches=dialog->findChildren<QQuickItem *>("geminiBulkProcessingSwitch");
    QCOMPARE(switches.size(),2);
    QQuickItem *toggle=switches.first();
    QTRY_VERIFY(toggle->isVisible());
    QVERIFY(toggle->property("checked").toBool());
    screenshot("pool-bulk-transcription");
    auto window=qobject_cast<QQuickWindow *>(dialog.get());
    auto click=[&]{QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,
        toggle->mapToScene(QPointF(toggle->width()/2,toggle->height()/2)).toPoint());};
    click();
    QTRY_VERIFY(!dialog->property("draftSettings").value<QJSValue>().property("geminiBulkProcessing").toBool());
    QVERIFY(backend->appSettings().value("geminiBulkProcessing").toBool());
    screenshot("pool-single-transcription");
    QVERIFY(QMetaObject::invokeMethod(dialog.get(),"accept"));
    QVERIFY(!backend->appSettings().value("geminiBulkProcessing").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog.get(),"open"));
    dialog->setProperty("currentCategory",11);
    toggle=switches.last();
    QTRY_VERIFY(toggle->isVisible());
    QVERIFY(!toggle->property("checked").toBool());
    screenshot("pool-single-translation");
    click();
    QTRY_VERIFY(dialog->property("draftSettings").value<QJSValue>().property("geminiBulkProcessing").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog.get(),"reject"));
    QVERIFY(!backend->appSettings().value("geminiBulkProcessing").toBool());
    QVERIFY(backend->resetAppSettings());
    QVERIFY(backend->appSettings().value("geminiBulkProcessing").toBool());
  }
  void textEffectsWaitsForCompleteGalleryWhenOpened(){
    QQuickWindow window;
    QQmlComponent component(engine.get(),QUrl::fromLocalFile(QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/panels/TextPanel.qml")));
    QTRY_VERIFY2(component.isReady(),qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object,qPrintable(component.errorString()));
    auto panel=qobject_cast<QQuickItem *>(object.get());
    QVERIFY(panel);
    panel->setParentItem(window.contentItem());
    auto context=QQmlEngine::contextForObject(panel);
    QVERIFY(context);
    auto tabs=context->objectForName("propTabs");
    auto effects=context->objectForName("textEffectsTab");
    QVERIFY(tabs);
    QVERIFY(effects);
    QQuickItem *loader=nullptr;
    for(auto item:panel->findChildren<QQuickItem *>())
      if(item->property("sourceComponent").value<QQmlComponent *>()==effects)
        loader=item;
    QVERIFY(loader);

    // Step the actual asynchronous loader ourselves so the assertions cover
    // partial creation even on machines that can load it within one frame.
    QQmlIncubationController incubation;
    auto previous=engine->incubationController();
    engine->setIncubationController(&incubation);
    auto restore=qScopeGuard([&]{engine->setIncubationController(previous);});
    window.show();
    for(int width:{400,768,1000}){
      window.resize(width,440);
      panel->setSize(QSizeF(width,440));
      tabs->setProperty("currentIndex",2);
      QVERIFY(!loader->property("active").toBool());
      tabs->setProperty("currentIndex",3);
      QCOMPARE(loader->property("status").toInt(),2); // Loader.Loading
      QVERIFY2(!loader->isVisible(),"Partially created text samples must stay hidden");
      QElapsedTimer timeout;
      timeout.start();
      while(incubation.incubatingObjectCount()>0 && timeout.elapsed()<10000){
        incubation.incubateFor(1);
        if(loader->property("status").toInt()==2)
          QVERIFY2(!loader->isVisible(),"Gallery became visible during incubation");
      }
      QCOMPARE(loader->property("status").toInt(),1); // Loader.Ready
      QVERIFY(loader->isVisible());
      auto gallery=loader->property("item").value<QObject *>();
      QVERIFY(gallery);
      QTRY_VERIFY(gallery->property("_settled").toBool());
    }
    tabs->setProperty("currentIndex",2);
    QVERIFY(!loader->isVisible());
  }
  void translationRendersLiveSegmentsAndCompletedCount(){
    const QString python=QStringLiteral(CUTPRO_SOURCE_ROOT "/dist/Cut Pro 2026/python/python.exe");
    if(!QFileInfo::exists(python))QSKIP("Bundled Python runtime is unavailable");
    QVERIFY(QMetaObject::invokeMethod(dialog.get(),"reject"));
    QTemporaryDir folder;
    const QString input=folder.filePath("source.srt");
    QFile file(input);QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray srt;
    for(int i=0;i<400;++i){
      const QString start=QTime(0,0).addSecs(i).toString("HH:mm:ss,zzz");
      const QString end=QTime(0,0).addSecs(i+1).toString("HH:mm:ss,zzz");
      srt+=QStringLiteral("%1\n%2 --> %3\nsource %4\n\n").arg(i+1).arg(start,end).arg(i).toUtf8();
    }
    QCOMPARE(file.write(srt),srt.size());file.close();
    QVERIFY(backend->importSubtitles(input));
    const QVariantList original=backend->transcript();

    QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost,0));
    QList<QTcpSocket *> held;
    int requests=0;
    bool complete=false;
    auto respond=[](QTcpSocket *socket){
      QJsonArray texts;for(int i=0;i<40;++i)texts.append("live translated text");
      const QJsonObject message{{"content",QString::fromUtf8(QJsonDocument(texts).toJson(QJsonDocument::Compact))}};
      const QByteArray body=QJsonDocument(QJsonObject{{"choices",QJsonArray{QJsonObject{{"message",message}}}}}).toJson(QJsonDocument::Compact);
      socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body);
      socket->disconnectFromHost();
    };
    connect(&server,&QTcpServer::newConnection,this,[&]{
      while(server.hasPendingConnections()){
        auto socket=server.nextPendingConnection();
        auto buffer=std::make_shared<QByteArray>();
        connect(socket,&QTcpSocket::readyRead,this,[&,socket,buffer]{
          *buffer+=socket->readAll();
          const int header=buffer->indexOf("\r\n\r\n");if(header<0)return;
          const auto length=QRegularExpression("Content-Length: (\\d+)",QRegularExpression::CaseInsensitiveOption)
              .match(QString::fromUtf8(buffer->left(header)));
          if(!length.hasMatch()||buffer->size()<header+4+length.captured(1).toInt())return;
          disconnect(socket,&QTcpSocket::readyRead,this,nullptr);
          ++requests;
          if(requests==1||complete)respond(socket);else held.append(socket);
        });
      }
    });
    auto cancel=qScopeGuard([&]{backend->cancelTranslation();QTest::qWait(100);});
    QVariantMap settings{{"pythonExecutable",python},{"translationProvider","openai_compatible"},
      {"translationTabitokenModel","fixture"},{"translationTabitokenApiKeys","fixture-key"},
      {"translationTabitokenBaseUrl",QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort())}};
    QQuickWindow window;window.resize(980,680);
    QQmlComponent component(engine.get(),QUrl::fromLocalFile(QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/panels/TextPanel.qml")));
    QTRY_VERIFY2(component.isReady(),qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());QVERIFY2(object,qPrintable(component.errorString()));
    auto panel=qobject_cast<QQuickItem *>(object.get());QVERIFY(panel);
    panel->setParentItem(window.contentItem());panel->setSize(QSizeF(980,680));
    auto context=QQmlEngine::contextForObject(panel);QVERIFY(context);
    auto tabs=context->objectForName("propTabs");QVERIFY(tabs);
    tabs->setProperty("currentIndex",2);
    panel->setProperty("subTab",0);
    auto list=context->objectForName("transcriptList");QVERIFY(list);
    auto skeleton=context->objectForName("transcriptSkeleton");QVERIFY(skeleton);
    auto summary=context->objectForName("transcriptSummary");QVERIFY(summary);
    window.show();
    QVERIFY(backend->translateTranscript("es",settings));
    QTRY_COMPARE_WITH_TIMEOUT(backend->translationCompletedSegments(),40,10000);
    QTRY_COMPARE_WITH_TIMEOUT(held.size(),1,3000);
    QCOMPARE(summary->property("text").toString(),QStringLiteral("Translating 40/400"));
    QVERIFY(list->property("visible").toBool());
    QVERIFY(!skeleton->property("visible").toBool());
    QCOMPARE(list->property("count").toInt(),400);
    QCOMPARE(backend->translationPreview().first().toMap().value("text").toString(),QStringLiteral("live translated text"));
    QCOMPARE(backend->transcript(),original);
    auto rendered=[&]{
      // ListView delegates belong to the visual tree, not the panel's QObject tree.
      QList<QQuickItem *> pending{panel};
      while(!pending.isEmpty()){
        auto item=pending.takeLast();
        if(item->isVisible()&&item->property("text").toString()==QStringLiteral("live translated text"))return true;
        pending.append(item->childItems());
      }
      return false;
    };
    QTRY_VERIFY(rendered());
    window.resize(1100,740);
    const QString artifacts=qEnvironmentVariable("CUTPRO_MCP_UI_ARTIFACTS");
    if(!artifacts.isEmpty()){
      QTest::qWait(150);QVERIFY(QDir().mkpath(artifacts));
      QVERIFY(window.grabWindow().save(QDir(artifacts).filePath("translation-live-40-of-400.png")));
    }
    backend->cancelTranslation();
    QTRY_VERIFY_WITH_TIMEOUT(!backend->translationInProgress(),3000);
    QCOMPARE(backend->transcript(),original);
    QCOMPARE(list->property("model").toList(),original);
    for(auto socket:held)socket->abort();held.clear();requests=0;
    QVERIFY(backend->translateTranscript("es",settings));
    QCOMPARE(backend->translationCompletedSegments(),0);
    QTRY_COMPARE_WITH_TIMEOUT(backend->translationCompletedSegments(),40,10000);
    QTRY_COMPARE_WITH_TIMEOUT(held.size(),1,3000);
    complete=true;for(auto socket:held)respond(socket);held.clear();
    QTRY_VERIFY_WITH_TIMEOUT(!backend->translationInProgress(),10000);
    QCOMPARE(backend->translationCompletedSegments(),400);
    QCOMPARE(backend->transcript().last().toMap().value("text").toString(),QStringLiteral("live translated text"));
    QCOMPARE(summary->property("text").toString(),QStringLiteral("400 segments"));
  }
  void poolProtectionStatesRenderAndSurviveResizeAndClose(){
    const auto previous=pool.state();
    auto restore=qScopeGuard([&]{pool.setState(previous);});
    QVariantMap state{{"core","running"},{"readyCount",1},{"inFlight",1},
      {"accounts",QVariantList{QVariantMap{{"id","fixture-account"},{"email","fix***@***"},{"status","ready"}}}},
      {"activeRequests",QVariantList{QVariantMap{{"id","fixture-request"},{"model","gemini-test"},{"stage","pacing"},{"accountId","fixture-account"}}}},
      {"protection",QVariantMap{{"state","active"},{"retryAfterSeconds",0}}}};
    pool.setState(state);
    QQmlComponent component(engine.get(),QUrl::fromLocalFile(QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/pool/PoolWindow.qml")));
    QVERIFY2(component.isReady(),qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object,qPrintable(component.errorString()));
    auto window=qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(window);
    window->setProperty("dialogWidth",1040);
    window->setProperty("dialogHeight",700);
    window->show();
    QTRY_COMPARE(window->property("routingStatus").toString(),QStringLiteral("Waiting to send"));
    auto hasVisibleText=[&](const QString &text){
      for(auto item:window->findChildren<QQuickItem *>())
        if(item->isVisible() && item->property("text").toString()==text)return true;
      return false;
    };
    QTRY_VERIFY(hasVisibleText(QStringLiteral("Request protection active")));
    const QString artifactDir=qEnvironmentVariable("CUTPRO_POOL_UI_ARTIFACTS");
    auto capture=[&](const QString &name){
      QTest::qWait(100);
      const auto image=window->grabWindow();
      QVERIFY(!image.isNull());
      if(!artifactDir.isEmpty()){QDir().mkpath(artifactDir);QVERIFY(image.save(QDir(artifactDir).filePath(name+".png")));}
    };
    capture("pool-protected");
    for(int width:{820,1040}){
      window->resize(width,560);
      state["protection"]=QVariantMap{{"state","cooldown"},{"retryAfterSeconds",60}};
      pool.setState(state);
      QTRY_COMPARE(window->property("routingStatus").toString(),QStringLiteral("Cooling down · 60 s"));
      QTRY_VERIFY(hasVisibleText(QStringLiteral("Cooling down · 60 s")));
      capture(QStringLiteral("pool-cooldown-%1").arg(width));
    }
    state["protection"]=QVariantMap{{"state","active"},{"retryAfterSeconds",0}};
    for(const auto &stage:{QStringLiteral("deduplicating"),QStringLiteral("sending"),QStringLiteral("pacing")}){
      auto requests=state.value("activeRequests").toList();
      auto request=requests[0].toMap();request["stage"]=stage;requests[0]=request;state["activeRequests"]=requests;
      pool.setState(state);
      QCoreApplication::processEvents();
    }
    QTRY_COMPARE(window->property("routingStatus").toString(),QStringLiteral("Waiting to send"));
    window->close();
    QVERIFY(!window->isVisible());
  }
  void failedTranscriptionRowOpensItsOwnReason(){
    QQmlComponent component(engine.get(),QUrl::fromLocalFile(QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/components/common/ProgressDashboard.qml")));
    QVERIFY2(component.isReady(),qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object,qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(720,480);
    object->setProperty("parent",QVariant::fromValue(window.contentItem()));
    object->setProperty("transcriptionJobs",QVariantList{QVariantMap{
      {"id","fixture-clip"},{"name","failed episode.mp4"},{"status","Failed"},
      {"detail","The model declined the transcript (OTHER)"},{"progress",0.0}}});
    window.show();
    QVERIFY(QMetaObject::invokeMethod(object.get(),"open"));
    QObject *row=nullptr;
    auto findRow=[&]{
      QList<QQuickItem *> items{window.contentItem()};
      while(!items.isEmpty()){
        auto child=items.takeLast();
        if(child->property("clipName").toString()=="failed episode.mp4")return static_cast<QObject *>(child);
        items.append(child->childItems());
      }
      return static_cast<QObject *>(nullptr);
    };
    QTRY_VERIFY((row=findRow()));
    QCOMPARE(row->property("failReason").toString(),QStringLiteral("The model declined the transcript (OTHER)"));
    QVERIFY(QMetaObject::invokeMethod(row,"detailRequested"));
    QObject *reason=nullptr;
    for(auto child:object->findChildren<QObject *>())
      if(child->property("reasonTitle").toString()=="Transcription failed")reason=child;
    QVERIFY(reason);
    QVERIFY(reason->property("visible").toBool());
    QVERIFY(reason->property("reasonText").toString().contains("failed episode.mp4"));
    QVERIFY(reason->property("reasonText").toString().contains("(OTHER)"));
    QVERIFY(QMetaObject::invokeMethod(reason,"close"));
    QVERIFY(QMetaObject::invokeMethod(object.get(),"close"));
  }
  void poolAccountCheckboxesAndSvgCloseWork(){
    const auto previous=pool.state();
    auto restore=qScopeGuard([&]{pool.setState(previous);});
    pool.setState(QVariantMap{{"core","stopped"},{"readyCount",1},
      {"accounts",QVariantList{
        QVariantMap{{"id","one"},{"email","first@example.invalid"},{"status","ready"}},
        QVariantMap{{"id","two"},{"email","second@example.invalid"},{"status","ready"},{"enabled",false}}}}});
    QQmlComponent component(engine.get(),QUrl::fromLocalFile(QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/pool/PoolWindow.qml")));
    QVERIFY2(component.isReady(),qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object,qPrintable(component.errorString()));
    auto window=qobject_cast<QQuickWindow *>(object.get());QVERIFY(window);
    window->setProperty("ownerWindow",QVariant::fromValue(qobject_cast<QQuickWindow *>(dialog.get())));
    window->setProperty("dialogWidth",1040);window->setProperty("dialogHeight",700);
    QVERIFY(QMetaObject::invokeMethod(window,"openNative"));
    auto click=[&](QQuickItem *item){
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,
        item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());
    };
    auto manage=window->findChild<QQuickItem *>("pool-manage-accounts");QVERIFY(manage);
    auto drawer=window->findChild<QObject *>("pool-account-drawer");QVERIFY(drawer);
    QTest::qWait(100);click(manage);QTRY_VERIFY(drawer->property("visible").toBool());
    auto content=drawer->property("contentItem").value<QQuickItem *>();QVERIFY(content);
    auto find=[&](const QString &name)->QQuickItem *{
      QList<QQuickItem *> pending{content};
      while(!pending.isEmpty()){
        auto item=pending.takeLast();
        if(item->objectName()==name)return item;
        pending.append(item->childItems());
      }
      return nullptr;
    };
    auto check=[&](const QString &id){return find("pool-account-enabled-"+id);};
    QTRY_VERIFY(check("one"));QVERIFY(check("one")->property("checked").toBool());
    QTRY_VERIFY(check("two"));QVERIFY(!check("two")->property("checked").toBool());
    QTRY_VERIFY(drawer->property("opened").toBool());
    QTest::qWait(150);
    auto setup=find("pool-account-setup-one");QVERIFY(setup);
    QCOMPARE(setup->property("text").toString(),QStringLiteral("Open Antigravity"));
    auto verify=find("pool-account-verify-one");QVERIFY(verify);
    auto remove=find("pool-account-remove-one");QVERIFY(remove);
    QVERIFY(remove->mapToItem(content,QPointF(remove->width(),0)).x()<=content->width());
    QVERIFY(QMetaObject::invokeMethod(verify,"clicked"));
    QCOMPARE(pool.lastVerifiedAccount,QStringLiteral("one"));
    for(bool enabled:{false,true,false,true}){
      click(check("one"));
      QTRY_COMPARE(pool.lastAccount,QStringLiteral("one"));QCOMPARE(pool.lastEnabled,enabled);
      QTRY_COMPARE(window->property("readyCount").toInt(),enabled?1:0);
      QTRY_COMPARE(check("one")->property("checked").toBool(),enabled);
    }
    auto close=find("pool-account-close");QVERIFY(close);
    QVERIFY(close->property("text").toString().isEmpty());
    auto background=close->property("background").value<QQuickItem *>();QVERIFY(background);
    QCOMPARE(background->metaObject()->className(),"QQuickItem");
    QVERIFY(QFile::exists(":/assets/icons/window-close.svg"));
    QVERIFY(QFile::exists(":/assets/icons/check.svg"));
    const QString artifactDir=qEnvironmentVariable("CUTPRO_POOL_UI_ARTIFACTS");
    if(!artifactDir.isEmpty()){
      QDir().mkpath(artifactDir);QTest::qWait(150);QVERIFY(window->grabWindow().save(QDir(artifactDir).filePath("pool-account-checkboxes.png")));
    }
    click(close);QTRY_VERIFY(!drawer->property("visible").toBool());
    window->resize(820,560);
    QTRY_VERIFY(manage->mapToScene(QPointF(manage->width()/2,manage->height()/2)).x()<window->width());
    click(manage);QTRY_VERIFY(drawer->property("opened").toBool());
    QTest::qWait(150);
    QTest::keyClick(window,Qt::Key_Escape);QTRY_VERIFY(!drawer->property("visible").toBool());
    window->close();QVERIFY(!window->isVisible());
  }
  void cleanupTestCase(){dialog.reset();engine.reset();mcp.reset();backend.reset();}
};
int main(int argc,char **argv){
  QGuiApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);QQuickStyle::setStyle("Basic");
  // Offscreen Qt has no Windows system-font default. Use the editor's normal
  // Windows UI face when rendering verification captures, not a random font.
  if (QGuiApplication::platformName() == "offscreen") {
    // The offscreen platform does not enumerate Windows' system font files.
    // Register the face too: naming an undiscovered face rendered square glyphs.
    const QString fontPath=QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/segoeui.ttf");
    const int fontId=QFontDatabase::addApplicationFont(fontPath);
    const QStringList families=QFontDatabase::applicationFontFamilies(fontId);
    app.setFont(QFont(families.isEmpty()?QStringLiteral("Segoe UI"):families.first(),9));
  }
  if(app.arguments().contains("--serve-sdk-test")) {
    const QByteArray isolatedSettings=qgetenv("CUTPRO_SETTINGS_FILE");
    // Opt-in integration uses the installed app's ordinary license verification.
    // Preferences/cache/outputs still belong to the isolated probe directory.
    if(qEnvironmentVariableIntValue("CUTPRO_TEST_STORED_LICENSE")==1) {
      QCoreApplication::setOrganizationName("Antigravity");QCoreApplication::setApplicationName("Cut Pro");qunsetenv("CUTPRO_SETTINGS_FILE");
    }
    Backend realBackend;qputenv("CUTPRO_SETTINGS_FILE",isolatedSettings);
    auto settings=realBackend.defaultAppSettings();settings["autoSaveEnabled"]=false;settings["workspaceLocation"]=qEnvironmentVariable("CUTPRO_MCP_PROBE_DIR");
    realBackend.applyAppSettings(settings);
    // All preference writes above use the isolated file. The editing-only MCP
    // catalog cannot write app settings. Keep ordinary registry license reads
    // consistent for both Backend and the independent export verification;
    // switching stores between those checks falsely signals license tampering.
    if(qEnvironmentVariableIntValue("CUTPRO_TEST_STORED_LICENSE")==1)
      qunsetenv("CUTPRO_SETTINGS_FILE");
    McpServer server(&realBackend,realBackend.keyframeEngine(),realBackend.textToSpeechEngine());
    if(!server.setListening(true,0))return 2;
    // Capture actual export progress while the official SDK drives the editor.
    // No synthetic job, progress value or client activity enters the UI.
    EmptyPool emptyPool;
    qmlRegisterSingletonInstance("CutPro",1,0,"Backend",&realBackend);
    qmlRegisterSingletonInstance("CutPro",1,0,"Mcp",&server);
    qmlRegisterSingletonInstance("CutPro",1,0,"Pool",&emptyPool);
    qmlRegisterUncreatableType<AppCursor>("CutPro",1,0,"AppCursor","Attached cursor properties");
    QQmlEngine engine;
    QQmlComponent component(&engine,QUrl::fromLocalFile(QStringLiteral(CUTPRO_SOURCE_ROOT "/qml/settings/SettingsDialog.qml")));
    std::unique_ptr<QObject> dialog(component.create());
    if(!dialog)return 4;
    QMetaObject::invokeMethod(dialog.get(),"open");
    dialog->setProperty("currentCategory",12);
    if(auto page=dialog->findChild<QObject *>("mcpSettingsPage"))page->setProperty("currentTab",2);
    bool captured=false;
    QObject::connect(&server,&McpServer::jobsChanged,&engine,[&]{
      if(captured || !realBackend.exportInProgress() || realBackend.exportProgress()<=0)return;
      captured=true;
      QTimer::singleShot(0,&engine,[&]{
        const QString dir=qEnvironmentVariable("CUTPRO_MCP_UI_ARTIFACTS");
        if(auto window=qobject_cast<QQuickWindow *>(dialog.get());window && !dir.isEmpty()){
          QDir().mkpath(dir);window->grabWindow().save(QDir(dir).filePath("mcp-real-export.png"));
        }
      });
    });
    QFile ready(qEnvironmentVariable("CUTPRO_MCP_PROBE_READY"));
    if(!ready.open(QIODevice::WriteOnly))return 3;
    ready.write(QJsonDocument(QJsonObject{{"endpoint",server.endpoint()},{"licensed",realBackend.licensed()}}).toJson());ready.close();
    QTimer::singleShot(180000,&app,&QCoreApplication::quit);return app.exec();
  }
  McpUiTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "mcp_ui_tests.moc"
