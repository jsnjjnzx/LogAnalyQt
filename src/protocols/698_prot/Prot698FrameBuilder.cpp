#include "Prot698FrameBuilder.h"
#include "Prot698Parser.h"
#include <QRegularExpression>
#include <QMetaType>
#include <cmath>

QByteArray
Prot698FrameBuilder::physicalAddressToSaBytes(const QString &physicalAddressHex) {
  QString cleanHex = physicalAddressHex;
  cleanHex.remove(' ');
  cleanHex.remove(':');
  if (cleanHex.size() % 2 || !QRegularExpression("^[0-9a-fA-F]*$").match(cleanHex).hasMatch()) return {};
  QByteArray displayBytes = QByteArray::fromHex(cleanHex.toLatin1());
  QByteArray serverAddr;
  for (int byteIndex = displayBytes.size() - 1; byteIndex >= 0; --byteIndex)
    serverAddr.append(displayBytes.at(byteIndex));
  return serverAddr;
}

FrameOptions Prot698FrameBuilder::defaultClientRequestOptions(
    const QString &physicalAddressHex) {
  FrameOptions options;
  options.serverAddr = physicalAddressToSaBytes(physicalAddressHex);
  if (options.serverAddr.isEmpty()) {
    if (physicalAddressHex.trimmed().isEmpty()) options.serverAddr = QByteArray(6, '\0');
    else options.validationError = "物理地址不是有效十六进制";
  }
  return options;
}

FrameOptions Prot698FrameBuilder::optionsFromMap(const QVariantMap &map, QString *err) {
  if (err) err->clear();
  FrameOptions options;
  const auto fail = [&](const QString &message) {
    options.validationError = message;
    if (err) *err = message;
  };
  const QStringList keys{"dir", "prm", "split", "scramble", "funcCode", "saType",
      "logicAddress", "ca", "serverAddrHex", "serverAddr", "physicalAddressHex"};
  for (auto it = map.cbegin(); it != map.cend(); ++it) {
    if (!keys.contains(it.key())) { fail("frameOptions." + it.key() + ": 未知字段"); return options; }
  }
  for (const auto &key : {"dir", "prm", "split", "scramble"}) {
    if (map.contains(key) && map.value(key).metaType().id() != QMetaType::Bool) {
      fail(QString("frameOptions.") + key + ": 必须是bool"); return options;
    }
  }
  options.dir = map.value("dir", false).toBool();
  options.prm = map.value("prm", true).toBool();
  options.split = map.value("split", false).toBool();
  options.scramble = map.value("scramble", false).toBool();
  const auto number = [&](const QString &key, int fallback, int max, int *result) {
    if (!map.contains(key)) { *result = fallback; return true; }
    const auto value = map.value(key);
    const auto id = value.metaType().id();
    if (id == QMetaType::Bool || id == QMetaType::QString || !value.canConvert<double>()) return false;
    const double n = value.toDouble();
    if (!std::isfinite(n) || std::floor(n) != n || n < 0 || n > max) return false;
    *result = static_cast<int>(n); return true;
  };
  if (!number("funcCode", 3, 7, &options.funcCode) || !number("saType", 0, 3, &options.saType) ||
      !number("logicAddress", 0, 1, &options.logicAddress) || !number("ca", 0, 255, &options.ca)) {
    fail("frameOptions: 数值类型、范围或逻辑地址不受支持"); return options;
  }
  const bool physical = map.contains("physicalAddressHex");
  if ((physical && (map.contains("serverAddrHex") || map.contains("serverAddr"))) ||
      (map.contains("serverAddrHex") && map.contains("serverAddr"))) {
    fail("frameOptions: 显示地址和线序地址只能指定一个"); return options;
  }
  const auto address = map.value(physical ? "physicalAddressHex" : map.contains("serverAddrHex") ? "serverAddrHex" : "serverAddr");
  if (!address.isValid()) return options;
  if (address.metaType().id() != QMetaType::QString) { fail("frameOptions: 地址必须是Hex字符串"); return options; }
  QString hex = address.toString(); hex.remove(' '); hex.remove(':');
  if (hex.isEmpty() || hex.size() % 2 || !QRegularExpression("^[0-9a-fA-F]+$").match(hex).hasMatch()) {
    fail("frameOptions: 地址不是有效Hex"); return options;
  }
  options.serverAddr = physical ? physicalAddressToSaBytes(hex) : QByteArray::fromHex(hex.toLatin1());
  return options;
}

QByteArray Prot698FrameBuilder::buildFrame(const QByteArray &apdu,
                                           const FrameOptions &opt,
                                           QString *err) {
  if (err) err->clear();
  if (!opt.validationError.isEmpty()) { if (err) *err = opt.validationError; return {}; }
  if (opt.split || opt.scramble) { if (err) *err = "尚不支持分帧或加扰组帧"; return {}; }
  if (opt.funcCode < 0 || opt.funcCode > 7 || opt.saType < 0 || opt.saType > 3 ||
      opt.logicAddress < 0 || opt.logicAddress > 1 || opt.ca < 0 || opt.ca > 255) {
    if (err) *err = "帧头字段范围不合法"; return {};
  }
  const auto tag = apdu.isEmpty() ? 0 : static_cast<quint8>(apdu.front());
  if ((tag == 5 || tag == 6 || tag == 7 || tag == 9 || tag == 0x85 || tag == 0x86 || tag == 0x87 || tag == 0x89) && opt.funcCode != 3) {
    if (err) *err = "普通应用数据服务功能码必须为3"; return {};
  }
  if (apdu.isEmpty()) {
    if (err)
      *err = "APDU 不能为空";
    return {};
  }
  if (opt.serverAddr.isEmpty() || opt.serverAddr.size() > 16) {
    if (err)
      *err = "服务器地址长度必须为 1-16 字节";
    return {};
  }

  const qsizetype declaredLength = 9 + opt.serverAddr.size() + apdu.size();
  if (declaredLength > 0x3fff) { if (err) *err = "完整帧长度超过14位字节长度上限"; return {}; }
  int addrLen = opt.serverAddr.size();
  quint8 saInd = static_cast<quint8>((addrLen - 1) & 0x0F);
  saInd |= static_cast<quint8>((opt.logicAddress & 0x01) << 4);
  saInd |= static_cast<quint8>((opt.saType & 0x03) << 6);

  quint8 control = 0;
  if (opt.dir)
    control |= 0x80;
  if (opt.prm)
    control |= 0x40;
  if (opt.split)
    control |= 0x20;
  if (opt.scramble)
    control |= 0x08;
  control |= static_cast<quint8>(opt.funcCode & 0x07);

  int apduLen = apdu.size();
  int lengthValue = 9 + addrLen + apduLen;
  int frameSize = lengthValue + 2;
  QByteArray frame(frameSize, '\0');

  frame[0] = static_cast<char>(0x68);
  frame[1] = static_cast<char>(lengthValue & 0xFF);
  frame[2] = static_cast<char>((lengthValue >> 8) & 0x3F);
  frame[3] = static_cast<char>(control);
  frame[4] = static_cast<char>(saInd);
  for (int addrIndex = 0; addrIndex < addrLen; ++addrIndex)
    frame[5 + addrIndex] = opt.serverAddr.at(addrIndex);
  frame[5 + addrLen] = static_cast<char>(opt.ca & 0xFF);

  quint16 hcs = Prot698Parser::calcCRC16(
      reinterpret_cast<const quint8 *>(frame.constData()) + 1, 5 + addrLen);
  frame[6 + addrLen] = static_cast<char>(hcs & 0xFF);
  frame[7 + addrLen] = static_cast<char>((hcs >> 8) & 0xFF);

  int apduStart = 8 + addrLen;
  for (int apduIndex = 0; apduIndex < apduLen; ++apduIndex)
    frame[apduStart + apduIndex] = apdu.at(apduIndex);

  quint16 fcs = Prot698Parser::calcCRC16(
      reinterpret_cast<const quint8 *>(frame.constData()) + 1,
      apduStart + apduLen - 1);
  frame[apduStart + apduLen] = static_cast<char>(fcs & 0xFF);
  frame[apduStart + apduLen + 1] = static_cast<char>((fcs >> 8) & 0xFF);
  frame[frameSize - 1] = static_cast<char>(0x16);
  return frame;
}