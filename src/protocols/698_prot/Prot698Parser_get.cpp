#include "Prot698Parser.h"

void Prot698Parser::parseGetRequest(const QByteArray &data, int &offset,
                                    QList<Prot698Field> &fields, int indent) {
  if (offset >= data.size())
    return;
  uint8_t tag = data[offset];
  QString tagName;
  switch (tag) {
  case 1:
    tagName = "GetRequestNormal";
    break;
  case 2:
    tagName = "GetRequestNormalList";
    break;
  case 3:
    tagName = "GetRequestRecord";
    break;
  case 4:
    tagName = "GetRequestRecordList";
    break;
  case 5:
    tagName = "GetRequestNext";
    break;
  case 6:
    tagName = "GetRequestMD5";
    break;
  case 22:
    tagName = "GetRequestConsistentRecord";
    break;
  case 23:
    tagName = "GetRequestSimplifyRecord";
    break;
  default:
    tagName = QString("Unknown(%1)").arg(tag);
    break;
  }
  fields.append({"GET-Request Type", QString::asprintf("%02X", tag), tagName,
                 offset, 1, indent});
  offset++;

  parsePIID(data, offset, fields, indent);

  switch (tag) {
  case 1: // GetRequestNormal: OAD
    parseOAD(data, offset, fields, indent, "GET");
    break;
  case 2: // GetRequestNormalList: SEQUENCE OF OAD
  {
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append(
          {"Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
      offset++;
      for (int i = 0; i < cnt; i++) {
        parseOAD(data, offset, fields, indent + 1, "GET");
      }
    }
  } break;
  case 3: // GetRequestRecord: GetRecord (OAD, RSD, RCSD)
    parseGetRecord(data, offset, fields, indent);
    break;
  case 4: // GetRequestRecordList: SEQUENCE OF GetRecord
  {
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append(
          {"Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
      offset++;
      for (int i = 0; i < cnt; i++) {
        parseGetRecord(data, offset, fields, indent + 1);
      }
    }
  } break;
  case 5: // GetRequestNext: last-block-number (long-unsigned)
    if (offset + 2 <= data.size()) {
      uint16_t num = ((uint8_t)data[offset] << 8) |
                     (uint8_t)data[offset + 1];
      // long-unsigned is 2 bytes usually.
      fields.append({"末次块序号", data.mid(offset, 2).toHex().toUpper(),
                     QString::number(num), offset, 2, indent});
      offset += 2;
    }
    break;
  case 6: // GetRequestMD5: OAD
    parseOAD(data, offset, fields, indent, "GET");
    break;
  case 22: // GetRequestConsistentRecord: GetConsistentRecord (OAD, RSD, RCSD)
  case 23: // GetRequestSimplifyRecord: GetConsistentRecord (OAD, RSD, RCSD)
    // Both use GetConsistentRecord structure which is OAD, RSD, RCSD
    parseOAD(data, offset, fields, indent, "GET");
    parseRSD(data, offset, fields, indent);
    parseRCSD(data, offset, fields, indent);
    break;
  }
}

void Prot698Parser::parseGetResponse(const QByteArray &data, int &offset,
                                     QList<Prot698Field> &fields, int indent) {
  if (offset >= data.size())
    return;
  uint8_t tag = data[offset];
  QString tagName;
  switch (tag) {
  case 1:
    tagName = "GetResponseNormal";
    break;
  case 2:
    tagName = "GetResponseNormalList";
    break;
  case 3:
    tagName = "GetResponseRecord";
    break;
  case 4:
    tagName = "GetResponseRecordList";
    break;
  case 5:
    tagName = "GetResponseNext";
    break;
  case 6:
    tagName = "GetResponseMD5";
    break;
  case 22:
    tagName = "GetResponseConsistentRecord";
    break;
  case 23:
    tagName = "GetResponseSimplifyRecord";
    break;
  default:
    tagName = QString("Unknown(%1)").arg(tag);
    break;
  }
  fields.append({"GET-Response Type", QString::asprintf("%02X", tag), tagName,
                 offset, 1, indent});
  offset++;

  parsePIID_ACD(data, offset, fields, indent);

  switch (tag) {
  case 1: // GetResponseNormal: A-ResultNormal
    parseAResultNormal(data, offset, fields, indent);
    break;
  case 2: // GetResponseNormalList: SEQUENCE OF A-ResultNormal
  {
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append(
          {"Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
      offset++;
      for (int i = 0; i < cnt; i++) {
        parseAResultNormal(data, offset, fields, indent + 1);
      }
    }
  } break;
  case 3: // GetResponseRecord: A-ResultRecord
    parseAResultRecord(data, offset, fields, indent);
    break;
  case 4: // GetResponseRecordList: SEQUENCE OF A-ResultRecord
  {
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append(
          {"Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
      offset++;
      for (int i = 0; i < cnt; i++) {
        parseAResultRecord(data, offset, fields, indent + 1);
      }
    }
  } break;
  case 5: // GetResponseNext: bool (end), long-unsigned (frame), CHOICE
    if (offset < data.size()) {
      bool isEnd = data[offset] != 0;
      fields.append({"末帧标志",
                     QString::asprintf("%02X", (uint8_t)data[offset]),
                     isEnd ? "True" : "False", offset, 1, indent});
      offset++;
    }
    if (offset + 2 <= data.size()) {
      uint16_t num = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      fields.append({"分帧序号", data.mid(offset, 2).toHex().toUpper(),
                     QString::number(num), offset, 2, indent});
      offset += 2;
    }
    // CHOICE: [0] DAR, [1] SeqOf A-ResultNormal, [2] SeqOf A-ResultRecord
    if (offset < data.size()) {
      uint8_t choice = data[offset];
      fields.append({"Response Data Choice", QString::asprintf("%02X", choice),
                     "Choice Tag", offset, 1, indent});
      offset++;
      if (choice == 0) {
        parseDAR(data, offset, fields, indent + 1);
      } else if (choice == 1) {
        // SeqOf A-ResultNormal
        if (offset < data.size()) {
          uint8_t cnt = data[offset];
          fields.append({"Count", QString::number(cnt), "Sequence Count",
                         offset, 1, indent + 1});
          offset++;
          for (int i = 0; i < cnt; i++) {
            parseAResultNormal(data, offset, fields, indent + 2);
          }
        }
      } else if (choice == 2) {
        // SeqOf A-ResultRecord
        if (offset < data.size()) {
          uint8_t cnt = data[offset];
          fields.append({"Count", QString::number(cnt), "Sequence Count",
                         offset, 1, indent + 1});
          offset++;
          for (int i = 0; i < cnt; i++) {
            parseAResultRecord(data, offset, fields, indent + 2);
          }
        }
      }
    }
    break;
  case 6: // GetResponseMD5: OAD, CHOICE { DAR, OctetString }
    parseOAD(data, offset, fields, indent, "GET");
    if (offset < data.size()) {
      uint8_t choice = data[offset];
      fields.append({"Result Choice", QString::asprintf("%02X", choice),
                     "Choice Tag", offset, 1, indent});
      offset++;
      if (choice == 0) {
        parseDAR(data, offset, fields, indent + 1);
      } else if (choice == 1) {
        // OctetString
        int len = decodeLength(data, offset);
        if (offset + len <= data.size()) {
          fields.append({"MD5", data.mid(offset, len).toHex().toUpper(),
                         "OctetString", offset, len, indent + 1});
          offset += len;
        }
      }
    }
    break;
  case 22: // GetResponseConsistentRecord: A-ResultConsistentRecord
    // A-ResultConsistentRecord: OAD, RSD, RCSD, CHOICE { DAR, SeqOf A-RecordRow
    // }
    parseOAD(data, offset, fields, indent, "GET");
    parseRSD(data, offset, fields, indent);
    {
      QList<Prot698AttrDef> oads;
      int colCount = parseRCSD(data, offset, fields, indent, &oads);
      if (offset < data.size()) {
        uint8_t choice = data[offset];
        fields.append({"Result Choice", QString::asprintf("%02X", choice),
                       "Choice Tag", offset, 1, indent});
        offset++;
        if (choice == 0) {
          parseDAR(data, offset, fields, indent + 1);
        } else if (choice == 1) {
          // SeqOf A-RecordRow
          if (offset < data.size()) {
            uint8_t cnt = data[offset]; // Number of rows
            fields.append({"Row Count", QString::number(cnt), "Sequence Count",
                           offset, 1, indent + 1});
            offset++;
            for (int i = 0; i < cnt; i++) {
              fields.append({"Row", QString::number(i + 1),
                             QString("Row %1 Data").arg(i + 1), offset, 0,
                             indent + 2});
              for (int j = 0; j < colCount; j++) {
                const Prot698SubItem *itemDef =
                    (j < oads.size()) ? &oads[j].dataDef : nullptr;
                parseData(data, offset, fields, indent + 3, itemDef);
              }
            }
          }
        }
      }
    }
    break;
  case 23: // GetResponseSimplifyRecord
    // A-ResultSimplifyRecord: OAD, RSD, RCSD, CHOICE { DAR, A-SimplifyRecord }
    parseAResultSimplifyRecord(data, offset, fields, indent);
    break;
  }
}

void Prot698Parser::parseGetRecord(const QByteArray &data, int &offset,
                                   QList<Prot698Field> &fields, int indent) {
  parseOAD(data, offset, fields, indent, "GET");
  parseRSD(data, offset, fields, indent);
  parseRCSD(data, offset, fields, indent);
}

void Prot698Parser::parseAResultNormal(const QByteArray &data, int &offset,
                                       QList<Prot698Field> &fields,
                                       int indent) {
  Prot698AttrDef *def = parseOAD(data, offset, fields, indent, "GET");
  parseGetResult(data, offset, fields, indent, def);
}

void Prot698Parser::parseAResultRecord(const QByteArray &data, int &offset,
                                       QList<Prot698Field> &fields,
                                       int indent) {
  parseOAD(data, offset, fields, indent, "GET");
  QList<Prot698AttrDef> oads;
  int colCount = parseRCSD(data, offset, fields, indent, &oads);

  if (offset < data.size()) {
    uint8_t choice = data[offset];
    fields.append({"Result Choice", QString::asprintf("%02X", choice),
                   choice == 1 ? "SequenceOfData" : "DAR", offset, 1, indent});
    offset++;
    if (choice == 1) {
      if (offset < data.size()) {
        uint8_t cnt = data[offset];
        fields.append({"Row Count", QString::number(cnt), "Data Count", offset,
                       1, indent + 1});
        offset++;
        for (int i = 0; i < cnt; i++) {
          fields.append({"Row", QString::number(i + 1),
                         QString("Row %1 Data").arg(i + 1), offset, 0,
                         indent + 2});
          for (int j = 0; j < colCount; j++) {
            const Prot698SubItem *itemDef =
                (j < oads.size()) ? &oads[j].dataDef : nullptr;
            parseData(data, offset, fields, indent + 3, itemDef);
          }
        }
      }
    } else {
      parseDAR(data, offset, fields, indent + 1);
    }
  }
}

void Prot698Parser::parseGetResult(const QByteArray &data, int &offset,
                                   QList<Prot698Field> &fields, int indent,
                                   const Prot698AttrDef *attrDef) {
  if (offset < data.size()) {
    uint8_t choice = data[offset];
    fields.append({"GetResult Choice", QString::asprintf("%02X", choice),
                   choice == 1 ? "Data" : "DAR", offset, 1, indent});
    offset++;
    if (choice == 1) {
      const Prot698SubItem *itemDef = attrDef ? &attrDef->dataDef : nullptr;
      parseData(data, offset, fields, indent + 1, itemDef);
    } else {
      parseDAR(data, offset, fields, indent + 1);
    }
  }
}

void Prot698Parser::parseAResultSimplifyRecord(const QByteArray &data,
                                               int &offset,
                                               QList<Prot698Field> &fields,
                                               int indent) {
  parseOAD(data, offset, fields, indent, "GET");
  parseRSD(data, offset, fields, indent);
  int innerColCount = parseRCSD(data, offset, fields, indent); // Outer RCSD
  if (offset < data.size()) {
    uint8_t choice = data[offset];
    fields.append({"Result Choice", QString::asprintf("%02X", choice),
                   choice == 1 ? "A-SimplifyRecord" : "DAR", offset, 1,
                   indent});
    offset++;
    if (choice == 0) {
      parseDAR(data, offset, fields, indent + 1);
    } else if (choice == 1) {
      // A-SimplifyRecord
      // SEQUENCE { OAD, RCSD, DefaultInfo(CHOICE), Rows(SEQUENCE OF) }
      fields.append({"A-SimplifyRecord", "", "Simplified Record Structure",
                     offset, 0, indent + 1});

      parseOAD(data, offset, fields, indent + 2, "GET"); // Inner OAD
      QList<Prot698AttrDef> oads;
      int recordColCount =
          parseRCSD(data, offset, fields, indent + 2, &oads); // Inner RCSD

      // Default Info: CHOICE { [0] DAR, [1] Data }
      if (offset < data.size()) {
        uint8_t defChoice = data[offset];
        fields.append({"Default Info Choice",
                       QString::asprintf("%02X", defChoice),
                       defChoice == 1 ? "Data" : "DAR", offset, 1, indent + 2});
        offset++;
        if (defChoice == 0) {
          parseDAR(data, offset, fields, indent + 3);
        } else if (defChoice == 1) {
          parseData(data, offset, fields, indent + 3);
        }
      }

      // Rows: SEQUENCE OF A-SimplifyRecordRow
      if (offset < data.size()) {
        uint8_t cnt = data[offset];
        fields.append({"Row Count", QString::number(cnt), "Sequence Count",
                       offset, 1, indent + 2});
        offset++;
        for (int i = 0; i < cnt; i++) {
          fields.append({"Row", QString::number(i + 1),
                         QString("Row %1 Data").arg(i + 1), offset, 0,
                         indent + 3});
          for (int j = 0; j < recordColCount; j++) {
            const Prot698SubItem *itemDef =
                (j < oads.size()) ? &oads[j].dataDef : nullptr;
            parseData(data, offset, fields, indent + 4, itemDef);
          }
        }
      }
    }
  }
}
