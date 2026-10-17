#include "Prot698Parser.h"
#include "Prot698OadManager.h"

Prot698AttrDef* Prot698Parser::parseOMD(const QByteArray &data, int &offset, QList<Prot698Field> &fields, int indent)
{
    Prot698AttrDef* def = nullptr;
    if (offset + 4 <= data.size()) {
        QByteArray omd = data.mid(offset, 4);
        // OMD: OI(2) + MethodId(1) + Mode(1)
        uint16_t oi = (uint8_t)omd[0] << 8 | (uint8_t)omd[1];
        uint8_t methodId = (uint8_t)omd[2];
        uint8_t mode = (uint8_t)omd[3];

        QString oiStr = QString("%1").arg(oi, 4, 16, QChar('0')).toUpper();
        QString desc = QString("OI:%1, Method:%2, Mode:%3").arg(oiStr).arg(methodId).arg(mode);

        // Lookup
        def = Prot698OadManager::instance().getMethodDef(oiStr, methodId);
        if (def) {
            // Check OP
            QString expectedOp = "ACTION";
            QStringList ops = def->op.split('|');
            if (!ops.contains(expectedOp)) {
                    desc += " [OP Mismatch: Expected " + expectedOp + ", Found " + def->op + "]";
            }

            Prot698ObjDef* obj = Prot698OadManager::instance().getObjDef(oiStr);
            if (obj) {
                desc = obj->name + " - " + def->name + " (" + desc + ")";
            } else {
                desc += " (" + def->name + ")";
            }
        } else {
             Prot698ObjDef* obj = Prot698OadManager::instance().getObjDef(oiStr);
             if (obj) {
                 desc = obj->name + " (" + desc + ")";
             }
        }

        fields.append({"OMD", omd.toHex().toUpper(), desc, offset, 4, indent});
        offset += 4;
    }
    return def;
}

void Prot698Parser::parseActionRequest(const QByteArray &data, int &offset, QList<Prot698Field> &fields, int indent)
{
    if (offset >= data.size()) return;
    uint8_t tag = data[offset];
    QString tagName;
    switch(tag) {
        case 1: tagName = "ActionRequestNormal"; break;
        case 2: tagName = "ActionRequestNormalList"; break;
        case 3: tagName = "ActionThenGetRequestNormalList"; break;
        default: tagName = QString("Unknown(%1)").arg(tag); break;
    }
    fields.append({"ACTION-Request Type", QString::asprintf("%02X", tag), tagName, offset, 1, indent});
    offset++;

    parsePIID(data, offset, fields, indent);

    switch(tag) {
        case 1: // ActionRequestNormal: OMD, Data
            {
                Prot698AttrDef* def = parseOMD(data, offset, fields, indent);
                const Prot698SubItem* itemDef = def ? &def->dataDef : nullptr;
                parseData(data, offset, fields, indent, itemDef);
            }
            break;
        case 2: // ActionRequestNormalList: SEQUENCE OF (OMD, Data)
            {
                if (offset < data.size()) {
                    uint8_t cnt = data[offset];
                    fields.append({"Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
                    offset++;
                    for(int i=0; i<cnt; i++) {
                        fields.append({QString("Item %1").arg(i+1), "", "", offset, 0, indent + 1});
                        Prot698AttrDef* def = parseOMD(data, offset, fields, indent + 2);
                        const Prot698SubItem* itemDef = def ? &def->dataDef : nullptr;
                        parseData(data, offset, fields, indent + 2, itemDef);
                    }
                }
            }
            break;
        case 3: // ActionThenGetRequestNormalList: SEQUENCE OF (OMD, Data), SEQUENCE OF (OAD, unsigned)
            {
                // Action List
                if (offset < data.size()) {
                    uint8_t cnt = data[offset];
                    fields.append({"Action Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
                    offset++;
                    for(int i=0; i<cnt; i++) {
                        fields.append({QString("Action Item %1").arg(i+1), "", "", offset, 0, indent + 1});
                        Prot698AttrDef* def = parseOMD(data, offset, fields, indent + 2);
                        const Prot698SubItem* itemDef = def ? &def->dataDef : nullptr;
                        parseData(data, offset, fields, indent + 2, itemDef);
                    }
                }
                // Get List
                if (offset < data.size()) {
                    uint8_t cnt = data[offset];
                    fields.append({"Get Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
                    offset++;
                    for(int i=0; i<cnt; i++) {
                        fields.append({QString("Get Item %1").arg(i+1), "", "", offset, 0, indent + 1});
                        parseOAD(data, offset, fields, indent + 2, "GET");
                        // unsigned (delay)
                        if (offset < data.size()) {
                            uint8_t delay = data[offset];
                            fields.append({"Delay", QString::number(delay), "Seconds", offset, 1, indent + 2});
                            offset++;
                        }
                    }
                }
            }
            break;
    }

}

void Prot698Parser::parseActionResponse(const QByteArray &data, int &offset, QList<Prot698Field> &fields, int indent)
{
    if (offset >= data.size()) return;
    uint8_t tag = data[offset];
    QString tagName;
    switch(tag) {
        case 1: tagName = "ActionResponseNormal"; break;
        case 2: tagName = "ActionResponseNormalList"; break;
        case 3: tagName = "ActionThenGetResponseNormalList"; break;
        default: tagName = QString("Unknown(%1)").arg(tag); break;
    }
    fields.append({"ACTION-Response Type", QString::asprintf("%02X", tag), tagName, offset, 1, indent});
    offset++;

    parsePIID_ACD(data, offset, fields, indent);

    switch(tag) {
        case 1: // ActionResponseNormal: OMD, DAR, Data OPTIONAL
            {
                parseOMD(data, offset, fields, indent);
                parseDAR(data, offset, fields, indent);
                if (offset < data.size()) {
                    uint8_t hasData = data[offset];
                    fields.append({"Has Data", QString::number(hasData), hasData ? "Present" : "Absent", offset, 1, indent});
                    offset++;
                    if (hasData) {
                        parseData(data, offset, fields, indent);
                    }
                }
            }
            break;
        case 2: // ActionResponseNormalList: SEQUENCE OF (OMD, DAR, Data OPTIONAL)
            {
                if (offset < data.size()) {
                    uint8_t cnt = data[offset];
                    fields.append({"Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
                    offset++;
                    for(int i=0; i<cnt; i++) {
                        fields.append({QString("Item %1").arg(i+1), "", "", offset, 0, indent + 1});
                        parseOMD(data, offset, fields, indent + 2);
                        parseDAR(data, offset, fields, indent + 2);
                        if (offset < data.size()) {
                            uint8_t hasData = data[offset];
                            fields.append({"Has Data", QString::number(hasData), hasData ? "Present" : "Absent", offset, 1, indent + 2});
                            offset++;
                            if (hasData) {
                                parseData(data, offset, fields, indent + 2);
                            }
                        }
                    }
                }
            }
            break;
        case 3: // ActionThenGetResponseNormalList: SEQUENCE OF (OMD, DAR, Data OPTIONAL), SEQUENCE OF A-ResultNormal
            {
                // Action Result List
                if (offset < data.size()) {
                    uint8_t cnt = data[offset];
                    fields.append({"Action Result Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
                    offset++;
                    for(int i=0; i<cnt; i++) {
                        fields.append({QString("Action Result Item %1").arg(i+1), "", "", offset, 0, indent + 1});
                        parseOMD(data, offset, fields, indent + 2);
                        parseDAR(data, offset, fields, indent + 2);
                        if (offset < data.size()) {
                            uint8_t hasData = data[offset];
                            fields.append({"Has Data", QString::number(hasData), hasData ? "Present" : "Absent", offset, 1, indent + 2});
                            offset++;
                            if (hasData) {
                                parseData(data, offset, fields, indent + 2);
                            }
                        }
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
