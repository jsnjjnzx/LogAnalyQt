#ifndef PROT645DEF_H
#define PROT645DEF_H

#include <QList>
#include <QString>

// 645协议字段定义 Result Item for Parser
struct Prot645Field {
  QString name;
  QString value;
  QString description;
  int offset;
  int length;
  int indent = 0;
};

// 前向声明
struct Prot645SubFieldDef;

// 子字段定义(用于负荷记录等复杂结构)
struct Prot645FieldDef {
  QString name;                        // 字段名称
  int length;                          // 字段长度(-1表示变长)
  QString format;                      // 数据格式
  QString separator;                   // 分隔符(用于变长字段,如"AA")
  QString expected;                    // 期望值(用于验证,如"A0A0")
  QList<Prot645SubFieldDef> subFields; // 嵌套子字段列表
};

// 嵌套子字段定义(用于字段内部的数据项)
struct Prot645SubFieldDef {
  QString name;      // 子字段名称
  int length;        // 子字段长度
  QString format;    // 数据格式(如"XXXXXX.XX")
  QString unit;      // 单位
  bool sign = false; // 是否带符号解析，默认不带符号
};

// 位域定义（用于 BitField 格式，如主动上报状态字）
struct Prot645BitDef {
  int     pos;  // 位编号 (0-based，从最低位 Bit0 开始)
  QString name; // 该位为 1 时的含义
};

// 数据项定义 (来自XML)
struct Prot645DataDef {
  QString code;                  // DI3 DI2 DI1 DI0 (Hex String)
  QString name;                  // 数据项名称
  int length;                    // 数据长度 (字节数,-1表示变长)
  QString format;                // 数据格式 (XXXXXX.XX / BitField / ...)
  QString unit;                  // 单位
  QString rw;                    // 读写权限
  bool sign = false;             // 是否带符号解析，默认不带符号
  QList<Prot645FieldDef> fields;    // 子字段列表(用于LoadRecord等复杂结构)
  QList<Prot645FieldDef> reqFields; // 请求子字段列表(可选)
  QList<Prot645FieldDef> rspFields; // 应答子字段列表(可选)
  QList<Prot645BitDef>   bitDefs;  // 位域定义列表(用于 BitField 格式)

  const QList<Prot645FieldDef> &getFieldsForDirection(bool isRequest) const {
    if (isRequest && !reqFields.isEmpty()) {
      return reqFields;
    }
    if (!isRequest && !rspFields.isEmpty()) {
      return rspFields;
    }
    return fields;
  }
};

#endif // PROT645DEF_H
