#include "Prot698Parser.h"

void Prot698Parser::parseSecurityRequest(const QByteArray &data, int &offset, QList<Prot698Field> &fields, int indent)
{
    // SECURITY-Request ::= SEQUENCE {
    //     应用数据单元 CHOICE {
    //         [0] 明文数据单元 octet-string,
    //         [1] 密文数据单元 octet-string
    //     },
    //     数据验证信息 CHOICE {
    //         [0] 数据验证码 SID_MAC,
    //         [1] 随机数 RN,
    //         [2] 随机数+MAC RN_MAC,
    //         [3] 安全标识 SID
    //     }
    // }

    // 1. 应用数据单元 CHOICE
    if (offset < data.size()) {
        uint8_t choice = data[offset];
        QString choiceStr;
        switch(choice) {
            case 0: choiceStr = "明文数据单元"; break;
            case 1: choiceStr = "密文数据单元"; break;
            default: choiceStr = QString("Unknown(%1)").arg(choice); break;
        }
        fields.append({"应用数据单元 Choice", QString::asprintf("%02X", choice), choiceStr, offset, 1, indent});
        offset++;

        // octet-string
        int len = decodeLength(data, offset);
        if (offset + len <= data.size()) {
            fields.append({choiceStr, data.mid(offset, len).toHex().toUpper(), "octet-string", offset, len, indent + 1});
            // If plaintext, maybe we can try to parse it as APDU recursively?
            // But usually it's just data.
            offset += len;
        }
    }

    // 2. 数据验证信息 CHOICE
    if (offset < data.size()) {
        uint8_t choice = data[offset];
        QString choiceStr;
        switch(choice) {
            case 0: choiceStr = "数据验证码 (SID_MAC)"; break;
            case 1: choiceStr = "随机数 (RN)"; break;
            case 2: choiceStr = "随机数+MAC (RN_MAC)"; break;
            case 3: choiceStr = "安全标识 (SID)"; break;
            default: choiceStr = QString("Unknown(%1)").arg(choice); break;
        }
        fields.append({"数据验证信息 Choice", QString::asprintf("%02X", choice), choiceStr, offset, 1, indent});
        offset++;

        switch(choice) {
            case 0: // SID_MAC: SEQUENCE { SID, MAC }
                // Assuming SID and MAC are octet-strings or fixed length.
                // Standard 698.45 usually defines SID as octet-string(8) or similar, MAC as octet-string(4).
                // But let's assume they are octet-strings with length prefix if not specified otherwise.
                // However, in many implementations, SID is 4-8 bytes, MAC is 4 bytes.
                // Let's use decodeLength for safety if they are octet-strings.
                // If they are implicit types, we might need specific knowledge.
                // Based on "RN_MAC ::= SEQUENCE { RN, MAC }", and "SID_MAC", let's assume standard octet-string encoding.
                {
                    // SID
                    int len = decodeLength(data, offset);
                    if (offset + len <= data.size()) {
                        fields.append({"SID", data.mid(offset, len).toHex().toUpper(), "octet-string", offset, len, indent + 1});
                        offset += len;
                    }
                    // MAC
                    len = decodeLength(data, offset);
                    if (offset + len <= data.size()) {
                        fields.append({"MAC", data.mid(offset, len).toHex().toUpper(), "octet-string", offset, len, indent + 1});
                        offset += len;
                    }
                }
                break;
            case 1: // RN
                {
                    int len = decodeLength(data, offset);
                    if (offset + len <= data.size()) {
                        fields.append({"RN", data.mid(offset, len).toHex().toUpper(), "octet-string", offset, len, indent + 1});
                        offset += len;
                    }
                }
                break;
            case 2: // RN_MAC: SEQUENCE { RN, MAC }
                {
                    // RN
                    int len = decodeLength(data, offset);
                    if (offset + len <= data.size()) {
                        fields.append({"RN", data.mid(offset, len).toHex().toUpper(), "octet-string", offset, len, indent + 1});
                        offset += len;
                    }
                    // MAC
                    len = decodeLength(data, offset);
                    if (offset + len <= data.size()) {
                        fields.append({"MAC", data.mid(offset, len).toHex().toUpper(), "octet-string", offset, len, indent + 1});
                        offset += len;
                    }
                }
                break;
            case 3: // SID
                {
                    int len = decodeLength(data, offset);
                    if (offset + len <= data.size()) {
                        fields.append({"SID", data.mid(offset, len).toHex().toUpper(), "octet-string", offset, len, indent + 1});
                        offset += len;
                    }
                }
                break;
        }
    }
}

void Prot698Parser::parseSecurityResponse(const QByteArray &data, int &offset, QList<Prot698Field> &fields, int indent)
{
    // SECURITY-Response ::= SEQUENCE {
    //     应用数据单元 CHOICE {
    //         [0] 明文数据单元 octet-string,
    //         [1] 密文数据单元 octet-string,
    //         [2] 安全传输错误码 DAR
    //     },
    //     数据验证信息 CHOICE OPTIONAL {
    //         [0] 数据MAC MAC
    //     }
    // }

    // 1. 应用数据单元 CHOICE
    if (offset < data.size()) {
        uint8_t choice = data[offset];
        QString choiceStr;
        switch(choice) {
            case 0: choiceStr = "明文数据单元"; break;
            case 1: choiceStr = "密文数据单元"; break;
            case 2: choiceStr = "安全传输错误码 (DAR)"; break;
            default: choiceStr = QString("Unknown(%1)").arg(choice); break;
        }
        fields.append({"应用数据单元 Choice", QString::asprintf("%02X", choice), choiceStr, offset, 1, indent});
        offset++;

        if (choice == 0 || choice == 1) {
            // octet-string
            int len = decodeLength(data, offset);
            if (offset + len <= data.size()) {
                fields.append({choiceStr, data.mid(offset, len).toHex().toUpper(), "octet-string", offset, len, indent + 1});
                offset += len;
            }
        } else if (choice == 2) {
            // DAR
            parseDAR(data, offset, fields, indent + 1);
        }
    }

    // 2. 数据验证信息 CHOICE OPTIONAL
    // How to check optional?
    // In A-XDR, OPTIONAL is usually indicated by a boolean flag (0/1) before the element?
    // Or is it implicit?
    // In 698.45, OPTIONAL fields in a SEQUENCE are usually preceded by a presence flag (1 byte, 0=absent, 1=present).
    // Let's assume presence flag.
    if (offset < data.size()) {
        uint8_t hasValidation = data[offset];
        fields.append({"数据验证信息 Presence", QString::asprintf("%02X", hasValidation), hasValidation ? "Present" : "Absent", offset, 1, indent});
        offset++;

        if (hasValidation) {
            if (offset < data.size()) {
                uint8_t choice = data[offset];
                fields.append({"数据验证信息 Choice", QString::asprintf("%02X", choice), choice == 0 ? "数据MAC" : "Unknown", offset, 1, indent + 1});
                offset++;

                if (choice == 0) {
                    // MAC (octet-string)
                    int len = decodeLength(data, offset);
                    if (offset + len <= data.size()) {
                        fields.append({"MAC", data.mid(offset, len).toHex().toUpper(), "octet-string", offset, len, indent + 2});
                        offset += len;
                    }
                }
            }
        }
    }
}
