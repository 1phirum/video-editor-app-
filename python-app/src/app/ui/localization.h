#pragma once

// Runtime UI language switching (English / Khmer).
//
// Every user-facing QML string is wrapped in qsTr(), so it is looked up through
// the QTranslator installed on the application. Switching language swaps that
// translator and calls QQmlEngine::retranslate(), which re-evaluates every qsTr
// binding in the loaded scene live - no restart. English is the source
// language, so it needs no .qm: with no translator installed the literal source
// strings show.
//
// This lives on the launcher (cutpro.exe) rather than in an engine .dll because
// it holds the QQmlApplicationEngine* it has to call retranslate() on, and only
// the launcher owns the engine - the same reason AppCursor lives here.

#include <QObject>
#include <QString>
#include <QVariantList>

class QQmlApplicationEngine;
class QTranslator;

class Localization : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString language READ language NOTIFY languageChanged)

public:
  explicit Localization(QObject *parent = nullptr);

  QString language() const { return m_language; }

  // Set after the engine exists so a later setLanguage() can retranslate the
  // live scene. Before it is set (the startup call), the translator is still
  // installed so the first paint is already in the chosen language.
  void setEngine(QQmlApplicationEngine *engine) { m_engine = engine; }

  // Swap the installed translator to `code` ("en" | "km") and retranslate the
  // scene. A no-op if the language is already current. Unknown codes fall back
  // to English (source strings).
  Q_INVOKABLE void setLanguage(const QString &code);

  // [{ code, label }] for the settings selector, e.g.
  // [{code:"en",label:"English"},{code:"km",label:"ខ្មែរ"}].
  Q_INVOKABLE QVariantList availableLanguages() const;

signals:
  void languageChanged();

private:
  QQmlApplicationEngine *m_engine = nullptr;
  QTranslator *m_translator = nullptr;
  QString m_language;
};
