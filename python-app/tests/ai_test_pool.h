#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantMap>
#include <memory>

// Isolated loopback model service. Never loads account files or contacts Google.
class AiTestPool final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
  Q_PROPERTY(QStringList models READ models NOTIFY modelsChanged)
  Q_PROPERTY(bool checkingModels READ checkingModels CONSTANT)
public:
  explicit AiTestPool(QObject *parent = nullptr) : QObject(parent) {
    server.listen(QHostAddress::LocalHost, 0);
    connect(&server, &QTcpServer::newConnection, this, [this] {
      while (auto socket = server.nextPendingConnection()) {
        auto data = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, data] {
          data->append(socket->readAll());
          const auto end = data->indexOf("\r\n\r\n");
          if (end < 0 || socket->property("handled").toBool()) return;
          int length = -1;
          for (const auto &line : data->left(end).split('\n'))
            if (line.trimmed().toLower().startsWith("content-length:")) length = line.mid(line.indexOf(':') + 1).trimmed().toInt();
          if (length < 0 || data->size() < end + 4 + length) return;
          socket->setProperty("handled", true);
          paths.append(data->left(data->indexOf("\r\n")));
          headers.append(data->left(end));
          requests.append(QJsonDocument::fromJson(data->mid(end + 4, length)).object());
          if (hold) return;
          const auto response = responses.isEmpty() ? text("Finished the requested work.") : responses.takeFirst();
          const QByteArray body = QJsonDocument(response).toJson(QJsonDocument::Compact);
          const int code = httpStatus; httpStatus = 200;
          QTimer::singleShot(20, socket, [socket, body, code] {
            socket->write("HTTP/1.1 " + QByteArray::number(code) + " Result\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
          });
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      }
    });
  }
  QVariantMap state() const {
    return {{"core", online ? "running" : "stopped"}, {"readyCount", online ? 1 : 0},
            {"endpoint", endpoint.isEmpty() ? "127.0.0.1:" + QString::number(server.serverPort()) : endpoint}};
  }
  QStringList models() const { return modelNames; }
  bool checkingModels() const { return false; }
  void setOnline(bool value) { online = value; emit stateChanged(); }
  void reset() { responses.clear(); requests.clear(); paths.clear(); headers.clear(); hold = false; httpStatus = 200; }
  Q_INVOKABLE void ensureCore() { ++refreshes; }
  Q_INVOKABLE void refresh() { ++refreshes; }
  Q_INVOKABLE void checkModels() { ++refreshes; }
  static QJsonObject answer(const QJsonArray &parts) {
    return {{"candidates", QJsonArray{QJsonObject{{"finishReason", "STOP"},
      {"content", QJsonObject{{"role", "model"}, {"parts", parts}}}}}}};
  }
  static QJsonObject text(const QString &value) { return answer(QJsonArray{QJsonObject{{"text", value}}}); }
  static QJsonObject call(const QString &wrapper, const QString &tool, const QJsonObject &arguments) {
    return {{"functionCall", QJsonObject{{"name", wrapper}, {"args", QJsonObject{
      {"name", tool}, {"argumentsJson", QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact))},
      {"reason", "Update the requested editor item"}}}}}};
  }
  QTcpServer server;
  QList<QJsonObject> responses, requests;
  QList<QByteArray> paths, headers;
  QStringList modelNames{"test-flash"};
  QString endpoint;
  bool online = true, hold = false;
  int httpStatus = 200, refreshes = 0;
signals:
  void stateChanged();
  void modelsChanged();
};
