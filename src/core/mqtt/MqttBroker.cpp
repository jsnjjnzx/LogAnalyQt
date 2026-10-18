#include "MqttBroker.h"
#include "MqttWorker.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include "../config/CredentialAccess.h"
#include <QHostAddress>
#include <QNetworkInterface>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <mosquitto.h>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
extern const unsigned char mqtt_broker_data[];
extern const size_t mqtt_broker_size;
#endif

namespace {
QString writeFile(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("无法写入 Broker 配置：") + file.errorString();
    return {};
}
}

MqttBroker::MqttBroker(const QString &dataRoot, QObject *parent, const QString &helperOverride)
    : QObject(parent), m_dataRoot(dataRoot), m_helperOverride(helperOverride), m_config(Mqtt::brokerDefaults()) {
    MqttWorker::initializeLibrary();
    m_process.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
#endif
    m_startTimer.setSingleShot(true);
    m_startTimer.setInterval(8000);
    connect(&m_startTimer, &QTimer::timeout, this, [this] {
        if (m_state == "starting") {
            setState("error", QStringLiteral("Broker 启动超时。"));
            m_process.kill();
        }
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &MqttBroker::readOutput);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_state == "stopping") return;
        if (error == QProcess::FailedToStart) {
            m_startTimer.stop();
            setState("error", QStringLiteral("无法启动内置 Broker：") + m_process.errorString());
        }
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int exitCode, QProcess::ExitStatus) {
        m_startTimer.stop();
        readOutput();
        if (!m_output.isEmpty()) { processLine(QString::fromUtf8(m_output)); m_output.clear(); }
        m_clients.clear();
        emit clientsChanged();
        if (m_state == "stopping") setState("stopped", QStringLiteral("已停止"));
        else if (m_state != "error") setState("error", QStringLiteral("Broker 已退出（%1）：%2").arg(exitCode).arg(m_events.isEmpty() ? QStringLiteral("请查看服务事件") : m_events.last()));
        m_runtime.reset();
    });
    refreshInterfaces();
}

MqttBroker::~MqttBroker() {
    m_startTimer.stop();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(2000)) { m_process.kill(); m_process.waitForFinished(1000); }
    }
}

void MqttBroker::setState(const QString &state, const QString &detail) {
    m_state = state;
    m_detail = detail;
    emit stateChanged();
}

QString MqttBroker::configure(const QVariantMap &config) {
    if (active()) return QStringLiteral("请先停止 Broker，再修改服务配置。");
    if (auto error = Mqtt::validateBroker(config); !error.isEmpty()) return error;
    m_config = config;
    emit configurationChanged();
    return {};
}

void MqttBroker::refreshInterfaces() {
    m_interfaces = {QVariantMap{{"address", "0.0.0.0"}, {"label", QStringLiteral("全部 IPv4 网卡 · 0.0.0.0")}},
                    QVariantMap{{"address", "127.0.0.1"}, {"label", QStringLiteral("仅本机 · 127.0.0.1")}}};
    for (const auto &networkInterface : QNetworkInterface::allInterfaces()) {
        if (!(networkInterface.flags() & QNetworkInterface::IsUp) || networkInterface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const auto &entry : networkInterface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            const auto address = entry.ip().toString();
            m_interfaces.append(QVariantMap{{"address", address}, {"label", networkInterface.humanReadableName() + " · " + address}});
        }
    }
    emit interfacesChanged();
    emit configurationChanged();
}

QString MqttBroker::connectionAddress() const {
    auto host = m_config.value("host").toString();
    if (host == "0.0.0.0" || host == "::") {
        QStringList addresses;
        for (const auto &entry : m_interfaces) {
            const auto address = entry.toMap().value("address").toString();
            if (address != "0.0.0.0" && address != "127.0.0.1") addresses.append(address + ":" + m_config.value("port").toString());
        }
        return addresses.isEmpty() ? QStringLiteral("暂无局域网 IP；仅本机可用") : addresses.join(" / ");
    }
    if (host.contains(':')) host = '[' + host + ']';
    return host + ':' + m_config.value("port").toString();
}

QVariantList MqttBroker::clients() const {
    QVariantList result;
    for (auto it = m_clients.cbegin(); it != m_clients.cend(); ++it) result.append(it.value());
    return result;
}

QString MqttBroker::helperPath(QString *error) {
    if (!m_helperOverride.isEmpty()) return m_helperOverride;
#ifdef Q_OS_WIN
    const auto bytes = QByteArray::fromRawData(reinterpret_cast<const char *>(mqtt_broker_data), mqtt_broker_size);
    const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    const auto directory = QDir(m_dataRoot).filePath("mqtt/runtime/" + QString::fromLatin1(hash.left(16)));
    if (!QDir().mkpath(directory)) { *error = QStringLiteral("无法创建内置 Broker 目录。"); return {}; }
    const auto path = QDir(directory).filePath("LogAnalyQtBroker.exe");
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && existing.size() == bytes.size()
        && QCryptographicHash::hash(existing.readAll(), QCryptographicHash::Sha256).toHex() == hash) return path;
    existing.close();
    *error = writeFile(path, bytes);
    return error->isEmpty() ? path : QString();
#elif defined(Q_OS_MACOS)
    return QDir(QCoreApplication::applicationDirPath()).filePath("../Helpers/LogAnalyQtBroker");
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath("LogAnalyQtBroker");
#endif
}

QString MqttBroker::prepareRuntime() {
    const auto directory = QDir(m_dataRoot).filePath("mqtt");
    if (!QDir().mkpath(directory)) return QStringLiteral("无法创建 MQTT 运行目录。");
    m_runtime = std::make_unique<QTemporaryDir>(QDir(directory).filePath("run-XXXXXX"));
    if (!m_runtime->isValid()) return QStringLiteral("无法创建 Broker 临时配置目录。");
    QByteArray config = "persistence false\nlog_dest stdout\nlog_timestamp false\nlog_type error\nlog_type warning\nlog_type notice\nlog_type information\nconnection_messages true\n";
    config += "max_packet_size 2097152\nmessage_size_limit 1048576\nmax_queued_bytes 8388608\nmax_queued_messages 1000\n";
    config += "listener " + QByteArray::number(m_config.value("port").toInt()) + ' ' + m_config.value("host").toString().toUtf8() + '\n';
    config += "max_connections 100\nallow_anonymous " + QByteArray(m_config.value("anonymous").toBool() ? "true\n" : "false\n");
    if (!m_config.value("anonymous").toBool()) {
        mosquitto_pw *password = nullptr;
        int rc = mosquitto_pw_new(&password, MOSQ_PW_SHA512_PBKDF2);
        if (!rc) rc = mosquitto_pw_hash_encoded(password, m_config.value("password").toString().toUtf8().constData());
        const QByteArray encoded = !rc ? QByteArray(mosquitto_pw_get_encoded(password)) : QByteArray();
        mosquitto_pw_cleanup(password);
        if (rc || encoded.isEmpty()) return QStringLiteral("无法生成 Broker 认证文件。");
        const auto content = m_config.value("username").toString().toUtf8() + ':' + encoded + '\n';
        if (auto error = writeFile(m_runtime->filePath("users.txt"), content); !error.isEmpty()) return error;
        config += "password_file users.txt\n";
    }
    if (m_config.value("tls").toBool()) {
        for (const auto &pair : {qMakePair("certFile", "server.pem"), qMakePair("keyFile", "server.key")}) {
            QFile source(m_config.value(QLatin1String(pair.first)).toString());
            if (!source.open(QIODevice::ReadOnly)) return QStringLiteral("无法读取 Broker 证书：") + source.errorString();
            if (source.size() > Mqtt::MaxPayloadBytes) return QStringLiteral("证书文件过大。");
            if (auto error = writeFile(m_runtime->filePath(QLatin1String(pair.second)), source.readAll()); !error.isEmpty()) return error;
        }
        config += "certfile server.pem\nkeyfile server.key\n";
    }
    return writeFile(m_runtime->filePath("broker.conf"), config);
}

QVariantMap MqttBroker::start() {
    if (active() || m_process.state() != QProcess::NotRunning) return Mqtt::result(QStringLiteral("Broker 已在运行或正在停止。"));
    QString credentialError;
    if (!CredentialAccess::loadMqtt(&m_config, &credentialError)) return Mqtt::result(credentialError);
    if (auto error = Mqtt::validateBroker(m_config, true); !error.isEmpty()) return Mqtt::result(error);
    QString error;
    const auto helper = helperPath(&error);
    if (error.isEmpty() && !QFileInfo(helper).isExecutable()) error = QStringLiteral("内置 Broker 缺失或不可执行，请检查安装包。");
    if (error.isEmpty()) error = prepareRuntime();
    if (!error.isEmpty()) { m_runtime.reset(); setState("error", error); return Mqtt::result(error); }
    ++m_run;
    m_output.clear();
    m_events.clear();
    m_clients.clear();
    emit eventsChanged();
    emit clientsChanged();
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LOGANALYQT_BROKER_MANAGED", "1");
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(m_runtime->path());
    setState("starting", QStringLiteral("正在启动本机 Broker"));
    m_startTimer.start();
    m_process.start(QFileInfo(helper).absoluteFilePath(), {"-c", "broker.conf"});
    return Mqtt::result();
}

void MqttBroker::stop() {
    if (m_process.state() == QProcess::NotRunning) { setState("stopped", QStringLiteral("已停止")); return; }
    m_startTimer.stop();
    setState("stopping", QStringLiteral("正在停止 Broker"));
    m_process.closeWriteChannel();
    const auto run = m_run;
    QTimer::singleShot(2000, this, [this, run] {
        if (run == m_run && m_process.state() != QProcess::NotRunning) m_process.kill();
    });
}

void MqttBroker::readOutput() {
    m_output += m_process.readAllStandardOutput();
    if (m_output.size() > 256 * 1024) m_output = m_output.right(256 * 1024);
    int newline;
    while ((newline = m_output.indexOf('\n')) >= 0) {
        const auto line = QString::fromUtf8(m_output.left(newline)).trimmed();
        m_output.remove(0, newline + 1);
        if (!line.isEmpty()) processLine(line.left(4096));
    }
}

void MqttBroker::processLine(const QString &line) {
    m_events.append(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + line);
    while (m_events.size() > 500) m_events.removeFirst();
    emit eventsChanged();
    if (m_state == "starting" && line.startsWith("mosquitto version ") && line.endsWith(" running")) {
        m_startTimer.stop();
        setState("running", QStringLiteral("Broker 运行中"));
    }
    // shortcut: inventory uses Mosquitto 2.1.2 events; update parser/tests when upgrading the broker.
    static const QRegularExpression joined(QStringLiteral("^New client connected from (.+):([0-9]+) as (.+) \\(p[0-9]+,"));
    static const QRegularExpression left(QStringLiteral("^Client (.+) \\[(.+):([0-9]+)\\] disconnected"));
    const auto join = joined.match(line);
    const auto leave = left.match(line);
    if (join.hasMatch()) {
        const auto id = join.captured(3);
        m_clients.insert(id, {{"clientId", id}, {"address", join.captured(1)}, {"port", join.captured(2).toInt()},
            {"connectedAt", QDateTime::currentDateTime().toString("HH:mm:ss")}, {"local", id == m_localClientId}});
        emit clientsChanged();
    } else if (leave.hasMatch()) {
        const auto client = m_clients.value(leave.captured(1));
        if (client.value("address").toString() == leave.captured(2) && client.value("port").toInt() == leave.captured(3).toInt()) {
            m_clients.remove(leave.captured(1));
            emit clientsChanged();
        }
    }
}

QVariantMap MqttBroker::localClientConfiguration(const QVariantMap &previous) {
    auto config = previous;
    m_localClientId = config.value("clientId").toString();
    config.insert("name", QStringLiteral("本机测试客户端"));
    auto host = m_config.value("host").toString();
    if (host == "0.0.0.0") host = "127.0.0.1";
    else if (host == "::") host = "::1";
    config.insert("host", host);
    config.insert("port", m_config.value("port"));
    config.insert("tls", m_config.value("tls"));
    config.insert("caFile", m_config.value("caFile").toString().isEmpty() ? m_config.value("certFile") : m_config.value("caFile"));
    config.insert("username", m_config.value("anonymous").toBool() ? QVariant("") : m_config.value("username"));
    config.insert("password", m_config.value("anonymous").toBool() ? QVariant("") : m_config.value("password"));
    config.insert("autoReconnect", true);
    return config;
}
