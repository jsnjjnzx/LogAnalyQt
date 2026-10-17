#ifndef PROT698DEF_H
#define PROT698DEF_H

#include <cstdint>

#pragma pack(push, 1)

// 长度域 L (Figure 4)
union Prot698Length {
    uint16_t val;
    struct {
        uint16_t len : 14;      // bit0-13: 长度值
        uint16_t unit : 1;      // bit14: 0=字节, 1=千字节
        uint16_t res : 1;       // bit15: 保留
    } bits;
};

// 控制域 C (Figure 5)
union Prot698Control {
    uint8_t val;
    struct {
        uint8_t func : 3;       // bit0-2: 功能码
        uint8_t scramble : 1;   // bit3: 扰码标志 (0=不加, 1=加)
        uint8_t res : 1;        // bit4: 保留
        uint8_t split : 1;      // bit5: 分帧标志 (0=无, 1=有)
        uint8_t prm : 1;        // bit6: 启动标志 (0=从动, 1=主动)
        uint8_t dir : 1;        // bit7: 方向 (0=客户机到服务器, 1=服务器到客户机)
    } bits;
};

// 服务器地址标识 (Figure 11 First Byte)
union Prot698SAIndicator {
    uint8_t val;
    struct {
        uint8_t len_mask : 4;   // bit0-3: 地址长度 (L = val + 1)
        uint8_t res : 2;        // bit4-5: 保留
        uint8_t type : 2;       // bit6-7: 地址类型 (0=单地址, 1=通配, 2=组, 3=广播)
    } bits;
};

// 分帧信息 (Figure 12)
union Prot698SplitInfo {
    uint16_t val;
    struct {
        uint16_t seq : 12;      // bit0-11: 帧序号
        uint16_t res : 2;       // bit12-13: 保留
        uint16_t type : 2;      // bit14-15: 分帧类型 (0=起始, 1=最后, 2=确认, 3=中间)
                                // 注意：代码注释说 0起始, 10确认, 01最后, 11中间.
                                // 二进制 00=0, 10=2, 01=1, 11=3.
                                // 所以 0=Start, 1=End, 2=Confirm, 3=Middle
    } bits;
};

// 数据类型定义 (Data)
enum class Prot698DataType : uint8_t {
    Null = 0,
    Array = 1,
    Structure = 2,
    Bool = 3,
    BitString = 4,
    DoubleLong = 5,
    DoubleLongUnsigned = 6,
    OctetString = 9,
    VisibleString = 10,
    Utf8String = 12,
    Integer = 15,
    Long = 16,
    Unsigned = 17,
    LongUnsigned = 18,
    Long64 = 20,
    Long64Unsigned = 21,
    Enum = 22,
    Float32 = 23,
    Float64 = 24,
    DateTime = 25,
    Date = 26,
    Time = 27,
    DateTimeS = 28,
    OI = 80,
    OAD = 81,
    ROAD = 82,
    OMD = 83,
    TI = 84,
    TSA = 85,
    MAC = 86,
    RN = 87,
    Region = 88,
    ScalerUnit = 89,
    RSD = 90,
    CSD = 91,
    MS = 92,
    SID = 93,
    SID_MAC = 94,
    COMDCB = 95,
    RCSD = 96,
    VQDS = 97,
    Checksum = 98,
    Unknown = 255
};

// APDU Tag 定义
enum class Prot698ApduTag : uint8_t {
    LinkRequest = 1,
    ConnectRequest = 2,
    ReleaseRequest = 3,
    GetRequest = 5,
    SetRequest = 6,
    ActionRequest = 7,
    ReportResponse = 8,
    ProxyRequest = 9,
    SecurityRequest = 16,
    CompactGetRequest = 69,
    CompactSetRequest = 70,
    CompactProxyRequest = 73,
    ErrorResponse = 110,

    LinkResponse = 129,
    ConnectResponse = 130,
    ReleaseResponse = 131,
    ReleaseNotification = 132,
    GetResponse = 133,
    SetResponse = 134,
    ActionResponse = 135,
    ReportNotification = 136,
    ProxyResponse = 137,
    SecurityResponse = 144,
    CompactGetResponse = 197,
    CompactSetResponse = 198,
    CompactProxyResponse = 201,
    ServerErrorResponse = 238
};

#pragma pack(pop)

#endif // PROT698DEF_H
