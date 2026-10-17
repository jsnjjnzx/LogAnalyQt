#ifndef PROT645PARSER_H
#define PROT645PARSER_H

#include "Prot645Def.h"
#include <QByteArray>
#include <QList>

/**
 * @brief DL/T 645-2007 协议解析器
 *
 * 帧结构：
 * 68H | A0..A5 | 68H | C | L | D0..Dm | CS | 16H
 *
 * 关键特性：
 * - 传输时，数据域(D0..Dm)字节 + 0x33
 * - CS = (68H + A0..A5 + 68H + C + L + D0..Dm) % 256
 */
#include "ProtocolDetector.h"

// ...

class Prot645Parser {
public:
  Prot645Parser();

  // 静态方法：检查 DL/T 645 帧是否有效（用于协议自动检测）
  static bool isValidFrame(const QByteArray &data);

  // 静态方法：获取协议概要信息
  static ProtocolSummary getSummary(const QByteArray &data);

  /**
   * @brief 解析 DL/T 645 协议帧
   * @param data 原始帧数据（完整的字节数组）
   * @return 解析后的字段列表
   */
  QList<Prot645Field> parse(const QByteArray &data);

private:
  /**
   * @brief 计算校验和
   * 计算范围：从第一个 68H 开始，到 CS 前结束
   */
  static uint8_t calculateCS(const QByteArray &data, int len);
  static QString bcdToString(const uint8_t *data, int len,
                             bool reverse = false);
  static QString parseValue(const uint8_t *data, int len, const QString &format,
                            const QString &unit, bool sign = false);

  // 解析负荷记录(支持Field子字段)
  static QList<Prot645Field> parseLoadRecord(const uint8_t *data, int len,
                                             const Prot645DataDef *def,
                                             bool isRequest, int baseOffset);
};

#endif // PROT645PARSER_H
