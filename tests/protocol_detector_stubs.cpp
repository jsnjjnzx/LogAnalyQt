#include "3762_prot/Prot3762Parser.h"
#include "645_prot/Prot645Parser.h"
#include "698_prot/Prot698Parser.h"
#include "app_msg/AppMsgParser.h"
#include "iec_101/Prot101Parser.h"
#include "iec_104/Prot104Parser.h"
#include "module_msg/ModuleMsgParser.h"

bool Prot101Parser::isValidFrame(const QByteArray &) {
  return false;
}

bool Prot698Parser::isValidFrame(const QByteArray &) {
  return false;
}

bool ModuleMsg::ModuleMsgParser::isValidFrame(const QByteArray &) {
  return false;
}

bool Prot3762Parser::isValidFrame(const QByteArray &) {
  return false;
}

bool Prot645Parser::isValidFrame(const QByteArray &) {
  return false;
}

bool Prot104Parser::isValidFrame(const QByteArray &) {
  return false;
}

bool AppMsg::AppMsgParser::isValidFrame(const QByteArray &) {
  return false;
}

int AppMsg::AppMsgParser::getFullFrameSize(const QByteArray &) {
  return 0;
}
