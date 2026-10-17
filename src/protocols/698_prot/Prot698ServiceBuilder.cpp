#include "Prot698ServiceBuilder.h"
#include "Prot698DataEncoder.h"
#include "Prot698Def.h"
#include <cmath>
#include <QMetaType>

QByteArray Prot698ServiceBuilder::bytesFromHex(const QString &hex, QString *err,
                                               int expectedBytes) {
  QString cleanHex = hex;
  cleanHex.remove("0x", Qt::CaseInsensitive);
  cleanHex.remove(' ');
  cleanHex.remove('\n');
  cleanHex.remove('\r');
  cleanHex.remove('\t');
  cleanHex.remove(':');
  if (cleanHex.size() % 2 != 0) {
    if (err)
      *err = "十六进制字符串长度必须为偶数";
    return {};
  }
  QByteArray bytes = QByteArray::fromHex(cleanHex.toLatin1());
  if (bytes.size() * 2 != cleanHex.size()) {
    if (err)
      *err = "十六进制字符串包含非法字符";
    return {};
  }
  if (expectedBytes >= 0 && bytes.size() != expectedBytes) {
    if (err)
      *err = QString("需要 %1 字节，实际 %2 字节")
                 .arg(expectedBytes)
                 .arg(bytes.size());
    return {};
  }
  return bytes;
}

void Prot698ServiceBuilder::appendUInt16(QByteArray &bytes, int value) {
  bytes.append(static_cast<char>((value >> 8) & 0xFF));
  bytes.append(static_cast<char>(value & 0xFF));
}

QByteArray Prot698ServiceBuilder::buildPiid(const QVariantMap &piid) {
  int seqNo = piid.value("seqNo", 1).toInt();
  bool highPriority = piid.value("highPriority", false).toBool();
  return Prot698DataEncoder::encodePIID(seqNo, highPriority);
}

QByteArray Prot698ServiceBuilder::buildPiidAcd(const QVariantMap &piid) {
  QByteArray bytes = buildPiid(piid);
  if (!bytes.isEmpty() && piid.value("acd", false).toBool()) {
    quint8 value = static_cast<quint8>(bytes.at(0));
    bytes[0] = static_cast<char>(value | 0x40);
  }
  return bytes;
}

QByteArray Prot698ServiceBuilder::buildTimeTag(const QVariantMap &timeTag,
                                               QString *err) {
  bool hasTimeTag = timeTag.value("hasTimeTag", false).toBool();
  QByteArray bytes;
  if (!hasTimeTag) {
    bytes.append(static_cast<char>(0x00));
    return bytes;
  }
  bytes.append(static_cast<char>(0x01));
  QByteArray timeBytes = Prot698DataEncoder::encodeDateTimeS(timeTag, err);
  if (timeBytes.isEmpty())
    return {};
  bytes.append(timeBytes);
  bytes.append(Prot698DataEncoder::encodeTI(timeTag.value("tiUnit", 0).toInt(),
                                            timeTag.value("tiValue", 0).toInt(),
                                            err));
  return bytes;
}

QByteArray Prot698ServiceBuilder::buildFromSpec(const QVariantMap &serviceSpec,
                                                QString *err) {
  if (err) err->clear();
  const auto reject = [&](const QString &message) { if (err) *err = message; return QByteArray{}; };
  const auto validInteger = [&](const QVariant &value, int maximum) {
    if (value.metaType().id() == QMetaType::Bool || value.metaType().id() == QMetaType::QString || !value.canConvert<double>()) return false;
    const double number = value.toDouble();
    return std::isfinite(number) && std::floor(number) == number && number >= 0 && number <= maximum;
  };
  QString serviceCode = serviceSpec.value("serviceCode", serviceSpec.value("service"))
                            .toString()
                            .toUpper();
  QVariantMap piid = serviceSpec.value("piid").toMap();
  QVariantMap timeTag = serviceSpec.value("timeTag").toMap();
  QByteArray apdu;
  if (piid.contains("seqNo") && !validInteger(piid.value("seqNo"), 63)) return reject("piid.seqNo: 必须为0..63整数");
  for (const auto &key : {"highPriority", "acd"}) if (piid.contains(key) && piid.value(key).metaType().id() != QMetaType::Bool) return reject(QString("piid.") + key + ": 必须为bool");
  for (const auto &key : {"lastBlockNo", "blockNo", "totalTimeout"}) if (serviceSpec.contains(key) && !validInteger(serviceSpec.value(key), 65535)) return reject(QString(key) + ": 必须为0..65535整数");
  if (timeTag.contains("hasTimeTag") && timeTag.value("hasTimeTag").metaType().id() != QMetaType::Bool) return reject("timeTag.hasTimeTag: 必须为bool");

  auto appendTimeTag = [&]() -> bool {
    if (err && !err->isEmpty())
      return false;
    if (serviceCode.contains("RESPONSE")) apdu.append(char(0)); // absent FollowReport precedes TimeTag
    QByteArray timeTagBytes = buildTimeTag(timeTag, err);
    if (err && !err->isEmpty()) return false;
    if (timeTagBytes.isEmpty() && timeTag.value("hasTimeTag", false).toBool())
      return false;
    apdu.append(timeTagBytes);
    return true;
  };

  auto appendHexList = [&](const QVariantList &items, const QString &name) -> bool {
    if (items.isEmpty() || items.size() > 255) {
      if (err)
        *err = QString("%1 至少需要 1 项，最多 255 项").arg(name);
      return false;
    }
    apdu.append(static_cast<char>(items.size()));
    for (const QVariant &itemValue : items) {
      apdu.append(bytesFromHex(itemValue.toString(), err));
      if (err && !err->isEmpty())
        return false;
    }
    return true;
  };

  if (serviceCode == "GET_NORMAL") {
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::GetRequest)));
    apdu.append(static_cast<char>(0x01));
    apdu.append(buildPiid(piid));
    apdu.append(bytesFromHex(serviceSpec.value("oad").toString(), err, 4));
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "GET_LIST") {
    QVariantList oads = serviceSpec.value("oads").toList();
    if (oads.isEmpty() || oads.size() > 255) {
      if (err)
        *err = "GET_LIST 至少需要 1 个 OAD，最多 255 个";
      return {};
    }
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::GetRequest)));
    apdu.append(static_cast<char>(0x02));
    apdu.append(buildPiid(piid));
    apdu.append(static_cast<char>(oads.size()));
    for (const QVariant &oadValue : oads)
      apdu.append(bytesFromHex(oadValue.toString(), err, 4));
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "GET_RECORD") {
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::GetRequest)));
    apdu.append(static_cast<char>(0x03));
    apdu.append(buildPiid(piid));
    apdu.append(bytesFromHex(serviceSpec.value("oad").toString(), err, 4));
    apdu.append(bytesFromHex(serviceSpec.value("rsdHex", "00").toString(), err));
    apdu.append(bytesFromHex(serviceSpec.value("rcsdHex", "00").toString(), err));
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "GET_NEXT") {
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::GetRequest)));
    apdu.append(static_cast<char>(0x05));
    apdu.append(buildPiid(piid));
    appendUInt16(apdu, serviceSpec.value("lastBlockNo", 0).toInt());
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "SET_NORMAL") {
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::SetRequest)));
    apdu.append(static_cast<char>(0x01));
    apdu.append(buildPiid(piid));
    apdu.append(bytesFromHex(serviceSpec.value("oad").toString(), err, 4));
    apdu.append(bytesFromHex(serviceSpec.value("dataHex").toString(), err));
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "SET_LIST") {
    QVariantList items = serviceSpec.value("items").toList();
    if (items.isEmpty() || items.size() > 255) {
      if (err)
        *err = "SET_LIST 至少需要 1 个写入项，最多 255 个";
      return {};
    }
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::SetRequest)));
    apdu.append(static_cast<char>(0x02));
    apdu.append(buildPiid(piid));
    apdu.append(static_cast<char>(items.size()));
    for (const QVariant &itemValue : items) {
      QVariantMap item = itemValue.toMap();
      apdu.append(bytesFromHex(item.value("oad").toString(), err, 4));
      apdu.append(bytesFromHex(item.value("dataHex").toString(), err));
    }
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "ACTION_NORMAL") {
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::ActionRequest)));
    apdu.append(static_cast<char>(0x01));
    apdu.append(buildPiid(piid));
    apdu.append(bytesFromHex(serviceSpec.value("omd").toString(), err, 4));
    apdu.append(bytesFromHex(serviceSpec.value("dataHex", "00").toString(), err));
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "ACTION_LIST") {
    QVariantList items = serviceSpec.value("items").toList();
    if (items.isEmpty() || items.size() > 255) {
      if (err)
        *err = "ACTION_LIST 至少需要 1 个方法项，最多 255 个";
      return {};
    }
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::ActionRequest)));
    apdu.append(static_cast<char>(0x02));
    apdu.append(buildPiid(piid));
    apdu.append(static_cast<char>(items.size()));
    for (const QVariant &itemValue : items) {
      QVariantMap item = itemValue.toMap();
      apdu.append(bytesFromHex(item.value("omd").toString(), err, 4));
      apdu.append(bytesFromHex(item.value("dataHex", "00").toString(), err));
    }
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "PROXY_GET_LIST") {
    QVariantList servers = serviceSpec.value("servers").toList();
    if (servers.isEmpty() || servers.size() > 255) {
      if (err)
        *err = "PROXY_GET_LIST 至少需要 1 个目标服务器，最多 255 个";
      return {};
    }
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::ProxyRequest)));
    apdu.append(static_cast<char>(0x01));
    apdu.append(buildPiid(piid));
    appendUInt16(apdu, serviceSpec.value("totalTimeout", 10).toInt());
    apdu.append(static_cast<char>(servers.size()));
    for (const QVariant &serverValue : servers) {
      QVariantMap server = serverValue.toMap();
      QByteArray tsa = bytesFromHex(server.value("tsaHex").toString(), err);
      apdu.append(Prot698DataEncoder::encodeLength(tsa.size()));
      apdu.append(tsa);
      if (!validInteger(server.value("timeout", 5), 65535)) return reject("servers.timeout: 必须为0..65535整数");
      appendUInt16(apdu, server.value("timeout", 5).toInt());
      QVariantList oads = server.value("oads").toList();
      if (oads.isEmpty() || oads.size() > 255) {
        if (err)
          *err = "代理目标服务器至少需要 1 个 OAD，最多 255 个";
        return {};
      }
      apdu.append(static_cast<char>(oads.size()));
      for (const QVariant &oadValue : oads)
        apdu.append(bytesFromHex(oadValue.toString(), err, 4));
    }
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "GET_RESPONSE_NORMAL") {
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::GetResponse)));
    apdu.append(static_cast<char>(0x01));
    apdu.append(buildPiidAcd(piid));
    apdu.append(bytesFromHex(serviceSpec.value("resultHex").toString(), err));
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "GET_RESPONSE_LIST") {
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::GetResponse)));
    apdu.append(static_cast<char>(0x02));
    apdu.append(buildPiidAcd(piid));
    if (!appendHexList(serviceSpec.value("results").toList(), "GET_RESPONSE_LIST"))
      return {};
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "GET_RESPONSE_RECORD") {
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::GetResponse)));
    apdu.append(static_cast<char>(0x03));
    apdu.append(buildPiidAcd(piid));
    apdu.append(bytesFromHex(serviceSpec.value("recordHex").toString(), err));
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "GET_RESPONSE_NEXT") {
    int choice = serviceSpec.value("choice", 0).toInt();
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::GetResponse)));
    apdu.append(static_cast<char>(0x05));
    apdu.append(buildPiidAcd(piid));
    apdu.append(static_cast<char>(serviceSpec.value("end", true).toBool() ? 0x01 : 0x00));
    appendUInt16(apdu, serviceSpec.value("blockNo", 0).toInt());
    apdu.append(static_cast<char>(choice & 0xFF));
    if (choice == 0) {
      int dar = serviceSpec.value("dar", 0).toInt();
      if (dar < 0 || dar > 0xFF) {
        if (err)
          *err = "DAR 必须在 0-255 范围内";
        return {};
      }
      apdu.append(static_cast<char>(dar & 0xFF));
    } else if (choice == 1) {
      if (!appendHexList(serviceSpec.value("normalResults").toList(), "GET_RESPONSE_NEXT 普通结果列表"))
        return {};
    } else if (choice == 2) {
      if (!appendHexList(serviceSpec.value("recordResults").toList(), "GET_RESPONSE_NEXT 记录结果列表"))
        return {};
    } else {
      if (err)
        *err = "GET_RESPONSE_NEXT 的结果选择只能是 DAR、普通结果列表或记录结果列表";
      return {};
    }
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "SET_RESPONSE_NORMAL") {
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::SetResponse)));
    apdu.append(static_cast<char>(0x01));
    apdu.append(buildPiidAcd(piid));
    apdu.append(bytesFromHex(serviceSpec.value("resultHex").toString(), err));
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "SET_RESPONSE_LIST") {
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::SetResponse)));
    apdu.append(static_cast<char>(0x02));
    apdu.append(buildPiidAcd(piid));
    if (!appendHexList(serviceSpec.value("results").toList(), "SET_RESPONSE_LIST"))
      return {};
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "ACTION_RESPONSE_NORMAL") {
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::ActionResponse)));
    apdu.append(static_cast<char>(0x01));
    apdu.append(buildPiidAcd(piid));
    apdu.append(bytesFromHex(serviceSpec.value("resultHex").toString(), err));
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "ACTION_RESPONSE_LIST") {
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::ActionResponse)));
    apdu.append(static_cast<char>(0x02));
    apdu.append(buildPiidAcd(piid));
    if (!appendHexList(serviceSpec.value("results").toList(), "ACTION_RESPONSE_LIST"))
      return {};
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (serviceCode == "PROXY_GET_RESPONSE_LIST") {
    QVariantList servers = serviceSpec.value("servers").toList();
    if (servers.isEmpty() || servers.size() > 255) {
      if (err)
        *err = "PROXY_GET_RESPONSE_LIST 至少需要 1 个目标服务器，最多 255 个";
      return {};
    }
    apdu.append(static_cast<char>(static_cast<quint8>(Prot698ApduTag::ProxyResponse)));
    apdu.append(static_cast<char>(0x01));
    apdu.append(buildPiidAcd(piid));
    apdu.append(static_cast<char>(servers.size()));
    for (const QVariant &serverValue : servers) {
      QVariantMap server = serverValue.toMap();
      QByteArray tsa = bytesFromHex(server.value("tsaHex").toString(), err);
      if (err && !err->isEmpty())
        return {};
      apdu.append(Prot698DataEncoder::encodeLength(tsa.size()));
      apdu.append(tsa);
      if (!appendHexList(server.value("results").toList(), "代理读取结果列表"))
        return {};
    }
    return appendTimeTag() ? apdu : QByteArray();
  }

  if (err)
    *err = QString("暂不支持的 APDU 服务：%1").arg(serviceCode);
  return {};
}