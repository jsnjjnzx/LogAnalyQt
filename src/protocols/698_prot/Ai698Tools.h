#ifndef AI698TOOLS_H
#define AI698TOOLS_H

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

struct AiFrameContext {
  QString requestId;
  QJsonObject terminal;
  QDateTime submittedAt;
  int piidSequence = -1;
  QString definitionId;
};

class Ai698Tools {
public:
  QJsonArray definitions() const;
  QJsonObject invoke(const QString &name, const QJsonObject &arguments,
                     const AiFrameContext &context) const;
};

#endif // AI698TOOLS_H
