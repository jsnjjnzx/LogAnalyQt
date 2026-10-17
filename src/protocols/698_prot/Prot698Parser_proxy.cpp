
#include "Prot698Parser.h"

void Prot698Parser::parseCOMDCB(const QByteArray &data, int &offset,
                                QList<Prot698Field> &fields, int indent) {
  if (offset + 5 <= data.size()) {
    QByteArray dcb = data.mid(offset, 5);
    uint8_t baud = dcb[0];
    uint8_t parity = dcb[1];
    uint8_t databits = dcb[2];
    uint8_t stopbits = dcb[3];
    uint8_t flowctrl = dcb[4];

    QString desc = QString("Baud:%1, Parity:%2, Data:%3, Stop:%4, Flow:%5")
                       .arg(baud)
                       .arg(parity)
                       .arg(databits)
                       .arg(stopbits)
                       .arg(flowctrl);

    fields.append({"COMDCB", dcb.toHex().toUpper(), desc, offset, 5, indent});
    offset += 5;
  }
}

void Prot698Parser::parseProxyRequest(const QByteArray &data, int &offset,
                                      QList<Prot698Field> &fields, int indent) {
  if (offset >= data.size())
    return;
  uint8_t tag = data[offset];
  QString tagName;
  switch (tag) {
  case 1:
    tagName = "ProxyGetRequestList";
    break;
  case 2:
    tagName = "ProxyGetRequestRecord";
    break;
  case 3:
    tagName = "ProxySetRequestList";
    break;
  case 4:
    tagName = "ProxySetThenGetRequestList";
    break;
  case 5:
    tagName = "ProxyActionRequestList";
    break;
  case 6:
    tagName = "ProxyActionThenGetRequestList";
    break;
  case 7:
    tagName = "ProxyTransCommandRequest";
    break;
  case 8:
    tagName = "ProxyInnerTransCommandRequest";
    break;
  default:
    tagName = QString("Unknown(%1)").arg(tag);
    break;
  }
  fields.append({"PROXY-Request Type", QString::asprintf("%02X", tag), tagName,
                 offset, 1, indent});
  offset++;

  parsePIID(data, offset, fields, indent);

  // TimeOut (long-unsigned) for whole request
  if (tag != 7 && tag != 8) { // TransCommand doesn't have global timeout in
                              // table 100/101? Wait, check image.
    // Table 93: ProxyRequest::CHOICE
    // [1]..[6] have TimeOut.
    // [7] ProxyTransCommandRequest (Table 100) -> PIID, COMDCB, SEQUENCE OF...
    // No global timeout. [8] ProxyInnerTransCommandRequest (Table 101) -> PIID,
    // COMDCB, SEQUENCE OF... No global timeout.
    if (offset + 2 <= data.size()) {
      uint16_t timeout =
          ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      fields.append({"Total TimeOut", QString::number(timeout), "Seconds",
                     offset, 2, indent});
      offset += 2;
    }
  }

  switch (tag) {
  case 1: // ProxyGetRequestList
  {
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append({"Target Server Count", QString::number(cnt),
                     "Sequence Count", offset, 1, indent});
      offset++;
      for (int i = 0; i < cnt; i++) {
        fields.append({QString("Target Server %1").arg(i + 1), "", "", offset,
                       0, indent + 1});
        parseTSA(data, offset, fields, indent + 2);
        // TimeOut
        if (offset + 2 <= data.size()) {
          uint16_t to =
              ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
          fields.append({"Server TimeOut", QString::number(to), "Seconds",
                         offset, 2, indent + 2});
          offset += 2;
        }
        // OAD List
        if (offset < data.size()) {
          uint8_t oadCnt = data[offset];
          fields.append({"OAD Count", QString::number(oadCnt), "Sequence Count",
                         offset, 1, indent + 2});
          offset++;
          for (int j = 0; j < oadCnt; j++) {
            parseOAD(data, offset, fields, indent + 3, "GET");
          }
        }
      }
    }
  } break;
  case 2: // ProxyGetRequestRecord
  {
    parseTSA(data, offset, fields, indent + 1);
    // TimeOut
    if (offset + 2 <= data.size()) {
      uint16_t to = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      fields.append({"Server TimeOut", QString::number(to), "Seconds", offset,
                     2, indent + 1});
      offset += 2;
    }
    parseOAD(data, offset, fields, indent + 1, "GET");
    parseRSD(data, offset, fields, indent + 1);
    parseRCSD(data, offset, fields, indent + 1);
  } break;
  case 3: // ProxySetRequestList
  {
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append({"Target Server Count", QString::number(cnt),
                     "Sequence Count", offset, 1, indent});
      offset++;
      for (int i = 0; i < cnt; i++) {
        fields.append({QString("Target Server %1").arg(i + 1), "", "", offset,
                       0, indent + 1});
        parseTSA(data, offset, fields, indent + 2);
        // TimeOut
        if (offset + 2 <= data.size()) {
          uint16_t to =
              ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
          fields.append({"Server TimeOut", QString::number(to), "Seconds",
                         offset, 2, indent + 2});
          offset += 2;
        }
        // Set List
        if (offset < data.size()) {
          uint8_t setCnt = data[offset];
          fields.append({"Set Count", QString::number(setCnt), "Sequence Count",
                         offset, 1, indent + 2});
          offset++;
          for (int j = 0; j < setCnt; j++) {
            Prot698AttrDef *def =
                parseOAD(data, offset, fields, indent + 3, "SET");
            parseData(data, offset, fields, indent + 3,
                      (def) ? &def->dataDef : nullptr);
          }
        }
      }
    }
  } break;
  case 4: // ProxySetThenGetRequestList
  {
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append({"Target Server Count", QString::number(cnt),
                     "Sequence Count", offset, 1, indent});
      offset++;
      for (int i = 0; i < cnt; i++) {
        fields.append({QString("Target Server %1").arg(i + 1), "", "", offset,
                       0, indent + 1});
        parseTSA(data, offset, fields, indent + 2);
        // TimeOut
        if (offset + 2 <= data.size()) {
          uint16_t to =
              ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
          fields.append({"Server TimeOut", QString::number(to), "Seconds",
                         offset, 2, indent + 2});
          offset += 2;
        }
        // Set List
        if (offset < data.size()) {
          uint8_t setCnt = data[offset];
          fields.append({"Set Count", QString::number(setCnt), "Sequence Count",
                         offset, 1, indent + 2});
          offset++;
          for (int j = 0; j < setCnt; j++) {
            Prot698AttrDef *def =
                parseOAD(data, offset, fields, indent + 3, "SET");
            parseData(data, offset, fields, indent + 3,
                      (def) ? &def->dataDef : nullptr);
          }
        }
        // Get List
        if (offset < data.size()) {
          uint8_t getCnt = data[offset];
          fields.append({"Get Count", QString::number(getCnt), "Sequence Count",
                         offset, 1, indent + 2});
          offset++;
          for (int j = 0; j < getCnt; j++) {
            parseOAD(data, offset, fields, indent + 3, "GET");
            // Delay (unsigned)
            if (offset < data.size()) {
              uint8_t delay = data[offset];
              fields.append({"Delay", QString::number(delay), "Seconds", offset,
                             1, indent + 3});
              offset++;
            }
          }
        }
      }
    }
  } break;
  case 5: // ProxyActionRequestList
  {
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append({"Target Server Count", QString::number(cnt),
                     "Sequence Count", offset, 1, indent});
      offset++;
      for (int i = 0; i < cnt; i++) {
        fields.append({QString("Target Server %1").arg(i + 1), "", "", offset,
                       0, indent + 1});
        parseTSA(data, offset, fields, indent + 2);
        // TimeOut
        if (offset + 2 <= data.size()) {
          uint16_t to =
              ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
          fields.append({"Server TimeOut", QString::number(to), "Seconds",
                         offset, 2, indent + 2});
          offset += 2;
        }
        // Action List
        if (offset < data.size()) {
          uint8_t actCnt = data[offset];
          fields.append({"Action Count", QString::number(actCnt),
                         "Sequence Count", offset, 1, indent + 2});
          offset++;
          for (int j = 0; j < actCnt; j++) {
            Prot698AttrDef *def = parseOMD(data, offset, fields, indent + 3);
            const Prot698SubItem *itemDef = def ? &def->dataDef : nullptr;
            parseData(data, offset, fields, indent + 3, itemDef);
          }
        }
      }
    }
  } break;
  case 6: // ProxyActionThenGetRequestList
  {
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append({"Target Server Count", QString::number(cnt),
                     "Sequence Count", offset, 1, indent});
      offset++;
      for (int i = 0; i < cnt; i++) {
        fields.append({QString("Target Server %1").arg(i + 1), "", "", offset,
                       0, indent + 1});
        parseTSA(data, offset, fields, indent + 2);
        // TimeOut
        if (offset + 2 <= data.size()) {
          uint16_t to =
              ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
          fields.append({"Server TimeOut", QString::number(to), "Seconds",
                         offset, 2, indent + 2});
          offset += 2;
        }
        // Action List
        if (offset < data.size()) {
          uint8_t actCnt = data[offset];
          fields.append({"Action Count", QString::number(actCnt),
                         "Sequence Count", offset, 1, indent + 2});
          offset++;
          for (int j = 0; j < actCnt; j++) {
            Prot698AttrDef *def = parseOMD(data, offset, fields, indent + 3);
            const Prot698SubItem *itemDef = def ? &def->dataDef : nullptr;
            parseData(data, offset, fields, indent + 3, itemDef);
          }
        }
        // Get List
        if (offset < data.size()) {
          uint8_t getCnt = data[offset];
          fields.append({"Get Count", QString::number(getCnt), "Sequence Count",
                         offset, 1, indent + 2});
          offset++;
          for (int j = 0; j < getCnt; j++) {
            parseOAD(data, offset, fields, indent + 3, "GET");
            // Delay (unsigned)
            if (offset < data.size()) {
              uint8_t delay = data[offset];
              fields.append({"Delay", QString::number(delay), "Seconds", offset,
                             1, indent + 3});
              offset++;
            }
          }
        }
      }
    }
  } break;
  case 7: // ProxyTransCommandRequest
  {
    // 按协议表100: PIID, OAD, COMDCB, 接收等待报文超时(秒),
    // 接收等待字节超时(毫秒), 转发命令
    parseOAD(data, offset, fields, indent + 1, ""); // 转发端口对象描述符
    parseCOMDCB(data, offset, fields, indent + 1); // 端口通信控制块
    // 接收等待报文超时时间(秒) - long-unsigned
    if (offset + 2 <= data.size()) {
      uint16_t timeout =
          ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      fields.append({"接收等待报文超时时间", QString::number(timeout), "秒",
                     offset, 2, indent + 1});
      offset += 2;
    }
    // 接收等待字节超时时间(毫秒) - long-unsigned
    if (offset + 2 <= data.size()) {
      uint16_t byteTimeout =
          ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      fields.append({"接收等待字节超时时间", QString::number(byteTimeout),
                     "毫秒", offset, 2, indent + 1});
      offset += 2;
    }
    // 转发至目标端口的命令 - octet-string
    {
      int len = decodeLength(data, offset);
      if (offset + len <= data.size()) {
        fields.append({"转发命令", data.mid(offset, len).toHex().toUpper(),
                       "octet-string", offset, len, indent + 1});
        offset += len;
      }
    }
  } break;
  case 8: // ProxyInnerTransCommandRequest
  {
    // Tag 8 可能有不同结构，保留原有循环逻辑
    parseCOMDCB(data, offset, fields, indent + 1);
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append({"Trans Item Count", QString::number(cnt), "Sequence Count",
                     offset, 1, indent});
      offset++;
      for (int i = 0; i < cnt; i++) {
        fields.append({QString("Trans Item %1").arg(i + 1), "", "", offset, 0,
                       indent + 1});
        parseOAD(data, offset, fields, indent + 2); // Port OAD
        // Baud (long-unsigned)
        if (offset + 2 <= data.size()) {
          uint16_t baud =
              ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
          fields.append({"Baud Rate", QString::number(baud), "bps", offset, 2,
                         indent + 2});
          offset += 2;
        }
        // Delay (long-unsigned)
        if (offset + 2 <= data.size()) {
          uint16_t delay =
              ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
          fields.append(
              {"Delay", QString::number(delay), "ms", offset, 2, indent + 2});
          offset += 2;
        }
        // Data (octet-string)
        int len = decodeLength(data, offset);
        if (offset + len <= data.size()) {
          fields.append({"Trans Data", data.mid(offset, len).toHex().toUpper(),
                         "octet-string", offset, len, indent + 2});
          offset += len;
        }
      }
    }
  } break;
  }
}

void Prot698Parser::parseProxyResponse(const QByteArray &data, int &offset,
                                       QList<Prot698Field> &fields,
                                       int indent) {
  if (offset >= data.size())
    return;
  uint8_t tag = data[offset];
  QString tagName;
  switch (tag) {
  case 1:
    tagName = "ProxyGetResponseList";
    break;
  case 2:
    tagName = "ProxyGetResponseRecord";
    break;
  case 3:
    tagName = "ProxySetResponseList";
    break;
  case 4:
    tagName = "ProxySetThenGetResponseList";
    break;
  case 5:
    tagName = "ProxyActionResponseList";
    break;
  case 6:
    tagName = "ProxyActionThenGetResponseList";
    break;
  case 7:
    tagName = "ProxyTransCommandResponse";
    break;
  case 8:
    tagName = "ProxyInnerTransCommandResponse";
    break;
  default:
    tagName = QString("Unknown(%1)").arg(tag);
    break;
  }
  fields.append({"PROXY-Response Type", QString::asprintf("%02X", tag), tagName,
                 offset, 1, indent});
  offset++;

  parsePIID_ACD(data, offset, fields, indent);

  switch (tag) {
  case 1: // ProxyGetResponseList
  {
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append({"Target Server Count", QString::number(cnt),
                     "Sequence Count", offset, 1, indent});
      offset++;
      for (int i = 0; i < cnt; i++) {
        fields.append({QString("Target Server %1").arg(i + 1), "", "", offset,
                       0, indent + 1});
        parseTSA(data, offset, fields, indent + 2);
        // Result List
        if (offset < data.size()) {
          uint8_t resCnt = data[offset];
          fields.append({"Result Count", QString::number(resCnt),
                         "Sequence Count", offset, 1, indent + 2});
          offset++;
          for (int j = 0; j < resCnt; j++) {
            parseAResultNormal(data, offset, fields, indent + 3);
          }
        }
      }
    }
  } break;
  case 2: // ProxyGetResponseRecord
  {
    parseTSA(data, offset, fields, indent + 1);
    parseAResultRecord(data, offset, fields, indent + 1);
  } break;
  case 3: // ProxySetResponseList
  {
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append({"Target Server Count", QString::number(cnt),
                     "Sequence Count", offset, 1, indent});
      offset++;
      for (int i = 0; i < cnt; i++) {
        fields.append({QString("Target Server %1").arg(i + 1), "", "", offset,
                       0, indent + 1});
        parseTSA(data, offset, fields, indent + 2);
        // Result List
        if (offset < data.size()) {
          uint8_t resCnt = data[offset];
          fields.append({"Result Count", QString::number(resCnt),
                         "Sequence Count", offset, 1, indent + 2});
          offset++;
          for (int j = 0; j < resCnt; j++) {
            parseOAD(data, offset, fields, indent + 3, "SET");
            parseDAR(data, offset, fields, indent + 3);
          }
        }
      }
    }
  } break;
  case 4: // ProxySetThenGetResponseList
  {
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append({"Target Server Count", QString::number(cnt),
                     "Sequence Count", offset, 1, indent});
      offset++;
      for (int i = 0; i < cnt; i++) {
        fields.append({QString("Target Server %1").arg(i + 1), "", "", offset,
                       0, indent + 1});
        parseTSA(data, offset, fields, indent + 2);
        // Set Result List
        if (offset < data.size()) {
          uint8_t setCnt = data[offset];
          fields.append({"Set Result Count", QString::number(setCnt),
                         "Sequence Count", offset, 1, indent + 2});
          offset++;
          for (int j = 0; j < setCnt; j++) {
            parseOAD(data, offset, fields, indent + 3, "SET");
            parseDAR(data, offset, fields, indent + 3);
          }
        }
        // Get Result List
        if (offset < data.size()) {
          uint8_t getCnt = data[offset];
          fields.append({"Get Result Count", QString::number(getCnt),
                         "Sequence Count", offset, 1, indent + 2});
          offset++;
          for (int j = 0; j < getCnt; j++) {
            parseAResultNormal(data, offset, fields, indent + 3);
          }
        }
      }
    }
  } break;
  case 5: // ProxyActionResponseList
  {
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append({"Target Server Count", QString::number(cnt),
                     "Sequence Count", offset, 1, indent});
      offset++;
      for (int i = 0; i < cnt; i++) {
        fields.append({QString("Target Server %1").arg(i + 1), "", "", offset,
                       0, indent + 1});
        parseTSA(data, offset, fields, indent + 2);
        // Action Result List
        if (offset < data.size()) {
          uint8_t actCnt = data[offset];
          fields.append({"Action Result Count", QString::number(actCnt),
                         "Sequence Count", offset, 1, indent + 2});
          offset++;
          for (int j = 0; j < actCnt; j++) {
            Prot698AttrDef *def = parseOMD(data, offset, fields, indent + 3);
            parseDAR(data, offset, fields, indent + 3);
            // Data OPTIONAL
            if (offset < data.size()) {
              uint8_t hasData = data[offset];
              fields.append({"Has Data", QString::number(hasData),
                             hasData ? "Present" : "Absent", offset, 1,
                             indent + 3});
              offset++;
              if (hasData) {
                const Prot698SubItem *itemDef = def ? &def->dataDef : nullptr;
                parseData(data, offset, fields, indent + 3, itemDef);
              }
            }
          }
        }
      }
    }
  } break;
  case 6: // ProxyActionThenGetResponseList
  {
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append({"Target Server Count", QString::number(cnt),
                     "Sequence Count", offset, 1, indent});
      offset++;
      for (int i = 0; i < cnt; i++) {
        fields.append({QString("Target Server %1").arg(i + 1), "", "", offset,
                       0, indent + 1});
        parseTSA(data, offset, fields, indent + 2);
        // Action Result List
        if (offset < data.size()) {
          uint8_t actCnt = data[offset];
          fields.append({"Action Result Count", QString::number(actCnt),
                         "Sequence Count", offset, 1, indent + 2});
          offset++;
          for (int j = 0; j < actCnt; j++) {
            Prot698AttrDef *def = parseOMD(data, offset, fields, indent + 3);
            parseDAR(data, offset, fields, indent + 3);
            // Data OPTIONAL
            if (offset < data.size()) {
              uint8_t hasData = data[offset];
              fields.append({"Has Data", QString::number(hasData),
                             hasData ? "Present" : "Absent", offset, 1,
                             indent + 3});
              offset++;
              if (hasData) {
                const Prot698SubItem *itemDef = def ? &def->dataDef : nullptr;
                parseData(data, offset, fields, indent + 3, itemDef);
              }
            }
          }
        }
        // Get Result List
        if (offset < data.size()) {
          uint8_t getCnt = data[offset];
          fields.append({"Get Result Count", QString::number(getCnt),
                         "Sequence Count", offset, 1, indent + 2});
          offset++;
          for (int j = 0; j < getCnt; j++) {
            parseAResultNormal(data, offset, fields, indent + 3);
          }
        }
      }
    }
  } break;
  case 7: // ProxyTransCommandResponse
  {
    parseOAD(data, offset, fields, indent + 1); // Port OAD
    if (offset < data.size()) {
      uint8_t choice = data[offset];
      fields.append({"Trans Result Choice", QString::asprintf("%02X", choice),
                     choice == 1 ? "octet-string" : "DAR", offset, 1,
                     indent + 1});
      offset++;
      if (choice == 0) {
        parseDAR(data, offset, fields, indent + 2);
      } else if (choice == 1) {
        int len = decodeLength(data, offset);
        if (offset + len <= data.size()) {
          fields.append({"Trans Data", data.mid(offset, len).toHex().toUpper(),
                         "octet-string", offset, len, indent + 2});
          offset += len;
        }
      }
    }
  } break;
  case 8: // ProxyInnerTransCommandResponse
  {
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append({"Trans Result Count", QString::number(cnt),
                     "Sequence Count", offset, 1, indent});
      offset++;
      for (int i = 0; i < cnt; i++) {
        fields.append({QString("Trans Result Item %1").arg(i + 1), "", "",
                       offset, 0, indent + 1});
        parseOAD(data, offset, fields, indent + 2); // Port OAD
        if (offset < data.size()) {
          uint8_t choice = data[offset];
          fields.append(
              {"Trans Result Choice", QString::asprintf("%02X", choice),
               choice == 1 ? "octet-string" : "DAR", offset, 1, indent + 2});
          offset++;
          if (choice == 0) {
            parseDAR(data, offset, fields, indent + 3);
          } else if (choice == 1) {
            int len = decodeLength(data, offset);
            if (offset + len <= data.size()) {
              fields.append({"Trans Data",
                             data.mid(offset, len).toHex().toUpper(),
                             "octet-string", offset, len, indent + 3});
              offset += len;
            }
          }
        }
      }
    }
  } break;
  }
}
