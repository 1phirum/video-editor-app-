#pragma once

#include <QAbstractListModel>
#include <QVector>

// Conversation text only. Credentials and complete tool payloads never enter
// this model, which QML renders as plain text.
class AiMessageModel final : public QAbstractListModel {
  Q_OBJECT
public:
  enum Role { SpeakerRole = Qt::UserRole + 1, TextRole };
  explicit AiMessageModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  void append(const QString &speaker, const QString &text);
  void clear();
  QString recentContext() const;
private:
  struct Message { QString speaker, text; };
  QVector<Message> m_messages;
};
