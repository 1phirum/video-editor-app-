#include <QtTest>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml>
#include "app/ai/ai_assistant.h"
#include "app/core_app/backend.h"
#include "app/diagnostics/model_guard.h"
#include "app/mcp/mcp_server.h"
#include "app/ui/app_cursor.h"
#include "ai_test_pool.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <atomic>
#include <thread>
#endif

class AiAssistantUiTests final : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
  AiTestPool pool;
  std::unique_ptr<Backend> backend;
  std::unique_ptr<McpServer> editor;
  std::unique_ptr<AiAssistant> assistant;
  std::unique_ptr<QQmlEngine> engine;
  QStringList warnings;
  QPoint center(QQuickItem *item) const { return item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint(); }
  void capture(QQuickWindow &window, const QString &name, QQuickItem *target = nullptr) {
    const QString directory = qEnvironmentVariable("CUTPRO_AI_SCREENSHOTS");
    if (directory.isEmpty()) return;
    QDir().mkpath(directory); QTest::qWait(100);
    auto grab = (target ? target : window.contentItem()->childItems().first())->grabToImage();
    QVERIFY(grab);
    QSignalSpy ready(grab.get(), &QQuickItemGrabResult::ready);
    QVERIFY(ready.wait(3000));
    QVERIFY(grab->saveToFile(directory + "/" + name + ".png"));
  }
  std::unique_ptr<QObject> create(const QString &path, QQuickWindow &window, const QSize &size) {
    QQmlComponent component(engine.get(), QUrl("qrc:/qml/" + path));
    if (!component.isReady()) { QTest::qFail(qPrintable(component.errorString()), __FILE__, __LINE__); return {}; }
    QVariantMap properties{{"assistant", QVariant::fromValue(assistant.get())}};
    if (path == "views/EditView.qml") properties["ownerWindow"] = QVariant::fromValue(&window);
    auto object = std::unique_ptr<QObject>(component.createWithInitialProperties(properties));
    if (!object) { QTest::qFail(qPrintable(component.errorString()), __FILE__, __LINE__); return {}; }
    auto item = qobject_cast<QQuickItem *>(object.get());
    item->setParentItem(window.contentItem()); item->setSize(size); window.resize(size);
    window.show(); QTest::qWait(80);
    return object;
  }
private slots:
  void initTestCase() {
    QVERIFY(temp.isValid());
    qputenv("CUTPRO_SETTINGS_FILE", (temp.path() + "/settings.ini").toUtf8());
    qputenv("CUTPRO_MCP_KEY_FILE", (temp.path() + "/mcp.key").toUtf8());
    QQuickStyle::setStyle("Basic");
    QGuiApplication::setFont(QFont("Segoe UI", 9));
    backend = std::make_unique<Backend>();
    editor = std::make_unique<McpServer>(backend.get(), backend->keyframeEngine(), backend->textToSpeechEngine());
    assistant = std::make_unique<AiAssistant>(backend.get(), editor.get(), &pool);
    qmlRegisterSingletonInstance("CutPro", 1, 0, "Backend", backend.get());
    qmlRegisterSingletonInstance("CutPro", 1, 0, "Mcp", editor.get());
    qmlRegisterSingletonInstance("CutPro", 1, 0, "Ai", assistant.get());
    qmlRegisterSingletonInstance("CutPro", 1, 0, "Pool", &pool);
    qmlRegisterSingletonInstance("CutPro", 1, 0, "ModelGuard", &ModelGuard::instance());
    qmlRegisterUncreatableType<AppCursor>("CutPro", 1, 0, "AppCursor", "Attached cursor properties");
    engine = std::make_unique<QQmlEngine>();
    connect(engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &errors) {
      for (const auto &error : errors) if (error.url().toString().contains("/ai/") || error.url().toString().contains("AiAssistantPanel") || error.url().toString().endsWith("/EditView.qml"))
        warnings.append(error.toString());
    });
  }
  void init() { warnings.clear(); pool.reset(); pool.setOnline(true); assistant->newConversation(); }
  void poolSetupAndTabsUseRealModels() {
    QQuickWindow window; auto object = create("panels/AiAssistantPanel.qml", window, {420, 850}); QVERIFY(object);
    auto model = object->findChild<QObject *>("aiModel"); QVERIFY(model);
    QCOMPARE(model->property("count").toInt(), 1); QCOMPARE(model->property("displayText").toString(), "test-flash");
    QSignalSpy pools(object.get(), SIGNAL(poolsRequested()));
    auto button = object->findChild<QQuickItem *>("aiOpenPools"); QVERIFY(button);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(button)); QCOMPARE(pools.size(), 1);
    pool.setOnline(false); QVERIFY(!assistant->ready());
    auto prompt = object->findChild<QObject *>("aiPrompt"); QVERIFY(prompt); prompt->setProperty("text", "My draft");
    QVERIFY(!object->findChild<QObject *>("aiSend")->property("enabled").toBool());
    capture(window, "ai-pool-setup");
    pool.setOnline(true); QVERIFY(assistant->ready()); QCOMPARE(prompt->property("text").toString(), "My draft");
    object->setProperty("currentTab", 2); QTest::qWait(30);
    QCOMPARE(object->findChild<QObject *>("aiTools")->property("count").toInt(), 6);
    capture(window, "ai-tools");
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
  }
  void keyboardSendsThroughPoolAndHandlesMultiline() {
    QQuickWindow window; auto object = create("panels/AiAssistantPanel.qml", window, {420, 850}); QVERIFY(object);
    assistant->setMode("Ask");
    auto prompt = object->findChild<QQuickItem *>("aiPrompt"); QVERIFY(prompt);
    prompt->setProperty("text", "First line"); prompt->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_End);
    QTest::keyClick(&window, Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(pool.requests.size(), 0); QVERIFY(prompt->property("text").toString().contains('\n'));
    pool.responses = {AiTestPool::text("A pool-backed reply for this test.")};
    QTest::keyClick(&window, Qt::Key_Return);
    QTRY_COMPARE(assistant->state(), "completed"); QCOMPARE(pool.requests.size(), 1);
    QCOMPARE(prompt->property("text").toString(), "");
    capture(window, "ai-chat-reply");
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
  }
  void drawerResizingAndRepeatedTogglesPreserveEditor() {
    QQuickWindow window; auto object = create("views/EditView.qml", window, {1600, 960}); QVERIFY(object);
    auto root = qobject_cast<QQuickItem *>(object.get());
    auto loader = object->findChild<QObject *>("residentEditorLoader"); QVERIFY(loader);
    auto resident = loader->property("item").value<QQuickItem *>(); QVERIFY(resident);
    for (int i = 0; i < 12; ++i) {
      object->setProperty("assistantVisible", true); QTest::qWait(5);
      QCOMPARE(loader->property("item").value<QQuickItem *>(), resident);
      object->setProperty("assistantVisible", false); QTest::qWait(5);
      QCOMPARE(loader->property("item").value<QQuickItem *>(), resident);
    }
    object->setProperty("assistantVisible", true);
    QTRY_VERIFY(object->findChild<QQuickItem *>("aiAssistantPanel"));
    auto handle = object->findChild<QQuickItem *>("aiResizeHandle"); QVERIFY(handle);
    const auto handlePosition = handle->mapToScene({1, 200}).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, handlePosition);
    QTest::mouseMove(&window, handlePosition - QPoint(65, 0), 20);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, handlePosition - QPoint(65, 0));
    QVERIFY(object->property("assistantWidth").toDouble() > 450);
    object->setProperty("assistantWidth", 420);
    capture(window, "ai-editor-desktop");
    root->setSize({1100, 760}); window.resize(1100, 760); QTest::qWait(40);
    QCOMPARE(loader->property("item").value<QQuickItem *>(), resident);
    auto panel = object->findChild<QQuickItem *>("aiAssistantPanel"); QVERIFY(panel);
    QCOMPARE(panel->width(), 360.0);
    auto prompt = panel->findChild<QQuickItem *>("aiPrompt"); QVERIFY(prompt);
    QVERIFY(prompt->mapToScene({0, 0}).y() + prompt->height() < window.height());
    capture(window, "ai-editor-compact");
    for (const QString &layout : {QString("VERTICAL"), QString("EFFECT EDITOR"), QString("ESSENTIALS")}) {
      object->setProperty("layoutPreset", layout); QTest::qWait(40);
      auto layoutItem = loader->property("item").value<QQuickItem *>(); QVERIFY(layoutItem);
      object->setProperty("assistantVisible", false); object->setProperty("assistantVisible", true);
      QCOMPARE(loader->property("item").value<QQuickItem *>(), layoutItem);
    }
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
  }
  void reviewAtMinimumHeightKeepsComposerAndApplyReachable() {
    QQuickWindow window; auto object = create("panels/AiAssistantPanel.qml", window, {360, 496}); QVERIFY(object);
    assistant->setScope("sequence"); assistant->setMode("Edit");
    auto conversation = object->findChild<QQuickItem *>("aiConversation"); QVERIFY(conversation);
    QVERIFY(conversation->property("contentHeight").toDouble() > conversation->height());
    QJsonArray calls;
    for (int i = 0; i < 6; ++i) calls.append(AiTestPool::call("cutpro_edit_editor", "cutpro_set_editor_property", {{"property", "playheadMs"}, {"value", i * 1000}}));
    pool.responses = {AiTestPool::answer(calls)};
    QVERIFY(assistant->send("Review these timeline seeks.")); QTRY_VERIFY(assistant->awaitingReview());
    QTest::qWait(100); QVERIFY(QMetaObject::invokeMethod(conversation, "positionViewAtEnd")); QTest::qWait(50);
    auto apply = object->findChild<QQuickItem *>("aiApplyChanges"); QVERIFY(apply);
    auto prompt = object->findChild<QQuickItem *>("aiPrompt"); QVERIFY(prompt);
    const auto applyPosition = apply->mapToScene({0, 0});
    QVERIFY(applyPosition.y() >= conversation->mapToScene({0, 0}).y());
    QVERIFY(applyPosition.y() + apply->height() <= prompt->mapToScene({0, 0}).y());
    QVERIFY(prompt->mapToScene({0, 0}).y() + prompt->height() < window.height());
    capture(window, "ai-review-minimum-height");
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    assistant->stop();
  }
  void splitterHasWideTargetAndRespectsDockAndDrawerBounds() {
    QQuickWindow window; auto object = create("views/EditView.qml", window, {1600, 850}); QVERIFY(object);
    auto root = qobject_cast<QQuickItem *>(object.get());
    object->setProperty("assistantVisible", true);
    auto handle = object->findChild<QQuickItem *>("aiResizeHandle"); QVERIFY(handle);
    QCOMPARE(handle->width(), 12.0);
    auto drag = [&](int delta) {
      const auto point = handle->mapToScene({1, 200}).toPoint();
      QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, point);
      QTest::mouseMove(&window, point + QPoint(delta, 0), 20);
      QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, point + QPoint(delta, 0));
    };
    drag(-65); QCOMPARE(object->property("assistantWidth").toDouble(), 485.0);
    drag(-300); QCOMPARE(object->property("assistantWidth").toDouble(), 560.0);
    root->setSize({1380, 850}); window.resize(1380, 850); QTest::qWait(30);
    QCOMPARE(object->property("assistantPanelWidth").toDouble(), 420.0);
    QCOMPARE(object->property("workspaceWidth").toDouble(), 960.0);
    QTest::mouseDClick(&window, Qt::LeftButton, Qt::NoModifier, handle->mapToScene({6, 200}).toPoint());
    QCOMPARE(object->property("assistantWidth").toDouble(), 420.0);
    root->setSize({1100, 760}); window.resize(1100, 760); QTest::qWait(30);
    drag(-70); QCOMPARE(object->property("assistantDrawerWidth").toDouble(), 430.0);
    QCOMPARE(object->property("workspaceWidth").toDouble(), 1100.0);
    QTest::mouseDClick(&window, Qt::LeftButton, Qt::NoModifier, handle->mapToScene({6, 200}).toPoint());
    QCOMPARE(object->property("assistantDrawerWidth").toDouble(), 360.0);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
  }
  void dragPopOutDockAndClosePreserveDraftAndActiveRequest() {
    QQuickWindow window; auto object = create("views/EditView.qml", window, {1600, 850}); QVERIFY(object);
    object->setProperty("assistantVisible", true); QTest::qWait(30);
    auto root = qobject_cast<QQuickItem *>(object.get());
    auto editorLoader = object->findChild<QObject *>("residentEditorLoader"); QVERIFY(editorLoader);
    auto residentEditor = editorLoader->property("item").value<QQuickItem *>();
    auto loader = object->findChild<QObject *>("residentAssistantLoader"); QVERIFY(loader);
    QPointer<QQuickItem> panel = loader->property("item").value<QQuickItem *>(); QVERIFY(panel);
    QPointer<QQuickWindow> floating = object->findChild<QQuickWindow *>("aiFloatingWindow"); QVERIFY(floating);
    QCOMPARE(floating->transientParent(), &window);
    auto header = panel->findChild<QQuickItem *>("aiHeaderDrag"); QVERIFY(header);
    auto prompt = panel->findChild<QQuickItem *>("aiPrompt"); QVERIFY(prompt);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(panel->findChild<QQuickItem *>("aiNewConversation")));
    QVERIFY(!object->property("assistantFloating").toBool());
    prompt->setProperty("text", "Keep my draft while moving the panel.");
    panel->setProperty("currentTab", 1);
    assistant->setMode("Ask"); pool.hold = true;
    QVERIFY(assistant->send("Inspect the project.")); QTRY_COMPARE(pool.requests.size(), 1);
    const int messageCount = assistant->messages()->rowCount();
    const auto point = center(header);
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, point);
    QTest::mouseMove(&window, point + QPoint(1, 0), 20);
    QVERIFY(!object->property("assistantFloating").toBool());
    QTest::mouseMove(&window, point + QPoint(40, 0), 20);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, point + QPoint(40, 0));
    QTRY_VERIFY(object->property("assistantFloating").toBool()); QTRY_VERIFY(floating->isVisible());
    QCOMPARE(loader->property("item").value<QQuickItem *>(), panel.data());
    QCOMPARE(panel->window(), floating.data());
    QCOMPARE(editorLoader->property("item").value<QQuickItem *>(), residentEditor);
    QCOMPARE(object->property("workspaceWidth").toDouble(), 1600.0);
    QCOMPARE(prompt->property("text").toString(), "Keep my draft while moving the panel.");
    QCOMPARE(panel->property("currentTab").toInt(), 1);
    QCOMPARE(assistant->messages()->rowCount(), messageCount); QVERIFY(assistant->busy());
    floating->resize(500, 620); QTest::qWait(50); QCOMPARE(panel->width(), 500.0);
    panel->setProperty("currentTab", 0);
    capture(*floating, "ai-floating", floating->contentItem());
    root->setVisible(false); QTRY_VERIFY(!floating->isVisible());
    root->setVisible(true); QTRY_VERIFY(floating->isVisible());
    QSignalSpy close(object.get(), SIGNAL(requestCloseAi()));
    QVERIFY(!floating->close()); QCOMPARE(close.size(), 1);
    object->setProperty("assistantVisible", false); QTRY_VERIFY(!floating->isVisible());
    object->setProperty("assistantVisible", true); QTRY_VERIFY(floating->isVisible());
    QVERIFY(assistant->busy());
    auto dock = panel->findChild<QQuickItem *>("aiDockToggle"); QVERIFY(dock);
    QTest::mouseClick(floating, Qt::LeftButton, Qt::NoModifier, center(dock));
    QTRY_VERIFY(!object->property("assistantFloating").toBool()); QVERIFY(!floating->isVisible());
    QCOMPARE(panel->window(), &window); QCOMPARE(prompt->property("text").toString(), "Keep my draft while moving the panel.");
    for (int i = 0; i < 3; ++i) {
      QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(dock)); QTRY_VERIFY(floating->isVisible());
      QTest::mouseDClick(floating, Qt::LeftButton, Qt::NoModifier, center(header));
      QTRY_VERIFY(!floating->isVisible()); QCOMPARE(loader->property("item").value<QQuickItem *>(), panel.data());
    }
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(dock)); QTRY_VERIFY(floating->isVisible());
    object.reset(); QTRY_VERIFY(floating.isNull()); QVERIFY(panel.isNull());
    assistant->stop();
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
  }
  void nativeHeldDragMovesPopupAndResizesEdges() {
#ifndef Q_OS_WIN
    QSKIP("Native input probe is Windows-only.");
#else
    if (!qEnvironmentVariableIsSet("CUTPRO_AI_NATIVE_DRAG"))
      QSKIP("Opt-in probe moves the desktop pointer; set CUTPRO_AI_NATIVE_DRAG=1.");
    QQuickWindow window; window.setFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    auto object = create("views/EditView.qml", window, {1100, 680}); QVERIFY(object);
    window.setPosition(40, 40); window.requestActivate();
    object->setProperty("assistantVisible", true); QTest::qWait(100);
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto panel = object->findChild<QQuickItem *>("aiAssistantPanel"); QVERIFY(panel);
    auto header = panel->findChild<QQuickItem *>("aiHeaderDrag"); QVERIFY(header);
    auto floating = object->findChild<QQuickWindow *>("aiFloatingWindow"); QVERIFY(floating);
    const auto ownerHandle = reinterpret_cast<HWND>(window.winId());
    const auto popupHandle = reinterpret_cast<HWND>(floating->winId());
    const DWORD guiThread = GetWindowThreadProcessId(ownerHandle, nullptr);
    const auto logicalPoint = center(header);
    const auto previousDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    RECT client{}; GetClientRect(ownerHandle, &client);
    POINT point{qRound(double(logicalPoint.x()) * client.right / window.width()), qRound(double(logicalPoint.y()) * client.bottom / window.height())};
    const bool mapped = ClientToScreen(ownerHandle, &point);
    SetForegroundWindow(ownerHandle);
    const auto targetHandle = WindowFromPoint(point);
    SetThreadDpiAwarenessContext(previousDpi);
    QVERIFY(mapped); QCOMPARE(targetHandle, ownerHandle);
    RECT before{}, after{};
    // Native frame/DPI rounding can shift the origin by a few physical pixels.
    auto followsPointer = [&](LONG dx, LONG dy) {
      return qAbs(after.left - before.left - dx) <= 8 && qAbs(after.top - before.top - dy) <= 8;
    };
    // OS move/resize runs a native message loop. A worker supplies input and
    // releases the button while the GUI thread processes that loop.
    auto gesture = [&](POINT start, POINT trigger, POINT end) {
      std::atomic_bool complete = false;
      bool entered = false;
      std::thread input([&] {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        POINT original{}; GetCursorPos(&original);
        SetCursorPos(start.x, start.y); Sleep(80);
        INPUT button{}; button.type = INPUT_MOUSE; button.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &button, sizeof(INPUT)); Sleep(80);
        SetCursorPos(trigger.x, trigger.y);
        const auto deadline = GetTickCount64() + 5000;
        while (GetTickCount64() < deadline) {
          GUITHREADINFO info{}; info.cbSize = sizeof(info);
          if (GetGUIThreadInfo(guiThread, &info) && info.hwndMoveSize == popupHandle) { entered = true; break; }
          Sleep(10);
        }
        Sleep(100);
        GetWindowRect(popupHandle, &before);
        SetCursorPos(end.x, end.y);
        const auto updateDeadline = GetTickCount64() + 2000;
        do {
          Sleep(10); GetWindowRect(popupHandle, &after);
        } while (EqualRect(&before, &after) && GetTickCount64() < updateDeadline);
        button.mi.dwFlags = MOUSEEVENTF_LEFTUP; SendInput(1, &button, sizeof(INPUT));
        Sleep(80); SetCursorPos(original.x, original.y);
        complete = true;
      });
      while (!complete) QTest::qWait(10);
      input.join();
      return entered;
    };
    QVERIFY(gesture(point, {point.x - 40, point.y + 10}, {point.x - 140, point.y + 75}));
    QVERIFY(object->property("assistantFloating").toBool()); QVERIFY(floating->isVisible());
    QVERIFY(followsPointer(-100, 65));
    QCOMPARE(panel->window(), floating);
    const auto floatingPoint = center(header);
    const POINT title{after.left + qRound(floatingPoint.x() * floating->devicePixelRatio()), after.top + qRound(floatingPoint.y() * floating->devicePixelRatio())};
    QVERIFY(gesture(title, {title.x - 40, title.y + 10}, {title.x - 140, title.y + 75}));
    QVERIFY(followsPointer(-100, 65));
    const auto resizePosition = floating->position();
    const auto resizeWidth = floating->width();
    const POINT edge{after.right - 2, (after.top + after.bottom) / 2};
    QVERIFY(gesture(edge, {edge.x + 30, edge.y}, {edge.x + 80, edge.y}));
    QTRY_VERIFY(floating->width() > resizeWidth);
    QCOMPARE(floating->position(), resizePosition);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
#endif
  }
  void cleanupTestCase() { engine.reset(); assistant.reset(); editor.reset(); backend.reset(); }
};

int main(int argc, char **argv) { QGuiApplication app(argc, argv); AiAssistantUiTests tests; return QTest::qExec(&tests, argc, argv); }
#include "ai_assistant_ui_tests.moc"
