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

QList<CsgUpstreamField> Parse(
    quint8 control, quint8 afn, quint32 di,
    const QByteArray &content) {
  return CsgUpstreamParser().parse(BuildCsgApplicationFrame(
      control, afn, 0x00, BuildCsgDataUnit(0x0000, di, content)));
}

void TestFileTransferStructures() {
  QByteArray start = QByteArray::fromHex("0000020004000000");
  start.append("firmware.bin", 12);
  start.append(QByteArray(20, '\0'));
  start.append(QByteArray::fromHex("3412"));
  const QList<CsgUpstreamField> startFields =
      Parse(0x08, 0x0F, 0xE3010001, start);
  Expect(CsgFieldDescription(startFields, "文件性质").contains("终端升级文件") &&
             CsgFieldDescription(startFields, "文件总段数") == "2" &&
             CsgFieldDescription(startFields, "文件大小") == "4" &&
             CsgFieldDescription(startFields, "文件名称") == "firmware.bin",
         "FD1 应解析文件性质、段数、大小和名称");

  const QList<CsgUpstreamField> segment = Parse(
      0x08, 0x0F, 0xE3010002,
      QByteArray::fromHex("03000300AABBCCF66C"));
  Expect(CsgFieldDescription(segment, "文件段号") == "3" &&
             CsgFieldDescription(segment, "文件段长度") == "3" &&
             CsgFieldValue(segment, "文件段内容") == "AABBCC" &&
             CsgFieldDescription(segment, "段校验")
                 .contains("未判定") &&
             !CsgFieldDescription(segment, "段校验")
                  .contains("校验通过"),
         "FD2 应按长度截取内容；规范参数不完整时不得判定 CRC 通过");

  const QList<CsgUpstreamField> badCrc = Parse(
      0x08, 0x0F, 0xE3010002,
      QByteArray::fromHex("03000300AABBCC0000"));
  Expect(CsgFieldDescription(badCrc, "段校验")
             .contains("未判定") &&
             !CsgFieldDescription(badCrc, "段校验")
                  .contains("校验失败"),
         "FD2 CRC 参数不完整时不得把任意值判为失败");

  const QList<CsgUpstreamField> truncated = Parse(
      0x08, 0x0F, 0xE3010002,
      QByteArray::fromHex("03000500AABBCCF66C"));
  Expect(CsgFieldDescription(truncated, "数据项解析错误")
             .contains("文件段内容"),
         "FD2 声明长度超出剩余内容时应报告字段级错误");

  const QList<CsgUpstreamField> unknownLayout = Parse(
      0x88, 0x0F, 0xE3010003,
      QByteArray::fromHex("010203"));
  Expect(CsgFieldValue(unknownLayout, "未解析应用数据")
             .contains("010203"),
         "方向相关且边界不唯一的 FD3 应保持原文");
}

void TestFileTransferDirectionDispatch() {
  const QList<CsgUpstreamField> startReply =
      Parse(0x88, 0x0F, 0xE3010001, QByteArray::fromHex("07"));
  Expect(CsgFieldValue(startReply, "错误码 ERR") == "07" &&
             CsgFieldDescription(startReply, "错误码 ERR") ==
                 "校验失败" &&
             CsgFieldValue(startReply, "文件性质").isEmpty(),
         "FD1 上行应答应解析一字节 ERR，不得套用下行文件属性");

  const QList<CsgUpstreamField> segmentReply =
      Parse(0x88, 0x0F, 0xE3010002, QByteArray::fromHex("00"));
  Expect(CsgFieldDescription(segmentReply, "错误码 ERR") == "正确" &&
             CsgFieldValue(segmentReply, "文件段号").isEmpty(),
         "FD2 上行应答应解析 ERR，不得套用下行文件段结构");

  const QList<CsgUpstreamField> addressListReply =
      Parse(0x88, 0x0F, 0xE3010006, QByteArray::fromHex("04"));
  Expect(CsgFieldDescription(addressListReply, "错误码 ERR") ==
             "无此数据项" &&
             CsgFieldValue(addressListReply,
                           "待升级电表测量点列表").isEmpty(),
         "FD6 上行确认/否认应解析 ERR，不得套用下行位图");

  const QList<CsgUpstreamField> query =
      Parse(0x08, 0x0F, 0xE3010003, QByteArray());
  Expect(CsgFieldDescription(query, "数据项解析错误").isEmpty() &&
             CsgFieldValue(query, "未解析应用数据").isEmpty(),
         "FD3 下行查询只有 DA/DI 时应完整结束，不得生成伪内容");

  const QList<CsgUpstreamField> queryReply =
      Parse(0x88, 0x0F, 0xE3010003,
            QByteArray::fromHex("010203"));
  Expect(CsgFieldValue(queryReply, "未解析应用数据")
             .contains("010203") &&
             CsgFieldValue(queryReply, "错误码 ERR").isEmpty(),
         "FD3 上行查询应答应保留内容，不得误解析为 ERR");
}

void TestRelayOneByteLength() {
  const QList<CsgUpstreamField> down = Parse(
      0x08, 0x10, 0xE3020001,
      QByteArray::fromHex("0001040108000503AABBCC"));
  Expect(CsgFieldDescription(down, "中继类型") == "普通中继" &&
             CsgFieldDescription(down, "端口号") == "1" &&
             CsgFieldDescription(down, "通信波特率") == "1200 bps" &&
             CsgFieldValue(down, "中继报文") == "AABBCC",
         "E3020001 下行应解析串口参数和一字节长度报文");

  const QList<CsgUpstreamField> up = Parse(
      0x88, 0x10, 0xE3020001,
      QByteArray::fromHex("0003AABBCC"));
  Expect(CsgFieldDescription(up, "中继报文长度") == "3" &&
             CsgFieldValue(up, "中继报文") == "AABBCC" &&
             CsgFieldValue(up, "端口号").isEmpty(),
         "E3020001 上行应使用响应结构，不得误套下行串口字段");
}

void TestRelayTwoByteLengthAndTruncation() {
  const QList<CsgUpstreamField> up = Parse(
      0x88, 0x10, 0xE3020002,
      QByteArray::fromHex("010300AABBCC"));
  Expect(CsgFieldDescription(up, "中继类型") == "拉闸命令" &&
             CsgFieldDescription(up, "中继报文长度") == "3" &&
             CsgFieldValue(up, "中继报文") == "AABBCC",
         "E3020002 上行应按两字节长度解析中继报文");

  const QList<CsgUpstreamField> truncated = Parse(
      0x88, 0x10, 0xE3020002,
      QByteArray::fromHex("000500AABB"));
  Expect(CsgFieldDescription(truncated, "数据项解析错误")
             .contains("中继报文"),
         "中继报文声明长度超出剩余字节时应失败");
}

} // namespace

int main() {
  QString error;
  Expect(CsgUpstreamConfigManager::instance().loadXml(
             QStringLiteral(CSG_TEST_SOURCE_DIR
                            "/protocol_xml/csg_upstream_protocol.xml"),
             &error),
         "正式 XML 应加载成功: " + error);
  TestFileTransferStructures();
  TestFileTransferDirectionDispatch();
  TestRelayOneByteLength();
  TestRelayTwoByteLengthAndTruncation();
  if (g_failures == 0) {
    QTextStream(stdout)
        << "All CSG upstream transfer tests passed.\n";
  }
  return g_failures == 0 ? 0 : 1;
}
