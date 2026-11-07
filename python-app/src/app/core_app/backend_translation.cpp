// Translation model discovery, kept out of backend.cpp.
//
// One question - "what can this endpoint actually run?" - answered by asking the
// endpoint instead of shipping a list. Everything the interface needs comes back
// through translationModelsChecked(): the catalogue, the name to show for the
// provider, and the URL the probe reached.

#include "app/core_app/backend.h"
#include "app/settings/app_settings.h"

bool Backend::checkTranslationModels(const QVariantMap &settings) {
  // Wired here rather than in the constructor: nothing about launch needs the
  // probe, and a session that never opens the settings dialog never pays for it.
  if (!m_translationModelProbeWired) {
    m_translationModelProbeWired = true;
    connect(&m_translationModelProbe, &TranslationModelProbe::stateChanged, this,
            &Backend::translationModelsChanged);
    connect(&m_translationModelProbe, &TranslationModelProbe::finished, this,
            [this](bool ok, const QStringList &models, const QString &label,
                   const QString &baseUrl, const QString &message) {
              // A failed check is worth the error banner - the user pressed a
              // button and is owed the reason - but a successful one is not
              // news, and the section prints the count itself.
              if (!ok && !message.isEmpty())
                setError(message);
              else if (ok)
                clearError();
              emit translationModelsChanged();
              emit translationModelsChecked(ok, models, label, baseUrl, message);
            });
  }
  const QVariantMap probeSettings =
      settings.isEmpty() ? m_appSettings : AppSettings::normalized(settings);
  if (!m_translationModelProbe.start(probeSettings)) {
    setError(m_translationModelProbe.status());
    emit translationModelsChanged();
    return false;
  }
  clearError();
  emit translationModelsChanged();
  return true;
}

void Backend::cancelTranslationModelCheck() {
  m_translationModelProbe.cancel();
}
