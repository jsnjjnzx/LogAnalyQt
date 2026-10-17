#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSet>
#include <QTimer>

class QNetworkReply;

class DeepSeekClient : public QObject {
  Q_OBJECT
public:
  static constexpr int RequestTimeoutMs = 45000;
  static constexpr qsizetype ResponseLimit = 2 * 1024 * 1024;
  static constexpr qsizetype ArgumentsLimit = 128 * 1024;
  explicit DeepSeekClient(QNetworkAccessManager *manager = nullptr,
                          QObject *parent = nullptr);
  ~DeepSeekClient() override;
  void complete(const QString &requestId, const QUrl &baseUrl,
                const QByteArray &apiKey, const QJsonObject &body);
  void probeConnection(const QString &requestId, const QUrl &baseUrl,
                       const QByteArray &apiKey, const QString &model);
  void cancel(const QString &requestId);
  bool isBusy() const { return !m_reply.isNull(); }

signals:
  void completed(QString requestId, QJsonObject message, QString finishReason);
  void failed(QString requestId, QString code, QString safeMessage);

private:
  void start(const QString &requestId, const QUrl &baseUrl,
             const QByteArray &apiKey, QJsonObject body,
             const QString &probeNonce = {});
  bool readAvailable();
  void finishReply();
  void fail(const QString &code, const QString &safeMessage);
  void releaseReply(bool abort);
  QPointer<QNetworkAccessManager> m_manager;
  QPointer<QNetworkReply> m_reply;
  QTimer m_timeout;
  QString m_requestId;
  QByteArray m_response;
  QSet<QString> m_toolNames;
  QString m_probeNonce;
};
