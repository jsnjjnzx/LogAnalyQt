#ifndef LOGBACKEND_H
#define LOGBACKEND_H

#include "3762_prot/Prot3762Parser.h"
#include "698_prot/Prot698Parser.h"
#include "ai/AiFramingController.h"
#include "PresetManager.h"
#include "ProtocolSyncManager.h"
#include "ProtocolDetector.h"
#include "app_msg/AppMsgDef.h"
#include "iec_101/Prot101Parser.h"
#include "iec_104/Prot104Parser.h"
#include "module_msg/ModuleMsgParser.h"
#include <QAbstractListModel>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QHash>
#include <QFile>
#include <QObject>
#include <QStringList>
#include <QTextStream>

class QTimer;

struct ParseResultItem {
  QString name;
  QString value;
  QString description;
  int indent = 0;
  int offset = -1;
  int length = -1;
  bool highlighted = false;
  bool isDiff = false;
  bool expanded = true;
  bool hasChildren = false;
  bool isFrameSeparator = false; // 帧分隔符标识
};

// 全局预览概要信息结构
struct GlobalPreviewItem {
  QString timestamp;   // 时间
  QString protocol;    // 规约
  QString address;     // 地址
  QString direction;   // 方向位
  QString frameSeq;    // 帧序号
  QString messageType; // 报文类型
  int lineNumber = -1; // 行号（用于定位）
  QString rawData;     // 原始报文数据
};

class ParseResultModel : public QAbstractListModel {
  Q_OBJECT
public:
  enum Roles {
    NameRole = Qt::UserRole + 1,
    ValueRole,
    DescriptionRole,
    IndentRole,
    OffsetRole,
    LengthRole,
    HighlightedRole,
    IsDiffRole,
    ExpandedRole,
    HasChildrenRole,
    SubtreeExpandedRole,
    SubtreeStateRole,
    FrameSeparatorRole // 帧分隔符角色
  };

  explicit ParseResultModel(QObject *parent = nullptr);
  ~ParseResultModel() override;
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex &index, const QVariant &value,
               int role = Qt::EditRole) override;
  QHash<int, QByteArray> roleNames() const override;

  void clear();
  void addItem(const ParseResultItem &item);
  void setHighlight(int start, int length);

  Q_INVOKABLE void toggleExpand(int row);
  Q_INVOKABLE void toggleSubtree(int row);
  Q_INVOKABLE QList<int> getColumnWidths(double fontWidth, int indentWidth);

private:
  void rebuildVisibleItems();

  QList<ParseResultItem *> m_allItems;
  QList<ParseResultItem *> m_visibleItems;
};

// 全局预览数据模型
class GlobalPreviewModel : public QAbstractListModel {
  Q_OBJECT
public:
  enum Roles {
    TimestampRole = Qt::UserRole + 1,
    ProtocolRole,
    AddressRole,
    DirectionRole,
    FrameSeqRole,
    MessageTypeRole,
    LineNumberRole,
    RawDataRole
  };

  explicit GlobalPreviewModel(QObject *parent = nullptr);
  ~GlobalPreviewModel() override;
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  void clear();
  void addItem(const GlobalPreviewItem &item);

private:
  QList<GlobalPreviewItem> m_items;
};

class LogBackend : public QObject {
  Q_OBJECT
  Q_PROPERTY(QStringList fileList READ fileList NOTIFY fileListChanged)
  Q_PROPERTY(QStringList fileDisplayList READ fileDisplayList NOTIFY fileListChanged)
  Q_PROPERTY(QString currentFileContent READ currentFileContent NOTIFY
                 currentFileContentChanged)
  Q_PROPERTY(QString currentFilePath READ currentFilePath WRITE
                 setCurrentFilePath NOTIFY currentFilePathChanged)
  Q_PROPERTY(QString parseResult READ parseResult NOTIFY parseResultChanged)
  Q_PROPERTY(ParseResultModel *resultModel READ resultModel CONSTANT)
  Q_PROPERTY(ParseResultModel *resultModel2 READ resultModel2 CONSTANT)
  Q_PROPERTY(ParseResultModel *popupResultModel READ popupResultModel CONSTANT)
  Q_PROPERTY(
      GlobalPreviewModel *globalPreviewModel READ globalPreviewModel CONSTANT)
  Q_PROPERTY(ProtocolSyncManager *syncManager READ syncManager CONSTANT)
  Q_PROPERTY(ProtocolSyncManager *promptSyncManager READ promptSyncManager CONSTANT)
  Q_PROPERTY(AiFramingController *aiFraming READ aiFraming CONSTANT)
  Q_PROPERTY(int protocolType READ protocolType WRITE setProtocolType NOTIFY
                 protocolTypeChanged)
  Q_PROPERTY(QString currentProtocolName READ currentProtocolName NOTIFY
                 currentProtocolNameChanged)
  Q_PROPERTY(QString appVersion READ appVersion NOTIFY appVersionChanged)
  Q_PROPERTY(int totalLines READ totalLines NOTIFY scanProgressChanged)
  Q_PROPERTY(int scannedLines READ scannedLines NOTIFY scanProgressChanged)
  Q_PROPERTY(double scanProgress READ scanProgress NOTIFY scanProgressChanged)
  Q_PROPERTY(int unknownLines READ unknownLines NOTIFY scanProgressChanged)
  Q_PROPERTY(bool securityControlActive READ securityControlActive NOTIFY
                 securityControlStateChanged)
  Q_PROPERTY(QString securityControlMessage READ securityControlMessage NOTIFY
                 securityControlStateChanged)
  Q_PROPERTY(int securityControlRemainingSeconds READ
                 securityControlRemainingSeconds NOTIFY
                     securityControlCountdownChanged)

public:
  explicit LogBackend(QObject *parent = nullptr);

  QStringList fileList() const;
  QStringList fileDisplayList() const;
  QString currentFileContent() const;
  QString currentFilePath() const;
  QString parseResult() const;
  ParseResultModel *resultModel() const;
  ParseResultModel *resultModel2() const;
  ParseResultModel *popupResultModel() const;
  GlobalPreviewModel *globalPreviewModel() const;
  ProtocolSyncManager *syncManager() const;
  ProtocolSyncManager *promptSyncManager() const { return m_promptSyncManager; }
  AiFramingController *aiFraming() const { return m_aiFraming; }
  int protocolType() const;
  QString currentProtocolName() const;
  QString appVersion() const;
  int totalLines() const;
  int scannedLines() const;
  double scanProgress() const;
  int unknownLines() const;
  bool securityControlActive() const;
  QString securityControlMessage() const;
  int securityControlRemainingSeconds() const;

  Q_INVOKABLE void openFolder(const QString &path);
  Q_INVOKABLE void loadFile(const QString &fileName,
                            const QString &encoding = "auto");
  Q_INVOKABLE void loadRawText(const QString &text);
  Q_INVOKABLE void applyFilter(const QString &regex);
  Q_INVOKABLE QVariantMap testMonitorExpression(const QString &pattern, bool ignoreCase, const QString &sample) const;
  Q_INVOKABLE void parseHex(const QString &text, const QString &encoding,
                            int targetModel = 1, int protocolOverride = -1);
  Q_INVOKABLE int createParseSession();
  Q_INVOKABLE void closeParseSession(int modelId);
  Q_INVOKABLE ParseResultModel *parseModel(int modelId) const;
  Q_INVOKABLE void parsePopupApdu(const QString &apduHex);
  Q_INVOKABLE void compareModels();
  Q_INVOKABLE void clearDiffs();
  Q_INVOKABLE void clearAllParses();
  Q_INVOKABLE int findNextDiff(int currentIndex);
  Q_INVOKABLE int findPrevDiff(int currentIndex);
  Q_INVOKABLE void handleLink(const QString &link, int modelId = 1);
  Q_INVOKABLE QString getReadmeContent();
  Q_INVOKABLE QString getChangelogContent();
  Q_INVOKABLE QString getAppVersion();
  Q_INVOKABLE QString replace698Address(const QString &hexStr,
                                        const QString &newAddress,
                                        const QString &newLogicAddress);
  Q_INVOKABLE QString formatHexString(const QString &hexStr);
  Q_INVOKABLE void copyToClipboard(const QString &text);
  Q_INVOKABLE void scanGlobalPreview();
  Q_INVOKABLE QList<int> getLineRange(int lineNumber);
  Q_INVOKABLE QVariant getUiSetting(const QString &key,
                                    const QVariant &defaultValue = QVariant());
  Q_INVOKABLE bool setUiSetting(const QString &key, const QVariant &value);

  // 禁用 QML TextArea 的 QTextDocument undo 历史（防止长时间运行内存泄漏）
  Q_INVOKABLE void disableTextAreaUndo(QObject *quickTextDocument);
  Q_INVOKABLE void styleLogDocument(QObject *quickTextDocument);

  // 预置报文管理相关方法
  Q_INVOKABLE QStringList getPresetList(const QString &protocol);
  Q_INVOKABLE QString getPresetContent(const QString &protocol,
                                       const QString &summary);
  Q_INVOKABLE bool addPresetMessage(const QString &protocol,
                                    const QString &summary,
                                    const QString &content);
  Q_INVOKABLE bool removePresetMessage(const QString &protocol,
                                       const QString &summary);
  Q_INVOKABLE bool updatePresetMessage(const QString &protocol,
                                       const QString &oldSummary,
                                       const QString &newSummary,
                                       const QString &newContent);

  // 698 智能组帧
  Q_INVOKABLE QVariantList listSmartFramingProtocols();
  Q_INVOKABLE QVariantList list698ServiceTypes();
  Q_INVOKABLE QVariantMap get698ServiceSchema(const QString &serviceCode);
  Q_INVOKABLE QVariantMap getOadSchema(const QString &oiHex, int attrId,
                                       bool isMethod = false,
                                       int dataIndex = 0);
  Q_INVOKABLE QVariantList listAllOad(const QString &filterText = QString());
  Q_INVOKABLE QString build698ApduFromTree(const QString &serviceCode,
                                           const QVariantMap &treeValue);
  Q_INVOKABLE QString buildFullFrame(const QString &apduHex,
                                     const QVariantMap &frameOptions = QVariantMap());
  Q_INVOKABLE QVariantList verifyHexData(const QString &hexText);
  Q_INVOKABLE QString getCurrent698Address() const;
  Q_INVOKABLE int getNextPiid();

  // APP 智能组帧
  Q_INVOKABLE QVariantList listAppInterfaceGroups();
  Q_INVOKABLE QVariantList listAppInterfaces(int iid, const QString &kind,
                                             const QString &filterText = QString());
  Q_INVOKABLE QVariantMap getAppInterfaceSchema(int iid, int iop,
                                                const QString &kind);
  Q_INVOKABLE QString buildAppPayloadFromTree(int iid, int iop,
                                              const QString &kind,
                                              const QVariant &treeValue);
  Q_INVOKABLE QString buildAppMessageFromTree(int iid, int iop,
                                              const QString &kind,
                                              const QVariant &treeValue,
                                              const QVariantMap &headerOptions);
  Q_INVOKABLE QVariantList verifySmartFrame(const QString &protocolCode,
                                            const QString &hexText);

  Q_INVOKABLE QStringList listFramingTemplates();
  Q_INVOKABLE QString loadFramingTemplate(const QString &name);
  Q_INVOKABLE bool saveFramingTemplate(const QString &name,
                                       const QString &json);
  Q_INVOKABLE bool deleteFramingTemplate(const QString &name);

public slots:
  void setCurrentFilePath(const QString &path);
  void setProtocolType(int type);

signals:
  void fileListChanged();
  void currentFileContentChanged();
  void currentFilePathChanged();
  void parseResultChanged();
  void highlightRange(int start, int end, int modelId);
  void protocolTypeChanged();
  void currentProtocolNameChanged();
  void appVersionChanged();
  void statusMessage(QString message, int timeout);
  void scanProgressChanged();
  void update698Address(QString physicalAddress,
                        QString logicAddress); // 698地址自动填充信号
  void securityControlStateChanged();
  void securityControlCountdownChanged();

private:
  enum class SecurityControlTrigger {
    None,
    NoRecord,
    Expired,
    CopyMissing,
    StateCorrupted,
    StateMismatch,
    TimeRollback,
    StateWriteFailed,
    UnsupportedPlatform,
  };

  void calculateHighlight(int byteOffset, int byteLength);
  void initializeSecurityControl();
  void startSecurityControlEnforcement(SecurityControlTrigger trigger,
                                       const QDateTime &lastVerifiedTime,
                                       const QString &detail = QString());
  void clearSecurityControlEnforcement();
  void attemptSecurityControlVerification();
  void recordSuccessfulSecurityVerification();
  bool isSecurityVerificationExpired(const QDateTime &lastVerifiedTime) const;
  QDateTime loadLegacySecurityVerificationTime() const;
  void clearLegacySecurityVerificationTime();
  QString buildSecurityControlMessage(SecurityControlTrigger trigger,
                                      const QDateTime &lastVerifiedTime,
                                      const QString &detail) const;
  void loadSettings();
  void saveSettings();
  uint16_t calc698CRC16(const uint8_t *data, int len);

  // 辅助函数：解析单个帧
  bool parseSingleFrame(const QByteArray &data, ProtocolType protocolType,
                        ParseResultModel *target, int targetModel,
                        int offsetAdjustment = 0);

  QStringList m_fileList;
  QStringList m_fileDisplayList;
  QString m_currentFileContent;
  QString m_originalContent;
  QString m_currentFilePath;
  QString m_currentFolderPath;
  QString m_parseResult;
  QHash<int, QString> m_parsedTexts;
  QHash<int, ParseResultModel *> m_sessionModels;
  int m_nextSessionModelId = 4;

  ParseResultModel *m_resultModel;
  ParseResultModel *m_resultModel2;
  ParseResultModel *m_popupResultModel;
  GlobalPreviewModel *m_globalPreviewModel;
  ProtocolSyncManager *m_protocolSyncManager;
  ProtocolSyncManager *m_promptSyncManager;

  int m_protocolType;                  // 当前选择的协议类型
  ProtocolType m_lastDetectedProtocol; // 最后一次自动检测的协议类型
  mutable QString m_appVersion;        // 应用版本号缓存

  int m_totalLines = 0;
  int m_scannedLines = 0;
  double m_scanProgress = 0.0;
  int m_unknownLines = 0;
  bool m_securityControlActive = false;
  QString m_securityControlMessage;
  SecurityControlTrigger m_securityControlTrigger =
      SecurityControlTrigger::None;
  int m_securityControlRemainingSeconds = 0;
  bool m_securityVerificationPending = false;
  bool m_securityStatusProbePending = false;
  QTimer *m_securityCloseTimer = nullptr;
  QTimer *m_securityCountdownTimer = nullptr;
  int m_securityTamperCountSeed = 0;
  QDateTime m_securityMinimumAllowedUtc;
  QString m_securityInstallIdHint;

  QString m_current698PhysicalAddr;
  int m_piidCounter = 0;
  AiFramingController *m_aiFraming = nullptr;

  PresetManager *m_presetManager; // 预置报文管理器
};

#endif // LOGBACKEND_H
