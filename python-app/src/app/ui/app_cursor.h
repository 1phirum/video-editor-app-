#pragma once

// Bitmap mouse cursors for the editing surfaces.
//
// Qt Quick's MouseArea.cursorShape can only name the handful of shapes in the
// Qt::CursorShape enum, and several timeline gestures have no shape in it that
// says the right thing (a razor cut showed as a crosshair, both trim handles as
// a plain horizontal resize, the zoom tool as a move-all arrow). This exposes
// the PNG cursor set under assets/cursors as an attached property so any Item
// can ask for one by name and keep the declarative form:
//
//     MouseArea {
//         AppCursor.name: root.activeTool === 3 ? "Razor" : "Select"
//     }
//
// Setting the name to "" removes the override and the Item falls back to
// whatever its parent chain provides.

#include <QCursor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml>

class QQuickItem;

class AppCursorAttached : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)

public:
  explicit AppCursorAttached(QQuickItem *item);

  QString name() const { return m_name; }
  void setName(const QString &name);

signals:
  void nameChanged();

private:
  void apply();

  QQuickItem *m_item = nullptr;
  QString m_name;
};

class AppCursor : public QObject {
  Q_OBJECT

public:
  using QObject::QObject;

  static AppCursorAttached *qmlAttachedProperties(QObject *object);

  // Builds (and caches) the cursor named in assets/cursors as
  // Cur_<name>_<hotspotX>_<hotspotY>.png. Returns a default arrow cursor when
  // the name is unknown, so a typo degrades instead of clearing the cursor.
  static QCursor cursor(const QString &name, qreal devicePixelRatio);

  // Every name the asset folder provides. Useful from the QML console when
  // picking a cursor for a new gesture.
  Q_INVOKABLE static QStringList available();
};

QML_DECLARE_TYPEINFO(AppCursor, QML_HAS_ATTACHED_PROPERTIES)
