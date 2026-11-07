#include <QtTest>
#include <QGuiApplication>
#include <QTemporaryDir>
#include "app/ai/ai_assistant.h"
#include "app/core_app/pool_manager.h"
#include "app/mcp/mcp_server.h"
#include "ai_test_pool.h"

class AiEditorFixture final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString projectId MEMBER projectId NOTIFY projectChanged)
  Q_PROPERTY(QString sequenceId MEMBER sequenceId NOTIFY sequenceChanged)
  Q_PROPERTY(QString projectName READ projectName CONSTANT)
  Q_PROPERTY(QString sequenceName READ projectName CONSTANT)
  Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)
  Q_PROPERTY(QVariantList media READ media CONSTANT)
  Q_PROPERTY(int mediaCount READ mediaCount CONSTANT)
  Q_PROPERTY(QString selectedClipId MEMBER selectedClipId NOTIFY selectionChanged)
  Q_PROPERTY(QStringList effectEditSelection MEMBER selection NOTIFY selectionChanged)
  Q_PROPERTY(QString lastError MEMBER lastError NOTIFY errorChanged)
  Q_PROPERTY(bool licensed READ licensed CONSTANT)
  Q_PROPERTY(bool dirty MEMBER dirty NOTIFY projectChanged)
  Q_PROPERTY(bool exportInProgress MEMBER exporting NOTIFY exportChanged)
  Q_PROPERTY(double exportProgress MEMBER exportProgress NOTIFY exportChanged)
  Q_PROPERTY(QString exportStatus MEMBER exportStatus NOTIFY exportChanged)
public:
  QString projectId = "project-1", sequenceId = "sequence-1", selectedClipId = "clip-1", lastError, exportStatus;
  QStringList selection{"clip-1"};
  bool exporting = false, dirty = false;
  double exportProgress = 0;
  int colorCalls = 0, cancelCalls = 0;
  QVariant color;
  QString projectName() const { return "Fixture project"; }
  bool licensed() const { return true; }
  int mediaCount() const { return 1; }
  QVariantList media() const { return {QVariantMap{{"id", "media-1"}, {"name", "Fixture.mp4"}}}; }
  QVariantList clips() const { return {QVariantMap{{"id", "clip-1"}, {"mediaId", "media-1"}}}; }
  Q_INVOKABLE bool setClipColorSetting(const QString &clipId, const QString &key, const QVariant &value) {
    if (clipId != "clip-1" || key != "temperature") return false;
    ++colorCalls; color = value; emit clipsChanged(); return true;
  }
  Q_INVOKABLE bool startExportWithSettings(const QVariantMap &settings) {
    Q_UNUSED(settings);
    exporting = true; exportStatus = "Exporting"; emit exportChanged(); return true;
  }
  Q_INVOKABLE void cancelExport() {
    ++cancelCalls; exporting = false; exportStatus = "Export cancelled"; emit exportChanged();
  }
  Q_INVOKABLE void setEffectEditSelection(const QStringList &ids) { selection = ids; emit selectionChanged(); }
signals:
  void projectChanged();
  void sequenceChanged();
  void clipsChanged();
  void selectionChanged();
  void errorChanged();
  void exportChanged();
  void operationFailed(const QString &title, const QString &reason);
};

class AiAssistantTests final : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
  static QJsonObject colorArguments(const QString &clip = "clip-1") {
    return {{"clipId", clip}, {"key", "temperature"}, {"value", 8}};
  }
  static QJsonObject edit(const QString &tool, const QJsonObject &arguments) {
    return AiTestPool::answer(QJsonArray{AiTestPool::call("cutpro_edit_editor", tool, arguments)});
  }
private slots:
  void initTestCase() { QVERIFY(temp.isValid()); qputenv("CUTPRO_MCP_KEY_FILE", (temp.path() + "/mcp.key").toUtf8()); }
  void poolResourcesAreAuthoritative() {
    AiEditorFixture backend; McpServer editor(&backend); AiTestPool pool;
    AiAssistant assistant(&backend, &editor, &pool);
    QVERIFY(assistant.ready()); QCOMPARE(assistant.model(), "test-flash");
    QVERIFY(!editor.running()); // Internal editing does not enable external access.
    pool.setOnline(false); QVERIFY(!assistant.ready());
    QVERIFY(!assistant.send("Hello")); QCOMPARE(pool.requests.size(), 0);
    pool.endpoint = "example.invalid:20128"; pool.setOnline(true); QVERIFY(!assistant.ready());
    pool.endpoint.clear(); emit pool.stateChanged(); QVERIFY(assistant.ready());
    pool.modelNames = {}; emit pool.modelsChanged(); QVERIFY(!assistant.ready());
  }
  void reviewedEditUsesPoolAndPreservesThoughtSignature() {
    AiEditorFixture backend; McpServer editor(&backend); AiTestPool pool;
    auto call = AiTestPool::call("cutpro_edit_editor", "cutpro_setClipColorSetting", colorArguments());
    call["thoughtSignature"] = "fixture-signature";
    pool.responses = {AiTestPool::answer(QJsonArray{call}), AiTestPool::text("Warmed the selected clip.")};
    AiAssistant assistant(&backend, &editor, &pool);
    QVERIFY(assistant.send("Make the selected clip warmer."));
    QTRY_VERIFY(assistant.awaitingReview()); QCOMPARE(backend.colorCalls, 0);
    QCOMPARE(assistant.actions().first().toMap().value("targets").toString(), "clip-1");
    QCOMPARE(pool.requests.size(), 1); QVERIFY(pool.paths.first().contains("/v1beta/models/test-flash:generateContent"));
    QVERIFY(!pool.headers.first().contains("Authorization"));
    assistant.applyPlan();
    QTRY_COMPARE(assistant.state(), "completed"); QCOMPARE(backend.colorCalls, 1); QCOMPARE(backend.color.toInt(), 8);
    QCOMPARE(pool.requests.size(), 2);
    const auto history = pool.requests.last().value("contents").toArray();
    QVERIFY(QJsonDocument(history).toJson().contains("fixture-signature"));
    QVERIFY(QJsonDocument(history).toJson().contains("functionResponse"));
    QCOMPARE(assistant.actions().first().toMap().value("status").toString(), "completed");
  }
  void askRejectsEditingAndCanReadState() {
    AiEditorFixture backend; McpServer editor(&backend); AiTestPool pool;
    pool.responses = {edit("cutpro_setClipColorSetting", colorArguments())};
    AiAssistant assistant(&backend, &editor, &pool); assistant.setMode("Ask");
    QVERIFY(assistant.send("Explain this edit.")); QTRY_COMPARE(assistant.state(), "failed"); QCOMPARE(backend.colorCalls, 0);
    assistant.newConversation(); pool.reset();
    pool.responses = {AiTestPool::answer(QJsonArray{AiTestPool::call("cutpro_read_editor", "cutpro_get_state", {})}), AiTestPool::text("This is the fixture sequence.")};
    QVERIFY(assistant.send("Which sequence is open?")); QTRY_COMPARE(assistant.state(), "completed");
    QCOMPARE(pool.requests.size(), 2); QCOMPARE(backend.colorCalls, 0);
    const auto declarations = pool.requests.first().value("tools").toArray().first().toObject().value("functionDeclarations").toArray();
    QCOMPARE(declarations.size(), 1); QCOMPARE(declarations.first().toObject().value("name").toString(), "cutpro_read_editor");
  }
  void invalidBatchAndOutsideSelectionNeverApply() {
    AiEditorFixture backend; McpServer editor(&backend); AiTestPool pool;
    pool.responses = {AiTestPool::answer(QJsonArray{
      AiTestPool::call("cutpro_edit_editor", "cutpro_setClipColorSetting", colorArguments()),
      AiTestPool::call("cutpro_edit_editor", "cutpro_setClipColorSetting", colorArguments("clip-outside"))})};
    AiAssistant assistant(&backend, &editor, &pool); assistant.setScope("selection");
    QVERIFY(assistant.send("Warm my selection.")); QTRY_COMPARE(assistant.state(), "failed"); QCOMPARE(backend.colorCalls, 0);
    QVERIFY(assistant.error().contains("outside"));
    assistant.newConversation(); pool.reset();
    auto bad = colorArguments(); bad["clipId"] = 5;
    pool.responses = {edit("cutpro_setClipColorSetting", bad)};
    QVERIFY(assistant.send("Warm it.")); QTRY_COMPARE(assistant.state(), "failed"); QCOMPARE(backend.colorCalls, 0);
  }
  void staleReviewIsRejectedAndPlanRequiresEdit() {
    AiEditorFixture backend; McpServer editor(&backend); AiTestPool pool;
    pool.responses = {edit("cutpro_setClipColorSetting", colorArguments())};
    AiAssistant assistant(&backend, &editor, &pool);
    QVERIFY(assistant.send("Warm it.")); QTRY_VERIFY(assistant.awaitingReview());
    emit backend.clipsChanged(); assistant.applyPlan(); QCOMPARE(assistant.state(), "failed"); QCOMPARE(backend.colorCalls, 0);
    assistant.newConversation(); pool.reset(); assistant.setMode("Plan");
    pool.responses = {edit("cutpro_setClipColorSetting", colorArguments())};
    QVERIFY(assistant.send("Plan a warmer color.")); QTRY_VERIFY(assistant.awaitingReview());
    assistant.applyPlan(); QCOMPARE(backend.colorCalls, 0);
    assistant.setMode("Edit"); assistant.applyPlan(); QTRY_COMPARE(assistant.state(), "completed"); QCOMPARE(backend.colorCalls, 1);
  }
  void asynchronousCompletionIsAwaitedAndFailureReported() {
    AiEditorFixture backend; McpServer editor(&backend); AiTestPool pool;
    pool.responses = {edit("cutpro_startExportWithSettings", {{"settings", QJsonObject{}}})};
    AiAssistant assistant(&backend, &editor, &pool); assistant.setScope("sequence");
    QVERIFY(assistant.send("Export this sequence.")); QTRY_VERIFY(assistant.awaitingReview()); assistant.applyPlan();
    QVERIFY(backend.exporting); QCOMPARE(assistant.state(), "running"); QCOMPARE(pool.requests.size(), 1);
    backend.exportProgress = .64; emit backend.exportChanged(); QCOMPARE(assistant.progress(), .64);
    backend.exporting = false; backend.exportStatus = "Export complete"; emit backend.exportChanged();
    QTRY_COMPARE(assistant.state(), "completed"); QCOMPARE(pool.requests.size(), 2);
    QVERIFY(QJsonDocument(pool.requests.last()).toJson().contains("jobOutcome"));
    assistant.newConversation(); pool.reset();
    pool.responses = {edit("cutpro_startExportWithSettings", {{"settings", QJsonObject{}}})};
    QVERIFY(assistant.send("Export again.")); QTRY_VERIFY(assistant.awaitingReview()); assistant.applyPlan();
    backend.lastError = "Fixture export failure"; backend.exporting = false;
    emit backend.operationFailed("Export failed", backend.lastError); emit backend.exportChanged();
    QTRY_COMPARE(assistant.state(), "failed"); QCOMPARE(pool.requests.size(), 1);
  }
  void stopDoesNotCancelSharedJobsOrReplayWork() {
    AiEditorFixture backend; McpServer editor(&backend); AiTestPool pool;
    backend.exporting = true; pool.hold = true;
    AiAssistant assistant(&backend, &editor, &pool); assistant.setScope("sequence");
    QVERIFY(assistant.send("Explain this project.")); QTRY_COMPARE(pool.requests.size(), 1);
    assistant.stop(); QCOMPARE(assistant.state(), "cancelled"); QVERIFY(backend.exporting); QCOMPARE(backend.cancelCalls, 0);
    backend.exporting = false; assistant.newConversation(); pool.reset();
    pool.responses = {edit("cutpro_startExportWithSettings", {{"settings", QJsonObject{}}})};
    QVERIFY(assistant.send("Export it.")); QTRY_VERIFY(assistant.awaitingReview()); assistant.applyPlan();
    assistant.stop(); QVERIFY(!backend.exporting); QCOMPARE(backend.cancelCalls, 1); QCOMPARE(assistant.state(), "cancelled");
    QTest::qWait(60); QCOMPARE(pool.requests.size(), 1);
  }
  void projectSwitchAndPoolFailureStopDispatch() {
    AiEditorFixture backend; McpServer editor(&backend); AiTestPool pool;
    pool.hold = true; AiAssistant assistant(&backend, &editor, &pool);
    QVERIFY(assistant.send("Warm the edit.")); QTRY_COMPARE(pool.requests.size(), 1);
    backend.projectId = "project-2"; emit backend.projectChanged(); QCOMPARE(assistant.state(), "cancelled"); QCOMPARE(backend.colorCalls, 0);
    assistant.newConversation(); pool.reset(); pool.httpStatus = 429;
    pool.responses = {QJsonObject{{"error", QJsonObject{{"message", "Pool cooling down"}}}}};
    QVERIFY(assistant.send("Try again.")); QTRY_COMPARE(assistant.state(), "failed"); QCOMPARE(assistant.error(), "Pool cooling down");
    QTest::qWait(60); QCOMPARE(pool.requests.size(), 1);
  }
  void messageHistoryIsBounded() {
    AiMessageModel model;
    for (int i = 0; i < 130; ++i) model.append("user", QString::number(i));
    QCOMPARE(model.rowCount(), 120); QCOMPARE(model.data(model.index(0), AiMessageModel::TextRole).toString(), "10");
    model.clear(); QCOMPARE(model.rowCount(), 0);
  }
  void bulkSelectionAndGlobalActionsRespectScope() {
    AiEditorFixture backend; McpServer editor(&backend); AiTestPool pool;
    AiAssistant assistant(&backend, &editor, &pool);
    pool.responses = {edit("cutpro_setEffectEditSelection", {{"ids", QJsonArray{"clip-outside"}}})};
    QVERIFY(assistant.send("Select that clip.")); QTRY_COMPARE(assistant.state(), "failed");
    QCOMPARE(backend.selection, QStringList{"clip-1"});
    assistant.newConversation(); pool.reset();
    pool.responses = {edit("cutpro_startExportWithSettings", {{"settings", QJsonObject{}}})};
    QVERIFY(assistant.send("Export.")); QTRY_COMPARE(assistant.state(), "failed"); QVERIFY(!backend.exporting);
  }
  void idleProjectSwitchClearsPreviousContext() {
    AiEditorFixture backend; McpServer editor(&backend); AiTestPool pool;
    AiAssistant assistant(&backend, &editor, &pool);
    pool.responses = {AiTestPool::text("OLD_PROJECT_ONLY")};
    QVERIFY(assistant.send("Explain.")); QTRY_COMPARE(assistant.state(), "completed");
    backend.projectId = "new-project"; emit backend.projectChanged();
    QVERIFY(assistant.send("Explain the new project.")); QTRY_COMPARE(assistant.state(), "completed");
    QVERIFY(!QJsonDocument(pool.requests.last()).toJson().contains("OLD_PROJECT_ONLY"));
  }
  void consecutiveReviewedBatchesDoNotReplayCompletedActions() {
    AiEditorFixture backend; McpServer editor(&backend); AiTestPool pool;
    pool.responses = {edit("cutpro_setClipColorSetting", colorArguments()),
                      edit("cutpro_setClipColorSetting", colorArguments()), AiTestPool::text("Done.")};
    AiAssistant assistant(&backend, &editor, &pool);
    QVERIFY(assistant.send("Make two adjustments.")); QTRY_VERIFY(assistant.awaitingReview());
    assistant.applyPlan(); QTRY_COMPARE(pool.requests.size(), 2); QTRY_VERIFY(assistant.awaitingReview());
    QCOMPARE(backend.colorCalls, 1); assistant.applyPlan(); QTRY_COMPARE(assistant.state(), "completed");
    QCOMPARE(backend.colorCalls, 2); QCOMPARE(assistant.actions().size(), 2);
  }
  void livePoolReadOnlySmoke() {
    if (!qEnvironmentVariableIsSet("CUTPRO_AI_LIVE_SMOKE")) QSKIP("Live pool smoke is opt-in.");
    AiEditorFixture backend; McpServer editor(&backend);
    auto &pool = PoolManager::instance();
    AiAssistant assistant(&backend, &editor, &pool); assistant.setMode("Ask");
    QTRY_VERIFY_WITH_TIMEOUT(assistant.ready(), 15000);
    const auto originalClips = backend.clips();
    QVERIFY(assistant.send("Inspect the current editor state using cutpro_get_state, then tell me the project name and selected clip ID in one sentence."));
    QTRY_VERIFY_WITH_TIMEOUT(!assistant.busy(), 320000);
    QVERIFY2(assistant.state() == "completed", qPrintable(assistant.error()));
    QCOMPARE(backend.colorCalls, 0); QCOMPARE(backend.clips(), originalClips); QVERIFY(assistant.actions().isEmpty());
    const QString reply = assistant.messages()->data(assistant.messages()->index(assistant.messages()->rowCount() - 1, 0), AiMessageModel::TextRole).toString();
    QVERIFY(reply.contains("Fixture project")); QVERIFY(reply.contains("clip-1"));
    qInfo().noquote() << "Live pool model:" << assistant.model() << "Reply:" << reply;
  }
};

int main(int argc, char **argv) { QGuiApplication app(argc, argv); AiAssistantTests tests; return QTest::qExec(&tests, argc, argv); }
#include "ai_assistant_tests.moc"
