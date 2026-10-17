#ifndef CSGUPSTREAMCONFIGMANAGER_H
#define CSGUPSTREAMCONFIGMANAGER_H

#include "CsgUpstreamDef.h"

class CsgUpstreamConfigManager {
public:
  static CsgUpstreamConfigManager &instance();

  bool loadXml(const QString &filePath, QString *errorMessage = nullptr);
  bool validateXml(const QString &filePath,
                   QString *errorMessage = nullptr) const;

  const CsgUpstreamAfnDef *getAfnDef(quint8 afn) const;
  const CsgUpstreamDiDef *getDiDef(const QString &key) const;
  QString getDiName(const QString &key) const;
  QString getVersion() const;
  QString getDate() const;
  bool isLoaded() const;

private:
  CsgUpstreamConfigManager() = default;

  bool parseXml(const QString &filePath,
                QMap<int, CsgUpstreamAfnDef> *afns,
                QMap<QString, CsgUpstreamDiDef> *dataItems,
                QString *version, QString *date,
                QString *errorMessage) const;

  QMap<int, CsgUpstreamAfnDef> m_afns;
  QMap<QString, CsgUpstreamDiDef> m_dataItems;
  QString m_version;
  QString m_date;
  bool m_loaded = false;
};

#endif // CSGUPSTREAMCONFIGMANAGER_H
