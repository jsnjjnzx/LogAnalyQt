#include "csg_upstream_test_support.h"

#include "csg_upstream/CsgUpstreamConfigManager.h"
#include "csg_upstream/CsgUpstreamParser.h"

#include <QTextStream>

namespace {

int g_failures = 0;

void Expect(bool condition, const QString &message) {
  if (!condition) {
    QTextStream(stderr) << "FAIL: " << message << '\n';
    ++g_failures;
  }
}

QList<CsgUpstreamField> ParseContent(
    quint8 afn, quint16 da, quint32 di,
    const QByteArray &content) {
  return CsgUpstreamParser().parse(BuildCsgApplicationFrame(
      0x88, afn, 0x00, BuildCsgDataUnit(da, di, content)));
}

void TestAlarmCommonHeader() {
  const QList<CsgUpstreamField> occurred = ParseContent(
      0x13, 0x0001, 0xE200002F,
      QByteArray::fromHex("01251204142734"));
  Expect(CsgFieldValue(occurred, "告警状态") == "01" &&
             CsgFieldDescription(occurred, "告警状态") == "告警发生",
         "ARD1 应按 BIN 解析告警发生状态");
  Expect(CsgFieldDescription(occurred, "告警发生时间") ==
             "2025-12-04 14:27:34",
         "ARD1 应解析六字节告警发生时间");

  const QList<CsgUpstreamField> recovered = ParseContent(
      0x13, 0x0001, 0xE200002F,
      QByteArray::fromHex("00251204142800"));
  Expect(CsgFieldDescription(recovered, "告警状态") == "告警恢复",
         "告警状态 0 应解释为告警恢复");

  const QList<CsgUpstreamField> invalid = ParseContent(
      0x13, 0x0001, 0xE200002F,
      QByteArray::fromHex("FF251204142800"));
  Expect(CsgFieldDescription(invalid, "告警状态") == "告警状态无效",
         "告警状态 FFH 应解释为无效");
}

void TestErd1ReferencedDataItems() {
  const QByteArray content = QByteArray::fromHex(
      "2512041427340102"
      "300100E0251204142735"
      "0000010078563412");
  const QList<CsgUpstreamField> fields = ParseContent(
      0x0E, 0x0001, 0xE2010001, content);
  Expect(CsgFieldDescription(fields, "编程时间") ==
             "2025-12-04 14:27:34" &&
             CsgFieldDescription(fields, "编程数据标识组数") == "2",
         "ERD1 应解析公共字段和动态数据项数量");
  Expect(CsgFieldDescription(
             fields, "编程数据[1]/数据标识编码")
             .contains("E0000130 终端时间") &&
             CsgFieldDescription(
                 fields, "编程数据[1]/终端时间") ==
                 "2025-12-04 14:27:35",
         "ERD1 应按第一个内层 DI 查表解析编程后内容");
  Expect(CsgFieldDescription(
             fields, "编程数据[2]/数据标识编码")
             .contains("00010000 当前正向有功总电能") &&
             CsgFieldDescription(
                 fields,
                 "编程数据[2]/当前正向有功总电能") ==
                 "123456.78 kWh",
         "ERD1 应继续按第二个内层 DI 的 XML 结构解析内容");
  Expect(CountCsgFields(fields, "数据项解析错误") == 0,
         "完整 ERD1 不应报错");

  const QList<CsgUpstreamField> missing = ParseContent(
      0x0E, 0x0001, 0xE2010001,
      QByteArray::fromHex(
          "251204142734010178563412AABB"));
  Expect(CsgFieldDescription(missing, "数据项解析错误")
             .contains("12345678") &&
             CsgFieldDescription(missing, "数据项解析错误")
                 .contains("未配置"),
         "ERD1 引用未配置的内层 DI 时必须明确失败");
}

void TestFixedEventRecords() {
  const QList<CsgUpstreamField> lossVoltage = ParseContent(
      0x0E, 0x0001, 0xE2010004,
      QByteArray::fromHex(
          "251204010203251204020304"
          "01000000020000000300000004000000"));
  Expect(CsgFieldDescription(lossVoltage, "开始时间") ==
             "2025-12-04 01:02:03" &&
             CsgFieldDescription(lossVoltage, "结束时间") ==
             "2025-12-04 02:03:04" &&
             CsgFieldDescription(
                 lossVoltage, "正向有功总电能量增量") ==
                 "000000.01 kWh",
         "ERD3 应解析开始/结束时间和电能增量");

  const QList<CsgUpstreamField> security = ParseContent(
      0x0E, 0x0000, 0xE2010020,
      QByteArray::fromHex("25120414273406"));
  Expect(CsgFieldDescription(security, "安全事件时间") ==
             "2025-12-04 14:27:34" &&
             CsgFieldDescription(security, "事件类型")
                 .contains("USB设备接入"),
         "ERD14 应解析安全事件时间和类型");

  QByteArray replacement =
      QByteArray::fromHex("25120414273403");
  replacement.append(QByteArray(63, '\x11'));
  replacement.append(QByteArray(63, '\x22'));
  const QList<CsgUpstreamField> module = ParseContent(
      0x0E, 0x0000, 0xE2010021, replacement);
  const QString before =
      CsgFieldValue(module, "变更前槽位功能模块信息");
  const QString after =
      CsgFieldValue(module, "变更后槽位功能模块信息");
  Expect(CsgFieldDescription(module, "槽位号").startsWith("3") &&
             before.size() == 126 && after.size() == 126,
         QString("ERD15 应按两个 63 字节模块信息块解析（before=%1, after=%2, error=%3）")
             .arg(before.size())
             .arg(after.size())
             .arg(CsgFieldDescription(module, "数据项解析错误")));
}

} // namespace

int main() {
  QString error;
  Expect(CsgUpstreamConfigManager::instance().loadXml(
             QStringLiteral(CSG_TEST_SOURCE_DIR
                            "/protocol_xml/csg_upstream_protocol.xml"),
             &error),
         "正式 XML 应加载成功: " + error);
  TestAlarmCommonHeader();
  TestErd1ReferencedDataItems();
  TestFixedEventRecords();
  if (g_failures == 0) {
    QTextStream(stdout)
        << "All CSG upstream alarm and event tests passed.\n";
  }
  return g_failures == 0 ? 0 : 1;
}
