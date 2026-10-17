#include "csg_upstream_test_support.h"

#include "csg_upstream/CsgUpstreamConfigManager.h"
#include "csg_upstream/CsgUpstreamParser.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTextStream>

namespace {

int g_failureCount = 0;

void Expect(bool condition, const QString &message) {
  if (condition) {
    return;
  }
  QTextStream(stderr) << "FAIL: " << message << '\n';
  ++g_failureCount;
}

QList<CsgUpstreamField> ParseDataUnit(
    quint8 control, quint8 afn, quint8 seq, quint32 di,
    const QByteArray &content = {},
    const QByteArray &tail = {}) {
  return CsgUpstreamParser().parse(BuildCsgApplicationFrame(
      control, afn, seq,
      BuildCsgDataUnit(0x0000, di, content) + tail));
}

void ExpectDi(const QList<CsgUpstreamField> &fields,
              const QString &wireBytes, const QString &code,
              const QString &name) {
  Expect(CsgFieldValue(fields, "数据标识 DI") == wireBytes,
         code + " 的数据列应保留四字节报文原文");
  const QString description =
      CsgFieldDescription(fields, "数据标识 DI");
  Expect(description.contains(code) && description.contains(name),
         code + " 的说明列应显示正常阅读顺序 DI 和名称");
}

void TestRepresentativeDiDictionary() {
  const auto &manager = CsgUpstreamConfigManager::instance();
  const QList<QPair<QString, QString>> expected = {
      {"E0000130", "终端时间"},
      {"E0000140", "终端复位"},
      {"E0000150", "告警主动上报屏蔽字"},
      {"E0000160", "月冻结电量"},
      {"E0001002", "登录退出"},
      {"E0001003", "终端心跳是否带时标"},
      {"E1010000", "用户电费信息"},
      {"E1010001", "购电次数"},
  };
  for (const auto &entry : expected) {
    const CsgUpstreamDiDef *definition =
        manager.getDiDef(entry.first);
    Expect(definition != nullptr,
           entry.first + " 应录入独立 DI 表");
    Expect(definition && definition->name == entry.second,
           entry.first + " 名称应严格来自规范");
  }
}

void TestReadParameterDirections() {
  const QList<CsgUpstreamField> query = ParseDataUnit(
      0x08, 0x0A, 0x00, 0xE0000130);
  ExpectDi(query, "300100E0", "E0000130", "终端时间");
  Expect(CsgFieldValue(query, "终端时间").isEmpty(),
         "AFN0A 下行查询不得虚构 DI 内容");
  Expect(CountCsgFields(query, "数据项解析错误") == 0,
         "AFN0A 下行查询应只解析 DA/DI");

  const QList<CsgUpstreamField> response = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0000130,
      QByteArray::fromHex("251204142734"));
  ExpectDi(response, "300100E0", "E0000130", "终端时间");
  Expect(CsgFieldValue(response, "终端时间") ==
             "251204142734",
         "终端时间的数据列应保留六字节报文原文");
  Expect(CsgFieldDescription(response, "终端时间") ==
             "2025-12-04 14:27:34",
         "终端时间的说明列应显示固定时间解析结果");
}

void TestWriteAndErrorDirections() {
  const QByteArray password =
      QByteArray::fromHex("00112233445566778899AABBCCDDEEFF");
  const QList<CsgUpstreamField> write = ParseDataUnit(
      0x08, 0x04, 0x00, 0xE0000140,
      QByteArray::fromHex("02"), password);
  ExpectDi(write, "400100E0", "E0000140", "终端复位");
  Expect(CsgFieldValue(write, "复位类型") == "02",
         "终端复位类型的数据列应保留一字节报文原文");
  Expect(CsgFieldDescription(write, "复位类型")
             .contains("终端硬件复位"),
         "终端复位类型的说明列应解释 02H");
  Expect(CsgFieldValue(write, "消息认证码 PW") ==
             "00112233445566778899AABBCCDDEEFF",
         "AFN04 下行写参数应保持 PW 边界");

  const QStringList errorDescriptions = {
      "正确",           "中继命令没有返回",
      "设置内容非法",   "密码权限不足",
      "无此数据项",     "命令时间失效",
      "目标地址不存在", "校验失败",
  };
  for (int code = 0; code < errorDescriptions.size(); ++code) {
    const QByteArray errorByte(1, static_cast<char>(code));
    const QList<CsgUpstreamField> error = ParseDataUnit(
        0x88, 0x04, 0x00, 0xE0000140, errorByte);
    const QString expectedRaw =
        QString("%1").arg(code, 2, 16, QLatin1Char('0')).toUpper();
    Expect(CsgFieldValue(error, "错误码 ERR") == expectedRaw,
           QString("ERR=%1H 的数据列应保留一字节报文原文")
               .arg(expectedRaw));
    Expect(CsgFieldDescription(error, "错误码 ERR") ==
               errorDescriptions.at(code),
           QString("ERR=%1H 的说明列应使用附录 F.1 定义")
               .arg(expectedRaw));
  }
}

void TestSimpleParameterItems() {
  QByteArray mask(32, '\0');
  mask[0] = '\x05';
  const QList<CsgUpstreamField> alarm = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0000150, mask);
  ExpectDi(alarm, "500100E0", "E0000150",
           "告警主动上报屏蔽字");
  Expect(CsgFieldValue(alarm, "告警主动上报屏蔽字") ==
             QString::fromLatin1(mask.toHex()).toUpper(),
         "告警屏蔽字的数据列应保留完整 32 字节原文");
  Expect(CsgFieldDescription(alarm,
                             "告警主动上报屏蔽字")
             .contains("256 位位串"),
         "告警屏蔽字的说明列应按 BS 基础类型解析");

  const QList<CsgUpstreamField> freeze = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0000160,
      QByteArray::fromHex("0015"));
  ExpectDi(freeze, "600100E0", "E0000160", "月冻结电量");
  Expect(CsgFieldValue(freeze, "结算小时") == "00" &&
             CsgFieldValue(freeze, "结算日") == "15",
         "月冻结时间应按 HH、DD 两个原始字段解析");

  const QList<CsgUpstreamField> logout = ParseDataUnit(
      0x88, 0x02, 0x00, 0xE0001002);
  ExpectDi(logout, "021000E0", "E0001002", "登录退出");
  Expect(CountCsgFields(logout, "数据项解析错误") == 0,
         "登录退出无数据体时不应报错");

  const QList<CsgUpstreamField> timeFlag = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0001003,
      QByteArray::fromHex("01"));
  ExpectDi(timeFlag, "031000E0", "E0001003",
           "终端心跳是否带时标");
  Expect(CsgFieldValue(timeFlag, "是否带时标") == "01" &&
             CsgFieldDescription(timeFlag, "是否带时标")
                 .contains("带时标"),
         "心跳时标标志应保持 BIN 原文并解释取值");
}

void TestMasterStationDataDirections() {
  const QList<CsgUpstreamField> request = ParseDataUnit(
      0x88, 0x20, 0x00, 0xE1010000);
  ExpectDi(request, "000001E1", "E1010000", "用户电费信息");
  Expect(CsgFieldValue(request, "剩余金额").isEmpty(),
         "AFN20 上行请求不得解析不存在的内容");

  const QList<CsgUpstreamField> fee = ParseDataUnit(
      0x08, 0x20, 0x00, 0xE1010000,
      QByteArray::fromHex("8000012312041427"));
  ExpectDi(fee, "000001E1", "E1010000", "用户电费信息");
  Expect(CsgFieldValue(fee, "剩余金额") == "80000123",
         "剩余金额的数据列应保留四字节报文原文");
  Expect(CsgFieldDescription(fee, "剩余金额")
             .contains("-000001.23 元"),
         "剩余金额的说明列应解析 BCD 首位符号");
  Expect(CsgFieldValue(fee, "时间") == "12041427" &&
             CsgFieldDescription(fee, "时间") ==
                 "12-04 14:27",
         "用户电费时间应解析 MMDDhhmm");

  const QList<CsgUpstreamField> purchases = ParseDataUnit(
      0x08, 0x20, 0x00, 0xE1010001,
      QByteArray::fromHex("0012"));
  ExpectDi(purchases, "010001E1", "E1010001", "购电次数");
  Expect(CsgFieldValue(purchases, "购电次数") == "0012" &&
             CsgFieldDescription(purchases, "购电次数")
                 .contains("0012 次"),
         "购电次数应解析两字节 BCD 并显示单位");
}

void TestCommunicationParameters() {
  const QList<CsgUpstreamField> address = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0000100,
      QByteArray::fromHex("0A2F12E42329000004"));
  ExpectDi(address, "000100E0", "E0000100", "主站通信地址");
  Expect(CsgFieldValue(address, "IP 地址") == "0A2F12E4" &&
             CsgFieldDescription(address, "IP 地址") ==
                 "10.47.18.228",
         "主站 IP 的数据列应保留原文，说明列应显示点分十进制");
  Expect(CsgFieldValue(address, "端口号") == "2329" &&
             CsgFieldDescription(address, "端口号")
                 .contains("9001"),
         "主站端口应按规范示例解析为 9001");
  Expect(CsgFieldValue(address, "通信通道类型") == "04" &&
             CsgFieldDescription(address, "通信通道类型")
                 .contains("Ethernet"),
         "通信通道类型应保留 BIN 原文并显示规范说明");

  QByteArray apn(16, '\0');
  apn.replace(0, 5, "CMNET");
  const QList<CsgUpstreamField> apnFields = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0000104, apn);
  ExpectDi(apnFields, "040100E0", "E0000104", "APN");
  Expect(CsgFieldValue(apnFields, "APN") ==
             QString::fromLatin1(apn.toHex()).toUpper() &&
             CsgFieldDescription(apnFields, "APN")
                 .startsWith("CMNET"),
         "APN 数据列应保留 16 字节原文，说明列应去掉尾部 00");

  const QList<CsgUpstreamField> mode = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0000110,
      QByteArray::fromHex("05"));
  ExpectDi(mode, "100100E0", "E0000110",
           "终端通信网络模式选择");
  const QString modeDescription =
      CsgFieldDescription(mode, "终端通信网络模式选择");
  Expect(modeDescription.contains("2G=可选择") &&
             modeDescription.contains("3G=不可选择") &&
             modeDescription.contains("4G=可选择"),
         "通信网络模式字应按 BS 位定义解释 2G、3G、4G");
}

void TestThresholdParameters() {
  const QList<CsgUpstreamField> threshold = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE000018A,
      QByteArray::fromHex("3412"));
  ExpectDi(threshold, "8A0100E0", "E000018A",
           "电能表停走阈值");
  Expect(CsgFieldValue(threshold, "电能表停走阈值") == "3412" &&
             CsgFieldDescription(threshold,
                                 "电能表停走阈值")
                 .contains("12.34 kWh"),
         "多字节 BCD 阈值应按低字节在前恢复小数和单位");

  const QList<CsgUpstreamField> cpu = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0000190,
      QByteArray::fromHex("7856"));
  ExpectDi(cpu, "900100E0", "E0000190",
           "系统 CPU 占用率阈值");
  Expect(CsgFieldDescription(cpu, "系统 CPU 占用率阈值")
             .contains("56.78 %"),
         "百分比阈值应保持 BCD 基础类型并显示单位");
}

void TestMeterReadingChannelTemplate() {
  const QList<CsgUpstreamField> channel = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE000021F,
      QByteArray::fromHex("13102500000005"));
  ExpectDi(channel, "1F0200E0", "E000021F", "载波通道");
  Expect(CsgFieldValue(channel, "抄表基准时间") ==
             "1310250000" &&
             CsgFieldDescription(channel, "抄表基准时间") ==
                 "2013-10-25 00:00",
         "载波通道模板应解析 YYMMDDhhmm 基准时间");
  Expect(CsgFieldValue(channel, "定时抄表周期单位") == "00" &&
             CsgFieldDescription(channel,
                                 "定时抄表周期单位")
                 .contains("分"),
         "抄表周期单位应按 BIN 固定规则显示");
  Expect(CsgFieldValue(channel, "定时抄表周期") == "05" &&
             CsgFieldDescription(channel, "定时抄表周期")
                 .startsWith("5"),
         "抄表周期的数据列应保留原文，说明列应显示数值");
}

void TestMeterReadingParameterDynamicCounts() {
  const QList<CsgUpstreamField> normal = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0000230,
      QByteArray::fromHex(
          "0100000101020101020102300100E0031000E0"));
  ExpectDi(normal, "300200E0", "E0000230",
           "抄表参数配置表1");
  Expect(CsgFieldValue(normal, "信息点标识[1]") == "0101" &&
             CsgFieldDescription(normal, "信息点标识[1]") ==
                 "第 1 组 p1" &&
             CsgFieldValue(normal, "信息点标识[2]") == "0201" &&
             CsgFieldDescription(normal, "信息点标识[2]") ==
                 "第 1 组 p2",
         "信息点组数 n=2 时应展开两个 DA 原文和点号");
  Expect(CsgFieldValue(normal, "数据标识编码[1]") ==
             "300100E0" &&
             CsgFieldDescription(normal, "数据标识编码[1]")
                 .contains("E0000130") &&
             CsgFieldValue(normal, "数据标识编码[2]") ==
                 "031000E0" &&
             CsgFieldDescription(normal, "数据标识编码[2]")
                 .contains("E0001003"),
         "数据标识组数 m=2 时应展开两个 DI 并独立查表");
  Expect(CountCsgFields(normal, "数据项解析错误") == 0,
         "普通 n=2、m=2 抄表参数不应产生游标错误");

  const QList<CsgUpstreamField> sentinel = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE000024F,
      QByteArray::fromHex(
          "01 0000 01 01 FF FFFF FF FFFFFFFF"));
  ExpectDi(sentinel, "4F0200E0", "E000024F",
           "抄表参数配置表32");
  Expect(CountCsgFields(sentinel, "信息点标识[1]") == 1 &&
             CsgFieldValue(sentinel, "信息点标识[1]") ==
                 "FFFF",
         "n=FF 时应按规范只解析一个 FF FF 信息点占位项");
  Expect(CountCsgFields(sentinel, "数据标识编码[1]") == 1 &&
             CsgFieldValue(sentinel, "数据标识编码[1]") ==
                 "FFFFFFFF",
         "m=FF 时应按规范只解析一个全 F 数据标识占位项");
  Expect(CountCsgFields(sentinel, "数据项解析错误") == 0,
         "FF 哨兵不得被误判为 255 个重复项");
}

void TestCalculationParameterTemplate() {
  const QList<CsgUpstreamField> calculation = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0000702,
      QByteArray::fromHex(
          "3412 300100E0 01 0100 81 0200 00 0000 00 0000"
          " 00 0000 00 0000 00 0000 00 0000"));
  ExpectDi(calculation, "020700E0", "E0000702", "运算量2");
  Expect(CsgFieldValue(calculation, "所属测量点号") == "3412" &&
             CsgFieldValue(calculation, "数据标识编码") ==
                 "300100E0" &&
             CsgFieldDescription(calculation, "数据标识编码")
                 .contains("E0000130"),
         "运算量模板应保留所属测量点和内层 DI 原文并独立查表");
  Expect(CsgFieldValue(calculation, "运算符号1") == "01" &&
             CsgFieldDescription(calculation, "运算符号1")
                 .contains("加") &&
             CsgFieldValue(calculation, "测量点号1") == "0100" &&
             CsgFieldValue(calculation, "运算符号2") == "81" &&
             CsgFieldDescription(calculation, "运算符号2")
                 .contains("减") &&
             CsgFieldValue(calculation, "测量点号8") == "0000",
         "运算量应完整解析 8 组运算符号和测量点记录");
  Expect(CountCsgFields(calculation, "数据项解析错误") == 0,
         "运算量模板不应产生字段边界错误");
}

void TestPortParameterTemplate() {
  const QList<CsgUpstreamField> port = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0000A03,
      QByteArray::fromHex("01 04 01 08 00"));
  ExpectDi(port, "030A00E0", "E0000A03", "485端口定义");
  Expect(CsgFieldValue(port, "端口功能") == "01" &&
             CsgFieldDescription(port, "端口功能")
                 .contains("485总线级联"),
         "端口功能应按 BIN 原文解释规范取值");
  Expect(CsgFieldValue(port, "通信波特率") == "04" &&
             CsgFieldDescription(port, "通信波特率")
                 .contains("1200 bps"),
         "通信波特率应按 BIN 值乘 300 固定解析");
  Expect(CsgFieldValue(port, "校验方式") == "01" &&
             CsgFieldDescription(port, "校验方式")
                 .contains("偶校验") &&
             CsgFieldDescription(port, "数据位").contains("8 位") &&
             CsgFieldDescription(port, "停止位").contains("1 位"),
         "端口参数各 BIN 字段应显示实际含义");
}

void TestMeterControlPasswordRawData() {
  const QByteArray content = QByteArray::fromHex(
      "010203040506 11223344 55667788 0A");
  const QByteArray messagePassword = QByteArray::fromHex(
      "00112233445566778899AABBCCDDEEFF");
  const QList<CsgUpstreamField> control = ParseDataUnit(
      0x08, 0x04, 0x00, 0xE0001100, content,
      messagePassword);
  ExpectDi(control, "001100E0", "E0001100", "电能表遥控拉闸");
  Expect(CsgFieldValue(control, "电能表地址") ==
             "010203040506" &&
             CsgFieldValue(control, "电能表控制密码") ==
                 "11223344" &&
             CsgFieldValue(control, "操作者代码") ==
                 "55667788",
         "测量点控制地址、控制密码和操作者代码应保留 BIN 原文");
  Expect(CsgFieldDescription(control, "控制有效时间")
             .startsWith("10"),
         "控制有效时间应按 BIN 数值显示分钟");
  Expect(CsgFieldValue(control, "消息认证码 PW") ==
             "00112233445566778899AABBCCDDEEFF",
         "控制数据自身密码不得吞并 AFN04 的消息认证码");
}

void TestPhotovoltaicAdjustmentChoice() {
  const QList<CsgUpstreamField> photovoltaic = ParseDataUnit(
      0x08, 0x04, 0x00, 0xE0001219,
      QByteArray::fromHex(
          "010203040506 11223344 55667788 01 78563412 0A"),
      QByteArray::fromHex(
          "00112233445566778899AABBCCDDEEFF"));
  ExpectDi(photovoltaic, "191200E0", "E0001219",
           "光伏发电调节");
  Expect(CsgFieldValue(photovoltaic, "调节方式") == "01" &&
             CsgFieldDescription(photovoltaic, "调节方式")
                 .contains("有功功率"),
         "光伏调节方式应按 BIN 原文选择对应分支");
  Expect(CsgFieldValue(photovoltaic, "光伏发电调节值") ==
             "78563412" &&
             CsgFieldDescription(photovoltaic,
                                 "光伏发电调节值/有功功率")
                 .contains("1234.5678 kW"),
         "光伏有功功率分支应按四字节 BCD 解析值和单位");
  Expect(CsgFieldValue(photovoltaic, "控制有效时间") == "0A",
         "Choice 只应消费所选调节值，随后继续解析有效时间");
  Expect(CsgFieldValue(photovoltaic,
                       "光伏发电调节值/开关机")
             .isEmpty(),
         "有功功率分支不得解析开关机值");
}

void TestApplicationManagementDynamicList() {
  QByteArray firstName(32, '\0');
  firstName.replace(0, 5, "meter");
  QByteArray secondName(32, '\0');
  secondName.replace(0, 6, "update");
  QByteArray content(1, '\x02');
  content += firstName;
  content += QByteArray(1, '\0');
  content += secondName;
  content += QByteArray(1, '\x02');

  const QList<CsgUpstreamField> applications = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0001400, content);
  ExpectDi(applications, "001400E0", "E0001400",
           "APP控制列表");
  Expect(CsgFieldValue(applications, "APP数量") == "02" &&
             CountCsgFields(applications, "APP记录[1]") == 1 &&
             CountCsgFields(applications, "APP记录[2]") == 1,
         "APP数量应驱动两个复合记录的解析");
  Expect(CsgFieldDescription(applications,
                             "APP记录[1]/APP名称")
                 .startsWith("meter") &&
             CsgFieldDescription(applications,
                                 "APP记录[1]/APP控制状态")
                 .contains("启动") &&
             CsgFieldDescription(applications,
                                 "APP记录[2]/APP名称")
                 .startsWith("update") &&
             CsgFieldDescription(applications,
                                 "APP记录[2]/APP控制状态")
                 .contains("卸载"),
         "每个 APP 记录应独立解析 ASCII 名称和 BIN 控制状态");
  Expect(CountCsgFields(applications, "数据项解析错误") == 0,
         "APP 动态列表不应产生游标错误");
}

void TestLoadManagementRangeTemplates() {
  const QList<CsgUpstreamField> switchState = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0000C2E,
      QByteArray::fromHex("01"));
  ExpectDi(switchState, "2E0C00E0", "E0000C2E",
           "输出开关状态");
  Expect(CsgFieldValue(switchState, "输出开关状态") == "01",
         "输出开关 2EH 应复用 20H 区间模板");

  const QList<CsgUpstreamField> period = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0000D18,
      QByteArray::fromHex("1230 01 78563412"));
  ExpectDi(period, "180D00E0", "E0000D18",
           "功率控制系列2时段");
  Expect(CsgFieldDescription(period, "起始时间") == "12:30" &&
             CsgFieldDescription(period, "功控投入标志") ==
                 "有效" &&
             CsgFieldDescription(period, "功率定值")
                 .contains("123456.78 kW"),
         "系列2时段 8 应复用 11H 模板并解析完整字段");

  const QList<CsgUpstreamField> circuit = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0000E2C,
      QByteArray::fromHex("12"));
  ExpectDi(circuit, "2C0E00E0", "E0000E2C",
           "输出控制轮次断线检测结果");
  Expect(CsgFieldValue(circuit, "断线检测结果及回路") == "12",
         "断线检测 2CH 应复用 21H 区间模板");
}

void TestMeasurementPointParameters() {
  const QList<CsgUpstreamField> status = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0800000,
      QByteArray::fromHex("01"));
  ExpectDi(status, "000080E0", "E0800000", "测量点状态");
  Expect(CsgFieldValue(status, "测量点状态") == "01" &&
             CsgFieldDescription(status, "测量点状态") == "有效",
         "测量点状态应保持 BIN 原文并解释有效性");

  const QList<CsgUpstreamField> port = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE080000B,
      QByteArray::fromHex("04 01 08 00"));
  ExpectDi(port, "0B0080E0", "E080000B", "测量点端口参数");
  Expect(CsgFieldDescription(port, "通信波特率") == "1200 bps" &&
             CsgFieldDescription(port, "校验方式") == "偶校验" &&
             CsgFieldDescription(port, "数据位") == "8 位" &&
             CsgFieldDescription(port, "停止位") == "1 位",
         "测量点端口参数应复用固定 BIN 显示规则");

  const QList<CsgUpstreamField> voltage = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0800011,
      QByteArray::fromHex("0022"));
  ExpectDi(voltage, "110080E0", "E0800011", "额定电压");
  Expect(CsgFieldValue(voltage, "额定电压") == "0022" &&
             CsgFieldDescription(voltage, "额定电压") ==
                 "220.0 V（二次侧）",
         "额定电压应按低字节在前解析 BCD 值和单位");
}

void TestPowerQualityParameters() {
  const QList<CsgUpstreamField> imbalance = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0800100,
      QByteArray::fromHex("0030"));
  ExpectDi(imbalance, "000180E0", "E0800100",
           "电流不平衡度阈值");
  Expect(CsgFieldValue(imbalance, "电流不平衡度阈值") ==
             "0030" &&
             CsgFieldDescription(imbalance,
                                 "电流不平衡度阈值")
                 .startsWith("30.00 %"),
         "电流不平衡度阈值应按两字节 BCD 解析百分比");

  const QList<CsgUpstreamField> powerFactors = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0800205,
      QByteArray::fromHex(
          "0001 0002 0003 0004 0005 0006 0007 0008 0009"));
  ExpectDi(powerFactors, "050280E0", "E0800205",
           "功率因数区段定值");
  Expect(CsgFieldValue(powerFactors, "功率因数定值1") ==
             "0001" &&
             CsgFieldDescription(powerFactors,
                                 "功率因数定值1") == "0.100" &&
             CsgFieldValue(powerFactors, "功率因数定值9") ==
                 "0009" &&
             CsgFieldDescription(powerFactors,
                                 "功率因数定值9")
                 .startsWith("0.900"),
         "九段功率因数阈值应按协议顺序完整解析");
  Expect(CountCsgFields(powerFactors, "数据项解析错误") == 0,
         "九段功率因数阈值不应产生字段边界错误");

  const QList<CsgUpstreamField> cooling = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0020107,
      QByteArray::fromHex("0003"));
  ExpectDi(cooling, "070102E0", "E0020107",
           "最高制冷温度");
  Expect(CsgFieldDescription(cooling, "最高制冷温度") ==
             "030.0 ℃",
         "制冷温度范围应按 BCD 解析实际温度和单位");
}

void TestAssetManagementParameters() {
  QByteArray assetCode(32, '\0');
  assetCode.replace(0, 10, "METER-0001");
  const QList<CsgUpstreamField> meterAsset = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0800303, assetCode);
  ExpectDi(meterAsset, "030380E0", "E0800303",
           "电能表资产管理编码");
  Expect(CsgFieldValue(meterAsset, "电能表资产管理编码") ==
             QString::fromLatin1(assetCode.toHex()).toUpper() &&
             CsgFieldDescription(meterAsset,
                                 "电能表资产管理编码") ==
                 "METER-0001",
         "电能表资产编码应保持 32 字节 ASCII 原文并去除尾部 00");

  const QList<CsgUpstreamField> activeConstant = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0800309,
      QByteArray::fromHex("563412"));
  ExpectDi(activeConstant, "090380E0", "E0800309",
           "电表有功常数");
  Expect(CsgFieldDescription(activeConstant, "电表有功常数") ==
             "123456 imp/kWh",
         "电表有功常数应按低字节在前解析 BCD 和单位");

  const QList<CsgUpstreamField> moduleVersion = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0800403,
      QByteArray::fromHex("3412"));
  ExpectDi(moduleVersion, "030480E0", "E0800403",
           "模块软件版本信息");
  Expect(CsgFieldValue(moduleVersion, "模块软件版本信息") ==
             "3412" &&
             CsgFieldDescription(moduleVersion,
                                 "模块软件版本信息") == "1234",
         "通信模块软件版本应保持 BCD 类型和低字节在前规则");

  const QList<CsgUpstreamField> bootloader = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0800404,
      QByteArray::fromHex("7F"));
  ExpectDi(bootloader, "040480E0", "E0800404",
           "模块Bootloader版本号");
  Expect(CsgFieldValue(bootloader, "模块Bootloader版本号") ==
             "7F" &&
             CsgFieldDescription(bootloader,
                                 "模块Bootloader版本号") == "127",
         "Bootloader 版本应保持 BIN 原文并显示数值");

  QByteArray collectorAsset(32, '\0');
  collectorAsset.replace(0, 9, "COLLECTOR");
  const QList<CsgUpstreamField> collector = ParseDataUnit(
      0x88, 0x0A, 0x00, 0xE0800500, collectorAsset);
  ExpectDi(collector, "000580E0", "E0800500",
           "采集器资产编号");
  Expect(CsgFieldDescription(collector, "采集器资产编号") ==
             "COLLECTOR",
         "采集器资产编号应按 ASCII 解析");

  for (quint32 di : {0xE08003FFU, 0xE08004FFU,
                     0xE08005FFU}) {
    const QList<CsgUpstreamField> collection = ParseDataUnit(
        0x88, 0x0A, 0x00, di);
    Expect(CountCsgFields(collection, "数据项解析错误") == 0,
           "资产集合 FF 项不得虚构不存在的数据体");
    Expect(CsgFieldDescription(collection, "数据标识 DI")
               .contains("集合"),
           "资产集合 FF 项应显示集合名称");
  }
}

} // namespace

int main() {
  const QString xmlPath =
      QStringLiteral(CSG_TEST_SOURCE_DIR "/protocol_xml/"
                                     "csg_upstream_protocol.xml");
  QString errorMessage;
  Expect(CsgUpstreamConfigManager::instance().loadXml(
             xmlPath, &errorMessage),
         "正式 XML 应加载成功: " + errorMessage);

  TestRepresentativeDiDictionary();
  TestReadParameterDirections();
  TestWriteAndErrorDirections();
  TestSimpleParameterItems();
  TestMasterStationDataDirections();
  TestCommunicationParameters();
  TestThresholdParameters();
  TestMeterReadingChannelTemplate();
  TestMeterReadingParameterDynamicCounts();
  TestCalculationParameterTemplate();
  TestPortParameterTemplate();
  TestMeterControlPasswordRawData();
  TestPhotovoltaicAdjustmentChoice();
  TestApplicationManagementDynamicList();
  TestLoadManagementRangeTemplates();
  TestMeasurementPointParameters();
  TestPowerQualityParameters();
  TestAssetManagementParameters();

  if (g_failureCount == 0) {
    QTextStream(stdout)
        << "All CSG upstream parameter tests passed.\n";
  }
  return g_failureCount == 0 ? 0 : 1;
}
