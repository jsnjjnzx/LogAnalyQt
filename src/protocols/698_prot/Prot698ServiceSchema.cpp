#include "Prot698ServiceSchema.h"
#include "Prot698DataEncoder.h"
#include "Prot698OadManager.h"
#include "Prot698FrameBuilder.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <cmath>
#include <functional>

namespace {
QJsonObject serviceInputSchema(const QString &code, bool aiInput = true);
bool recordColumns(const QVariantMap &rcsd, QList<const Prot698SubItem *> *columns,
                   QString *err, const QString &path) {
  const auto items = rcsd.value("items").toList();
  for (int i = 0; i < items.size(); ++i) {
    const auto item = items.at(i).toMap();
    if (item.value("choice").toInt() != 0) {
      if (err) *err = path + ".items[" + QString::number(i) + "].choice: unsupported_definition: ROAD记录cell的array/structure语义需要目标例帧；普通OAD列和空记录可用";
      return false;
    }
    const auto oad = item.value("oad").toMap();
    auto *definition = Prot698OadManager::instance().getAttrDataDef(
        oad.value("oi").toString().toUpper(), oad.value("attrId").toInt(), oad.value("index").toInt());
    if (!definition) {
      if (err) *err = path + ".items[" + QString::number(i) + "].oad: 找不到RCSD列定义";
      return false;
    }
    columns->append(definition);
  }
  return true;
}
QString tsaFromDisplay(const QJsonObject &tsa, QString *err) {
  const auto options = Prot698FrameBuilder::optionsFromMap({
      {"physicalAddressHex", tsa["address"].toString()},
      {"saType", tsa["addressType"].toInt()}, {"logicAddress", tsa["logicAddress"].toInt()}}, err);
  if (!options.validationError.isEmpty() || options.serverAddr.isEmpty() || options.serverAddr.size() > 16) return {};
  const int indicator = (options.serverAddr.size() - 1) | (options.saType << 6) | (options.logicAddress << 4);
  const QByteArray bytes = QByteArray(1, char(indicator)) + options.serverAddr;
  return QString::fromLatin1(bytes.toHex().toUpper());
}
}

QString Prot698ServiceSchema::normalizeServiceCode(const QString &serviceCode) {
  return serviceCode.trimmed().toUpper();
}

QVariantMap Prot698ServiceSchema::node(const QString &key, const QString &name,
                                       const QString &kind, bool required,
                                       bool repeatable,
                                       const QVariantList &children) {
  QVariantMap item;
  item["key"] = key;
  item["name"] = name;
  item["kind"] = kind;
  item["required"] = required;
  item["repeatable"] = repeatable;
  item["children"] = children;
  return item;
}

QVariantList Prot698ServiceSchema::serviceTypes() {
  auto makeService = [](const QString &code, const QString &name,
                        const QString &group, const QString &category,
                        const QString &subCategory,
                        const QString &direction) {
    QVariantMap item;
    item["code"] = code;
    item["name"] = name;
    item["group"] = group;
    item["category"] = category;
    item["subCategory"] = subCategory;
    item["direction"] = direction;
    item["buildSupported"] = true;
    QVariantList limitations;
    if (code == "GET_RESPONSE_RECORD" || code == "GET_RESPONSE_NEXT")
      limitations.append("非空rows按RCSD普通OAD列编码；ROAD记录cell容器语义待目标例帧，DAR与空rows仍支持");
    if (code == "GET_RECORD") limitations.append("MS type5/6/7原始Hex分支未向AI开放");
    item["unsupportedReasons"] = limitations;
    item["verificationStatus"] = "pending";
    return item;
  };
    return {makeService("GET_NORMAL", "读取单个属性", "客户机请求", "读取", "单个属性", "客户机请求"),
      makeService("GET_LIST", "读取多个属性", "客户机请求", "读取", "多个属性", "客户机请求"),
      makeService("GET_RECORD", "读取记录型数据", "客户机请求", "读取", "记录型数据", "客户机请求"),
      makeService("GET_NEXT", "读取下一块数据", "客户机请求", "读取", "下一块数据", "客户机请求"),
      makeService("SET_NORMAL", "设置单个属性", "客户机请求", "设置", "单个属性", "客户机请求"),
      makeService("SET_LIST", "设置多个属性", "客户机请求", "设置", "多个属性", "客户机请求"),
      makeService("ACTION_NORMAL", "执行单个方法", "客户机请求", "执行", "单个方法", "客户机请求"),
      makeService("ACTION_LIST", "执行多个方法", "客户机请求", "执行", "多个方法", "客户机请求"),
      makeService("PROXY_GET_LIST", "代理读取属性列表", "客户机请求", "代理", "读取属性列表", "客户机请求"),
      makeService("GET_RESPONSE_NORMAL", "读取响应单个属性", "服务器响应", "读取响应", "单个属性", "服务器响应"),
      makeService("GET_RESPONSE_LIST", "读取响应多个属性", "服务器响应", "读取响应", "多个属性", "服务器响应"),
      makeService("GET_RESPONSE_RECORD", "读取响应记录型数据", "服务器响应", "读取响应", "记录型数据", "服务器响应"),
      makeService("GET_RESPONSE_NEXT", "读取响应下一块数据", "服务器响应", "读取响应", "下一块数据", "服务器响应"),
      makeService("SET_RESPONSE_NORMAL", "设置响应单个属性", "服务器响应", "设置响应", "单个属性", "服务器响应"),
      makeService("SET_RESPONSE_LIST", "设置响应多个属性", "服务器响应", "设置响应", "多个属性", "服务器响应"),
      makeService("ACTION_RESPONSE_NORMAL", "执行响应单个方法", "服务器响应", "执行响应", "单个方法", "服务器响应"),
      makeService("ACTION_RESPONSE_LIST", "执行响应多个方法", "服务器响应", "执行响应", "多个方法", "服务器响应"),
      makeService("PROXY_GET_RESPONSE_LIST", "代理读取响应", "服务器响应", "代理响应", "读取结果列表", "服务器响应")};
}

QVariantMap Prot698ServiceSchema::schemaForService(const QString &serviceCode) {
  QString code = normalizeServiceCode(serviceCode);
  bool known = false;
  for (const auto &item : serviceTypes()) if (item.toMap().value("code").toString() == code) known = true;
  if (!known) return {};
  QVariantList children;
  children.append(node("piid", "PIID", "piid"));

  if (code == "GET_NORMAL") {
    children.append(node("oad", "对象属性 OAD", "oad"));
  } else if (code == "GET_LIST") {
    children.append(node("oads", "对象属性列表", "oadList", true, true));
  } else if (code == "GET_RECORD") {
    children.append(node("oad", "记录型对象属性 OAD", "oad"));
    children.append(node("rsd", "记录选择描述符 RSD", "rsd"));
    children.append(node("rcsd", "记录列选择描述符 RCSD", "rcsd"));
  } else if (code == "GET_NEXT") {
    children.append(node("lastBlockNo", "上一块序号", "integer"));
  } else if (code == "SET_NORMAL") {
    children.append(node("setItem", "写入项", "setItem"));
  } else if (code == "SET_LIST") {
    children.append(node("setItems", "写入项列表", "setItemList", true, true));
  } else if (code == "ACTION_NORMAL") {
    children.append(node("actionItem", "方法调用项", "actionItem"));
  } else if (code == "ACTION_LIST") {
    children.append(
        node("actionItems", "方法调用项列表", "actionItemList", true, true));
  } else if (code == "PROXY_GET_LIST") {
    children.append(node("totalTimeout", "代理总超时", "integer"));
    children.append(node("servers", "目标服务器列表", "proxyServerList", true,
                         true));
  } else if (code == "GET_RESPONSE_NORMAL") {
    children.append(node("result", "读取结果", "aResultNormal"));
  } else if (code == "GET_RESPONSE_LIST") {
    children.append(node("results", "读取结果列表", "aResultNormalList", true,
                         true));
  } else if (code == "GET_RESPONSE_RECORD") {
    children.append(node("recordResult", "记录型读取结果", "aResultRecord"));
  } else if (code == "GET_RESPONSE_NEXT") {
    children.append(node("nextResult", "下一块读取结果", "getNextResult"));
  } else if (code == "SET_RESPONSE_NORMAL") {
    children.append(node("setResult", "写入结果", "setResult"));
  } else if (code == "SET_RESPONSE_LIST") {
    children.append(node("setResults", "写入结果列表", "setResultList", true,
                         true));
  } else if (code == "ACTION_RESPONSE_NORMAL") {
    children.append(node("actionResult", "方法执行结果", "actionResult"));
  } else if (code == "ACTION_RESPONSE_LIST") {
    children.append(node("actionResults", "方法执行结果列表", "actionResultList",
                         true, true));
  } else if (code == "PROXY_GET_RESPONSE_LIST") {
    children.append(node("servers", "代理目标服务器结果", "proxyResponseServerList",
                         true, true));
  }

  children.append(node("timeTag", "时间标签", "timeTag", false));

  QVariantMap root;
  root["key"] = code;
  root["name"] = code;
  root["kind"] = "root";
  root["children"] = children;
  root["inputSchema"] = serviceInputSchema(code).toVariantMap();
  for (const auto &service : serviceTypes()) if (service.toMap().value("code").toString() == code) {
    const auto item = service.toMap();
    root["buildSupported"] = item.value("buildSupported");
    root["unsupportedReasons"] = item.value("unsupportedReasons");
    root["verificationStatus"] = item.value("verificationStatus");
  }
  return root;
}

QString Prot698ServiceSchema::validateTreeValue(const QString &serviceCode,
                                                const QVariantMap &treeValue) {
  QString error;
  validateJsonTreeValue(serviceCode, Prot698DataEncoder::toExactJson(treeValue).toObject(),
                        nullptr, &error, false);
  return error;
}

QString Prot698ServiceSchema::oadHexFromMap(const QVariantMap &oad,
                                            QString *err) {
  QString oi = oad.value("oi").toString().trimmed().toUpper();
  int attrId = oad.value("attrId", 2).toInt();
  int index = oad.value("index", 0).toInt();
  QByteArray bytes = Prot698DataEncoder::encodeOAD(oi, attrId, index, err);
  return bytes.isEmpty() ? QString() : QString::fromLatin1(bytes.toHex().toUpper());
}

QString Prot698ServiceSchema::omdHexFromMap(const QVariantMap &omd,
                                            QString *err) {
  QString oi = omd.value("oi").toString().trimmed().toUpper();
  int methodId = omd.value("methodId", 1).toInt();
  int mode = omd.value("mode", 0).toInt();
  QByteArray bytes = Prot698DataEncoder::encodeOMD(oi, methodId, mode, err);
  return bytes.isEmpty() ? QString() : QString::fromLatin1(bytes.toHex().toUpper());
}

QString Prot698ServiceSchema::encodeDataForOad(const QVariantMap &oad,
                                               const QVariant &dataValue,
                                               bool isMethod,
                                               QString *err) {
  QString oi = oad.value("oi").toString().trimmed().toUpper();
  int id = isMethod ? oad.value("methodId", 1).toInt()
                    : oad.value("attrId", 2).toInt();
  int dataIndex = isMethod ? 0 : oad.value("index", 0).toInt();
  Prot698SubItem *definition =
      isMethod ? Prot698OadManager::instance().getMethodDataDef(oi, id)
               : Prot698OadManager::instance().getAttrDataDef(oi, id,
                                                               dataIndex);
  if (!definition) {
    if (err)
      *err = QString("找不到 %1 %2-%3 的 XML 定义")
                 .arg(isMethod ? "OMD" : "OAD")
                 .arg(oi)
                 .arg(id);
    return QString();
  }
  QByteArray bytes = Prot698DataEncoder::encode(*definition, dataValue, err);
  return bytes.isEmpty() && Prot698DataEncoder::typeFromName(definition->type) !=
                                Prot698DataType::Null
             ? QString()
             : QString::fromLatin1(bytes.toHex().toUpper());
}

QByteArray Prot698ServiceSchema::bytesFromHexText(const QString &hex,
                                                  QString *err) {
  QString cleanHex = hex;
  cleanHex.remove("0x", Qt::CaseInsensitive);
  cleanHex.remove(' ');
  cleanHex.remove('\n');
  cleanHex.remove('\r');
  cleanHex.remove('\t');
  cleanHex.remove(':');
  if (cleanHex.isEmpty())
    return {};
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
  return bytes;
}

void Prot698ServiceSchema::appendUInt16BE(QByteArray &bytes, int value) {
  bytes.append(static_cast<char>((value >> 8) & 0xFF));
  bytes.append(static_cast<char>(value & 0xFF));
}

void Prot698ServiceSchema::appendUInt16LE(QByteArray &bytes, int value) {
  bytes.append(static_cast<char>(value & 0xFF));
  bytes.append(static_cast<char>((value >> 8) & 0xFF));
}

QByteArray Prot698ServiceSchema::encodeMsBytes(const QVariantMap &ms,
                                               QString *err) {
  int type = ms.value("type", 0).toInt();
  if (type < 0 || type > 9) {
    if (err)
      *err = "MS 类型必须在 0-9 范围内";
    return {};
  }

  QByteArray bytes;
  bytes.append(static_cast<char>(type & 0xFF));
  QVariantList values = ms.value("values").toList();

  auto appendByteList = [&](const QVariantList &items) -> bool {
    if (items.size() > 255) {
      if (err)
        *err = "MS 列表元素数量超过 255";
      return false;
    }
    bytes.append(static_cast<char>(items.size()));
    for (const QVariant &item : items) {
      int value = item.toInt();
      if (value < 0 || value > 0xFF) {
        if (err)
          *err = "MS 字节值必须在 0-255 范围内";
        return false;
      }
      bytes.append(static_cast<char>(value & 0xFF));
    }
    return true;
  };

  auto appendUInt16List = [&](const QVariantList &items) -> bool {
    if (items.size() > 255) {
      if (err)
        *err = "MS 列表元素数量超过 255";
      return false;
    }
    bytes.append(static_cast<char>(items.size()));
    for (const QVariant &item : items) {
      int value = item.toInt();
      if (value < 0 || value > 0xFFFF) {
        if (err)
          *err = "MS 双字节值必须在 0-65535 范围内";
        return false;
      }
      appendUInt16LE(bytes, value);
    }
    return true;
  };

  switch (type) {
  case 0:
  case 1:
    break;
  case 2:
    if (!appendByteList(values))
      return {};
    break;
  case 3: {
    QVariantList tsaItems = ms.value("tsaItems").toList();
    if (tsaItems.size() > 255) {
      if (err)
        *err = "TSA 数量超过 255";
      return {};
    }
    bytes.append(static_cast<char>(tsaItems.size()));
    for (const QVariant &tsaValue : tsaItems) {
      QByteArray tsaBytes = bytesFromHexText(tsaValue.toString(), err);
      if (err && !err->isEmpty())
        return {};
      bytes.append(Prot698DataEncoder::encodeLength(tsaBytes.size()));
      bytes.append(tsaBytes);
    }
    break;
  }
  case 4:
  case 8:
    if (!appendUInt16List(values))
      return {};
    break;
  case 5:
  case 6:
  case 7:
    bytes.append(bytesFromHexText(ms.value("rawHex").toString(), err));
    if (err && !err->isEmpty())
      return {};
    break;
  case 9: {
    int logicDevice = ms.value("logicDevice", 0).toInt();
    if (logicDevice < 0 || logicDevice > 0xFF) {
      if (err)
        *err = "特定逻辑设备必须在 0-255 范围内";
      return {};
    }
    bytes.append(static_cast<char>(logicDevice & 0xFF));
    break;
  }
  }
  return bytes;
}

QByteArray Prot698ServiceSchema::encodeRsdSelector2Body(
    const QVariantMap &selector, QString *err) {
  QVariantMap oad = selector.value("oad").toMap();
  QByteArray bytes;
  bytes.append(QByteArray::fromHex(oadHexFromMap(oad, err).toLatin1()));
  if (err && !err->isEmpty())
    return {};

  QString startHex = encodeDataForOad(oad, selector.value("startValue"), false, err);
  if (err && !err->isEmpty())
    return {};
  QString endHex = encodeDataForOad(oad, selector.value("endValue"), false, err);
  if (err && !err->isEmpty())
    return {};
  QString intervalHex =
      encodeDataForOad(oad, selector.value("intervalValue"), false, err);
  if (err && !err->isEmpty())
    return {};

  bytes.append(QByteArray::fromHex(startHex.toLatin1()));
  bytes.append(QByteArray::fromHex(endHex.toLatin1()));
  bytes.append(QByteArray::fromHex(intervalHex.toLatin1()));
  return bytes;
}

QByteArray Prot698ServiceSchema::encodeRsdBytes(const QVariantMap &rsd,
                                                QString *err) {
  int type = rsd.value("type", 0).toInt();
  if (type < 0 || type > 11) {
    if (err)
      *err = "RSD 选择方法必须在 0-11 范围内";
    return {};
  }

  QByteArray bytes;
  bytes.append(static_cast<char>(type & 0xFF));

  switch (type) {
  case 0:
    break;
  case 1: {
    QVariantMap selector = rsd.value("selector").toMap();
    QVariantMap oad = selector.value("oad").toMap();
    bytes.append(QByteArray::fromHex(oadHexFromMap(oad, err).toLatin1()));
    if (err && !err->isEmpty())
      return {};
    QString dataHex = encodeDataForOad(oad, selector.value("value"), false, err);
    if (err && !err->isEmpty())
      return {};
    bytes.append(QByteArray::fromHex(dataHex.toLatin1()));
    break;
  }
  case 2:
    bytes.append(encodeRsdSelector2Body(rsd.value("selector").toMap(), err));
    break;
  case 3: {
    QVariantList items = rsd.value("items").toList();
    if (items.size() > 255) {
      if (err)
        *err = "Selector3 元素数量超过 255";
      return {};
    }
    bytes.append(static_cast<char>(items.size()));
    for (const QVariant &item : items) {
      bytes.append(encodeRsdSelector2Body(item.toMap(), err));
      if (err && !err->isEmpty())
        return {};
    }
    break;
  }
  case 4:
    bytes.append(Prot698DataEncoder::encodeDateTimeS(
        rsd.value("startTime").toMap(), err));
    if (err && !err->isEmpty()) return {};
    bytes.append(encodeMsBytes(rsd.value("ms").toMap(), err));
    break;
  case 5:
    bytes.append(Prot698DataEncoder::encodeDateTimeS(
        rsd.value("storageTime").toMap(), err));
    if (err && !err->isEmpty()) return {};
    bytes.append(encodeMsBytes(rsd.value("ms").toMap(), err));
    break;
  case 6:
    bytes.append(Prot698DataEncoder::encodeDateTimeS(
        rsd.value("startTime").toMap(), err));
    if (err && !err->isEmpty()) return {};
    bytes.append(Prot698DataEncoder::encodeDateTimeS(
        rsd.value("endTime").toMap(), err));
    if (err && !err->isEmpty()) return {};
    bytes.append(encodeMsBytes(rsd.value("ms").toMap(), err));
    break;
  case 7:
  case 8: {
    bytes.append(Prot698DataEncoder::encodeDateTimeS(
        rsd.value("startTime").toMap(), err));
    if (err && !err->isEmpty()) return {};
    bytes.append(Prot698DataEncoder::encodeDateTimeS(
        rsd.value("endTime").toMap(), err));
    if (err && !err->isEmpty()) return {};
    QVariantMap ti = rsd.value("ti").toMap();
    bytes.append(Prot698DataEncoder::encodeTI(ti.value("unit").toInt(),
                                              ti.value("value").toInt(), err));
    bytes.append(encodeMsBytes(rsd.value("ms").toMap(), err));
    break;
  }
  case 9: {
    int n = rsd.value("n", 1).toInt();
    if (n < 0 || n > 0xFF) {
      if (err)
        *err = "Selector9 的 n 必须在 0-255 范围内";
      return {};
    }
    bytes.append(static_cast<char>(n & 0xFF));
    break;
  }
  case 10: {
    int n = rsd.value("n", 1).toInt();
    if (n < 0 || n > 0xFF) {
      if (err)
        *err = "Selector10 的 n 必须在 0-255 范围内";
      return {};
    }
    bytes.append(static_cast<char>(n & 0xFF));
    bytes.append(encodeMsBytes(rsd.value("ms").toMap(), err));
    break;
  }
  case 11: {
    QVariantList items = rsd.value("items").toList();
    if (items.size() > 255) {
      if (err)
        *err = "Selector11 RSD 数量超过 255";
      return {};
    }
    bytes.append(static_cast<char>(items.size()));
    for (const QVariant &item : items) {
      bytes.append(encodeRsdBytes(item.toMap(), err));
      if (err && !err->isEmpty())
        return {};
    }
    break;
  }
  }

  if (err && !err->isEmpty())
    return {};
  return bytes;
}

QString Prot698ServiceSchema::encodeRsdFromMap(const QVariantMap &rsd,
                                               QString *err) {
  QByteArray bytes = encodeRsdBytes(rsd, err);
  return (err && !err->isEmpty()) ? QString()
                                  : QString::fromLatin1(bytes.toHex().toUpper());
}

QByteArray Prot698ServiceSchema::encodeCsdBytes(const QVariantMap &csd,
                                                QString *err) {
  int choice = csd.value("choice", 0).toInt();
  if (choice < 0 || choice > 1) {
    if (err)
      *err = "CSD choice 只能是 OAD(0) 或 ROAD(1)";
    return {};
  }

  QByteArray bytes;
  bytes.append(static_cast<char>(choice & 0xFF));
  bytes.append(QByteArray::fromHex(
      oadHexFromMap(csd.value("oad").toMap(), err).toLatin1()));
  if (err && !err->isEmpty())
    return {};

  if (choice == 1) {
    QVariantList roadOads = csd.value("roadOads").toList();
    if (roadOads.size() > 255) {
      if (err)
        *err = "ROAD 关联 OAD 数量超过 255";
      return {};
    }
    bytes.append(static_cast<char>(roadOads.size()));
    for (const QVariant &oad : roadOads) {
      bytes.append(QByteArray::fromHex(oadHexFromMap(oad.toMap(), err).toLatin1()));
      if (err && !err->isEmpty())
        return {};
    }
  }
  return bytes;
}

QString Prot698ServiceSchema::encodeRcsdFromMap(const QVariantMap &rcsd,
                                                QString *err) {
  QVariantList items = rcsd.value("items").toList();
  if (items.size() > 255) {
    if (err)
      *err = "RCSD CSD 数量超过 255";
    return {};
  }
  QByteArray bytes;
  bytes.append(static_cast<char>(items.size()));
  for (const QVariant &item : items) {
    bytes.append(encodeCsdBytes(item.toMap(), err));
    if (err && !err->isEmpty())
      return {};
  }
  return QString::fromLatin1(bytes.toHex().toUpper());
}

QString Prot698ServiceSchema::encodeAResultNormalFromMap(
    const QVariantMap &result, QString *err) {
  QVariantMap oad = result.value("oad").toMap();
  QByteArray bytes;
  bytes.append(QByteArray::fromHex(oadHexFromMap(oad, err).toLatin1()));
  if (err && !err->isEmpty())
    return QString();

  int choice = result.value("choice", 1).toInt();
  if (choice == 1) {
    bytes.append(static_cast<char>(0x01));
    QString dataHex = encodeDataForOad(oad, result.value("dataValue"), false, err);
    if (err && !err->isEmpty())
      return QString();
    bytes.append(QByteArray::fromHex(dataHex.toLatin1()));
  } else {
    int dar = result.value("dar", 0).toInt();
    if (dar < 0 || dar > 0xFF) {
      if (err)
        *err = "DAR 必须在 0-255 范围内";
      return QString();
    }
    bytes.append(static_cast<char>(0x00));
    bytes.append(static_cast<char>(dar & 0xFF));
  }

  return QString::fromLatin1(bytes.toHex().toUpper());
}

QString Prot698ServiceSchema::encodeAResultRecordFromMap(
    const QVariantMap &result, QString *err) {
  QVariantMap oad = result.value("oad").toMap();
  QByteArray bytes;
  bytes.append(QByteArray::fromHex(oadHexFromMap(oad, err).toLatin1()));
  if (err && !err->isEmpty())
    return QString();
  bytes.append(QByteArray::fromHex(
      encodeRcsdFromMap(result.value("rcsd").toMap(), err).toLatin1()));
  if (err && !err->isEmpty())
    return QString();

  int choice = result.value("choice", 0).toInt();
  if (choice == 1) {
    const bool structured = result.contains("rows");
    const auto rows = result.value("rows").toList();
    int rowCount = structured ? rows.size() : result.value("rowCount", 0).toInt();
    if (rowCount < 0 || rowCount > 0xFF) {
      if (err)
        *err = "记录行数必须在 0-255 范围内";
      return QString();
    }
    bytes.append(static_cast<char>(0x01));
    bytes.append(static_cast<char>(rowCount & 0xFF));
    if (structured) {
      QList<const Prot698SubItem *> columns;
      if (!rows.isEmpty() && !recordColumns(result.value("rcsd").toMap(), &columns, err, "$.rcsd")) return {};
      for (const auto &row : rows) {
        const auto cells = row.toList();
        for (int i = 0; i < cells.size(); ++i) {
          const auto encoded = Prot698DataEncoder::encode(*columns.at(i), cells.at(i), err);
          if (encoded.isEmpty() || (err && !err->isEmpty())) return {};
          bytes.append(encoded);
        }
      }
    } else bytes.append(bytesFromHexText(result.value("rowsHex").toString(), err));
    if (err && !err->isEmpty()) return {};
  } else {
    int dar = result.value("dar", 0).toInt();
    if (dar < 0 || dar > 0xFF) {
      if (err)
        *err = "DAR 必须在 0-255 范围内";
      return QString();
    }
    bytes.append(static_cast<char>(0x00));
    bytes.append(static_cast<char>(dar & 0xFF));
  }

  return QString::fromLatin1(bytes.toHex().toUpper());
}

QString Prot698ServiceSchema::encodeSetResultFromMap(const QVariantMap &result,
                                                     QString *err) {
  QByteArray bytes;
  bytes.append(QByteArray::fromHex(
      oadHexFromMap(result.value("oad").toMap(), err).toLatin1()));
  if (err && !err->isEmpty())
    return QString();
  int dar = result.value("dar", 0).toInt();
  if (dar < 0 || dar > 0xFF) {
    if (err)
      *err = "DAR 必须在 0-255 范围内";
    return QString();
  }
  bytes.append(static_cast<char>(dar & 0xFF));
  return QString::fromLatin1(bytes.toHex().toUpper());
}

QString Prot698ServiceSchema::encodeActionResultFromMap(
    const QVariantMap &result, QString *err) {
  QVariantMap omd = result.value("omd").toMap();
  QByteArray bytes;
  bytes.append(QByteArray::fromHex(omdHexFromMap(omd, err).toLatin1()));
  if (err && !err->isEmpty())
    return QString();
  int dar = result.value("dar", 0).toInt();
  if (dar < 0 || dar > 0xFF) {
    if (err)
      *err = "DAR 必须在 0-255 范围内";
    return QString();
  }
  bytes.append(static_cast<char>(dar & 0xFF));
  bool hasData = result.value("hasData", false).toBool();
  bytes.append(static_cast<char>(hasData ? 0x01 : 0x00));
  if (hasData) {
    Prot698SubItem dynamic; dynamic.type = "Data";
    const auto dataBytes = Prot698DataEncoder::encode(dynamic, result.value("dataValue"), err);
    if (err && !err->isEmpty()) return QString();
    bytes.append(dataBytes);
  }
  return QString::fromLatin1(bytes.toHex().toUpper());
}

QVariantList Prot698ServiceSchema::encodeResultList(
    const QVariantList &items,
    QString (*encoder)(const QVariantMap &, QString *), QString *err) {
  QVariantList result;
  for (const QVariant &itemValue : items) {
    result.append(encoder(itemValue.toMap(), err));
    if (err && !err->isEmpty())
      return {};
  }
  return result;
}

QVariantMap Prot698ServiceSchema::toServiceSpec(const QString &serviceCode,
                                                const QVariantMap &inputTreeValue,
                                                QString *err) {
  QString code = normalizeServiceCode(serviceCode);
  QVariantMap treeValue;
  QString validateError;
  const bool valid = validateJsonTreeValue(code, Prot698DataEncoder::toExactJson(inputTreeValue).toObject(),
                        &treeValue, &validateError, false);
  if (!valid) {
    if (err)
      *err = validateError.isEmpty() ? "服务树校验失败" : validateError;
    return {};
  }

  QVariantMap spec;
  spec["serviceCode"] = code;
  spec["piid"] = treeValue.value("piid").toMap();
  spec["timeTag"] = treeValue.value("timeTag").toMap();

  if (code == "GET_NORMAL") {
    spec["oad"] = oadHexFromMap(treeValue.value("oad").toMap(), err);
  } else if (code == "GET_LIST") {
    QVariantList oadHexList;
    for (const QVariant &oadValue : treeValue.value("oads").toList())
      oadHexList.append(oadHexFromMap(oadValue.toMap(), err));
    spec["oads"] = oadHexList;
  } else if (code == "GET_RECORD") {
    spec["oad"] = oadHexFromMap(treeValue.value("oad").toMap(), err);
    spec["rsdHex"] = encodeRsdFromMap(treeValue.value("rsd").toMap(), err);
    spec["rcsdHex"] = encodeRcsdFromMap(treeValue.value("rcsd").toMap(), err);
  } else if (code == "GET_NEXT") {
    spec["lastBlockNo"] = treeValue.value("lastBlockNo", 0).toInt();
  } else if (code == "SET_NORMAL") {
    QVariantMap item = treeValue.value("setItem").toMap();
    QVariantMap oad = item.value("oad").toMap();
    spec["oad"] = oadHexFromMap(oad, err);
    spec["dataHex"] = encodeDataForOad(oad, item.value("dataValue"), false, err);
  } else if (code == "SET_LIST") {
    QVariantList items;
    for (const QVariant &itemValue : treeValue.value("setItems").toList()) {
      QVariantMap inputItem = itemValue.toMap();
      QVariantMap oad = inputItem.value("oad").toMap();
      QVariantMap outputItem;
      outputItem["oad"] = oadHexFromMap(oad, err);
      outputItem["dataHex"] =
          encodeDataForOad(oad, inputItem.value("dataValue"), false, err);
      items.append(outputItem);
    }
    spec["items"] = items;
  } else if (code == "ACTION_NORMAL") {
    QVariantMap item = treeValue.value("actionItem").toMap();
    QVariantMap omd = item.value("omd").toMap();
    spec["omd"] = omdHexFromMap(omd, err);
    spec["dataHex"] =
        encodeDataForOad(omd, item.value("dataValue"), true, err);
  } else if (code == "ACTION_LIST") {
    QVariantList items;
    for (const QVariant &itemValue : treeValue.value("actionItems").toList()) {
      QVariantMap inputItem = itemValue.toMap();
      QVariantMap omd = inputItem.value("omd").toMap();
      QVariantMap outputItem;
      outputItem["omd"] = omdHexFromMap(omd, err);
      outputItem["dataHex"] =
          encodeDataForOad(omd, inputItem.value("dataValue"), true, err);
      items.append(outputItem);
    }
    spec["items"] = items;
  } else if (code == "PROXY_GET_LIST") {
    spec["totalTimeout"] = treeValue.value("totalTimeout", 10).toInt();
    QVariantList servers;
    for (const QVariant &serverValue : treeValue.value("servers").toList()) {
      QVariantMap inputServer = serverValue.toMap();
      QVariantMap outputServer;
      outputServer["tsaHex"] = inputServer.value("tsaHex").toString();
      outputServer["timeout"] = inputServer.value("timeout", 5).toInt();
      QVariantList oadHexList;
      for (const QVariant &oadValue : inputServer.value("oads").toList())
        oadHexList.append(oadHexFromMap(oadValue.toMap(), err));
      outputServer["oads"] = oadHexList;
      servers.append(outputServer);
    }
    spec["servers"] = servers;
  } else if (code == "GET_RESPONSE_NORMAL") {
    spec["resultHex"] =
        encodeAResultNormalFromMap(treeValue.value("result").toMap(), err);
  } else if (code == "GET_RESPONSE_LIST") {
    spec["results"] = encodeResultList(treeValue.value("results").toList(),
                                        encodeAResultNormalFromMap, err);
  } else if (code == "GET_RESPONSE_RECORD") {
    spec["recordHex"] = encodeAResultRecordFromMap(
        treeValue.value("recordResult").toMap(), err);
  } else if (code == "GET_RESPONSE_NEXT") {
    QVariantMap nextResult = treeValue.value("nextResult").toMap();
    int choice = nextResult.value("choice", 0).toInt();
    spec["end"] = nextResult.value("end", true).toBool();
    spec["blockNo"] = nextResult.value("blockNo", 0).toInt();
    spec["choice"] = choice;
    spec["dar"] = nextResult.value("dar", 0).toInt();
    if (choice == 1) {
      spec["normalResults"] = encodeResultList(
          nextResult.value("normalResults").toList(), encodeAResultNormalFromMap,
          err);
    } else if (choice == 2) {
      spec["recordResults"] = encodeResultList(
          nextResult.value("recordResults").toList(), encodeAResultRecordFromMap,
          err);
    }
  } else if (code == "SET_RESPONSE_NORMAL") {
    spec["resultHex"] =
        encodeSetResultFromMap(treeValue.value("setResult").toMap(), err);
  } else if (code == "SET_RESPONSE_LIST") {
    spec["results"] = encodeResultList(treeValue.value("setResults").toList(),
                                        encodeSetResultFromMap, err);
  } else if (code == "ACTION_RESPONSE_NORMAL") {
    spec["resultHex"] = encodeActionResultFromMap(
        treeValue.value("actionResult").toMap(), err);
  } else if (code == "ACTION_RESPONSE_LIST") {
    spec["results"] = encodeResultList(
        treeValue.value("actionResults").toList(), encodeActionResultFromMap,
        err);
  } else if (code == "PROXY_GET_RESPONSE_LIST") {
    QVariantList servers;
    for (const QVariant &serverValue : treeValue.value("servers").toList()) {
      QVariantMap inputServer = serverValue.toMap();
      QVariantMap outputServer;
      outputServer["tsaHex"] = inputServer.value("tsaHex").toString();
      outputServer["results"] = encodeResultList(
          inputServer.value("results").toList(), encodeAResultNormalFromMap,
          err);
      if (err && !err->isEmpty())
        return {};
      servers.append(outputServer);
    }
    spec["servers"] = servers;
  }

  if (err && !err->isEmpty())
    return {};
  return spec;
}
namespace {
QJsonObject jsonObjectSchema(const QJsonObject &props, const QJsonArray &required) {
  return {{"type", "object"}, {"properties", props}, {"required", required}, {"additionalProperties", false}};
}
QJsonObject jsonInteger(int minimum = 0, int maximum = 255) {
  return {{"type", "integer"}, {"minimum", minimum}, {"maximum", maximum}};
}
QJsonObject jsonList(const QJsonObject &item, int minimum = 1) {
  return {{"type", "array"}, {"items", item}, {"minItems", minimum}, {"maxItems", 255}};
}
QJsonObject oadInput(bool method = false) {
  return jsonObjectSchema({{"oi", QJsonObject{{"type", "string"}, {"pattern", "^[0-9A-Fa-f]{4}$"}}},
      {method ? "methodId" : "attrId", jsonInteger(1)},
      {method ? "mode" : "index", jsonInteger()}},
      {"oi", method ? "methodId" : "attrId", method ? "mode" : "index"});
}
QJsonObject msInputSchema() {
  QJsonArray branches;
  for (int type = 0; type <= 9; ++type) {
    QJsonObject props{{"type", QJsonObject{{"const", type}}}};
    QJsonArray required{"type"};
    if (type == 2 || type == 4 || type == 8) { props["values"] = jsonList(jsonInteger(0, type == 2 ? 255 : 65535), 0); required.append("values"); }
    if (type == 3) { props["tsaItems"] = jsonList(QJsonObject{{"type", "string"}, {"description", "TSA encoded bytes, 2..17 bytes"}}, 0); required.append("tsaItems"); }
    if (type == 9) { props["logicDevice"] = jsonInteger(); required.append("logicDevice"); }
    auto branch = jsonObjectSchema(props, required);
    if (type >= 5 && type <= 7) { branch["not"] = QJsonObject{}; branch["description"] = "unsupported_definition: MS rawHex branch is unavailable to AI"; }
    branches.append(branch);
  }
  return {{"oneOf", branches}};
}
QJsonObject rsdInputSchema() {
  QJsonArray branches;
  const QStringList names{"不选择", "指定属性值", "属性值区间及间隔", "多个属性值区间", "采集启动时间和表计集合",
      "采集存储时间和表计集合", "采集启动时间区间和表计集合", "存储时间区间、间隔和表计集合",
      "成功时间区间、间隔和表计集合", "上第 n 次记录", "最新 n 条记录和表计集合", "多个记录选择条件"};
  Prot698SubItem dateDef; dateDef.type = "date_time_s";
  const auto date = QJsonObject::fromVariantMap(Prot698DataEncoder::subItemToSchema(dateDef)["inputSchema"].toMap());
  const QJsonObject value{{"description", "Validate against selector.oad inputSchema"}};
  const auto single = jsonObjectSchema({{"oad", oadInput()}, {"value", value}}, {"oad", "value"});
  const auto range = jsonObjectSchema({{"oad", oadInput()}, {"startValue", value},
      {"endValue", value}, {"intervalValue", value}}, {"oad", "startValue", "endValue", "intervalValue"});
  Prot698SubItem tiDef; tiDef.type = "TI";
  const auto ti = QJsonObject::fromVariantMap(Prot698DataEncoder::subItemToSchema(tiDef)["inputSchema"].toMap());
  for (int type = 0; type <= 11; ++type) {
    QJsonObject props{{"type", QJsonObject{{"const", type}}}};
    QJsonArray required{"type"};
    const auto add = [&](const QString &key, const QJsonObject &schema) { props[key] = schema; required.append(key); };
    if (type == 1 || type == 2) add("selector", type == 1 ? single : range);
    if (type == 3) add("items", jsonList(range, 0));
    if (type == 11) add("items", jsonList(QJsonObject{{"$ref", "#/$defs/rsd"}}, 0));
    if (type == 4 || type == 6 || type == 7 || type == 8) add("startTime", date);
    if (type == 5) add("storageTime", date);
    if (type == 6 || type == 7 || type == 8) add("endTime", date);
    if (type >= 4 && type <= 8 || type == 10) add("ms", msInputSchema());
    if (type == 7 || type == 8) add("ti", ti);
    if (type == 9 || type == 10) add("n", jsonInteger());
    auto branch = jsonObjectSchema(props, required);
    branch["title"] = names.at(type);
    if (type == 9) branch["description"] = "Select the nth most recent record: n=1 means the latest one record. This selects rows, not an OAD data index.";
    if (type == 10) branch["description"] = "Select the latest n records for the explicitly specified meter set (MS).";
    branches.append(branch);
  }
  return {{"oneOf", branches}};
}
QJsonObject serviceInputSchema(const QString &code, bool aiInput) {
  const auto oad = oadInput(); const auto omd = oadInput(true);
  const QJsonObject data{{"description", "Value validated against the selected OAD/OMD inputSchema; query get_definition first"}};
  auto setItem = jsonObjectSchema({{"oad", oad}, {"dataValue", data}}, {"oad", "dataValue"});
  auto actionItem = jsonObjectSchema({{"omd", omd}, {"dataValue", data}}, {"omd", "dataValue"});
  auto normal = jsonObjectSchema({{"oad", oad}, {"choice", jsonInteger(0, 1)},
      {"dar", jsonInteger()}, {"dataValue", data}}, {"oad", "choice"});
  normal["oneOf"] = QJsonArray{
      jsonObjectSchema({{"oad", oad}, {"choice", QJsonObject{{"const", 0}}}, {"dar", jsonInteger()}}, {"oad", "choice", "dar"}),
      jsonObjectSchema({{"oad", oad}, {"choice", QJsonObject{{"const", 1}}}, {"dataValue", data}}, {"oad", "choice", "dataValue"})};
  auto csd = jsonObjectSchema({{"choice", jsonInteger(0, 1)}, {"oad", oad},
      {"roadOads", jsonList(oad, 0)}}, {"choice", "oad"});
  csd["oneOf"] = QJsonArray{
      jsonObjectSchema({{"choice", QJsonObject{{"const", 0}}}, {"oad", oad}}, {"choice", "oad"}),
      jsonObjectSchema({{"choice", QJsonObject{{"const", 1}}}, {"oad", oad}, {"roadOads", jsonList(oad, 0)}}, {"choice", "oad", "roadOads"})};
  auto rcsd = jsonObjectSchema({{"items", jsonList(csd, 0)}}, {"items"});
  const auto rowsShape = jsonList(jsonList(data, 0), 0);
  QJsonObject recordProperties{{"oad", oad}, {"rcsd", rcsd}, {"choice", jsonInteger(0, 1)},
      {"dar", jsonInteger()}, {"rows", rowsShape}};
  if (!aiInput) {
    recordProperties["rowCount"] = jsonInteger();
    recordProperties["rowsHex"] = QJsonObject{{"type", "string"}};
  }
  auto record = jsonObjectSchema(recordProperties, {"oad", "rcsd", "choice"});
  auto dataRows = jsonObjectSchema({{"oad", oad}, {"rcsd", rcsd},
      {"choice", QJsonObject{{"const", 1}}}, {"rows", rowsShape}}, {"oad", "rcsd", "choice", "rows"});
  const auto ordinaryCsd = jsonObjectSchema({{"choice", QJsonObject{{"type", "integer"}, {"const", 0}}}, {"oad", oad}}, {"choice", "oad"});
  const auto ordinaryRcsd = jsonObjectSchema({{"items", jsonList(ordinaryCsd, 1)}}, {"items"});
  dataRows["allOf"] = QJsonArray{QJsonObject{
      {"if", QJsonObject{{"properties", QJsonObject{{"rows", QJsonObject{{"minItems", 1}}}}}}},
      {"then", QJsonObject{{"properties", QJsonObject{{"rcsd", ordinaryRcsd}}}}}}};
  dataRows["description"] = "Rows are ordered arrays of exactly RCSD.items count cells; query each OAD inputSchema. Row count is derived locally. Empty rows need no cell definition.";
  dataRows["x-unsupportedReason"] = "Nonempty rows with CSD.choice=1 (ROAD) need a target example confirming the cell container; ordinary OAD columns, empty rows and DAR are supported.";
  record["oneOf"] = QJsonArray{
      jsonObjectSchema({{"oad", oad}, {"rcsd", rcsd}, {"choice", QJsonObject{{"const", 0}}}, {"dar", jsonInteger()}}, {"oad", "rcsd", "choice", "dar"}), dataRows};
  const auto tsa = jsonObjectSchema({
      {"address", QJsonObject{{"type", "string"}, {"pattern", "^([0-9A-Fa-f]{2}){1,16}$"}, {"description", "Human display order; local code reverses once and adds the TSA descriptor. Never compute tsaHex."}}},
      {"addressType", jsonInteger(0, 3)}, {"logicAddress", jsonInteger(0, 1)}},
      {"address", "addressType", "logicAddress"});
  const auto proxyServer = [&](bool response) {
    QJsonObject props{{"tsa", tsa}};
    QJsonArray required;
    if (aiInput) required.append("tsa");
    else props["tsaHex"] = QJsonObject{{"type", "string"}, {"description", "Legacy manual encoded TSA, without outer length"}};
    if (response) { props["results"] = jsonList(normal); required.append("results"); }
    else { props["timeout"] = jsonInteger(0, 65535); props["oads"] = jsonList(oad); required.append("timeout"); required.append("oads"); }
    return jsonObjectSchema(props, required);
  };
  auto setResult = jsonObjectSchema({{"oad", oad}, {"dar", jsonInteger()}}, {"oad", "dar"});
  auto actionResult = jsonObjectSchema({{"omd", omd}, {"dar", jsonInteger()},
      {"hasData", QJsonObject{{"type", "boolean"}}}, {"dataValue", data}}, {"omd", "dar", "hasData"});
  Prot698SubItem dynamicDef; dynamicDef.type = "Data";
  const auto dynamicSchema = QJsonObject::fromVariantMap(Prot698DataEncoder::subItemToSchema(dynamicDef)["inputSchema"].toMap());
  actionResult["oneOf"] = QJsonArray{
      jsonObjectSchema({{"omd", omd}, {"dar", jsonInteger()}, {"hasData", QJsonObject{{"const", false}}}}, {"omd", "dar", "hasData"}),
      jsonObjectSchema({{"omd", omd}, {"dar", jsonInteger()}, {"hasData", QJsonObject{{"const", true}}}, {"dataValue", QJsonObject{{"$ref", "#/$defs/typedData"}}}}, {"omd", "dar", "hasData", "dataValue"})};
  QJsonObject props{{"piid", jsonObjectSchema({{"seqNo", jsonInteger(0, 63)},
      {"highPriority", QJsonObject{{"type", "boolean"}}}, {"acd", QJsonObject{{"type", "boolean"}}}}, {"seqNo"})}};
  if (!code.contains("RESPONSE")) {
    auto piidSchema = props["piid"].toObject(); auto piidProperties = piidSchema["properties"].toObject();
    piidProperties["acd"] = QJsonObject{{"type", "boolean"}, {"const", false}};
    piidSchema["properties"] = piidProperties; props["piid"] = piidSchema;
  }
  QJsonArray required{"piid"};
  const auto add = [&](const QString &key, const QJsonObject &schema) { props[key] = schema; required.append(key); };
  if (code == "GET_NORMAL") add("oad", oad);
  else if (code == "GET_LIST") add("oads", jsonList(oad));
  else if (code == "GET_RECORD") {
    auto columns = rcsd;
    columns["description"] = "Record columns: items=[] encodes RCSD=0 and selects all columns (the complete record). Use explicit CSDs only when specific columns are requested; do not repeat the record-table OAD as a column to mean all columns.";
    add("oad", oad); add("rsd", QJsonObject{{"$ref", "#/$defs/rsd"}}); add("rcsd", columns);
  } else if (code == "GET_NEXT") add("lastBlockNo", jsonInteger(0, 65535));
  else if (code == "SET_NORMAL") add("setItem", setItem);
  else if (code == "SET_LIST") add("setItems", jsonList(setItem));
  else if (code == "ACTION_NORMAL") add("actionItem", actionItem);
  else if (code == "ACTION_LIST") add("actionItems", jsonList(actionItem));
  else if (code == "PROXY_GET_LIST") {
    add("totalTimeout", jsonInteger(0, 65535));
    add("servers", jsonList(proxyServer(false)));
  } else if (code == "GET_RESPONSE_NORMAL") add("result", normal);
  else if (code == "GET_RESPONSE_LIST") add("results", jsonList(normal));
  else if (code == "GET_RESPONSE_RECORD") add("recordResult", record);
  else if (code == "GET_RESPONSE_NEXT") add("nextResult", jsonObjectSchema({
      {"end", QJsonObject{{"type", "boolean"}}}, {"blockNo", jsonInteger(0, 65535)},
      {"choice", jsonInteger(0, 2)}, {"dar", jsonInteger()},
      {"normalResults", jsonList(normal)}, {"recordResults", jsonList(record)}}, {"end", "blockNo", "choice"}));
  else if (code == "SET_RESPONSE_NORMAL") add("setResult", setResult);
  else if (code == "SET_RESPONSE_LIST") add("setResults", jsonList(setResult));
  else if (code == "ACTION_RESPONSE_NORMAL") add("actionResult", actionResult);
  else if (code == "ACTION_RESPONSE_LIST") add("actionResults", jsonList(actionResult));
  else if (code == "PROXY_GET_RESPONSE_LIST") add("servers", jsonList(proxyServer(true)));
  props["timeTag"] = jsonObjectSchema({{"hasTimeTag", QJsonObject{{"type", "boolean"}}},
      {"year", jsonInteger(1, 9999)}, {"month", jsonInteger(1, 12)}, {"day", jsonInteger(1, 31)},
      {"hour", jsonInteger(0, 23)}, {"minute", jsonInteger(0, 59)}, {"second", jsonInteger(0, 59)},
      {"tiUnit", jsonInteger(0, 5)}, {"tiValue", jsonInteger(0, 65535)}}, {"hasTimeTag"});
  auto schema = jsonObjectSchema(props, required);
  auto definitions = dynamicSchema["$defs"].toObject(); definitions["rsd"] = rsdInputSchema();
  schema["$defs"] = definitions;
  return schema;
}
struct ServiceValidator {
  QString *err;
  bool ai;
  int nodes = 0;
  bool fail(const QString &path, const QString &message) {
    if (err) *err = path + ": " + message;
    return false;
  }
  bool object(const QJsonObject &obj, const QStringList &keys,
              const QStringList &required, const QString &path) {
    for (auto it = obj.begin(); it != obj.end(); ++it)
      if (!keys.contains(it.key())) {
        if (ai && (((it.key() == "rowsHex" || it.key() == "rowCount") && keys.contains("rows")) ||
                   (it.key() == "tsaHex" && keys.contains("tsa"))))
          return fail(path + "." + it.key(), "unsupported_definition: AI请使用结构化rows/tsa，编码Hex仅保留旧人工入口");
        return fail(path + "." + it.key(), "未知字段");
      }
    for (const auto &key : required)
      if (!obj.contains(key)) return fail(path + "." + key, "缺少必填字段");
    return true;
  }
  bool integer(const QJsonValue &value, int min, int max, const QString &path) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()) || std::floor(value.toDouble()) != value.toDouble() || value.toDouble() < min || value.toDouble() > max)
      return fail(path, "整数类型或范围不合法");
    return true;
  }
  bool shape(const QJsonObject &schema, const QJsonValue &value, const QString &path, int depth = 0) {
    if (++nodes > 8192 || depth > 40) return fail(path, "服务树深度或数量超过限制");
    if (schema.contains("enum") && !schema["enum"].toArray().contains(value)) return fail(path, "不在枚举集合中");
    if (schema.contains("const") && schema["const"] != value) return fail(path, "与分支固定值不一致");
    if (ai && schema.contains("not")) return fail(path, "unsupported_definition: 该分支暂不支持");
    if (ai && schema.contains("oneOf")) {
      bool matched = false;
      QString firstError;
      int bestMatch = -2;
      for (const auto &branch : schema["oneOf"].toArray()) {
        const auto branchSchema = branch.toObject();
        int matchingConstants = 0;
        const auto properties = branchSchema["properties"].toObject();
        for (auto it = properties.begin(); it != properties.end(); ++it) {
          const auto property = it.value().toObject();
          if (!property.contains("const") || !value.toObject().contains(it.key())) continue;
          if (value.toObject()[it.key()] != property["const"]) { matchingConstants = -1; break; }
          ++matchingConstants;
        }
        QString branchError;
        ServiceValidator attempt{&branchError, ai};
        if (attempt.shape(branchSchema, value, path, depth + 1)) { matched = true; break; }
        if (matchingConstants > bestMatch ||
            (matchingConstants == bestMatch && branchError.contains("unsupported_definition"))) {
          bestMatch = matchingConstants; firstError = branchError;
        }
      }
      if (!matched) { if (err) *err = firstError; return false; }
    }
    const QString type = schema["type"].toString();
    if (type == "object") {
      if (!value.isObject()) return fail(path, "必须是对象");
      const auto obj = value.toObject();
      const auto props = schema["properties"].toObject();
      if (schema.contains("properties")) {
        QStringList required;
        for (auto key : schema["required"].toArray()) required.append(key.toString());
        if (!object(obj, props.keys(), required, path)) return false;
        for (auto it = obj.begin(); it != obj.end(); ++it)
          if (!shape(props[it.key()].toObject(), it.value(), path + "." + it.key(), depth + 1)) return false;
      }
    } else if (type == "array") {
      if (!value.isArray()) return fail(path, "必须是数组");
      const auto array = value.toArray();
      if (array.size() < schema["minItems"].toInt() || array.size() > schema["maxItems"].toInt(255)) return fail(path, "数组数量不合法");
      for (int i = 0; i < array.size(); ++i)
        if (!shape(schema["items"].toObject(), array.at(i), path + "[" + QString::number(i) + "]", depth + 1)) return false;
    } else if (type == "integer") {
      if (!integer(value, schema["minimum"].toInt(), schema["maximum"].toInt(255), path)) return false;
    } else if (type == "boolean") {
      if (!value.isBool()) return fail(path, "必须是bool");
    } else if (type == "string") {
      if (!value.isString()) return fail(path, "必须是字符串");
      if (value.toString().size() > 131070) return fail(path, "字符串过长");
      if (schema.contains("pattern") && !QRegularExpression(schema["pattern"].toString()).match(value.toString()).hasMatch()) return fail(path, "字符串格式不合法");
    }
    return true;
  }
  bool member(const QJsonObject &obj, bool method, const QString &op,
              const QString &path, Prot698SubItem **data = nullptr) {
    auto &manager = Prot698OadManager::instance();
    const QString oi = obj["oi"].toString().toUpper();
    const int id = obj[method ? "methodId" : "attrId"].toInt();
    auto *definition = method ? manager.getMethodDef(oi, id) : manager.getAttrDef(oi, id);
    if (!definition) return fail(path, "找不到对象属性或方法定义");
    const auto ops = definition->op.split('|');
    for (const auto &allowed : ops)
      if (allowed != "GET" && allowed != "SET" && allowed != "ACTION") return fail(path, "unsupported_definition: 无法确认OP " + definition->op);
    if (!ops.contains(op)) return fail(path, "当前定义不允许 " + op);
    if (!method && obj["index"].toInt() != 0) {
      if (ai) return fail(path + ".index", "unsupported_definition: 非零索引语义尚未确认");
      if (!manager.getAttrDataDef(oi, id, obj["index"].toInt())) return fail(path + ".index", "属性索引超出当前定义");
    }
    if (ai && method && obj["mode"].toInt() != 0)
      return fail(path + ".mode", "unsupported_definition: 非零方法模式尚未确认");
    if (data) *data = method ? manager.getMethodDataDef(oi, id) : manager.getAttrDataDef(oi, id, obj["index"].toInt());
    return true;
  }
  bool data(const QJsonObject &memberObj, const QJsonValue &value, bool method,
            const QString &op, const QString &path, QVariant *raw,
            const QString &valuePath = QString()) {
    Prot698SubItem *definition = nullptr;
    if (!member(memberObj, method, op, path, &definition)) return false;
    if (!definition) return fail(path, "没有数据定义");
    return Prot698DataEncoder::validateJsonValue(*definition, value, raw, err,
        valuePath.isEmpty() ? path + ".dataValue" : valuePath, ai);
  }
  bool validateOads(const QJsonArray &items, const QString &path) {
    for (int i = 0; i < items.size(); ++i)
      if (!member(items.at(i).toObject(), false, "GET", path + "[" + QString::number(i) + "]")) return false;
    return true;
  }
  bool rcsd(const QJsonObject &obj, const QString &path) {
    const auto items = obj["items"].toArray();
    for (int i = 0; i < items.size(); ++i) {
      const auto item = items.at(i).toObject(); const auto p = path + ".items[" + QString::number(i) + "]";
      if (!member(item["oad"].toObject(), false, "GET", p + ".oad")) return false;
      if (item["choice"].toInt() == 1) {
        if (!item.contains("roadOads")) return fail(p + ".roadOads", "缺少必填字段");
        if (!validateOads(item["roadOads"].toArray(), p + ".roadOads")) return false;
      } else if (ai && item.contains("roadOads")) return fail(p + ".roadOads", "当前分支不允许该字段");
    }
    return true;
  }
  bool time(const QJsonObject &obj, const QString &path) {
    Prot698SubItem def; def.type = "date_time_s";
    QVariant raw;
    return Prot698DataEncoder::validateJsonValue(def, obj, &raw, err, path, ai);
  }
  bool ms(const QJsonObject &obj, const QString &path) {
    if (!object(obj, {"type", "values", "tsaItems", "rawHex", "logicDevice"}, {"type"}, path) || !integer(obj["type"], 0, 9, path + ".type")) return false;
    const int type = obj["type"].toInt();
    const QString key = type == 3 ? "tsaItems" : type == 9 ? "logicDevice" : type == 5 || type == 6 || type == 7 ? "rawHex" : type == 0 || type == 1 ? "" : "values";
    if (ai) for (auto it = obj.begin(); it != obj.end(); ++it) if (it.key() != "type" && it.key() != key) return fail(path + "." + it.key(), "当前MS分支不允许该字段");
    if (key.isEmpty()) return true;
    if (!obj.contains(key)) return fail(path + "." + key, "缺少必填字段");
    if (key == "rawHex") {
      if (ai) return fail(path + ".rawHex", "unsupported_definition: MS原始Hex未向AI开放");
      Prot698SubItem def; def.type = "octet_string"; QVariant raw;
      return Prot698DataEncoder::validateJsonValue(def, obj[key], &raw, err, path + ".rawHex", false);
    }
    if (key == "logicDevice") return integer(obj[key], 0, 255, path + ".logicDevice");
    if (!obj[key].isArray() || obj[key].toArray().size() > 255) return fail(path + "." + key, "需要最多255项数组");
    const auto list = obj[key].toArray();
    for (int i = 0; i < list.size(); ++i) {
      const auto p = path + "." + key + "[" + QString::number(i) + "]";
      if (type == 3) { Prot698SubItem def; def.type = "TSA"; QVariant raw; if (!Prot698DataEncoder::validateJsonValue(def, list.at(i), &raw, err, p, ai)) return false; }
      else if (!integer(list.at(i), 0, type == 2 ? 255 : 65535, p)) return false;
    }
    return true;
  }
  bool rsd(QJsonObject &obj, const QString &path, int depth = 0) {
    if (depth > 16) return fail(path, "RSD递归深度超过16");
    if (!object(obj, {"type", "selector", "items", "startTime", "endTime", "storageTime", "ms", "ti", "n"}, {"type"}, path) || !integer(obj["type"], 0, 11, path + ".type")) return false;
    int type = obj["type"].toInt();
    QStringList keys{"type"};
    if (type == 1 || type == 2) keys << "selector";
    if (type == 3 || type == 11) keys << "items";
    if (type == 4 || type == 6 || type == 7 || type == 8) keys << "startTime";
    if (type == 5) keys << "storageTime";
    if (type == 6 || type == 7 || type == 8) keys << "endTime";
    if (type >= 4 && type <= 8 || type == 10) keys << "ms";
    if (type == 7 || type == 8) keys << "ti";
    if (type == 9 || type == 10) keys << "n";
    for (const auto &key : keys) if (!obj.contains(key)) return fail(path + "." + key, "缺少必填字段");
    if (ai && !object(obj, keys, keys, path)) return false;
    for (const auto &key : {QString("startTime"), QString("endTime"), QString("storageTime")}) if (keys.contains(key)) {
      if (!obj[key].isObject() || !time(obj[key].toObject(), path + "." + key)) return false;
    }
    if (keys.contains("ms") && (!obj["ms"].isObject() || !ms(obj["ms"].toObject(), path + ".ms"))) return false;
    if (keys.contains("n") && !integer(obj["n"], 0, 255, path + ".n")) return false;
    if (keys.contains("ti")) { Prot698SubItem def; def.type = "TI"; QVariant raw; if (!Prot698DataEncoder::validateJsonValue(def, obj["ti"], &raw, err, path + ".ti", ai)) return false; }
    const auto selector = [&](QJsonObject &sel, int kind, const QString &p) {
      QStringList fields = kind == 1 ? QStringList{"oad", "value"} : QStringList{"oad", "startValue", "endValue", "intervalValue"};
      if (!object(sel, fields, fields, p) || !shape(oadInput(), sel["oad"], p + ".oad")) return false;
      for (const auto &key : fields) if (key != "oad") {
        QVariant raw;
        if (!data(sel["oad"].toObject(), sel[key], false, "GET", p + "." + key, &raw, p + "." + key)) return false;
        sel[key] = Prot698DataEncoder::toExactJson(raw);
      }
      return true;
    };
    if (type == 1 || type == 2) {
      if (!obj["selector"].isObject()) return fail(path + ".selector", "必须是对象");
      auto sel = obj["selector"].toObject(); if (!selector(sel, type, path + ".selector")) return false; obj["selector"] = sel;
    }
    if (type == 3 || type == 11) {
      if (!obj["items"].isArray() || obj["items"].toArray().size() > 255) return fail(path + ".items", "需要最多255项数组");
      auto list = obj["items"].toArray();
      for (int i = 0; i < list.size(); ++i) {
        if (!list.at(i).isObject()) return fail(path, "选择项必须是对象");
        auto item = list.at(i).toObject(); const auto p = path + ".items[" + QString::number(i) + "]";
        if (!(type == 11 ? rsd(item, p, depth + 1) : selector(item, 2, p))) return false;
        list[i] = item;
      }
      obj["items"] = list;
    }
    return true;
  }
  bool result(QJsonObject &obj, int kind, const QString &path) {
    bool method = kind == 3;
    const QString key = method ? "omd" : "oad";
    if (!member(obj[key].toObject(), method, kind == 2 ? "SET" : method ? "ACTION" : "GET", path + "." + key)) return false;
    if (kind == 2) return true;
    if (kind == 3) {
      if (obj["hasData"].toBool()) {
        if (!obj.contains("dataValue")) return fail(path + ".dataValue", "缺少必填字段");
        Prot698SubItem dynamic; dynamic.type = "Data"; QVariant raw;
        if (!Prot698DataEncoder::validateJsonValue(dynamic, obj["dataValue"], &raw, err, path + ".dataValue", ai)) return false;
        obj["dataValue"] = Prot698DataEncoder::toExactJson(raw);
      } else if (ai && obj.contains("dataValue")) return fail(path + ".dataValue", "hasData=false禁止dataValue");
      return true;
    }
    if (kind == 1 && !rcsd(obj["rcsd"].toObject(), path + ".rcsd")) return false;
    if (obj["choice"].toInt() == 0) {
      if (!obj.contains("dar")) return fail(path + ".dar", "缺少必填字段");
      if (ai) for (const auto &forbidden : {"dataValue", "rowCount", "rowsHex", "rows"}) if (obj.contains(forbidden)) return fail(path + "." + forbidden, "DAR分支不允许该字段");
    } else if (kind == 1) {
      if (obj.contains("rows")) {
        if (obj.contains("rowsHex")) return fail(path, "rows与rowsHex不能同时指定");
        auto rows = obj["rows"].toArray();
        QList<const Prot698SubItem *> columns;
        if (!rows.isEmpty() && !recordColumns(obj["rcsd"].toObject().toVariantMap(), &columns, err, path + ".rcsd")) return false;
        if (!rows.isEmpty() && columns.isEmpty()) return fail(path + ".rcsd.items", "非空记录行至少需要一个OAD列");
        if (rows.size() * columns.size() > 4096) return fail(path + ".rows", "记录行列总量超过4096");
        if (obj.contains("rowCount") && obj["rowCount"].toInt() != rows.size()) return fail(path + ".rowCount", "rowCount与实际rows数量不一致");
        for (int r = 0; r < rows.size(); ++r) {
          auto cells = rows.at(r).toArray();
          const auto rowPath = path + ".rows[" + QString::number(r) + "]";
          if (cells.size() != columns.size()) return fail(rowPath, "每行cell数量必须与RCSD普通OAD列数精确一致");
          for (int c = 0; c < cells.size(); ++c) {
            QVariant raw;
            if (!Prot698DataEncoder::validateJsonValue(*columns.at(c), cells.at(c), &raw, err,
                  rowPath + "[" + QString::number(c) + "]", ai)) return false;
            cells[c] = Prot698DataEncoder::toExactJson(raw);
          }
          rows[r] = cells;
        }
        obj["rows"] = rows;
      } else {
        if (ai) return fail(path + ".rows", "缺少必填字段rows，AI不接受rowsHex");
        if (!obj.contains("rowCount") || !obj.contains("rowsHex")) return fail(path, "记录分支需要rows或旧人工rowCount/rowsHex");
        Prot698SubItem bytes; bytes.type = "octet_string"; QVariant raw;
        if (!Prot698DataEncoder::validateJsonValue(bytes, obj["rowsHex"], &raw, err, path + ".rowsHex", false)) return false;
      }
    } else {
      if (!obj.contains("dataValue")) return fail(path + ".dataValue", "缺少必填字段");
      if (ai && obj.contains("dar")) return fail(path + ".dar", "Data分支不允许DAR");
      QVariant raw;
      if (!data(obj["oad"].toObject(), obj["dataValue"], false, "GET", path, &raw)) return false;
      obj["dataValue"] = Prot698DataEncoder::toExactJson(raw);
    }
    return true;
  }
};
}

bool Prot698ServiceSchema::validateJsonTreeValue(const QString &serviceCode,
    const QJsonObject &treeValue, QVariantMap *normalizedTree, QString *err, bool aiInput) {
  if (err) err->clear();
  const auto code = normalizeServiceCode(serviceCode);
  bool known = false;
  for (const auto &service : serviceTypes()) if (service.toMap()["code"].toString() == code) known = true;
  ServiceValidator v{err, aiInput};
  if (!known) return v.fail("$.serviceCode", "未知服务");
  int totalNodes = 0;
  qsizetype stringBytes = 0;
  std::function<bool(const QJsonValue &, const QString &, int)> budget;
  budget = [&](const QJsonValue &value, const QString &path, int depth) {
    if (++totalNodes > 8192 || depth > 40) return v.fail(path, "整个服务树深度或节点总数超过限制");
    if (value.isString()) {
      stringBytes += value.toString().toUtf8().size();
      if (stringBytes > 2 * 1024 * 1024) return v.fail(path, "整个服务树字符串总量超过限制");
    }
    if (value.isArray()) {
      const auto array = value.toArray();
      for (int i = 0; i < array.size(); ++i) if (!budget(array.at(i), path + "[" + QString::number(i) + "]", depth + 1)) return false;
    } else if (value.isObject()) {
      const auto object = value.toObject();
      for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key().size() > 128) return v.fail(path, "字段名过长");
        stringBytes += it.key().toUtf8().size();
        if (!budget(it.value(), path + "." + it.key(), depth + 1)) return false;
      }
    }
    return true;
  };
  if (!budget(treeValue, "$", 0)) return false;
  if (!v.shape(serviceInputSchema(code, aiInput), treeValue, "$")) return false;
  QJsonObject tree = treeValue;
  if (tree.contains("timeTag")) {
    auto tag = tree["timeTag"].toObject();
    if (tag["hasTimeTag"].toBool()) {
      QJsonObject date;
      for (const auto &key : {"year", "month", "day", "hour", "minute", "second"}) {
        if (!tag.contains(key)) return v.fail(QString("$.timeTag.") + key, "缺少必填字段");
        date[key] = tag[key];
      }
      if (!v.time(date, "$.timeTag")) return false;
      if (!tag.contains("tiUnit") || !tag.contains("tiValue")) return v.fail("$.timeTag", "缺少TI");
    } else if (aiInput && tag.size() != 1) return v.fail("$.timeTag", "无时间标签时禁止其他时间字段");
  }
  if (code == "GET_NORMAL" || code == "GET_RECORD") {
    if (!v.member(tree["oad"].toObject(), false, "GET", "$.oad")) return false;
  } else if (code == "GET_LIST" && !v.validateOads(tree["oads"].toArray(), "$.oads")) return false;
  if (code == "GET_RECORD") {
    auto rsd = tree["rsd"].toObject();
    if (!v.rsd(rsd, "$.rsd") || !v.rcsd(tree["rcsd"].toObject(), "$.rcsd")) return false;
    tree["rsd"] = rsd;
  }
  const auto writeItem = [&](QJsonObject &item, bool method, const QString &path) {
    QVariant raw;
    if (!v.data(item[method ? "omd" : "oad"].toObject(), item["dataValue"], method, method ? "ACTION" : "SET", path, &raw)) return false;
    item["dataValue"] = Prot698DataEncoder::toExactJson(raw); return true;
  };
  if (code == "SET_NORMAL" || code == "ACTION_NORMAL") {
    const QString key = code == "SET_NORMAL" ? "setItem" : "actionItem";
    auto item = tree[key].toObject(); if (!writeItem(item, code == "ACTION_NORMAL", "$." + key)) return false; tree[key] = item;
  }
  if (code == "SET_LIST" || code == "ACTION_LIST") {
    const QString key = code == "SET_LIST" ? "setItems" : "actionItems";
    auto items = tree[key].toArray();
    for (int i = 0; i < items.size(); ++i) {
      auto item = items.at(i).toObject(); if (!writeItem(item, code == "ACTION_LIST", "$." + key + "[" + QString::number(i) + "]")) return false; items[i] = item;
    }
    tree[key] = items;
  }
  const auto results = [&](QJsonObject &holder, const QString &key, int kind, bool list, const QString &path) {
    if (!list) { auto item = holder[key].toObject(); if (!v.result(item, kind, path + "." + key)) return false; holder[key] = item; return true; }
    auto array = holder[key].toArray();
    for (int i = 0; i < array.size(); ++i) { auto item = array.at(i).toObject(); if (!v.result(item, kind, path + "." + key + "[" + QString::number(i) + "]")) return false; array[i] = item; }
    holder[key] = array; return true;
  };
  if (code == "GET_RESPONSE_NORMAL" && !results(tree, "result", 0, false, "$")) return false;
  if (code == "GET_RESPONSE_LIST" && !results(tree, "results", 0, true, "$")) return false;
  if (code == "GET_RESPONSE_RECORD" && !results(tree, "recordResult", 1, false, "$")) return false;
  if (code == "SET_RESPONSE_NORMAL" && !results(tree, "setResult", 2, false, "$")) return false;
  if (code == "SET_RESPONSE_LIST" && !results(tree, "setResults", 2, true, "$")) return false;
  if (code == "ACTION_RESPONSE_NORMAL" && !results(tree, "actionResult", 3, false, "$")) return false;
  if (code == "ACTION_RESPONSE_LIST" && !results(tree, "actionResults", 3, true, "$")) return false;
  if (code == "GET_RESPONSE_NEXT") {
    auto next = tree["nextResult"].toObject(); const int choice = next["choice"].toInt();
    const QString key = choice == 0 ? "dar" : choice == 1 ? "normalResults" : "recordResults";
    if (!next.contains(key)) return v.fail("$.nextResult." + key, "缺少必填字段");
    if (aiInput) for (const auto &other : {QString("dar"), QString("normalResults"), QString("recordResults")}) if (other != key && next.contains(other)) return v.fail("$.nextResult." + other, "当前choice不允许该字段");
    if (choice && !results(next, key, choice == 1 ? 0 : 1, true, "$.nextResult")) return false;
    tree["nextResult"] = next;
  }
  if (code == "PROXY_GET_LIST" || code == "PROXY_GET_RESPONSE_LIST") {
    auto servers = tree["servers"].toArray();
    for (int i = 0; i < servers.size(); ++i) {
      auto server = servers.at(i).toObject(); const auto path = "$.servers[" + QString::number(i) + "]";
      if (server.contains("tsa") && server.contains("tsaHex")) return v.fail(path, "结构化tsa与旧tsaHex不能同时指定");
      if (server.contains("tsa")) {
        const auto hex = tsaFromDisplay(server["tsa"].toObject(), err);
        if (hex.isEmpty()) return v.fail(path + ".tsa", "结构化TSA地址编码失败");
        server["tsaHex"] = hex; server.remove("tsa");
      } else if (!server.contains("tsaHex")) return v.fail(path + ".tsa", "缺少必填字段tsa");
      Prot698SubItem tsa; tsa.type = "TSA"; QVariant raw;
      if (!Prot698DataEncoder::validateJsonValue(tsa, server["tsaHex"], &raw, err, path + ".tsaHex", aiInput)) return false;
      if (code == "PROXY_GET_LIST") { if (!v.validateOads(server["oads"].toArray(), path + ".oads")) return false; }
      else if (!results(server, "results", 0, true, path)) return false;
      servers[i] = server;
    }
    tree["servers"] = servers;
  }
  if (normalizedTree) *normalizedTree = tree.toVariantMap();
  return true;
}
