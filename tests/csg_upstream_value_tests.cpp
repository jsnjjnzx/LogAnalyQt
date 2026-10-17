#include "csg_upstream_test_support.h"

#include "csg_upstream/CsgUpstreamConfigManager.h"
#include "csg_upstream/CsgUpstreamParser.h"

#include <QDir>
#include <QFile>
#include <QList>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

int g_failureCount = 0;
int g_fileSequence = 0;

void Expect(bool condition, const QString &message) {
  if (condition) {
    return;
  }
  QTextStream(stderr) << "FAIL: " << message << '\n';
  ++g_failureCount;
}

QString WriteXml(const QTemporaryDir &directory,
                 const QString &content) {
  const QString fileName =
      QString("value-%1.xml").arg(++g_fileSequence);
  const QString path = QDir(directory.path()).filePath(fileName);
  QFile file(path);
  Expect(file.open(QIODevice::WriteOnly | QIODevice::Text),
         "应能写入临时 XML: " + path);
  if (file.isOpen()) {
    file.write(content.toUtf8());
    file.close();
  }
  return path;
}

QString Document(const QString &fields,
                 const QString &diCode = "E0000200") {
  return QString(
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
             "<ProtCsgUpstreamConfig version=\"v1.2.0\" "
             "date=\"2026-08-05\">"
             "<AFNs><AFN code=\"0A\" name=\"读参数\">"
             "<Service direction=\"up\" type=\"DataUnits\" "
             "layout=\"content\" cardinality=\"one\" "
             "pw=\"none\" tp=\"none\"/>"
             "</AFN></AFNs><DIs>"
             "<DI code=\"%1\" name=\"测试数据项\" type=\"Struct\">"
             "%2</DI></DIs></ProtCsgUpstreamConfig>")
      .arg(diCode, fields);
}

QList<CsgUpstreamField> ParseWithFields(
    const QTemporaryDir &directory, const QString &fields,
    const QByteArray &content, const QString &diCode = "E0000200") {
  const QString path = WriteXml(directory, Document(fields, diCode));
  QString errorMessage;
  const bool loaded =
      CsgUpstreamConfigManager::instance().loadXml(path, &errorMessage);
  Expect(loaded, "临时 XML 应加载成功: " + errorMessage);
  if (!loaded) {
    return {};
  }

  bool diOk = false;
  const quint32 di = diCode.toUInt(&diOk, 16);
  Expect(diOk, "测试 DI 应为十六进制");
  return CsgUpstreamParser().parse(BuildCsgApplicationFrame(
      0x88, 0x0A, 0x01,
      BuildCsgDataUnit(0x0000, di, content)));
}

void ExpectInvalidXml(const QTemporaryDir &directory,
                      const QString &fields,
                      const QString &expectedError) {
  const QString path = WriteXml(directory, Document(fields));
  QString errorMessage;
  Expect(!CsgUpstreamConfigManager::instance().validateXml(
             path, &errorMessage),
         "非法 XML 应校验失败");
  Expect(errorMessage.contains(expectedError),
         QString("错误应包含 %1，实际为: %2")
             .arg(expectedError, errorMessage));
}

void TestAscii(const QTemporaryDir &directory) {
  const auto fields = ParseWithFields(
      directory,
      "<Field name=\"APN\" type=\"ASCII\" length=\"6\"/>",
      QByteArray::fromHex("434D4E455400"));
  Expect(CsgFieldValue(fields, "APN") == "434D4E455400",
         "ASCII 数据列应保留六字节原文");
  Expect(CsgFieldDescription(fields, "APN") == "CMNET",
         "ASCII 说明应去除尾部 00 并显示文本");
}

void TestBitString(const QTemporaryDir &directory) {
  const auto fields = ParseWithFields(
      directory,
      "<Field name=\"状态字\" type=\"BS\" length=\"1\">"
      "<Bit index=\"0\" name=\"bit0\" zero=\"0\" one=\"1\"/>"
      "<Bit index=\"1\" name=\"bit1\" zero=\"0\" one=\"1\"/>"
      "<Bit index=\"2\" name=\"bit2\" zero=\"0\" one=\"1\"/>"
      "</Field>",
      QByteArray::fromHex("05"));
  Expect(CsgFieldValue(fields, "状态字") == "05",
         "BS 数据列应保留原文字节");
  Expect(CsgFieldDescription(fields, "状态字") ==
             "bit0=1；bit1=0；bit2=1",
         "BS 应按最低位开始解释已定义位");
}

void TestTimeTypes(const QTemporaryDir &directory) {
  const auto fields = ParseWithFields(
      directory,
      "<Field name=\"完整时间\" type=\"YYMMDDhhmmss\" length=\"6\"/>"
      "<Field name=\"四位年任务时间\" type=\"YYYYMMDDhhmm\" length=\"6\"/>"
      "<Field name=\"日时分秒\" type=\"DDhhmmss\" length=\"4\"/>"
      "<Field name=\"时分\" type=\"hhmm\" length=\"2\"/>"
      "<Field name=\"反序时分秒\" type=\"hhmmss\" length=\"3\" "
      "byteOrder=\"little\"/>"
      "<Field name=\"全零日期时间\" type=\"YYMMDDhhmm\" length=\"5\"/>"
      "<Field name=\"全FF月日时间\" type=\"MMDDhhmm\" length=\"4\"/>",
      QByteArray::fromHex(
          "251204142734202512041415041427341830422714"
          "0000000000FFFFFFFF"));
  Expect(CsgFieldDescription(fields, "完整时间") ==
             "2025-12-04 14:27:34",
         "YYMMDDhhmmss 应解析完整时间");
  Expect(CsgFieldDescription(fields, "四位年任务时间") ==
             "2025-12-04 14:15",
         "YYYYMMDDhhmm 应解析四位年任务数据时间");
  Expect(CsgFieldDescription(fields, "日时分秒") ==
             "04日 14:27:34",
         "DDhhmmss 应解析日和时分秒");
  Expect(CsgFieldDescription(fields, "时分") == "18:30",
         "hhmm 应解析时分");
  Expect(CsgFieldDescription(fields, "反序时分秒") == "14:27:42",
         "little endian hhmmss 应按反序字节解析时分秒");
  Expect(CsgFieldDescription(fields, "全零日期时间") == "无数据",
         "日期时间字段全 00 应解析为无数据");
  Expect(CsgFieldDescription(fields, "全FF月日时间") == "无数据",
         "日期时间字段全 FF 应解析为无数据");
}

void TestSignedValues(const QTemporaryDir &directory) {
  const auto fields = ParseWithFields(
      directory,
      "<Field name=\"有符号BIN\" type=\"BIN\" length=\"2\" sign=\"msb\"/>"
      "<Field name=\"有符号BCD\" type=\"BCD\" length=\"4\" "
      "format=\"NNNNNN.NN\" sign=\"leading-bit\"/>",
      QByteArray::fromHex("800280000123"));
  Expect(CsgFieldDescription(fields, "有符号BIN") == "-2",
         "BIN 最高位符号应先剥离再解析数值");
  Expect(CsgFieldDescription(fields, "有符号BCD") ==
             "-000001.23",
         "BCD 最高符号位应先剥离再校验 BCD 数字");
}

void TestLengthReference(const QTemporaryDir &directory) {
  const QString definitions =
      "<Field id=\"dataLength\" name=\"长度\" type=\"BIN\" length=\"2\"/>"
      "<Field name=\"内容\" type=\"BIN\" lengthRef=\"dataLength\"/>";
  const auto fields = ParseWithFields(
      directory, definitions, QByteArray::fromHex("0003A1A2A3"));
  Expect(CsgFieldValue(fields, "内容") == "A1A2A3",
         "lengthRef 应只截取引用长度指定的三字节");

  const auto truncated = ParseWithFields(
      directory, definitions, QByteArray::fromHex("0004A1A2A3"));
  Expect(CsgFieldDescription(truncated, "数据项解析错误")
             .contains("需要 4 字节，剩余 3 字节"),
         "lengthRef 超过剩余数据时应产生字段级错误");
}

void TestRepeatedStruct(const QTemporaryDir &directory) {
  const auto fields = ParseWithFields(
      directory,
      "<Field id=\"recordCount\" name=\"数量\" type=\"BIN\" length=\"1\"/>"
      "<Field name=\"记录\" type=\"Struct\" countRef=\"recordCount\">"
      "<Field name=\"编号\" type=\"BIN\" length=\"1\"/>"
      "<Field name=\"数值\" type=\"BIN\" length=\"1\"/>"
      "</Field>",
      QByteArray::fromHex("02010A020B"));
  Expect(CsgFieldValue(fields, "记录[1]/编号") == "01" &&
             CsgFieldDescription(fields, "记录[1]/数值") == "10",
         "Struct countRef 应解析第一条复合记录");
  Expect(CsgFieldValue(fields, "记录[2]/编号") == "02" &&
             CsgFieldDescription(fields, "记录[2]/数值") == "11",
         "Struct countRef 应解析第二条复合记录");
}

void TestFieldChoice(const QTemporaryDir &directory) {
  const auto fields = ParseWithFields(
      directory,
      "<Field id=\"mode\" name=\"模式\" type=\"BIN\" length=\"1\"/>"
      "<Field name=\"调节值\" type=\"Choice\" selectorRef=\"mode\">"
      "<Case value=\"0\"><Field name=\"模式零值\" type=\"BIN\" length=\"1\"/></Case>"
      "<Case value=\"1\"><Field name=\"模式一值\" type=\"BIN\" length=\"1\"/></Case>"
      "</Field>",
      QByteArray::fromHex("017F"));
  Expect(CsgFieldValue(fields, "调节值/模式一值") == "7F",
         "Choice 应只解析 selectorRef 对应的 Case 1");
  Expect(CsgFieldValue(fields, "调节值/模式零值").isEmpty(),
         "Choice 不得解析未选择的 Case 0");
}

void TestReferenceValidation(const QTemporaryDir &directory) {
  ExpectInvalidXml(
      directory,
      "<Field name=\"内容\" type=\"BIN\" length=\"1\" lengthRef=\"n\"/>",
      "length 与 lengthRef 互斥");
  ExpectInvalidXml(
      directory,
      "<Field name=\"内容\" type=\"BIN\" lengthRef=\"missing\"/>",
      "lengthRef 必须引用此前的 BIN Field");
  ExpectInvalidXml(
      directory,
      "<Field name=\"选择\" type=\"Choice\" selectorRef=\"missing\">"
      "<Case value=\"0\"><Field name=\"值\" type=\"BIN\" length=\"1\"/></Case>"
      "</Field>",
      "selectorRef 必须引用此前的 BIN Field");
  ExpectInvalidXml(
      directory,
      "<Field name=\"状态\" type=\"BS\" length=\"1\">"
      "<Bit index=\"8\" name=\"越界\" zero=\"0\" one=\"1\"/>"
      "</Field>",
      "Bit index 超出字段位数");
}

} // namespace

int main() {
  QTemporaryDir directory;
  Expect(directory.isValid(), "应能创建值解码测试临时目录");
  if (!directory.isValid()) {
    return 1;
  }

  TestAscii(directory);
  TestBitString(directory);
  TestTimeTypes(directory);
  TestSignedValues(directory);
  TestLengthReference(directory);
  TestRepeatedStruct(directory);
  TestFieldChoice(directory);
  TestReferenceValidation(directory);

  if (g_failureCount == 0) {
    QTextStream(stdout) << "All CSG upstream value tests passed.\n";
  }
  return g_failureCount == 0 ? 0 : 1;
}
