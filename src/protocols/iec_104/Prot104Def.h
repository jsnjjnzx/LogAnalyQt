#ifndef PROT104DEF_H
#define PROT104DEF_H

#include <cstdint>

#pragma pack(push, 1)

// IEC 60870-5-104 协议定义
// 参考: DL/T634.5104-2009 配电自动化系统应用实施细则

// ============================================================================
// APCI (Application Protocol Control Information) 定义
// ============================================================================

// APCI 控制域格式 - I帧 (信息传输格式)
// I帧用于传输用户数据(ASDU)
union Prot104ControlI {
    uint32_t val;
    struct {
        // 第1个八位位组
        uint8_t zero1 : 1;      // bit0: 必须为0 (I帧标识)
        uint8_t sendSeqLow : 7; // bit1-7: 发送序号低7位
        // 第2个八位位组
        uint8_t sendSeqHigh : 8; // bit8-15: 发送序号高8位
        // 第3个八位位组
        uint8_t zero2 : 1;       // bit16: 必须为0
        uint8_t recvSeqLow : 7;  // bit17-23: 接收序号低7位
        // 第4个八位位组
        uint8_t recvSeqHigh : 8; // bit24-31: 接收序号高8位
    } bits;
};

// APCI 控制域格式 - S帧 (监督功能格式)
// S帧用于确认接收,不传输用户数据
union Prot104ControlS {
    uint32_t val;
    struct {
        // 第1个八位位组
        uint8_t type : 2;        // bit0-1: 必须为01 (S帧标识)
        uint8_t reserved1 : 6;   // bit2-7: 保留,置0
        // 第2个八位位组
        uint8_t reserved2 : 8;   // bit8-15: 保留,置0
        // 第3个八位位组
        uint8_t zero : 1;        // bit16: 必须为0
        uint8_t recvSeqLow : 7;  // bit17-23: 接收序号低7位
        // 第4个八位位组
        uint8_t recvSeqHigh : 8; // bit24-31: 接收序号高8位
    } bits;
};

// APCI 控制域格式 - U帧 (未编号控制功能格式)
// U帧用于链路层控制,不传输用户数据
union Prot104ControlU {
    uint32_t val;
    struct {
        // 第1个八位位组
        uint8_t type : 2;        // bit0-1: 必须为11 (U帧标识)
        uint8_t func : 4;        // bit2-5: 功能码
        uint8_t reserved1 : 2;   // bit6-7: 保留,置0
        // 第2-4个八位位组
        uint8_t reserved2 : 8;   // bit8-15: 保留,置0
        uint8_t reserved3 : 8;   // bit16-23: 保留,置0
        uint8_t reserved4 : 8;   // bit24-31: 保留,置0
    } bits;
};

// U帧功能码定义
enum class Prot104UFuncCode : uint8_t {
    STARTDT_ACT = 0x01,    // 启动数据传输激活
    STARTDT_CON = 0x02,    // 启动数据传输确认
    STOPDT_ACT = 0x04,     // 停止数据传输激活
    STOPDT_CON = 0x08,     // 停止数据传输确认
    TESTFR_ACT = 0x10,     // 测试帧激活
    TESTFR_CON = 0x20      // 测试帧确认
};

// ============================================================================
// ASDU (Application Service Data Unit) 定义
// ============================================================================

// ASDU 固定部分
struct Prot104ASDUHeader {
    uint8_t typeId;          // 类型标识
    uint8_t vsq;             // 可变结构限定词 (SQ + 数量)
    uint8_t cot;             // 传输原因 (单字节格式)
    uint8_t addr;            // 公共地址 (单字节格式)
};

// 可变结构限定词 (VSQ)
union Prot104VSQ {
    uint8_t val;
    struct {
        uint8_t num : 7;     // bit0-6: 信息对象数量 (1-127)
        uint8_t sq : 1;      // bit7: 顺序标志 (0=地址不连续, 1=地址连续)
    } bits;
};

// 传输原因编码
enum class Prot104CauseOfTransmission : uint8_t {
    COT_PER_CYC = 1,       // 周期/循环
    COT_BACK = 2,          // 背景扫描
    COT_SPONT = 3,         // 突发/自发
    COT_INIT = 4,          // 初始化
    COT_REQ = 5,           // 请求或者被请求
    COT_ACT = 6,           // 激活
    COT_ACTCON = 7,        // 激活确认
    COT_DEACT = 8,         // 停止激活
    COT_DEACTCON = 9,      // 停止激活确认
    COT_ACTTERM = 10,      // 激活终止
    COT_RETREM = 11,       // 远程命令引起的返送信息
    COT_RETLOC = 12,       // 当地命令引起的返送信息
    COT_FILE = 13,         // 文件传输
    COT_INROGEN = 20,      // 响应站召唤
    COT_INRO1 = 21,        // 响应第1组召唤
    COT_INRO2 = 22,        // 响应第2组召唤
    COT_INRO3 = 23,        // 响应第3组召唤
    COT_INRO4 = 24,        // 响应第4组召唤
    COT_INRO5 = 25,        // 响应第5组召唤
    COT_INRO6 = 26,        // 响应第6组召唤
    COT_INRO7 = 27,        // 响应第7组召唤
    COT_INRO8 = 28,        // 响应第8组召唤
    COT_INRO9 = 29,        // 响应第9组召唤
    COT_INRO10 = 30,       // 响应第10组召唤
    COT_INRO11 = 31,       // 响应第11组召唤
    COT_INRO12 = 32,       // 响应第12组召唤
    COT_INRO13 = 33,       // 响应第13组召唤
    COT_INRO14 = 34,       // 响应第14组召唤
    COT_INRO15 = 35,       // 响应第15组召唤
    COT_INRO16 = 36,       // 响应第16组召唤
    COT_REQCOGEN = 37,     // 响应计数量站召唤
    COT_REQCO1 = 38,       // 响应第1组计数量召唤
    COT_REQCO2 = 39,       // 响应第2组计数量召唤
    COT_REQCO3 = 40,       // 响应第3组计数量召唤
    COT_REQCO4 = 41,       // 响应第4组计数量召唤
    COT_UNKNOWNTYPE = 44,  // 未知的类型标识
    COT_UNKNOWNCOT = 45,   // 未知的传输原因
    COT_UNKNOWNCA = 46,    // 未知的应用服务数据单元公共地址
    COT_UNKNOWNIOA = 47    // 未知的信息对象地址
};

// ============================================================================
// 类型标识 (Type ID) 定义
// ============================================================================

enum class Prot104TypeId : uint8_t {
    // 过程信息监视方向
    M_SP_NA_1 = 1,         // 单点信息
    M_SP_TA_1 = 2,         // 带时标的单点信息
    M_DP_NA_1 = 3,         // 双点信息
    M_DP_TA_1 = 4,         // 带时标的双点信息
    M_ST_NA_1 = 5,         // 步位置信息
    M_ST_TA_1 = 6,         // 带时标的步位置信息
    M_BO_NA_1 = 7,         // 比特串
    M_BO_TA_1 = 8,         // 带时标的比特串
    M_ME_NA_1 = 9,         // 测量值, 归一化值
    M_ME_TA_1 = 10,        // 测量值, 带时标的归一化值
    M_ME_NB_1 = 11,        // 测量值, 标度化值
    M_ME_TB_1 = 12,        // 测量值, 带时标的标度化值
    M_ME_NC_1 = 13,        // 测量值, 短浮点数
    M_ME_TC_1 = 14,        // 测量值, 带时标的短浮点数
    M_IT_NA_1 = 15,        // 累计量
    M_IT_TA_1 = 16,        // 带时标的累计量
    M_EP_TA_1 = 17,        // 带时标的继电保护装置事件
    M_EP_TB_1 = 18,        // 带时标的继电保护装置成组启动事件
    M_EP_TC_1 = 19,        // 带时标的继电保护装置输出电路信息
    M_PS_NA_1 = 20,        // 具有状态变位检出的成组单点信息
    M_ME_ND_1 = 21,        // 测量值, 不带品质描述的归一化值
    
    // 带长时标的过程信息
    M_SP_TB_1 = 30,        // 带CP56Time2a时标的单点信息
    M_DP_TB_1 = 31,        // 带CP56Time2a时标的双点信息
    M_ST_TB_1 = 32,        // 带CP56Time2a时标的步位置信息
    M_BO_TB_1 = 33,        // 带CP56Time2a时标的比特串
    M_ME_TD_1 = 34,        // 带CP56Time2a时标的测量值, 归一化值
    M_ME_TE_1 = 35,        // 带CP56Time2a时标的测量值, 标度化值
    M_ME_TF_1 = 36,        // 带CP56Time2a时标的测量值, 短浮点数
    M_IT_TB_1 = 37,        // 带CP56Time2a时标的累计量
    M_EP_TD_1 = 38,        // 带CP56Time2a时标的继电保护装置事件
    M_EP_TE_1 = 39,        // 带CP56Time2a时标的继电保护装置成组启动事件
    M_EP_TF_1 = 40,        // 带CP56Time2a时标的继电保护装置输出电路信息
    
    // 控制方向过程信息
    C_SC_NA_1 = 45,        // 单命令
    C_DC_NA_1 = 46,        // 双命令
    C_RC_NA_1 = 47,        // 升降命令
    C_SE_NA_1 = 48,        // 设定值命令, 归一化值
    C_SE_NB_1 = 49,        // 设定值命令, 标度化值
    C_SE_NC_1 = 50,        // 设定值命令, 短浮点数
    C_BO_NA_1 = 51,        // 比特串
    
    // 带长时标的控制方向过程信息
    C_SC_TA_1 = 58,        // 带CP56Time2a时标的单命令
    C_DC_TA_1 = 59,        // 带CP56Time2a时标的双命令
    C_RC_TA_1 = 60,        // 带CP56Time2a时标的升降命令
    C_SE_TA_1 = 61,        // 带CP56Time2a时标的设定值命令, 归一化值
    C_SE_TB_1 = 62,        // 带CP56Time2a时标的设定值命令, 标度化值
    C_SE_TC_1 = 63,        // 带CP56Time2a时标的设定值命令, 短浮点数
    C_BO_TA_1 = 64,        // 带CP56Time2a时标的比特串
    
    // 系统信息监视方向
    M_EI_NA_1 = 70,        // 初始化结束
    
    // 系统信息控制方向
    C_IC_NA_1 = 100,       // 站召唤(总召唤)命令
    C_CI_NA_1 = 101,       // 计数量召唤命令
    C_RD_NA_1 = 102,       // 读命令
    C_CS_NA_1 = 103,       // 时钟同步命令
    C_TS_NA_1 = 104,       // 测试命令
    C_RP_NA_1 = 105,       // 复位进程命令
    C_CD_NA_1 = 106,       // 延时获得命令
    C_TS_TA_1 = 107,       // 带CP56Time2a时标的测试命令
    
    // 文件传输
    F_FR_NA_1 = 120,       // 文件准备就绪
    F_SR_NA_1 = 121,       // 节准备就绪
    F_SC_NA_1 = 122,       // 召唤目录, 召唤文件, 召唤节
    F_LS_NA_1 = 123,       // 最后的节, 最后的段
    F_AF_NA_1 = 124,       // 确认文件, 确认节
    F_SG_NA_1 = 125,       // 段
    F_DR_TA_1 = 126,       // 目录
    F_SC_NB_1 = 127        // 查询日志, 查询日志区段
};

// ============================================================================
// 信息对象定义
// ============================================================================

// 单点信息 SIQ (Single-point Information with Quality)
union Prot104SIQ {
    uint8_t val;
    struct {
        uint8_t spi : 1;     // bit0: 单点信息 (0=分, 1=合)
        uint8_t reserved : 3; // bit1-3: 保留
        uint8_t bl : 1;      // bit4: 闭塞 (0=未闭塞, 1=闭塞)
        uint8_t sb : 1;      // bit5: 取代 (0=未被取代, 1=被取代)
        uint8_t nt : 1;      // bit6: 当前值 (0=当前值, 1=非当前值)
        uint8_t iv : 1;      // bit7: 有效性 (0=有效, 1=无效)
    } bits;
};

// 双点信息 DIQ (Double-point Information with Quality)
union Prot104DIQ {
    uint8_t val;
    struct {
        uint8_t dpi : 2;     // bit0-1: 双点信息 (0=不确定, 1=分, 2=合, 3=不确定)
        uint8_t reserved : 2; // bit2-3: 保留
        uint8_t bl : 1;      // bit4: 闭塞
        uint8_t sb : 1;      // bit5: 取代
        uint8_t nt : 1;      // bit6: 当前值
        uint8_t iv : 1;      // bit7: 有效性
    } bits;
};

// 品质描述 QDS (Quality Descriptor)
union Prot104QDS {
    uint8_t val;
    struct {
        uint8_t ov : 1;      // bit0: 溢出 (0=无溢出, 1=溢出)
        uint8_t reserved : 2; // bit1-2: 保留
        uint8_t ei : 1;      // bit3: 累积进位 (0=无进位, 1=有进位)
        uint8_t ca : 1;      // bit4: 调整 (0=未调整, 1=已调整)
        uint8_t cy : 1;      // bit5: 周期 (0=无周期溢出, 1=周期溢出)
        uint8_t se : 1;      // bit6: 顺序 (0=顺序正常, 1=顺序异常)
        uint8_t iv : 1;      // bit7: 有效性 (0=有效, 1=无效)
    } bits;
};

// 单命令 SCO (Single Command)
union Prot104SCO {
    uint8_t val;
    struct {
        uint8_t scs : 1;     // bit0: 单命令状态 (0=分, 1=合)
        uint8_t reserved : 1; // bit1: 保留
        uint8_t qu : 5;      // bit2-6: 限定词 (0=无额外定义, 1=短脉冲, 2=长脉冲, 3=持续输出)
        uint8_t se : 1;      // bit7: 选择/执行 (0=执行, 1=选择)
    } bits;
};

// ============================================================================
// 时间标签定义
// ============================================================================

// CP24Time2a - 3字节时间标签 (毫秒)
struct Prot104CP24Time2a {
    uint16_t milliseconds;   // 毫秒 (0-59999)
    uint8_t iv : 1;          // bit16: 有效位 (0=有效, 1=无效)
    uint8_t reserved : 7;    // bit17-23: 保留
};

// CP56Time2a - 7字节时间标签 (完整时间)
struct Prot104CP56Time2a {
    uint16_t milliseconds;   // 毫秒 (0-59999)
    uint8_t minute : 6;      // 分钟 (0-59)
    uint8_t iv : 1;          // 有效位 (0=有效, 1=无效)
    uint8_t reserved1 : 1;   // 保留
    uint8_t hour : 5;        // 小时 (0-23)
    uint8_t su : 1;          // 夏令时 (0=标准时间, 1=夏令时)
    uint8_t reserved2 : 2;   // 保留
    uint8_t day : 5;         // 日 (1-31)
    uint8_t dow : 3;         // 星期 (1-7, 1=星期一)
    uint8_t month : 4;       // 月 (1-12)
    uint8_t reserved3 : 4;   // 保留
    uint8_t year : 7;        // 年 (0-99, 表示2000-2099)
    uint8_t reserved4 : 1;   // 保留
};

#pragma pack(pop)

#endif // PROT104DEF_H
