#ifndef PROT3762DEF_H
#define PROT3762DEF_H

#include <QList>
#include <QMap>
#include <QString>


// 376.2协议字段定义
struct Prot3762FieldDef {
  QString name; // 字段名称
  QString type; // 字段类型：BIN(二进制), BCD(BCD码), ASCII(ASCII字符串),
                // None(无数据), Blob(二进制块)
  int length;          // 字段长度（字节数），-1表示可变长度
  QString description; // 字段描述
  QString fieldId; // 字段唯一标识（可选，用于被其他字段引用）
  QString nestedProtocolRef; // 引用的规约类型字段ID（可选，用于嵌套协议解析）
  QString countRef; // 引用的数量字段ID（可选，字段重复次数由该字段决定）
};

// 376.2 Protocol Data Unit Identity (Fn)
struct Prot3762FnDef {
  QString name;
  QString type; // "Blob"(不解析), "Struct"(结构化解析), "None"(无数据)
  QString direction; // "up"(上行), "down"(下行), ""(双向通用)
  QList<Prot3762FieldDef> fields; // 字段定义列表（当type="Struct"时使用）
};

// Application Function (AFN)
struct Prot3762AfnDef {
  QString name;
  // Fn Code -> 定义（使用复合键：fnCode + direction）
  QMap<QString, Prot3762FnDef> fns; // key格式: "1" 或 "1_up" 或 "1_down"
};

// Result Item for Parser
struct Prot3762Field {
  QString name;
  QString value;
  QString description;
  int offset;
  int length;
  int indent = 0;
};

#endif // PROT3762DEF_H
