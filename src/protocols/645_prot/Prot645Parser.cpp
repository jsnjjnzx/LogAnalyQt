#include "Prot645Parser.h"
#include "Prot645ConfigManager.h"
#include "../../common/AppPaths.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTextStream>

// 日志辅助函数
static void writeLog(const QString &msg) {
  QString logDir = AppPaths::logDirectory();
  QDir().mkpath(logDir);
  QFile logFile(logDir + "/log.txt");
  if (logFile.open(QIODevice::WriteOnly | QIODevice::Append |
                   QIODevice::Text)) {
    QTextStream out(&logFile);
    out << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")
        << " " << msg << "\n";
    logFile.close();
  }
}

Prot645Parser::Prot645Parser() {}

bool Prot645Parser::isValidFrame(const QByteArray &data) {
  int len = data.size();
  // 最小长度：68(1) + A(6) + 68(1) + C(1) + L(1) + CS(1) + 16(1) = 12
  if (len < 12)
    return false;

  // 检查起始符（两个68）
  if ((uint8_t)data[0] != 0x68 || (uint8_t)data[7] != 0x68) {
    return false;
  }

  // 检查长度字段 L
  // L 在偏移量 9
  uint8_t L = (uint8_t)data[9];
  // 关键修改：在日志解析场景中，如果数据长度小于期望长度，
  // 说明帧不完整，应该拒绝
  int expectedLen = 12 + L;
  if (len < expectedLen) {
    return false;
  }

  // 重要修改：检查计算出的位置的结束符，而非数据末尾
  // 结束符应该在 offset = expectedLen - 1 的位置
  if ((uint8_t)data[expectedLen - 1] != 0x16) {
    return false;
  }

  // 校验 CS
  // CS 是从第一个 68 到数据域末尾的累加和
  // CS 位置在 expectedLen - 2
  uint8_t cs_calc = calculateCS(data, expectedLen - 2);
  uint8_t cs_recv = (uint8_t)data[expectedLen - 2];

  return cs_calc == cs_recv;
}

QList<Prot645Field> Prot645Parser::parse(const QByteArray &data) {
  QList<Prot645Field> fields;
  int totalLen = data.size();
  // 需要至少能读取到长度字段与CS位置
  if (totalLen < 12) {
    return fields;
  }

  int offset = 0;

  // 1. 起始符 (68H)
  fields.append(
      {"起始符",
       QString("%1").arg((uint8_t)data[offset], 2, 16, QChar('0')).toUpper(),
       "帧起始标志", offset, 1, 0});
  offset += 1;

  // 2. 地址域 (A0..A5)
  // 645协议中，地址域A0为低字节，A5为高字节。显示时通常习惯高位在前(A5...A0)。
  QString addrStr =
      bcdToString((const uint8_t *)data.mid(offset, 6).data(), 6, true);
  fields.append({"地址域", addrStr, "通信地址 (A5...A0)", offset, 6, 0});
  offset += 6;

  // 3. 起始符 (68H)
  fields.append(
      {"起始符",
       QString("%1").arg((uint8_t)data[offset], 2, 16, QChar('0')).toUpper(),
       "帧起始标志", offset, 1, 0});
  offset += 1;

  // 4. 控制码 (C)
  uint8_t C = (uint8_t)data[offset];
  QString cDesc;
  if ((C & 0x80) == 0)
    cDesc = "主站发出的命令帧";
  else
    cDesc = "从站发出的应答帧";
  // 可以添加更多控制码解析逻辑
  fields.append({"控制码", QString("%1").arg(C, 2, 16, QChar('0')).toUpper(),
                 cDesc, offset, 1, 0});
  offset += 1;

  // 5. 数据域长度 (L)
  uint8_t L = (uint8_t)data[offset];
  fields.append({"数据长度", QString::number(L),
                 QString("数据域长度: %1 字节").arg(L), offset, 1, 0});
  offset += 1;

  int expectedLen = 12 + L;
  bool hasFullFrame = (totalLen >= expectedLen);
  uint8_t func = (C & 0x1F);
  bool isRequest = ((C & 0x80) == 0);

  // 6. 数据域 (Data)
  if (L > 0) {
    QByteArray rawData = data.mid(offset, L);
    QByteArray procData;
    procData.resize(L);
    for (int i = 0; i < L; i++) {
      procData[i] = rawData[i] - 0x33;
    }

    QString rawHex = rawData.toHex().toUpper();
    QString procHex = procData.toHex().toUpper();

    fields.append({"数据域(原始)", rawHex,
             QString("未减0x33，减0x33: %1").arg(procHex), offset, L, 0});

    // 如果数据长度>=4，前4个字节是数据标识(DI)
    if (L >= 4) {
      // DI3 DI2 DI1 DI0 -> DI0 DI1 DI2 DI3 (645低位在前)
      QString diCode = bcdToString((const uint8_t *)procData.data(), 4, true);

      // 查找定义
      Prot645DataDef *def = Prot645ConfigManager::instance().getDataDef(diCode);
      QString diName = def ? def->name : "未知标识";
      QString diDesc = def ? QString("格式: %1, 单位: %2").arg(def->format).arg(def->unit) : "";
      QString fullDesc = diDesc.isEmpty() ? diName : QString("%1 (%2)").arg(diName).arg(diDesc);
      
      fields.append({"数据标识", diCode, fullDesc, offset, 4, 1});

      // 如果还有剩余数据 (L > 4)，则为数据内容
      if (L > 4) {
        int valLen = L - 4;
        int valOffset = offset + 4;

        // --- 逻辑分歧点：优先检查特殊业务结构 ---

        // 1. 负荷记录 (LoadRecord)
        const QList<Prot645FieldDef> &selectedFields = def ? def->getFieldsForDirection(isRequest) : QList<Prot645FieldDef>();
        if (def && def->format == "LoadRecord" && !selectedFields.isEmpty()) {
          QList<Prot645Field> loadRecordFields = parseLoadRecord((const uint8_t *)procData.mid(4).data(), valLen, def, isRequest, valOffset);
          fields.append(loadRecordFields);
        }
        // 2. 位域状态字 (BitField)
        else if (def && (def->format == "BitField" || def->format == "BitFieldCount")) {
          int statusLen = (def->length > 0) ? qMin(def->length, valLen) : qMin(12, valLen);
          QList<QPair<int, QString>> activeBits;
          for (int bitPos = 0; bitPos < statusLen * 8; bitPos++) {
            int byteIdx = bitPos / 8;
            int bitIdx  = bitPos % 8;
            if ((uint8_t)procData[4 + byteIdx] >> bitIdx & 0x01) {
              QString bitName;
              for (const Prot645BitDef &bd : def->bitDefs) {
                if (bd.pos == bitPos) { bitName = bd.name; break; }
              }
              if (bitName.isEmpty()) bitName = QString("Bit%1(保留)").arg(bitPos);
              activeBits.append({bitPos, bitName});
            }
          }
          QString statusHex = bcdToString((const uint8_t *)procData.mid(4, statusLen).data(), statusLen, false);
          QStringList activeNames;
          for (auto &ab : activeBits) activeNames << QString("Bit%1:%2").arg(ab.first).arg(ab.second);
          fields.append({"状态字", statusHex, activeNames.isEmpty() ? "无告警" : activeNames.join(" | "), valOffset, statusLen, 1});

          if (def->format == "BitFieldCount" && valLen > statusLen && !activeBits.isEmpty()) {
            const uint8_t *cntData = (const uint8_t *)procData.data() + (4 + statusLen);
            int cntLen = valLen - statusLen;
            int pos = 0;
            while (pos < cntLen && (uint8_t)cntData[pos] != 0xAA) pos++;
            if (pos < cntLen && (uint8_t)cntData[pos] == 0xAA) {
              int segStart = ++pos;
              while (pos < cntLen && (uint8_t)cntData[pos] != 0xAA) pos++;
              int segLen = pos - segStart;
              for (int i = 0; i < segLen && i < activeBits.size(); i++) {
                fields.append({QString("事件次数[Bit%1]").arg(activeBits[i].first),
                               QString("%1").arg(cntData[segStart + i], 2, 16, QChar('0')).toUpper(),
                               QString("%1 发生 %2 次").arg(activeBits[i].second).arg((uint8_t)cntData[segStart + i]),
                               valOffset + statusLen + segStart + i, 1, 2});
              }
            }
          }
        }
        // 3. 通用数据解析（额外识别 0x14 写请求和 0x03 安全认证请求的特殊结构）
        else {
          bool isWriteRequest = (func == 0x14 && isRequest);
          bool isAuthRequest = (func == 0x03 && isRequest);
          int dataStartOffset = 4;
          int actualValLen = valLen;

          if (isWriteRequest) {
            if (valLen >= 4) {
              fields.append({"密码权限及密码", bcdToString((const uint8_t *)procData.mid(4, 4).data(), 4, true), "PA + P0P1P2", valOffset, 4, 1});
            }
            if (valLen >= 8) {
              fields.append({"操作者代码", bcdToString((const uint8_t *)procData.mid(8, 4).data(), 4, true), "C0...C3", valOffset + 4, 4, 1});
            }
            dataStartOffset = 12;
            actualValLen = valLen - 8;
          } else if (isAuthRequest) {
            // 安全认证请求: DI(4) + 操作者代码(4) + DATA(m)
            if (valLen >= 4) {
              fields.append({"操作者代码", bcdToString((const uint8_t *)procData.mid(4, 4).data(), 4, true), "C0...C3", valOffset, 4, 1});
              dataStartOffset = 8;
              actualValLen = valLen - 4;
            }
          }

          if (actualValLen > 0) {
            // 修正：不再受限于 def->length，将报文中的所有剩余数据都视为“数据内容”
            int dataLen = actualValLen; 
            const uint8_t *valPtr = (const uint8_t *)procData.data() + dataStartOffset;
            QString valCode = bcdToString(valPtr, dataLen, true);
            
            QString valStr;
            if (isWriteRequest || isAuthRequest) {
                // 修正：0x14 和 0x03 的数据内容是加密后的密文+MAC，不能按数据项定义解析
                valStr = "密文 + MAC";
            } else {
                valStr = parseValue(valPtr, dataLen, 
                                    def ? def->format : "Hex", 
                                    def ? def->unit : "", 
                                    def ? def->sign : false);
            }
            
            fields.append({"数据内容", valCode, valStr, valOffset + (dataStartOffset - 4), dataLen, 1});
          }
        }
      }
    } else {
      fields.append({"数据域(处理)", procHex, "已减0x33", offset, L, 1});
    }

    offset += L;
  }

  // 7. 校验码 (CS)
  if (hasFullFrame && offset < totalLen) {
    uint8_t cs = (uint8_t)data[offset];
    uint8_t csCalc = calculateCS(data, offset);
    QString csDesc =
        (cs == csCalc)
            ? "OK"
            : QString("Fail (Calc: %1)")
                  .arg(QString("%1").arg(csCalc, 2, 16, QChar('0')).toUpper());
    fields.append({"校验码", QString("%1").arg(cs, 2, 16, QChar('0')).toUpper(),
                   csDesc, offset, 1, 0});
    offset += 1;

    // 8. 结束符 (16H)
    if (offset < totalLen) {
      QString endHex =
          QString("%1").arg((uint8_t)data[offset], 2, 16, QChar('0')).toUpper();
      QString endDesc = ((uint8_t)data[offset] == 0x16)
                            ? "帧结束标志"
                            : "Error: Not 16H";
      fields.append({"结束符", endHex, endDesc, offset, 1, 0});
    }
  } else {
    fields.append({"校验码", "--", "数据长度不足，无法校验", offset, 0, 0});
  }

  return fields;
}

QString Prot645Parser::parseValue(const uint8_t *data, int len,
                                  const QString &format, const QString &unit,
                                  bool sign) {
  // 全FF表示无效数据
  bool allFF = true;
  for (int i = 0; i < len; ++i) {
    if (data[i] != 0xFF) {
      allFF = false;
      break;
    }
  }
  if (allFF) {
    QString invalid = "无效数据";
    if (!unit.isEmpty()) {
      invalid += " " + unit;
    }
    return invalid;
  }

  // 1. 获取BCD字符串（默认反转，因为645低位在前）
  // 处理符号位
  bool isNegative = false;
  QString bcd;

  if (sign && len > 0) {
    // 检查最高字节最高位
    if (data[len - 1] & 0x80) {
      isNegative = true;
      // 复制数据去掉符号位
      QByteArray modData((const char *)data, len);
      modData[len - 1] = modData[len - 1] & 0x7F;
      bcd = bcdToString((const uint8_t *)modData.data(), len, true);
    } else {
      bcd = bcdToString(data, len, true);
    }
  } else {
    bcd = bcdToString(data, len, true);
  }

  // 2. 特殊格式处理：日期时间类型需要倒序解析
  // 645协议中日期时间数据采用倒序存储（低位在前）
  // 例如：YYMMDDWW 实际存储顺序是 WW DD MM YY

  // 处理 YYMMDDWWhhmmss 格式（7字节）
  if (format == "YYMMDDWWhhmmss" && len >= 7) {
    // 倒序读取: data[6]=YY, data[5]=MM, data[4]=DD, data[3]=WW, data[2]=hh,
    // data[1]=mm, data[0]=ss
    QString yy = QString("%1").arg(data[6], 2, 16, QChar('0')).toUpper();
    QString mm = QString("%1").arg(data[5], 2, 16, QChar('0')).toUpper();
    QString dd = QString("%1").arg(data[4], 2, 16, QChar('0')).toUpper();
    QString ww = QString("%1").arg(data[3], 2, 16, QChar('0')).toUpper();
    QString hh = QString("%1").arg(data[2], 2, 16, QChar('0')).toUpper();
    QString min = QString("%1").arg(data[1], 2, 16, QChar('0')).toUpper();
    QString ss = QString("%1").arg(data[0], 2, 16, QChar('0')).toUpper();
    QString result = QString("20%1-%2-%3 星期%4 %5:%6:%7")
                         .arg(yy)
                         .arg(mm)
                         .arg(dd)
                         .arg(ww)
                         .arg(hh)
                         .arg(min)
                         .arg(ss);
    if (!unit.isEmpty()) {
      result += " " + unit;
    }
    return result;
  }

  // 处理 YYMMDDhhmmss 格式（6字节）
  if (format == "YYMMDDhhmmss" && len >= 6) {
    // 倒序读取: data[5]=YY, data[4]=MM, data[3]=DD, data[2]=hh, data[1]=mm,
    // data[0]=ss
    QString yy = QString("%1").arg(data[5], 2, 16, QChar('0')).toUpper();
    QString mm = QString("%1").arg(data[4], 2, 16, QChar('0')).toUpper();
    QString dd = QString("%1").arg(data[3], 2, 16, QChar('0')).toUpper();
    QString hh = QString("%1").arg(data[2], 2, 16, QChar('0')).toUpper();
    QString min = QString("%1").arg(data[1], 2, 16, QChar('0')).toUpper();
    QString ss = QString("%1").arg(data[0], 2, 16, QChar('0')).toUpper();
    QString result = QString("20%1-%2-%3 %4:%5:%6")
                         .arg(yy)
                         .arg(mm)
                         .arg(dd)
                         .arg(hh)
                         .arg(min)
                         .arg(ss);
    if (!unit.isEmpty()) {
      result += " " + unit;
    }
    return result;
  }

  // 处理 YYMMDDhhmmNN 格式（6字节）
  if (format == "YYMMDDhhmmNN" && len >= 6) {
    // 倒序读取: data[5]=YY, data[4]=MM, data[3]=DD, data[2]=hh, data[1]=mm, data[0]=NN
    QString yy = QString("%1").arg(data[5], 2, 16, QChar('0')).toUpper();
    QString mm = QString("%1").arg(data[4], 2, 16, QChar('0')).toUpper();
    QString dd = QString("%1").arg(data[3], 2, 16, QChar('0')).toUpper();
    QString hh = QString("%1").arg(data[2], 2, 16, QChar('0')).toUpper();
    QString min = QString("%1").arg(data[1], 2, 16, QChar('0')).toUpper();
    QString nn = QString("%1").arg(data[0], 2, 16, QChar('0')).toUpper();
    QString result = QString("20%1-%2-%3 %4:%5 数量:%6")
                         .arg(yy)
                         .arg(mm)
                         .arg(dd)
                         .arg(hh)
                         .arg(min)
                         .arg(nn);
    if (!unit.isEmpty()) {
      result += " " + unit;
    }
    return result;
  }

  // 处理 YYMMDDhhmm 格式（5字节）
  if (format == "YYMMDDhhmm" && len >= 5) {
    // 倒序读取: data[4]=YY, data[3]=MM, data[2]=DD, data[1]=hh, data[0]=mm
    QString yy = QString("%1").arg(data[4], 2, 16, QChar('0')).toUpper();
    QString mm = QString("%1").arg(data[3], 2, 16, QChar('0')).toUpper();
    QString dd = QString("%1").arg(data[2], 2, 16, QChar('0')).toUpper();
    QString hh = QString("%1").arg(data[1], 2, 16, QChar('0')).toUpper();
    QString min = QString("%1").arg(data[0], 2, 16, QChar('0')).toUpper();
    QString result =
        QString("20%1-%2-%3 %4:%5").arg(yy).arg(mm).arg(dd).arg(hh).arg(min);
    if (!unit.isEmpty()) {
      result += " " + unit;
    }
    return result;
  }

  // 处理 YYMMDDWW 格式（4字节）
  if (format == "YYMMDDWW" && len >= 4) {
    // 倒序读取: data[3]=YY, data[2]=MM, data[1]=DD, data[0]=WW
    QString yy = QString("%1").arg(data[3], 2, 16, QChar('0')).toUpper();
    QString mm = QString("%1").arg(data[2], 2, 16, QChar('0')).toUpper();
    QString dd = QString("%1").arg(data[1], 2, 16, QChar('0')).toUpper();
    QString ww = QString("%1").arg(data[0], 2, 16, QChar('0')).toUpper();
    QString result =
        QString("20%1-%2-%3 星期%4").arg(yy).arg(mm).arg(dd).arg(ww);
    if (!unit.isEmpty()) {
      result += " " + unit;
    }
    return result;
  }

  // 处理 hhmmss 格式（3字节）
  if (format == "hhmmss" && len >= 3) {
    // 倒序读取: data[2]=hh, data[1]=mm, data[0]=ss
    QString hh = QString("%1").arg(data[2], 2, 16, QChar('0')).toUpper();
    QString min = QString("%1").arg(data[1], 2, 16, QChar('0')).toUpper();
    QString ss = QString("%1").arg(data[0], 2, 16, QChar('0')).toUpper();
    QString result = QString("%1:%2:%3").arg(hh).arg(min).arg(ss);
    if (!unit.isEmpty()) {
      result += " " + unit;
    }
    return result;
  }

  // 3. 根据格式插入小数点
  // 格式如: XXXXXX.XX
  int dotPosFromRight = 0;
  int dotIndex = format.indexOf('.');
  if (dotIndex != -1) {
    dotPosFromRight = format.length() - 1 - dotIndex;
  }

  // 如果是纯数字格式 X.X
  bool isNumeric = true;
  // 简单检查：如果格式里包含 Y, M, D, h, m, s, N，则可能不是简单的十进制数
  if (format.contains('Y') || format.contains('M') || format.contains('D') ||
      format.contains('h') || format.contains('m') || format.contains('s')) {
    isNumeric = false;
  }

  QString result = bcd;

  // 对于纯数字格式，插入小数点
  if (isNumeric && dotPosFromRight > 0) {
    if (dotPosFromRight < result.length()) {
      result.insert(result.length() - dotPosFromRight, '.');
    } else {
      // 补零?
      while (result.length() <= dotPosFromRight) {
        result.prepend('0');
      }
      result.insert(result.length() - dotPosFromRight, '.');
    }
  }

  // 4. 附加单位
  if (!unit.isEmpty()) {
    result += " " + unit;
  }

  // 5. 添加负号
  if (isNegative) {
    result.prepend("-");
  }

  return result;
}

ProtocolSummary Prot645Parser::getSummary(const QByteArray &data) {
  ProtocolSummary summary;
  if (!isValidFrame(data))
    return summary;

  // 地址: A5...A0
  summary.address =
      bcdToString((const uint8_t *)data.mid(1, 6).data(), 6, true);

  // 控制码
  uint8_t C = (uint8_t)data[8];
  if (C & 0x80) {
    summary.direction = "下行(从->主)";
  } else {
    summary.direction = "上行(主->从)";
  }

  // 根据功能码设置消息类型
  uint8_t func = C & 0x1F;
  switch (func) {
  case 0x03:
    summary.messageType = "安全认证";
    break;
  case 0x11:
    summary.messageType = "读数据";
    break;
  case 0x12:
    summary.messageType = "读后续数据";
    break;
  case 0x13:
    summary.messageType = "读通信地址";
    break;
  case 0x14:
    summary.messageType = "写数据";
    break;
  case 0x15:
    summary.messageType = "写通信地址";
    break;
  case 0x16:
    summary.messageType = "冻结命令";
    break;
  case 0x17:
    summary.messageType = "更改通信速率";
    break;
  case 0x18:
    summary.messageType = "修改密码";
    break;
  case 0x19:
    summary.messageType = "最大需量清零";
    break;
  case 0x1A:
    summary.messageType = "电表清零";
    break;
  case 0x1B:
    summary.messageType = "事件清零";
    break;
  // ... 其他可以扩展
  default:
    summary.messageType =
        QString("功能码 %1").arg(func, 2, 16, QChar('0')).toUpper();
    break;
  }

  if (C & 0x20)
    summary.messageType += " (后续帧)";
  if (C & 0x40)
    summary.messageType += " (异常应答)";

  // 帧序号无
  summary.frameSeq = "-";

  return summary;
}

uint8_t Prot645Parser::calculateCS(const QByteArray &data, int len) {
  uint8_t sum = 0;
  for (int i = 0; i < len; ++i) {
    sum += (uint8_t)data[i];
  }
  return sum;
}

QString Prot645Parser::bcdToString(const uint8_t *data, int len, bool reverse) {
  QString result;
  if (reverse) {
    for (int i = len - 1; i >= 0; --i) {
      result.append(QString("%1").arg(data[i], 2, 16, QChar('0')));
    }
  } else {
    for (int i = 0; i < len; ++i) {
      result.append(QString("%1").arg(data[i], 2, 16, QChar('0')));
    }
  }
  return result.toUpper();
}

// 解析负荷记录(支持Field子字段和分隔符)
QList<Prot645Field> Prot645Parser::parseLoadRecord(const uint8_t *data, int len,
                                                   const Prot645DataDef *def,
                                                   bool isRequest,
                                                   int baseOffset) {
  writeLog("=== parseLoadRecord called ===");

  QList<Prot645Field> fields;
  const QList<Prot645FieldDef> &fieldDefs =
      def ? def->getFieldsForDirection(isRequest)
          : QList<Prot645FieldDef>();

  if (!def || fieldDefs.isEmpty()) {
    writeLog(QString("No def or fields empty. def=%1, fields count=%2")
                 .arg(def ? "exists" : "null")
                 .arg(def ? fieldDefs.size() : 0));
    return fields;
  }

  writeLog(QString("Processing %1 fields for %2")
               .arg(fieldDefs.size())
               .arg(def->name));

  int offset = 0;

  // 遍历每个子字段
  for (const Prot645FieldDef &field : fieldDefs) {
    if (offset >= len) {
      break; // 数据已用完
    }

    QString fieldValue;
    QString fieldDesc = field.name;
    int fieldLen = 0;

    // 根据字段类型解析
    if (field.length > 0) {
      // 固定长度字段
      fieldLen = qMin(field.length, len - offset);

      QString rawHex = bcdToString(data + offset, fieldLen, false);
      QString parsedDesc = field.format;

      if (field.format == "Hex") {
        // 十六进制格式
        // 检查期望值
        if (!field.expected.isEmpty() && rawHex != field.expected.toUpper()) {
          parsedDesc += QString(" (期望:%1, 实际:%2)")
                            .arg(field.expected.toUpper())
                            .arg(rawHex);
        }
      } else if (field.format == "Dec") {
        // 十进制格式
        if (fieldLen > 0) {
          parsedDesc = QString("%1 (Dec)").arg(data[offset]);
        }
      } else if (field.format == "YYMMDDhhmm") {
        // 时间格式 - 倒序解析
        if (fieldLen >= 5) {
          // 倒序读取: data[offset+4]=YY, data[offset+3]=MM, ...
          QString yy =
              QString("%1").arg(data[offset + 4], 2, 16, QChar('0')).toUpper();
          QString monthStr =
              QString("%1").arg(data[offset + 3], 2, 16, QChar('0')).toUpper();
          QString dd =
              QString("%1").arg(data[offset + 2], 2, 16, QChar('0')).toUpper();
          QString hh =
              QString("%1").arg(data[offset + 1], 2, 16, QChar('0')).toUpper();
          QString minStr =
              QString("%1").arg(data[offset], 2, 16, QChar('0')).toUpper();
          parsedDesc = QString("20%1-%2-%3 %4:%5")
                           .arg(yy)
                           .arg(monthStr)
                           .arg(dd)
                           .arg(hh)
                           .arg(minStr);
        }
        } else if (field.format == "YYMMDDhhmmNN") {
        // 时间+数量格式 - 倒序解析
        if (fieldLen >= 6) {
          QString yy =
            QString("%1").arg(data[offset + 5], 2, 16, QChar('0')).toUpper();
          QString monthStr =
            QString("%1").arg(data[offset + 4], 2, 16, QChar('0')).toUpper();
          QString dd =
            QString("%1").arg(data[offset + 3], 2, 16, QChar('0')).toUpper();
          QString hh =
            QString("%1").arg(data[offset + 2], 2, 16, QChar('0')).toUpper();
          QString minStr =
            QString("%1").arg(data[offset + 1], 2, 16, QChar('0')).toUpper();
          QString nn =
            QString("%1").arg(data[offset], 2, 16, QChar('0')).toUpper();
          parsedDesc = QString("20%1-%2-%3 %4:%5 数量:%6")
                   .arg(yy)
                   .arg(monthStr)
                   .arg(dd)
                   .arg(hh)
                   .arg(minStr)
                   .arg(nn);
        }
      } else {
        // 其他格式, 默认已经在 rawHex 中
      }

      fields.append(
          {field.name, rawHex, parsedDesc, baseOffset + offset, fieldLen, 2});
      offset += fieldLen;

    } else {
      // 变长字段,需要查找分隔符
      if (!field.separator.isEmpty()) {
        // 将分隔符转换为字节
        uint8_t sep = field.separator.toUInt(nullptr, 16);

        // 查找分隔符位置
        int sepPos = -1;
        for (int i = offset; i < len; ++i) {
          if (data[i] == sep) {
            sepPos = i;
            break;
          }
        }

        if (sepPos >= 0) {
          fieldLen = sepPos - offset;

          // 保存当前字段名,用于分隔符标注
          QString currentFieldName = field.name;

          // 如果字段有子字段定义,则解析每个子字段
          if (!field.subFields.isEmpty() && fieldLen > 0) {
            writeLog(QString("Parsing %1 subfields for %2")
                         .arg(field.subFields.size())
                         .arg(field.name));

            QString groupValue = bcdToString(data + offset, fieldLen, false);
            // 先添加父字段作为容器
            fields.append({field.name, groupValue, "Group", baseOffset + offset,
                           fieldLen, 2});

            int subOffset = offset;
            for (const Prot645SubFieldDef &subField : field.subFields) {
              if (subOffset + subField.length <= offset + fieldLen) {
                // 解析子字段
                QString subHex =
                    bcdToString(data + subOffset, subField.length, false);
                QString subParsed =
                    parseValue(data + subOffset, subField.length,
                               subField.format, subField.unit, subField.sign);
                fields.append({subField.name, subHex, subParsed,
                               baseOffset + subOffset, subField.length, 3});
                subOffset += subField.length;
              }
            }
            offset += fieldLen;
          } else if (fieldLen > 0) {
            // 没有子字段定义,显示为Block
            fieldValue = bcdToString(data + offset, fieldLen, false);
            fieldDesc += QString(" (%1字节)").arg(fieldLen);
            fields.append({fieldDesc, fieldValue, "Block", baseOffset + offset,
                           fieldLen, 2});
            offset += fieldLen;
          }

          // 添加分隔符字段(使用之前保存的字段名)
          fieldValue =
              QString("%1").arg(data[sepPos], 2, 16, QChar('0')).toUpper();
          fields.append({QString("分隔符(%1)").arg(currentFieldName),
                         fieldValue, "Sep", baseOffset + sepPos, 1, 2});
          offset++; // 跳过分隔符
        } else {
          // 没找到分隔符,读取剩余所有数据
          fieldLen = len - offset;
          if (fieldLen > 0) {
            fieldValue = bcdToString(data + offset, fieldLen, false);
            fieldDesc += QString(" (剩余%1字节)").arg(fieldLen);
            fields.append({fieldDesc, fieldValue, "Block", baseOffset + offset,
                           fieldLen, 2});
            offset += fieldLen;
          }
        }
      }
    }
  }

  return fields;
}
