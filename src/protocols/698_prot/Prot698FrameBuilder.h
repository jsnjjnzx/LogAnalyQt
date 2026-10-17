#ifndef PROT698FRAMEBUILDER_H
#define PROT698FRAMEBUILDER_H

#include <QByteArray>
#include <QString>
#include <QVariantMap>

struct FrameOptions {
  bool dir = false;
  bool prm = true;
  bool split = false;
  bool scramble = false;
  int funcCode = 3;
  int saType = 0;
  int logicAddress = 0;
  int ca = 0;
  QByteArray serverAddr;
  QString validationError;
};

class Prot698FrameBuilder {
public:
  static FrameOptions defaultClientRequestOptions(
      const QString &physicalAddressHex = QString());
  static FrameOptions optionsFromMap(const QVariantMap &map,
                                     QString *err = nullptr);
  static QByteArray buildFrame(const QByteArray &apdu, const FrameOptions &opt,
                               QString *err = nullptr);

private:
  static QByteArray physicalAddressToSaBytes(const QString &physicalAddressHex);
};

#endif // PROT698FRAMEBUILDER_H
