#include "ai_message_model.h"

int AiMessageModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : m_messages.size();
}

QVariant AiMessageModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_messages.size()) return {};
  const auto &message = m_messages[index.row()];
  if (role == SpeakerRole) return message.speaker;
  if (role == TextRole) return message.text;
  return {};
}

QHash<int, QByteArray> AiMessageModel::roleNames() const {
  return {{SpeakerRole, "speaker"}, {TextRole, "messageText"}};
}

void AiMessageModel::append(const QString &speaker, const QString &text) {
  if (text.trimmed().isEmpty()) return;
  if (m_messages.size() >= 120) {
    beginRemoveRows({}, 0, 0); m_messages.removeFirst(); endRemoveRows();
  }
  const int row = m_messages.size();
  beginInsertRows({}, row, row);
  m_messages.append({speaker, text.left(16000)});
  endInsertRows();
}

void AiMessageModel::clear() {
  if (m_messages.isEmpty()) return;
  beginRemoveRows({}, 0, m_messages.size() - 1); m_messages.clear(); endRemoveRows();
}

QString AiMessageModel::recentContext() const {
  QString context;
  for (int i = qMax(0, static_cast<int>(m_messages.size()) - 6); i < m_messages.size(); ++i) {
    const auto &message = m_messages[i];
    context += message.speaker + ": " + message.text.left(2000) + '\n';
  }
  return context;
}
