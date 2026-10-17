#include "Prot698Parser.h"

void Prot698Parser::parseSetRequest(const QByteArray &data, int &offset, QList<Prot698Field> &fields, int indent)
{
    if (offset >= data.size()) return;
    uint8_t tag = data[offset];
    QString tagName;
    switch(tag) {
        case 1: tagName = "SetRequestNormal"; break;
        case 2: tagName = "SetRequestNormalList"; break;
        case 3: tagName = "SetThenGetRequestNormalList"; break;
        default: tagName = QString("Unknown(%1)").arg(tag); break;
    }
    fields.append({"SET-Request Type", QString::asprintf("%02X", tag), tagName, offset, 1, indent});
    offset++;

    parsePIID(data, offset, fields, indent);

    switch(tag) {
        case 1: // SetRequestNormal: OAD, Data
            {
                Prot698AttrDef* def = parseOAD(data, offset, fields, indent, "SET");
                parseData(data, offset, fields, indent, (def) ? &def->dataDef : nullptr);
            }
            break;
        case 2: // SetRequestNormalList: SEQUENCE OF (OAD, Data)
            {
                if (offset < data.size()) {
                    uint8_t cnt = data[offset];
                    fields.append({"Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
                    offset++;
                    for(int i=0; i<cnt; i++) {
                        fields.append({QString("Item %1").arg(i+1), "", "", offset, 0, indent + 1});
                        Prot698AttrDef* def = parseOAD(data, offset, fields, indent + 2, "SET");
                        parseData(data, offset, fields, indent + 2, (def) ? &def->dataDef : nullptr);
                    }
                }
            }
            break;
        case 3: // SetThenGetRequestNormalList: SEQUENCE OF (OAD, Data), SEQUENCE OF OAD
            {
                // Set List
                if (offset < data.size()) {
                    uint8_t cnt = data[offset];
                    fields.append({"Set Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
                    offset++;
                    for(int i=0; i<cnt; i++) {
                        fields.append({QString("Set Item %1").arg(i+1), "", "", offset, 0, indent + 1});
                        Prot698AttrDef* def = parseOAD(data, offset, fields, indent + 2, "SET");
                        parseData(data, offset, fields, indent + 2, (def) ? &def->dataDef : nullptr);
                    }
                }
                // Get List
                if (offset < data.size()) {
                    uint8_t cnt = data[offset];
                    fields.append({"Get Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
                    offset++;
                    for(int i=0; i<cnt; i++) {
                        parseOAD(data, offset, fields, indent + 1, "GET");
                    }
                }
            }
            break;
    }

}

void Prot698Parser::parseSetResponse(const QByteArray &data, int &offset, QList<Prot698Field> &fields, int indent)
{
    if (offset >= data.size()) return;
    uint8_t tag = data[offset];
    QString tagName;
    switch(tag) {
        case 1: tagName = "SetResponseNormal"; break;
        case 2: tagName = "SetResponseNormalList"; break;
        case 3: tagName = "SetThenGetResponseNormalList"; break;
        default: tagName = QString("Unknown(%1)").arg(tag); break;
    }
    fields.append({"SET-Response Type", QString::asprintf("%02X", tag), tagName, offset, 1, indent});
    offset++;

    parsePIID(data, offset, fields, indent);

    switch(tag) {
        case 1: // SetResponseNormal: OAD, DAR
            parseOAD(data, offset, fields, indent, "SET");
            parseDAR(data, offset, fields, indent);
            break;
        case 2: // SetResponseNormalList: SEQUENCE OF (OAD, DAR)
            {
                if (offset < data.size()) {
                    uint8_t cnt = data[offset];
                    fields.append({"Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
                    offset++;
                    for(int i=0; i<cnt; i++) {
                        fields.append({QString("Item %1").arg(i+1), "", "", offset, 0, indent + 1});
                        parseOAD(data, offset, fields, indent + 2, "SET");
                        parseDAR(data, offset, fields, indent + 2);
                    }
                }
            }
            break;
        case 3: // SetThenGetResponseNormalList: SEQUENCE OF (OAD, DAR), SEQUENCE OF A-ResultNormal
            {
                // Set Result List
                if (offset < data.size()) {
                    uint8_t cnt = data[offset];
                    fields.append({"Set Result Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
                    offset++;
                    for(int i=0; i<cnt; i++) {
                        fields.append({QString("Set Result Item %1").arg(i+1), "", "", offset, 0, indent + 1});
                        parseOAD(data, offset, fields, indent + 2, "SET");
                        parseDAR(data, offset, fields, indent + 2);
                    }
                }
                // Get Result List
                if (offset < data.size()) {
                    uint8_t cnt = data[offset];
                    fields.append({"Get Result Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
                    offset++;
                    for(int i=0; i<cnt; i++) {
                        fields.append({QString("Get Result Item %1").arg(i+1), "", "", offset, 0, indent + 1});
                        // A-ResultNormal: OAD, GetResult
                        Prot698AttrDef* def = parseOAD(data, offset, fields, indent + 2, "GET");
                        parseGetResult(data, offset, fields, indent + 2, def);
                    }
                }
            }
            break;
    }

}
