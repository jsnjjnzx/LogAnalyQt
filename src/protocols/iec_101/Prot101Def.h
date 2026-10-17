#ifndef PROT101DEF_H
#define PROT101DEF_H

#include <cstdint>

#pragma pack(push, 1)

// IEC 60870-5-101 协议定义
// 参考: DL/T634.5101-2002

// 固定帧长 Start Byte
#define PROT101_FIXED_START 0x10
// 可变帧长 Start Byte
#define PROT101_VAR_START   0x68
// 单字符 Start Byte (E5)
#define PROT101_SINGLE_CHAR 0xE5
// 结束字符
#define PROT101_STOP_BYTE   0x16

// 控制域定义
union Prot101Control {
    uint8_t val;
    struct {
        uint8_t func : 4; // 功能码
        uint8_t fcv_dfc : 1; // FCV (帧计数位有效) / DFC (数据流控制)
        uint8_t fcb_acd : 1; // FCB (帧计数位) / ACD (访问请求)
        uint8_t prm : 1; // 启动报文位 (0: 从动站, 1: 启动站)
        uint8_t dir : 1; // 传输方向位 (0: 下行, 1: 上行) (注: 这里的上下行定义可能根据具体实现有所不同，通常DIR=0为主站发往从站)
    } bits;
};

// 类型标识 (TypeID) - 与 104 大部分通用
enum class Prot101TypeId : uint8_t {
    // 监视方向过程信息
    M_SP_NA_1   = 1,   // 单点信息
    M_SP_TA_1   = 2,   // 带时标的单点信息
    M_DP_NA_1   = 3,   // 双点信息
    M_DP_TA_1   = 4,   // 带时标的双点信息
    M_ST_NA_1   = 5,   // 步位置信息
    M_ST_TA_1   = 6,   // 带时标的步位置信息
    M_BO_NA_1   = 7,   // 32比特串
    M_BO_TA_1   = 8,   // 带时标的32比特串
    M_ME_NA_1   = 9,   // 测量值, 规一化值
    M_ME_TA_1   = 10,  // 带时标的测量值, 规一化值
    M_ME_NB_1   = 11,  // 测量值, 标度化值
    M_ME_TB_1   = 12,  // 带时标的测量值, 标度化值
    M_ME_NC_1   = 13,  // 测量值, 短浮点数
    M_ME_TC_1   = 14,  // 带时标的测量值, 短浮点数
    M_IT_NA_1   = 15,  // 累计量
    M_IT_TA_1   = 16,  // 带时标的累计量
    M_EP_TA_1   = 17,  // 带时标的继电保护设备事件
    M_EP_TB_1   = 18,  // 带时标的继电保护设备成组启动事件
    M_EP_TC_1   = 19,  // 带时标的继电保护设备成组输出电路信息
    M_PS_NA_1   = 20,  // 具有状态变位检出的成组单点信息
    M_ME_ND_1   = 21,  // 测量值, 不带品质描述的规一化值

    M_SP_TB_1   = 30,  // 带CP56Time2a时标的单点信息
    M_DP_TB_1   = 31,  // 带CP56Time2a时标的双点信息
    M_ST_TB_1   = 32,  // 带CP56Time2a时标的步位置信息
    M_BO_TB_1   = 33,  // 带CP56Time2a时标的32比特串
    M_ME_TD_1   = 34,  // 带CP56Time2a时标的测量值, 规一化值
    M_ME_TE_1   = 35,  // 带CP56Time2a时标的测量值, 标度化值
    M_ME_TF_1   = 36,  // 带CP56Time2a时标的测量值, 短浮点数
    M_IT_TB_1   = 37,  // 带CP56Time2a时标的累计量
    M_EP_TD_1   = 38,  // 带CP56Time2a时标的继电保护设备事件
    M_EP_TE_1   = 39,  // 带CP56Time2a时标的继电保护设备成组启动事件
    M_EP_TF_1   = 40,  // 带CP56Time2a时标的继电保护设备成组输出电路信息

    // 控制方向过程信息
    C_SC_NA_1   = 45,  // 单命令
    C_DC_NA_1   = 46,  // 双命令
    C_RC_NA_1   = 47,  // 步调节命令
    C_SE_NA_1   = 48,  // 设定值命令, 规一化值
    C_SE_NB_1   = 49,  // 设定值命令, 标度化值
    C_SE_NC_1   = 50,  // 设定值命令, 短浮点数
    C_BO_NA_1   = 51,  // 32比特串命令

    // 系统命令
    C_IC_NA_1   = 100, // 总召唤
    C_CI_NA_1   = 101, // 电能脉冲召唤
    C_RD_NA_1   = 102, // 读命令
    C_CS_NA_1   = 103, // 时钟同步命令
    C_TS_NA_1   = 104, // 测试命令
    C_RP_NA_1   = 105, // 复位进程命令
    C_CD_NA_1   = 106, // 延时获得命令
};

// 传送原因 (Cause of Transmission)
enum class Prot101CauseOfTransmission : uint8_t {
    COT_PER_CYC     = 1,   // 周期、循环
    COT_BACK        = 2,   // 背景扫描
    COT_SPONT       = 3,   // 突发(自发)
    COT_INIT        = 4,   // 初始化
    COT_REQ         = 5,   // 请求或被请求
    COT_ACT         = 6,   // 激活
    COT_ACT_CON     = 7,   // 激活确认
    COT_DEACT       = 8,   // 停止激活
    COT_DEACT_CON   = 9,   // 停止激活确认
    COT_ACT_TERM    = 10,  // 激活终止
    COT_RETREM      = 11,  // 远方命令引起的返送信息
    COT_RETLOC      = 12,  // 当地命令引起的返送信息
    COT_FILE        = 13,  // 文件传输
    COT_INTRO       = 20,  // 响应站召唤
    COT_REQ_GRP_1   = 21,  // 响应第1组召唤
    COT_REQ_GRP_2   = 22,  // 响应第2组召唤
    COT_REQ_GRP_3   = 23,  // 响应第3组召唤
    COT_REQ_GRP_4   = 24,  // 响应第4组召唤
    COT_REQ_GRP_5   = 25,  // 响应第5组召唤
    COT_REQ_GRP_6   = 26,  // 响应第6组召唤
    COT_REQ_GRP_7   = 27,  // 响应第7组召唤
    COT_REQ_GRP_8   = 28,  // 响应第8组召唤
    COT_REQ_GRP_9   = 29,  // 响应第9组召唤
    COT_REQ_GRP_10  = 30,  // 响应第10组召唤
    COT_REQ_GRP_11  = 31,  // 响应第11组召唤
    COT_REQ_GRP_12  = 32,  // 响应第12组召唤
    COT_REQ_GRP_13  = 33,  // 响应第13组召唤
    COT_REQ_GRP_14  = 34,  // 响应第14组召唤
    COT_REQ_GRP_15  = 35,  // 响应第15组召唤
    COT_REQ_GRP_16  = 36,  // 响应第16组召唤
    COT_REQ_COG     = 37,  // 响应计数量总召唤
    COT_REQ_COG_1   = 38,  // 响应第1组计数量召唤
    COT_REQ_COG_2   = 39,  // 响应第2组计数量召唤
    COT_REQ_COG_3   = 40,  // 响应第3组计数量召唤
    COT_REQ_COG_4   = 41,  // 响应第4组计数量召唤
    COT_UNKNOWN_TYPE = 44, // 未知的类型标识
    COT_UNKNOWN_COT  = 45, // 未知的传送原因
    COT_UNKNOWN_CA   = 46, // 未知的ASDU公共地址
    COT_UNKNOWN_IOA  = 47, // 未知的信息对象地址
};

// 功能码定义
enum class Prot101FunctionCode : uint8_t {
    // PRM = 1, 启动站发送
    FC_RESET_LINK       = 0,  // 复位远方链路
    FC_RESET_USER       = 1,  // 复位用户进程
    FC_TEST_LINK        = 2,  // 发送测试链路功能
    FC_USER_DATA_CONF   = 3,  // 发送需要确认的用户数据
    FC_USER_DATA_NO_CONF = 4, // 发送不需要确认的用户数据
    FC_REQ_ACCESS       = 8,  // 请求访问需求
    FC_REQ_STATUS       = 9,  // 请求链路状态
    FC_REQ_USER_DATA_1  = 10, // 请求1级用户数据
    FC_REQ_USER_DATA_2  = 11, // 请求2级用户数据

    // PRM = 0, 从动站发送
    FC_ACK              = 0,  // 认可: 肯定认可
    FC_NACK             = 1,  // 认可: 否定认可
    FC_USER_DATA        = 8,  // 响应: 用户数据
    FC_NACK_NO_DATA     = 9,  // 响应: 无所请求的用户数据
    FC_STATUS_LINK      = 11, // 响应: 链路状态或访问需求
};

#pragma pack(pop)

#endif // PROT101DEF_H
