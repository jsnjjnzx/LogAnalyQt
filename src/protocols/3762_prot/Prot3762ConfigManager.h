#ifndef PROT3762CONFIGMANAGER_H
#define PROT3762CONFIGMANAGER_H

#include "Prot3762Def.h"
#include <QMap>
#include <QObject>


class Prot3762ConfigManager {
public:
  static Prot3762ConfigManager &instance();

  bool loadXml(const QString &filePath);
  Prot3762AfnDef *getAfnDef(int afn);
  Prot3762FnDef *getFnDef(int afn, int fn, const QString &direction = "");
  QString getVersion() const { return m_version; }
  QString getDate() const { return m_date; }

private:
  Prot3762ConfigManager();
  QMap<int, Prot3762AfnDef> m_afns;
  QString m_version;
  QString m_date;
};

#endif // PROT3762CONFIGMANAGER_H
