#include "csg_upstream_test_support.h"

#include "csg_upstream/CsgUpstreamConfigManager.h"
#include "csg_upstream/CsgUpstreamParser.h"

#include <QTextStream>

namespace {
int g_failures = 0;
void Expect(bool condition, const QString &message) {
  if (!condition) { QTextStream(stderr) << "FAIL: " << message << '\n'; ++g_failures; }
}

QList<CsgUpstreamField> ParseQuery(quint32 di) {
  return CsgUpstreamParser().parse(BuildCsgApplicationFrame(
      0x08, 0x0A, 0x00, BuildCsgDataUnit(0x0000, di)));
}

void TestFourTaskSeries() {
  struct Series { quint32 base; QString name; };
  const QList<Series> series = {{0xE0000300, "普通任务号"},
                                {0xE0000400, "中继任务号"},
                                {0xE0001500, "表端任务号"},
                                {0xE0001600, "数据推送任务号"}};
  for (const Series &entry : series) {
    for (quint8 number : {quint8{1}, quint8{62}, quint8{253}}) {
      const QList<CsgUpstreamField> fields =
          ParseQuery(entry.base | number);
      const QString description =
          CsgFieldDescription(fields, "数据标识 DI");
      Expect(description.contains(entry.name) &&
                 description.contains(QString::number(number)),
             entry.name + "应由程序归一化 XX 并显示实际任务号");
    }
    const QString collection =
        CsgFieldDescription(ParseQuery(entry.base | 0xFE),
                            "数据标识 DI");
    Expect(collection.contains("集合") &&
               !collection.contains("254"),
           entry.name + " FE 应保持集合语义，不得归一化为任务号254");
  }
}

void TestRelayTaskVariablePayload() {
  const QByteArray content = QByteArray::fromHex(
      "0125120408300201251204000001020101010001040108000503AABBCC");
  const QList<CsgUpstreamField> fields =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x88, 0x0A, 0x00,
          BuildCsgDataUnit(0x0000, 0xE000043E, content)));
  Expect(CsgFieldValue(fields, "中继报文内容") == "AABBCC" &&
             CsgFieldDescription(fields, "中继报文长度") == "3",
         "中继任务应按 BIN 长度展开中继报文原文");
}
} // namespace

int main() {
  QString error;
  Expect(CsgUpstreamConfigManager::instance().loadXml(
             QStringLiteral(CSG_TEST_SOURCE_DIR
                            "/protocol_xml/csg_upstream_protocol.xml"),
             &error), "正式 XML 应加载成功: " + error);
  TestFourTaskSeries();
  TestRelayTaskVariablePayload();
  if (g_failures == 0) QTextStream(stdout) << "All CSG upstream task family tests passed.\n";
  return g_failures == 0 ? 0 : 1;
}
