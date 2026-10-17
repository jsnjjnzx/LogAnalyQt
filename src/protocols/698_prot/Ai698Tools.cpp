#include "Ai698Tools.h"
#include "Prot698DataEncoder.h"
#include "Prot698FrameBuilder.h"
#include "Prot698OadManager.h"
#include "Prot698Parser.h"
#include "Prot698ServiceBuilder.h"
#include "Prot698ServiceSchema.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <functional>

namespace {
constexpr int maximumArguments = 128 * 1024;
constexpr int maximumDefinition = 128 * 1024;
QJsonObject success(const QJsonObject &data) {
  return {{"ok", true}, {"data", data}, {"error", QJsonValue(QJsonValue::Null)}};
}
QJsonObject issue(const QString &code, const QString &message, const QString &path = "$") {
  return {{"code", code}, {"message", message}, {"path", path}};
}
QJsonObject failure(const QString &code, const QString &message, const QString &path = "$") {
  return {{"ok", false}, {"data", QJsonValue(QJsonValue::Null)}, {"error", issue(code, message, path)}};
}
QJsonObject schemaIssue(const QString &error) {
  const auto split = error.indexOf(": ");
  const QString path = error.startsWith('$') && split >= 0 ? error.left(split) : "$.treeValue";
  const QString message = split >= 0 ? error.mid(split + 2) : error;
  const QString code = message.contains("unsupported_definition") || message.contains("不支持的动态 Data 类型") ? "unsupported_definition"
      : message.contains("缺少必填") ? "missing_field"
      : message.contains("找不到对象") || message.contains("找不到 OAD") || message.contains("找不到 OMD") ? "unknown_object"
      : "invalid_arguments";
  return issue(code, message, path == "$" ? "$.treeValue" : "$.treeValue" + path.mid(1));
}
bool integer(const QJsonValue &value, int minimum, int maximum) {
  return value.isDouble() && std::isfinite(value.toDouble()) &&
      std::floor(value.toDouble()) == value.toDouble() &&
      value.toDouble() >= minimum && value.toDouble() <= maximum;
}
QString argumentsError(const QJsonObject &obj, const QStringList &keys,
                       const QStringList &required) {
  for (auto it = obj.begin(); it != obj.end(); ++it)
    if (!keys.contains(it.key())) return "$\t工具参数包含未知字段";
  for (const auto &key : required)
    if (!obj.contains(key)) return "$." + key + "\t缺少必填工具参数";
  return {};
}
QJsonObject argumentFailure(const QString &message) {
  const int split = message.indexOf('\t');
  return failure("invalid_arguments", message.mid(split + 1), message.left(split));
}
QJsonObject objectSchema(const QJsonObject &props, const QJsonArray &required) {
  return {{"type", "object"}, {"properties", props}, {"required", required}, {"additionalProperties", false}};
}
QJsonObject numericSchema(int minimum, int maximum) {
  return {{"type", "integer"}, {"minimum", minimum}, {"maximum", maximum}};
}
QJsonValue compactSchema(const QJsonValue &value, bool root = true) {
  if (value.isArray()) {
    QJsonArray out;
    for (const auto &entry : value.toArray()) out.append(compactSchema(entry, false));
    return out;
  }
  if (!value.isObject()) return value;
  QJsonObject out;
  const auto obj = value.toObject();
  for (auto it = obj.begin(); it != obj.end(); ++it) {
    if (!root && it.key() == "$defs") continue;
    out[it.key()] = compactSchema(it.value(), false);
  }
  return out;
}
QJsonObject dataDefinition(const Prot698SubItem &def, bool root = true) {
  auto out = QJsonObject::fromVariantMap(Prot698DataEncoder::subItemToSchema(def));
  out.remove("children");
  if (!root) out.remove("inputSchema");
  else out["inputSchema"] = compactSchema(out["inputSchema"]);
  QJsonArray children;
  for (int i = 0; i < def.children.size(); ++i) {
    auto child = dataDefinition(def.children.at(i), false);
    child["optionIndex"] = i;
    children.append(child);
  }
  out["children"] = children;
  return out;
}
bool withinTreeBudget(const QJsonValue &value, int *nodes, int depth = 0) {
  if (++*nodes > 10000 || depth > 32) return false;
  if (value.isArray()) {
    for (const auto &entry : value.toArray()) if (!withinTreeBudget(entry, nodes, depth + 1)) return false;
  } else if (value.isObject()) {
    const auto object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) if (!withinTreeBudget(it.value(), nodes, depth + 1)) return false;
  }
  return true;
}
QJsonObject service(const QString &code) {
  for (const auto &entry : Prot698ServiceSchema::serviceTypes())
    if (entry.toMap().value("code").toString() == code)
      return QJsonObject::fromVariantMap(entry.toMap());
  return {};
}
QJsonObject defaultPiid(const AiFrameContext &context) {
  return {{"seqNo", context.piidSequence}, {"highPriority", false}, {"acd", false}};
}
QJsonObject defaults(const AiFrameContext &context) {
  return {{"piid", defaultPiid(context)}, {"timeTag", QJsonObject{{"hasTimeTag", false}}}};
}
QString contextError(const AiFrameContext &context) {
  if (context.requestId.isEmpty() || context.requestId.size() > 80 || !context.submittedAt.isValid())
    return "请求上下文或提交时间无效";
  if (context.piidSequence < 0 || context.piidSequence > 63) return "冻结PIID不在0..63范围内";
  const auto &terminal = context.terminal;
  const QStringList keys{"id", "name", "address", "addressType", "logicAddress", "clientAddress"};
  if (!argumentsError(terminal, keys, keys).isEmpty()) return "终端快照字段不完整或含未知字段";
  for (const auto &key : {"id", "name"}) {
    if (!terminal[key].isString() || terminal[key].toString().trimmed().isEmpty() || terminal[key].toString().size() > 128)
      return "终端ID或名称无效";
  }
  if (!terminal["address"].isString()) return "终端物理地址必须为Hex字符串";
  const QString address = terminal["address"].toString().trimmed();
  if (!QRegularExpression("^([0-9A-Fa-f]{2}){1,16}$").match(address).hasMatch()) return "终端物理地址必须为1..16字节连续Hex";
  if (!integer(terminal["addressType"], 0, 3) || !integer(terminal["logicAddress"], 0, 1) ||
      !integer(terminal["clientAddress"], 0, 255)) return "终端地址类型、逻辑地址或客户机地址无效";
  return {};
}
QJsonArray warnings(const QString &oi = {}) {
  QJsonArray out;
  int duplicateOi = 0, duplicateMember = 0;
  for (const auto &entry : Prot698OadManager::instance().diagnostics()) {
    auto item = QJsonObject::fromVariantMap(entry.toMap());
    const QString code = item["code"].toString();
    if (code == "duplicate_oi") ++duplicateOi;
    if (code == "duplicate_member") ++duplicateMember;
    if (!oi.isEmpty() && item["oi"].toString() == oi) {
      item.remove("sourcePath");
      out.append(item);
    }
  }
  if (duplicateOi || duplicateMember)
    out.prepend(QJsonObject{{"code", "dictionary_duplicates"},
        {"message", "存在重复定义，沿用最后生效项；Size仅供参考，长度按数据类型与服务结构校验"},
        {"duplicateOiCount", duplicateOi}, {"duplicateMemberCount", duplicateMember}});
  return out;
}
QJsonArray schemaLimitations(const QJsonObject &schema) {
  QJsonArray limitations;
  QSet<QString> visitedReferences;
  std::function<void(const QJsonValue &, const QString &)> visit;
  visit = [&](const QJsonValue &value, const QString &path) {
    if (value.isArray()) {
      const auto array = value.toArray();
      for (int i = 0; i < array.size(); ++i) visit(array.at(i), path + "/" + QString::number(i));
    } else if (value.isObject()) {
      const auto object = value.toObject();
      const QString reference = object["$ref"].toString();
      if (reference.startsWith("#/$defs/") && !visitedReferences.contains(reference)) {
        visitedReferences.insert(reference);
        visit(schema["$defs"].toObject()[reference.mid(8)], reference);
      }
      if (object.contains("not") || object.contains("x-unsupportedReason")) limitations.append(QJsonObject{{"code", "unsupported_definition"},
          {"schemaPath", path}, {"message", object.contains("x-unsupportedReason") ? object["x-unsupportedReason"].toString() : object["description"].toString("该输入分支当前未实现")}});
      for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key() == "$defs") continue;
        QString escaped = it.key(); escaped.replace('~', "~0"); escaped.replace('/', "~1");
        visit(it.value(), path + "/" + escaped);
      }
    }
  };
  visit(schema, "#");
  return limitations;
}
QJsonValue shapeGuide(const QJsonObject &schema, int depth = 0) {
  if (depth > 4 || schema.contains("$ref")) return "<按inputSchema引用继续填写>";
  if (schema.contains("const")) return schema["const"];
  if (schema.contains("oneOf")) {
    QJsonArray alternatives;
    for (const auto &entry : schema["oneOf"].toArray()) alternatives.append(shapeGuide(entry.toObject(), depth + 1));
    return QJsonObject{{"chooseExactlyOneShape", alternatives}};
  }
  if (schema["type"] == "object") {
    QJsonObject fields;
    const auto props = schema["properties"].toObject();
    for (auto key : schema["required"].toArray()) fields[key.toString()] = shapeGuide(props[key.toString()].toObject(), depth + 1);
    return fields;
  }
  if (schema["type"] == "array") {
    if (schema.contains("prefixItems")) {
      QJsonArray fields;
      for (const auto &entry : schema["prefixItems"].toArray()) fields.append(shapeGuide(entry.toObject(), depth + 1));
      return fields;
    }
    return QJsonArray{shapeGuide(schema["items"].toObject(), depth + 1)};
  }
  return "<必填值；按inputSchema的" + schema["type"].toString("字段") + "约束填写>";
}
QString parameterSummary(const QJsonObject &tree) {
  QStringList labels;
  std::function<void(const QJsonValue &)> visit;
  visit = [&](const QJsonValue &value) {
    if (labels.size() >= 12) return;
    if (value.isArray()) { for (const auto &entry : value.toArray()) visit(entry); return; }
    if (!value.isObject()) return;
    const auto obj = value.toObject();
    if (obj["oi"].isString() && (obj.contains("attrId") || obj.contains("methodId"))) {
      const QString label = obj["oi"].toString().toUpper() +
          (obj.contains("methodId") ? " 方法" + QString::number(obj["methodId"].toInt())
                                     : " 属性" + QString::number(obj["attrId"].toInt()));
      if (!labels.contains(label)) labels.append(label);
    }
    for (auto it = obj.begin(); it != obj.end(); ++it) visit(it.value());
  };
  visit(tree);
  if (tree.contains("lastBlockNo")) labels.append("上一块序号 " + QString::number(tree["lastBlockNo"].toInt()));
  return labels.join("、");
}
QJsonObject boundedDefinition(const QJsonObject &data) {
  if (QJsonDocument(data).toJson(QJsonDocument::Compact).size() > maximumDefinition)
    return failure("budget_exceeded", "查询结果超过128KiB，请缩小分页或查询具体成员；未返回截断Schema");
  return success(data);
}
QJsonObject overview(int id, const Prot698AttrDef &def, bool method) {
  const auto schema = dataDefinition(def.dataDef);
  const bool validOp = method ? def.op == "ACTION"
      : def.op == "GET" || def.op == "SET" || def.op == "GET|SET" || def.op == "SET|GET";
  auto reasons = schema["unsupportedReasons"].toArray();
  if (!validOp) reasons.append("unsupported_definition: 未确认的OP " + def.op);
  const auto *value = &def.dataDef;
  if (value->type == "array" && value->children.size() == 1) value = &value->children.first();
  QJsonArray fields;
  for (const auto &child : value->children) {
    if (fields.size() == 16) break;
    fields.append(QJsonObject{{"name", child.name}, {"type", child.type}});
  }
  return {{"id", id}, {"name", def.name}, {"op", def.op},
      {"type", def.dataDef.type}, {"fields", fields},
      {"hasMoreFields", value->children.size() > fields.size()},
      {"buildSupported", validOp && schema["buildSupported"].toBool()},
      {"unsupportedReasons", reasons}, {"verificationStatus", "pending"}};
}
}

QJsonArray Ai698Tools::definitions() const {
  const auto draft = objectSchema({{"serviceCode", QJsonObject{{"type", "string"}, {"minLength", 1}}},
      {"treeValue", QJsonObject{{"type", "object"}, {"description", "Use get_definition inputSchema; frozen PIID/absent timeTag defaults may be omitted"}}}}, {"serviceCode", "treeValue"});
  const auto serviceDefinition = objectSchema({{"kind", QJsonObject{{"const", "service"}}},
      {"serviceCode", QJsonObject{{"type", "string"}, {"minLength", 1}}}}, {"kind", "serviceCode"});
  const auto objectDefinition = objectSchema({{"kind", QJsonObject{{"const", "object"}}},
      {"oi", QJsonObject{{"type", "string"}, {"pattern", "^[0-9A-Fa-f]{4}$"}}},
      {"memberId", numericSchema(0, 255)}, {"isMethod", QJsonObject{{"type", "boolean"}}},
      {"dataIndex", numericSchema(0, 255)}}, {"kind", "oi", "memberId", "isMethod", "dataIndex"});
  QJsonArray out;
  const auto add = [&](const QString &name, const QString &description, const QJsonObject &parameters) {
    out.append(QJsonObject{{"type", "function"}, {"function", QJsonObject{
        {"name", name}, {"description", description}, {"parameters", parameters}}}});
  };
  add("list_services", "Discover all local 698 services and branch limitations; implementation and evidence status are separate.", objectSchema({}, {}));
  add("search_objects", "Search the loaded XML by OI/object/attribute/method names. Results are paginated; never invent object definitions.",
      objectSchema({{"query", QJsonObject{{"type", "string"}, {"maxLength", 200}}},
          {"offset", numericSchema(0, 2147483647)}, {"limit", numericSchema(1, 50)}}, {"query", "offset", "limit"}));
  auto definitionProperties = serviceDefinition["properties"].toObject();
  const auto memberProperties = objectDefinition["properties"].toObject();
  for (auto it = memberProperties.begin(); it != memberProperties.end(); ++it) definitionProperties[it.key()] = it.value();
  definitionProperties["kind"] = QJsonObject{{"type", "string"}, {"enum", QJsonArray{"service", "object"}}};
  auto definitionSchema = objectSchema(definitionProperties, {"kind"});
  definitionSchema["oneOf"] = QJsonArray{serviceDefinition, objectDefinition};
  add("get_definition", "Get a complete service inputSchema or an XML object member definition; memberId=0 returns its member indexes. Shape guides are not business defaults.", definitionSchema);
  add("validate_draft", "Validate a serviceCode/treeValue against the frozen dictionary and terminal context. No PIID allocation or target changes.", draft);
  add("build_frame", "Revalidate and deterministically construct one locally checked frame using the frozen terminal/PIID. Never supply frameOptions or a model-generated frame.", draft);
  return out;
}

QJsonObject Ai698Tools::invoke(const QString &name, const QJsonObject &arguments,
                               const AiFrameContext &context) const {
  if (!QStringList{"list_services", "search_objects", "get_definition", "validate_draft", "build_frame"}.contains(name))
    return failure("unknown_tool", "未知本地工具");
  if (QJsonDocument(arguments).toJson(QJsonDocument::Compact).size() > maximumArguments)
    return failure("budget_exceeded", "工具参数超过128KiB");
  int nodes = 0;
  const auto budgetValue = arguments["treeValue"].isObject() ? arguments["treeValue"] : QJsonValue(arguments);
  if (!withinTreeBudget(budgetValue, &nodes)) return failure("budget_exceeded", "工具树超过32层深度或10000节点预算");
  auto &manager = Prot698OadManager::instance();
  if (manager.definitionId().isEmpty()) return failure("configuration_missing", "698对象字典尚未加载");
  if (context.definitionId.isEmpty() || context.definitionId != manager.definitionId())
    return failure("context_changed", "对象定义已变化，请重新提交请求");
  const auto invalidContext = contextError(context);
  if (!invalidContext.isEmpty()) return failure("invalid_input", invalidContext, "$.context");

  if (name == "list_services") {
    const auto error = argumentsError(arguments, {}, {});
    if (!error.isEmpty()) return argumentFailure(error);
    return success({{"items", QJsonArray::fromVariantList(Prot698ServiceSchema::serviceTypes())},
        {"definitionId", manager.definitionId()}, {"warnings", warnings()}});
  }
  if (name == "search_objects") {
    const auto error = argumentsError(arguments, {"query", "offset", "limit"}, {"query", "offset", "limit"});
    if (!error.isEmpty()) return argumentFailure(error);
    if (!arguments["query"].isString() || arguments["query"].toString().size() > 200)
      return failure("invalid_arguments", "查询必须是最多200字符的字符串", "$.query");
    if (!integer(arguments["offset"], 0, 2147483647) || !integer(arguments["limit"], 1, 50))
      return failure("invalid_arguments", "offset必须为非负整数，limit必须为1..50整数");
    const auto matches = manager.listAllOi(arguments["query"].toString());
    const int offset = arguments["offset"].toInt(), limit = arguments["limit"].toInt();
    QJsonArray items;
    for (qsizetype i = offset; i < matches.size() && i - offset < limit; ++i)
      items.append(QJsonObject::fromVariantMap(matches.at(i).toMap()));
    return boundedDefinition({{"items", items}, {"total", static_cast<int>(matches.size())},
        {"offset", offset}, {"hasMore", offset < matches.size() && matches.size() - offset > items.size()},
        {"definitionId", manager.definitionId()}, {"warnings", warnings()}});
  }
  if (name == "get_definition") {
    if (!arguments["kind"].isString()) return failure("invalid_arguments", "kind必须为service或object", "$.kind");
    if (arguments["kind"] == "service") {
      const auto error = argumentsError(arguments, {"kind", "serviceCode"}, {"kind", "serviceCode"});
      if (!error.isEmpty()) return argumentFailure(error);
      if (!arguments["serviceCode"].isString() || arguments["serviceCode"].toString().trimmed().isEmpty()) return failure("invalid_arguments", "serviceCode必须是非空字符串", "$.serviceCode");
      const QString code = arguments["serviceCode"].toString().trimmed().toUpper();
      auto data = QJsonObject::fromVariantMap(Prot698ServiceSchema::schemaForService(code));
      if (data.isEmpty()) return failure("unsupported_definition", "未知服务", "$.serviceCode");
      auto schema = compactSchema(data["inputSchema"]).toObject();
      auto properties = schema["properties"].toObject();
      auto piidSchema = properties["piid"].toObject();
      auto piidProperties = piidSchema["properties"].toObject();
      auto sequence = piidProperties["seqNo"].toObject(); sequence["const"] = context.piidSequence;
      piidProperties["seqNo"] = sequence; piidSchema["properties"] = piidProperties; properties["piid"] = piidSchema;
      schema["properties"] = properties;
      QJsonArray required;
      for (const auto &key : schema["required"].toArray()) if (key != "piid") required.append(key);
      schema["required"] = required;
      data["inputSchema"] = schema;
      data.remove("children"); // UI kind descriptors do not express tool transport defaults.
      data["transportDefaultsMayBeOmitted"] = true;
      data["kind"] = "service"; data["serviceCode"] = code;
      data["serviceName"] = service(code)["name"];
      data["definitionId"] = manager.definitionId(); data["defaults"] = defaults(context);
      data["shapeGuide"] = shapeGuide(data["inputSchema"].toObject()); data["shapeOnly"] = true;
      data["limitations"] = schemaLimitations(data["inputSchema"].toObject());
      data["warnings"] = warnings();
      return boundedDefinition(data);
    }
    if (arguments["kind"] != "object") return failure("invalid_arguments", "kind必须为service或object", "$.kind");
    const auto error = argumentsError(arguments, {"kind", "oi", "memberId", "isMethod", "dataIndex"}, {"kind", "oi", "memberId", "isMethod", "dataIndex"});
    if (!error.isEmpty()) return argumentFailure(error);
    if (!arguments["oi"].isString() || !QRegularExpression("^[0-9A-Fa-f]{4}$").match(arguments["oi"].toString()).hasMatch())
      return failure("invalid_arguments", "OI必须为4位Hex", "$.oi");
    if (!integer(arguments["memberId"], 0, 255) || !integer(arguments["dataIndex"], 0, 255) || !arguments["isMethod"].isBool())
      return failure("invalid_arguments", "成员/索引必须为0..255整数，isMethod必须为bool");
    const QString oi = arguments["oi"].toString().toUpper();
    const int memberId = arguments["memberId"].toInt(), index = arguments["dataIndex"].toInt();
    auto *object = manager.getObjDef(oi);
    if (!object) return failure("unknown_object", "对象不在当前字典中", "$.oi");
    QJsonObject data{{"kind", "object"}, {"oi", oi}, {"objectName", object->name},
        {"definitionId", manager.definitionId()}, {"warnings", warnings(oi)}, {"verificationStatus", "pending"}};
    if (!memberId) {
      if (index != 0) return failure("invalid_arguments", "成员索引模式的dataIndex必须为0", "$.dataIndex");
      QJsonArray attrs, methods;
      for (auto it = object->attributes.cbegin(); it != object->attributes.cend(); ++it) attrs.append(overview(it.key(), it.value(), false));
      for (auto it = object->methods.cbegin(); it != object->methods.cend(); ++it) methods.append(overview(it.key(), it.value(), true));
      data["attrs"] = attrs; data["methods"] = methods;
      data["buildSupported"] = true; data["unsupportedReasons"] = QJsonArray{};
      return boundedDefinition(data);
    }
    const bool method = arguments["isMethod"].toBool();
    auto *member = method ? manager.getMethodDef(oi, memberId) : manager.getAttrDef(oi, memberId);
    if (!member) return failure("unknown_object", "属性或方法不在当前对象定义中", "$.memberId");
    auto memberInfo = overview(memberId, *member, method);
    const auto definition = dataDefinition(member->dataDef);
    for (auto it = definition.begin(); it != definition.end(); ++it) data[it.key()] = it.value();
    data["name"] = member->name; data["op"] = member->op;
    data["memberId"] = memberId; data["isMethod"] = method; data["dataIndex"] = index;
    auto reasons = memberInfo["unsupportedReasons"].toArray();
    if (index != 0) reasons.append("$.dataIndex: unsupported_definition: 非零数据索引尚未向AI开放");
    data["buildSupported"] = memberInfo["buildSupported"].toBool() && index == 0;
    data["unsupportedReasons"] = reasons;
    data["shapeGuide"] = shapeGuide(data["inputSchema"].toObject()); data["shapeOnly"] = true;
    data["limitations"] = schemaLimitations(data["inputSchema"].toObject());
    return boundedDefinition(data);
  }

  const auto error = argumentsError(arguments, {"serviceCode", "treeValue"}, {"serviceCode", "treeValue"});
  if (!error.isEmpty()) return argumentFailure(error);
  if (!arguments["serviceCode"].isString() || arguments["serviceCode"].toString().trimmed().isEmpty() || !arguments["treeValue"].isObject())
    return failure("invalid_arguments", "需要非空serviceCode和对象treeValue");
  const QString code = arguments["serviceCode"].toString().trimmed().toUpper();
  auto tree = arguments["treeValue"].toObject();
  if (!tree.contains("piid")) tree["piid"] = defaultPiid(context);
  if (!tree.contains("timeTag")) tree["timeTag"] = QJsonObject{{"hasTimeTag", false}};
  QJsonArray errors;
  const auto piid = tree["piid"].toObject();
  if (!tree["piid"].isObject() || !integer(piid["seqNo"], 0, 63) || piid["seqNo"].toInt() != context.piidSequence)
    errors.append(issue("context_changed", "PIID必须匹配本次请求冻结的序号", "$.treeValue.piid.seqNo"));
  QVariantMap normalized;
  QString validationError;
  if (errors.isEmpty() && !Prot698ServiceSchema::validateJsonTreeValue(code, tree, &normalized, &validationError, true)) errors.append(schemaIssue(validationError));
  const auto metadata = service(code);
  if (errors.isEmpty() && !metadata["buildSupported"].toBool()) errors.append(issue("unsupported_definition", "该服务当前不可构建", "$.serviceCode"));
  if (name == "validate_draft") return success({{"valid", errors.isEmpty()}, {"errors", errors}, {"definitionId", manager.definitionId()}});
  if (!errors.isEmpty()) return {{"ok", false}, {"data", QJsonValue(QJsonValue::Null)}, {"error", errors.first()}};

  auto spec = Prot698ServiceSchema::toServiceSpec(code, normalized, &validationError);
  if (!validationError.isEmpty()) return failure("build_failed", "已验证草稿编码失败: " + validationError);
  const auto apdu = Prot698ServiceBuilder::buildFromSpec(spec, &validationError);
  if (apdu.isEmpty() || !validationError.isEmpty()) return failure("build_failed", "APDU编码失败: " + validationError);
  auto frameOptions = Prot698FrameBuilder::defaultClientRequestOptions(context.terminal["address"].toString().trimmed());
  const bool response = metadata["direction"] == "服务器响应";
  frameOptions.dir = response; frameOptions.prm = !response; frameOptions.funcCode = 3;
  frameOptions.saType = context.terminal["addressType"].toInt();
  frameOptions.logicAddress = context.terminal["logicAddress"].toInt();
  frameOptions.ca = context.terminal["clientAddress"].toInt();
  const auto frame = Prot698FrameBuilder::buildFrame(apdu, frameOptions, &validationError);
  if (frame.isEmpty() || !validationError.isEmpty() || !Prot698Parser::validateSingleFrame(frame, &validationError))
    return failure("build_failed", "完整帧未通过本地校验: " + validationError);
  if (context.definitionId != manager.definitionId()) return failure("context_changed", "构建期间对象定义已变化，请重新提交");
  const auto normalizedJson = Prot698DataEncoder::toExactJson(normalized).toObject();
  return success({{"requestId", context.requestId}, {"serviceCode", code}, {"serviceName", metadata["name"]},
      {"treeValue", tree}, {"normalizedTree", normalizedJson},
      {"inputTreeText", QString::fromUtf8(QJsonDocument(tree).toJson(QJsonDocument::Indented))},
      {"normalizedTreeText", QString::fromUtf8(QJsonDocument(normalizedJson).toJson(QJsonDocument::Indented))},
      {"terminal", context.terminal},
      {"submittedAt", context.submittedAt.toOffsetFromUtc(context.submittedAt.offsetFromUtc()).toString(Qt::ISODateWithMs)},
      {"definitionId", manager.definitionId()}, {"apduHex", QString::fromLatin1(apdu.toHex().toUpper())},
      {"frameHex", QString::fromLatin1(frame.toHex().toUpper())},
      {"summary", metadata["name"].toString() + "；" + parameterSummary(tree) + "；已构建并通过本地完整帧校验"}, {"warnings", warnings()}});
}
