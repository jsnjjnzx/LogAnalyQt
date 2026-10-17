#include "csg_upstream/CsgUpstreamConfigManager.h"
#include "csg_upstream/CsgUpstreamParser.h"
#include "ProtocolDetector.h"

#include <QDir>
#include <QFile>
#include <QList>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

static_assert(static_cast<int>(ProtocolType::Protocol645) == 7);
static_assert(static_cast<int>(ProtocolType::ProtocolCsgUpstream) == 8);

int g_failureCount = 0;

void Expect(bool condition, const QString &message) {
  if (condition) {
    return;
  }

  QTextStream(stderr) << "FAIL: " << message << '\n';
  ++g_failureCount;
}

QString WriteXml(const QTemporaryDir &dir, const QString &name,
                 const QString &content) {
  const QString path = QDir(dir.path()).filePath(name);
  QFile file(path);
  Expect(file.open(QIODevice::WriteOnly | QIODevice::Text),
         "应能写入临时 XML: " + path);
  if (file.isOpen()) {
    file.write(content.toUtf8());
    file.close();
  }
  return path;
}

QString ValidDocument(const QString &afnBody,
                      const QString &diBody) {
  return QString(
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
             "<ProtCsgUpstreamConfig version=\"v1.0.1\" "
             "date=\"2026-08-04\"><AFNs>%1</AFNs>"
             "<DIs>%2</DIs></ProtCsgUpstreamConfig>")
      .arg(afnBody, diBody);
}

QMap<QString, QByteArray> LoadFrames(const QString &path) {
  QMap<QString, QByteArray> frames;
  QFile file(path);
  Expect(file.open(QIODevice::ReadOnly | QIODevice::Text),
         "应能打开黄金帧文件: " + path);
  while (!file.atEnd()) {
    const QByteArray line = file.readLine().trimmed();
    if (line.isEmpty()) {
      continue;
    }
    const int separator = line.indexOf('|');
    Expect(separator > 0, "黄金帧行应包含名称分隔符");
    if (separator <= 0) {
      continue;
    }
    frames.insert(QString::fromUtf8(line.left(separator)),
                  QByteArray::fromHex(line.mid(separator + 1)));
  }
  return frames;
}

QString FieldValue(const QList<CsgUpstreamField> &fields,
                   const QString &name) {
  for (const CsgUpstreamField &field : fields) {
    if (field.name == name) {
      return field.value;
    }
  }
  return QString();
}

QString FieldDescription(const QList<CsgUpstreamField> &fields,
                         const QString &name) {
  for (const CsgUpstreamField &field : fields) {
    if (field.name == name) {
      return field.description;
    }
  }
  return QString();
}

bool HasField(const QList<CsgUpstreamField> &fields,
              const QString &name) {
  for (const CsgUpstreamField &field : fields) {
    if (field.name == name) {
      return true;
    }
  }
  return false;
}

QByteArray BuildFrame(quint8 control, quint8 afn, quint8 seq,
                      quint16 da, quint32 di,
                      const QByteArray &payload) {
  QByteArray user;
  user.append(static_cast<char>(control));
  user.append(QByteArray::fromHex("25314403041100"));
  user.append(static_cast<char>(afn));
  user.append(static_cast<char>(seq));
  user.append(static_cast<char>(da & 0xFF));
  user.append(static_cast<char>((da >> 8) & 0xFF));
  for (int shift = 0; shift <= 24; shift += 8) {
    user.append(static_cast<char>((di >> shift) & 0xFF));
  }
  user.append(payload);

  const quint16 length = static_cast<quint16>(user.size());
  QByteArray frame;
  frame.append('\x68');
  frame.append(static_cast<char>(length & 0xFF));
  frame.append(static_cast<char>((length >> 8) & 0xFF));
  frame.append(static_cast<char>(length & 0xFF));
  frame.append(static_cast<char>((length >> 8) & 0xFF));
  frame.append('\x68');
  frame.append(user);

  quint8 checksum = 0;
  for (char byte : user) {
    checksum = static_cast<quint8>(
        checksum + static_cast<quint8>(byte));
  }
  frame.append(static_cast<char>(checksum));
  frame.append('\x16');
  return frame;
}

void TestConfigLoads(const QString &xmlPath) {
  QString error;
  auto &manager = CsgUpstreamConfigManager::instance();
  Expect(manager.loadXml(xmlPath, &error),
         "首期 XML 应加载成功: " + error);
  Expect(manager.isLoaded(), "加载成功后 isLoaded 应为 true");
  Expect(manager.getVersion() == "v2.0.0", "版本应来自 XML");
  Expect(manager.getDate() == "2026-08-05", "日期应来自 XML");
  const CsgUpstreamAfnDef *taskAfn = manager.getAfnDef(0x12);
  Expect(taskAfn && taskAfn->name == "读任务数据",
         "AFN 12 应从独立 AFN 表查询");

  const CsgUpstreamDiDef *login =
      manager.getDiDef("E0001000");
  Expect(login != nullptr, "应找到终端登录 DI");
  Expect(login && login->name == "终端登录", "DI 名称应来自 XML");
  Expect(login && login->fields.size() == 3, "登录应包含三个字段定义");

  const CsgUpstreamDiDef *confirm =
      manager.getDiDef("E0000000");
  Expect(confirm != nullptr, "应从独立 DI 表找到确认数据项");
  Expect(confirm && !confirm->fields.isEmpty() &&
             confirm->fields.first().type == CsgUpstreamFieldType::Bin &&
             confirm->fields.first().display == "ConfirmationResult" &&
             confirm->fields.first().enumValues.isEmpty(),
         "确认结果应保持规范 BIN 类型并由业务显示规则解释");

  const CsgUpstreamDiDef *forwardEnergy =
      manager.getDiDef("050601FF");
  Expect(forwardEnergy &&
             forwardEnergy->name == "日冻结正向有功电能数据块",
         "应从同一张 DI 表精确查询 050601FF");
  Expect(forwardEnergy && forwardEnergy->fields.size() == 3,
         "050601FF 应包含费率数、总电能和重复费率电能定义");
  const CsgUpstreamDiDef *reverseEnergy =
      manager.getDiDef("050602FF");
  Expect(reverseEnergy &&
             reverseEnergy->name == "日冻结反向有功电能数据块",
         "应从同一张 DI 表精确查询 050602FF");

  Expect(manager.getDiName("E00003XX") == "普通任务号",
         "配置管理器应按 E00003XX 字符串精确查表");
  Expect(manager.getDiDef("E000033E") == nullptr,
         "配置管理器不应把实际任务 DI 当作 XML 通配命中");
}

void TestOrdinaryTaskParameterDefinition() {
  const CsgUpstreamDiDef *definition =
      CsgUpstreamConfigManager::instance().getDiDef("E00003XX");
  Expect(definition != nullptr, "应找到 E00003XX 普通任务参数定义");
  if (!definition) {
    return;
  }

  Expect(definition->type == "Struct", "E00003XX 应为 Struct");
  Expect(definition->fields.size() == 14,
         "E00003XX 应按表 A.10.2 定义 14 个字段");
  if (definition->fields.size() != 14) {
    return;
  }

  const QStringList expectedNames = {
      "有效性标志",       "上报基准时间",       "定时上报周期单位",
      "定时上报周期",     "数据结构方式",       "采样基准时间",
      "定时采样周期单位", "定时采样周期",       "数据抽取倍率",
      "执行次数",         "信息点标识组数 n",   "信息点标识",
      "数据标识编码组数 m", "数据标识编码"};
  for (int index = 0; index < expectedNames.size(); ++index) {
    Expect(definition->fields.at(index).name == expectedNames.at(index),
           QString("E00003XX 第 %1 个字段应为 %2")
               .arg(index + 1)
               .arg(expectedNames.at(index)));
  }

  for (int index = 0; index < definition->fields.size(); ++index) {
    const bool isTimeField = index == 1 || index == 5;
    const CsgUpstreamFieldType expectedType =
        isTimeField ? CsgUpstreamFieldType::BcdDateTime
                    : CsgUpstreamFieldType::Bin;
    Expect(definition->fields.at(index).type == expectedType,
           QString("E00003XX 第 %1 个字段的基础类型应符合表 A.10.2")
               .arg(index + 1));
    Expect(definition->fields.at(index).enumValues.isEmpty(),
           QString("E00003XX 第 %1 个字段不应配置枚举取值表")
               .arg(index + 1));
  }
}

QByteArray OrdinaryTaskParameterPayload() {
  return QByteArray::fromHex(
      "01251204083002010025120400000106020A00"
      "020101020102FF010605FF020605");
}

QByteArray OrdinaryTaskParameterFrame(const QByteArray &payload) {
  return BuildFrame(0x88, 0x0A, 0x71, 0x0000, 0xE000033E,
                    payload);
}

void TestOrdinaryTaskParameterParsing() {
  CsgUpstreamParser parser;
  const auto fields =
      parser.parse(OrdinaryTaskParameterFrame(
          OrdinaryTaskParameterPayload()));

  Expect(FieldDescription(fields, "数据标识 DI").contains(
             "E000033E 普通任务号62"),
         "实际任务 DI 应归一化到 E00003XX 并显示任务号 62");
  Expect(FieldValue(fields, "有效性标志") == "01",
         "有效性标志的数据列应为原始字节 01");
  Expect(FieldDescription(fields, "有效性标志") ==
             "有效（0：无效；1：有效）",
         "有效性标志的说明列应为业务含义和协议说明");
  Expect(FieldValue(fields, "上报基准时间") == "2512040830",
         "上报基准时间的数据列应为五字节原文");
  Expect(FieldDescription(fields, "上报基准时间") ==
             "2025-12-04 08:30",
         "上报基准时间的说明列应为时间字符串");
  Expect(FieldValue(fields, "定时上报周期单位") == "02",
         "定时上报周期单位的数据列应为原始字节 02");
  Expect(FieldDescription(fields, "定时上报周期单位") ==
             "日（0~3 依次表示分、时、日、月）",
         "定时上报周期单位应解析为日并追加协议说明");
  Expect(FieldValue(fields, "定时上报周期") == "01",
         "定时上报周期的数据列应为原始字节 01");
  Expect(FieldDescription(fields, "定时上报周期") ==
             "1（终端以设定的时间开始上报，并按定时上报周期循环；0 表示不上报）",
         "定时上报周期的说明列应为数值和协议说明");
  Expect(FieldValue(fields, "数据结构方式") == "00",
         "数据结构方式的数据列应为原始字节 00");
  Expect(FieldDescription(fields, "数据结构方式") ==
             "自描述格式（0：自描述格式组织数据；1：按任务定义的数据格式组织数据）",
         "数据结构方式的说明列应为业务含义和协议说明");
  Expect(FieldValue(fields, "采样基准时间") == "2512040000",
         "采样基准时间的数据列应为五字节原文");
  Expect(FieldDescription(fields, "采样基准时间") ==
             "2025-12-04 00:00",
         "采样基准时间的说明列应为时间字符串");
  Expect(FieldValue(fields, "定时采样周期单位") == "01",
         "定时采样周期单位的数据列应为原始字节 01");
  Expect(FieldDescription(fields, "定时采样周期单位") ==
             "时（0~3 依次表示分、时、日、月）",
         "定时采样周期单位应解析为时并追加协议说明");
  Expect(FieldValue(fields, "定时采样周期") == "06",
         "定时采样周期的数据列应为原始字节 06");
  Expect(FieldDescription(fields, "定时采样周期") ==
             "6（终端以设定的时间开始采集，并按定时采样周期循环）",
         "定时采样周期的说明列应为数值和协议说明");
  Expect(FieldValue(fields, "数据抽取倍率") == "02",
         "数据抽取倍率的数据列应为原始字节 02");
  Expect(FieldDescription(fields, "数据抽取倍率") ==
             "2（按照此倍率抽取数据上送；如抽取倍率为 2，表示采样 2 点、上送最近的 1 点）",
         "数据抽取倍率的说明列应为数值和协议说明");
  Expect(FieldValue(fields, "执行次数") == "0A00",
         "执行次数的数据列应保持报文小端字节顺序");
  Expect(FieldDescription(fields, "执行次数") ==
             "10（0 表示永远执行；非 0 表示任务执行次数）",
         "执行次数的说明列应为十进制数值和协议说明");
  Expect(FieldValue(fields, "信息点标识组数 n") == "02",
         "信息点标识组数的数据列应为原始字节 02");
  Expect(FieldDescription(fields, "信息点标识组数 n") == "2",
         "信息点标识组数的说明列应为十进制数值");
  Expect(FieldValue(fields, "信息点标识[1]") == "0101",
         "第 1 个信息点的数据列应为两字节原文");
  Expect(FieldDescription(fields, "信息点标识[1]") ==
             "第 1 组 p1（信息点标识参见 6.1.3）",
         "第 1 个信息点的说明列应为 DA 解析结果");
  Expect(FieldValue(fields, "信息点标识[2]") == "0201",
         "第 2 个信息点的数据列应为两字节原文");
  Expect(FieldDescription(fields, "信息点标识[2]") ==
             "第 1 组 p2（信息点标识参见 6.1.3）",
         "第 2 个信息点的说明列应为 DA 解析结果");
  Expect(FieldValue(fields, "数据标识编码组数 m") == "02",
         "数据标识编码组数的数据列应为原始字节 02");
  Expect(FieldDescription(fields, "数据标识编码组数 m") ==
             "2（n×m 的值应小于 256）",
         "数据标识编码组数的说明列应为数值和乘积约束");
  Expect(FieldValue(fields, "数据标识编码[1]") == "FF010605",
         "第 1 个数据标识的数据列应保持报文字节顺序");
  Expect(FieldDescription(fields, "数据标识编码[1]") ==
             "050601FF 日冻结正向有功电能数据块（数据标识编码参见 6.1.4）",
         "第 1 个数据标识的说明列应为正常阅读顺序和名称");
  Expect(FieldValue(fields, "数据标识编码[2]") == "FF020605",
         "第 2 个数据标识的数据列应保持报文字节顺序");
  Expect(FieldDescription(fields, "数据标识编码[2]") ==
             "050602FF 日冻结反向有功电能数据块（数据标识编码参见 6.1.4）",
         "第 2 个数据标识的说明列应为正常阅读顺序和名称");
  Expect(!HasField(fields, "数据项解析错误"),
         "完整普通任务参数不应产生解析错误");
  Expect(FieldValue(fields, "未解析应用数据").isEmpty(),
         "完整普通任务参数不应保留未解析数据");

  QByteArray unknownValidity = OrdinaryTaskParameterPayload();
  unknownValidity[0] = '\x02';
  Expect(FieldDescription(
             parser.parse(OrdinaryTaskParameterFrame(unknownValidity)),
             "有效性标志") == "未知有效性标志 2（0：无效；1：有效）",
         "未知有效性值应保留数值并明确标记未知");

  QByteArray unknownPeriodUnit = OrdinaryTaskParameterPayload();
  unknownPeriodUnit[6] = '\x04';
  const auto unknownPeriodFields =
      parser.parse(OrdinaryTaskParameterFrame(unknownPeriodUnit));
  Expect(FieldValue(unknownPeriodFields, "定时上报周期单位") == "04",
         "未知周期单位的数据列应保留原始字节 04");
  Expect(FieldDescription(unknownPeriodFields, "定时上报周期单位") ==
             "未知周期单位 4（0~3 依次表示分、时、日、月）",
         "未知周期单位应保留数值并明确标记未知");

  QByteArray unknownStructureMode = OrdinaryTaskParameterPayload();
  unknownStructureMode[8] = '\x02';
  Expect(FieldDescription(
             parser.parse(OrdinaryTaskParameterFrame(
                 unknownStructureMode)),
             "数据结构方式") ==
             "未知数据结构方式 2（0：自描述格式组织数据；1：按任务定义的数据格式组织数据）",
         "未知数据结构方式应保留数值并明确标记未知");

  QByteArray invalidPayload = QByteArray::fromHex(
      "01251204083002010025120400000106020A0010");
  for (int index = 0; index < 16; ++index) {
    invalidPayload.append(QByteArray::fromHex("0101"));
  }
  invalidPayload.append('\x10');
  const auto invalidFields =
      parser.parse(OrdinaryTaskParameterFrame(invalidPayload));
  Expect(FieldDescription(invalidFields, "数据项解析错误").contains(
             "必须小于 256"),
         "n×m 达到 256 时应返回明确的乘积约束错误");
  const QByteArray invalidServiceData =
      QByteArray::fromHex("00003E0300E0") + invalidPayload;
  Expect(FieldValue(invalidFields, "未解析应用数据") ==
             QString::fromLatin1(invalidServiceData.toHex()).toUpper(),
         "乘积约束失败时应从 DA 开始保留完整 Service 原文");
}

void TestInvalidXml(const QString &validXmlPath) {
  QTemporaryDir dir;
  Expect(dir.isValid(), "应能创建配置校验临时目录");
  if (!dir.isValid()) {
    return;
  }

  struct InvalidXmlCase {
    QString name;
    QString xml;
    QString expectedError;
  };

  const QList<InvalidXmlCase> cases = {
      {"wrong-root", "<Wrong version=\"v1\" date=\"2026-08-04\"/>",
       "根节点应为 <ProtCsgUpstreamConfig>"},
      {"empty-version",
       "<ProtCsgUpstreamConfig version=\"\" "
       "date=\"2026-08-04\"/>",
       "version 不能为空"},
      {"bad-date",
       "<ProtCsgUpstreamConfig version=\"v1\" "
       "date=\"2026-02-30\"/>",
       "date"},
      {"missing-afns",
       "<ProtCsgUpstreamConfig version=\"v1\" date=\"2026-08-04\">"
       "<DIs/></ProtCsgUpstreamConfig>",
       "AFNs"},
      {"missing-dis",
       "<ProtCsgUpstreamConfig version=\"v1\" date=\"2026-08-04\">"
       "<AFNs/></ProtCsgUpstreamConfig>",
       "DIs"},
      {"duplicate-afns-container",
       "<ProtCsgUpstreamConfig version=\"v1\" date=\"2026-08-04\">"
       "<AFNs/><AFNs/><DIs/></ProtCsgUpstreamConfig>",
       "AFNs"},
      {"duplicate-dis-container",
       "<ProtCsgUpstreamConfig version=\"v1\" date=\"2026-08-04\">"
       "<AFNs/><DIs/><DIs/></ProtCsgUpstreamConfig>",
       "DIs"},
      {"nested-old-model",
       "<ProtCsgUpstreamConfig version=\"v1\" date=\"2026-08-04\">"
       "<AFN code=\"12\" name=\"读任务数据\">"
       "<DI code=\"E000033E\" name=\"普通任务号62\" type=\"None\"/>"
       "</AFN></ProtCsgUpstreamConfig>",
       "AFNs"},
      {"empty-afn-name",
       ValidDocument("<AFN code=\"12\" name=\"\"/>", QString()),
       "AFN name 不能为空"},
      {"empty-di-name",
       ValidDocument(QString(),
                     "<DI code=\"E000033E\" name=\"\" type=\"None\"/>"),
       "DI name 不能为空"},
      {"legacy-direction",
       ValidDocument(
           QString(),
           "<DI code=\"E000033E\" name=\"任务\" type=\"None\" "
           "direction=\"up\"/>"),
       "不允许属性 direction"},
      {"legacy-ref",
       ValidDocument(
           QString(),
           "<DI code=\"E000033E\" name=\"任务\" type=\"None\" "
           "ref=\"AFN12\"/>"),
       "不允许属性 ref"},
      {"legacy-pattern-range",
       ValidDocument(
           QString(),
           "<DI code=\"E00003XX\" name=\"任务\" min=\"01\" "
           "max=\"FD\" type=\"ServiceDefined\"/>"),
       "不支持 min/max"},
      {"legacy-service-ref",
       ValidDocument(
           "<AFN code=\"12\" name=\"任务\">"
           "<Service direction=\"up\" type=\"Struct\" "
           "ref=\"E00003XX\"/>"
           "</AFN>",
           QString()),
       "不允许属性 ref"},
      {"legacy-selector-ref",
       ValidDocument(
           "<AFN code=\"12\" name=\"任务\">"
           "<Service direction=\"up\" type=\"Choice\">"
           "<Selector name=\"方式\" type=\"BIN\" length=\"1\" "
           "ref=\"E00003XX\"/>"
           "<Case value=\"0\" name=\"A\"/>"
           "</Service></AFN>",
           QString()),
       "不允许属性 ref"},
      {"legacy-case-binding",
       ValidDocument(
           "<AFN code=\"12\" name=\"任务\">"
           "<Service direction=\"up\" type=\"Choice\">"
           "<Selector name=\"方式\" type=\"BIN\" length=\"1\"/>"
           "<Case value=\"0\" name=\"A\" binding=\"E00003XX\"/>"
           "</Service></AFN>",
           QString()),
       "不允许属性 binding"},
      {"legacy-field-schema",
       ValidDocument(
           "<AFN code=\"12\" name=\"任务\">"
           "<Service direction=\"down\" type=\"Struct\">"
           "<Field name=\"数量\" type=\"BIN\" length=\"1\" "
           "schema=\"DI\"/>"
           "</Service></AFN>",
           QString()),
       "不允许属性 schema"},
      {"duplicate-service-direction",
       ValidDocument(
           "<AFN code=\"12\" name=\"任务\">"
           "<Service direction=\"up\" type=\"Struct\"/>"
           "<Service direction=\"up\" type=\"Struct\"/>"
           "</AFN>",
           QString()),
       "重复 Service direction"},
      {"bad-service-type",
       ValidDocument(
           "<AFN code=\"12\" name=\"任务\">"
           "<Service direction=\"up\" type=\"Array\"/>"
           "</AFN>",
           QString()),
       "Service type"},
      {"data-units-missing-layout",
       ValidDocument(
           "<AFN code=\"0A\" name=\"读参数\">"
           "<Service direction=\"up\" type=\"DataUnits\" "
           "cardinality=\"many\" pw=\"optional\" tp=\"seq\"/>"
           "</AFN>",
           QString()),
       "layout 不受支持"},
      {"bad-service-cardinality",
       ValidDocument(
           "<AFN code=\"0A\" name=\"读参数\">"
           "<Service direction=\"up\" type=\"DataUnits\" "
           "layout=\"content\" cardinality=\"some\"/>"
           "</AFN>",
           QString()),
       "cardinality 只能是 one 或 many"},
      {"bad-service-password-policy",
       ValidDocument(
           "<AFN code=\"0A\" name=\"读参数\">"
           "<Service direction=\"up\" type=\"DataUnits\" "
           "layout=\"content\" cardinality=\"many\" pw=\"always\"/>"
           "</AFN>",
           QString()),
       "pw 只能是 none、optional 或 required"},
      {"bad-service-time-tag-policy",
       ValidDocument(
           "<AFN code=\"0A\" name=\"读参数\">"
           "<Service direction=\"up\" type=\"DataUnits\" "
           "layout=\"content\" cardinality=\"many\" tp=\"fixed\"/>"
           "</AFN>",
           QString()),
       "tp 只能是 none 或 seq"},
      {"unknown-service-attribute",
       ValidDocument(
           "<AFN code=\"0A\" name=\"读参数\">"
           "<Service direction=\"up\" type=\"DataUnits\" "
           "layout=\"content\" cardinality=\"many\" "
           "pww=\"required\"/>"
           "</AFN>",
           QString()),
       "不允许属性 pww"},
      {"data-units-with-child",
       ValidDocument(
           "<AFN code=\"0A\" name=\"读参数\">"
           "<Service direction=\"up\" type=\"DataUnits\" "
           "layout=\"content\" cardinality=\"many\">"
           "<Field name=\"错误字段\" type=\"BIN\" length=\"1\"/>"
           "</Service></AFN>",
           QString()),
       "DataUnits 不能包含子元素"},
      {"legacy-service-with-data-units-layout",
       ValidDocument(
           "<AFN code=\"12\" name=\"任务\">"
           "<Service direction=\"down\" type=\"Struct\" "
           "layout=\"content\"/>"
           "</AFN>",
           QString()),
       "layout/cardinality 仅适用于 DataUnits"},
      {"choice-without-selector",
       ValidDocument(
           "<AFN code=\"12\" name=\"任务\">"
           "<Service direction=\"up\" type=\"Choice\">"
           "<Case value=\"0\" name=\"自描述\"/>"
           "</Service></AFN>",
           QString()),
       "Choice 必须包含一个 Selector"},
      {"duplicate-case-value",
       ValidDocument(
           "<AFN code=\"12\" name=\"任务\">"
           "<Service direction=\"up\" type=\"Choice\">"
           "<Selector name=\"方式\" type=\"BIN\" length=\"1\"/>"
           "<Case value=\"0\" name=\"A\"/>"
           "<Case value=\"0\" name=\"B\"/>"
           "</Service></AFN>",
           QString()),
       "重复 Case value"},
      {"blob-not-last",
       ValidDocument(
           "<AFN code=\"12\" name=\"任务\">"
           "<Service direction=\"up\" type=\"Choice\">"
           "<Selector name=\"方式\" type=\"BIN\" length=\"1\"/>"
           "<Case value=\"0\" name=\"A\">"
           "<Field name=\"原文\" type=\"Blob\" length=\"-1\"/>"
           "<Field name=\"数量\" type=\"BIN\" length=\"1\"/>"
           "</Case></Service></AFN>",
           QString()),
       "Blob 必须位于最后"},
      {"bad-afn",
       ValidDocument("<AFN code=\"0G\" name=\"错误\"/>", QString()),
       "AFN code"},
      {"duplicate-afn",
       ValidDocument("<AFN code=\"02\" name=\"A\"/>"
                     "<AFN code=\"02\" name=\"B\"/>",
                     QString()),
       "重复 AFN"},
      {"bad-di",
       ValidDocument(QString(),
                     "<DI code=\"E00010Z\" name=\"A\" type=\"None\"/>"),
       "DI code"},
      {"bad-di-non-hex-letter",
       ValidDocument(QString(),
                     "<DI code=\"E0001G00\" name=\"A\" type=\"None\"/>"),
       "DI code"},
      {"duplicate-di",
       ValidDocument(QString(),
                     "<DI code=\"E0001000\" name=\"A\" type=\"None\"/>"
                     "<DI code=\"E0001000\" name=\"B\" type=\"None\"/>"),
       "重复 DI"},
      {"bad-di-type",
       ValidDocument(QString(),
                     "<DI code=\"E0001000\" name=\"A\" type=\"Array\"/>"),
       "DI type"},
      {"none-with-field",
       ValidDocument(
           QString(),
           "<DI code=\"E0001000\" name=\"A\" type=\"None\">"
           "<Field name=\"A\" type=\"BCD\" length=\"1\"/>"
           "</DI>"),
       "None 类型不能包含 Field"},
      {"bad-field-type",
       ValidDocument(
           QString(),
           "<DI code=\"E0001000\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"string\" length=\"1\"/>"
           "</DI>"),
       "Field type"},
      {"zero-length",
       ValidDocument(
           QString(),
           "<DI code=\"E0001000\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BCD\" length=\"0\"/>"
           "</DI>"),
       "length"},
      {"bad-required",
       ValidDocument(
           QString(),
           "<DI code=\"E0001000\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BCD\" length=\"1\" "
           "required=\"yes\"/>"
           "</DI>"),
       "required"},
      {"bad-byte-order",
       ValidDocument(
           QString(),
           "<DI code=\"E0001000\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BCD\" length=\"1\" "
           "byteOrder=\"big\"/>"
           "</DI>"),
       "byteOrder"},
      {"bad-datetime-length",
       ValidDocument(
           QString(),
           "<DI code=\"E0001000\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BCD_DATETIME\" length=\"4\"/>"
           "</DI>"),
       "BCD_DATETIME length"},
      {"bad-datetime-format",
       ValidDocument(
           QString(),
           "<DI code=\"E0001000\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BCD_DATETIME\" length=\"5\" "
           "format=\"YYMMDDhhmmss\"/>"
           "</DI>"),
       "format"},
      {"bad-yymmddhhmm-length",
       ValidDocument(
           QString(),
           "<DI code=\"E0001000\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"YYMMDDhhmm\" length=\"6\"/>"
           "</DI>"),
       "YYMMDDhhmm length 必须为 5"},
      {"format-on-yymmddhhmm",
       ValidDocument(
           QString(),
           "<DI code=\"E0001000\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"YYMMDDhhmm\" length=\"5\" "
           "format=\"YYMMDDhhmm\"/>"
           "</DI>"),
       "YYMMDDhhmm 不需要配置 format"},
      {"format-on-bin",
       ValidDocument(
           QString(),
           "<DI code=\"E0001000\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BIN\" length=\"1\" "
           "format=\"decimal\"/>"
           "</DI>"),
       "format 仅适用于 BCD 或 BCD_DATETIME"},
      {"duplicate-field-id",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field id=\"count\" name=\"A\" type=\"BIN\" length=\"1\"/>"
           "<Field id=\"count\" name=\"B\" type=\"BIN\" length=\"1\"/>"
           "</DI>"),
       "重复 Field id"},
      {"unknown-count-ref",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BCD\" length=\"4\" "
           "countRef=\"missing\"/>"
           "</DI>"),
       "countRef 必须引用此前的 BIN 或 BCD Field"},
      {"non-numeric-count-ref",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field id=\"count\" name=\"A\" type=\"ASCII\" length=\"1\"/>"
           "<Field name=\"B\" type=\"BCD\" length=\"1\" "
           "countRef=\"count\"/>"
           "</DI>"),
       "countRef 必须引用此前的 BIN 或 BCD Field"},
      {"count-ref-ff-without-ref",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BIN\" length=\"1\" "
           "countRefFF=\"1\"/>"
           "</DI>"),
       "countRefFF 必须与 countRef 同时配置且为正整数"},
      {"zero-count-ref-ff",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field id=\"count\" name=\"数量\" type=\"BIN\" length=\"1\"/>"
           "<Field name=\"A\" type=\"BIN\" length=\"1\" "
           "countRef=\"count\" countRefFF=\"0\"/>"
           "</DI>"),
       "countRefFF 必须与 countRef 同时配置且为正整数"},
      {"bad-display",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BIN\" length=\"2\" "
           "display=\"point\"/>"
           "</DI>"),
       "display 取值不受支持"},
      {"bad-validity-display-length",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BIN\" length=\"2\" "
           "display=\"ValidityFlag\"/>"
           "</DI>"),
       "display=ValidityFlag 仅适用于长度为 1 的 BIN Field"},
      {"bad-ipv4-display-length",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BIN\" length=\"3\" "
           "display=\"IPv4\"/>"
           "</DI>"),
       "display=IPv4 仅适用于长度为 4 的 BIN Field"},
      {"period-unit-display-on-bcd",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BCD\" length=\"1\" "
           "display=\"PeriodUnit\"/>"
           "</DI>"),
       "display=PeriodUnit 仅适用于长度为 1 的 BIN Field"},
      {"bad-structure-display-length",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BIN\" length=\"2\" "
           "display=\"DataStructureMode\"/>"
           "</DI>"),
       "display=DataStructureMode 仅适用于长度为 1 的 BIN Field"},
      {"bad-da-display-length",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BIN\" length=\"1\" "
           "display=\"DA\"/>"
           "</DI>"),
       "display=DA 仅适用于长度为 2"},
      {"di-display-without-little-endian",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BIN\" length=\"4\" "
           "display=\"DI\"/>"
           "</DI>"),
       "display=DI 必须配置 byteOrder=little"},
      {"incomplete-product-constraint",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BIN\" length=\"1\" "
           "productRef=\"count\"/>"
           "</DI>"),
       "productRef 与 productMaxExclusive 必须同时配置"},
      {"unknown-product-ref",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BIN\" length=\"1\" "
           "productRef=\"missing\" productMaxExclusive=\"256\"/>"
           "</DI>"),
       "productRef 必须引用此前的 BIN Field"},
      {"zero-product-maximum",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field id=\"count\" name=\"A\" type=\"BIN\" length=\"1\"/>"
           "<Field name=\"B\" type=\"BIN\" length=\"1\" "
           "productRef=\"count\" productMaxExclusive=\"0\"/>"
           "</DI>"),
       "productMaxExclusive 必须为正整数"},
      {"unknown-task-group-ref",
       ValidDocument(
           "<AFN code=\"12\" name=\"任务\">"
           "<Service direction=\"up\" type=\"Struct\">"
           "<Field id=\"n\" name=\"n\" type=\"BIN\" length=\"1\"/>"
           "<Field name=\"组\" type=\"DataItemGroups\" "
           "pointCountRef=\"n\" diCountRef=\"missing\"/>"
           "</Service></AFN>",
           QString()),
       "diCountRef 必须引用此前的 BIN Field"},
      {"task-groups-in-di",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field name=\"组\" type=\"DataItemGroups\" "
           "pointCountRef=\"n\" diCountRef=\"m\"/>"
           "</DI>"),
       "DataItemGroups 仅允许用于 Service"},
      {"bad-bcd-format",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BCD\" length=\"4\" "
           "format=\"NN.NN\"/>"
           "</DI>"),
       "format 中 N 的数量"},
      {"bad-bcd-fill",
       ValidDocument(
           QString(),
           "<DI code=\"050601FF\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BCD\" length=\"4\" "
           "fill=\"FFFF\"/>"
           "</DI>"),
       "fill 必须是两位十六进制"},
      {"enum-without-values",
       ValidDocument(
           QString(),
           "<DI code=\"E0001000\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"enum\" length=\"1\"/>"
           "</DI>"),
       "enum Field 必须包含 enum_value"},
      {"duplicate-enum",
       ValidDocument(
           QString(),
           "<DI code=\"E0001000\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"enum\" length=\"1\">"
           "<enum_value value=\"1\" name=\"A\"/>"
           "<enum_value value=\"1\" name=\"B\"/>"
           "</Field></DI>"),
       "重复 enum_value"},
      {"enum-out-of-range",
       ValidDocument(
           QString(),
           "<DI code=\"E0001000\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"enum\" length=\"1\">"
           "<enum_value value=\"256\" name=\"A\"/>"
           "</Field></DI>"),
       "超出"},
      {"enum-too-wide",
       ValidDocument(
           QString(),
           "<DI code=\"E0001000\" name=\"A\" type=\"Struct\">"
           "<Field name=\"A\" type=\"enum\" length=\"9\">"
           "<enum_value value=\"1\" name=\"A\"/>"
           "</Field></DI>"),
       "enum Field length 不能超过 8"},
      {"required-after-optional",
       ValidDocument(
           QString(),
           "<DI code=\"E0001000\" name=\"登录\" type=\"Struct\">"
           "<Field name=\"A\" type=\"BCD\" length=\"1\" "
           "required=\"false\"/>"
           "<Field name=\"B\" type=\"BCD\" length=\"1\"/>"
           "</DI>"),
       "必选字段不能位于可选字段之后"},
  };

  const auto &manager = CsgUpstreamConfigManager::instance();
  for (const InvalidXmlCase &testCase : cases) {
    const QString path = WriteXml(dir, testCase.name + ".xml", testCase.xml);
    QString error;
    Expect(!manager.validateXml(path, &error),
           testCase.name + " 应校验失败");
    Expect(error.contains(testCase.expectedError),
           testCase.name + " 错误应包含 '" + testCase.expectedError +
               "'，实际为: " + error);
    Expect(error.contains(testCase.name + ".xml"),
           testCase.name + " 错误应包含文件名，实际为: " + error);
    if (testCase.name == "bad-field-type") {
      Expect(error.count("Field type 不受支持") == 1,
             "未知 Field type 错误上下文不应重复: " + error);
    }
  }

  auto &mutableManager = CsgUpstreamConfigManager::instance();
  QString error;
  Expect(mutableManager.loadXml(validXmlPath, &error),
         "原子保留测试应先加载正式 XML: " + error);
  const QString invalidPath = WriteXml(
      dir, "atomic-invalid.xml",
      ValidDocument("<AFN code=\"GG\" name=\"错误\"/>", QString()));
  Expect(!mutableManager.loadXml(invalidPath, &error),
         "非法 XML 不应替换当前配置");
  Expect(mutableManager.getVersion() == "v2.0.0",
         "失败加载后应保留原版本");
  Expect(mutableManager.getDate() == "2026-08-05",
         "失败加载后应保留原日期");
  const CsgUpstreamAfnDef *taskAfn = mutableManager.getAfnDef(0x12);
  Expect(taskAfn && taskAfn->name == "读任务数据",
         "失败加载后应保留原 AFN 定义");
  const CsgUpstreamDiDef *login =
      mutableManager.getDiDef("E0001000");
  Expect(login && login->name == "终端登录",
         "失败加载后应保留原 DI 定义");
  Expect(mutableManager.getDiName("E00003XX") == "普通任务号",
         "失败加载后应保留 E00003XX 精确键定义");

  const QString independentKeysPath = WriteXml(
      dir, "independent-keys.xml",
      ValidDocument(
          QString(),
          "<DI code=\"E00003XX\" name=\"任务系列\" "
          "type=\"ServiceDefined\"/>"
          "<DI code=\"E000033E\" name=\"精确任务62\" type=\"None\"/>"));
  Expect(mutableManager.loadXml(independentKeysPath, &error),
         "两个字符串键应可独立加载: " + error);
  Expect(mutableManager.getDiName("E00003XX") == "任务系列" &&
             mutableManager.getDiName("E000033E") == "精确任务62",
         "配置管理器应把 E00003XX 与 E000033E 视为互不关联的精确键");
  Expect(mutableManager.loadXml(validXmlPath, &error),
         "独立键测试后应恢复正式 XML: " + error);
}

void TestStrictValidation(const QMap<QString, QByteArray> &frames) {
  const QByteArray login = frames.value("login");
  Expect(CsgUpstreamParser::getFrameLength(login) == 26,
         "登录帧长度应为 L+8=26");
  Expect(CsgUpstreamParser::isValidFrame(login),
         "登录帧应通过严格校验");
  Expect(CsgUpstreamParser::isValidFrame(
             login + frames.value("confirm")),
         "存在尾随帧时只校验第一帧");

  QByteArray badFirstStart = login;
  badFirstStart[0] = '\x67';
  Expect(CsgUpstreamParser::getFrameLength(badFirstStart) == 0,
         "首起始符错误不得返回帧长");

  QByteArray badRepeatedLength = login;
  badRepeatedLength[3] = '\x13';
  Expect(!CsgUpstreamParser::isValidFrame(badRepeatedLength),
         "重复长度不一致必须拒绝");

  QByteArray badSecondStart = login;
  badSecondStart[5] = '\x67';
  Expect(!CsgUpstreamParser::isValidFrame(badSecondStart),
         "第二起始符错误必须拒绝");

  QByteArray badChecksum = login;
  badChecksum[24] = static_cast<char>(
      static_cast<quint8>(badChecksum.at(24)) ^ 0x01);
  Expect(!CsgUpstreamParser::isValidFrame(badChecksum),
         "CS 错误必须拒绝");
  Expect(!CsgUpstreamParser::isValidFrame(login.left(25)),
         "截断帧必须拒绝");

  QByteArray badEnd = login;
  badEnd[25] = '\x15';
  Expect(!CsgUpstreamParser::isValidFrame(badEnd),
         "结束字符错误必须拒绝");
}

void TestConfiguredItems(const QMap<QString, QByteArray> &frames,
                         const QString &xmlPath) {
  CsgUpstreamParser parser;

  const auto login = parser.parse(frames.value("login"));
  Expect(FieldValue(login, "传输方向 DIR") == "1", "登录应为上行");
  Expect(FieldValue(login, "长度 L") == "1200" &&
             FieldDescription(login, "长度 L").contains("18"),
         "长度字段的数据列应为两字节原文，说明列应为十进制长度");
  Expect(FieldValue(login, "行政区划码 A1") == "253144" &&
             FieldDescription(login, "行政区划码 A1").contains("443125"),
         "行政区划码的数据列应为原文，说明列应为正常阅读顺序");
  Expect(FieldValue(login, "终端地址 A2") == "030411" &&
             FieldDescription(login, "终端地址 A2").contains("110403"),
         "终端地址的数据列应为原文，说明列应为小端解析结果");
  Expect(FieldValue(login, "应用功能码 AFN") == "02" &&
             FieldDescription(login, "应用功能码 AFN") ==
                 "链路接口检测",
         "AFN 名称应来自 XML");
  Expect(FieldValue(login, "信息点标识 DA") == "0000" &&
             FieldDescription(login, "信息点标识 DA") == "p0",
         "DA 的数据列应为原文，说明列应为点号");
  Expect(FieldValue(login, "数据标识 DI") == "001000E0" &&
             FieldDescription(login, "数据标识 DI").contains("E0001000"),
         "DI 应按正常顺序显示");
  Expect(FieldDescription(login, "数据标识 DI").contains("终端登录"),
         "DI 名称应来自 XML");
  Expect(FieldValue(login, "规约版本号") == "1001" &&
             FieldDescription(login, "规约版本号").contains("0110"),
         "10 01 应按小端 BCD 显示为 0110");
  Expect(FieldValue(login, "校验和 CS") == "EE" &&
             FieldDescription(login, "校验和 CS").contains("计算 EE") &&
             FieldDescription(login, "校验和 CS").contains("校验通过"),
         "校验和的数据列应为原文，说明列应为计算结果");

  const auto confirm = parser.parse(frames.value("confirm"));
  Expect(FieldValue(confirm, "确认结果") == "00" &&
             FieldDescription(confirm, "确认结果").contains("全部肯定"),
         "00 应映射为全部肯定");

  const auto heartbeat = parser.parse(frames.value("heartbeat"));
  Expect(FieldDescription(heartbeat, "数据标识 DI").contains("终端心跳"),
         "零数据心跳应解析成功");
  Expect(!HasField(heartbeat, "数据项解析错误"),
         "没有可选时标的心跳不应报错");

  const auto task = parser.parse(frames.value("task62"));
  Expect(FieldDescription(task, "数据标识 DI").contains("普通任务号62"),
         "任务 DI 名称应从独立 DI 表查询");

  QTemporaryDir dir;
  Expect(dir.isValid(), "应能创建 XML 驱动名称测试目录");
  if (!dir.isValid()) {
    return;
  }
  QFile xmlFile(xmlPath);
  Expect(xmlFile.open(QIODevice::ReadOnly | QIODevice::Text),
         "应能读取正式 XML 用于名称联动测试");
  QString changedXml = QString::fromUtf8(xmlFile.readAll());
  changedXml.replace("name=\"终端登录\"", "name=\"XML测试登录\"");
  const QString changedPath =
      WriteXml(dir, "changed-name.xml", changedXml);
  QString error;
  auto &manager = CsgUpstreamConfigManager::instance();
  Expect(manager.loadXml(changedPath, &error),
         "修改名称后的合法 XML 应加载成功: " + error);
  Expect(FieldDescription(parser.parse(frames.value("login")),
                          "数据标识 DI")
             .contains("XML测试登录"),
         "解析结果应随 XML 中的 DI 名称变化");
  Expect(manager.loadXml(xmlPath, &error),
         "名称联动测试后应恢复正式 XML: " + error);
}

void TestTaskData(const QMap<QString, QByteArray> &frames,
                  const QString &xmlPath) {
  CsgUpstreamParser parser;
  const auto fields = parser.parse(frames.value("task62"));
  Expect(FieldValue(fields, "应用功能码 AFN") == "12" &&
             FieldDescription(fields, "应用功能码 AFN") ==
                 "读任务数据",
         "AFN 12 名称应来自独立 AFN 表");
  Expect(FieldValue(fields, "数据标识 DI") == "3E0300E0" &&
             FieldDescription(fields, "数据标识 DI").contains(
                 "E000033E 普通任务号62"),
         "E000033E 名称应来自独立 DI 表");
  Expect(FieldValue(fields, "数据结构方式") == "00" &&
             FieldDescription(fields, "数据结构方式") ==
                 "自描述方式",
         "00 应解析为自描述方式");
  Expect(FieldValue(fields, "信息点标识组数 n") == "03" &&
             FieldDescription(fields, "信息点标识组数 n") == "3",
         "n 应为 3");
  Expect(FieldValue(fields, "数据标识编码组数 m") == "01" &&
             FieldDescription(fields, "数据标识编码组数 m") == "1",
         "m 应为 1");
  Expect(FieldValue(fields, "任务数据内容").isEmpty(),
         "自描述任务不应再整体保留为一个 Blob");
  Expect(FieldValue(fields, "任务数据组[1]/信息点标识") == "0201" &&
             FieldDescription(fields,
                              "任务数据组[1]/信息点标识") ==
                 "第 1 组 p2",
         "第 1 组应解析 DA=0201 为 p2");
  Expect(FieldValue(fields, "任务数据组[1]/数据标识编码") ==
             "FF010605" &&
             FieldDescription(fields,
                              "任务数据组[1]/数据标识编码") ==
                 "050601FF 日冻结正向有功电能数据块",
         "第 1 组应从独立 DI 表解析 050601FF 名称");
  Expect(FieldValue(fields, "任务数据组[1]/费率数") == "04" &&
             FieldDescription(fields, "任务数据组[1]/费率数") == "4",
         "第 1 组费率数应为 4");
  Expect(FieldValue(fields,
                    "任务数据组[1]/日冻结正向有功总电能") ==
             "FFFFFFFF" &&
             FieldDescription(
                 fields,
                 "任务数据组[1]/日冻结正向有功总电能") ==
                 "无数据",
         "第 1 组全 FF 总电能应显示无数据");
  for (int rate = 1; rate <= 4; ++rate) {
    const QString fieldName =
        QString("任务数据组[1]/日冻结正向有功费率电能[%1]")
            .arg(rate);
    Expect(FieldValue(fields, fieldName) == "FFFFFFFF" &&
               FieldDescription(fields, fieldName) == "无数据",
           QString("第 1 组费率 %1 电能应显示无数据").arg(rate));
  }
  Expect(FieldValue(fields, "任务数据组[1]/数据时间") ==
             "2512040000" &&
             FieldDescription(fields, "任务数据组[1]/数据时间") ==
                 "2025-12-04 00:00",
         "第 1 组数据时间应正确解析");

  Expect(FieldValue(fields, "任务数据组[2]/数据标识编码") ==
             "FF020605" &&
             FieldDescription(fields,
                              "任务数据组[2]/数据标识编码") ==
                 "050602FF 日冻结反向有功电能数据块",
         "第 2 组应从独立 DI 表解析 050602FF 名称");
  Expect(FieldValue(fields, "任务数据组[2]/费率数") == "04" &&
             FieldDescription(fields, "任务数据组[2]/费率数") == "4",
         "第 2 组费率数应为 4");
  Expect(FieldValue(fields,
                    "任务数据组[2]/日冻结反向有功总电能") ==
             "FFFFFFFF" &&
             FieldDescription(
                 fields,
                 "任务数据组[2]/日冻结反向有功总电能") ==
                 "无数据",
         "第 2 组全 FF 总电能应显示无数据");
  for (int rate = 1; rate <= 4; ++rate) {
    const QString fieldName =
        QString("任务数据组[2]/日冻结反向有功费率电能[%1]")
            .arg(rate);
    Expect(FieldValue(fields, fieldName) == "FFFFFFFF" &&
               FieldDescription(fields, fieldName) == "无数据",
           QString("第 2 组费率 %1 电能应显示无数据").arg(rate));
  }
  Expect(FieldValue(fields, "任务数据组[2]/数据时间") ==
             "2512040000" &&
             FieldDescription(fields, "任务数据组[2]/数据时间") ==
                 "2025-12-04 00:00",
         "第 2 组数据时间应正确解析");

  Expect(FieldValue(fields, "任务数据组[3]/数据标识编码") ==
             "6C8000E1" &&
             FieldDescription(fields,
                              "任务数据组[3]/数据标识编码") ==
                 "E100806C 数据项暂未配置",
         "第 3 组未知内层 DI 应明确标记未配置");
  Expect(FieldValue(fields,
                    "任务数据组[3]/未配置数据标识内容") ==
             "FFFFFFFFFF",
         "最后一组未知内层 DI 应保留时间字段之前的内容原文");
  Expect(FieldValue(fields, "任务数据组[3]/数据时间") ==
             "2512040000" &&
             FieldDescription(fields, "任务数据组[3]/数据时间") ==
                 "2025-12-04 00:00",
         "最后一组未知内层 DI 仍应解析末尾数据时间");
  Expect(FieldValue(fields, "任务数据时间") == "202512040000" &&
             FieldDescription(fields, "任务数据时间") ==
                 "2025-12-04 00:00",
         "任务帧末尾四位年任务数据时间应独立解析");
  Expect(!HasField(fields, "数据项解析错误"),
         "合法任务帧不应产生字段级错误");
  Expect(FieldValue(fields, "未解析应用数据").isEmpty(),
         "合法任务帧不应重复标记未解析数据");

  QTemporaryDir dir;
  Expect(dir.isValid(), "应能创建 AFN/DI 独立名称测试目录");
  if (!dir.isValid()) {
    return;
  }
  QFile xmlFile(xmlPath);
  Expect(xmlFile.open(QIODevice::ReadOnly | QIODevice::Text),
         "应能读取正式 XML 用于 AFN/DI 独立名称测试");
  const QString originalXml = QString::fromUtf8(xmlFile.readAll());
  auto &manager = CsgUpstreamConfigManager::instance();
  QString error;

  QString changedAfnXml = originalXml;
  changedAfnXml.replace("name=\"读任务数据\"",
                        "name=\"XML测试任务服务\"");
  const QString changedAfnPath =
      WriteXml(dir, "changed-afn-name.xml", changedAfnXml);
  Expect(manager.loadXml(changedAfnPath, &error),
         "修改 AFN 名称后的 XML 应加载成功: " + error);
  const auto changedAfnFields = parser.parse(frames.value("task62"));
  Expect(FieldDescription(changedAfnFields, "应用功能码 AFN")
             .contains("XML测试任务服务"),
         "修改 AFN 名称应只更新 AFN 显示");
  Expect(FieldDescription(changedAfnFields, "数据标识 DI")
             .contains("普通任务号62"),
         "修改 AFN 名称不应改变 DI 显示");

  QString changedDiXml = originalXml;
  changedDiXml.replace("name=\"普通任务号\"",
                       "name=\"XML测试任务号\"");
  const QString changedDiPath =
      WriteXml(dir, "changed-di-name.xml", changedDiXml);
  Expect(manager.loadXml(changedDiPath, &error),
         "修改 DI 名称后的 XML 应加载成功: " + error);
  const auto changedDiFields = parser.parse(frames.value("task62"));
  Expect(FieldDescription(changedDiFields, "应用功能码 AFN")
             .contains("读任务数据"),
         "修改 DI 名称不应改变 AFN 显示");
  Expect(FieldDescription(changedDiFields, "数据标识 DI")
             .contains("XML测试任务号62"),
         "修改 DI 名称应只更新 DI 显示");

  QString changedServiceXml = originalXml;
  changedServiceXml.replace("name=\"数据结构方式\"",
                            "name=\"XML数据结构方式\"");
  changedServiceXml.replace("name=\"自描述方式\"",
                            "name=\"XML自描述方式\"");
  changedServiceXml.replace("name=\"信息点标识组数 n\"",
                            "name=\"XML信息点组数 n\"");
  const QString changedServicePath =
      WriteXml(dir, "changed-service.xml", changedServiceXml);
  Expect(manager.loadXml(changedServicePath, &error),
         "修改 Service 结构后的 XML 应加载成功: " + error);
  const auto changedServiceFields =
      parser.parse(frames.value("task62"));
  Expect(FieldValue(changedServiceFields, "XML数据结构方式") == "00" &&
             FieldDescription(changedServiceFields,
                              "XML数据结构方式") ==
                 "XML自描述方式",
         "选择字段和分支名称应由 XML Service 驱动");
  Expect(FieldValue(changedServiceFields, "XML信息点组数 n") == "03" &&
             FieldDescription(changedServiceFields,
                              "XML信息点组数 n") == "3",
         "分支字段名称应由 XML Service 驱动");
  Expect(FieldValue(changedServiceFields, "数据结构方式").isEmpty() &&
             FieldValue(changedServiceFields,
                        "信息点标识组数 n").isEmpty(),
         "修改 XML Service 后不应继续输出硬编码字段名");

  QString changedInnerDiXml = originalXml;
  changedInnerDiXml.replace("name=\"日冻结正向有功电能数据块\"",
                            "name=\"XML正向电能数据块\"");
  changedInnerDiXml.replace("name=\"费率数\"",
                            "name=\"XML费率数\"");
  const QString changedInnerDiPath =
      WriteXml(dir, "changed-inner-di.xml", changedInnerDiXml);
  Expect(manager.loadXml(changedInnerDiPath, &error),
         "修改内层 DI 后的 XML 应加载成功: " + error);
  const auto changedInnerDiFields =
      parser.parse(frames.value("task62"));
  Expect(FieldValue(changedInnerDiFields,
                    "任务数据组[1]/数据标识编码") ==
             "FF010605" &&
             FieldDescription(changedInnerDiFields,
                              "任务数据组[1]/数据标识编码") ==
                 "050601FF XML正向电能数据块",
         "内层 DI 名称应由独立 DI 表驱动");
  Expect(FieldValue(changedInnerDiFields,
                    "任务数据组[1]/XML费率数") == "04" &&
             FieldDescription(changedInnerDiFields,
                              "任务数据组[1]/XML费率数") == "4",
         "内层 DI 字段名称应由 XML 驱动");

  Expect(manager.loadXml(xmlPath, &error),
         "AFN/DI 独立名称测试后应恢复正式 XML: " + error);

  const QByteArray truncatedTask = BuildFrame(
      0xC4, 0x12, 0x71, 0x0000, 0xE000033E,
      QByteArray::fromHex("0003"));
  const auto truncatedFields = parser.parse(truncatedTask);
  Expect(FieldDescription(truncatedFields, "数据项解析错误").contains(
             "数据标识编码组数 m"),
         "截断任务固定头应指出 XML 定义中缺失的字段");
  Expect(FieldValue(truncatedFields, "未解析应用数据") ==
             "00003E0300E00003",
         "截断任务固定头应从 DA 开始保留全部 Service 原文");

  const QByteArray taskDefinition = BuildFrame(
      0xC4, 0x12, 0x71, 0x0000, 0xE000033E,
      QByteArray::fromHex("01AABB"));
  const auto taskDefinitionFields = parser.parse(taskDefinition);
  Expect(FieldValue(taskDefinitionFields, "数据结构方式") == "01" &&
             FieldDescription(taskDefinitionFields,
                              "数据结构方式") ==
                 "任务定义方式",
         "01 应解析为任务定义方式");
  Expect(FieldValue(taskDefinitionFields, "信息点标识组数 n").isEmpty() &&
             FieldValue(taskDefinitionFields,
                        "数据标识编码组数 m").isEmpty(),
         "任务定义方式不应读取 n 和 m");
  Expect(FieldValue(taskDefinitionFields, "任务数据内容") == "AABB",
         "任务定义方式应保留方式字节后的全部原文");

  const QByteArray emptyTaskDefinition = BuildFrame(
      0xC4, 0x12, 0x71, 0x0000, 0xE000033E,
      QByteArray::fromHex("01"));
  Expect(!HasField(parser.parse(emptyTaskDefinition),
                   "数据项解析错误"),
         "只有方式字节的任务定义数据仍应满足最小结构");

  const QByteArray unknownMode = BuildFrame(
      0xC4, 0x12, 0x71, 0x0000, 0xE000033E,
      QByteArray::fromHex("02CCDD"));
  const auto unknownModeFields = parser.parse(unknownMode);
  Expect(FieldValue(unknownModeFields, "数据结构方式") == "02" &&
             FieldDescription(unknownModeFields,
                              "数据结构方式") ==
                 "未知数据结构方式 2",
         "未知数据结构方式应保留原始数值");
  Expect(FieldValue(unknownModeFields, "未解析应用数据") == "CCDD",
         "未知方式应保留方式字节后的全部原文");

  const QByteArray downlinkTask = BuildFrame(
      0x44, 0x12, 0x71, 0x0000, 0xE000033E,
      QByteArray::fromHex("2512040000251205000001"));
  const auto downlinkFields = parser.parse(downlinkTask);
  Expect(FieldValue(downlinkFields, "数据结构方式").isEmpty(),
         "AFN 12 下行请求不得按上行任务数据格式解析");
  Expect(FieldValue(downlinkFields, "数据起始时间") ==
             "2512040000" &&
             FieldDescription(downlinkFields, "数据起始时间") ==
                 "2025-12-04 00:00",
         "AFN 12 下行起始时间应按 XML Service 解析");
  Expect(FieldValue(downlinkFields, "数据结束时间") ==
             "2512050000" &&
             FieldDescription(downlinkFields, "数据结束时间") ==
                 "2025-12-05 00:00",
         "AFN 12 下行结束时间应按 XML Service 解析");
  Expect(FieldValue(downlinkFields, "数据密度") == "01" &&
             FieldDescription(downlinkFields, "数据密度") == "1",
         "AFN 12 下行数据密度应按 XML Service 解析");
  Expect(!HasField(downlinkFields, "数据项解析错误") &&
             FieldValue(downlinkFields, "未解析应用数据").isEmpty(),
         "完整 AFN 12 下行请求不应保留为未解析原文");
}

void TestDynamicTaskDataItems() {
  CsgUpstreamParser parser;

  const QByteArray directEnergyItem = BuildFrame(
      0xC4, 0x02, 0x71, 0x0000, 0x050601FF,
      QByteArray::fromHex("02785634122301000056040000"));
  const auto directEnergyFields = parser.parse(directEnergyItem);
  Expect(FieldValue(directEnergyFields,
                    "日冻结正向有功费率电能[2]") == "56040000" &&
             FieldDescription(directEnergyFields,
                              "日冻结正向有功费率电能[2]") ==
                 "000004.56 kWh",
         "同一 DI 定义作为外层数据项时也应执行 countRef");
  Expect(!HasField(directEnergyFields, "数据项解析错误"),
         "独立 DI 解析不应遗留第二个费率字段");

  const QByteArray twoRates = BuildFrame(
      0xC4, 0x12, 0x71, 0x0000, 0xE0000301,
      QByteArray::fromHex(
          "0001010101FF010605027856341223010000560400002512041234"));
  const auto twoRateFields = parser.parse(twoRates);
  Expect(FieldDescription(twoRateFields, "数据标识 DI")
             .contains("普通任务号1"),
         "业务解析器应把 E0000301 归一化为任务系列并显示任务号1");
  Expect(FieldValue(twoRateFields, "任务数据组[1]/费率数") == "02" &&
             FieldDescription(twoRateFields,
                              "任务数据组[1]/费率数") == "2",
         "费率数 2 应控制后续重复字段数量");
  Expect(FieldValue(twoRateFields,
                    "任务数据组[1]/日冻结正向有功总电能") ==
             "78563412" &&
             FieldDescription(
                 twoRateFields,
                 "任务数据组[1]/日冻结正向有功总电能") ==
                 "123456.78 kWh",
         "BCD 总电能应按 XML 格式和单位显示");
  Expect(FieldValue(
             twoRateFields,
             "任务数据组[1]/日冻结正向有功费率电能[1]") == "23010000" &&
             FieldDescription(
                 twoRateFields,
                 "任务数据组[1]/日冻结正向有功费率电能[1]") ==
                 "000001.23 kWh" &&
             FieldValue(
                 twoRateFields,
                 "任务数据组[1]/日冻结正向有功费率电能[2]") == "56040000" &&
             FieldDescription(
                 twoRateFields,
                 "任务数据组[1]/日冻结正向有功费率电能[2]") ==
                 "000004.56 kWh",
         "两个费率电能应按 countRef 动态解析");
  Expect(FieldValue(
             twoRateFields,
             "任务数据组[1]/日冻结正向有功费率电能[3]")
             .isEmpty(),
         "费率数 2 不应产生第 3 个重复字段");
  Expect(FieldValue(twoRateFields, "任务数据组[1]/数据时间") ==
             "2512041234" &&
             FieldDescription(twoRateFields,
                              "任务数据组[1]/数据时间") ==
                 "2025-12-04 12:34",
         "合成任务组的数据时间应正确解析");

  const QByteArray unknownLast = BuildFrame(
      0xC4, 0x12, 0x71, 0x0000, 0xE00003FD,
      QByteArray::fromHex(
          "000101010178563412AABB2512041234"));
  const auto unknownLastFields = parser.parse(unknownLast);
  Expect(FieldDescription(unknownLastFields, "数据标识 DI")
             .contains("普通任务号253"),
         "业务解析器应显示普通任务号范围上界 253");
  Expect(FieldValue(unknownLastFields,
                    "任务数据组[1]/未配置数据标识内容") == "AABB",
         "最后一组未知内层 DI 应保留内容原文");
  Expect(FieldValue(unknownLastFields,
                    "任务数据组[1]/数据时间") ==
             "2512041234" &&
             FieldDescription(unknownLastFields,
                              "任务数据组[1]/数据时间") ==
                 "2025-12-04 12:34",
         "最后一组未知内层 DI 应从末尾解析时间");

  const QByteArray unknownMiddle = BuildFrame(
      0xC4, 0x12, 0x71, 0x0000, 0xE000033E,
      QByteArray::fromHex(
          "000201010178563412AABB25120412340101FF01060500"));
  const auto unknownMiddleFields = parser.parse(unknownMiddle);
  Expect(FieldDescription(unknownMiddleFields, "数据项解析错误")
             .contains("非最后一组"),
         "未知中间 DI 无法确定内容边界时应停止并明确报错");
  Expect(!FieldValue(unknownMiddleFields, "未解析应用数据").isEmpty(),
         "未知中间 DI 应保留完整应用数据");

  const QByteArray truncatedGroup = BuildFrame(
      0xC4, 0x12, 0x71, 0x0000, 0xE000033E,
      QByteArray::fromHex("0001010101"));
  Expect(FieldDescription(parser.parse(truncatedGroup),
                          "数据项解析错误")
             .contains("数据标识编码"),
         "任务组缺少完整内层 DI 时应报告截断字段");

  const QByteArray tooManyGroups = BuildFrame(
      0xC4, 0x12, 0x71, 0x0000, 0xE000033E,
      QByteArray::fromHex("001010"));
  Expect(FieldDescription(parser.parse(tooManyGroups),
                          "数据项解析错误")
             .contains("小于 256"),
         "n×m 不小于 256 时应拒绝展开");

  const QByteArray outsideTaskRange = BuildFrame(
      0xC4, 0x12, 0x71, 0x0000, 0xE00003FE,
      QByteArray::fromHex("01"));
  Expect(!FieldDescription(parser.parse(outsideTaskRange),
                           "数据标识 DI")
              .contains("普通任务号"),
         "E00003FE 不应归一化为普通任务系列");
}

void TestFieldBoundaries() {
  CsgUpstreamParser parser;

  const QByteArray unknownConfirmValue = BuildFrame(
      0x08, 0x00, 0x60, 0x0000, 0xE0000000,
      QByteArray::fromHex("02"));
  const auto unknownConfirmFields = parser.parse(unknownConfirmValue);
  const QString unknownConfirm =
      FieldDescription(unknownConfirmFields, "确认结果");
  Expect(FieldValue(unknownConfirmFields, "确认结果") == "02",
         "未知确认值的数据列应保留原始字节 02");
  Expect(unknownConfirm.contains("2") &&
             unknownConfirm.contains("未知确认结果"),
         "未知 BIN 确认值应保留数字并标记未知确认结果");

  const QByteArray fullLogin = BuildFrame(
      0xC9, 0x02, 0x70, 0x0000, 0xE0001000,
      QByteArray::fromHex("100101251204142734"));
  const auto fullFields = parser.parse(fullLogin);
  Expect(FieldValue(fullFields, "是否具备安全模块") == "01" &&
             FieldDescription(fullFields,
                              "是否具备安全模块").contains("具备"),
         "完整登录应解析安全模块标志");
  Expect(FieldValue(fullFields, "终端时间") == "251204142734" &&
             FieldDescription(fullFields, "终端时间").contains(
                 "2025-12-04 14:27:34"),
         "完整登录应解析终端时间");

  const QByteArray partialTime = BuildFrame(
      0xC9, 0x02, 0x70, 0x0000, 0xE0001000,
      QByteArray::fromHex("100101251204"));
  const auto partialFields = parser.parse(partialTime);
  Expect(!FieldDescription(partialFields,
                           "数据项解析错误").isEmpty(),
         "部分可选时间必须报告字段级错误");
  Expect(!FieldValue(partialFields, "未解析应用数据").isEmpty(),
         "字段级错误必须保留完整原始数据");

  const QByteArray invalidBcd = BuildFrame(
      0xC9, 0x02, 0x70, 0x0000, 0xE0001000,
      QByteArray::fromHex("FA01"));
  Expect(FieldDescription(parser.parse(invalidBcd),
                          "数据项解析错误")
             .contains("BCD"),
         "非法 BCD 不应越界或使整帧失败");

  const QByteArray invalidDate = BuildFrame(
      0xC9, 0x02, 0x70, 0x0000, 0xE0001000,
      QByteArray::fromHex("100101251332142734"));
  Expect(FieldDescription(parser.parse(invalidDate),
                          "数据项解析错误")
             .contains("日期"),
         "非法 BCD 日期应产生字段级错误");
}

void TestSummary(const QMap<QString, QByteArray> &frames) {
  const ProtocolSummary login =
      CsgUpstreamParser::getSummary(frames.value("login"));
  Expect(login.direction == "上行", "登录概要方向应为上行");
  Expect(login.address.contains("A1:443125"),
         "概要应包含反向显示的 A1");
  Expect(login.frameSeq == "0", "概要应包含低四位帧序号");
  Expect(login.messageType.contains("链路接口检测") &&
             login.messageType.contains("终端登录"),
         "登录概要应包含 XML 中的 AFN 和 DI 名称");

  const ProtocolSummary task =
      CsgUpstreamParser::getSummary(frames.value("task62"));
  Expect(task.messageType.contains("读任务数据") &&
             task.messageType.contains("普通任务号62"),
         "任务概要应组合独立 AFN 和 DI 名称");
}

void TestProtocolDetector(const QMap<QString, QByteArray> &frames) {
  int offset = -1;
  Expect(ProtocolDetector::detectProtocol(frames.value("login"), &offset) ==
             ProtocolType::ProtocolCsgUpstream,
         "自动检测应识别南网上行规约");
  Expect(offset == 0, "完整帧偏移应为 0");
  Expect(ProtocolDetector::getFrameLength(
             frames.value("login"),
             ProtocolType::ProtocolCsgUpstream) == 26,
         "检测器帧长应委托新解析器");
  Expect(ProtocolDetector::getProtocolName(
             ProtocolType::ProtocolCsgUpstream) ==
             "南网计量上行规约",
         "协议显示名称应固定");

  const QByteArray prefixed =
      QByteArray::fromHex("FFFF") + frames.value("login");
  Expect(ProtocolDetector::detectProtocol(prefixed, &offset) ==
                 ProtocolType::ProtocolCsgUpstream &&
             offset == 2,
         "滑动窗口应找到前置噪声后的新协议");

  QByteArray badChecksum = frames.value("login");
  badChecksum[24] = static_cast<char>(
      static_cast<quint8>(badChecksum.at(24)) ^ 0x01);
  Expect(ProtocolDetector::detectProtocol(badChecksum, &offset) ==
             ProtocolType::Unknown,
         "校验和错误的候选不得识别为新协议");
}

void TestTerminalLog(const QString &path,
                     const QString &reportPath = QString()) {
  QFile file(path);
  Expect(file.open(QIODevice::ReadOnly | QIODevice::Text),
         "应能打开真实日志: " + path);
  if (!file.isOpen()) {
    return;
  }

  const QRegularExpression candidatePattern("68[0-9A-Fa-f]{20,}");
  int candidateCount = 0;
  int validCount = 0;
  int detectedCount = 0;
  int parsedCount = 0;
  int parseErrorCount = 0;
  int invalidFieldBoundaryCount = 0;
  int ordinaryTaskOuterTimeCount = 0;
  int lineNumber = 0;
  QStringList parseFailures;
  QStringList reportEntries;
  QMap<quint8, int> afnCounts;
  QMap<quint32, int> diCounts;
  CsgUpstreamParser parser;

  while (!file.atEnd()) {
    ++lineNumber;
    const QString line = QString::fromUtf8(file.readLine());
    QString longestCandidate;
    QRegularExpressionMatchIterator matches =
        candidatePattern.globalMatch(line);
    while (matches.hasNext()) {
      const QString candidate = matches.next().captured();
      if (candidate.size() > longestCandidate.size()) {
        longestCandidate = candidate;
      }
    }
    if (longestCandidate.isEmpty()) {
      continue;
    }

    ++candidateCount;
    const QByteArray frame = longestCandidate.toLatin1();
    const QByteArray bytes = QByteArray::fromHex(frame);
    if (!CsgUpstreamParser::isValidFrame(bytes)) {
      reportEntries.append(
          QString("### 候选 %1（日志第 %2 行）\n\n"
                  "- 结果：无效候选，不是完整的南网计量上行帧\n"
                  "- 原因：未通过 FT1.2 双长度、第二起始符、校验和或结束符严格校验\n\n"
                  "```text\n%3\n```\n")
              .arg(candidateCount)
              .arg(lineNumber)
              .arg(QString::fromLatin1(frame).toUpper()));
      continue;
    }

    ++validCount;
    if (ProtocolDetector::detectProtocol(bytes) ==
        ProtocolType::ProtocolCsgUpstream) {
      ++detectedCount;
    }
    const QList<CsgUpstreamField> fields = parser.parse(bytes);
    bool parsedWithoutError = !fields.isEmpty();
    bool hasOrdinaryTaskOuterTime = false;
    for (const CsgUpstreamField &field : fields) {
      if (field.name == "数据项解析错误") {
        parsedWithoutError = false;
        parseFailures.append(
            QString("line %1: %2").arg(lineNumber).arg(
                field.description));
      }
      if (field.offset < 0 || field.length < 0 ||
          field.offset + field.length > bytes.size()) {
        ++invalidFieldBoundaryCount;
        parsedWithoutError = false;
        parseFailures.append(
            QString("line %1: field %2 boundary %3+%4 > %5")
                .arg(lineNumber)
                .arg(field.name)
                .arg(field.offset)
                .arg(field.length)
                .arg(bytes.size()));
      }
      if (field.name == "任务数据时间") {
        hasOrdinaryTaskOuterTime = true;
      }
    }
    if (parsedWithoutError) {
      ++parsedCount;
    } else {
      ++parseErrorCount;
    }
    const quint8 afn = static_cast<quint8>(bytes.at(14));
    const quint32 di = static_cast<quint8>(bytes.at(18)) |
                       (static_cast<quint32>(
                            static_cast<quint8>(bytes.at(19)))
                        << 8) |
                       (static_cast<quint32>(
                            static_cast<quint8>(bytes.at(20)))
                        << 16) |
                       (static_cast<quint32>(
                            static_cast<quint8>(bytes.at(21)))
                        << 24);
    if (di == 0xE000033E && hasOrdinaryTaskOuterTime) {
      ++ordinaryTaskOuterTimeCount;
    }
    ++afnCounts[afn];
    ++diCounts[di];
    reportEntries.append(
        QString("### 候选 %1（日志第 %2 行）\n\n"
                "- 结果：%3\n"
                "- AFN：`%4`\n"
                "- 外层 DI：`%5`\n"
                "- 解析字段数：%6\n"
                "- 字段边界：全部位于 %7 字节原始帧内\n\n"
                "```text\n%8\n```\n")
            .arg(candidateCount)
            .arg(lineNumber)
            .arg(parsedWithoutError ? "解析成功" : "解析失败")
            .arg(afn, 2, 16, QLatin1Char('0'))
            .arg(di, 8, 16, QLatin1Char('0'))
            .arg(fields.size())
            .arg(bytes.size())
            .arg(QString::fromLatin1(frame).toUpper()));
  }

  if (!reportPath.isEmpty()) {
    QFile report(reportPath);
    Expect(report.open(QIODevice::WriteOnly | QIODevice::Text),
           "应能写入真实日志全量解析报告: " + reportPath);
    if (report.isOpen()) {
      QTextStream output(&report);
      output << "# Terminal3.log 南网计量上行规约全量解析报告\n\n"
             << "## 结论\n\n"
             << "从日志逐行提取以 `68` 开始的最长十六进制候选后，共得到 "
             << candidateCount << " 条候选；其中 " << validCount
             << " 条是严格校验有效的完整协议帧，并且 " << parsedCount
             << " 条全部完成字段树解析。解析错误 " << parseErrorCount
             << " 条，字段越界 " << invalidFieldBoundaryCount
             << " 条。其余 " << candidateCount - validCount
             << " 条只是日志说明中的十六进制片段，不是完整协议帧。\n\n"
             << "> 说明：这里的“完整解析成功”表示链路、服务、已配置 DI、"
             << "动态组边界和尾字段均成功形成字段树。日志中的内层 DI `E100806C` "
             << "未出现在 2026 版规范 C.3 表内，其 5 字节内容按未知 DI 原文保留；"
             << "对应组数据时间和外层任务数据时间仍已独立解析。\n\n"
             << "| 指标 | 数量 |\n"
             << "| --- | ---: |\n"
             << "| 提取候选 | " << candidateCount << " |\n"
             << "| 有效完整帧 | " << validCount << " |\n"
             << "| 协议检测成功 | " << detectedCount << " |\n"
             << "| 完整解析成功 | " << parsedCount << " |\n"
             << "| 解析错误 | " << parseErrorCount << " |\n"
             << "| 字段越界 | " << invalidFieldBoundaryCount << " |\n"
             << "| 无效候选 | " << candidateCount - validCount << " |\n\n"
             << "## 服务分布\n\n"
             << "| AFN | 帧数 |\n"
             << "| --- | ---: |\n"
             << "| 00 | " << afnCounts.value(0x00) << " |\n"
             << "| 02 | " << afnCounts.value(0x02) << " |\n"
             << "| 12 | " << afnCounts.value(0x12) << " |\n\n"
             << "## 逐条审计\n\n"
             << reportEntries.join("\n");
      report.close();
    }
  }

  QTextStream(stdout)
      << "Terminal log statistics: candidates=" << candidateCount
      << ", valid=" << validCount
      << ", detected=" << detectedCount
      << ", parsed=" << parsedCount
      << ", parseErrors=" << parseErrorCount
      << ", invalid=" << candidateCount - validCount
      << ", AFN00=" << afnCounts.value(0x00)
      << ", AFN02=" << afnCounts.value(0x02)
      << ", AFN12=" << afnCounts.value(0x12)
      << ", DI_E0000000=" << diCounts.value(0xE0000000)
      << ", DI_E0001001=" << diCounts.value(0xE0001001)
      << ", DI_E0001000=" << diCounts.value(0xE0001000)
      << ", DI_E000033E=" << diCounts.value(0xE000033E) << '\n';
  for (const QString &failure : parseFailures) {
    QTextStream(stderr) << "Terminal log parse failure: "
                        << failure << '\n';
  }

  Expect(candidateCount == 485, "真实日志候选数应为 485");
  Expect(validCount == 480, "真实日志有效帧数应为 480");
  Expect(detectedCount == 480,
         "真实日志有效帧均应由全局检测器识别为南网计量上行规约");
  Expect(parsedCount == 480,
         "真实日志 480 个有效帧均应生成无错误字段树");
  Expect(parseErrorCount == 0,
         "真实日志有效帧不得出现数据项解析错误");
  Expect(invalidFieldBoundaryCount == 0,
         "真实日志所有解析字段必须位于原始报文边界内");
  Expect(candidateCount - validCount == 5,
         "真实日志无效候选数应为 5");
  Expect(afnCounts.value(0x00) == 240, "AFN=00H 应有 240 帧");
  Expect(afnCounts.value(0x02) == 115, "AFN=02H 应有 115 帧");
  Expect(afnCounts.value(0x12) == 125, "AFN=12H 应有 125 帧");
  Expect(diCounts.value(0xE0000000) == 240,
         "DI=E0000000 应有 240 帧");
  Expect(diCounts.value(0xE0001001) == 114,
         "DI=E0001001 应有 114 帧");
  Expect(diCounts.value(0xE0001000) == 1,
         "DI=E0001000 应有 1 帧");
  Expect(diCounts.value(0xE000033E) == 1,
         "DI=E000033E 应有 1 帧");
  Expect(ordinaryTaskOuterTimeCount == 1,
         "真实日志 E000033E 应保留未知内层 DI 并解析外层任务数据时间");
}

void PrintOrdinaryTaskParameter() {
  const auto fields = CsgUpstreamParser().parse(
      OrdinaryTaskParameterFrame(OrdinaryTaskParameterPayload()));
  QTextStream output(stdout);
  output << "E000033E 普通任务参数解析结果\n";
  output << "字段 | 数据 | 说明\n";
  bool printField = false;
  for (const CsgUpstreamField &field : fields) {
    if (field.name == "数据标识 DI") {
      printField = true;
    }
    if (field.name == "校验和 CS") {
      break;
    }
    if (!printField) {
      continue;
    }
    output << QString(field.indent * 2, QLatin1Char(' '))
           << field.name << " | " << field.value << " | "
           << field.description << '\n';
  }
}

} // namespace

int main(int argc, char *argv[]) {
  const QString xmlPath =
      QStringLiteral(CSG_TEST_SOURCE_DIR "/protocol_xml/"
                                     "csg_upstream_protocol.xml");
  const QString framesPath =
      QStringLiteral(CSG_TEST_SOURCE_DIR "/tests/fixtures/"
                                     "csg_upstream_frames.txt");
  const QMap<QString, QByteArray> frames = LoadFrames(framesPath);
  TestConfigLoads(xmlPath);
  TestOrdinaryTaskParameterDefinition();
  TestInvalidXml(xmlPath);
  TestStrictValidation(frames);
  TestConfiguredItems(frames, xmlPath);
  TestTaskData(frames, xmlPath);
  TestOrdinaryTaskParameterParsing();
  TestDynamicTaskDataItems();
  TestFieldBoundaries();
  TestSummary(frames);
  TestProtocolDetector(frames);

  QStringList arguments;
  for (int index = 1; index < argc; ++index) {
    arguments.append(QString::fromLocal8Bit(argv[index]));
  }
  if (arguments.size() == 1 &&
      arguments.first() == "--show-ordinary-task-parameter") {
    PrintOrdinaryTaskParameter();
  } else if (arguments.size() == 2 && arguments.first() == "--log") {
    TestTerminalLog(arguments.at(1));
  } else if (arguments.size() == 4 && arguments.at(0) == "--log" &&
             arguments.at(2) == "--report") {
    TestTerminalLog(arguments.at(1), arguments.at(3));
  } else if (!arguments.isEmpty()) {
    Expect(false,
           "参数格式应为: --log <日志路径> [--report <报告路径>] 或 "
           "--show-ordinary-task-parameter");
  }

  if (g_failureCount == 0) {
    QTextStream(stdout) << "All CSG upstream protocol tests passed.\n";
  }
  return g_failureCount == 0 ? 0 : 1;
}
