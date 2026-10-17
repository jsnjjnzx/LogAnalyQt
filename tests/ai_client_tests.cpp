#include "core/ai/DeepSeekClient.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSslError>
#include <QTextStream>
#include <functional>
#include <memory>

namespace {
int failures = 0;
void expect(bool condition, const QString &message) {
  if (!condition) {
    QTextStream(stderr) << "FAIL: " << message << '\n';
    ++failures;
  }
}

QJsonObject toolCall(const QString &id = QStringLiteral("call.1:valid"),
                     const QString &name = QStringLiteral("list_services"),
                     const QString &arguments = QStringLiteral("{}")) {
  return {{QStringLiteral("id"), id}, {QStringLiteral("type"), QStringLiteral("function")},
          {QStringLiteral("function"),
           QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("arguments"), arguments}}}};
}

QJsonObject envelope(const QString &reason = QStringLiteral("tool_calls"),
                     const QJsonArray &calls = QJsonArray{toolCall()},
                     const QJsonValue &content = QJsonValue(QJsonValue::Null)) {
  QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")},
                       {QStringLiteral("content"), content},
                       {QStringLiteral("reasoning_content"), QStringLiteral("not for display")}};
  if (!calls.isEmpty())
    message.insert(QStringLiteral("tool_calls"), calls);
  return {{QStringLiteral("id"), QStringLiteral("provider-id")},
          {QStringLiteral("object"), QStringLiteral("chat.completion")},
          {QStringLiteral("choices"),
           QJsonArray{QJsonObject{{QStringLiteral("index"), 0},
                                  {QStringLiteral("finish_reason"), reason},
                                  {QStringLiteral("message"), message}}}}};
}

QJsonObject requestBody() {
  return {{QStringLiteral("model"), QStringLiteral("deepseek-flash")},
          {QStringLiteral("messages"),
           QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                  {QStringLiteral("content"), QStringLiteral("Read an attribute")}}}},
          {QStringLiteral("tools"),
           QJsonArray{QJsonObject{
               {QStringLiteral("type"), QStringLiteral("function")},
               {QStringLiteral("function"),
                QJsonObject{{QStringLiteral("name"), QStringLiteral("list_services")},
                             {QStringLiteral("parameters"), QJsonObject{}}}}}}}};
}

struct Response {
  int status = 200;
  QByteArray body = QJsonDocument(envelope()).toJson(QJsonDocument::Compact);
  QNetworkReply::NetworkError error = QNetworkReply::NoError;
  bool hold = false;
  bool oversizedHeader = false;
  bool tlsError = false;
  std::shared_ptr<bool> aborted = std::make_shared<bool>(false);
};

class FakeReply : public QNetworkReply {
public:
  FakeReply(const QNetworkRequest &request, Response response, QObject *parent)
      : QNetworkReply(parent), m_response(std::move(response)) {
    setRequest(request);
    setUrl(request.url());
    setOperation(QNetworkAccessManager::PostOperation);
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    if (!m_response.hold)
      QTimer::singleShot(0, this, [this]() { deliver(); });
  }
  void abort() override {
    *m_response.aborted = true;
    setError(OperationCanceledError, QStringLiteral("fake abort"));
    setFinished(true);
    emit finished();
  }
  void deliver() {
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, m_response.status);
    setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (m_response.oversizedHeader)
      setHeader(QNetworkRequest::ContentLengthHeader, DeepSeekClient::ResponseLimit + 1);
    emit metaDataChanged();
    if (m_response.tlsError)
      emit sslErrors({QSslError(QSslError::SelfSignedCertificate)});
    if (isFinished())
      return;
    if (m_response.error != NoError)
      setError(m_response.error, QStringLiteral("raw error must not leak fake-secret"));
    emit readyRead();
    if (isFinished())
      return;
    setFinished(true);
    emit finished();
  }
  qint64 bytesAvailable() const override {
    return m_response.body.size() - m_offset + QIODevice::bytesAvailable();
  }

protected:
  qint64 readData(char *data, qint64 maximum) override {
    const qint64 count = qMin(maximum, static_cast<qint64>(m_response.body.size()) - m_offset);
    if (count <= 0)
      return -1;
    memcpy(data, m_response.body.constData() + m_offset, count);
    m_offset += count;
    return count;
  }

private:
  Response m_response;
  qint64 m_offset = 0;
};

class FakeManager : public QNetworkAccessManager {
public:
  Response response;
  int requests = 0;
  QNetworkRequest lastRequest;
  QJsonObject lastBody;
  QPointer<FakeReply> lastReply;
  std::function<Response(const QJsonObject &)> respond;

protected:
  QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                QIODevice *outgoing) override {
    expect(operation == PostOperation, QStringLiteral("仅创建 POST"));
    ++requests;
    lastRequest = request;
    lastBody = QJsonDocument::fromJson(outgoing->readAll()).object();
    lastReply = new FakeReply(request, respond ? respond(lastBody) : response, this);
    return lastReply;
  }
};

struct Capture {
  int completed = 0;
  int failed = 0;
  QString id;
  QString code;
  QString text;
  QJsonObject message;
  void attach(DeepSeekClient &client) {
    QObject::connect(&client, &DeepSeekClient::completed, &client,
                     [this](const QString &requestId, const QJsonObject &value, const QString &) {
                       ++completed;
                       id = requestId;
                       message = value;
                     });
    QObject::connect(&client, &DeepSeekClient::failed, &client,
                     [this](const QString &requestId, const QString &errorCode, const QString &message) {
                       ++failed;
                       id = requestId;
                       code = errorCode;
                       text = message;
                     });
  }
  void wait() const {
    QElapsedTimer elapsed;
    elapsed.start();
    while (completed + failed == 0 && elapsed.elapsed() < 500)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expect(completed + failed != 0, QStringLiteral("模型请求在隔离事件循环结束"));
  }
};

void testRequestAndTools() {
  FakeManager manager;
  manager.response.body = QJsonDocument(envelope(
      QStringLiteral("tool_calls"), QJsonArray{toolCall(), toolCall(QStringLiteral("call-2"))})).toJson();
  DeepSeekClient client(&manager);
  Capture capture;
  capture.attach(client);
  auto body = requestBody();
  body.insert(QStringLiteral("stream"), true);
  body.insert(QStringLiteral("thinking"), QJsonObject{{QStringLiteral("type"), QStringLiteral("enabled")}});
  client.complete(QStringLiteral("local-id"), QUrl(QStringLiteral("https://api.deepseek.com/v1/")), "fake-secret", body);
  capture.wait();
  expect(capture.completed == 1 && capture.id == QStringLiteral("local-id") &&
             capture.message.value(QStringLiteral("tool_calls")).toArray().size() == 2,
         QStringLiteral("null content、多工具与本地 requestId 保留"));
  expect(capture.message.value(QStringLiteral("reasoning_content")).toString() == QStringLiteral("not for display"),
         QStringLiteral("assistant message 包括 reasoning 原样保留供后续上下文"));
  expect(manager.lastRequest.url().toString() == QStringLiteral("https://api.deepseek.com/v1/chat/completions"),
         QStringLiteral("只拼一次 chat/completions"));
  expect(manager.lastBody.value(QStringLiteral("stream")) == false &&
             manager.lastBody.value(QStringLiteral("thinking")).toObject().value(QStringLiteral("type")) == QStringLiteral("disabled"),
         QStringLiteral("强制非流式和显式关闭 thinking"));
  expect(manager.lastBody.value(QStringLiteral("max_tokens")) == 4096 &&
             manager.lastRequest.rawHeader("Authorization") == "Bearer fake-secret" &&
             manager.lastRequest.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::ManualRedirectPolicy,
         QStringLiteral("限制输出、Bearer 与禁止自动重定向"));
}

void testResponseFailures() {
  auto run = [](const Response &response, const QString &expected) {
    FakeManager manager;
    manager.response = response;
    DeepSeekClient client(&manager);
    Capture capture;
    capture.attach(client);
    client.complete(QStringLiteral("failure-id"), QUrl(QStringLiteral("https://api.deepseek.com")), "fake-secret", requestBody());
    capture.wait();
    expect(capture.failed == 1 && capture.completed == 0 && capture.code == expected,
           QStringLiteral("拒绝响应: ") + expected + QStringLiteral(" actual=") + capture.code);
    expect(!capture.text.contains(QStringLiteral("fake-secret")) &&
               !capture.text.contains(QStringLiteral("raw error")),
           QStringLiteral("网络失败不展示 Key 或原始错误正文"));
  };
  for (const auto &entry : {
           qMakePair(401, QStringLiteral("http_auth")), qMakePair(402, QStringLiteral("http_balance")),
           qMakePair(429, QStringLiteral("http_rate_limit")), qMakePair(503, QStringLiteral("http_server")),
           qMakePair(422, QStringLiteral("http_invalid_request")), qMakePair(302, QStringLiteral("http_redirect"))}) {
    Response response;
    response.status = entry.first;
    response.body = "raw fake-secret must never leak";
    run(response, entry.second);
  }
  for (const auto &entry : {
           qMakePair(QStringLiteral("length"), QStringLiteral("truncated_response")),
           qMakePair(QStringLiteral("content_filter"), QStringLiteral("content_filtered")),
           qMakePair(QStringLiteral("aborted"), QStringLiteral("provider_interrupted")),
           qMakePair(QStringLiteral("insufficient_system_resource"), QStringLiteral("provider_interrupted")),
           qMakePair(QStringLiteral("unknown"), QStringLiteral("invalid_response"))}) {
    Response response;
    response.body = QJsonDocument(envelope(entry.first)).toJson();
    run(response, entry.second);
  }
  for (const QJsonObject &invalid : {
           QJsonObject{},
           envelope(QStringLiteral("stop")),
           envelope(QStringLiteral("tool_calls"), {}),
           envelope(QStringLiteral("tool_calls"), QJsonArray{toolCall(), toolCall()}),
           envelope(QStringLiteral("tool_calls"), QJsonArray{toolCall(QStringLiteral("x"), QStringLiteral("list_services"), QStringLiteral("[]"))})}) {
    Response response;
    response.body = QJsonDocument(invalid).toJson();
    const auto calls = invalid.value(QStringLiteral("choices")).toArray();
    const bool badArguments = !calls.isEmpty() && response.body.contains("\"[]\"");
    run(response, badArguments ? QStringLiteral("invalid_arguments") : QStringLiteral("invalid_response"));
  }
  Response response;
  response.body = QJsonDocument(envelope(QStringLiteral("tool_calls"),
      QJsonArray{toolCall(QStringLiteral("x"), QStringLiteral("shell"))})).toJson();
  run(response, QStringLiteral("unknown_tool"));
  response.body = "<html>fake-secret</html>";
  run(response, QStringLiteral("invalid_response"));
  response.body = QByteArray(DeepSeekClient::ResponseLimit + 1, ' ');
  run(response, QStringLiteral("response_too_large"));
  response = Response{};
  response.oversizedHeader = true;
  run(response, QStringLiteral("response_too_large"));
  response = Response{};
  response.tlsError = true;
  run(response, QStringLiteral("tls_error"));
  response = Response{};
  response.error = QNetworkReply::HostNotFoundError;
  run(response, QStringLiteral("network_error"));
}

void testCancellationTimeoutAndInputs() {
  FakeManager manager;
  manager.response.hold = true;
  const auto aborted = manager.response.aborted;
  DeepSeekClient client(&manager);
  Capture capture;
  capture.attach(client);
  client.complete(QStringLiteral("first"), QUrl(QStringLiteral("https://api.deepseek.com")), "fake-secret", requestBody());
  client.cancel(QStringLiteral("other"));
  expect(client.isBusy(), QStringLiteral("不能用错误 id 取消"));
  client.complete(QStringLiteral("second"), QUrl(QStringLiteral("https://api.deepseek.com")), "fake-secret", requestBody());
  expect(capture.code == QStringLiteral("busy") && capture.id == QStringLiteral("second") && manager.requests == 1,
         QStringLiteral("忙时拒绝且不替换原请求"));
  client.cancel(QStringLiteral("first"));
  expect(*aborted && !client.isBusy() && capture.code == QStringLiteral("cancelled") && capture.id == QStringLiteral("first"),
         QStringLiteral("取消先使状态失效再 abort"));
  manager.lastReply->deliver();
  expect(capture.completed == 0 && capture.failed == 2, QStringLiteral("取消后的迟到结果不能成功"));

  FakeManager timeoutManager;
  timeoutManager.response.hold = true;
  DeepSeekClient timeoutClient(&timeoutManager);
  Capture timed;
  timed.attach(timeoutClient);
  auto timer = timeoutClient.findChild<QTimer *>(QString(), Qt::FindDirectChildrenOnly);
  expect(timer && timer->interval() == DeepSeekClient::RequestTimeoutMs, QStringLiteral("默认绝对超时为45秒"));
  timer->setInterval(1); // Test-only timer acceleration; production has no timeout bypass API.
  timeoutClient.complete(QStringLiteral("timed"), QUrl(QStringLiteral("https://api.deepseek.com")), "fake-secret", requestBody());
  timed.wait();
  expect(timed.code == QStringLiteral("timeout") && *timeoutManager.response.aborted,
         QStringLiteral("总请求超时主动 abort"));

  FakeManager invalidManager;
  DeepSeekClient invalidClient(&invalidManager);
  Capture invalid;
  invalid.attach(invalidClient);
  invalidClient.complete(QStringLiteral("bad-url"), QUrl(QStringLiteral("http://localhost:8080")), "fake-secret", requestBody());
  expect(invalid.failed == 1 && invalidManager.requests == 0, QStringLiteral("生产校验拒绝 HTTP，不依赖测试开关"));
  invalidClient.complete(QStringLiteral("bad-key"), QUrl(QStringLiteral("https://api.deepseek.com")), "fake\nkey", requestBody());
  expect(invalid.failed == 2 && invalidManager.requests == 0, QStringLiteral("非法 Key 不创建网络请求"));
}

void testProbeAndClarification() {
  FakeManager manager;
  manager.respond = [](const QJsonObject &body) {
    const auto text = body.value(QStringLiteral("messages")).toArray().first().toObject()
                          .value(QStringLiteral("content")).toString();
    const auto nonce = text.section(QLatin1Char(' '), -1);
    Response response;
    response.body = QJsonDocument(envelope(QStringLiteral("tool_calls"),
        QJsonArray{toolCall(QStringLiteral("probe-id"), QStringLiteral("connection_probe"),
                           QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("nonce"), nonce}}).toJson(QJsonDocument::Compact)))})).toJson();
    return response;
  };
  DeepSeekClient client(&manager);
  Capture probe;
  probe.attach(client);
  client.probeConnection(QStringLiteral("connection"), QUrl(QStringLiteral("https://api.deepseek.com")), "temporary-fake-key", QStringLiteral("deepseek-flash"));
  probe.wait();
  expect(probe.completed == 1 && manager.lastBody.value(QStringLiteral("max_tokens")) == 128,
         QStringLiteral("真实连接入口使用小工具随机 nonce 探测（此测试为网络替身）"));
  expect(!QJsonDocument(manager.lastBody).toJson().contains("terminal"), QStringLiteral("连接测试不发送终端"));

  manager.respond = {};
  manager.response.body = QJsonDocument(envelope(QStringLiteral("tool_calls"),
      QJsonArray{toolCall(QStringLiteral("probe-id"), QStringLiteral("connection_probe"), QStringLiteral("{\"nonce\":\"wrong\"}"))})).toJson();
  Capture wrong;
  wrong.attach(client);
  client.probeConnection(QStringLiteral("wrong"), QUrl(QStringLiteral("https://api.deepseek.com")), "temporary-fake-key", QStringLiteral("deepseek-flash"));
  wrong.wait();
  expect(wrong.code == QStringLiteral("invalid_response"), QStringLiteral("HTTP200但错误nonce不能冒充连接成功"));

  manager.response.body = QJsonDocument(envelope(QStringLiteral("stop"), {}, QStringLiteral("请补充属性编号"))).toJson();
  Capture clarification;
  clarification.attach(client);
  client.complete(QStringLiteral("question"), QUrl(QStringLiteral("https://api.deepseek.com")), "fake-secret", requestBody());
  clarification.wait();
  expect(clarification.completed == 1 && clarification.message.value(QStringLiteral("content")) == QStringLiteral("请补充属性编号"),
         QStringLiteral("非空 stop 内容交给控制器澄清，不当作帧"));
}
} // namespace

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  testRequestAndTools();
  testResponseFailures();
  testCancellationTimeoutAndInputs();
  testProbeAndClarification();
  if (!failures)
    QTextStream(stdout) << "AI client tests passed (QNetworkReply substitutes only)\n";
  return failures ? 1 : 0;
}
