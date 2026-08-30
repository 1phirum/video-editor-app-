#include "app/export/timeline_effect_window.h"

#include "app/effects/effect_registry.h"
#include "app/effects/effect_stack.h"
#include "app/effects/video_effect_pipeline.h"

#include <QLatin1Char>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

namespace {
// Verified against the bundled ffmpeg with `-h filter=<name>`, then by running a
// gated chain through it: every filter the video pipeline emits can carry
// `enable=` except the two in `ungatedFilters` below. `format` and `setsar` are
// format plumbing - they carry no picture change, so running them outside the
// window is not a visible difference and they need no gate.
const QSet<QString> &plumbingFilters() {
  static const QSet<QString> names{QStringLiteral("format"),
                                   QStringLiteral("setsar")};
  return names;
}

// `deflicker` has no timeline support at all. `tmix` advertises it but does not
// survive it: gating it emits duplicate timestamps at the window edge (the
// frames it had buffered while disabled come out carrying their old times), and
// an encoder handed those either drops frames or refuses them. Both are refused
// at drop time rather than exported wrong.
const QSet<QString> &ungatedFilters() {
  static const QSet<QString> names{QStringLiteral("deflicker"),
                                   QStringLiteral("tmix")};
  return names;
}

QString filterName(const QString &token) {
  const int equals = token.indexOf(QLatin1Char('='));
  return equals < 0 ? token : token.left(equals);
}

QString seconds(qint64 ms) { return QString::number(ms / 1000.0, 'f', 3); }

// The video pipeline keeps commas out of its option values on purpose - a comma
// is what separates one filter from the next - so splitting the chain on commas
// recovers exactly the filters it emitted.
QStringList chainTokens(const QString &chain) {
  return chain.isEmpty() ? QStringList()
                         : chain.split(QLatin1Char(','), Qt::SkipEmptyParts);
}

QString defaultChainFor(const QString &effectId) {
  const QVariantMap definition = EffectRegistry::definition(effectId);
  if (definition.isEmpty())
    return {};
  QVariantList stack;
  if (!EffectStack::append(&stack, definition, QStringLiteral("probe")))
    return {};
  return VideoEffectPipeline::filters(stack);
}
} // namespace

QString TimelineEffectWindow::gatedFilters(const QVariantList &effectStack,
                                          qint64 windowStartMs,
                                          qint64 windowEndMs) {
  if (windowEndMs <= windowStartMs + 1)
    return {};
  const QString chain = VideoEffectPipeline::filters(effectStack);
  if (chain.isEmpty())
    return {};

  // Half-open, the same convention the blur curve uses, so two bars that meet
  // edge to edge do not both act on the frame they share.
  const QString window = QStringLiteral("gte(t,%1)*lt(t,%2)")
                             .arg(seconds(windowStartMs), seconds(windowEndMs));

  QStringList gated;
  for (const QString &token : chainTokens(chain)) {
    const QString name = filterName(token);
    if (plumbingFilters().contains(name)) {
      gated << token;
      continue;
    }
    // A filter that cannot be gated would otherwise run over the whole clip,
    // which is the opposite of what a bar means. The drop path refuses these, so
    // reaching here means a project carried one in; dropping the whole chain is
    // the reading that cannot surprise anyone with an effect they did not ask
    // for outside the bar.
    if (ungatedFilters().contains(name))
      return {};
    gated << token + (token.contains(QLatin1Char('=')) ? QStringLiteral(":")
                                                       : QStringLiteral("="))
                 + QStringLiteral("enable='") + window + QStringLiteral("'");
  }
  return gated.join(QLatin1Char(','));
}

bool TimelineEffectWindow::supportsWindowing(const QString &effectId) {
  for (const QString &token : chainTokens(defaultChainFor(effectId))) {
    const QString name = filterName(token);
    if (!plumbingFilters().contains(name) && ungatedFilters().contains(name))
      return false;
  }
  return true;
}
