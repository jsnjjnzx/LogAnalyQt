#ifndef PROT101PARSER_H
#define PROT101PARSER_H

#include "Prot101Def.h"
#include "ProtocolDetector.h"
#include <QByteArray>
#include <QList>
#include <QString>


// 解析结果字段结构
struct Prot101Field {
  QString name;        // 字段名称
  QString value;       // 十六进制值
  QString description; // 解析描述
  int offset;          // 字节偏移量
  int length;          // 字节长度
  int indent;          // 缩进层级
};

class Prot101Parser {
public:
  Prot101Parser();

  // 静态方法：检查 IEC 101 帧是否有效（用于协议自动检测）
  // 进行严格的帧格式和校验验证
  static bool isValidFrame(const QByteArray &data);

  // 获取协议概要
  static ProtocolSummary getSummary(const QByteArray &data);

  // 设置链路地址长度 (1或2字节)
  void setLinkAddressLength(int len);
  // 设置公共地址长度 (1或2字节)
  void setCommonAddressLength(int len);
  // 设置传送原因长度 (1或2字节)
  void setCotLength(int len);
  // 设置信息对象地址长度 (1, 2或3字节)
  void setInfoObjAddrLength(int len);

  // 解析一帧IEC 101数据
  QList<Prot101Field> parse(const QByteArray &data);

private:
  int m_linkAddrLen;
  int m_commonAddrLen;
  int m_cotLen;
  int m_ioaLen;

  // 解析固定帧
  void parseFixedFrame(const QByteArray &data, QList<Prot101Field> &fields);
  // 解析可变帧
  void parseVariableFrame(const QByteArray &data, QList<Prot101Field> &fields);
  // 解析单字符
  void parseSingleChar(const QByteArray &data, QList<Prot101Field> &fields);

  // 解析控制域
  void parseControlField(uint8_t ctrl, int offset, QList<Prot101Field> &fields,
                         int indent);
  // 解析链路地址
  void parseLinkAddress(const QByteArray &data, int &offset,
                        QList<Prot101Field> &fields, int indent);
  // 解析ASDU
  void parseASDU(const QByteArray &data, int &offset,
                 QList<Prot101Field> &fields, int indent);

  // 解析信息对象
  void parseInformationObject(const QByteArray &data, int &offset,
                              QList<Prot101Field> &fields, int indent,
                              uint8_t typeId, bool sq, int num);

  // 辅助解析函数
  QString parseCP24Time2a(const QByteArray &data, int &offset);
  QString parseCP56Time2a(const QByteArray &data, int &offset);

  QString getTypeIdName(uint8_t typeId);
  QString getCauseOfTransmissionName(uint8_t cot);
  QString getFunctionCodeName(uint8_t func, bool prm);
  QString getSIQDescription(uint8_t siq);
  QString getDIQDescription(uint8_t diq);
  QString getQDSDescription(uint8_t qds);
  QString getSCODescription(uint8_t sco);
};

#endif // PROT101PARSER_H
