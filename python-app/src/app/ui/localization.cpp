#include "app/ui/localization.h"

#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QTranslator>
#include <QVariantMap>

void Localization::setLanguage(const QString &code) {
  // Anything not shipped as a .qm is treated as the source language.
  const QString normalized = (code == QStringLiteral("km"))
                                 ? QStringLiteral("km")
                                 : QStringLiteral("en");
  if (normalized == m_language && (m_translator || normalized == QStringLiteral("en")))
    return;

  // Drop the previous translator before installing the next one, so the
  // application never has two competing catalogues installed at once.
  if (m_translator) {
    QCoreApplication::removeTranslator(m_translator);
    delete m_translator;
    m_translator = nullptr;
  }

  if (normalized == QStringLiteral("km")) {
    auto *translator = new QTranslator(this);
    // Embedded by CMake at qrc:/i18n/cutpro_km.qm.
    if (translator->load(QStringLiteral(":/i18n/cutpro_km.qm"))) {
      QCoreApplication::installTranslator(translator);
      m_translator = translator;
    } else {
      // The catalogue is missing or unreadable: fall back to English rather
      // than leaving a half-installed state. The UI stays usable.
      delete translator;
    }
  }

  m_language = normalized;
  // Re-evaluate every qsTr binding in the loaded scene. Harmless before the
  // engine is set (startup): the translator is already installed, so the first
  // load picks it up.
  if (m_engine)
    m_engine->retranslate();
  emit languageChanged();
}

QVariantList Localization::availableLanguages() const {
  QVariantList languages;
  languages.append(QVariantMap{{QStringLiteral("code"), QStringLiteral("en")},
                               {QStringLiteral("label"), QStringLiteral("English")}});
  languages.append(QVariantMap{{QStringLiteral("code"), QStringLiteral("km")},
                               {QStringLiteral("label"), QStringLiteral("ខ្មែរ")}});
  return languages;
}

Localization::Localization(QObject *parent) : QObject(parent) {}
