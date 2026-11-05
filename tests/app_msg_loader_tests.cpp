#include "app_msg/AppMsgLoader.h"
#include "app_msg/AppMsgParser.h"

#include <QCoreApplication>
#include <QTemporaryFile>
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

const AppMsg::FieldDef *FindField(const QList<AppMsg::FieldDef> &fields,
                                  const QString &name) {
  for (const AppMsg::FieldDef &field : fields) {
    if (field.name == name) {
      return &field;
    }
  }
  return nullptr;
}

const AppMsg::AppMsgField *FindParsedField(
    const QList<AppMsg::AppMsgField> &fields, const QString &name) {
  for (const AppMsg::AppMsgField &field : fields) {
    if (field.name == name) {
      return &field;
    }
  }
  return nullptr;
}

void ExpectEnum(const AppMsg::FieldDef *field, int value,
                const QString &text) {
  Expect(field != nullptr, "字段应存在");
  if (!field) {
    return;
  }
  Expect(field->type == AppMsg::FieldType::Enum, field->name + " 应为枚举");
  Expect(field->enumMap.value(value) == text,
         QString("%1 的枚举 %2 应为 %3").arg(field->name).arg(value).arg(text));
}

} // namespace

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);

  const QString path =
      QStringLiteral(APP_MSG_TEST_SOURCE_DIR "/protocol_xml/app_msg_protocol.xml");
  Expect(AppMsg::AppMsgLoader::instance().load(path), "应能加载应用层消息协议");

  const AppMsg::InterfaceDef *dial =
      AppMsg::AppMsgLoader::instance().getInterface(0x0002, 0x0014);
  Expect(dial != nullptr, "应存在设置拨号参数接口");
  if (dial) {
    const AppMsg::FieldDef *parameters =
        FindField(dial->request.fields, QStringLiteral("拨号参数"));
    Expect(parameters != nullptr && !parameters->children.isEmpty(),
           "拨号参数应为非空序列");
    if (parameters && !parameters->children.isEmpty()) {
      const AppMsg::FieldDef *item = &parameters->children.first();
      ExpectEnum(FindField(item->children, QStringLiteral("运营商")), 0,
                 QStringLiteral("中国移动"));
      ExpectEnum(FindField(item->children, QStringLiteral("运营商")), 4,
                 QStringLiteral("未知"));
    }
    ExpectEnum(FindField(dial->request.fields, QStringLiteral("锁定网络")), 4,
               QStringLiteral("锁定5G网络"));
    ExpectEnum(FindField(dial->request.fields, QStringLiteral("鉴权方式")), 255,
               QStringLiteral("自适应"));
  }

  const AppMsg::InterfaceDef *status =
      AppMsg::AppMsgLoader::instance().getInterface(0x0002, 0x0011);
  Expect(status != nullptr, "应存在查询模块拨号状态接口");
  if (status) {
    ExpectEnum(FindField(status->response.fields, QStringLiteral("运营商")), 3,
               QStringLiteral("无线专网"));
  }

  QByteArray frame;
  frame.append(char(0x01));
  frame.append(QByteArray::fromHex("00000000"));
  frame.append("S\0D\0", 4);
  frame.append(QByteArray::fromHex("14000200"));
  frame.append(char(0x15));
  frame.append(QByteArray::fromHex("0101000B70706F77382D736A2E6865"));
  frame.append(QByteArray::fromHex("0130013004FF"));

  AppMsg::AppMsgParser parser;
  const QList<AppMsg::AppMsgField> parsed = parser.parseToFields(frame);
  const AppMsg::AppMsgField *operatorField =
      FindParsedField(parsed, QStringLiteral("运营商"));
  Expect(operatorField != nullptr, "解析结果应包含运营商字段");
  if (operatorField) {
    Expect(operatorField->description.contains(QStringLiteral("中国移动")),
           "运营商 0 应显示中国移动，实际为 " + operatorField->description);
  }
  const AppMsg::AppMsgField *lockField =
      FindParsedField(parsed, QStringLiteral("锁定网络"));
  Expect(lockField != nullptr, "解析结果应包含锁定网络字段");
  if (lockField) {
    Expect(lockField->description.contains(QStringLiteral("锁定5G网络")),
           "锁定网络 4 应显示锁定5G网络，实际为 " + lockField->description);
  }

  QTemporaryFile fixture;
  if (fixture.open()) {
    fixture.write(R"xml(<protocol><interface_group iid="1"><interface iop="2">
      <request><field name="enum" type="enum"/>
        <enum_value value="1" name="first"/><enum_value value="2" name="second"/>
        <description>separator</description><enum_value value="1" name="unrelated"/>
        <field name="number" type="unsigned"/><enum_value value="2" name="unrelated"/>
      </request></interface></interface_group></protocol>)xml");
    const QString fileName = fixture.fileName();
    fixture.close();
    auto &loader = AppMsg::AppMsgLoader::instance();
    Expect(loader.load(fileName), "应能加载同级枚举边界用例");
    const auto *iface = loader.getInterface(1, 2);
    Expect(iface && iface->request.fields.size() == 2, "边界用例应有两个字段");
    if (iface && iface->request.fields.size() == 2) {
      ExpectEnum(&iface->request.fields.first(), 1, "first");
      ExpectEnum(&iface->request.fields.first(), 2, "second");
      Expect(iface->request.fields.last().enumMap.isEmpty(), "非枚举字段不能接收枚举项");
    }
  } else {
    Expect(false, "应能创建枚举边界用例");
  }

  if (g_failureCount == 0) {
    QTextStream(stdout) << "PASS: AppMsg loader enum assertions\n";
  }
  return g_failureCount == 0 ? 0 : 1;
}
