#ifndef PROT3762PARSER_H
#define PROT3762PARSER_H

#include "Prot3762Def.h"
#include "ProtocolDetector.h"
#include <QByteArray>
#include <QList>
#include <cstdint>


/**
 * @brief DL/T 376.2协议解析器
 *
 * 本类实现了DL/T 376.2协议的帧解析功能，将原始字节数组解析为结构化的字段列表。
 *
 * 帧结构（按DL/T 376.2规范）：
 * 68H | L(2) | C(1) | R(6) | [A(0或12)] | AFN(1) | DT(2) | APDU(可变) | CS(1) |
 * 16H
 *
 * 关键特性：
 * - 信息域R：固定6字节，包含路由、通信模块标识等信息
 * - 地址域A：条件存在，根据R[0]的D2位（通信模块标识）决定是否存在
 *   长度 = 0字节（无地址域）或 12+6*中继级别字节（有地址域）
 * - 校验和CS：从控制域C开始计算（偏移量3），包括C、R、A、AFN、DT、APDU
 */
class Prot3762Parser {
public:
  Prot3762Parser();

  // 静态方法：检查 Q/GDW 376.2 帧是否有效（用于协议自动检测）
  // 进行严格的帧格式和校验和验证
  static bool isValidFrame(const QByteArray &data);

  // 获取协议概要
  static ProtocolSummary getSummary(const QByteArray &data);

  /**
   * @brief 解析DL/T 376.2协议帧
   *
   * @param data 原始帧数据（完整的字节数组，包含68H起始和16H结束）
   * @return 解析后的字段列表，如果帧无效则返回空列表
   *
   * 解析流程：
   * 1. 验证起始字符68H和最小帧长度（15字节）
   * 2. 提取长度字段L（2字节，表示总帧长度）
   * 3. 解析控制域C（1字节，包含DIR、PRM、功能码等）
   * 4. 解析信息域R（6字节固定长度，包含路由和通信模块标识）
   * 5. 根据R[0]的D2位（通信模块标识）条件解析地址域A
   *    - 当D2=0时：0字节（无地址域）
   *    - 当D2=1时：12+6*中继级别字节（源地址+中继地址+目的地址）
   * 6. 解析AFN（应用功能码，1字节）
   * 7. 解析DT（数据类型标识，2字节）
   * 8. 提取APDU应用数据（可变长度，从DT后到CS前）
   * 9. 验证校验和CS（从控制域C开始计算）
   * 10. 验证结束字符16H
   */
  QList<Prot3762Field> parse(const QByteArray &data);

private:
  /**
   * @brief 计算校验和
   *
   * @param data 原始数据
   * @param start 起始偏移量（通常为3，从C字段开始）
   * @param len 计算长度（从start到CS字段之前）
   * @return 计算得到的校验和（模256和）
   *
   * 校验和计算范围：C + R + A + AFN + DT + APDU
   */
  uint8_t calculateCS(const QByteArray &data, int start, int len);

  /**
   * @brief 获取应用功能码名称
   * @param afn 应用功能码
   * @return AFN名称
   */
  QString getAfnName(int afn);
};

#endif // PROT3762PARSER_H
