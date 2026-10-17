#include "Prot698Parser.h"

void Prot698Parser::parseReportNotification(const QByteArray &data, int &offset, QList<Prot698Field> &fields, int indent)
{
    if (offset >= data.size()) return;
    uint8_t tag = data[offset];
    QString tagName;
    switch(tag) {
        case 1: tagName = "ReportNotificationList"; break;
        case 2: tagName = "ReportNotificationRecordList"; break;
        case 3: tagName = "ReportNotificationTransData"; break;
        case 4: tagName = "ReportRequestClientService"; break;
        case 6: tagName = "ReportNotificationSimplifyRecord"; break;
        default: tagName = QString("Unknown(%1)").arg(tag); break;
    }
    fields.append({"REPORT-Notification Type", QString::asprintf("%02X", tag), tagName, offset, 1, indent});
    offset++;

    parsePIID_ACD(data, offset, fields, indent);

    switch(tag) {
        case 1: // ReportNotificationList: SEQUENCE OF A-ResultNormal
            {
                if (offset < data.size()) {
                    uint8_t cnt = data[offset];
                    fields.append({"Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
                    offset++;
                    for(int i=0; i<cnt; i++) {
                        fields.append({QString("Item %1").arg(i+1), "", "", offset, 0, indent + 1});
                        parseAResultNormal(data, offset, fields, indent + 2);
                    }
                }
            }
            break;
        case 2: // ReportNotificationRecordList: SEQUENCE OF A-ResultRecord
            {
                if (offset < data.size()) {
                    uint8_t cnt = data[offset];
                    fields.append({"Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
                    offset++;
                    for(int i=0; i<cnt; i++) {
                        fields.append({QString("Item %1").arg(i+1), "", "", offset, 0, indent + 1});
                        parseAResultRecord(data, offset, fields, indent + 2);
                    }
                }
            }
            break;
        case 3: // ReportNotificationTransData: OAD, SEQUENCE OF octet-string
            {
                parseOAD(data, offset, fields, indent);
                if (offset < data.size()) {
                    uint8_t cnt = data[offset];
                    fields.append({"Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
                    offset++;
                    for(int i=0; i<cnt; i++) {
                        // octet-string
                        int len = decodeLength(data, offset);
                        if (offset + len <= data.size()) {
                            fields.append({QString("TransData %1").arg(i+1), data.mid(offset, len).toHex().toUpper(), "octet-string", offset, len, indent + 1});
                            offset += len;
                        }
                    }
                }
            }
            break;
        case 4: // ReportRequestClientService: octet-string (Client-APDU)
            {
                int len = decodeLength(data, offset);
                if (offset + len <= data.size()) {
                    fields.append({"Client-APDU", data.mid(offset, len).toHex().toUpper(), "octet-string", offset, len, indent});
                    // Optionally parse inner APDU?
                    // int innerOffset = offset;
                    // parseApdu(data, innerOffset, fields, indent + 1);
                    offset += len;
                }
            }
            break;
        case 6: // ReportNotificationSimplifyRecord: A-ResultSimplifyRecord
            parseAResultSimplifyRecord(data, offset, fields, indent);
            break;
    }
}

void Prot698Parser::parseReportResponse(const QByteArray &data, int &offset, QList<Prot698Field> &fields, int indent)
{
    if (offset >= data.size()) return;
    uint8_t tag = data[offset];
    QString tagName;
    switch(tag) {
        case 1: tagName = "ReportResponseList"; break;
        case 2: tagName = "ReportResponseRecordList"; break;
        case 3: tagName = "ReportResponseTransData"; break;
        case 4: tagName = "ReportResponseClientService"; break;
        case 6: tagName = "ReportResponseSimplifyRecord"; break;
        default: tagName = QString("Unknown(%1)").arg(tag); break;
    }
    fields.append({"REPORT-Response Type", QString::asprintf("%02X", tag), tagName, offset, 1, indent});
    offset++;

    parsePIID(data, offset, fields, indent);

    switch(tag) {
        case 1: // ReportResponseList: SEQUENCE OF OAD
            {
                if (offset < data.size()) {
                    uint8_t cnt = data[offset];
                    fields.append({"Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
                    offset++;
                    for(int i=0; i<cnt; i++) {
                        parseOAD(data, offset, fields, indent + 1);
                    }
                }
            }
            break;
        case 2: // ReportResponseRecordList: SEQUENCE OF OAD
            {
                if (offset < data.size()) {
                    uint8_t cnt = data[offset];
                    fields.append({"Count", QString::number(cnt), "Sequence Count", offset, 1, indent});
                    offset++;
                    for(int i=0; i<cnt; i++) {
                        parseOAD(data, offset, fields, indent + 1);
                    }
                }
            }
            break;
        case 3: // ReportResponseTransData: Just PIID (already parsed)
            break;
        case 4: // ReportResponseClientService: octet-string (Server-APDU)
            {
                int len = decodeLength(data, offset);
                if (offset + len <= data.size()) {
                    fields.append({"Server-APDU", data.mid(offset, len).toHex().toUpper(), "octet-string", offset, len, indent});
                    offset += len;
                }
            }
            break;
        case 6: // ReportResponseSimplifyRecord: Just PIID (already parsed)
            break;
    }
}
