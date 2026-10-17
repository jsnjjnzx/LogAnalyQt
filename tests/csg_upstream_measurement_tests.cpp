#include "csg_upstream_test_support.h"

#include "csg_upstream/CsgUpstreamConfigManager.h"
#include "csg_upstream/CsgUpstreamParser.h"

#include <QList>
#include <QString>
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

const CsgUpstreamField *FindField(
    const QList<CsgUpstreamField> &fields, const QString &name) {
  for (const CsgUpstreamField &field : fields) {
    if (field.name == name) {
      return &field;
    }
  }
  return nullptr;
}

QList<CsgUpstreamField> ParseTimedContent(
    const QByteArray &applicationData) {
  return CsgUpstreamParser().parse(BuildCsgApplicationFrame(
      0x88, 0x0D, 0x00, applicationData));
}

void TestMultipleTimedContentsAndNextDataUnit() {
  const QByteArray firstContent =
      QByteArray::fromHex("02002501020304012511020304");
  const QByteArray secondContent =
      QByteArray::fromHex("01002502030405");
  const QList<CsgUpstreamField> fields = ParseTimedContent(
      BuildCsgDataUnit(0x0001, 0xE0000000, firstContent) +
      BuildCsgDataUnit(0x0002, 0xE0000000, secondContent));

  Expect(CountCsgFields(fields, "信息点标识 DA") == 2,
         "AFN0D 上行应继续解析第二个 DA/DI 数据单元");
  Expect(CsgFieldValue(fields, "数据标识内容数", 0) == "02" &&
             CsgFieldDescription(fields, "数据标识内容数", 0) == "2",
         "第一个数据单元应解析两个历史数据内容");
  Expect(CsgFieldValue(fields, "历史数据[1]/确认结果") == "00" &&
             CsgFieldValue(fields, "历史数据[2]/确认结果") == "01",
         "每个历史时间点都应按 DI 结构解析内容");
  Expect(CsgFieldDescription(fields, "历史数据[1]/数据时间") ==
             "2025-01-02 03:04" &&
             CsgFieldDescription(fields, "历史数据[2]/数据时间") ==
             "2025-11-02 03:04",
         "每个历史数据内容后都应解析独立数据时间");
  Expect(CsgFieldValue(fields, "历史数据[3]/确认结果") == "00" &&
             CsgFieldDescription(fields, "历史数据[3]/数据时间") ==
             "2025-02-03 04:05",
         "第二个 DA/DI 的内容编号应延续且边界正确");

  const CsgUpstreamField *firstValue =
      FindField(fields, "历史数据[1]/确认结果");
  const CsgUpstreamField *firstTime =
      FindField(fields, "历史数据[1]/数据时间");
  Expect(firstValue && firstValue->length == 1 &&
             firstValue->value == "00",
         "历史内容字段应保留一字节原文和长度");
  Expect(firstValue && firstTime &&
             firstTime->offset == firstValue->offset + 1 &&
             firstTime->length == 5 &&
             firstTime->value == "2501020304",
         "历史数据时间 offset、length 和原文应紧随内容");
  Expect(CountCsgFields(fields, "数据项解析错误") == 0,
         "完整的多时间点 AFN0D 上行报文不应报错");
}

void TestNoDataMarker() {
  const QList<CsgUpstreamField> fields = ParseTimedContent(
      BuildCsgDataUnit(0x0001, 0xE0000000,
                       QByteArray::fromHex("01FF2501020304")));
  Expect(CsgFieldValue(fields, "历史数据[1]/无数据") == "FF",
         "历史数据缺失时应按规范识别 FFH 填充");
  Expect(CsgFieldDescription(fields, "历史数据[1]/数据时间") ==
             "2025-01-02 03:04",
         "FFH 无数据标记后仍应解析数据时间");
}

void TestTimedContentFailures() {
  const QList<CsgUpstreamField> zeroCount = ParseTimedContent(
      BuildCsgDataUnit(0x0001, 0xE0000000,
                       QByteArray::fromHex("00")));
  Expect(CsgFieldDescription(zeroCount, "数据项解析错误")
             .contains("内容数不能为 0"),
         "内容数为 0 应明确拒绝");

  const QList<CsgUpstreamField> truncatedTime = ParseTimedContent(
      BuildCsgDataUnit(0x0001, 0xE0000000,
                       QByteArray::fromHex("0100250102")));
  Expect(CsgFieldDescription(truncatedTime, "数据项解析错误")
             .contains("数据时间不足"),
         "历史数据时间截断应报告字段边界错误");

  const QByteArray unknownMiddle =
      BuildCsgDataUnit(0x0001, 0x12345678,
                       QByteArray::fromHex("01AABB2501020304")) +
      BuildCsgDataUnit(0x0002, 0xE0000000,
                       QByteArray::fromHex("01002502030405"));
  const QList<CsgUpstreamField> unknown =
      ParseTimedContent(unknownMiddle);
  Expect(CsgFieldDescription(unknown, "数据项解析错误")
             .contains("未知 DI") &&
             CsgFieldValue(unknown, "未解析应用数据")
                 .contains("AABB"),
         "中间未知 DI 无法确定内容长度时应失败并保留原文");
}

QList<CsgUpstreamField> ParseCurrentData(
    quint32 di, const QByteArray &content) {
  return CsgUpstreamParser().parse(BuildCsgApplicationFrame(
      0x88, 0x0C, 0x00,
      BuildCsgDataUnit(0x0001, di, content)));
}

void TestTerminalSpecificFixedValues() {
  const QList<CsgUpstreamField> ports = ParseCurrentData(
      0xE1800003, QByteArray::fromHex("12"));
  Expect(CsgFieldValue(ports, "串行通信端口数量") == "12" &&
             CsgFieldDescription(ports, "串行通信端口数量") == "12",
         "E1800003 应严格按规范 BCD 格式解析串口数量");

  const QList<CsgUpstreamField> resources = ParseCurrentData(
      0xE1800031, QByteArray::fromHex("56341221000078563412"));
  Expect(CsgFieldDescription(resources, "内存使用数量") ==
             "123456 MB" &&
             CsgFieldDescription(resources, "磁盘使用数量") ==
             "000021 MB" &&
             CsgFieldDescription(resources, "运行时间") ==
             "12345678 s",
         "E1800031 三个字段应按低字节在前的 BCD 数值和单位解析");
}

void TestTopologyRecords() {
  const QByteArray content = QByteArray::fromHex(
      "050201010203040506020A0B0C0D0E0F101112131415"
      "20212223242501303132333435");
  const QList<CsgUpstreamField> fields = ParseCurrentData(
      0xE1800023, content);

  Expect(CsgFieldDescription(fields, "总记录数") == "5" &&
             CsgFieldDescription(fields, "本帧记录数") == "2" &&
             CsgFieldDescription(fields, "起始记录序号") == "1",
         "拓扑详细信息应解析三项 BIN 记录控制字段");
  Expect(CsgFieldValue(fields, "拓扑记录[1]/节点地址") ==
             "010203040506" &&
             CsgFieldValue(fields,
                           "拓扑记录[1]/子节点地址[1]") ==
             "0A0B0C0D0E0F" &&
             CsgFieldValue(fields,
                           "拓扑记录[1]/子节点地址[2]") ==
             "101112131415" &&
             CsgFieldValue(fields,
                           "拓扑记录[2]/子节点地址[1]") ==
             "303132333435",
         "拓扑记录应按每条记录自己的子节点数量动态展开");
  Expect(CountCsgFields(fields, "数据项解析错误") == 0,
         "完整拓扑记录不应报错");
}

void TestApplicationList() {
  QByteArray content;
  content.append('\x02');
  content.append("METERAPP", 8);
  content.append("V1.2.3\0\0", 8);
  content.append("CS", 2);
  content.append(QByteArray::fromHex("0012500100000010"));
  content.append("UPDATER\0", 8);
  content.append("V2.0\0\0\0\0", 8);
  content.append("CS", 2);
  content.append(QByteArray::fromHex("0000250300000020"));

  const QList<CsgUpstreamField> fields = ParseCurrentData(
      0xE1800032, content);
  Expect(CsgFieldDescription(fields, "APP数量") == "2" &&
             CsgFieldDescription(fields,
                                 "APP记录[1]/APP名称") ==
             "METERAPP" &&
             CsgFieldDescription(fields,
                                 "APP记录[2]/APP版本号") ==
             "V2.0",
         "终端 APP 列表应按数量展开 ASCII 固定字段");
  Expect(CsgFieldDescription(fields,
                             "APP记录[1]/APP运行状态")
             .startsWith("运行") &&
             CsgFieldDescription(fields,
                                 "APP记录[2]/APP运行状态")
                 .startsWith("异常"),
         "APP 运行状态 BIN 基础类型应由业务规则解释");
  Expect(CountCsgFields(fields, "数据项解析错误") == 0,
         "完整 APP 动态列表不应报错");
}

void TestCurrentMeasurementCategories() {
  const QList<CsgUpstreamField> energy = ParseCurrentData(
      0x00010000, QByteArray::fromHex("78563412"));
  Expect(CsgFieldDescription(energy, "当前正向有功总电能") ==
             "123456.78 kWh",
         "当前电能应按低字节在前的 BCD 和规范单位解析");

  const QList<CsgUpstreamField> voltage = ParseCurrentData(
      0x02010100, QByteArray::fromHex("0123"));
  const QList<CsgUpstreamField> current = ParseCurrentData(
      0x02020100, QByteArray::fromHex("234501"));
  const QList<CsgUpstreamField> power = ParseCurrentData(
      0x02030000, QByteArray::fromHex("341200"));
  const QList<CsgUpstreamField> factor = ParseCurrentData(
      0x02060000, QByteArray::fromHex("0010"));
  const QList<CsgUpstreamField> frequency = ParseCurrentData(
      0x02800002, QByteArray::fromHex("0050"));
  Expect(CsgFieldDescription(voltage, "A相电压") == "230.1 V" &&
             CsgFieldDescription(current, "A相电流") ==
             "014.523 A" &&
             CsgFieldDescription(power, "瞬时总有功功率") ==
             "00.1234 kW" &&
             CsgFieldDescription(factor, "总功率因数") == "1.000" &&
             CsgFieldDescription(frequency, "电网频率") == "50.00 Hz",
         "电压、电流、功率、功率因数和频率应按各自 BCD 格式解析");

  QByteArray asset(32, '\0');
  asset.replace(0, 8, "METER001");
  const QList<CsgUpstreamField> assetFields = ParseCurrentData(
      0x04000403, asset);
  const QList<CsgUpstreamField> status = ParseCurrentData(
      0x04000501, QByteArray::fromHex("0100"));
  Expect(CsgFieldDescription(assetFields, "资产管理编码") ==
             "METER001" &&
             CsgFieldDescription(status, "电表运行状态字1")
                 .contains("有功功率方向"),
         "资产 ASCII 和设备状态 BS 数据应保持协议基础类型");
}

void TestCurrentEnergyRateBlock() {
  const QList<CsgUpstreamField> fields = ParseCurrentData(
      0x0001FF00,
      QByteArray::fromHex("02785634121111111122222222"));
  Expect(CsgFieldDescription(fields, "费率数") == "2" &&
             CsgFieldDescription(fields,
                                 "当前正向有功费率电能[1]") ==
             "111111.11 kWh" &&
             CsgFieldDescription(fields,
                                 "当前正向有功费率电能[2]") ==
             "222222.22 kWh",
         "当前电能数据块应按费率数动态展开总值后的费率值");
}

void TestDailyAndMonthlyHistory() {
  const QByteArray dailyContent = QByteArray::fromHex(
      "01027856341211111111222222222512040000");
  const QList<CsgUpstreamField> daily = ParseTimedContent(
      BuildCsgDataUnit(0x0001, 0x050601FF, dailyContent));
  Expect(CsgFieldDescription(
             daily, "历史数据[1]/日冻结正向有功总电能") ==
             "123456.78 kWh" &&
             CsgFieldDescription(
                 daily,
                 "历史数据[1]/日冻结正向有功费率电能[2]") ==
             "222222.22 kWh" &&
             CsgFieldDescription(daily,
                                 "历史数据[1]/数据时间") ==
             "2025-12-04 00:00",
         "日冻结费率块应在 AFN0D 中按内容数、费率数和数据时间解析");

  const QByteArray monthlyContent =
      QByteArray::fromHex("01785634122512010000");
  const QList<CsgUpstreamField> monthly = ParseTimedContent(
      BuildCsgDataUnit(0x0001, 0x00010001, monthlyContent));
  Expect(CsgFieldDescription(
             monthly, "历史数据[1]/月冻结正向有功总电能") ==
             "123456.78 kWh" &&
             CsgFieldDescription(monthly,
                                 "历史数据[1]/数据时间") ==
             "2025-12-01 00:00",
         "月冻结数据应使用 C.4 右侧有效 DI 列并解析月数据时间");
}

void TestDailyStatisticTimeByteOrder() {
  const QList<CsgUpstreamField> voltage = ParseCurrentData(
      0xE1008010,
      QByteArray::fromHex(
          "990700000001000000000000000000012237102912942119112912"));
  Expect(CsgFieldDescription(voltage, "最高电压出现时间") ==
             "12-29 10:37" &&
             CsgFieldDescription(voltage, "最低电压出现时间") ==
             "12-29 11:19",
         "E1008010 的 MMDDhhmm 应按低字节在前解析");
  Expect(CountCsgFields(voltage, "数据项解析错误") == 0,
         "合法 E1008010 日统计时间不应报告月日无效");

  const QList<CsgUpstreamField> activePower = ParseCurrentData(
      0xE1008018,
      QByteArray::fromHex("9865005923291200000035102912"));
  Expect(CsgFieldDescription(activePower,
                             "最高有功功率出现时间") ==
             "12-29 23:59" &&
             CsgFieldDescription(activePower,
                                 "最低有功功率出现时间") ==
             "12-29 10:35",
         "E1008018 的 MMDDhhmm 应按低字节在前解析");
  Expect(CountCsgFields(activePower, "数据项解析错误") == 0,
         "合法 E1008018 日统计时间不应报告月日无效");
}

void TestDailyStatisticStructuredItems() {
  const QList<CsgUpstreamField> phaseA = ParseCurrentData(
      0xE1008011,
      QByteArray::fromHex(
          "990700000001000000000000000000002237102912972119112912"));
  const QList<CsgUpstreamField> phaseB = ParseCurrentData(
      0xE1008012,
      QByteArray::fromHex(
          "990700000001000000000000000000012237102912942119112912"));
  const QList<CsgUpstreamField> phaseC = ParseCurrentData(
      0xE1008013,
      QByteArray::fromHex(
          "990700000001000000000000000000002237102912972136112912"));
  Expect(CsgFieldDescription(phaseA, "A相最高电压") ==
             "220.0 V" &&
             CsgFieldDescription(phaseA, "A相最高电压出现时间") ==
             "12-29 10:37" &&
             CsgFieldDescription(phaseA, "A相最低电压") ==
             "219.7 V" &&
             CsgFieldDescription(phaseA, "A相最低电压出现时间") ==
             "12-29 11:19",
         "E1008011 应逐字段解析 A 相最高/最低电压和出现时间");
  Expect(CsgFieldDescription(phaseB, "B相最高电压") ==
             "220.1 V" &&
             CsgFieldDescription(phaseB, "B相最低电压") ==
             "219.4 V",
         "E1008012 应逐字段解析 B 相最高/最低电压");
  Expect(CsgFieldDescription(phaseC, "C相最低电压") ==
             "219.7 V" &&
             CsgFieldDescription(phaseC, "C相最低电压出现时间") ==
             "12-29 11:36",
         "E1008013 应逐字段解析 C 相最低电压和出现时间");
  Expect(CsgFieldValue(phaseA, "A相电压统计内容").isEmpty() &&
             CsgFieldValue(phaseB, "B相电压统计内容").isEmpty() &&
             CsgFieldValue(phaseC, "C相电压统计内容").isEmpty(),
         "三相电压合格率统计不应继续由 Blob 吞掉全部内容");

  const QList<CsgUpstreamField> voltageLoss = ParseCurrentData(
      0xE1008041,
      QByteArray::fromHex(
          "010000020000030000040000"
          "100000200000300000400000"
          "78563412111111112222222233333333"));
  Expect(CsgFieldDescription(voltageLoss, "总失压次数") ==
             "000001 次" &&
             CsgFieldDescription(voltageLoss, "C相失压次数") ==
             "000004 次" &&
             CsgFieldDescription(voltageLoss, "总失压累计时间") ==
             "000010 min" &&
             CsgFieldDescription(voltageLoss, "C相失压累计时间") ==
             "000040 min",
         "E1008041 应逐字段解析四类失压次数和累计时间");
  Expect(CsgFieldDescription(
             voltageLoss, "失压正向有功总电能增量") ==
             "123456.78 kWh" &&
             CsgFieldDescription(
                 voltageLoss, "失压反向有功总电能增量") ==
             "111111.11 kWh" &&
             CsgFieldDescription(
                 voltageLoss, "失压组合无功1总电能增量") ==
             "222222.22 kvarh" &&
             CsgFieldDescription(
                 voltageLoss, "失压组合无功2总电能增量") ==
             "333333.33 kvarh",
         "E1008041 应逐字段解析四类电能增量");
  Expect(CsgFieldValue(voltageLoss, "日失压统计内容").isEmpty(),
         "E1008041 不应继续由 Blob 吞掉 40 字节内容");
}

void TestStatusWordsAndLoadRecords() {
  const QList<CsgUpstreamField> terminalStatus = ParseCurrentData(
      0xE1800001, QByteArray::fromHex("0581"));
  Expect(CsgFieldDescription(terminalStatus, "终端状态字")
             .contains("计量柜门=开") &&
             CsgFieldDescription(terminalStatus, "终端状态字")
                 .contains("本体门=开") &&
             CsgFieldDescription(terminalStatus, "终端状态字")
                 .contains("保电=投入"),
         "终端状态字应按 C.5.1 的 BS 位定义解释");

  const QList<CsgUpstreamField> meterStatus = ParseCurrentData(
      0x04000501, QByteArray::fromHex("3C00"));
  Expect(CsgFieldDescription(meterStatus, "电表运行状态字1")
             .contains("时钟电池=欠压") &&
             CsgFieldDescription(meterStatus, "电表运行状态字1")
                 .contains("有功功率方向=反向"),
         "电表运行状态字1应按 C.5.3 位定义解释");

  const QByteArray pointLoadContent =
      QByteArray::fromHex("0101232501020304");
  const QList<CsgUpstreamField> pointLoad = ParseTimedContent(
      BuildCsgDataUnit(0x0001, 0x06100101,
                       pointLoadContent));
  const QByteArray carrierLoadContent =
      QByteArray::fromHex("0101232501020304");
  const QList<CsgUpstreamField> carrierLoad = ParseTimedContent(
      BuildCsgDataUnit(0x0001, 0x06120101,
                       carrierLoadContent));
  Expect(CsgFieldDescription(pointLoad,
                             "历史数据[1]/A相电压") ==
             "230.1 V" &&
             CsgFieldDescription(carrierLoad,
                                 "历史数据[1]/A相电压") ==
             "230.1 V",
         "测量点和宽带载波模块负荷记录应复用内容数和数据时间链路");
}

} // namespace

int main() {
  QString error;
  Expect(CsgUpstreamConfigManager::instance().loadXml(
             QStringLiteral(CSG_TEST_SOURCE_DIR
                            "/protocol_xml/csg_upstream_protocol.xml"),
             &error),
         "应加载南网上行协议 XML: " + error);

  TestMultipleTimedContentsAndNextDataUnit();
  TestNoDataMarker();
  TestTimedContentFailures();
  TestTerminalSpecificFixedValues();
  TestTopologyRecords();
  TestApplicationList();
  TestCurrentMeasurementCategories();
  TestCurrentEnergyRateBlock();
  TestDailyAndMonthlyHistory();
  TestDailyStatisticTimeByteOrder();
  TestDailyStatisticStructuredItems();
  TestStatusWordsAndLoadRecords();

  if (g_failureCount != 0) {
    QTextStream(stderr) << g_failureCount
                        << " CSG upstream measurement test(s) failed.\n";
    return 1;
  }
  QTextStream(stdout) << "All CSG upstream measurement tests passed.\n";
  return 0;
}
