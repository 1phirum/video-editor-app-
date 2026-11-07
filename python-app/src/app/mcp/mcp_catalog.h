#pragma once

#include <QJsonObject>
#include <QMap>
#include <QMetaMethod>
#include <QPointer>
#include <QVector>

// Explicit editor surface, with schemas derived from the real Qt signatures.
// Never turn this into unrestricted QObject invocation: Backend also exposes
// credentials, installers and decoder-consumption state that are not edit tools.
class McpCatalog {
public:
  struct Action {
    QPointer<QObject> target;
    QVector<QMetaMethod> signatures;
    QJsonObject tool;
    QString group;
  };
  explicit McpCatalog(QObject *backend, QObject *keyframes = nullptr,
                      QObject *tts = nullptr);
  const QMap<QString, Action> &actions() const { return m_actions; }
  QString validate(const QString &name, const QJsonObject &arguments) const;
  QJsonObject invoke(const QString &name, const QJsonObject &arguments) const;
  static QJsonObject result(const QJsonObject &data, bool error = false);
  static bool convert(const QJsonValue &value, QMetaType type, QVariant *out);
  static QStringList writableProperties();
  static QJsonObject propertySchema();

private:
  void add(QObject *target, const QString &prefix, const QString &group,
           const QStringList &names);
  QMap<QString, Action> m_actions;
};
