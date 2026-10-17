#include "csg_upstream_test_support.h"

#include "csg_upstream/CsgUpstreamConfigManager.h"
#include "csg_upstream/CsgUpstreamParser.h"

#include <QList>
#include <QPair>
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

void TestEveryAfnIsConfigured() {
  const QList<QPair<quint8, QString>> expected = {
      {0x00, "确认/否定"},       {0x02, "链路接口检测"},
      {0x04, "写参数"},         {0x06, "安全认证"},
      {0x0A, "读参数"},         {0x0C, "读当前数据"},
      {0x0D, "读历史数据"},     {0x0E, "读事件记录"},
      {0x0F, "文件传输"},       {0x10, "中继转发"},
      {0x12, "读任务数据"},     {0x13, "读告警数据"},
      {0x14, "级联命令"},       {0x15, "用户自定义数据"},
      {0x16, "数据安全传输"},   {0x17, "数据转加密"},
      {0x20, "请求主站数据"},
  };

  const auto &manager = CsgUpstreamConfigManager::instance();
  for (const auto &entry : expected) {
    const CsgUpstreamAfnDef *definition =
        manager.getAfnDef(entry.first);
    Expect(definition != nullptr,
           QString("AFN %1 应已配置")
               .arg(entry.first, 2, 16, QLatin1Char('0'))
               .toUpper());
    Expect(definition && definition->name == entry.second,
           QString("AFN %1 名称应为 %2")
               .arg(entry.first, 2, 16, QLatin1Char('0'))
               .arg(entry.second)
               .toUpper());
  }
}

void TestMultipleDataUnitsAreParsedIndependently() {
  const QByteArray first =
      BuildCsgDataUnit(0x0000, 0xE0000000,
                       QByteArray::fromHex("00"));
  const QByteArray second =
      BuildCsgDataUnit(0x0001, 0xE0000000,
                       QByteArray::fromHex("01"));
  const QByteArray frame = BuildCsgApplicationFrame(
      0x88, 0x0A, 0x01, first + second);
  const QList<CsgUpstreamField> fields =
      CsgUpstreamParser().parse(frame);

  Expect(CountCsgFields(fields, "信息点标识 DA") == 2,
         "两组数据单元应各显示一次 DA");
  Expect(CountCsgFields(fields, "数据标识 DI") == 2,
         "两组数据单元应各显示一次 DI，第一组不得重复");
  Expect(CsgFieldValue(fields, "数据标识 DI", 0) == "000000E0",
         "第一组 DI 数据列应保留报文字节");
  Expect(CsgFieldValue(fields, "数据标识 DI", 1) == "000000E0",
         "第二组 DI 数据列应保留报文字节");
  Expect(CsgFieldValue(fields, "确认结果", 0) == "00",
         "第一组内容应独立解析");
  Expect(CsgFieldValue(fields, "确认结果", 1) == "01",
         "第二组内容应独立解析");
  Expect(CountCsgFields(fields, "数据项解析错误") == 0,
         "完整的两组数据单元不应报错");
}

void TestTimeTagUsesTheSequenceBoundary() {
  const QByteArray dataUnit =
      BuildCsgDataUnit(0x0000, 0xE0000000,
                       QByteArray::fromHex("00"));
  const QByteArray timeTag = QByteArray::fromHex("0414273405");
  const QByteArray frame = BuildCsgApplicationFrame(
      0x88, 0x0A, 0x81, dataUnit + timeTag);
  const QList<CsgUpstreamField> fields =
      CsgUpstreamParser().parse(frame);

  Expect(CsgFieldValue(fields, "时间标签 Tp") == "0414273405",
         "TpV=1 时应从应用数据末尾截取五字节 Tp");
  Expect(CsgFieldValue(fields, "确认结果") == "00",
         "Tp 不得被数据项内容吞掉");
  Expect(CountCsgFields(fields, "数据项解析错误") == 0,
         "完整 Tp 不应报错");
}

void TestRequiredPasswordUsesTheServiceBoundary() {
  const QByteArray dataUnit =
      BuildCsgDataUnit(0x0000, 0xE0000000,
                       QByteArray::fromHex("00"));
  const QByteArray password =
      QByteArray::fromHex("00112233445566778899AABBCCDDEEFF");
  const QByteArray frame = BuildCsgApplicationFrame(
      0x08, 0x04, 0x01, dataUnit + password);
  const QList<CsgUpstreamField> fields =
      CsgUpstreamParser().parse(frame);

  Expect(CsgFieldValue(fields, "消息认证码 PW") ==
             "00112233445566778899AABBCCDDEEFF",
         "写参数下行应从数据单元之后截取十六字节 PW");
  Expect(CsgFieldValue(fields, "确认结果") == "00",
         "PW 不得被数据项内容吞掉");
  Expect(CountCsgFields(fields, "数据项解析错误") == 0,
         "完整必选 PW 不应报错");
}

void TestTruncatedTailsReportTheirOwnBoundary() {
  const QByteArray dataUnit =
      BuildCsgDataUnit(0x0000, 0xE0000000);

  const auto truncatedTimeTag = CsgUpstreamParser().parse(
      BuildCsgApplicationFrame(0x88, 0x0A, 0x81, dataUnit));
  Expect(CsgFieldDescription(truncatedTimeTag,
                             "数据项解析错误")
             .contains("Tp"),
         "TpV=1 且应用数据不足时应明确报告 Tp 截断");
  Expect(CsgFieldValue(truncatedTimeTag, "未解析应用数据") ==
             "0000000000E0",
         "Service 尾域检查失败时应从 DA 开始保留完整原文");

  const auto truncatedPassword = CsgUpstreamParser().parse(
      BuildCsgApplicationFrame(0x08, 0x04, 0x01, dataUnit));
  Expect(CsgFieldDescription(truncatedPassword,
                             "数据项解析错误")
             .contains("PW"),
         "必选 PW 不足十六字节时应明确报告 PW 截断");
}

void TestOptionalPasswordDoesNotSwallowAValidDataUnit() {
  const QByteArray timedQuery =
      QByteArray::fromHex("25010203042501020304");
  const QByteArray first =
      BuildCsgDataUnit(0x0000, 0x11223344, timedQuery);
  const QByteArray second =
      BuildCsgDataUnit(0x0001, 0x55667788, timedQuery);
  const QList<CsgUpstreamField> fields =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x08, 0x0E, 0x01, first + second));

  Expect(CountCsgFields(fields, "数据标识 DI") == 2,
         "恰好十六字节的合法定时查询数据单元不得被识别为 PW");
  Expect(CsgFieldValue(fields, "消息认证码 PW").isEmpty(),
         "合法第二数据单元不得生成 PW 字段");
  Expect(CountCsgFields(fields, "数据项解析错误") == 0,
         "两组合法定时查询数据单元不应报错");
}

void TestUnknownDiPreservesRawDataAtOptionalPasswordBoundary() {
  const QByteArray first =
      BuildCsgDataUnit(0x0000, 0xE0000000,
                       QByteArray::fromHex("00"));
  const QByteArray unknown =
      BuildCsgDataUnit(0x0001, 0x12345678,
                       QByteArray::fromHex("00112233445566778899"));
  const QList<CsgUpstreamField> fields =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x88, 0x0A, 0x01, first + unknown));

  Expect(CountCsgFields(fields, "数据标识 DI") == 2,
         "未知 DI 应保留自身 DA/DI，而不是被静默吞成 PW");
  Expect(CsgFieldValue(fields, "消息认证码 PW").isEmpty(),
         "未知 DI 与可选 PW 无法分界时不得猜测 PW");
  Expect(CsgFieldDescription(fields, "未解析应用数据")
             .contains("可选 PW 无法唯一分界"),
         "未知 DI 原文应明确标记可选 PW 边界歧义");
}

void TestOptionalDiFieldsDoNotConsumeTheNextDataUnit() {
  const QByteArray loginWithoutOptionalTail =
      BuildCsgDataUnit(0x0000, 0xE0001000,
                       QByteArray::fromHex("1234"));
  const QByteArray confirmation =
      BuildCsgDataUnit(0x0001, 0xE0000000,
                       QByteArray::fromHex("00"));
  const QList<CsgUpstreamField> fields =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x88, 0x0A, 0x01,
          loginWithoutOptionalTail + confirmation));

  Expect(CountCsgFields(fields, "数据标识 DI") == 2,
         "第一组 DI 的可选尾字段不得消费第二组 DA/DI");
  Expect(CountCsgFields(fields, "是否具备安全模块") == 0,
         "第二组数据单元头不得被误解析成第一组可选字段");
  Expect(CsgFieldValue(fields, "确认结果") == "00",
         "第二组数据项内容应保持独立解析");

  const QByteArray unknown =
      BuildCsgDataUnit(0x0001, 0x12345678,
                       QByteArray::fromHex("AABB"));
  const QList<CsgUpstreamField> unknownFields =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x88, 0x0A, 0x01,
          loginWithoutOptionalTail + unknown));
  Expect(CountCsgFields(unknownFields, "数据标识 DI") == 1,
         "未知下一组与可选字段冲突时不得猜测 DA/DI 边界");
  Expect(CsgFieldValue(unknownFields, "未解析应用数据") ==
             "010078563412AABB",
         "未知下一组与可选字段冲突时应保留全部候选原文");
  Expect(CsgFieldDescription(unknownFields,
                             "未解析应用数据")
             .contains("可选字段与下一未知 DI 无法唯一分界"),
         "未知下一组边界应明确标记歧义");
}

void TestOptionalDiFieldsStopBeforeOptionalPassword() {
  const QByteArray password =
      QByteArray::fromHex("00112233445566778899AABBCCDDEEFF");
  const QList<QByteArray> contents = {
      QByteArray::fromHex("1234"),
      QByteArray::fromHex("123401"),
  };
  for (const QByteArray &content : contents) {
    const QList<CsgUpstreamField> fields =
        CsgUpstreamParser().parse(BuildCsgApplicationFrame(
            0x88, 0x0A, 0x01,
            BuildCsgDataUnit(0x0000, 0xE0001000, content) +
                password));
    Expect(CsgFieldValue(fields, "消息认证码 PW") ==
               "00112233445566778899AABBCCDDEEFF",
           "DI 可选字段结束后应识别十六字节可选 PW");
    Expect(CountCsgFields(fields, "数据项解析错误") == 0,
           "可选 DI 字段与可选 PW 组合不应错位");
  }
}

void TestKnownDataUnitTakesPriorityOverPasswordLengthCollision() {
  const QByteArray firstContent = QByteArray::fromHex("123401");
  const QByteArray secondContent =
      QByteArray::fromHex("123401250102030405");
  const QList<CsgUpstreamField> fields =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x88, 0x0A, 0x01,
          BuildCsgDataUnit(0x0000, 0xE0001000, firstContent) +
              BuildCsgDataUnit(0x0001, 0xE0001000,
                               secondContent)));

  Expect(CountCsgFields(fields, "数据标识 DI") == 2,
         "可选字段加十五字节合法数据单元不得因总长十六而被吞成 PW");
  Expect(CsgFieldValue(fields, "消息认证码 PW").isEmpty(),
         "已配置的下一数据单元应优先于 PW 长度匹配");
  Expect(CountCsgFields(fields, "数据项解析错误") == 0,
         "PW 长度碰撞场景不应产生错位错误");
}

void TestShortUnknownBoundaryIsMarkedAmbiguous() {
  const QByteArray first =
      BuildCsgDataUnit(0x0000, 0xE0001000,
                       QByteArray::fromHex("1234"));
  const QByteArray unknownHeader =
      BuildCsgDataUnit(0x0001, 0x12345678);
  const QList<CsgUpstreamField> fields =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x88, 0x0A, 0x01, first + unknownHeader));

  Expect(CsgFieldValue(fields, "未解析应用数据") ==
             "010078563412",
         "短未知边界应保留全部候选原文");
  Expect(CsgFieldDescription(fields, "未解析应用数据")
             .contains("可选字段与下一未知 DI 无法唯一分界"),
         "短未知数据单元与可选字段冲突时必须明确标记歧义");
  Expect(CountCsgFields(fields, "是否具备安全模块") == 0,
         "短未知数据单元不得被猜测为上一组可选字段");
}

void TestCompleteOptionalFieldsAtEndRemainParseable() {
  const QByteArray completeContent =
      QByteArray::fromHex("123401250102030405");
  const QList<CsgUpstreamField> fields =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x88, 0x0A, 0x01,
          BuildCsgDataUnit(0x0000, 0xE0001000,
                           completeContent)));

  Expect(CsgFieldValue(fields, "是否具备安全模块") == "01",
         "末组精确七字节可选尾应按已知 DI 字段解析");
  Expect(CsgFieldValue(fields, "终端时间") == "250102030405",
         "完整可选终端时间不应被误标为未知 DI 歧义");
  Expect(CsgFieldValue(fields, "未解析应用数据").isEmpty(),
         "完整已知可选字段不应回退为原文");
}

void TestUnknownDiStillKeepsRequiredTailsSeparated() {
  const QByteArray unknown =
      BuildCsgDataUnit(0x0000, 0x12345678,
                       QByteArray::fromHex("010203"));
  const QByteArray password =
      QByteArray::fromHex("00112233445566778899AABBCCDDEEFF");
  const QByteArray timeTag = QByteArray::fromHex("0414273405");
  const QList<CsgUpstreamField> fields =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x08, 0x04, 0x81, unknown + password + timeTag));

  Expect(CsgFieldValue(fields, "未解析应用数据") ==
             "78563412010203",
         "未知 DI 原文应从 DI 开始保留且不得包含 PW/Tp");
  Expect(CsgFieldValue(fields, "消息认证码 PW") ==
             "00112233445566778899AABBCCDDEEFF",
         "未知 DI 不得丢失已识别的必选 PW");
  Expect(CsgFieldValue(fields, "时间标签 Tp") == "0414273405",
         "未知 DI 不得丢失已识别的 Tp");
  Expect(CountCsgFields(fields, "数据项解析错误") == 0,
         "未知 DI 原文保留不应回退为整段解析错误");
}

void TestVariableContentMarksOptionalPasswordAmbiguity() {
  const QByteArray variableContent =
      QByteArray::fromHex("00112233445566778899AABBCCDDEEFF01020304");
  const QList<CsgUpstreamField> timedContentFields =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x88, 0x0D, 0x01,
          BuildCsgDataUnit(0x0000, 0x12345678,
                           variableContent)));
  Expect(CsgFieldDescription(timedContentFields,
                             "未展开服务内容")
             .contains("可选 PW 无法唯一分界"),
         "变长历史内容应明确标记可选 PW 边界歧义");
  Expect(CsgFieldValue(timedContentFields,
                       "消息认证码 PW")
             .isEmpty(),
         "变长内容无法分界时不得猜测 PW");

  const QByteArray taskDefined =
      BuildCsgDataUnit(0x0000, 0xE0000301,
                       QByteArray(1, '\x01') + variableContent);
  const QList<CsgUpstreamField> blobFields =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x88, 0x12, 0x01, taskDefined));
  Expect(CsgFieldDescription(blobFields,
                             "未解析应用数据")
             .contains("可选 PW 无法唯一分界"),
         "Choice 尾部 Blob 应明确标记可选 PW 边界歧义");
  Expect(CsgFieldValue(blobFields, "消息认证码 PW").isEmpty(),
         "Choice 尾部 Blob 不得吞并或猜测 PW");

  const QByteArray unknownChoice =
      BuildCsgDataUnit(0x0000, 0xE0000301,
                       QByteArray(1, '\x02') + variableContent.left(16));
  const QList<CsgUpstreamField> unknownChoiceFields =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x88, 0x12, 0x01, unknownChoice));
  Expect(CsgFieldDescription(unknownChoiceFields,
                             "未解析应用数据")
             .contains("可选 PW 无法唯一分界"),
         "未知 Choice 值也应明确标记可选 PW 边界歧义");
  Expect(CsgFieldValue(unknownChoiceFields,
                       "消息认证码 PW")
             .isEmpty(),
         "未知 Choice 值不得猜测 PW");
}

void TestOpaqueSecurityServicesKeepTheirTailsSeparated() {
  const QList<CsgUpstreamField> customFields =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x88, 0x15, 0x01,
          BuildCsgDataUnit(0x0000, 0xE0002000,
                           QByteArray::fromHex("010203"))));
  Expect(CsgFieldValue(customFields, "厂商编码") == "0102" &&
             CsgFieldValue(customFields, "厂商自定义数据") == "03",
         "AFN15 厂商自定义内容应保持原文");

  const QByteArray password =
      QByteArray::fromHex("00112233445566778899AABBCCDDEEFF");
  const QByteArray timeTag = QByteArray::fromHex("0414273405");
  for (quint8 afn : {quint8{0x16}, quint8{0x17}}) {
    const QList<CsgUpstreamField> fields =
        CsgUpstreamParser().parse(BuildCsgApplicationFrame(
            0x88, afn, 0x81,
            BuildCsgDataUnit(0x0000, 0x12345678,
                             QByteArray::fromHex("A1B2C3")) +
                password + timeTag));
    Expect(CsgFieldValue(fields, "未解析应用数据")
               .contains("A1B2C3"),
           QString("AFN%1 安全载荷原文不得包含 PW/Tp")
               .arg(afn, 2, 16, QLatin1Char('0'))
               .toUpper());
    Expect(CsgFieldValue(fields, "消息认证码 PW") ==
               "00112233445566778899AABBCCDDEEFF",
           "AFN16/17 应分离必选 PW");
    Expect(CsgFieldValue(fields, "时间标签 Tp") == "0414273405",
           "AFN16/17 应按 TpV 分离 Tp");
  }
}

void TestFileAndRelayServicesMatchTheSpecifiedTailLayout() {
  const CsgUpstreamAfnDef *fileTransfer =
      CsgUpstreamConfigManager::instance().getAfnDef(0x0F);
  const CsgUpstreamAfnDef *relay =
      CsgUpstreamConfigManager::instance().getAfnDef(0x10);
  Expect(fileTransfer != nullptr && relay != nullptr,
         "AFN0F/10 服务应已配置");
  if (!fileTransfer || !relay) {
    return;
  }

  for (const QString &direction : {QString("down"), QString("up")}) {
    const CsgUpstreamServiceDef fileService =
        fileTransfer->services.value(direction);
    const CsgUpstreamServiceDef relayService =
        relay->services.value(direction);
    Expect(fileService.passwordPolicy == CsgUpstreamTailPolicy::None &&
               !fileService.timeTagFromSeq,
           "规范图28～30中的 AFN0F 不包含 PW/Tp");
    Expect(fileService.layout == CsgUpstreamServiceLayout::Content,
           "AFN0F 应先进入 G 类 DI 分派，再按方向选择 ERR 或内容");
    Expect(!relayService.timeTagFromSeq,
           "规范图31～32中的 AFN10 不包含 Tp");
  }

  const QList<CsgUpstreamField> fields =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x88, 0x0F, 0x01,
          BuildCsgDataUnit(0x0000, 0xE3010003,
                           QByteArray::fromHex("010203"))));
  Expect(CsgFieldValue(fields, "未解析应用数据")
             .contains("010203"),
         "AFN0F 查询文件信息上行内容应按原文保留");
  Expect(CsgFieldValue(fields, "错误码 ERR").isEmpty(),
         "AFN0F 查询文件信息上行不得被误解析为 ERR");
}

void TestTaskDataOuterTime() {
  const QByteArray taskContent =
      QByteArray::fromHex(
          "00010100000001010200222512041415");
  const QByteArray content =
      taskContent + QByteArray::fromHex("202512041415");
  const QList<CsgUpstreamField> fields =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x88, 0x12, 0x72,
          BuildCsgDataUnit(0x0000, 0xE0000301, content)));
  Expect(CsgFieldDescription(fields, "任务数据时间") ==
             "2025-12-04 14:15",
         "AFN12 自描述任务内容后应解析四位年任务数据时间");
  Expect(CsgFieldDescription(fields, "数据项解析错误").isEmpty(),
         "合法 AFN12 任务数据时间不得留下未定义字节");

  const QByteArray password = QByteArray::fromHex(
      "00112233445566778899202512041415");
  const QList<CsgUpstreamField> passwordFields =
      CsgUpstreamParser().parse(BuildCsgApplicationFrame(
          0x88, 0x12, 0x72,
          BuildCsgDataUnit(
              0x0000, 0xE0000301,
              taskContent + QByteArray::fromHex("202512041416") +
                  password)));
  Expect(CsgFieldDescription(passwordFields, "任务数据时间") ==
             "2025-12-04 14:16" &&
             CsgFieldValue(passwordFields, "消息认证码 PW") ==
                 "00112233445566778899202512041415",
         "PW 末 6 字节即使构成合法时间，也不得覆盖真正的任务数据时间");
  Expect(CsgFieldDescription(passwordFields, "数据项解析错误").isEmpty(),
         "任务数据时间与可选 PW 的严格布局不应产生解析错误");
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

  TestEveryAfnIsConfigured();
  TestMultipleDataUnitsAreParsedIndependently();
  TestTimeTagUsesTheSequenceBoundary();
  TestRequiredPasswordUsesTheServiceBoundary();
  TestTruncatedTailsReportTheirOwnBoundary();
  TestOptionalPasswordDoesNotSwallowAValidDataUnit();
  TestUnknownDiPreservesRawDataAtOptionalPasswordBoundary();
  TestOptionalDiFieldsDoNotConsumeTheNextDataUnit();
  TestOptionalDiFieldsStopBeforeOptionalPassword();
  TestKnownDataUnitTakesPriorityOverPasswordLengthCollision();
  TestShortUnknownBoundaryIsMarkedAmbiguous();
  TestCompleteOptionalFieldsAtEndRemainParseable();
  TestUnknownDiStillKeepsRequiredTailsSeparated();
  TestVariableContentMarksOptionalPasswordAmbiguity();
  TestOpaqueSecurityServicesKeepTheirTailsSeparated();
  TestFileAndRelayServicesMatchTheSpecifiedTailLayout();
  TestTaskDataOuterTime();

  if (g_failureCount == 0) {
    QTextStream(stdout) << "All CSG upstream service tests passed.\n";
  }
  return g_failureCount == 0 ? 0 : 1;
}
