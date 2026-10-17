#ifndef PROT698OADMANAGER_H
#define PROT698OADMANAGER_H

#include <QDomDocument>
#include <QList>
#include <QMap>
#include <QString>
#include <QVariantList>

struct Prot698SubItem {
  QString name;
  QString type;
  QString unit;
  int scaler = 0; // 10^scaler
  bool hasScaler = false;
  QString size;
  QString typeOption;
  bool selected = false;
  QString remark;
  QString format;
  QString defVal;
  QMap<int, QString> enumValues;
  QList<Prot698SubItem> children;
};

struct Prot698AttrDef {
  QString name;
  QString op;
  Prot698SubItem dataDef;
};

struct Prot698ObjDef {
  QString name;
  QMap<int, Prot698AttrDef> attributes; // ID -> Def
  QMap<int, Prot698AttrDef> methods;    // ID -> Def
};

class Prot698OadManager {
public:
  static Prot698OadManager &instance();
  bool loadXml(const QString &filePath);

  Prot698ObjDef *getObjDef(const QString &oi);
  Prot698AttrDef *getAttrDef(const QString &oi, int attrId);
  Prot698AttrDef *getMethodDef(const QString &oi, int methodId);
  Prot698SubItem *getAttrDataDef(const QString &oi, int attrId,
                                 int dataIndex = 0);
  Prot698SubItem *getMethodDataDef(const QString &oi, int methodId);
  QString getVersion() const { return m_version; }
  QString getDate() const { return m_date; }
  QString definitionId() const { return m_definitionId; }
  QString sourcePath() const { return m_sourcePath; }
  QVariantList diagnostics() const { return m_diagnostics; }
  QVariantList listAllOi(const QString &filterText = QString()) const;

private:
  Prot698OadManager();
  void parseSubItem(const QDomElement &elem, Prot698SubItem &item);
  void parseEnumValues(const QString &defVal, QMap<int, QString> &values);
  static Prot698SubItem *resolveIndexedDataDef(Prot698SubItem *root,
                                               int dataIndex);

  QMap<QString, Prot698ObjDef> m_objects; // OI -> Def
  QString m_version;
  QString m_date;
  QString m_definitionId;
  QString m_sourcePath;
  QVariantList m_diagnostics;
};

#endif // PROT698OADMANAGER_H
