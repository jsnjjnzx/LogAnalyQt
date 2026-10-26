#ifndef PROT698PARSER_H
#define PROT698PARSER_H

#include "Prot698Def.h"
#include "Prot698OadManager.h"
#include "ProtocolDetector.h"
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>


struct Prot698Field {
  QString name;
  QString value;
  QString description;
  int offset;
  int length;
  int indent = 0;
  QString summary;
  QString detail;

  Prot698Field() {}
  Prot698Field(const QString &n, const QString &v, const QString &d, int o,
               int l, int i = 0)
      : name(n), value(v), description(d), offset(o), length(l), indent(i) {}
};

// 解析结果结构，包含字段列表和提取的地址信息
struct Prot698ParseResult {
  QList<Prot698Field> fields; // 解析的字段列表
  QString physicalAddress;    // 提取的物理地址（BCD格式）
  QString logicAddress;       // 提取的逻辑地址
  bool isValid = false;       // 完整帧边界与CRC有效；不代表APDU属于AI组帧服务
};

class Prot698Parser {
public:
  Prot698Parser();

  // 日志前缀检测：校验首帧边界与HCS/FCS，允许其后仍有字节。
  static bool isValidFrame(const QByteArray &data);
  // AI组帧验收：精确单帧及受支持APDU完整消费。
  static bool validateSingleFrame(const QByteArray &data,
                                   QString *err = nullptr,
                                   bool validateApdu = true);
  static bool validateApdu(const QByteArray &data, QString *err = nullptr,
                           int *consumed = nullptr);

  // 静态方法：获取协议概要信息（用于全局预览）
  static ProtocolSummary getSummary(const QByteArray &data);

  // 解析一帧数据，返回解析结果列表
  // 如果解析失败或数据不完整，返回空列表或包含错误信息的列表
  QList<Prot698Field> parse(const QByteArray &data);

  // 解析一帧数据，返回包含地址信息的完整结果
  // 会自动从解析结果中提取物理地址和逻辑地址
  Prot698ParseResult parseWithAddress(const QByteArray &data);

  // 解析 APDU (Public for standalone usage)
  void parseApdu(const QByteArray &data, int &offset,
                 QList<Prot698Field> &fields, int indent);

  // 静态 CRC16 计算方法（公开，供 isValidFrame 使用）
  static quint16 calcCRC16(const quint8 *data, int len);

private:
  int m_dataDepth = 0;
  QString getFuncCodeDesc(quint8 funcCode);
  QString getServiceClassDesc(quint8 sc);

  // 解析 Data 类型
  void parseData(const QByteArray &data, int &offset,
                 QList<Prot698Field> &fields, int indent,
                 const Prot698SubItem *def = nullptr);
  QString getDataTypeName(Prot698DataType type);
  int decodeLength(const QByteArray &data, int &offset);

  // 解析 APDU
  QString getApduTagName(Prot698ApduTag tag);

  // Conformance Parsers
  void parseProtocolConformance(const QString &name, const QByteArray &data,
                                int &offset, QList<Prot698Field> &fields,
                                int indent, int fixedBitLen = -1);
  void parseFunctionConformance(const QString &name, const QByteArray &data,
                                int &offset, QList<Prot698Field> &fields,
                                int indent, int fixedBitLen = -1);

  // Helper
  void parseConnectMechanismInfo(const QByteArray &data, int &offset,
                                 QList<Prot698Field> &fields, int indent);
  void parseTimeTag(const QByteArray &data, int &offset,
                    QList<Prot698Field> &fields, int indent);
  void parseFollowReport(const QByteArray &data, int &offset,
                         QList<Prot698Field> &fields, int indent);

  // GET Service Parsers
  void parseGetRequest(const QByteArray &data, int &offset,
                       QList<Prot698Field> &fields, int indent);
  void parseGetResponse(const QByteArray &data, int &offset,
                        QList<Prot698Field> &fields, int indent);

  // SET Service Parsers
  void parseSetRequest(const QByteArray &data, int &offset,
                       QList<Prot698Field> &fields, int indent);
  void parseSetResponse(const QByteArray &data, int &offset,
                        QList<Prot698Field> &fields, int indent);

  // ACTION Service Parsers
  void parseActionRequest(const QByteArray &data, int &offset,
                          QList<Prot698Field> &fields, int indent);
  void parseActionResponse(const QByteArray &data, int &offset,
                           QList<Prot698Field> &fields, int indent);

  // REPORT Service Parsers
  void parseReportNotification(const QByteArray &data, int &offset,
                               QList<Prot698Field> &fields, int indent);
  void parseReportResponse(const QByteArray &data, int &offset,
                           QList<Prot698Field> &fields, int indent);

  // PROXY Service Parsers
  void parseProxyRequest(const QByteArray &data, int &offset,
                         QList<Prot698Field> &fields, int indent);
  void parseProxyResponse(const QByteArray &data, int &offset,
                          QList<Prot698Field> &fields, int indent);

  // SECURITY Service Parsers
  void parseSecurityRequest(const QByteArray &data, int &offset,
                            QList<Prot698Field> &fields, int indent);
  void parseSecurityResponse(const QByteArray &data, int &offset,
                             QList<Prot698Field> &fields, int indent);

  // Sub-structures for GET
  Prot698AttrDef *parseOAD(const QByteArray &data, int &offset,
                           QList<Prot698Field> &fields, int indent,
                           const QString &expectedOp = QString());
  Prot698AttrDef *parseOMD(const QByteArray &data, int &offset,
                           QList<Prot698Field> &fields, int indent);
  void parseCOMDCB(const QByteArray &data, int &offset,
                   QList<Prot698Field> &fields, int indent);
  void parseRSD(const QByteArray &data, int &offset,
                QList<Prot698Field> &fields, int indent);
  int parseRCSD(const QByteArray &data, int &offset,
                QList<Prot698Field> &fields, int indent,
                QList<Prot698AttrDef> *outOads = nullptr);
  int parseCSD(const QByteArray &data, int &offset, QList<Prot698Field> &fields,
               int indent, QList<Prot698AttrDef> *outOads = nullptr);
  int parseROAD(const QByteArray &data, int &offset,
                QList<Prot698Field> &fields, int indent,
                QList<Prot698AttrDef> *outOads = nullptr);
  void parseRegion(const QByteArray &data, int &offset,
                   QList<Prot698Field> &fields, int indent);
  void parseMS(const QByteArray &data, int &offset, QList<Prot698Field> &fields,
               int indent);
  void parseTI(const QByteArray &data, int &offset, QList<Prot698Field> &fields,
               int indent, const QString &name = "TI");
  void parseTSA(const QByteArray &data, int &offset,
                QList<Prot698Field> &fields, int indent);
  void parsePIID(const QByteArray &data, int &offset,
                 QList<Prot698Field> &fields, int indent);
  void parsePIID_ACD(const QByteArray &data, int &offset,
                     QList<Prot698Field> &fields, int indent);
  void parseDAR(const QByteArray &data, int &offset,
                QList<Prot698Field> &fields, int indent);
  void parseGetResult(const QByteArray &data, int &offset,
                      QList<Prot698Field> &fields, int indent,
                      const Prot698AttrDef *attrDef = nullptr);
  void parseAResultNormal(const QByteArray &data, int &offset,
                          QList<Prot698Field> &fields, int indent);
  void parseAResultRecord(const QByteArray &data, int &offset,
                          QList<Prot698Field> &fields, int indent);
  void parseAResultSimplifyRecord(const QByteArray &data, int &offset,
                                  QList<Prot698Field> &fields, int indent);
  void parseGetRecord(const QByteArray &data, int &offset,
                      QList<Prot698Field> &fields, int indent);

private:
  // Raw Type Readers (No Tag)
  QString readDateTime(const QByteArray &data, int &offset);
  QString readDateTimeS(const QByteArray &data, int &offset);
  QString readDate(const QByteArray &data, int &offset);
  QString readTime(const QByteArray &data, int &offset);
  QString readPIID(const QByteArray &data, int &offset);
};

#endif // PROT698PARSER_H
