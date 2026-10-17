#pragma once
#include "ProtocolDetector.h"
#include "ModuleMsgDef.h"
#include <QByteArray>

namespace ModuleMsg {

class ModuleMsgParser {
public:
  ModuleMsgParser();

  // 静态方法：检查 ModuleMsg 帧是否有效（用于协议自动检测）
  // 进行严格的帧格式和 FCS 校验验证
  static bool isValidFrame(const QByteArray &data);

  // 获取协议概要
  static ProtocolSummary getSummary(const QByteArray &data);

  ParsedPacket parse(const QByteArray &data);

  // 解析为 UI 展示字段列表（与其他协议 Parser 接口一致）
  QList<ModuleMsgField> parseToFields(const QByteArray &data);

private:
  ParsedField parseFieldRecursive(const QByteArray &data, int &cursor,
                                  const FieldDef &def, int level,
                                  int baseOffset, bool expectTag = false);
  int parseAXDRLength(const QByteArray &data, int &cursor);
  QString parseString(const QByteArray &data, int &cursor); // Null terminated
  QString parseAXDRString(const QByteArray &data,
                          int &cursor); // Length prefixed
};

} // namespace ModuleMsg
