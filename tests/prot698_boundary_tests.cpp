#include "698_prot/Prot698DataEncoder.h"
#include "698_prot/Prot698FrameBuilder.h"
#include "698_prot/Prot698OadManager.h"
#include "698_prot/Prot698Parser.h"
#include "698_prot/Prot698ServiceBuilder.h"
#include "698_prot/Prot698ServiceSchema.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>
#include <limits>

namespace {
int failures = 0;
int checks = 0;
void expect(bool condition, const QString &message) {
  ++checks;
  if (!condition) { ++failures; QTextStream(stderr) << "FAIL: " << message << '\n'; }
}
Prot698SubItem leaf(const QString &type) { Prot698SubItem d; d.type = type; return d; }
QByteArray encoded(const Prot698SubItem &d, const QJsonValue &v, bool ai = true) {
  QVariant raw; QString error;
  const bool ok = Prot698DataEncoder::validateJsonValue(d, v, &raw, &error, "$.value", ai);
  expect(ok, "valid " + d.type + ": " + error);
  if (!ok) return {};
  const auto bytes = Prot698DataEncoder::encode(d, raw, &error);
  expect(!bytes.isEmpty() && error.isEmpty(), "encode validated " + d.type + ": " + error);
  return bytes;
}
void reject(const Prot698SubItem &d, const QJsonValue &v, const QString &why, bool ai = true) {
  QVariant raw; QString error;
  expect(!Prot698DataEncoder::validateJsonValue(d, v, &raw, &error, "$.value", ai), why);
  expect(error.contains("$.value"), why + " must include a field path: " + error);
}
void rejectRaw(const Prot698SubItem &d, const QVariant &v, const QString &why) {
  QString error; const auto bytes = Prot698DataEncoder::encode(d, v, &error);
  expect(bytes.isEmpty() && !error.isEmpty(), why + ": " + bytes.toHex() + " / " + error);
}

// Independent non-reflected polynomial implementation; production uses reflected 0x8408.
quint16 reverse16(quint16 x) {
  quint16 y = 0; for (int i=0; i<16; ++i) { y = quint16((y << 1) | (x & 1)); x >>= 1; } return y;
}
quint16 referenceCrc(const QByteArray &bytes) {
  quint16 state = 0xffff;
  for (unsigned char byte : bytes) {
    unsigned char reversed = 0;
    for (int bit=0; bit<8; ++bit) reversed |= ((byte >> bit) & 1) << (7-bit);
    state ^= quint16(reversed) << 8;
    for (int bit=0; bit<8; ++bit) state = quint16((state << 1) ^ ((state & 0x8000) ? 0x1021 : 0));
  }
  return reverse16(state) ^ 0xffff;
}
void appendLe(QByteArray &b, quint16 value) { b.append(char(value)); b.append(char(value >> 8)); }
void writeLe(QByteArray &b, int offset, quint16 value) { b[offset]=char(value); b[offset+1]=char(value >> 8); }
QByteArray referenceFrame(const QByteArray &apdu, const QByteArray &wireAddress, quint8 control=0x43) {
  QByteArray out; out.append(char(0x68)); appendLe(out, quint16(9 + wireAddress.size() + apdu.size()));
  out.append(char(control)); out.append(char(wireAddress.size()-1)); out.append(wireAddress); out.append(char(0));
  appendLe(out, referenceCrc(out.mid(1))); out.append(apdu); appendLe(out, referenceCrc(out.mid(1))); out.append(char(0x16));
  return out;
}
void refreshFcs(QByteArray &frame) { writeLe(frame, frame.size()-3, referenceCrc(frame.mid(1, frame.size()-4))); }
void checkStrict(const QByteArray &frame, bool valid, const QString &why, bool apdu=true) {
  QString error;
  expect(Prot698Parser::validateSingleFrame(frame, &error, apdu) == valid, why + ": " + error);
  if (!valid) expect(!error.isEmpty(), why + " must report failure");
}
void testPrimitiveBoundaries() {
  const struct { const char *type; double min; double max; } ranges[] = {
    {"integer",-128,127},{"unsigned",0,255},{"long",-32768,32767},
    {"long_unsigned",0,65535},{"double_long",-2147483648.0,2147483647.0},
    {"double_long_unsigned",0,4294967295.0}};
  for (const auto &r:ranges) {
    auto d=leaf(r.type); encoded(d,r.min); encoded(d,r.max);
    reject(d,r.min-1,QString(r.type)+" below minimum"); reject(d,r.max+1,QString(r.type)+" above maximum");
    reject(d,true,QString(r.type)+" bool-as-number"); reject(d,1.5,QString(r.type)+" fractional");
    reject(d,"abc",QString(r.type)+" invalid decimal");
  }
  rejectRaw(leaf("unsigned"),256,"raw unsigned must not truncate");
  rejectRaw(leaf("integer"),128,"raw integer must not wrap sign");
  rejectRaw(leaf("not_a_type"),123,"unknown raw type must not become null");
  rejectRaw(leaf(""),123,"empty raw type must not become null");
  reject(leaf("not_a_type"),123,"unknown JSON type");
  expect(encoded(leaf("null"),QJsonValue(QJsonValue::Null)).toHex()=="00","explicit null encodes as null");
  reject(leaf("null"),0,"null must be explicit");
  expect(encoded(leaf("bool"),false).toHex()=="0300","false remains false");
  reject(leaf("bool"),"false","bool string must not become true");
  reject(leaf("bool"),1,"number must not become bool");
  auto enumeration=leaf("enum"); enumeration.enumValues={{1,"one"},{3,"three"}};
  expect(encoded(enumeration,3).toHex()=="1603","declared enum member");
  reject(enumeration,2,"undeclared enum value");
  reject(enumeration,"one","enum labels are not invented numeric values");
  expect(encoded(leaf("long64"),"-9223372036854775808").toHex()=="148000000000000000","signed64 minimum is exact");
  expect(encoded(leaf("long64"),"9223372036854775807").toHex()=="147fffffffffffffff","signed64 maximum is exact");
  expect(encoded(leaf("long64_unsigned"),"18446744073709551615").toHex()=="15ffffffffffffffff","unsigned64 maximum is exact");
  reject(leaf("long64"),"9223372036854775808","signed64 overflow");
  reject(leaf("long64_unsigned"),"18446744073709551616","unsigned64 overflow");
  reject(leaf("long64_unsigned"),"-1","unsigned64 negative");
  reject(leaf("long64"),9007199254740992.0,"unsafe JSON integer must use decimal string");
  reject(leaf("long64"),"1e3","integer exponent string is not decimal integer");
  reject(leaf("float32"),QJsonValue(std::numeric_limits<double>::infinity()),"nonfinite float32");
  reject(leaf("float64"),QJsonValue(std::numeric_limits<double>::quiet_NaN()),"nonfinite float64");
}
void testIndependentFloatAndResponseVectors() {
  // IEEE bit vectors and response suffix order independently derived from I02/I03.
  expect(encoded(leaf("float32"),1.0).toHex()=="173f800000","float32 1.0 independent network-order vector");
  expect(encoded(leaf("float32"),3.141592653589793).toHex()=="1740490fdb","float32 pi independent network-order vector");
  expect(encoded(leaf("float64"),1.0).toHex()=="183ff0000000000000","float64 1.0 independent network-order vector");
  expect(encoded(leaf("float64"),3.141592653589793).toHex()=="18400921fb54442d18","float64 pi independent network-order vector");
  const struct { const char *code; const char *field; const char *result; const char *apdu; } responses[]={
    {"GET_RESPONSE_NORMAL","resultHex","400002000000","8501014000020000000000"},
    {"SET_RESPONSE_NORMAL","resultHex","4000020000","86010140000200000000"},
    {"ACTION_RESPONSE_NORMAL","resultHex","400001000000","8701014000010000000000"}};
  for(const auto &r:responses) {
    QVariantMap spec{{"serviceCode",r.code},{"piid",QVariantMap{{"seqNo",1},{"highPriority",false},{"acd",false}}},{r.field,r.result}};
    QString error;auto bytes=Prot698ServiceBuilder::buildFromSpec(spec,&error);
    expect(bytes.toHex()==r.apdu&&error.isEmpty(),QString(r.code)+" independently derived no-FollowReport/no-TimeTag: "+error+" / "+bytes.toHex());
    int consumed=-1;error.clear();expect(Prot698Parser::validateApdu(QByteArray::fromHex(r.apdu),&error,&consumed)&&consumed==QByteArray::fromHex(r.apdu).size(),QString(r.code)+" independent response consumed: "+error);
    error.clear();expect(!Prot698Parser::validateApdu(QByteArray::fromHex(r.apdu).chopped(1),&error,&consumed),QString(r.code)+" missing second optional flag rejects");
    spec["timeTag"]=QVariantMap{{"hasTimeTag",true},{"year",2026},{"month",10},{"day",14},{"hour",0},{"minute",0},{"second",0},{"tiUnit",0},{"tiValue",1}};
    error.clear();bytes=Prot698ServiceBuilder::buildFromSpec(spec,&error);
    const auto expected=QByteArray::fromHex(r.apdu).chopped(1)+QByteArray::fromHex("0107ea0a0e000000000001");
    expect(bytes==expected&&error.isEmpty(),QString(r.code)+" FollowReport flag precedes nonempty TimeTag: "+error);
  }
}
void testDatesStringsAndContainers() {
  const QJsonObject date{{"year",2024},{"month",2},{"day",29},{"hour",23},{"minute",59},{"second",59}};
  expect(encoded(leaf("date_time_s"),date).toHex()=="1c07e8021d173b3b","leap datetime fields encode explicitly");
  auto invalid=date; invalid["year"]=2023; reject(leaf("date_time_s"),invalid,"invalid leap date");
  invalid=date; invalid["day"]=30; reject(leaf("date_time_s"),invalid,"February30");
  invalid=date; invalid.remove("second"); reject(leaf("date_time_s"),invalid,"missing second");
  invalid=date; invalid["hour"]=24; reject(leaf("date_time_s"),invalid,"hour24");
  reject(leaf("date_time_s"),QJsonObject{},"missing datetime");
  rejectRaw(leaf("date_time_s"),QVariantMap{},"raw missing datetime");
  rejectRaw(leaf("bit_string"),"10x1","raw invalid bit must not become zero");
  reject(leaf("bit_string"),"10x1","invalid JSON bitstring");
  expect(encoded(leaf("bit_string"),"1001").toHex()=="040490","non-byte-aligned bits");
  expect(encoded(leaf("utf8_string"),QString::fromUtf8("中")).toHex()=="0c03e4b8ad","UTF8 length is byte count");
  reject(leaf("octet_string"),"123","odd hex"); reject(leaf("octet_string"),"GG","invalid hex");
  reject(leaf("octet_string"),"A0xB","embedded hex prefix cannot splice nibbles");
  reject(leaf("octet_string"),"0x0xAB","repeated hex prefix rejects");
  expect(encoded(leaf("octet_string"),"0xAB").toHex()=="0901ab","one leading hex prefix is accepted");
  auto structure=leaf("structure"); structure.children={leaf("unsigned"),leaf("bool")};
  expect(encoded(structure,QJsonArray{1,false}).toHex()=="020211010300","structure fields preserve order");
  reject(structure,QJsonArray{1},"missing structure child"); reject(structure,QJsonArray{1,false,2},"extra structure child");
  rejectRaw(structure,QVariantList{1},"raw structure missing value must not fill zero");
  reject(structure,QJsonObject{{"value",1}},"container object is not ordered array");
  auto array=leaf("array"); array.children={structure};
  expect(encoded(array,QJsonArray{QJsonValue(QJsonArray{1,true})}).toHex()=="0101020211010301","array of structures");
  reject(array,QJsonArray{QJsonValue(QJsonArray{true,1})},"nested type mismatch");
  auto simpleArray=leaf("array"); simpleArray.children={leaf("unsigned")}; QJsonArray tooMany;
  for(int i=0;i<10001;++i)tooMany.append(1);
  reject(simpleArray,tooMany,"container node/count budget");
  auto withUnit=leaf("long_unsigned"); withUnit.unit="A"; withUnit.scaler=-2; withUnit.hasScaler=true;
  expect(encoded(withUnit,QJsonObject{{"representation","physical"},{"value","1.23"},{"unit","A"}}).toHex()=="12007b","exact physical scaling");
  expect(encoded(withUnit,QJsonObject{{"representation","raw"},{"value",123},{"unit","A"}}).toHex()=="12007b","explicit raw scaling");
  reject(withUnit,123,"AI numeric leaf must declare representation and unit");
  reject(withUnit,QJsonObject{{"representation","physical"},{"value","1.234"},{"unit","A"}},"physical value not integral in wire units");
  reject(withUnit,QJsonObject{{"representation","raw"},{"value",123},{"unit","V"}},"wrong unit");
  reject(withUnit,QJsonObject{{"representation","raw"},{"value",123},{"unit","A"},{"extra",1}},"unknown numeric wrapper key");
  expect(encoded(withUnit,123,false).toHex()=="12007b","old manual input keeps raw value semantics");
}
void testDynamicDataAndOptions() {
  auto data=leaf("Data");
  expect(encoded(data,QJsonObject{{"type","unsigned"},{"value",1}}).toHex()=="1101","typed Data does not encode null");
  const QJsonObject dynamic{{"type","structure"},{"value",QJsonArray{
    QJsonObject{{"type","unsigned"},{"value",1}},QJsonObject{{"type","null"},{"value",QJsonValue(QJsonValue::Null)}}}}};
  expect(encoded(data,dynamic).toHex()=="0202110100","dynamic structure has typed children");
  reject(data,1,"Data requires explicit type");
  reject(data,QJsonObject{{"type","bad"},{"value",1}},"unknown dynamic type");
  reject(data,QJsonObject{{"type","unsigned"},{"value",1},{"extra",true}},"unknown dynamic key");
  reject(data,QJsonObject{{"type","unsigned"}},"dynamic value missing");
  QJsonValue nested=QJsonObject{{"type","unsigned"},{"value",1}};
  for(int i=0;i<34;++i)nested=QJsonObject{{"type","array"},{"value",QJsonArray{nested}}};
  reject(data,nested,"dynamic depth budget");
  auto array=leaf("array"); auto first=leaf("option");first.typeOption="unsigned";first.selected=true;
  auto second=leaf("option");second.typeOption="long_unsigned";array.children={first,second};
  expect(encoded(array,QJsonArray{QJsonObject{{"optionIndex",1},{"value",256}}}).toHex()=="0101120100","explicit optionIndex overrides Selected without choice byte");
  reject(array,QJsonArray{1},"option must not auto-select Selected");
  reject(array,QJsonArray{QJsonObject{{"optionIndex",2},{"value",1}}},"option index range");
  reject(array,QJsonArray{QJsonObject{{"optionIndex",0},{"value",256}}},"candidate numeric range");
  reject(array,QJsonArray{QJsonObject{{"optionIndex",true},{"value",1}}},"option index bool");
  auto duplicate=first;duplicate.name="other unsigned"; array.children={first,duplicate};
  expect(encoded(array,QJsonArray{QJsonObject{{"optionIndex",1},{"value",7}}}).toHex()=="01011107","duplicate option types remain position-addressable");
  const auto schema=Prot698DataEncoder::subItemToSchema(array);
  expect(schema.contains("inputSchema"),"public definition exposes recursive inputSchema");
}
void testFrameBoundaries() {
  expect(referenceCrc("123456789")==0x906e,"independent CRC published check vector");
  expect(Prot698Parser::calcCRC16(reinterpret_cast<const quint8 *>("123456789"),9)==0x906e,"production CRC check vector");
  // Literal from an independent project's fixture, not generated by this builder.
  // Source and provenance limits: tests/prot698_sources.md, I01.
  const auto external = QByteArray::fromHex("681700430501000000000000a7e60501010000030000838016");
  expect(referenceCrc(external.mid(1,11))==0xe6a7,"independent literal HCS");
  expect(referenceCrc(external.mid(1,21))==0x8083,"independent literal FCS");
  checkStrict(external,true,"independent project literal request");
  QString externalError;
  expect(Prot698FrameBuilder::buildFrame(QByteArray::fromHex("0501010000030000"),
    Prot698FrameBuilder::defaultClientRequestOptions("000000000001"),&externalError)==external,
    "builder matches independently sourced literal frame: "+externalError);
  const auto apdu=QByteArray::fromHex("0501014000020000"); const auto address=QByteArray::fromHex("060504030201");
  const auto expected=referenceFrame(apdu,address);
  auto options=Prot698FrameBuilder::defaultClientRequestOptions("010203040506");
  expect(options.serverAddr==address,"display address reverses exactly once"); QString error;
  expect(Prot698FrameBuilder::buildFrame(apdu,options,&error)==expected,"builder agrees with independent layout/CRC oracle");
  const auto manual=Prot698FrameBuilder::optionsFromMap({{"serverAddrHex","060504030201"}});
  expect(manual.serverAddr==address,"legacy options map preserves wire-order bytes");
  expect(Prot698FrameBuilder::buildFrame(apdu,manual,&error)==expected,"two address entry paths agree when semantics are explicit");
  checkStrict(expected,true,"valid complete request");
  checkStrict(expected+char(0),false,"strict validator rejects trailing byte");
  expect(Prot698Parser::isValidFrame(expected+expected),"log-prefix detector keeps multi-frame prefix behavior");
  checkStrict(expected.left(expected.size()-1),false,"truncated end");
  auto corrupt=expected; corrupt[0]=char(0x69); checkStrict(corrupt,false,"bad start");
  corrupt=expected;corrupt[corrupt.size()-1]=char(0x17);checkStrict(corrupt,false,"bad end");
  corrupt=expected;corrupt[6+address.size()]^=char(1);refreshFcs(corrupt);
  checkStrict(corrupt,false,"bad HCS despite independently corrected FCS");
  corrupt=expected;corrupt[corrupt.size()-3]^=char(1);checkStrict(corrupt,false,"bad FCS");
  corrupt=expected;corrupt[4]=char(0x0f);checkStrict(corrupt,false,"address length exceeds actual frame header");
  for(int length=0;length<10;++length) {
    QByteArray shortFrame(12,char(0));shortFrame[0]=char(0x68);shortFrame[1]=char(length);shortFrame[11]=char(0x16);
    checkStrict(shortFrame,false,QString("tiny declared length %1").arg(length));
    expect(!Prot698Parser::isValidFrame(shortFrame),"prefix detector safely rejects tiny declaration");
  }
  for(int n=0;n<expected.size();++n)checkStrict(expected.left(n),false,"every incomplete prefix rejects");
  checkStrict(referenceFrame(apdu,address,0x63),false,"unsupported split flag");
  checkStrict(referenceFrame(apdu,address,0x4b),false,"unsupported scramble flag");
  QByteArray largest(0x3fff-9-address.size(),char(0));
  error.clear();const auto full=Prot698FrameBuilder::buildFrame(largest,options,&error);
  expect(!full.isEmpty()&&full.size()==0x4001&&error.isEmpty(),"maximum14-bit frame length accepted structurally");
  checkStrict(full,true,"max length frame CRC",false);
  largest.append(char(0));error.clear();expect(Prot698FrameBuilder::buildFrame(largest,options,&error).isEmpty()&&!error.isEmpty(),"14-bit overflow rejected before truncation");
  int consumed=-1;error.clear();expect(Prot698Parser::validateApdu(apdu,&error,&consumed)&&consumed==apdu.size(),"APDU exact consumption");
  error.clear();expect(!Prot698Parser::validateApdu(apdu+char(0),&error,&consumed),"APDU trailing byte rejected");
  for(int n=0;n<apdu.size();++n) {error.clear();expect(!Prot698Parser::validateApdu(apdu.left(n),&error,&consumed),"truncated APDU rejects");}
  checkStrict(referenceFrame(apdu+char(0),address),false,"valid CRC cannot hide APDU trailing data");
  checkStrict(referenceFrame(QByteArray::fromHex("ff0000"),address),false,"unknown APDU rejects");
}
QString writeXml(QTemporaryDir &dir,const QString &name,const QByteArray &contents) {
  const QString path=dir.filePath(name);QFile f(path);expect(f.open(QIODevice::WriteOnly),"temporary fixture opens");
  expect(f.write(contents)==contents.size(),"temporary fixture written");return path;
}
void testAtomicDictionary() {
  QTemporaryDir dir;expect(dir.isValid(),"temporary directory");auto &manager=Prot698OadManager::instance();
  const QByteArray fixture=R"XML(<OIConfig version="same" date="2026-01-01">
<Item OI="A001" Name="old"><Node Method="attrib" ID="2" OP="GET"><SubItem Type="unsigned"/></Node></Item>
<Item OI="A001" Name="winner"><Node Method="attrib" ID="3" OP="GET"><SubItem Type="unsigned"/></Node><Node Method="attrib" ID="3" OP="GET|SET" Name="last"><SubItem Type="ENUM" DefVal="0.off=0;1.on=1"/></Node></Item>
<Item OI="A002" Name="options"><Node Method="attrib" ID="2" OP="GET|SET"><SubItem Type="array"><SubItem Type="option" TypeOption="double_long" Selected="true" Size="4" Scaler="-2" Unit="A"/></SubItem></Node></Item></OIConfig>)XML";
  auto path=writeXml(dir,"first.xml",fixture);expect(manager.loadXml(path),"load synthetic XML");
  const QString id=manager.definitionId();expect(!id.isEmpty(),"content definitionId exists");
  auto object=manager.getObjDef("A001");expect(object&&object->name=="winner","last OI wins whole object");
  expect(manager.getAttrDef("A001",2)==nullptr,"old OI attributes are not merged back");
  auto attr=manager.getAttrDef("A001",3);expect(attr&&attr->name=="last"&&attr->op=="GET|SET","last member wins");
  expect(attr&&attr->dataDef.enumValues.size()==2,"uppercase enum values preserved");
  expect(manager.diagnostics().size()>=2,"duplicate OI and member diagnostics");
  auto options=manager.getAttrDataDef("A002",2);
  expect(options&&options->children.size()==1,"option child retained");
  if(options&&!options->children.isEmpty()) {const auto child=options->children.first();expect(child.typeOption=="double_long"&&child.selected&&child.size=="4"&&child.hasScaler&&child.scaler==-2,"option metadata retained");}
  auto bad=writeXml(dir,"bad.xml","<OIConfig><Item");expect(!manager.loadXml(bad),"malformed reload fails");
  expect(manager.definitionId()==id&&manager.getObjDef("A001")!=nullptr,"failed reload preserves old dictionary and revision");
  const auto oldPath=manager.sourcePath();expect(!manager.loadXml(dir.filePath("missing.xml")),"missing reload fails");
  expect(manager.sourcePath()==oldPath&&manager.definitionId()==id,"missing reload preserves source metadata");
  auto replacement=fixture;replacement.replace("winner","changed");
  auto replacementPath=writeXml(dir,"second.xml",replacement);expect(manager.loadXml(replacementPath),"same-version changed-content reload");
  expect(manager.definitionId()!=id,"definitionId covers content, not version/path alone");
  const auto clean=writeXml(dir,"clean.xml","<OIConfig version=\"same\"><Item OI=\"B001\" Name=\"new\"/></OIConfig>");
  expect(manager.loadXml(clean),"clean replacement load");expect(manager.getObjDef("A001")==nullptr&&manager.getObjDef("B001")!=nullptr,"successful reload replaces table atomically");
  expect(manager.diagnostics().isEmpty(),"diagnostics reflect current load only");
}
void testServiceStatusesAndSchema(const QString &sourceRoot) {
  auto &manager=Prot698OadManager::instance();expect(manager.loadXml(sourceRoot+"/protocol_xml/698_protocol.xml"),"restore real project definitions");
  const QSet<QString> expected={"GET_NORMAL","GET_LIST","GET_RECORD","GET_NEXT","SET_NORMAL","SET_LIST","ACTION_NORMAL","ACTION_LIST","PROXY_GET_LIST","GET_RESPONSE_NORMAL","GET_RESPONSE_LIST","GET_RESPONSE_RECORD","GET_RESPONSE_NEXT","SET_RESPONSE_NORMAL","SET_RESPONSE_LIST","ACTION_RESPONSE_NORMAL","ACTION_RESPONSE_LIST","PROXY_GET_RESPONSE_LIST"};
  QSet<QString> found;
  for(const auto &v:Prot698ServiceSchema::serviceTypes()) {
    const auto service=v.toMap();const auto code=service.value("code").toString();found.insert(code);
    expect(service.contains("buildSupported")&&service.value("buildSupported").metaType().id()==QMetaType::Bool,code+" buildSupported is bool");
    expect(service.contains("unsupportedReasons"),code+" reports unsupported reasons");
    const auto status=service.value("verificationStatus").toString();expect(status=="pending"||status=="local_verified"||status=="independent_verified",code+" has separate evidence status");
    if(!service.value("buildSupported").toBool())expect(!service.value("unsupportedReasons").toList().isEmpty(),code+" disabled reason");
    QTextStream(stdout)<<"SERVICE "<<code<<" buildSupported="<<service.value("buildSupported").toBool()<<" verification="<<status<<" limitations="<<service.value("unsupportedReasons").toList().size()<<'\n';
    QVariantMap normalized;QString error;
    expect(!Prot698ServiceSchema::validateJsonTreeValue(code,QJsonObject{},&normalized,&error),code+" rejects missing tree");
    expect(!error.isEmpty(),code+" missing tree reports error");
  }
  expect(found==expected,"all18 registered services remain discoverable exactly once");
  expect(Prot698ServiceSchema::serviceTypes().size()==18,"no duplicate service rows");
  const QJsonObject oad{{"oi","4000"},{"attrId",2},{"index",0}};
  const QJsonObject piid{{"seqNo",1},{"highPriority",false}};
  const QJsonObject good{{"piid",piid},{"oad",oad}};
  QVariantMap normalized;QString error;
  expect(Prot698ServiceSchema::validateJsonTreeValue("GET_NORMAL",good,&normalized,&error),"existing GET_NORMAL valid tree: "+error);
  auto bad=good;bad["extra"]=1;error.clear();expect(!Prot698ServiceSchema::validateJsonTreeValue("GET_NORMAL",bad,&normalized,&error),"unknown tree key rejects");
  auto missing=oad;missing.remove("attrId");bad=good;bad["oad"]=missing;error.clear();expect(!Prot698ServiceSchema::validateJsonTreeValue("GET_NORMAL",bad,&normalized,&error),"OAD attrId cannot be defaulted");
  bad=good;auto typed=oad;typed["attrId"]=true;bad["oad"]=typed;error.clear();expect(!Prot698ServiceSchema::validateJsonTreeValue("GET_NORMAL",bad,&normalized,&error),"OAD bool member rejects before conversion");
  bad=good;typed=oad;typed["oi"]="FFFF";bad["oad"]=typed;error.clear();expect(!Prot698ServiceSchema::validateJsonTreeValue("GET_NORMAL",bad,&normalized,&error),"unknown object rejects");
  bad=good;typed=oad;typed["index"]=1;bad["oad"]=typed;error.clear();expect(!Prot698ServiceSchema::validateJsonTreeValue("GET_NORMAL",bad,&normalized,&error),"unconfirmed nonzero index is locally unsupported");
  error.clear();expect(!Prot698ServiceSchema::validateJsonTreeValue("NOT_A_SERVICE",good,&normalized,&error),"unknown service rejects");
}
void testReviewBoundaryRegressions(const QString &sourceRoot) {
  auto scaled=leaf("long64");scaled.hasScaler=true;scaled.scaler=-2;scaled.unit="A";
  expect(encoded(scaled,QJsonObject{{"representation","physical"},{"value","-92233720368547758.08"},{"unit","A"}}).toHex()=="148000000000000000","physical signed64 minimum exact");
  scaled.type="long64_unsigned";
  expect(encoded(scaled,QJsonObject{{"representation","physical"},{"value","184467440737095516.15"},{"unit","A"}}).toHex()=="15ffffffffffffffff","physical unsigned64 maximum exact");
  reject(scaled,QJsonObject{{"representation","physical"},{"value","184467440737095516.16"},{"unit","A"}},"physical unsigned64 overflow");
  reject(scaled,QJsonObject{{"representation","physical"},{"value","1.001"},{"unit","A"}},"physical non-integral raw value");
  auto octets=leaf("octet_string");octets.size="1";
  expect(encoded(octets,"001122").toHex()=="0903001122","Size is reference metadata, not fixed encoded byte length");
  auto integer=leaf("unsigned");integer.size="8";expect(encoded(integer,255).toHex()=="11ff","primitive length follows type despite Size hint");
  reject(leaf("tsa"),"0501","TSA descriptor length mismatch");
  expect(encoded(leaf("tsa"),"0001").toHex()=="55020001","TSA descriptor matching one address byte");
  QString error;int consumed=-1;
  expect(!Prot698Parser::validateApdu(QByteArray::fromHex("090101000a010205010005014000020000"),&error,&consumed),"APDU rejects TSA descriptor mismatch with otherwise bounded bytes");
  const QJsonObject uiDate{{"year",2026},{"month",10},{"day",14},{"week",3},{"hour",15},{"minute",0},{"second",0},{"ms",0}};
  expect(encoded(leaf("date_time_s"),uiDate,false).toHex()=="1c07ea0a0e0f0000","manual complete FieldEditor date keys remain accepted");
  reject(leaf("date_time_s"),uiDate,"AI datetime_s rejects manual-only week/ms keys");
  expect(encoded(leaf("date"),uiDate,false).toHex()=="1a07ea0a0e03","manual date ignores unrelated time fields");
  expect(encoded(leaf("time"),uiDate,false).toHex()=="1b0f0000","manual time ignores unrelated date fields");
  const auto address=QByteArray::fromHex("010000000000");
  // Synthetic legacy-service regression inputs, not protocol-certified golden frames.
  const QMap<QString,QByteArray> legacy{
    {"LINK",QByteArray::fromHex("010101012c07ea0a0e030f00000000")},
    {"REPORT",QByteArray::fromHex("880101014000020000000000")},
    {"SECURITY",QByteArray::fromHex("10000805010140000200000101aa")}};
  Prot698Parser parser;
  for(auto i=legacy.cbegin();i!=legacy.cend();++i) {
    const auto frame=referenceFrame(i.value(),address,i.key()=="LINK"?0x81:0xc3);
    error.clear();expect(Prot698Parser::validateSingleFrame(frame,&error,false),i.key()+" complete CRC/layout accepted for log path: "+error);
    const auto parsed=parser.parseWithAddress(frame);
    expect(parsed.isValid&&!parsed.fields.isEmpty(),i.key()+" outside18 still displays as valid log frame");
    error.clear();expect(!Prot698Parser::validateApdu(i.value(),&error,&consumed),i.key()+" unsupported AI APDU remains closed");
    auto corrupt=frame;corrupt[corrupt.size()-3]^=char(1);
    expect(!parser.parseWithAddress(corrupt).isValid,i.key()+" corrupt FCS not marked valid by display parser");
  }
  QTemporaryDir dir;auto &manager=Prot698OadManager::instance();
  const auto path=writeXml(dir,"indexed.xml",R"XML(<OIConfig><Item OI="A011" Name="synthetic"><Node Method="attrib" ID="2" OP="GET|SET"><SubItem Type="structure"><SubItem Type="unsigned"/><SubItem Type="Data"/></SubItem></Node></Item></OIConfig>)XML");
  expect(manager.loadXml(path),"indexed service fixture");
  const QJsonObject piid{{"seqNo",1},{"highPriority",false},{"acd",false}};
  const QJsonObject indexed{{"oi","A011"},{"attrId",2},{"index",1}};
  QJsonObject tree{{"piid",piid},{"setItem",QJsonObject{{"oad",indexed},{"dataValue",7}}}};QVariantMap normalized;
  error.clear();expect(Prot698ServiceSchema::validateJsonTreeValue("SET_NORMAL",tree,&normalized,&error,false),"manual valid indexed structure field remains accepted: "+error);
  error.clear();expect(!Prot698ServiceSchema::validateJsonTreeValue("SET_NORMAL",tree,&normalized,&error,true),"AI nonzero index stays unsupported");
  auto aggregate=QJsonArray{};
  for(int i=0;i<200;++i)aggregate.append(QJsonObject{{"type","unsigned"},{"value",i}});
  QJsonArray items;
  for(int i=0;i<30;++i)items.append(QJsonObject{{"oad",QJsonObject{{"oi","A011"},{"attrId",2},{"index",0}}},
    {"dataValue",QJsonArray{1,QJsonObject{{"type","array"},{"value",aggregate}}}}});
  error.clear();expect(!Prot698ServiceSchema::validateJsonTreeValue("SET_LIST",QJsonObject{{"piid",piid},{"setItems",items}},&normalized,&error),"aggregate service/Data total node budget cannot reset per value");
  QJsonObject rsd{{"type",0}};for(int i=0;i<20;++i)rsd=QJsonObject{{"type",11},{"items",QJsonArray{rsd}}};
  error.clear();expect(!Prot698ServiceSchema::validateJsonTreeValue("GET_RECORD",QJsonObject{{"piid",piid},{"oad",QJsonObject{{"oi","A011"},{"attrId",2},{"index",0}}},{"rsd",rsd},{"rcsd",QJsonObject{{"items",QJsonArray{}}}}},&normalized,&error),"service/RSD global depth bounded");
  error.clear();auto acd=piid;acd["acd"]=true;
  expect(!Prot698ServiceSchema::validateJsonTreeValue("GET_NORMAL",QJsonObject{{"piid",acd},{"oad",QJsonObject{{"oi","A011"},{"attrId",2},{"index",0}}}},&normalized,&error),"request cannot set response ACD bit");
  QJsonValue deepData=QJsonObject{{"type","unsigned"},{"value",1}};
  for(int i=0;i<15;++i)deepData=QJsonObject{{"type","array"},{"value",QJsonArray{deepData}}};
  QJsonObject mixedRsd{{"type",1},{"selector",QJsonObject{{"oad",QJsonObject{{"oi","A011"},{"attrId",2},{"index",0}}},{"value",QJsonArray{1,deepData}}}}};
  for(int i=0;i<8;++i)mixedRsd=QJsonObject{{"type",11},{"items",QJsonArray{mixedRsd}}};
  error.clear();expect(!Prot698ServiceSchema::validateJsonTreeValue("GET_RECORD",QJsonObject{{"piid",piid},{"oad",QJsonObject{{"oi","A011"},{"attrId",2},{"index",0}}},{"rsd",mixedRsd},{"rcsd",QJsonObject{{"items",QJsonArray{}}}}},&normalized,&error),"shared depth includes individually bounded RSD and Data together");
  error.clear();expect(Prot698DataEncoder::encodeDateTimeS(QVariantMap{},&error).isEmpty()&&!error.isEmpty(),"direct datetime helper cannot bypass missing fields");
  error.clear();expect(Prot698DataEncoder::encodeOAD("1",2,0,&error).isEmpty()&&!error.isEmpty(),"OI helper needs exactly four hex digits");
  expect(Prot698DataEncoder::encodePIID(64).isEmpty(),"direct PIID helper rejects range overflow");
  expect(manager.loadXml(sourceRoot+"/protocol_xml/698_protocol.xml"),"restore dictionary after review regressions");
}
void testSyntheticAllServices(const QString &sourceRoot) {
  QTemporaryDir dir;auto &manager=Prot698OadManager::instance();
  const auto path=writeXml(dir,"services.xml",R"XML(<OIConfig version="synthetic"><Item OI="A010" Name="synthetic test only"><Node Method="attrib" ID="2" OP="GET|SET"><SubItem Type="unsigned"/></Node><Node Method="attrib" ID="3" OP="GET"><SubItem Type="unsigned"/></Node><Node Method="method" ID="1" OP="ACTION"><SubItem Type="integer"/></Node></Item></OIConfig>)XML");
  expect(manager.loadXml(path),"synthetic service dictionary load");
  const QJsonObject oad{{"oi","A010"},{"attrId",2},{"index",0}};
  const QJsonObject omd{{"oi","A010"},{"methodId",1},{"mode",0}};
  const QJsonObject piid{{"seqNo",1},{"highPriority",false}};
  const QJsonObject set{{"oad",oad},{"dataValue",1}};
  const QJsonObject action{{"omd",omd},{"dataValue",1}};
  const QJsonObject result{{"oad",oad},{"choice",0},{"dar",0}};
  const QJsonObject rcsd{{"items",QJsonArray{}}};
  const QJsonObject record{{"oad",oad},{"rcsd",rcsd},{"choice",0},{"dar",0}};
  const QJsonObject setResult{{"oad",oad},{"dar",0}};
  const QJsonObject actionResult{{"omd",omd},{"dar",0},{"hasData",false}};
  const QJsonObject requestServer{{"tsa",QJsonObject{{"address","000000000001"},{"addressType",0},{"logicAddress",0}}},{"timeout",5},{"oads",QJsonArray{oad}}};
  const QJsonObject responseServer{{"tsa",QJsonObject{{"address","000000000001"},{"addressType",0},{"logicAddress",0}}},{"results",QJsonArray{result}}};
  const QMap<QString,QJsonObject> inputs{
    {"GET_NORMAL",{{"oad",oad}}},{"GET_LIST",{{"oads",QJsonArray{oad}}}},
    {"GET_RECORD",{{"oad",oad},{"rsd",QJsonObject{{"type",0}}},{"rcsd",rcsd}}},
    {"GET_NEXT",{{"lastBlockNo",0}}},{"SET_NORMAL",{{"setItem",set}}},
    {"SET_LIST",{{"setItems",QJsonArray{set}}}},{"ACTION_NORMAL",{{"actionItem",action}}},
    {"ACTION_LIST",{{"actionItems",QJsonArray{action}}}},
    {"PROXY_GET_LIST",{{"totalTimeout",10},{"servers",QJsonArray{requestServer}}}},
    {"GET_RESPONSE_NORMAL",{{"result",result}}},{"GET_RESPONSE_LIST",{{"results",QJsonArray{result}}}},
    {"GET_RESPONSE_RECORD",{{"recordResult",record}}},
    {"GET_RESPONSE_NEXT",{{"nextResult",QJsonObject{{"end",true},{"blockNo",0},{"choice",0},{"dar",0}}}}},
    {"SET_RESPONSE_NORMAL",{{"setResult",setResult}}},{"SET_RESPONSE_LIST",{{"setResults",QJsonArray{setResult}}}},
    {"ACTION_RESPONSE_NORMAL",{{"actionResult",actionResult}}},{"ACTION_RESPONSE_LIST",{{"actionResults",QJsonArray{actionResult}}}},
    {"PROXY_GET_RESPONSE_LIST",{{"servers",QJsonArray{responseServer}}}}};
  expect(inputs.size()==18,"synthetic cases cover every registered service");
  for(auto i=inputs.cbegin();i!=inputs.cend();++i) {
    auto tree=i.value();tree["piid"]=piid;QVariantMap normalized;QString error;
    const bool valid=Prot698ServiceSchema::validateJsonTreeValue(i.key(),tree,&normalized,&error);
    expect(valid,i.key()+" synthetic legal input: "+error);
    if(!valid)continue;
    const auto spec=Prot698ServiceSchema::toServiceSpec(i.key(),normalized,&error);
    const auto apdu=Prot698ServiceBuilder::buildFromSpec(spec,&error);
    expect(!apdu.isEmpty()&&error.isEmpty(),i.key()+" synthetic deterministic build: "+error);
    if(!apdu.isEmpty()) {int consumed=-1;error.clear();expect(Prot698Parser::validateApdu(apdu,&error,&consumed)&&consumed==apdu.size(),i.key()+" full bounded APDU consumption: "+error);}
  }
  QVariantMap normalized;QString error;
  auto readonly=oad;readonly["attrId"]=3;
  expect(!Prot698ServiceSchema::validateJsonTreeValue("SET_NORMAL",QJsonObject{{"piid",piid},{"setItem",QJsonObject{{"oad",readonly},{"dataValue",1}}}},&normalized,&error),"SET rejects GET-only attribute");
  auto rawRecord=record;rawRecord["choice"]=1;rawRecord.remove("dar");rawRecord["rowCount"]=1;rawRecord["rowsHex"]="1101";error.clear();
  expect(!Prot698ServiceSchema::validateJsonTreeValue("GET_RESPONSE_RECORD",QJsonObject{{"piid",piid},{"recordResult",rawRecord}},&normalized,&error),"AI record rowsHex bypass rejects");
  auto returned=actionResult;returned["hasData"]=true;returned["dataValue"]=QJsonObject{{"type","unsigned"},{"value",255}};error.clear();
  expect(Prot698ServiceSchema::validateJsonTreeValue("ACTION_RESPONSE_NORMAL",QJsonObject{{"piid",piid},{"actionResult",returned}},&normalized,&error),"ACTION return Data uses explicit type rather than integer input definition: "+error);
  returned["hasData"]=false;error.clear();
  expect(!Prot698ServiceSchema::validateJsonTreeValue("ACTION_RESPONSE_NORMAL",QJsonObject{{"piid",piid},{"actionResult",returned}},&normalized,&error),"hasData=false rejects supplied dataValue");
  expect(manager.loadXml(sourceRoot+"/protocol_xml/698_protocol.xml"),"restore project dictionary after synthetic cases");
}
} // namespace
int main(int argc,char **argv) {
  QCoreApplication app(argc,argv);
  const QString sourceRoot=argc>1?QString::fromLocal8Bit(argv[1]):QStringLiteral(PROT698_TEST_SOURCE_DIR);
  testPrimitiveBoundaries();testIndependentFloatAndResponseVectors();testDatesStringsAndContainers();testDynamicDataAndOptions();testFrameBoundaries();testAtomicDictionary();testServiceStatusesAndSchema(sourceRoot);testSyntheticAllServices(sourceRoot);testReviewBoundaryRegressions(sourceRoot);
  QTextStream(stdout)<<"prot698 boundary checks: "<<checks<<", failures: "<<failures<<'\n';return failures?1:0;
}
