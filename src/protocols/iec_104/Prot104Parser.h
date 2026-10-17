#ifndef PROT104PARSER_H
#define PROT104PARSER_H

#include "Prot104Def.h"
#include "ProtocolDetector.h"
#include <QByteArray>
#include <QList>
#include <QString>


// 解析结果字段结构
struct Prot104Field {
  QString name;        // 字段名称
  QString value;       // 十六进制值
  QString description; // 解析描述
  int offset;          // 字节偏移量
  int length;          // 字节长度
  int indent;          // 缩进层级(用于显示层次结构)
};

class Prot104Parser {
public:
  Prot104Parser();

  // 静态方法：检查 IEC 104 帧是否有效（用于协议自动检测）
  // 进行严格的帧格式验证
  static bool isValidFrame(const QByteArray &data);

  // 获取协议概要
  static ProtocolSummary getSummary(const QByteArray &data);

  // 解析一帧IEC 104数据，返回解析结果列表
  // 如果解析失败或数据不完整，返回空列表或包含错误信息的列表
  QList<Prot104Field> parse(const QByteArray &data);

private:
  // 解析APCI (Application Protocol Control Information)
  void parseAPCI(const QByteArray &data, int &offset,
                 QList<Prot104Field> &fields, int indent);

  // 解析ASDU (Application Service Data Unit)
  void parseASDU(const QByteArray &data, int &offset,
                 QList<Prot104Field> &fields, int indent);

  // 解析信息对象
  void parseInformationObject(const QByteArray &data, int &offset,
                              QList<Prot104Field> &fields, int indent,
                              uint8_t typeId, bool sq, int num);

  // 解析时间标签
  QString parseCP24Time2a(const QByteArray &data, int &offset);
  QString parseCP56Time2a(const QByteArray &data, int &offset);

  // 辅助函数
  QString getTypeIdName(uint8_t typeId);
  QString getCauseOfTransmissionName(uint8_t cot);
  QString getUFuncName(uint8_t func);
  QString getSIQDescription(uint8_t siq);
  QString getDIQDescription(uint8_t diq);
  QString getQDSDescription(uint8_t qds);
  QString getSCODescription(uint8_t sco);

  // 获取发送序号(I帧)
  uint16_t getSendSeq(const Prot104ControlI &ctrl);

  // 获取接收序号(I帧或S帧)
  uint16_t getRecvSeq(const QByteArray &data, int offset);
};

#endif // PROT104PARSER_H
