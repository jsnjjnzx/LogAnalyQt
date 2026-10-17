#pragma once
#include "AppMsgDef.h"
#include "ProtocolDetector.h"
#include <QByteArray>


namespace AppMsg {

class AppMsgParser {
public:
  AppMsgParser();

  // 静态方法：检查 AppMsg 帧是否有效（用于协议自动检测）
  // 进行基本的帧格式验证
  static bool isValidFrame(const QByteArray &data);

  // 获取协议概要
  static ProtocolSummary getSummary(const QByteArray &data);

  // 获取完整帧长度 (基于头部解析)
  static int getFullFrameSize(const QByteArray &data);

  // 解析为内部数据结构
  ParsedPacket parse(const QByteArray &data);

  // 解析为 UI 展示字段列表（与其他协议 Parser 接口一致）
  QList<AppMsgField> parseToFields(const QByteArray &data);

private:
  ParsedField parseFieldRecursive(const QByteArray &data, int &cursor,
                                  const FieldDef &def, int level);
  int parseAXDRLength(const QByteArray &data, int &cursor);
  QString parseString(const QByteArray &data, int &cursor); // Null terminated
  QString parseAXDRString(const QByteArray &data,
                          int &cursor); // Length prefixed
};

} // namespace AppMsg
