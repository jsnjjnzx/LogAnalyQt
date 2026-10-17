#include "csg_upstream_test_support.h"

#include "ProtocolDetector.h"
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

QList<CsgUpstreamField> ParseSecure(
    quint8 control, quint8 afn, quint32 di,
    const QByteArray &content) {
  const QByteArray password(16, '\0');
  return CsgUpstreamParser().parse(BuildCsgApplicationFrame(
      control, afn, 0x00,
      BuildCsgDataUnit(0x0000, di, content) + password));
}

void TestAuthenticationAndSecurePayloads() {
  const QList<CsgUpstreamField> certificate = ParseSecure(
      0x08, 0x06, 0xE0010281,
      QByteArray::fromHex("0300AABBCC"));
  Expect(CsgFieldDescription(certificate, "终端证书长度") == "3" &&
             CsgFieldValue(certificate, "终端证书") == "AABBCC",
         "E0010281 应按两字节长度截取终端证书");

  QByteArray session = QByteArray::fromHex("0300A1B2C3");
  session.append(QByteArray(64, '\x5A'));
  const QList<CsgUpstreamField> sessionFields = ParseSecure(
      0x08, 0x06, 0xE0010182, session);
  Expect(CsgFieldValue(sessionFields, "会话密文") == "A1B2C3" &&
             CsgFieldValue(sessionFields, "签名数据").size() == 128,
         "E0010182 应分离会话密文和 64 字节签名");

  const QList<CsgUpstreamField> secure = ParseSecure(
      0x08, 0x16, 0xE001018F,
      QByteArray::fromHex("040001020304"));
  Expect(CsgFieldDescription(secure, "数据长度") == "4" &&
             CsgFieldValue(secure, "数据密文") == "01020304" &&
             CsgFieldDescription(secure, "数据密文")
                 .contains("密文"),
         "O.1 应按长度保留安全密文原文");
}

void TestDataReEncryption() {
  QByteArray content = QByteArray::fromHex(
      "04030201"
      "0300AABBCC"
      "02001122");
  content.append(QByteArray(20, '\x33'));
  content.append(QByteArray::fromHex("020001000100"));
  content.append(QByteArray::fromHex("010203040506"));
  content.append(QByteArray(32, '\x44'));
  content.append(QByteArray(32, '\x55'));
  const QList<CsgUpstreamField> fields = ParseSecure(
      0x08, 0x17, 0xE0010190, content);
  Expect(CsgFieldDescription(fields, "任务编号") == "16909060" &&
             CsgFieldValue(fields, "任务数据明文") == "AABBCC" &&
             CsgFieldValue(fields, "任务数据密文") == "1122" &&
             CsgFieldValue(fields, "电表密钥记录[1]/电表表号") ==
                 "010203040506",
         "E0010190 应解析双长度载荷及电表密钥记录");

  const QList<CsgUpstreamField> overflow = ParseSecure(
      0x08, 0x17, 0xE0010190,
      QByteArray::fromHex("040302010500AABB"));
  Expect(CsgFieldDescription(overflow, "数据项解析错误")
             .contains("任务数据明文"),
         "转加密长度越界必须失败");

  const QList<CsgUpstreamField> status = ParseSecure(
      0x88, 0x17, 0xE0010191,
      QByteArray::fromHex(
          "040302010200010001000100010203040506"));
  Expect(CsgFieldDescription(status, "已成功设备数量") == "1" &&
             CsgFieldValue(status, "已成功电表表号[1]") ==
                 "010203040506",
         "E0010191 应按本帧数量展开成功电表地址");
}

void TestVendorDataAndAppendixIEnvelope() {
  const QList<CsgUpstreamField> vendor =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x08, 0x15, 0x00,
          BuildCsgDataUnit(0x0000, 0xE0002000,
                           QByteArray::fromHex("3412AABBCC"))));
  Expect(CsgFieldValue(vendor, "厂商编码") == "3412" &&
             CsgFieldValue(vendor, "厂商自定义数据") == "AABBCC",
         "AFN15 应解析厂商编码并保留其余私有数据");

  const QByteArray envelope =
      QByteArray::fromHex("88030500AABBCCDDEE77");
  Expect(CsgUpstreamParser::isValidFrame(envelope) &&
             CsgUpstreamParser::getFrameLength(envelope) == 10 &&
             ProtocolDetector::detectProtocol(envelope) ==
                 ProtocolType::ProtocolCsgUpstream,
         "附录 I 外层应优先识别严格的 88H/长度/77H 边界");
  const QList<CsgUpstreamField> fields =
      CsgUpstreamParser().parse(envelope);
  Expect(CsgFieldDescription(fields, "压缩标志") == "压缩" &&
             CsgFieldDescription(fields, "完整性验证标志") ==
                 "有完整性验证" &&
             CsgFieldValue(fields, "封装载荷") == "AABBCCDDEE" &&
             CsgFieldDescription(fields, "封装载荷")
                 .contains("未展开"),
         "附录 I 应解释标志并保持压缩载荷原文");

  QByteArray badTail = envelope;
  badTail[badTail.size() - 1] = '\x76';
  Expect(!CsgUpstreamParser::isValidFrame(badTail),
         "附录 I 报尾不是 77H 时必须拒绝");
}

} // namespace

int main() {
  QString error;
  Expect(CsgUpstreamConfigManager::instance().loadXml(
             QStringLiteral(CSG_TEST_SOURCE_DIR
                            "/protocol_xml/csg_upstream_protocol.xml"),
             &error),
         "正式 XML 应加载成功: " + error);
  TestAuthenticationAndSecurePayloads();
  TestDataReEncryption();
  TestVendorDataAndAppendixIEnvelope();
  if (g_failures == 0) {
    QTextStream(stdout)
        << "All CSG upstream security tests passed.\n";
  }
  return g_failures == 0 ? 0 : 1;
}
