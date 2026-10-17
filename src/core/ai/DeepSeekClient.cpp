#include "DeepSeekClient.h"

#include "AiModelSettings.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSslError>
#include <QUuid>

namespace {
bool isIdentifier(const QString &name, int maxLength = 128) {
  static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]+$"));
  return !name.isEmpty() && name.size() <= maxLength &&
         pattern.match(name).hasMatch();
}

bool validCallId(const QString &id) {
  if (id.isEmpty() || id.size() > 256)
    return false;
  for (const QChar character : id) {
    if (character.isSpace() || character.category() == QChar::Other_Control)
      return false;
  }
  return true;
}

QString httpErrorCode(int status) {
  if (status == 401 || status == 403)
    return QStringLiteral("http_auth");
  if (status == 402)
    return QStringLiteral("http_balance");
  if (status == 429)
    return QStringLiteral("http_rate_limit");
  if (status >= 500)
    return QStringLiteral("http_server");
  if (status >= 300 && status < 400)
    return QStringLiteral("http_redirect");
  return QStringLiteral("http_invalid_request");
}

QString httpMessage(int status) {
  if (status == 401 || status == 403)
    return QStringLiteral("鉴权失败，请检查 API Key 和接入权限");
  if (status == 402)
    return QStringLiteral("模型账号余额不足");
  if (status == 429)
    return QStringLiteral("模型请求受到限流，请稍后重试");
  if (status >= 500)
    return QStringLiteral("模型服务暂时不可用，请稍后重试");
  if (status >= 300 && status < 400)
    return QStringLiteral("接入地址返回了重定向，请填写最终 HTTPS 根地址");
  return QStringLiteral("模型服务拒绝请求，请检查接入地址和模型 ID");
}
} // namespace

DeepSeekClient::DeepSeekClient(QNetworkAccessManager *manager, QObject *parent)
    : QObject(parent),
      m_manager(manager ? manager : new QNetworkAccessManager(this)),
      m_timeout(this) {
  m_timeout.setSingleShot(true);
  m_timeout.setInterval(RequestTimeoutMs);
  connect(&m_timeout, &QTimer::timeout, this, [this]() {
    if (m_reply)
      fail(QStringLiteral("timeout"), QStringLiteral("模型请求已超时"));
  });
}

DeepSeekClient::~DeepSeekClient() { releaseReply(true); }

void DeepSeekClient::complete(const QString &requestId, const QUrl &baseUrl,
                              const QByteArray &apiKey,
                              const QJsonObject &body) {
  start(requestId, baseUrl, apiKey, body);
}

void DeepSeekClient::probeConnection(const QString &requestId,
                                     const QUrl &baseUrl,
                                     const QByteArray &apiKey,
                                     const QString &model) {
  const QString nonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QJsonObject function{
      {QStringLiteral("name"), QStringLiteral("connection_probe")},
      {QStringLiteral("description"), QStringLiteral("Return the provided nonce unchanged.")},
      {QStringLiteral("parameters"),
       QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                   {QStringLiteral("properties"),
                    QJsonObject{{QStringLiteral("nonce"),
                                 QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                   {QStringLiteral("required"), QJsonArray{QStringLiteral("nonce")}},
                   {QStringLiteral("additionalProperties"), false}}}};
  start(requestId, baseUrl, apiKey,
        {{QStringLiteral("model"), model},
         {QStringLiteral("messages"),
          QJsonArray{QJsonObject{
              {QStringLiteral("role"), QStringLiteral("user")},
              {QStringLiteral("content"),
               QStringLiteral("Call connection_probe with nonce: ") + nonce}}}},
         {QStringLiteral("tools"),
          QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                                 {QStringLiteral("function"), function}}}},
         {QStringLiteral("tool_choice"),
          QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                       {QStringLiteral("function"),
                        QJsonObject{{QStringLiteral("name"), QStringLiteral("connection_probe")}}}}},
         {QStringLiteral("max_tokens"), 128}},
        nonce);
}

void DeepSeekClient::start(const QString &requestId, const QUrl &baseUrl,
                           const QByteArray &apiKey, QJsonObject body,
                           const QString &probeNonce) {
  if (m_reply) {
    emit failed(requestId, QStringLiteral("busy"), QStringLiteral("已有模型请求正在进行"));
    return;
  }
  if (!m_manager) {
    emit failed(requestId, QStringLiteral("network_error"), QStringLiteral("模型网络管理器不可用"));
    return;
  }
  QUrl normalizedUrl;
  auto checked = AiModelSettings::normalizeBaseUrl(
      QString::fromUtf8(baseUrl.toEncoded()), &normalizedUrl);
  if (checked.ok)
    checked = AiModelSettings::validateApiKey(apiKey);
  if (checked.ok)
    checked = AiModelSettings::validateModel(body.value(QStringLiteral("model")).toString());
  if (!checked.ok) {
    emit failed(requestId, checked.code, checked.message);
    return;
  }
  const auto messages = body.value(QStringLiteral("messages"));
  const auto tokens = body.value(QStringLiteral("max_tokens"));
  if (requestId.isEmpty() || requestId.size() > 128 || !messages.isArray() ||
      messages.toArray().isEmpty() ||
      (!tokens.isUndefined() &&
       (!tokens.isDouble() || tokens.toDouble() != tokens.toInt() ||
        tokens.toInt() < 1 || tokens.toInt() > 4096))) {
    emit failed(requestId, QStringLiteral("invalid_input"), QStringLiteral("模型请求参数无效"));
    return;
  }
  QSet<QString> toolNames;
  const auto toolValue = body.value(QStringLiteral("tools"));
  if (!toolValue.isUndefined() && !toolValue.isArray()) {
    emit failed(requestId, QStringLiteral("invalid_input"), QStringLiteral("模型工具定义无效"));
    return;
  }
  for (const auto &tool : toolValue.toArray()) {
    const auto object = tool.toObject();
    const auto function = object.value(QStringLiteral("function")).toObject();
    const auto name = function.value(QStringLiteral("name")).toString();
    if (!tool.isObject() || object.value(QStringLiteral("type")) != QStringLiteral("function") ||
        !isIdentifier(name) || toolNames.contains(name) ||
        !function.value(QStringLiteral("parameters")).isObject()) {
      emit failed(requestId, QStringLiteral("invalid_input"), QStringLiteral("模型工具定义无效"));
      return;
    }
    toolNames.insert(name);
  }
  body.insert(QStringLiteral("stream"), false);
  body.insert(QStringLiteral("thinking"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("disabled")}});
  if (tokens.isUndefined())
    body.insert(QStringLiteral("max_tokens"), 4096);
  if (!body.contains(QStringLiteral("tool_choice")) && !toolNames.isEmpty())
    body.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
  const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
  if (payload.size() > ResponseLimit) {
    emit failed(requestId, QStringLiteral("request_too_large"), QStringLiteral("模型请求超过大小限制"));
    return;
  }
  normalizedUrl.setPath(normalizedUrl.path() + QStringLiteral("/chat/completions"));
  QNetworkRequest request(normalizedUrl);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + apiKey);
  request.setRawHeader("Accept", "application/json");
  request.setTransferTimeout(RequestTimeoutMs);

  m_requestId = requestId;
  m_response.clear();
  m_toolNames = toolNames;
  m_probeNonce = probeNonce;
  m_reply = m_manager->post(request, payload);
  if (!m_reply) {
    fail(QStringLiteral("network_error"), QStringLiteral("无法创建模型网络请求"));
    return;
  }
  auto *reply = m_reply.data();
  reply->setReadBufferSize(ResponseLimit + 1);
  connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
    if (m_reply == reply)
      readAvailable();
  });
  connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply]() {
    if (m_reply == reply &&
        reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > ResponseLimit)
      fail(QStringLiteral("response_too_large"), QStringLiteral("模型响应超过大小限制"));
  });
  connect(reply, &QNetworkReply::sslErrors, this,
          [this, reply](const QList<QSslError> &) {
            if (m_reply == reply)
              fail(QStringLiteral("tls_error"), QStringLiteral("HTTPS 证书验证失败"));
          });
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    if (m_reply == reply)
      finishReply();
  });
  m_timeout.start();
}

void DeepSeekClient::cancel(const QString &requestId) {
  if (m_reply && m_requestId == requestId)
    fail(QStringLiteral("cancelled"), QStringLiteral("模型请求已取消"));
}

void DeepSeekClient::releaseReply(bool abort) {
  m_timeout.stop();
  auto *reply = m_reply.data();
  m_reply.clear();
  if (reply) {
    disconnect(reply, nullptr, this, nullptr);
    if (abort && !reply->isFinished())
      reply->abort();
    reply->deleteLater();
  }
  m_response.clear();
  m_toolNames.clear();
  m_probeNonce.clear();
  m_requestId.clear();
}

void DeepSeekClient::fail(const QString &code, const QString &safeMessage) {
  const QString id = m_requestId;
  releaseReply(true);
  emit failed(id, code, safeMessage);
}

bool DeepSeekClient::readAvailable() {
  if (!m_reply)
    return false;
  const qsizetype remaining = ResponseLimit + 1 - m_response.size();
  m_response += m_reply->read(remaining);
  if (m_response.size() > ResponseLimit) {
    fail(QStringLiteral("response_too_large"), QStringLiteral("模型响应超过大小限制"));
    return false;
  }
  return true;
}

void DeepSeekClient::finishReply() {
  if (!readAvailable())
    return;
  const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (status >= 300) {
    fail(httpErrorCode(status), httpMessage(status));
    return;
  }
  if (m_reply->error() != QNetworkReply::NoError) {
    const auto code = m_reply->error();
    fail(code == QNetworkReply::SslHandshakeFailedError ? QStringLiteral("tls_error")
         : code == QNetworkReply::TimeoutError ? QStringLiteral("timeout")
                                              : QStringLiteral("network_error"),
         QStringLiteral("模型网络请求失败，请检查网络和 HTTPS 接入地址"));
    return;
  }
  if (status != 200) {
    fail(QStringLiteral("invalid_response"), QStringLiteral("模型服务返回了无效 HTTP 状态"));
    return;
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(m_response, &parseError);
  const auto object = document.object();
  const auto choices = object.value(QStringLiteral("choices")).toArray();
  if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
      object.value(QStringLiteral("object")) != QStringLiteral("chat.completion") ||
      !object.value(QStringLiteral("id")).isString() ||
      object.value(QStringLiteral("id")).toString().isEmpty() ||
      choices.size() != 1 || !choices.first().isObject()) {
    fail(QStringLiteral("invalid_response"), QStringLiteral("模型响应不是有效对话对象"));
    return;
  }
  const auto choice = choices.first().toObject();
  const auto reasonValue = choice.value(QStringLiteral("finish_reason"));
  const QString reason = reasonValue.toString();
  if (!reasonValue.isString()) {
    fail(QStringLiteral("invalid_response"), QStringLiteral("模型响应缺少终止原因"));
    return;
  }
  if (reason != QStringLiteral("stop") && reason != QStringLiteral("tool_calls")) {
    fail(reason == QStringLiteral("length") ? QStringLiteral("truncated_response")
         : reason == QStringLiteral("content_filter") ? QStringLiteral("content_filtered")
         : reason == QStringLiteral("aborted") || reason == QStringLiteral("insufficient_system_resource")
             ? QStringLiteral("provider_interrupted")
             : QStringLiteral("invalid_response"),
         QStringLiteral("模型未完整返回结果，请修改需求或稍后重试"));
    return;
  }
  const auto index = choice.value(QStringLiteral("index"));
  const auto messageValue = choice.value(QStringLiteral("message"));
  const auto message = messageValue.toObject();
  const auto content = message.value(QStringLiteral("content"));
  const auto reasoning = message.value(QStringLiteral("reasoning_content"));
  const auto callsValue = message.value(QStringLiteral("tool_calls"));
  const auto calls = callsValue.toArray();
  if (!index.isDouble() || index.toDouble() != 0 || !messageValue.isObject() ||
      message.value(QStringLiteral("role")) != QStringLiteral("assistant") ||
      (!content.isString() && !content.isNull()) ||
      (!reasoning.isUndefined() && !reasoning.isString() && !reasoning.isNull()) ||
      (!callsValue.isUndefined() && !callsValue.isNull() && !callsValue.isArray()) ||
      (reason == QStringLiteral("tool_calls") && (calls.isEmpty() || calls.size() > 30)) ||
      (reason == QStringLiteral("stop") && !calls.isEmpty())) {
    fail(QStringLiteral("invalid_response"), QStringLiteral("模型消息或工具终止状态无效"));
    return;
  }
  QSet<QString> ids;
  for (const auto &callValue : calls) {
    const auto call = callValue.toObject();
    const QString id = call.value(QStringLiteral("id")).toString();
    const auto function = call.value(QStringLiteral("function")).toObject();
    const QString name = function.value(QStringLiteral("name")).toString();
    const auto args = function.value(QStringLiteral("arguments"));
    if (!callValue.isObject() || !validCallId(id) || ids.contains(id) ||
        call.value(QStringLiteral("type")) != QStringLiteral("function") ||
        !isIdentifier(name) || !args.isString() ||
        args.toString().toUtf8().size() > ArgumentsLimit) {
      fail(QStringLiteral("invalid_response"), QStringLiteral("模型工具调用格式无效"));
      return;
    }
    if (!m_toolNames.contains(name)) {
      fail(QStringLiteral("unknown_tool"), QStringLiteral("模型请求了未提供的工具"));
      return;
    }
    const auto argumentDocument = QJsonDocument::fromJson(args.toString().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !argumentDocument.isObject()) {
      fail(QStringLiteral("invalid_arguments"), QStringLiteral("模型工具参数不是有效 JSON 对象"));
      return;
    }
    if (!m_probeNonce.isEmpty() &&
        (calls.size() != 1 || name != QStringLiteral("connection_probe") ||
         argumentDocument.object().size() != 1 ||
         argumentDocument.object().value(QStringLiteral("nonce")) != m_probeNonce)) {
      fail(QStringLiteral("invalid_response"), QStringLiteral("模型未正确响应连接测试工具"));
      return;
    }
    ids.insert(id);
  }
  if (reason == QStringLiteral("stop") &&
      (!m_probeNonce.isEmpty() || content.toString().trimmed().isEmpty())) {
    fail(QStringLiteral("empty_response"), QStringLiteral("模型没有返回可用内容或连接工具调用"));
    return;
  }
  const QString id = m_requestId;
  releaseReply(false);
  emit completed(id, message, reason);
}
