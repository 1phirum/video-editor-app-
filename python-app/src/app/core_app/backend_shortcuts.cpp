#include "app/core_app/backend.h"

#include <QKeySequence>
#include <QVariantMap>
#include <QVector>

// Keyboard-shortcut resolution for the app's built-in commands. The canonical
// action table is the single source of truth: QML never hardcodes a sequence,
// it reads Backend.shortcut(id). User overrides live in appSettings under the
// "keyboardShortcuts" map (a plain id -> portable-text-sequence map), so they
// persist through the normal settings store and honour Settings OK/Cancel.
//
// Everything here speaks QKeySequence PortableText, which is also what QML's
// Shortcut.sequence and QKeySequence::fromString parse, so a stored string,
// a QML Shortcut, and matchesShortcut() all agree on the same canonical form.

namespace {

// Which surface a shortcut is live on. A conflict only matters when two actions
// can fire in the same place: same scope, or one of them is global (global
// shortcuts fire everywhere).
enum class Scope { Global, Playback, Timeline, Media };

struct ShortcutAction {
  const char *id;
  const char *label;    // QT_TR_NOOP source; wrapped with tr() at read time.
  const char *category; // Section header on the Shortcuts settings page.
  const char *defaultSequence; // Portable text, e.g. "Ctrl+Shift+Z", "Del".
  Scope scope;
};

// Order here is the order the Shortcuts page renders (grouped by category).
// Escape-exit-fullscreen and the Ctrl+Shift+D diagnostics dev shortcut are
// deliberately absent - they stay hardcoded and are not user-rebindable.
const QVector<ShortcutAction> &shortcutTable() {
  static const QVector<ShortcutAction> table{
      {"app.fullscreen", QT_TRANSLATE_NOOP("Backend", "Toggle fullscreen"),
       QT_TRANSLATE_NOOP("Backend", "App"), "F11", Scope::Global},
      {"app.settings", QT_TRANSLATE_NOOP("Backend", "Open settings"),
       QT_TRANSLATE_NOOP("Backend", "App"), "Ctrl+,", Scope::Global},
      {"playback.togglePlay", QT_TRANSLATE_NOOP("Backend", "Play / Pause"),
       QT_TRANSLATE_NOOP("Backend", "Playback"), "Space", Scope::Playback},
      {"edit.undo", QT_TRANSLATE_NOOP("Backend", "Undo"),
       QT_TRANSLATE_NOOP("Backend", "Editing"), "Ctrl+Z", Scope::Timeline},
      {"edit.redo", QT_TRANSLATE_NOOP("Backend", "Redo"),
       QT_TRANSLATE_NOOP("Backend", "Editing"), "Ctrl+Shift+Z",
       Scope::Timeline},
      {"edit.redoAlt", QT_TRANSLATE_NOOP("Backend", "Redo (alternate)"),
       QT_TRANSLATE_NOOP("Backend", "Editing"), "Ctrl+Y", Scope::Timeline},
      {"timeline.delete", QT_TRANSLATE_NOOP("Backend", "Delete clip"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "Del", Scope::Timeline},
      {"timeline.rippleDelete",
       QT_TRANSLATE_NOOP("Backend", "Ripple delete (close the gap)"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "Shift+Del", Scope::Timeline},
      {"timeline.splitAtPlayhead",
       QT_TRANSLATE_NOOP("Backend", "Cut at playhead"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "Ctrl+K", Scope::Timeline},
      {"timeline.selectAll", QT_TRANSLATE_NOOP("Backend", "Select all clips"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "Ctrl+A", Scope::Timeline},
      {"timeline.copy", QT_TRANSLATE_NOOP("Backend", "Copy clips"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "Ctrl+C", Scope::Timeline},
      {"timeline.cut", QT_TRANSLATE_NOOP("Backend", "Cut clips"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "Ctrl+X", Scope::Timeline},
      {"timeline.paste", QT_TRANSLATE_NOOP("Backend", "Paste clips"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "Ctrl+V", Scope::Timeline},
      {"timeline.frameBack", QT_TRANSLATE_NOOP("Backend", "Step back one frame"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "Left", Scope::Timeline},
      {"timeline.frameForward",
       QT_TRANSLATE_NOOP("Backend", "Step forward one frame"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "Right", Scope::Timeline},
      {"timeline.jumpBack", QT_TRANSLATE_NOOP("Backend", "Step back one second"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "Shift+Left", Scope::Timeline},
      {"timeline.jumpForward",
       QT_TRANSLATE_NOOP("Backend", "Step forward one second"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "Shift+Right", Scope::Timeline},
      {"timeline.goToStart", QT_TRANSLATE_NOOP("Backend", "Go to start"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "Home", Scope::Timeline},
      {"timeline.goToEnd", QT_TRANSLATE_NOOP("Backend", "Go to end"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "End", Scope::Timeline},
      {"timeline.zoomIn", QT_TRANSLATE_NOOP("Backend", "Zoom in"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "=", Scope::Timeline},
      {"timeline.zoomOut", QT_TRANSLATE_NOOP("Backend", "Zoom out"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "-", Scope::Timeline},
      {"timeline.previousMarker",
       QT_TRANSLATE_NOOP("Backend", "Previous marker"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "Shift+M", Scope::Timeline},
      {"timeline.nextMarker", QT_TRANSLATE_NOOP("Backend", "Next marker"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "Alt+M", Scope::Timeline},
      {"timeline.addMarker", QT_TRANSLATE_NOOP("Backend", "Add marker"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "M", Scope::Timeline},
      {"timeline.toggleSnap", QT_TRANSLATE_NOOP("Backend", "Toggle snapping"),
       QT_TRANSLATE_NOOP("Backend", "Timeline"), "S", Scope::Timeline},
      {"tool.select", QT_TRANSLATE_NOOP("Backend", "Selection tool"),
       QT_TRANSLATE_NOOP("Backend", "Tools"), "V", Scope::Timeline},
      {"tool.trackSelect",
       QT_TRANSLATE_NOOP("Backend", "Track-select tool"),
       QT_TRANSLATE_NOOP("Backend", "Tools"), "A", Scope::Timeline},
      // These two ids read as blade/slip for historical reasons and are kept so
      // existing user rebindings survive, but the labels now say what the keys
      // actually select: B is the ripple-edit tool and C is the razor. There is
      // no slip tool.
      {"tool.blade", QT_TRANSLATE_NOOP("Backend", "Ripple edit tool"),
       QT_TRANSLATE_NOOP("Backend", "Tools"), "B", Scope::Timeline},
      {"tool.slip", QT_TRANSLATE_NOOP("Backend", "Razor tool"),
       QT_TRANSLATE_NOOP("Backend", "Tools"), "C", Scope::Timeline},
      {"tool.hand", QT_TRANSLATE_NOOP("Backend", "Hand tool"),
       QT_TRANSLATE_NOOP("Backend", "Tools"), "H", Scope::Timeline},
      {"tool.zoom", QT_TRANSLATE_NOOP("Backend", "Zoom tool"),
       QT_TRANSLATE_NOOP("Backend", "Tools"), "Z", Scope::Timeline},
      {"media.selectAll", QT_TRANSLATE_NOOP("Backend", "Select all"),
       QT_TRANSLATE_NOOP("Backend", "Project media"), "Ctrl+A", Scope::Media},
      {"media.delete", QT_TRANSLATE_NOOP("Backend", "Delete media"),
       QT_TRANSLATE_NOOP("Backend", "Project media"), "Del", Scope::Media},
      {"media.rename", QT_TRANSLATE_NOOP("Backend", "Rename"),
       QT_TRANSLATE_NOOP("Backend", "Project media"), "F2", Scope::Media},
      {"media.paste", QT_TRANSLATE_NOOP("Backend", "Paste"),
       QT_TRANSLATE_NOOP("Backend", "Project media"), "Ctrl+V", Scope::Media}};
  return table;
}

const ShortcutAction *findAction(const QString &id) {
  for (const ShortcutAction &action : shortcutTable())
    if (id == QLatin1String(action.id))
      return &action;
  return nullptr;
}

// Canonical portable text, or empty when the input is not a usable single
// combination. Round-tripping through QKeySequence rejects garbage and folds
// "Delete"/"Del" and native spellings onto one form.
QString canonicalSequence(const QString &raw) {
  const QString trimmed = raw.trimmed();
  if (trimmed.isEmpty())
    return QString();
  const QKeySequence sequence(trimmed, QKeySequence::PortableText);
  if (sequence.isEmpty() || sequence[0].key() == Qt::Key_unknown)
    return QString();
  return sequence.toString(QKeySequence::PortableText);
}

bool scopesClash(Scope a, Scope b) {
  return a == b || a == Scope::Global || b == Scope::Global;
}

} // namespace

QString Backend::shortcut(const QString &id) const {
  const ShortcutAction *action = findAction(id);
  if (!action)
    return QString();
  const QVariantMap overrides =
      m_appSettings.value(QStringLiteral("keyboardShortcuts")).toMap();
  const QString override = canonicalSequence(overrides.value(id).toString());
  if (!override.isEmpty())
    return override;
  return QString::fromLatin1(action->defaultSequence);
}

QVariantList Backend::shortcutActions() const {
  const QVariantMap overrides =
      m_appSettings.value(QStringLiteral("keyboardShortcuts")).toMap();
  QVariantList rows;
  rows.reserve(shortcutTable().size());
  for (const ShortcutAction &action : shortcutTable()) {
    const QString id = QString::fromLatin1(action.id);
    const QString defaultSequence = QString::fromLatin1(action.defaultSequence);
    const QString override = canonicalSequence(overrides.value(id).toString());
    QVariantMap row;
    row.insert(QStringLiteral("id"), id);
    row.insert(QStringLiteral("label"), tr(action.label));
    row.insert(QStringLiteral("category"), tr(action.category));
    row.insert(QStringLiteral("sequence"),
               override.isEmpty() ? defaultSequence : override);
    row.insert(QStringLiteral("defaultSequence"), defaultSequence);
    row.insert(QStringLiteral("isCustom"), !override.isEmpty());
    rows.append(row);
  }
  return rows;
}

QString Backend::shortcutConflict(const QString &id,
                                  const QString &sequence) const {
  const ShortcutAction *target = findAction(id);
  const QString wanted = canonicalSequence(sequence);
  if (!target || wanted.isEmpty())
    return QString();
  for (const ShortcutAction &action : shortcutTable()) {
    if (id == QLatin1String(action.id))
      continue;
    if (!scopesClash(target->scope, action.scope))
      continue;
    if (shortcut(QString::fromLatin1(action.id)) == wanted)
      return tr(action.label);
  }
  return QString();
}

bool Backend::matchesShortcut(const QString &id, int key, int modifiers) const {
  if (key == 0 || key == Qt::Key_unknown)
    return false;
  const QString resolved = shortcut(id);
  if (resolved.isEmpty())
    return false;
  // Only the four editing modifiers decide a match; strip keypad/group bits so
  // Delete on the numpad or an IME group still fires.
  const int mask = Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier |
                   Qt::MetaModifier;
  const QKeySequence pressed(QKeyCombination(
      static_cast<Qt::KeyboardModifiers>(modifiers & mask),
      static_cast<Qt::Key>(key)));
  const QKeySequence target(resolved, QKeySequence::PortableText);
  return !target.isEmpty() && pressed == target;
}

QString Backend::sequenceFromEvent(int key, int modifiers) const {
  if (key == 0 || key == Qt::Key_unknown)
    return QString();
  // A bare modifier keydown (the user is still holding the chord) is not a
  // usable combination - report empty so the capture control keeps waiting.
  switch (key) {
  case Qt::Key_Control:
  case Qt::Key_Shift:
  case Qt::Key_Alt:
  case Qt::Key_AltGr:
  case Qt::Key_Meta:
    return QString();
  default:
    break;
  }
  const int mask = Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier |
                   Qt::MetaModifier;
  const QKeySequence sequence(QKeyCombination(
      static_cast<Qt::KeyboardModifiers>(modifiers & mask),
      static_cast<Qt::Key>(key)));
  return sequence.toString(QKeySequence::PortableText);
}
