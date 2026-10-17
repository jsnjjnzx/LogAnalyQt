#include "698_prot/Ai698Tools.h"
#include "698_prot/Prot698OadManager.h"
#include "698_prot/Prot698Parser.h"
#include "698_prot/Prot698DataEncoder.h"
#include "698_prot/Prot698ServiceSchema.h"
#include "698_prot/Prot698ServiceBuilder.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <QSet>
#include <QProcess>
#include <QStandardPaths>

#ifndef AI698_TEST_SOURCE_DIR
#define AI698_TEST_SOURCE_DIR "."
#endif
namespace {
int checks = 0, failures = 0;
void expect(bool condition, const QString &message) {
  ++checks;
  if (!condition) { ++failures; QTextStream(stderr) << "FAIL: " << message << '\n'; }
}
QJsonObject data(const QJsonObject &reply, const QString &message) {
  expect(reply["ok"].isBool() && reply["ok"].toBool(), message + ": " + QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
  expect(reply["error"].isNull(), message + ": successful envelope has null error");
  return reply["data"].toObject();
}
void rejects(const QJsonObject &reply, const QString &code, const QString &message) {
  expect(reply["ok"].isBool() && !reply["ok"].toBool() && reply["data"].isNull(), message + ": no successful result");
  expect(reply["error"].toObject()["code"] == code, message + ": " + QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
  expect(reply["error"].toObject()["path"].isString() && !reply["error"].toObject()["message"].toString().isEmpty(), message + ": actionable error");
}
AiFrameContext context() {
  return {"local-request-id", {{"id", "terminal-1"}, {"name", "冻结测试终端"},
      {"address", "010203040506"}, {"addressType", 0}, {"logicAddress", 1}, {"clientAddress", 17}},
      QDateTime::fromString("2026-10-14T12:34:56+08:00", Qt::ISODate), 37,
      Prot698OadManager::instance().definitionId()};
}
QJsonObject oad(const QString &oi, int id = 2) { return {{"oi", oi}, {"attrId", id}, {"index", 0}}; }
QJsonObject omd(const QString &oi, int id = 7) { return {{"oi", oi}, {"methodId", id}, {"mode", 0}}; }
QJsonObject draft(const QString &service, const QJsonObject &tree) { return {{"serviceCode", service}, {"treeValue", tree}}; }
QJsonObject memberQuery(const QString &oi, int id, bool method = false, int index = 0) {
  return {{"kind", "object"}, {"oi", oi}, {"memberId", id}, {"isMethod", method}, {"dataIndex", index}};
}
void checkFrame(const QJsonObject &result, const AiFrameContext &ctx, bool response = false) {
  expect(result["requestId"] == ctx.requestId && result["terminal"].toObject() == ctx.terminal, "result retains request and frozen terminal");
  expect(result["definitionId"] == ctx.definitionId && result["submittedAt"].toString().endsWith("+08:00"), "result retains definition and explicit timezone");
  const auto apdu = QByteArray::fromHex(result["apduHex"].toString().toLatin1());
  const auto frame = QByteArray::fromHex(result["frameHex"].toString().toLatin1());
  QString error;
  expect(!frame.isEmpty() && Prot698Parser::validateSingleFrame(frame, &error), "one strictly verified full frame: " + error);
  expect(frame.size() > 13 && quint8(frame.at(3)) == (response ? 0x83 : 0x43), "direction and user-data function derived locally");
  expect(frame.mid(5, 6).toHex() == "060504030201" && quint8(frame.at(4)) == 0x15 && quint8(frame.at(11)) == 17, "frozen display address reversed once; logic/CA preserved");
  expect(apdu.size() >= 3 && (quint8(apdu.at(2)) & 0x3f) == ctx.piidSequence, "PIID exactly matches frozen sequence");
  expect(result["treeValue"].toObject()["piid"].toObject()["seqNo"] == ctx.piidSequence, "explicit frozen PIID in result tree");
}
void testDirectory(Ai698Tools &tools, const AiFrameContext &ctx) {
  const auto functions = tools.definitions();
  expect(functions.size() == 5, "exactly five general tools");
  QSet<QString> names;
  for (const auto &entry : functions) {
    const auto function = entry.toObject()["function"].toObject(); names.insert(function["name"].toString());
    expect(entry.toObject()["type"] == "function" && function["parameters"].toObject()["additionalProperties"] == false, "strict outer function schema");
  }
  expect(names == QSet<QString>{"list_services", "search_objects", "get_definition", "validate_draft", "build_frame"}, "no business-specific tools");
  const auto listing = data(tools.invoke("list_services", {}, ctx), "service directory");
  expect(listing["items"].toArray().size() == 18 && listing["definitionId"] == ctx.definitionId, "18 runtime services and frozen revision");
  for (const auto &entry : listing["items"].toArray()) {
    const auto row = entry.toObject(); const QString code = row["code"].toString();
    expect(row["buildSupported"].isBool() && row["unsupportedReasons"].isArray() && row["verificationStatus"].isString(), code + " separates support/evidence");
    const auto definition = data(tools.invoke("get_definition", {{"kind", "service"}, {"serviceCode", code}}, ctx), code + " definition");
    expect(definition["inputSchema"].isObject() && definition["inputSchema"].toObject()["additionalProperties"] == false, code + " complete input object");
    expect(definition["defaults"].toObject()["piid"].toObject()["seqNo"] == ctx.piidSequence && definition["shapeOnly"] == true, code + " transport-only defaults and shape guide");
    const auto inputSchema = definition["inputSchema"].toObject();
    expect(inputSchema["properties"].toObject()["piid"].toObject()["properties"].toObject()["seqNo"].toObject()["const"] == ctx.piidSequence && !inputSchema["required"].toArray().contains("piid"), code + " schema specializes frozen optional transport defaults");
    if (code == "GET_NORMAL") expect(definition["limitations"].toArray().isEmpty(), "unreferenced schema definitions do not invent GET_NORMAL limitations");
    if (code == "GET_RECORD" || code == "GET_RESPONSE_RECORD") expect(!definition["limitations"].toArray().isEmpty(), code + " unsupported branches have exact schema paths");
  }
  rejects(tools.invoke("list_services", {{"shell", "echo"}}, ctx), "invalid_arguments", "no arbitrary invocation");
  rejects(tools.invoke("set_time", {}, ctx), "unknown_tool", "business command is not a tool");
  rejects(tools.invoke("search_objects", {{"query", "x"}, {"offset", 0}, {"limit", 51}}, ctx), "invalid_arguments", "page maximum");
  rejects(tools.invoke("search_objects", {{"query", "x"}, {"offset", false}, {"limit", 10}}, ctx), "invalid_arguments", "no bool offset coercion");
  rejects(tools.invoke("get_definition", {{"kind", "service"}, {"serviceCode", "GET_NORMAL"}, {"oi", "4000"}}, ctx), "invalid_arguments", "definition branches reject sibling fields");
  rejects(tools.invoke("get_definition", {{"kind", "service"}, {"serviceCode", "MADE_UP"}}, ctx), "unsupported_definition", "unknown service never becomes a generic root");
}
void testActualXml(Ai698Tools &tools, const AiFrameContext &ctx) {
  for (const auto &query : {QStringLiteral("停上电"), QStringLiteral("终端停上电事件"),
                           QStringLiteral("终端停／上电事件"), QStringLiteral("终端停 / 上电事件"), QStringLiteral("３１０６")}) {
    const auto found = data(tools.invoke("search_objects", {{"query", query}, {"offset", 0}, {"limit", 50}}, ctx), "event name spelling search");
    bool hasEvent = false;
    for (const auto &item : found["items"].toArray()) hasEvent |= item.toObject()["oi"] == "3106";
    expect(hasEvent, "punctuation/width variants find the actual event OI: " + query);
  }
  expect(data(tools.invoke("search_objects", {{"query", "!!!"}, {"offset", 0}, {"limit", 50}}, ctx), "punctuation-only query")["total"] == 0,
         "punctuation-only query must not become match-all");
  const auto eventMembers = data(tools.invoke("get_definition", memberQuery("3106", 0), ctx), "event member semantics");
  bool historyFields = false, statisticsFields = false;
  for (const auto &member : eventMembers["attrs"].toArray()) {
    const auto item = member.toObject();
    for (const auto &field : item["fields"].toArray()) {
      historyFields |= item["id"] == 2 && field.toObject()["name"] == QStringLiteral("事件记录序号");
      statisticsFields |= item["id"] == 7 && field.toObject()["name"] == QStringLiteral("累计时间及发生次数");
    }
  }
  expect(historyFields && statisticsFields, "member index distinguishes event history from current statistics without guessing member IDs");
  const auto recordDefinition = data(tools.invoke("get_definition", {{"kind", "service"}, {"serviceCode", "GET_RECORD"}}, ctx), "record selection semantics");
  const auto recordSchema = recordDefinition["inputSchema"].toObject();
  expect(recordSchema["$defs"].toObject()["rsd"].toObject()["oneOf"].toArray()[9].toObject()["description"].toString().contains("n=1"), "latest record selector is discoverable");
  expect(recordSchema["properties"].toObject()["rcsd"].toObject()["description"].toString().contains("selects all columns"), "complete record selection is discoverable");
  const QJsonObject latestEvent{{"oad", oad("3106")}, {"rsd", QJsonObject{{"type", 9}, {"n", 1}}}, {"rcsd", QJsonObject{{"items", QJsonArray{}}}}};
  const auto eventResult = data(tools.invoke("build_frame", draft("GET_RECORD", latestEvent), ctx), "latest terminal power event through generic record builder");
  checkFrame(eventResult, ctx);
  expect(eventResult["apduHex"] == "0503253106020009010000", "latest event wire vector: record service, OAD, selector9 n1, all columns, no time tag");
  const QJsonObject explicitTime{{"year", 2026}, {"month", 10}, {"day", 14}, {"hour", 12}, {"minute", 34}, {"second", 56}};
  const auto timeDefinition = data(tools.invoke("get_definition", memberQuery("4000", 2), ctx), "actual XML datetime definition");
  expect(timeDefinition["type"] == "date_time_s", "datetime comes from currently loaded XML");
  auto result = data(tools.invoke("build_frame", draft("SET_NORMAL", {{"setItem", QJsonObject{{"oad", oad("4000")}, {"dataValue", explicitTime}}}}), ctx), "actual XML date sample through general SET");
  checkFrame(result, ctx);
  expect(result["apduHex"].toString().contains("400002001C07EA0A0E0C2238"), "explicit wall-clock fields encoded without now/timezone inference");
  expect(result["normalizedTree"].toObject()["setItem"].toObject()["dataValue"].toObject() == explicitTime && result["normalizedTreeText"].toString().contains("\"second\": 56"), "complete date fields are locally serialized for review");
  auto missingSeconds = explicitTime; missingSeconds.remove("second");
  rejects(tools.invoke("build_frame", draft("SET_NORMAL", {{"setItem", QJsonObject{{"oad", oad("4000")}, {"dataValue", missingSeconds}}}}), ctx), "missing_field", "actual XML missing seconds stay missing");
  const auto badSelector = data(tools.invoke("validate_draft", draft("GET_RECORD", {{"oad", oad("4000")}, {"rcsd", QJsonObject{{"items", QJsonArray{}}}},
      {"rsd", QJsonObject{{"type", 1}, {"selector", QJsonObject{{"oad", oad("4000")}, {"value", missingSeconds}}}}}}), ctx), "RSD missing field path");
  expect(badSelector["valid"] == false && badSelector["errors"].toArray().first().toObject()["path"] == "$.treeValue.rsd.selector.value.second", "RSD reports the actual leaf path without a phantom dataValue");
  result = data(tools.invoke("build_frame", draft("SET_NORMAL", {{"setItem", QJsonObject{{"oad", oad("4001")}, {"dataValue", "112233445566"}}}}), ctx), "actual XML communication-address sample through general SET");
  checkFrame(result, ctx);
  expect(result["apduHex"].toString().contains("400102000906112233445566"), "business address changes only the attribute payload");
  const QJsonArray actualStructure{1, 2, 3, QJsonObject{{"representation", "raw"}, {"value", 4}, {"unit", "秒"}}, 5};
  result = data(tools.invoke("build_frame", draft("SET_NORMAL", {{"setItem", QJsonObject{{"oad", oad("4000", 4)}, {"dataValue", actualStructure}}}}), ctx), "actual XML structure and per-leaf units");
  checkFrame(result, ctx);
  const QJsonArray optionValues{QJsonObject{{"optionIndex", 1}, {"value", QJsonObject{{"representation", "raw"}, {"value", "4294967295"}, {"unit", "kWh"}}}}};
  result = data(tools.invoke("build_frame", draft("GET_RESPONSE_NORMAL", {{"result", QJsonObject{{"oad", oad("0000")}, {"choice", 1}, {"dataValue", optionValues}}}}), ctx), "actual XML unsigned option despite Selected=false");
  checkFrame(result, ctx, true);
  expect(result["apduHex"].toString().contains("010106FFFFFFFF"), "explicit option emits array and chosen unsigned type");
}
QString writeDictionary(QTemporaryDir &directory) {
  const QString path = directory.filePath("synthetic.xml");
  QFile file(path); expect(file.open(QIODevice::WriteOnly), "synthetic dictionary open");
  file.write(R"XML(<OIConfig version="test">
<Item OI="A101" Name="通用量"><Node Method="attrib" ID="2" OP="GET|SET" Name="标量"><SubItem Type="unsigned"/></Node><Node Method="attrib" ID="3" OP="GET" Name="只读量"><SubItem Type="unsigned"/></Node><Node Method="method" ID="7" OP="ACTION" Name="通用刷新"><SubItem Type="Data"/></Node><Node Method="method" ID="9" OP="ACTION" Name="独立返回"><SubItem Type="integer"/></Node></Item>
<Item OI="A102" Name="通用复合"><Node Method="attrib" ID="2" OP="GET|SET" Name="结构"><SubItem Type="structure"><SubItem Type="long64_unsigned"/><SubItem Type="array"><SubItem Type="unsigned"/></SubItem><SubItem Type="bool"/></SubItem></Node></Item>
<Item OI="A103" Name="参考长度"><Node Method="attrib" ID="2" OP="GET|SET" Name="字节串"><SubItem Type="octet_string" Size="1"/></Node></Item>
<Item OI="A104" Name="倍率"><Node Method="attrib" ID="2" OP="GET|SET" Name="电流"><SubItem Type="long_unsigned" Unit="A" Scaler="-2"/></Node></Item>
<Item OI="A105" Name="替代类型"><Node Method="attrib" ID="2" OP="GET|SET" Name="候选数组"><SubItem Type="array"><SubItem Type="option" TypeOption="long" Selected="true"/><SubItem Type="option" TypeOption="double_long" Selected="true"/></SubItem></Node></Item>
<Item OI="A106" Name="未知类型"><Node Method="attrib" ID="2" OP="GET|SET"><SubItem Type="not_supported"/></Node></Item>
<Item OI="A107" Name="旧覆盖"><Node Method="attrib" ID="2" OP="GET|SET"><SubItem Type="unsigned"/></Node></Item>
<Item OI="A107" Name="最后定义"><Node Method="attrib" ID="2" OP="GET|SET"><SubItem Type="long_unsigned"/></Node></Item>
<Item OI="A108" Name="记录集合"><Node Method="attrib" ID="2" OP="GET"><SubItem Type="array"><SubItem Type="structure"><SubItem Type="unsigned"/><SubItem Type="long_unsigned" Unit="A" Scaler="-2"/></SubItem></SubItem></Node></Item>
</OIConfig>)XML");
  file.close(); return path;
}
void testGeneralDefinitions(Ai698Tools &tools, const AiFrameContext &ctx) {
  const auto results = data(tools.invoke("search_objects", {{"query", "通用刷新"}, {"offset", 0}, {"limit", 1}}, ctx), "method-name search");
  expect(results["total"] == 1 && results["items"].toArray().first().toObject()["oi"] == "A101", "searches method labels as well as object names");
  const auto page = data(tools.invoke("search_objects", {{"query", ""}, {"offset", 0}, {"limit", 2}}, ctx), "first bounded page");
  expect(page["items"].toArray().size() == 2 && page["hasMore"] == true && page["total"] == 8, "stable paginated unique OI results");
  const auto empty = data(tools.invoke("search_objects", {{"query", "no-match"}, {"offset", 0}, {"limit", 2}}, ctx), "empty search is not load failure");
  expect(empty["total"] == 0 && empty["items"].toArray().isEmpty(), "empty directory matches explicit");
  const auto structure = data(tools.invoke("get_definition", memberQuery("A102", 2), ctx), "structured member definition");
  expect(structure["objectName"] == "通用复合" && structure["name"] == "结构" && structure["op"] == "GET|SET", "object/member names and permissions retained");
  expect(structure["inputSchema"].toObject()["prefixItems"].toArray().size() == 3, "ordered complete structure inputSchema");
  expect(structure["shapeGuide"].toArray().size() == 3, "shape guide preserves ordered fields without inventing numeric defaults");
  expect(!structure["children"].toArray().first().toObject().contains("inputSchema"), "nested metadata omits redundant root schemas without truncating canonical inputSchema");
  const auto options = data(tools.invoke("get_definition", memberQuery("A105", 2), ctx), "option metadata");
  const auto candidates = options["children"].toArray();
  expect(candidates.size() == 2 && candidates[1].toObject()["optionIndex"] == 1 && candidates[1].toObject()["typeOption"] == "double_long", "stable option index and actual TypeOption retained");
  const auto bytes = data(tools.invoke("get_definition", memberQuery("A103", 2), ctx), "reference size metadata");
  expect(bytes["referenceSize"] == "1" && bytes["sizeConstraint"].toString().contains("reference_only"), "Size remains a disclosed reference");
  const auto unsupported = data(tools.invoke("get_definition", memberQuery("A106", 2), ctx), "unsupported local definition is discoverable");
  expect(unsupported["buildSupported"] == false && !unsupported["unsupportedReasons"].toArray().isEmpty(), "unknown type disables only its member");
  const auto duplicate = data(tools.invoke("get_definition", memberQuery("A107", 2), ctx), "last-wins definition");
  expect(duplicate["objectName"] == "最后定义" && duplicate["type"] == "long_unsigned" && !duplicate["warnings"].toArray().isEmpty(), "effective duplicate retained with warning");
  expect(!QJsonDocument(duplicate).toJson().contains("sourcePath") && !QJsonDocument(duplicate).toJson().contains("synthetic.xml"), "no local source path in model tool output");
  const auto members = data(tools.invoke("get_definition", memberQuery("A101", 0), ctx), "member index");
  expect(members["attrs"].toArray().size() == 2 && members["methods"].toArray().size() == 2, "member zero returns both member indexes");
  rejects(tools.invoke("get_definition", memberQuery("A101", 0, false, 1), ctx), "invalid_arguments", "member index cannot contain nonzero dataIndex");
  rejects(tools.invoke("get_definition", memberQuery("FFFF", 2), ctx), "unknown_object", "unknown object");
}
void testBuilds(Ai698Tools &tools, const AiFrameContext &ctx) {
  const QJsonObject get{{"oad", oad("A101")}};
  auto result = data(tools.invoke("build_frame", draft("GET_NORMAL", get), ctx), "generic GET"); checkFrame(result, ctx);
  const QJsonObject scalar{{"setItem", QJsonObject{{"oad", oad("A101")}, {"dataValue", 201}}}};
  result = data(tools.invoke("build_frame", draft("SET_NORMAL", scalar), ctx), "generic scalar SET"); checkFrame(result, ctx);
  expect(result["apduHex"].toString().contains("A101020011C9"), "scalar object encoded by runtime definition");
  const QJsonArray composite{"18446744073709551615", QJsonArray{1, 2, 3}, true};
  const QJsonObject compound{{"setItem", QJsonObject{{"oad", oad("A102")}, {"dataValue", composite}}}};
  result = data(tools.invoke("build_frame", draft("SET_NORMAL", compound), ctx), "different OI/structure/64bit/array"); checkFrame(result, ctx);
  expect(result["apduHex"].toString().contains("15FFFFFFFFFFFFFFFF"), "64bit remains exact");
  expect(result["normalizedTree"].toObject()["setItem"].toObject()["dataValue"].toArray().first() == "18446744073709551615" && result["normalizedTreeText"].toString().contains("18446744073709551615"), "normalized uint64 is exact text, never a QML Number");
  const QJsonObject list{{"setItems", QJsonArray{scalar["setItem"], compound["setItem"]}}};
  result = data(tools.invoke("build_frame", draft("SET_LIST", list), ctx), "heterogeneous SET list"); checkFrame(result, ctx);
  const QJsonObject action{{"actionItem", QJsonObject{{"omd", omd("A101")},
      {"dataValue", QJsonObject{{"type", "structure"}, {"value", QJsonArray{
          QJsonObject{{"type", "unsigned"}, {"value", 9}}, QJsonObject{{"type", "null"}, {"value", QJsonValue(QJsonValue::Null)}}}}}}}}};
  result = data(tools.invoke("build_frame", draft("ACTION_NORMAL", action), ctx), "generic method and dynamic typed structure"); checkFrame(result, ctx);
  const QJsonObject physical{{"setItem", QJsonObject{{"oad", oad("A104")},
      {"dataValue", QJsonObject{{"representation", "physical"}, {"value", "1.23"}, {"unit", "A"}}}}}};
  result = data(tools.invoke("build_frame", draft("SET_NORMAL", physical), ctx), "explicit physical value");
  expect(result["apduHex"].toString().contains("12007B"), "physical becomes precise raw bytes");
  expect(result["normalizedTree"].toObject()["setItem"].toObject()["dataValue"] == 123 && result["normalizedTreeText"].toString().contains("\"dataValue\": 123"), "physical 1.23 A exposes precise raw123 locally");
  expect(result["treeValue"].toObject()["setItem"].toObject()["dataValue"].toObject()["unit"] == "A" && result["inputTreeText"].toString().contains("1.23"), "review input retains physical value and unit");
  const QJsonObject options{{"setItem", QJsonObject{{"oad", oad("A105")},
      {"dataValue", QJsonArray{QJsonObject{{"optionIndex", 1}, {"value", 100000}}}}}}};
  result = data(tools.invoke("build_frame", draft("SET_NORMAL", options), ctx), "explicit option selection");
  expect(result["apduHex"].toString().contains("05000186A0"), "option emits chosen type without choice byte");
  const QJsonObject response{{"setResult", QJsonObject{{"oad", oad("A101")}, {"dar", 0}}}};
  result = data(tools.invoke("build_frame", draft("SET_RESPONSE_NORMAL", response), ctx), "locally derived response direction"); checkFrame(result, ctx, true);
  const QJsonObject record{{"oad", oad("A101")}, {"rsd", QJsonObject{{"type", 0}}}, {"rcsd", QJsonObject{{"items", QJsonArray{}}}}};
  result = data(tools.invoke("build_frame", draft("GET_RECORD", record), ctx), "record selector service"); checkFrame(result, ctx);
  const QJsonObject proxy{{"totalTimeout", 10}, {"servers", QJsonArray{QJsonObject{{"tsa", QJsonObject{{"address", "000000000001"}, {"addressType", 0}, {"logicAddress", 0}}}, {"timeout", 5}, {"oads", QJsonArray{oad("A101"), oad("A102")}}}}}};
  result = data(tools.invoke("build_frame", draft("PROXY_GET_LIST", proxy), ctx), "explicit inner proxy target"); checkFrame(result, ctx);
  expect(result["apduHex"].toString().contains("0705010000000000"), "business TSA comes from explicit inner target");
  const QJsonObject noProxyTarget{{"totalTimeout", 10}, {"servers", QJsonArray{QJsonObject{{"timeout", 5}, {"oads", QJsonArray{oad("A101")}}}}}};
  rejects(tools.invoke("build_frame", draft("PROXY_GET_LIST", noProxyTarget), ctx), "missing_field", "outer target is never an implicit proxy target");
  const QJsonObject returned{{"actionResult", QJsonObject{{"omd", omd("A101", 9)}, {"dar", 0}, {"hasData", true},
      {"dataValue", QJsonObject{{"type", "unsigned"}, {"value", 255}}}}}};
  result = data(tools.invoke("build_frame", draft("ACTION_RESPONSE_NORMAL", returned), ctx), "typed response differs from integer method input"); checkFrame(result, ctx, true);
  const QJsonObject recordDar{{"recordResult", QJsonObject{{"oad", oad("A101")}, {"rcsd", QJsonObject{{"items", QJsonArray{}}}}, {"choice", 0}, {"dar", 0}}}};
  result = data(tools.invoke("build_frame", draft("GET_RESPONSE_RECORD", recordDar), ctx), "record response DAR branch"); checkFrame(result, ctx, true);
  const QJsonObject recordRows{{"recordResult", QJsonObject{{"oad", oad("A101")}, {"rcsd", QJsonObject{{"items", QJsonArray{}}}}, {"choice", 1}, {"rowCount", 1}, {"rowsHex", "1101"}}}};
  rejects(tools.invoke("build_frame", draft("GET_RESPONSE_RECORD", recordRows), ctx), "unsupported_definition", "opaque record row bypass is unavailable");
  const QJsonObject opaque{{"actionItem", QJsonObject{{"omd", omd("A101")}, {"dataValue", QJsonObject{{"type", "comdcb"}, {"value", "0000000000"}}}}}};
  rejects(tools.invoke("build_frame", draft("ACTION_NORMAL", opaque), ctx), "unsupported_definition", "opaque dynamic Data cannot bypass tool gate");
  auto mismatched = get; mismatched["piid"] = QJsonObject{{"seqNo", 38}};
  rejects(tools.invoke("build_frame", draft("GET_NORMAL", mismatched), ctx), "context_changed", "model cannot change sequence");
  auto injection = draft("GET_NORMAL", get); injection["frameOptions"] = QJsonObject{{"serverAddrHex", "AA"}};
  rejects(tools.invoke("build_frame", injection, ctx), "invalid_arguments", "tool cannot change target");
  auto badTree = get; badTree["terminal"] = ctx.terminal;
  rejects(tools.invoke("build_frame", draft("GET_NORMAL", badTree), ctx), "invalid_arguments", "tree cannot change target either");
  auto invalid = data(tools.invoke("validate_draft", draft("SET_NORMAL", {{"setItem", QJsonObject{{"oad", oad("A101")}}}}), ctx), "draft validation reports business errors without tool failure");
  expect(invalid["valid"] == false && invalid["errors"].toArray().first().toObject()["code"] == "missing_field", "missing business data stays missing");
  const auto readonly = draft("SET_NORMAL", {{"setItem", QJsonObject{{"oad", oad("A101", 3)}, {"dataValue", 1}}}});
  rejects(tools.invoke("build_frame", readonly, ctx), "invalid_arguments", "read-only OP cannot be bypassed");
  const auto unknown = draft("SET_NORMAL", {{"setItem", QJsonObject{{"oad", oad("A106")}, {"dataValue", 1}}}});
  rejects(tools.invoke("build_frame", unknown, ctx), "unsupported_definition", "unknown Data cannot encode null");
  const auto malformedHex = draft("SET_NORMAL", {{"setItem", QJsonObject{{"oad", oad("A103")}, {"dataValue", "A0xB"}}}});
  rejects(tools.invoke("build_frame", malformedHex, ctx), "invalid_arguments", "invalid embedded Hex prefix");
  const auto longerBytes = draft("SET_NORMAL", {{"setItem", QJsonObject{{"oad", oad("A103")}, {"dataValue", "AABBCC"}}}});
  result = data(tools.invoke("build_frame", longerBytes, ctx), "Size reference does not falsely impose exact length"); checkFrame(result, ctx);
}
void testStructuredProxyAndRows(Ai698Tools &tools, const AiFrameContext &ctx) {
  const QJsonObject target{{"address", "112233445566"}, {"addressType", 2}, {"logicAddress", 1}};
  const QJsonObject server{{"tsa", target}, {"timeout", 5}, {"oads", QJsonArray{oad("A101")}}};
  auto result = data(tools.invoke("build_frame", draft("PROXY_GET_LIST", {{"totalTimeout", 10}, {"servers", QJsonArray{server}}}), ctx), "structured display TSA conversion");
  checkFrame(result, ctx);
  expect(result["apduHex"] == "090125000A010795665544332211000501A101020000", "TSA descriptor/type/logic/reversal and proxy bytes independently derived");
  expect(result["treeValue"].toObject()["servers"].toArray().first().toObject()["tsa"].toObject() == target &&
      result["normalizedTree"].toObject()["servers"].toArray().first().toObject()["tsaHex"] == "95665544332211", "TSA audit retains input and exposes locally encoded wire bytes");
  auto legacyServer = server; legacyServer.remove("tsa"); legacyServer["tsaHex"] = "95665544332211";
  rejects(tools.invoke("build_frame", draft("PROXY_GET_LIST", {{"totalTimeout", 10}, {"servers", QJsonArray{legacyServer}}}), ctx), "unsupported_definition", "AI cannot choose its own encoded TSA shortcut");
  QVariantMap manual{{"piid", QVariantMap{{"seqNo", ctx.piidSequence}}}, {"totalTimeout", 10}, {"servers", QJsonArray{legacyServer}.toVariantList()}};
  QString error;
  const auto manualSpec = Prot698ServiceSchema::toServiceSpec("PROXY_GET_LIST", manual, &error);
  expect(Prot698ServiceBuilder::buildFromSpec(manualSpec, &error).toHex().toUpper() == result["apduHex"].toString().toLatin1() && error.isEmpty(), "legacy manual tsaHex emits identical bytes");
  auto incompleteTarget = target; incompleteTarget.remove("logicAddress"); auto badServer = server; badServer["tsa"] = incompleteTarget;
  rejects(tools.invoke("build_frame", draft("PROXY_GET_LIST", {{"totalTimeout", 10}, {"servers", QJsonArray{badServer}}}), ctx), "missing_field", "TSA logical address must be explicit");
  badServer = server; auto fractional = target; fractional["addressType"] = true; badServer["tsa"] = fractional;
  rejects(tools.invoke("build_frame", draft("PROXY_GET_LIST", {{"totalTimeout", 10}, {"servers", QJsonArray{badServer}}}), ctx), "invalid_arguments", "TSA rejects bool address type before conversion");
  badServer = server; auto malformed = target; malformed["address"] = "A0xB"; badServer["tsa"] = malformed;
  rejects(tools.invoke("build_frame", draft("PROXY_GET_LIST", {{"totalTimeout", 10}, {"servers", QJsonArray{badServer}}}), ctx), "invalid_arguments", "TSA display address rejects malformed Hex");
  const struct { const char *display; const char *wire; } addressLimits[] = {
      {"AA", "00AA"}, {"000102030405060708090A0B0C0D0E0F", "0F0F0E0D0C0B0A09080706050403020100"}};
  for (const auto &limit : addressLimits) {
    auto edge = server; edge["tsa"] = QJsonObject{{"address", limit.display}, {"addressType", 0}, {"logicAddress", 0}};
    const auto edgeResult = data(tools.invoke("build_frame", draft("PROXY_GET_LIST", {{"totalTimeout", 10}, {"servers", QJsonArray{edge}}}), ctx), "TSA length boundary");
    expect(edgeResult["normalizedTree"].toObject()["servers"].toArray().first().toObject()["tsaHex"] == limit.wire, "TSA1/16 physical bytes are independently expected");
  }
  auto oversized = server; oversized["tsa"] = QJsonObject{{"address", QString(34, 'A')}, {"addressType", 0}, {"logicAddress", 0}};
  rejects(tools.invoke("build_frame", draft("PROXY_GET_LIST", {{"totalTimeout", 10}, {"servers", QJsonArray{oversized}}}), ctx), "invalid_arguments", "TSA 17 physical bytes rejected");
  const QJsonObject responseServer{{"tsa", target}, {"results", QJsonArray{QJsonObject{{"oad", oad("A101")}, {"choice", 0}, {"dar", 0}}}}};
  result = data(tools.invoke("build_frame", draft("PROXY_GET_RESPONSE_LIST", {{"servers", QJsonArray{responseServer}}}), ctx), "structured proxy response target"); checkFrame(result, ctx, true);

  const QJsonObject columns{{"items", QJsonArray{QJsonObject{{"choice", 0}, {"oad", oad("A101")}}, QJsonObject{{"choice", 0}, {"oad", oad("A104")}}}}};
  const QJsonArray rows{QJsonArray{201, QJsonObject{{"representation", "physical"}, {"value", "1.23"}, {"unit", "A"}}},
      QJsonArray{2, QJsonObject{{"representation", "raw"}, {"value", 1}, {"unit", "A"}}}};
  QJsonObject record{{"oad", oad("A108")}, {"rcsd", columns}, {"choice", 1}, {"rows", rows}};
  result = data(tools.invoke("build_frame", draft("GET_RESPONSE_RECORD", {{"recordResult", record}}), ctx), "RCSD driven structured record rows"); checkFrame(result, ctx, true);
  expect(result["apduHex"] == "850325A10802000200A101020000A1040200010211C912007B11021200010000", "row count plus row-major tagged cells independently derived from fixed Go codec");
  const auto normalizedRows = result["normalizedTree"].toObject()["recordResult"].toObject()["rows"].toArray();
  expect(normalizedRows.first().toArray()[1] == 123 && normalizedRows.size() == 2 && !result["treeValue"].toObject()["recordResult"].toObject().contains("rowCount"), "row count is derived; physical cell exposes raw123");
  auto legacyRecord = record; legacyRecord.remove("rows"); legacyRecord["rowCount"] = 2; legacyRecord["rowsHex"] = "11C912007B1102120001";
  manual = {{"piid", QVariantMap{{"seqNo", ctx.piidSequence}}}, {"recordResult", legacyRecord.toVariantMap()}};
  error.clear(); const auto legacySpec = Prot698ServiceSchema::toServiceSpec("GET_RESPONSE_RECORD", manual, &error);
  expect(Prot698ServiceBuilder::buildFromSpec(legacySpec, &error).toHex().toUpper() == result["apduHex"].toString().toLatin1() && error.isEmpty(), "legacy manual record rowsHex remains byte-compatible");
  const QJsonObject next{{"end", true}, {"blockNo", 0x0102}, {"choice", 2}, {"recordResults", QJsonArray{record}}};
  result = data(tools.invoke("build_frame", draft("GET_RESPONSE_NEXT", {{"nextResult", next}}), ctx), "structured record rows in NEXT list"); checkFrame(result, ctx, true);
  expect(result["apduHex"].toString().startsWith("8505250101020201A1080200"), "NEXT record list preserves block and record envelopes");
  auto missingRows = record; missingRows.remove("rows");
  const auto missingRowsResult = data(tools.invoke("validate_draft", draft("GET_RESPONSE_RECORD", {{"recordResult", missingRows}}), ctx), "record missing rows");
  expect(missingRowsResult["valid"] == false && missingRowsResult["errors"].toArray().first().toObject()["path"] == "$.treeValue.recordResult.rows", "choice1 missing rows must not ask for the unrelated DAR branch");
  auto missingCell = record; missingCell["rows"] = QJsonArray{QJsonArray{1}};
  const auto missing = data(tools.invoke("validate_draft", draft("GET_RESPONSE_RECORD", {{"recordResult", missingCell}}), ctx), "record missing cell");
  expect(missing["valid"] == false && missing["errors"].toArray().first().toObject()["path"] == "$.treeValue.recordResult.rows[0]", "row width mismatch reports real row path");
  auto wrongUnit = record; wrongUnit["rows"] = QJsonArray{QJsonArray{1, QJsonObject{{"representation", "raw"}, {"value", 2}, {"unit", "V"}}}};
  rejects(tools.invoke("build_frame", draft("GET_RESPONSE_RECORD", {{"recordResult", wrongUnit}}), ctx), "invalid_arguments", "record cell obeys actual column unit");
  auto tooManyRows = record; QJsonArray excessive;
  for (int i = 0; i < 256; ++i) excessive.append(QJsonArray{1, QJsonObject{{"representation", "raw"}, {"value", 1}, {"unit", "A"}}});
  auto maximumRows = excessive; maximumRows.removeLast(); auto maximumRecord = record; maximumRecord["rows"] = maximumRows;
  result = data(tools.invoke("build_frame", draft("GET_RESPONSE_RECORD", {{"recordResult", maximumRecord}}), ctx), "255 structured rows fit the declared count");
  expect(result["normalizedTree"].toObject()["recordResult"].toObject()["rows"].toArray().size() == 255, "maximum row count is preserved exactly");
  tooManyRows["rows"] = excessive;
  rejects(tools.invoke("build_frame", draft("GET_RESPONSE_RECORD", {{"recordResult", tooManyRows}}), ctx), "invalid_arguments", "256 rows cannot truncate rowCount");
  auto empty = record; empty["rows"] = QJsonArray{};
  result = data(tools.invoke("build_frame", draft("GET_RESPONSE_RECORD", {{"recordResult", empty}}), ctx), "explicit empty record data"); checkFrame(result, ctx, true);
  const QJsonObject roadColumns{{"items", QJsonArray{QJsonObject{{"choice", 1}, {"oad", oad("A108")}, {"roadOads", QJsonArray{oad("A101"), oad("A104")}}}}}};
  auto road = record; road["rcsd"] = roadColumns; road["rows"] = QJsonArray{QJsonArray{QJsonArray{1, 2}}};
  rejects(tools.invoke("build_frame", draft("GET_RESPONSE_RECORD", {{"recordResult", road}}), ctx), "unsupported_definition", "uncertain ROAD cell container is never guessed");
  auto manualRoad = road; manualRoad.remove("rows"); manualRoad["rowCount"] = 1; manualRoad["rowsHex"] = "02021101120002";
  manual = {{"piid", QVariantMap{{"seqNo", ctx.piidSequence}}}, {"recordResult", manualRoad.toVariantMap()}};
  error.clear(); const auto roadSpec = Prot698ServiceSchema::toServiceSpec("GET_RESPONSE_RECORD", manual, &error);
  const auto roadApdu = Prot698ServiceBuilder::buildFromSpec(roadSpec, &error);
  expect(error.isEmpty() && Prot698Parser::validateApdu(roadApdu, &error), "synthetic manual ROAD has one tagged Data cell per CSD, not per associated OAD");
  road["rows"] = QJsonArray{};
  result = data(tools.invoke("build_frame", draft("GET_RESPONSE_RECORD", {{"recordResult", road}}), ctx), "ROAD empty rows need no container guess"); checkFrame(result, ctx, true);
  const QJsonObject structColumns{{"items", QJsonArray{QJsonObject{{"choice", 0}, {"oad", oad("A102")}}}}};
  record["rcsd"] = structColumns; record["rows"] = QJsonArray{QJsonArray{QJsonArray{"18446744073709551615", QJsonArray{1, 2}, false}}};
  result = data(tools.invoke("build_frame", draft("GET_RESPONSE_RECORD", {{"recordResult", record}}), ctx), "record cell can itself be XML structure with array and uint64"); checkFrame(result, ctx, true);
  expect(result["normalizedTreeText"].toString().contains("18446744073709551615"), "record uint64 remains exact review text");
}
void testPublishedSchemas(Ai698Tools &tools, const AiFrameContext &ctx) {
  QJsonObject schemas;
  for (const auto &entry : tools.definitions()) {
    const auto function = entry.toObject()["function"].toObject();
    schemas[function["name"].toString()] = function["parameters"];
  }
  const auto published = schemas["get_definition"].toObject()["properties"].toObject()["kind"].toObject();
  expect(published["enum"].toArray().contains("service") && published["enum"].toArray().contains("object") && !published.contains("const"), "public union kind admits both branches");
  auto serviceSchema = data(tools.invoke("get_definition", {{"kind", "service"}, {"serviceCode", "GET_NORMAL"}}, ctx), "published frozen service schema");
  schemas["get-tree"] = serviceSchema["inputSchema"];
  auto methodSchema = data(tools.invoke("get_definition", memberQuery("A101", 7, true), ctx), "published dynamic Data schema");
  schemas["typed-data"] = methodSchema["inputSchema"];
  auto physicalSchema = data(tools.invoke("get_definition", memberQuery("A104", 2), ctx), "published physical schema");
  schemas["physical"] = physicalSchema["inputSchema"];
  const auto recordSchema = data(tools.invoke("get_definition", {{"kind", "service"}, {"serviceCode", "GET_RECORD"}}, ctx), "published RSD recursive schema");
  schemas["record-tree"] = recordSchema["inputSchema"];
  schemas["proxy-tree"] = data(tools.invoke("get_definition", {{"kind", "service"}, {"serviceCode", "PROXY_GET_LIST"}}, ctx), "published structured TSA schema")["inputSchema"];
  schemas["record-response"] = data(tools.invoke("get_definition", {{"kind", "service"}, {"serviceCode", "GET_RESPONSE_RECORD"}}, ctx), "published rows schema")["inputSchema"];
  QJsonArray cases;
  const auto add = [&](const QString &name, const QJsonValue &value, bool valid) { cases.append(QJsonObject{{"schema", name}, {"value", value}, {"valid", valid}}); };
  add("get_definition", QJsonObject{{"kind", "service"}, {"serviceCode", "GET_NORMAL"}}, true);
  add("get_definition", memberQuery("A101", 2), true);
  add("get_definition", QJsonObject{{"kind", "service"}, {"serviceCode", "GET_NORMAL"}, {"memberId", 2}}, false);
  add("get_definition", QJsonObject{{"kind", "object"}, {"oi", "A101"}}, false);
  add("list_services", QJsonObject{}, true); add("list_services", QJsonObject{{"extra", 1}}, false);
  add("search_objects", QJsonObject{{"query", "x"}, {"offset", 0}, {"limit", 50}}, true);
  add("search_objects", QJsonObject{{"query", "x"}, {"offset", true}, {"limit", 50}}, false);
  add("search_objects", QJsonObject{{"query", "x"}, {"offset", 0}, {"limit", 51}}, false);
  add("get-tree", QJsonObject{{"oad", oad("A101")}}, true);
  add("get-tree", QJsonObject{{"oad", oad("A101")}, {"piid", QJsonObject{{"seqNo", ctx.piidSequence}}}}, true);
  add("get-tree", QJsonObject{{"oad", oad("A101")}, {"piid", QJsonObject{{"seqNo", ctx.piidSequence + 1}}}}, false);
  add("get-tree", QJsonObject{{"oad", oad("A101")}, {"frameOptions", QJsonObject{}}}, false);
  add("typed-data", QJsonObject{{"type", "structure"}, {"value", QJsonArray{QJsonObject{{"type", "unsigned"}, {"value", 1}}}}}, true);
  add("typed-data", QJsonObject{{"type", "unsigned"}, {"value", true}}, false);
  add("typed-data", QJsonObject{{"type", "not_real"}, {"value", 1}}, false);
  add("physical", QJsonObject{{"representation", "physical"}, {"value", "1.23"}, {"unit", "A"}}, true);
  add("physical", QJsonObject{{"representation", "raw"}, {"value", 123}, {"unit", "V"}}, false);
  const QJsonObject date{{"year", 2026}, {"month", 10}, {"day", 14}, {"hour", 12}, {"minute", 0}, {"second", 0}};
  QJsonObject record{{"oad", oad("A101")}, {"rcsd", QJsonObject{{"items", QJsonArray{}}}},
      {"rsd", QJsonObject{{"type", 7}, {"startTime", date}, {"endTime", date},
          {"ti", QJsonObject{{"unit", 0}, {"value", 1}}}, {"ms", QJsonObject{{"type", 0}}}}}};
  add("record-tree", record, true);
  auto badMs = record["rsd"].toObject(); badMs["ms"] = QJsonObject{{"type", 5}, {"rawHex", "0102"}}; record["rsd"] = badMs;
  add("record-tree", record, false);
  record["rsd"] = QJsonObject{{"type", 11}, {"items", QJsonArray{QJsonObject{{"type", 0}}}}};
  add("record-tree", record, true);
  QJsonObject proxyServer{{"tsa", QJsonObject{{"address", "112233445566"}, {"addressType", 2}, {"logicAddress", 1}}}, {"timeout", 5}, {"oads", QJsonArray{oad("A101")}}};
  add("proxy-tree", QJsonObject{{"totalTimeout", 10}, {"servers", QJsonArray{proxyServer}}}, true);
  proxyServer.remove("tsa"); proxyServer["tsaHex"] = "95665544332211";
  add("proxy-tree", QJsonObject{{"totalTimeout", 10}, {"servers", QJsonArray{proxyServer}}}, false);
  QJsonObject rowResult{{"oad", oad("A108")}, {"rcsd", QJsonObject{{"items", QJsonArray{QJsonObject{{"choice", 0}, {"oad", oad("A101")}}}}}}, {"choice", 1}, {"rows", QJsonArray{QJsonArray{1}}}};
  add("record-response", QJsonObject{{"recordResult", rowResult}}, true);
  auto malformedRow = rowResult; malformedRow["rows"] = QJsonArray{true};
  add("record-response", QJsonObject{{"recordResult", malformedRow}}, false);
  auto oldRow = rowResult; oldRow.remove("rows"); oldRow["rowCount"] = 1; oldRow["rowsHex"] = "1101";
  add("record-response", QJsonObject{{"recordResult", oldRow}}, false);
  rowResult["rcsd"] = QJsonObject{{"items", QJsonArray{QJsonObject{{"choice", 1}, {"oad", oad("A108")}, {"roadOads", QJsonArray{oad("A101")}}}}}};
  add("record-response", QJsonObject{{"recordResult", rowResult}}, false);
  rowResult["rows"] = QJsonArray{};
  add("record-response", QJsonObject{{"recordResult", rowResult}}, true);
  const auto python = QStandardPaths::findExecutable("python3");
  if (python.isEmpty()) { QTextStream(stdout) << "SKIP: optional independent JSON Schema checker: python3 unavailable\n"; return; }
  QProcess process;
  process.start(python, {"-c", R"PY(import json,sys
try:
 import jsonschema
except ImportError:
 sys.exit(77)
fixture=json.load(sys.stdin)
for schema in fixture['schemas'].values():
 jsonschema.Draft202012Validator.check_schema(schema)
for case in fixture['cases']:
 valid=jsonschema.Draft202012Validator(fixture['schemas'][case['schema']]).is_valid(case['value'])
 assert valid==case['valid'], (case['schema'], case['value'], valid)
print('independent JSON Schema cases:',len(fixture['cases']))
)PY"});
  expect(process.waitForStarted(5000), "independent JSON Schema checker starts");
  process.write(QJsonDocument(QJsonObject{{"schemas", schemas}, {"cases", cases}}).toJson(QJsonDocument::Compact));
  process.closeWriteChannel();
  expect(process.waitForFinished(15000), "independent JSON Schema checker finishes");
  if (process.exitCode() == 77) { QTextStream(stdout) << "SKIP: optional independent JSON Schema checker: jsonschema unavailable\n"; return; }
  expect(process.exitCode() == 0, "published schemas validate independent positive/negative fixtures: " + QString::fromUtf8(process.readAllStandardError()));
  QTextStream(stdout) << process.readAllStandardOutput();
}
void testContext(Ai698Tools &tools, const AiFrameContext &ctx) {
  rejects(tools.invoke("search_objects", {{"query", QString(128 * 1024, 'X')}, {"offset", 0}, {"limit", 1}}, ctx), "budget_exceeded", "arguments byte budget");
  QJsonValue nested = QJsonObject{{"value", 1}};
  for (int i = 0; i < 35; ++i) nested = QJsonObject{{"nested", nested}};
  rejects(tools.invoke("build_frame", draft("GET_NORMAL", {{"oad", oad("A101")}, {"extra", nested}}), ctx), "budget_exceeded", "whole tree depth budget");
  QJsonArray many; for (int i = 0; i < 10001; ++i) many.append(0);
  rejects(tools.invoke("build_frame", draft("GET_NORMAL", {{"oad", oad("A101")}, {"extra", many}}), ctx), "budget_exceeded", "whole tree node budget");
  auto changed = ctx; changed.definitionId = "not-current";
  rejects(tools.invoke("list_services", {}, changed), "context_changed", "revision freeze");
  changed = ctx; changed.piidSequence = 64;
  rejects(tools.invoke("build_frame", draft("GET_NORMAL", {{"oad", oad("A101")}}), changed), "invalid_input", "invalid local sequence");
  changed = ctx; changed.terminal["address"] = "0102x3";
  rejects(tools.invoke("build_frame", draft("GET_NORMAL", {{"oad", oad("A101")}}), changed), "invalid_input", "invalid terminal never falls back");
  changed = ctx; changed.terminal["clientAddress"] = true;
  rejects(tools.invoke("list_services", {}, changed), "invalid_input", "strict local terminal integer");
  changed = ctx; changed.terminal["address"] = "0A0B";
  const auto originalResult = data(tools.invoke("build_frame", draft("GET_NORMAL", {{"oad", oad("A101")}}), ctx), "original immutable context");
  expect(originalResult["terminal"].toObject()["address"] == "010203040506", "copy edits cannot mutate request snapshot");
}
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  const QString source = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(AI698_TEST_SOURCE_DIR);
  auto &manager = Prot698OadManager::instance();
  expect(manager.loadXml(source + "/protocol_xml/698_protocol.xml"), "real dictionary loads");
  Ai698Tools tools; testDirectory(tools, context()); testActualXml(tools, context());
  QTemporaryDir directory; expect(manager.loadXml(writeDictionary(directory)), "synthetic general dictionary loads");
  const auto snapshot = context(); testGeneralDefinitions(tools, snapshot); testBuilds(tools, snapshot); testStructuredProxyAndRows(tools, snapshot); testPublishedSchemas(tools, snapshot); testContext(tools, snapshot);
  expect(manager.loadXml(source + "/protocol_xml/698_protocol.xml"), "restore runtime real dictionary");
  rejects(tools.invoke("build_frame", draft("GET_NORMAL", {{"oad", oad("A101")}}), snapshot), "context_changed", "dictionary replaced between validation and build");
  QTextStream(stdout) << "ai698 tools checks: " << checks << ", failures: " << failures << '\n';
  return failures ? 1 : 0;
}
