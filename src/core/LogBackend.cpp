#include "LogBackend.h"
#include "SecurityStateStore.h"
#include "../common/AppPaths.h"
#include "698_prot/Prot698DataEncoder.h"
#include "698_prot/Prot698FrameBuilder.h"
#include "698_prot/Prot698ServiceBuilder.h"
#include "698_prot/Prot698ServiceSchema.h"
#include "app_msg/AppMsgBuilder.h"
#include "app_msg/AppMsgServiceSchema.h"
#include <QQuickTextDocument>
#include <QTextDocument>
#include "3762_prot/Prot3762ConfigManager.h"
#include "645_prot/Prot645ConfigManager.h"
#include "645_prot/Prot645Parser.h"
#include "csg_upstream/CsgUpstreamConfigManager.h"
#include "csg_upstream/CsgUpstreamParser.h"
#include "698_prot/Prot698OadManager.h"
#include "app_msg/AppMsgLoader.h"
#include "app_msg/AppMsgParser.h"
#include "module_msg/ModuleMsgLoader.h"
#include "module_msg/ModuleMsgParser.h"
#include <QCollator>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include "SshMonitorMatcher.h"
#include <QTimer>
#include "../common/AppSettings.h"
#include "LogTextHighlighter.h"
#include <QStringDecoder>
#include <QQuickTextDocument>
#include <QUrl>
#include <functional>

#define SECURITY_CONTROL_REQUIRED_DAYS 15
#define SECURITY_CONTROL_FORCE_EXIT_SECONDS 60

static constexpr bool kSecurityControlEnabled = false;

void LogBackend::styleLogDocument(QObject *quickTextDocument) {
  auto *quick = qobject_cast<QQuickTextDocument *>(quickTextDocument);
  if (!quick || !quick->textDocument()) return;
  auto *document = quick->textDocument();
  auto *existing = document->findChild<QSyntaxHighlighter *>("logSyntaxHighlight");
  if (!existing) new LogTextHighlighter(document);
}

static constexpr const char *kLegacySecurityLastVerifiedKey =
  "security/lastVerifiedProtocolSyncAt";

static QString resolveProtocolPath(const QString &fileName) {
  // 1. 优先检查可写数据目录下的 protocol_xml 目录
  QString path = QDir(AppPaths::protocolDirectory()).filePath(fileName);
  if (QFile::exists(path))
    return path;

  // 2. 检查程序同级目录
  path = QCoreApplication::applicationDirPath() + "/" + fileName;
  if (QFile::exists(path))
    return path;

  // 3. 检查当前工作目录（开发环境常见）
  if (QFile::exists(fileName))
    return fileName;
  path = "protocol_xml/" + fileName;
  if (QFile::exists(path))
    return path;

  // 4. 最后回退到 Qt 资源系统
  return ":/protocol_xml/" + fileName;
}

static void ensureProtocolsExtracted() {
  QString targetDir = AppPaths::protocolDirectory() + "/";
  QDir dir;
  if (!dir.exists(targetDir)) {
    if (!dir.mkpath(targetDir)) {
      qDebug() << "Failed to create protocol_xml directory";
      return;
    }
  }

  QStringList files = {"app_msg_protocol.xml", "module_msg_protocol.xml",
                       "3762_protocol.xml", "645_protocol.xml",
                       "698_protocol.xml", "csg_upstream_protocol.xml"};

  for (const QString &fileName : files) {
    QString targetPath = targetDir + fileName;
    if (!QFile::exists(targetPath)) {
      QString resPath = ":/protocol_xml/" + fileName;
      if (QFile::exists(resPath)) {
        if (QFile::copy(resPath, targetPath)) {
          // 移除只读属性（如果资源文件被标记为只读，拷贝后可能也是只读）
          QFile::setPermissions(
              targetPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                              QFileDevice::ReadGroup | QFileDevice::ReadOther);
          qDebug() << "Extracted" << fileName << "to" << targetPath;
        }
      }
    }
  }
}

// --- ParseResultModel Implementation ---

ParseResultModel::ParseResultModel(QObject *parent)
    : QAbstractListModel(parent) {}

ParseResultModel::~ParseResultModel() { qDeleteAll(m_allItems); }

int ParseResultModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return m_visibleItems.count();
}

QVariant ParseResultModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= m_visibleItems.count())
    return QVariant();
  const auto *item = m_visibleItems[index.row()];
  switch (role) {
  case NameRole:
    return item->name;
  case ValueRole:
    return item->value;
  case DescriptionRole:
    return item->description;
  case DescriptionSummaryRole:
    return item->summary.isEmpty() ? item->description : item->summary;
  case DescriptionDetailRole:
    return item->detail;
  case HasParentRole:
    return m_parentRows[item->sourceRow] >= 0;
  case BranchContinuesRole:
    return m_nextSiblingRows[item->sourceRow] >= 0;
  case TreeGuidesRole: {
    QVariantList guides;
    for (int parent = m_parentRows[item->sourceRow]; parent >= 0;
         parent = m_parentRows[parent]) {
      if (m_allItems[parent]->indent > 0 && m_nextSiblingRows[parent] >= 0)
        guides.append(m_allItems[parent]->indent);
    }
    return guides;
  }
  case IndentRole:
    return item->indent;
  case OffsetRole:
    return item->offset;
  case LengthRole:
    return item->length;
  case HighlightedRole:
    return item->highlighted;
  case IsDiffRole:
    return item->isDiff;
  case ExpandedRole:
    return item->expanded;
  case HasChildrenRole:
    return item->hasChildren;
  case SubtreeExpandedRole: {
    if (!item->hasChildren)
      return true;
    if (!item->expanded)
      return false;
    int allIndex = m_allItems.indexOf(const_cast<ParseResultItem *>(item));
    if (allIndex < 0)
      return item->expanded;
    for (int i = allIndex + 1; i < m_allItems.size(); ++i) {
      const ParseResultItem *child = m_allItems[i];
      if (child->indent <= item->indent)
        break;
      if (!child->expanded)
        return false;
    }
    return true;
  }
  case SubtreeStateRole: {
    // 0: all expanded, 1: mixed, 2: collapsed
    if (!item->hasChildren)
      return 0;
    if (!item->expanded)
      return 2;

    int allIndex = m_allItems.indexOf(const_cast<ParseResultItem *>(item));
    if (allIndex < 0)
      return item->expanded ? 0 : 2;

    // item is expanded; if any descendant is collapsed -> mixed
    for (int i = allIndex + 1; i < m_allItems.size(); ++i) {
      const ParseResultItem *child = m_allItems[i];
      if (child->indent <= item->indent)
        break;
      if (!child->expanded)
        return 1; // mixed
    }

    return 0; // all expanded
  }
  case FrameSeparatorRole:
    return item->isFrameSeparator;
  }
  return QVariant();
}

bool ParseResultModel::setData(const QModelIndex &index, const QVariant &value,
                               int role) {
  if (!index.isValid() || index.row() >= m_visibleItems.count())
    return false;
  auto *item = m_visibleItems[index.row()];
  bool changed = false;
  switch (role) {
  case IsDiffRole:
    if (item->isDiff != value.toBool()) {
      item->isDiff = value.toBool();
      changed = true;
    }
    break;
  case HighlightedRole:
    if (item->highlighted != value.toBool()) {
      item->highlighted = value.toBool();
      changed = true;
    }
    break;
  }
  if (changed) {
    emit dataChanged(index, index, {role});
    return true;
  }
  return false;
}

QHash<int, QByteArray> ParseResultModel::roleNames() const {
  QHash<int, QByteArray> roles;
  roles[NameRole] = "name";
  roles[ValueRole] = "value";
  roles[DescriptionRole] = "description";
  roles[IndentRole] = "indent";
  roles[OffsetRole] = "offset";
  roles[LengthRole] = "length";
  roles[HighlightedRole] = "highlighted";
  roles[IsDiffRole] = "isDiff";
  roles[ExpandedRole] = "expanded";
  roles[HasChildrenRole] = "hasChildren";
  roles[SubtreeExpandedRole] = "subtreeExpanded";
  roles[SubtreeStateRole] = "subtreeState";
  roles[FrameSeparatorRole] = "isFrameSeparator";
  roles[DescriptionSummaryRole] = "descriptionSummary";
  roles[DescriptionDetailRole] = "descriptionDetail";
  roles[TreeGuidesRole] = "treeGuides";
  roles[HasParentRole] = "hasParent";
  roles[BranchContinuesRole] = "branchContinues";
  return roles;
}

void ParseResultModel::clear() {
  beginResetModel();
  qDeleteAll(m_allItems);
  m_allItems.clear();
  m_visibleItems.clear();
  m_parentRows.clear();
  m_nextSiblingRows.clear();
  m_openRows.clear();
  endResetModel();
}

void ParseResultModel::addItem(const ParseResultItem &item) {
  auto *newItem = new ParseResultItem(item);
  newItem->sourceRow = m_allItems.size();
  int previousSibling = -1;
  if (item.isFrameSeparator) m_openRows.clear();
  while (!m_openRows.isEmpty() && m_allItems[m_openRows.last()]->indent >= item.indent) {
    const int previous = m_openRows.takeLast();
    if (m_allItems[previous]->indent == item.indent) previousSibling = previous;
  }
  const int parent = m_openRows.isEmpty() ? -1 : m_openRows.last();
  m_parentRows.append(parent);
  m_nextSiblingRows.append(-1);
  m_allItems.append(newItem);
  if (!item.isFrameSeparator) m_openRows.append(newItem->sourceRow);

  if (parent >= 0 && !m_allItems[parent]->hasChildren) {
    m_allItems[parent]->hasChildren = true;
    if (!m_visibleItems.isEmpty() && m_visibleItems.last() == m_allItems[parent])
      emit dataChanged(index(m_visibleItems.size() - 1), index(m_visibleItems.size() - 1), {HasChildrenRole});
  }
  if (previousSibling >= 0) {
    m_nextSiblingRows[previousSibling] = newItem->sourceRow;
    // Parsers append a whole frame synchronously; refresh old guides once per batch.
    if (!m_treeUpdatePending) {
      m_treeUpdatePending = true;
      QTimer::singleShot(0, this, [this] {
        m_treeUpdatePending = false;
        if (!m_visibleItems.isEmpty())
          emit dataChanged(index(0), index(m_visibleItems.size() - 1),
                           {TreeGuidesRole, BranchContinuesRole});
      });
    }
  }
  beginInsertRows(QModelIndex(), m_visibleItems.size(), m_visibleItems.size());
  m_visibleItems.append(newItem);
  endInsertRows();
}

void ParseResultModel::setHighlight(int start, int length) {
  for (int i = 0; i < m_visibleItems.count(); ++i) {
    bool shouldHighlight =
        (start != -1 && length != -1 && m_visibleItems[i]->offset == start &&
         m_visibleItems[i]->length == length);
    if (m_visibleItems[i]->highlighted != shouldHighlight) {
      m_visibleItems[i]->highlighted = shouldHighlight;
      emit dataChanged(index(i), index(i), {HighlightedRole});
    }
  }
  // Also update m_allItems so state is preserved if we expand/collapse
  for (auto *item : m_allItems) {
    bool shouldHighlight = (start != -1 && length != -1 &&
                            item->offset == start && item->length == length);
    item->highlighted = shouldHighlight;
  }
}

void ParseResultModel::toggleExpand(int row) {
  if (row < 0 || row >= m_visibleItems.count())
    return;

  ParseResultItem *item = m_visibleItems[row];
  item->expanded = !item->expanded;

  // Update the icon immediately
  emit dataChanged(index(row), index(row), {ExpandedRole, SubtreeStateRole});

  // Notify ancestors that their subtreeState may have changed
  int allIndex = m_allItems.indexOf(item);
  if (allIndex >= 0) {
    int curIndent = item->indent;
    for (int i = allIndex - 1; i >= 0; --i) {
      ParseResultItem *anc = m_allItems[i];
      if (anc->indent < curIndent) {
        int vis = m_visibleItems.indexOf(anc);
        if (vis >= 0)
          emit dataChanged(index(vis), index(vis), {SubtreeStateRole});
        curIndent = anc->indent;
        if (curIndent == 0)
          break;
      }
    }
  }

  if (item->expanded) {
    // Expanding: Insert visible children
    int allIndex = m_allItems.indexOf(item);
    if (allIndex == -1)
      return; // Should not happen

    QList<ParseResultItem *> itemsToInsert;
    QList<int> collapsedIndents;

    for (int i = allIndex + 1; i < m_allItems.size(); ++i) {
      ParseResultItem *child = m_allItems[i];
      if (child->indent <= item->indent)
        break; // End of subtree

      bool hidden = false;
      if (!collapsedIndents.isEmpty()) {
        if (child->indent > collapsedIndents.last()) {
          hidden = true;
        } else {
          while (!collapsedIndents.isEmpty() &&
                 collapsedIndents.last() >= child->indent) {
            collapsedIndents.removeLast();
          }
          if (!collapsedIndents.isEmpty() &&
              child->indent > collapsedIndents.last()) {
            hidden = true;
          }
        }
      }

      if (!hidden) {
        itemsToInsert.append(child);
        if (!child->expanded) {
          collapsedIndents.append(child->indent);
        }
      }
    }

    if (!itemsToInsert.isEmpty()) {
      beginInsertRows(QModelIndex(), row + 1, row + itemsToInsert.size());
      for (int i = 0; i < itemsToInsert.size(); ++i) {
        m_visibleItems.insert(row + 1 + i, itemsToInsert[i]);
      }
      endInsertRows();
    }

  } else {
    // Collapsing: Remove visible children
    int countToRemove = 0;
    for (int i = row + 1; i < m_visibleItems.size(); ++i) {
      if (m_visibleItems[i]->indent > item->indent) {
        countToRemove++;
      } else {
        break;
      }
    }

    if (countToRemove > 0) {
      beginRemoveRows(QModelIndex(), row + 1, row + countToRemove);
      for (int i = 0; i < countToRemove; ++i) {
        m_visibleItems.removeAt(row + 1);
      }
      endRemoveRows();
    }
  }
}

void ParseResultModel::toggleSubtree(int row) {
  if (row < 0 || row >= m_visibleItems.count())
    return;

  ParseResultItem *item = m_visibleItems[row];
  if (!item->hasChildren)
    return;

  int allIndex = m_allItems.indexOf(item);
  if (allIndex < 0)
    return;

  // Tri-state toggle behavior:
  // - If currently collapsed (root collapsed), expand all.
  // - Otherwise (all-expanded or mixed), collapse all.
  const bool newExpanded = !item->expanded;

  // Update state for this node + all descendants in the flat all-items list.
  item->expanded = newExpanded;
  QList<ParseResultItem *> subtreeAllItems;
  for (int i = allIndex + 1; i < m_allItems.size(); ++i) {
    ParseResultItem *child = m_allItems[i];
    if (child->indent <= item->indent)
      break;
    child->expanded = newExpanded;
    subtreeAllItems.append(child);
  }

  // Notify icon states for the clicked row.
  emit dataChanged(index(row), index(row),
                   {ExpandedRole, SubtreeExpandedRole, SubtreeStateRole});

  // Notify ancestors that their subtreeState may have changed
  int curIndent = item->indent;
  for (int i = allIndex - 1; i >= 0; --i) {
    ParseResultItem *anc = m_allItems[i];
    if (anc->indent < curIndent) {
      int vis = m_visibleItems.indexOf(anc);
      if (vis >= 0)
        emit dataChanged(index(vis), index(vis), {SubtreeStateRole});
      curIndent = anc->indent;
      if (curIndent == 0)
        break;
    }
  }

  // Compute currently visible descendants range.
  int countToRemove = 0;
  for (int i = row + 1; i < m_visibleItems.size(); ++i) {
    if (m_visibleItems[i]->indent > item->indent) {
      countToRemove++;
    } else {
      break;
    }
  }

  if (!newExpanded) {
    // Collapsing: remove all currently visible descendants.
    if (countToRemove > 0) {
      beginRemoveRows(QModelIndex(), row + 1, row + countToRemove);
      for (int i = 0; i < countToRemove; ++i) {
        m_visibleItems.removeAt(row + 1);
      }
      endRemoveRows();
    }
    return;
  }

  // Expanding: show full subtree (all descendants) as all are expanded now.
  // To avoid complicated diffing, drop current visible descendants and insert
  // full subtree.
  if (countToRemove > 0) {
    beginRemoveRows(QModelIndex(), row + 1, row + countToRemove);
    for (int i = 0; i < countToRemove; ++i) {
      m_visibleItems.removeAt(row + 1);
    }
    endRemoveRows();
  }

  if (!subtreeAllItems.isEmpty()) {
    beginInsertRows(QModelIndex(), row + 1, row + subtreeAllItems.size());
    for (int i = 0; i < subtreeAllItems.size(); ++i) {
      m_visibleItems.insert(row + 1 + i, subtreeAllItems[i]);
    }
    endInsertRows();
  }
}

QList<int> ParseResultModel::getColumnWidths(double fontWidth,
                                             int indentWidth) {
  int maxNameW = 0;
  int maxValueW = 0;

  for (const auto *item : m_visibleItems) {
    // Name Column: Indent + Name + ExpandIcon(approx 2 chars)
    // Heuristic: Non-ASCII chars are 2x width
    int nameLen = 0;
    for (QChar c : item->name) {
      nameLen += (c.unicode() > 0xFF) ? 2 : 1;
    }
    // Name Column: Indent + Name + ExpandIcon + SubtreeIcon
    int nameW = (item->indent * indentWidth) + (int)(nameLen * fontWidth) +
                (int)(3 * fontWidth);
    if (nameW > maxNameW)
      maxNameW = nameW;

    // Value Column
    int valLen = 0;
    for (QChar c : item->value) {
      valLen += (c.unicode() > 0xFF) ? 2 : 1;
    }
    int valW = (int)(valLen * fontWidth);
    if (valW > maxValueW)
      maxValueW = valW;
  }

  // Add some padding
  maxNameW += 40;
  maxValueW += 20;

  return {maxNameW, maxValueW};
}

void ParseResultModel::rebuildVisibleItems() {
  beginResetModel();
  m_visibleItems.clear();

  QList<int>
      collapsedIndents; // Stack of indent levels that are currently collapsed

  for (auto *item : m_allItems) {
    // Check if we are hidden by any collapsed parent
    bool hidden = false;
    if (!collapsedIndents.isEmpty()) {
      // If current indent is greater than the last collapsed indent, we are a
      // child of it -> hidden
      if (item->indent > collapsedIndents.last()) {
        hidden = true;
      } else {
        // We popped out of the collapsed block
        // Remove any indent levels from stack that are >= current indent
        while (!collapsedIndents.isEmpty() &&
               collapsedIndents.last() >= item->indent) {
          collapsedIndents.removeLast();
        }
        // Check again if we are still hidden (by a higher level parent)
        if (!collapsedIndents.isEmpty() &&
            item->indent > collapsedIndents.last()) {
          hidden = true;
        }
      }
    }

    if (!hidden) {
      m_visibleItems.append(item);
      // If this item is collapsed, add its indent to the stack
      if (!item->expanded) {
        collapsedIndents.append(item->indent);
      }
    }
  }

  endResetModel();
}

// --- GlobalPreviewModel Implementation ---

GlobalPreviewModel::GlobalPreviewModel(QObject *parent)
    : QAbstractListModel(parent) {}

GlobalPreviewModel::~GlobalPreviewModel() = default;

int GlobalPreviewModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return m_items.count();
}

QVariant GlobalPreviewModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= m_items.count())
    return QVariant();
  const auto &item = m_items[index.row()];
  switch (role) {
  case TimestampRole:
    return item.timestamp;
  case ProtocolRole:
    return item.protocol;
  case AddressRole:
    return item.address;
  case DirectionRole:
    return item.direction;
  case FrameSeqRole:
    return item.frameSeq;
  case MessageTypeRole:
    return item.messageType;
  case LineNumberRole:
    return item.lineNumber;
  case RawDataRole:
    return item.rawData;
  }
  return QVariant();
}

QHash<int, QByteArray> GlobalPreviewModel::roleNames() const {
  QHash<int, QByteArray> roles;
  roles[TimestampRole] = "timestamp";
  roles[ProtocolRole] = "protocol";
  roles[AddressRole] = "address";
  roles[DirectionRole] = "direction";
  roles[FrameSeqRole] = "frameSeq";
  roles[MessageTypeRole] = "messageType";
  roles[LineNumberRole] = "lineNumber";
  roles[RawDataRole] = "rawData";
  return roles;
}

void GlobalPreviewModel::clear() {
  beginResetModel();
  m_items.clear();
  endResetModel();
}

void GlobalPreviewModel::addItem(const GlobalPreviewItem &item) {
  beginInsertRows(QModelIndex(), m_items.count(), m_items.count());
  m_items.append(item);
  endInsertRows();
}

// --- LogBackend Implementation ---

LogBackend::LogBackend(QObject *parent)
    : QObject(parent), m_protocolType(0), // 默认自动检测
      m_lastDetectedProtocol(ProtocolType::Unknown) {
  m_resultModel = new ParseResultModel(this);
  m_resultModel2 = new ParseResultModel(this);
  m_popupResultModel = new ParseResultModel(this);
  m_globalPreviewModel = new GlobalPreviewModel(this);
  m_presetManager = new PresetManager(this); // 初始化预置报文管理器
  m_protocolSyncManager = new ProtocolSyncManager(this);
  ProtocolSyncManager::Dependencies promptSync;
  promptSync.promptFiles = true;
  m_promptSyncManager = new ProtocolSyncManager(promptSync, this);
  connect(m_protocolSyncManager,
          &ProtocolSyncManager::protocolFileDownloaded, this,
          [this](const QString &fileName) {
            if (fileName != "csg_upstream_protocol.xml")
              return;

            QString error;
            const QString path = resolveProtocolPath(fileName);
            if (!CsgUpstreamConfigManager::instance().loadXml(path, &error)) {
              qWarning() << "热加载南网计量上行规约 XML 失败:" << error;
              emit statusMessage("协议 XML 已下载，但热加载失败：" + error,
                                 5000);
              return;
            }

            emit currentProtocolNameChanged();
            emit statusMessage("南网计量上行规约 XML 已更新并生效", 3000);
          });
  m_securityCloseTimer = new QTimer(this);
  m_securityCloseTimer->setSingleShot(true);
  connect(m_securityCloseTimer, &QTimer::timeout, this, [this]() {
    if (!m_securityControlActive)
      return;
    qWarning() << "安全管控超时，程序即将退出";
    QCoreApplication::quit();
  });

  m_securityCountdownTimer = new QTimer(this);
  m_securityCountdownTimer->setInterval(1000);
  connect(m_securityCountdownTimer, &QTimer::timeout, this, [this]() {
    if (!m_securityControlActive || m_securityControlRemainingSeconds <= 0)
      return;
    --m_securityControlRemainingSeconds;
    emit securityControlCountdownChanged();
  });

  connect(m_protocolSyncManager, &ProtocolSyncManager::statusUpdated, this,
          &LogBackend::recordSuccessfulSecurityVerification);
#ifdef Q_OS_MACOS
  connect(m_protocolSyncManager, &ProtocolSyncManager::statusFinished, this,
          [this](bool success, const QString &message) {
            const bool wasProbe = m_securityStatusProbePending;
            m_securityStatusProbePending = false;
            if (!m_securityControlActive || success)
              return;
            if (wasProbe && !m_protocolSyncManager->connecting()) {
              m_securityVerificationPending = true;
              m_protocolSyncManager->connectToShare();
            } else {
              qWarning() << "安全管控获取 XML 列表失败:" << message;
            }
          });
#endif
  connect(m_protocolSyncManager, &ProtocolSyncManager::connectionFinished, this,
          [this](bool success, const QString &message) {
            if (!m_securityVerificationPending)
              return;

            m_securityVerificationPending = false;
            if (!success) {
              qWarning() << "安全管控自动连接内网服务器失败:" << message;
              return;
            }

            if (!m_protocolSyncManager->fetchSyncStatus()) {
              qWarning() << "安全管控连接成功，但获取 XML 列表失败:"
                         << m_protocolSyncManager->lastError();
            }
          });

  // 加载配置
  loadSettings();

  // 初始化版本号
  getAppVersion();

  // 释放内置规约到外部，方便用户查看和修改
  ensureProtocolsExtracted();

  // Initialize AppMsgLoader
  AppMsg::AppMsgLoader::instance().load(
      resolveProtocolPath("app_msg_protocol.xml"));

  // Initialize ModuleMsgLoader
  ModuleMsg::ModuleMsgLoader::instance().load(
      resolveProtocolPath("module_msg_protocol.xml"));

  // Initialize Prot3762ConfigManager
  Prot3762ConfigManager::instance().loadXml(
      resolveProtocolPath("3762_protocol.xml"));

  // Initialize Prot645ConfigManager
  Prot645ConfigManager::instance().loadXml(
      resolveProtocolPath("645_protocol.xml"));

  QString csgConfigError;
  if (!CsgUpstreamConfigManager::instance().loadXml(
          resolveProtocolPath("csg_upstream_protocol.xml"),
          &csgConfigError)) {
    qWarning() << "加载南网计量上行规约 XML 失败:" << csgConfigError;
  }

  // Initialize Prot698OadManager
  Prot698OadManager::instance().loadXml(
      resolveProtocolPath("698_protocol.xml"));
  m_aiFraming = new AiFramingController([this] { return getNextPiid(); }, this);

  initializeSecurityControl();

  // 配置已在loadSettings()中加载
}

QStringList LogBackend::fileList() const { return m_fileList; }
QStringList LogBackend::fileDisplayList() const { return m_fileDisplayList; }

QString LogBackend::currentFileContent() const { return m_currentFileContent; }

QString LogBackend::currentFilePath() const { return m_currentFilePath; }

QString LogBackend::parseResult() const { return m_parseResult; }

ParseResultModel *LogBackend::resultModel() const { return m_resultModel; }

ParseResultModel *LogBackend::resultModel2() const { return m_resultModel2; }

ParseResultModel *LogBackend::popupResultModel() const {
  return m_popupResultModel;
}

GlobalPreviewModel *LogBackend::globalPreviewModel() const {
  return m_globalPreviewModel;
}

ProtocolSyncManager *LogBackend::syncManager() const {
  return m_protocolSyncManager;
}

int LogBackend::totalLines() const { return m_totalLines; }

int LogBackend::scannedLines() const { return m_scannedLines; }

double LogBackend::scanProgress() const { return m_scanProgress; }

int LogBackend::unknownLines() const { return m_unknownLines; }

bool LogBackend::securityControlActive() const {
  return m_securityControlActive;
}

QString LogBackend::securityControlMessage() const {
  return m_securityControlMessage;
}

int LogBackend::securityControlRemainingSeconds() const {
  return m_securityControlRemainingSeconds;
}

uint16_t LogBackend::calc698CRC16(const uint8_t *data, int len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int j = 0; j < 8; ++j) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0x8408;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc ^ 0xFFFF;
}

QString LogBackend::replace698Address(const QString &hexStr,
                                      const QString &newAddress,
                                      const QString &newLogicAddress) {
  QString cleanHex = hexStr;
  cleanHex.remove(" ");
  cleanHex.remove("\n");
  cleanHex.remove("\r");
  cleanHex.remove("\t");

  QByteArray data = QByteArray::fromHex(cleanHex.toUtf8());
  if (data.size() < 10 || (uint8_t)data[0] != 0x68 ||
      (uint8_t)data[data.size() - 1] != 0x16) {
    return hexStr;
  }

  // 1. Find SA
  int offset = 3; // Skip 68 and Length(2)
  if (offset + 1 > data.size())
    return hexStr;
  offset++; // Skip Control(1)

  int saStart = offset;
  if (offset + 1 > data.size())
    return hexStr;
  uint8_t saInd = (uint8_t)data[offset];
  int totalAddrBytes = (saInd & 0x0F) + 1; // N+1 bytes
  bool hasExtLogicAddr = (saInd >> 5) & 0x01;
  int logicAddrInBit = (saInd >> 4) & 0x01;

  offset++; // Move past indicator

  int currentLogicAddr = 0;
  QByteArray currentPhyAddr;

  if (hasExtLogicAddr) {
    // Extended logical address is first byte after indicator
    if (offset < data.size()) {
      currentLogicAddr = (uint8_t)data[offset];
      offset++;
    }
    int phyLen = totalAddrBytes - 1;
    if (offset + phyLen <= data.size()) {
      currentPhyAddr = data.mid(offset, phyLen);
    }
  } else {
    currentLogicAddr = logicAddrInBit;
    int phyLen = totalAddrBytes;
    if (offset + phyLen <= data.size()) {
      currentPhyAddr = data.mid(offset, phyLen);
    }
  }

  // Determine Final Values
  QByteArray finalPhyAddrBytes;
  if (!newAddress.isEmpty()) {
    QString cleanNewAddr = newAddress;
    cleanNewAddr.remove(" ");
    if (cleanNewAddr.length() % 2 != 0)
      cleanNewAddr = "0" + cleanNewAddr;
    QByteArray newAddrBytes = QByteArray::fromHex(cleanNewAddr.toUtf8());
    // Input is usually Big Endian (human readable), 698 needs Little Endian.
    for (int i = newAddrBytes.size() - 1; i >= 0; --i) {
      finalPhyAddrBytes.append(newAddrBytes[i]);
    }
  } else {
    finalPhyAddrBytes = currentPhyAddr;
  }

  int finalLogicAddr = currentLogicAddr;
  if (!newLogicAddress.isEmpty()) {
    finalLogicAddr = newLogicAddress.toInt();
  }

  // 2. Construct new SA
  int newPhyLen = finalPhyAddrBytes.size();

  // Logic Address Handling
  bool useExtLogic = (finalLogicAddr > 1);
  uint8_t newSaInd = (saInd & 0xC0); // Keep address type (bits 6-7)

  if (useExtLogic) {
    newSaInd |= 0x20; // Set bit 5 (Extended Logical Address)
                      // Bit 4 is usually 0 when bit 5 is 1
  } else {
    if (finalLogicAddr == 1) {
      newSaInd |= 0x10; // Set bit 4
    }
    // Bit 5 is 0
  }

  int newAddrLen = newPhyLen + (useExtLogic ? 1 : 0);
  newSaInd |= (newAddrLen - 1); // Set bits 0-3 (Length mask)

  QByteArray newSa;
  newSa.append((char)newSaInd);
  if (useExtLogic) {
    newSa.append(
        (char)(finalLogicAddr & 0xFF)); // Extended Logical Address first
  }
  newSa.append(finalPhyAddrBytes); // Physical Address last

  // 3. Replace SA in data
  int oldSaTotalLen = 1 + totalAddrBytes;
  data.replace(saStart, oldSaTotalLen, newSa);

  // 4. Update Length field
  int lenDiff = newSa.size() - oldSaTotalLen;
  uint16_t oldLen = (uint8_t)data[1] | ((uint8_t)data[2] << 8);
  uint16_t newLen = oldLen + lenDiff;
  data[1] = (char)(newLen & 0xFF);
  data[2] = (char)((newLen >> 8) & 0xFF);

  // 5. Recalculate HCS
  int caOffset = saStart + newSa.size();
  if (caOffset + 1 > data.size())
    return hexStr;
  int hcsOffset = caOffset + 1;
  if (hcsOffset + 2 > data.size())
    return hexStr;

  uint16_t hcs =
      calc698CRC16((const uint8_t *)data.constData() + 1, hcsOffset - 1);
  data[hcsOffset] = (char)(hcs & 0xFF);
  data[hcsOffset + 1] = (char)((hcs >> 8) & 0xFF);

  // 6. Recalculate FCS
  int fcsOffset = data.size() - 3;
  if (fcsOffset < hcsOffset + 2)
    return hexStr;

  uint16_t fcs =
      calc698CRC16((const uint8_t *)data.constData() + 1, fcsOffset - 1);
  data[fcsOffset] = (char)(fcs & 0xFF);
  data[fcsOffset + 1] = (char)((fcs >> 8) & 0xFF);

  return data.toHex(' ').toUpper();
}

void LogBackend::copyToClipboard(const QString &text) {
  QGuiApplication::clipboard()->setText(text);
}

QString LogBackend::formatHexString(const QString &hexStr) {
  // 清理输入字符串
  QString cleanText = hexStr;
  cleanText.remove("0x");
  cleanText.remove(" ");
  cleanText.remove("\n");
  cleanText.remove("\r");
  cleanText.remove("\t");

  // 检查是否为有效的十六进制
  bool isHex = true;
  for (QChar c : cleanText) {
    if (!QString("0123456789ABCDEFabcdef").contains(c)) {
      isHex = false;
      break;
    }
  }

  if (!isHex || cleanText.isEmpty()) {
    return hexStr; // 返回原始字符串
  }

  // 格式化为带空格的大写格式
  QString formattedHex;
  for (int i = 0; i < cleanText.length(); i += 2) {
    if (i > 0)
      formattedHex += " ";
    formattedHex += cleanText.mid(i, 2).toUpper();
  }

  return formattedHex;
}

void LogBackend::setCurrentFilePath(const QString &path) {
  if (m_currentFilePath != path) {
    m_currentFilePath = path;
    emit currentFilePathChanged();
    // If path is just a filename, load it from current folder
    if (!path.contains("/") && !path.contains("\\")) {
      loadFile(path);
    }
  }
}

void LogBackend::openFolder(const QString &path) {
  QUrl url(path);
  QString localPath = url.isLocalFile() ? url.toLocalFile() : path;

  // Remove file:// prefix if present (though toLocalFile handles it)
  if (localPath.startsWith("file:///")) {
    localPath = localPath.mid(8);
  } else if (localPath.startsWith("file://")) {
    localPath = localPath.mid(7);
  }

  m_currentFolderPath = localPath;
  QDir dir(localPath);
  QStringList filters;
  filters << "*.txt" << "*.log";
  dir.setNameFilters(filters);

  m_fileList = dir.entryList(QDir::Files);

  // Numeric sort
  QCollator collator;
  collator.setNumericMode(true);
  std::sort(m_fileList.begin(), m_fileList.end(),
            [&](const QString &s1, const QString &s2) {
              return collator.compare(s1, s2) < 0;
            });

  m_fileDisplayList.clear();
  for (const QString &fileName : m_fileList) {
    const QFileInfo info(dir.filePath(fileName));
    const QString modified = info.lastModified()
                                 .toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
    m_fileDisplayList.append(fileName + QStringLiteral("  |  修改：") + modified
                            + QStringLiteral("  |  大小：")
                            + QLocale::c().formattedDataSize(info.size(), 1));
  }

  emit fileListChanged();
}

void LogBackend::loadFile(const QString &fileName, const QString &encoding) {
  if (m_currentFolderPath.isEmpty() || fileName.isEmpty())
    return;

  QString fullPath = m_currentFolderPath + "/" + fileName;
  QFile file(fullPath);
  if (file.open(QIODevice::ReadOnly)) {
    QByteArray data = file.readAll();
    file.close();

    QString content;

    if (encoding == "auto") {
      // Check for UTF-8 BOM
      if (data.startsWith("\xEF\xBB\xBF")) {
        content = QString::fromUtf8(data);
      } else {
        // Validate UTF-8
        bool isUtf8 = true;
        int len = data.size();
        const unsigned char *bytes = (const unsigned char *)data.constData();
        for (int i = 0; i < len; ++i) {
          if (bytes[i] < 0x80)
            continue;
          int trailing = 0;
          if ((bytes[i] & 0xE0) == 0xC0)
            trailing = 1;
          else if ((bytes[i] & 0xF0) == 0xE0)
            trailing = 2;
          else if ((bytes[i] & 0xF8) == 0xF0)
            trailing = 3;
          else {
            isUtf8 = false;
            break;
          }

          if (i + trailing >= len) {
            isUtf8 = false;
            break;
          }
          for (int j = 0; j < trailing; ++j) {
            i++;
            if ((bytes[i] & 0xC0) != 0x80) {
              isUtf8 = false;
              break;
            }
          }
          if (!isUtf8)
            break;
        }

        if (isUtf8) {
          content = QString::fromUtf8(data);
        } else {
          // Fallback to GBK (System)
          QStringDecoder decoder("GBK");
          if (decoder.isValid()) {
            content = decoder(data);
          } else {
            content = QString::fromLocal8Bit(data);
          }
        }
      }
    } else if (encoding == "UTF-8") {
      content = QString::fromUtf8(data);
    } else if (encoding == "GBK") {
      QStringDecoder decoder("GBK");
      if (decoder.isValid()) {
        content = decoder(data);
      } else {
        content = QString::fromLocal8Bit(data);
      }
    } else if (encoding == "ASCII") {
      content = QString::fromLatin1(data);
    } else {
      content = QString::fromUtf8(data);
    }

    // 统一换行符：将 Windows \r\n 替换为 \n，确保与 QML TextArea 字符位置一致
    content.replace("\r\n", "\n");
    content.remove('\r'); // 移除可能存在的单独 \r

    m_originalContent = content;
    m_currentFileContent = m_originalContent;

    // 重置最后检测到的协议
    m_lastDetectedProtocol = ProtocolType::Unknown;
    emit currentProtocolNameChanged();

    emit currentFileContentChanged();
  } else {
    m_originalContent = "Error loading file: " + fullPath;
    m_currentFileContent = m_originalContent;
    emit currentFileContentChanged();
  }
}

void LogBackend::loadRawText(const QString &text) {
  m_originalContent = text;
  m_currentFileContent = text;
  // 重置协议检测
  m_lastDetectedProtocol = ProtocolType::Unknown;
  emit currentProtocolNameChanged();
  // 不 emit currentFileContentChanged，避免覆盖 TextArea 中已显示的内容
}

void LogBackend::applyFilter(const QString &regexStr) {
  if (regexStr.isEmpty()) {
    m_currentFileContent = m_originalContent;
    emit currentFileContentChanged();
    return;
  }

  QRegularExpression regex(regexStr);
  if (!regex.isValid()) {
    emit statusMessage("Invalid Regular Expression: " + regex.errorString(), 5000);
    return;
  }

  QStringList lines = m_originalContent.split('\n');
  QStringList filteredLines;
  for (const QString &line : lines) {
    if (regex.match(line).hasMatch()) {
      filteredLines.append(line);
    }
  }

  m_currentFileContent = filteredLines.join('\n');
  emit currentFileContentChanged();
}

int LogBackend::createParseSession() {
  const int modelId = m_nextSessionModelId++;
  m_sessionModels.insert(modelId, new ParseResultModel(this));
  return modelId;
}

void LogBackend::closeParseSession(int modelId) {
  if (auto *model = m_sessionModels.take(modelId)) {
    m_parsedTexts.remove(modelId);
    model->deleteLater();
  }
}

ParseResultModel *LogBackend::parseModel(int modelId) const {
  if (modelId == 1) return m_resultModel;
  if (modelId == 2) return m_resultModel2;
  if (modelId == 3) return m_popupResultModel;
  return m_sessionModels.value(modelId, nullptr);
}

void LogBackend::parseHex(const QString &text, const QString &encoding,
                          int targetModel, int protocolOverride) {
  auto *target = parseModel(targetModel);
  if (!target || protocolOverride < -1 || protocolOverride > 8)
    return;
  const int selectedProtocol = protocolOverride < 0 ? m_protocolType : protocolOverride;
  m_parsedTexts[targetModel] = text;
  if (text.isEmpty()) {
    target->clear();
    if (targetModel == 1) {
      m_parseResult = "";
      emit parseResultChanged();
    }
    return;
  }

  // === [调试] 打印原始输入 ===
  qDebug() << "[parseHex] targetModel =" << targetModel
           << ", 原始输入长度 =" << text.length();
  // 只打印前120个字符，避免刷屏
  qDebug() << "[parseHex] 原始输入前120字符:" << text.left(120);

  QString result;

  // Simple logic: Find hex patterns
  // 1. Look for 0x...
  // 2. Look for [...]

  // For this first pass, let's just try to interpret the selection as hex if it
  // looks like it
  QString cleanText = text;
  cleanText.remove("0x");
  cleanText.remove(" ");
  cleanText.remove("\n");
  cleanText.remove("\r");
  cleanText.remove("\t");

  // Check if valid hex
  bool isHex = true;
  for (QChar c : cleanText) {
    if (!QString("0123456789ABCDEFabcdef").contains(c)) {
      isHex = false;
      break;
    }
  }

  qDebug() << "[parseHex] 去除空白后 cleanText 长度 =" << cleanText.length()
           << "，是否纯hex:" << isHex;

  if (!isHex) {
    // 尝试提取潜在的Hex报文流：至少包含 5 个字节（即 10 个有效 16 进制字符）
    // 允许包含空格、制表符分隔。使用正则匹配大段的 hex 流
    // 这里我们先将除了 hex
    // 字符和空格之外的字符作为分隔符，找到最长的那段含有足够多 hex 字符的片段
    QString potentialHex;
    QString currentHexStream;
    int hexCountInCurrent = 0;

    for (int i = 0; i < text.length(); ++i) {
      QChar c = text[i];
      if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F')) {
        currentHexStream.append(c);
        hexCountInCurrent++;
      } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        // 保留空格结构便于观察，但不增加计数
        if (!currentHexStream.isEmpty() && !currentHexStream.endsWith(' ')) {
          currentHexStream.append(' ');
        }
      } else {
        // 遇到明显的非法字符（如日志时间戳的 '-', ':', 字母 'g'-'z'
        // 等），切断当前流
        if (hexCountInCurrent >= 10 &&
            currentHexStream.length() > potentialHex.length()) {
          potentialHex = currentHexStream;
        }
        currentHexStream.clear();
        hexCountInCurrent = 0;
      }
    }

    // 检查最后一段
    if (hexCountInCurrent >= 10 &&
        currentHexStream.length() > potentialHex.length()) {
      potentialHex = currentHexStream;
    }

    if (!potentialHex.isEmpty()) {
      cleanText = potentialHex;
      cleanText.remove(" ");
      isHex = true;
      qDebug() << "[parseHex] 提取到最长hex段，长度 =" << cleanText.length()
               << "，前80字符:" << cleanText.left(80);
    } else {
      // 退化为原来的简单提取最长 hex 字符串逻辑（用于提取单个短地址等）
      QString longestHex;
      QString currentHex;
      for (QChar c : cleanText) {
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F')) {
          currentHex.append(c);
        } else {
          if (currentHex.length() > longestHex.length()) {
            longestHex = currentHex;
          }
          currentHex.clear();
        }
      }
      if (currentHex.length() > longestHex.length()) {
        longestHex = currentHex;
      }

      if (longestHex.length() >= 4) { // 至少2个字节
        cleanText = longestHex;
        isHex = true;
      }
    }
  }

  if (isHex && cleanText.length() >= 2) {
    // 格式化十六进制字符串为带空格的格式（XX XX XX）
    QString formattedHex;
    for (int i = 0; i < cleanText.length(); i += 2) {
      if (i > 0)
        formattedHex += " ";
      formattedHex += cleanText.mid(i, 2).toUpper();
    }

    // 更新存储的解析文本为格式化后的版本
    m_parsedTexts[targetModel] = formattedHex;

    QByteArray allData = QByteArray::fromHex(cleanText.toUtf8());

    // 清空目标Model，准备填充多帧数据
    target->clear();

    // 确定使用的协议类型（仅检测一次）
    ProtocolType protocolToUse;
    int firstFrameOffset = 0;

    if (selectedProtocol == 0) {
      // 自动检测模式 - 检测第一帧的协议类型
      protocolToUse =
          ProtocolDetector::detectProtocol(allData, &firstFrameOffset);
      if (protocolToUse == ProtocolType::Unknown && Prot698Parser::validateApdu(allData))
        protocolToUse = ProtocolType::Protocol698;
      if (targetModel < 4) {
        m_lastDetectedProtocol = protocolToUse;
        emit currentProtocolNameChanged();
      }
    } else {
      // 手动指定模式
      protocolToUse = static_cast<ProtocolType>(selectedProtocol);
      if (targetModel < 4)
        m_lastDetectedProtocol = protocolToUse;
      // 手动模式也检测一下偏移量
      ProtocolDetector::detectProtocol(allData, &firstFrameOffset);
    }

    // === [调试] 打印协议检测结果 ===
    qDebug() << "[parseHex] allData 总字节数 =" << allData.size()
             << "，firstFrameOffset =" << firstFrameOffset << "，检测到协议 ="
             << ProtocolDetector::getProtocolName(protocolToUse)
             << QString("(type=%1)").arg(static_cast<int>(protocolToUse));
    qDebug() << "[parseHex] allData 前32字节:"
             << allData.left(32).toHex(' ').toUpper();

    // 如果无法识别协议，提供明确的反馈
    if (protocolToUse == ProtocolType::Unknown) {
      if (targetModel == 1) {
        m_parseResult = "无法识别协议类型，请尝试手动选择协议。";
        emit parseResultChanged();
      } else if (targetModel >= 3) {
        // 向弹出面板的目标 Model 写入错误信息以便提示用户
        ParseResultItem errorItem;
        errorItem.name = "解析失败";
        errorItem.value = "";
        errorItem.description =
            "无法自动识别该段数据属于哪种协议。\n如果您确信这是一段有效报文，请"
            "在上方的下拉框中【手动指定】协议类型后再试。";
        target->addItem(errorItem);
      }
      return;
    }

    // 多帧循环处理
    QByteArray remainingData =
        allData.mid(firstFrameOffset); // 跳过前置垃圾字节
    int processedFrames = 0;
    const int MAX_FRAMES = 100; // 防止无限循环
    bool hasValidFrame = false;
    int currentAbsoluteOffset =
        firstFrameOffset; // 当前帧在原始数据中的绝对偏移量

    // 裸 APDU 没有链路层长度域，不能进入完整帧分割循环。
    if (protocolToUse == ProtocolType::Protocol698 &&
        !remainingData.isEmpty() && (quint8)remainingData[0] != 0x68) {
      // 不是以68开头，尝试直接解析为纯APDU
      bool parseSuccess = parseSingleFrame(remainingData, protocolToUse, target,
                                           targetModel, currentAbsoluteOffset);
      if (parseSuccess) {
        hasValidFrame = true;
        processedFrames = 1;
      }
      // 直接跳到结果处理，不进入多帧循环
      remainingData.clear();
    }

    while (!remainingData.isEmpty() && processedFrames < MAX_FRAMES) {
      // 获取当前帧的长度
      int frameLen =
          ProtocolDetector::getFrameLength(remainingData, protocolToUse);

      // 长度检查
      if (frameLen <= 0 || frameLen > remainingData.size()) {
        // 长度无效，可能遇到帧间垃圾字节
        // 尝试跳过一个字节，继续寻找下一个同类型帧
        if (remainingData.size() > 1) {
          remainingData = remainingData.mid(1);
          currentAbsoluteOffset++;
          continue; // 继续尝试下一个位置
        } else {
          break; // 数据不足，退出循环
        }
      }

      // 提取当前帧
      QByteArray frameData = remainingData.left(frameLen);

      // === [调试] 打印帧长度 ===
      qDebug() << "[parseHex] 第" << (processedFrames + 1)
               << "帧: frameLen =" << frameLen << "，帧首字节:"
               << QString("%1")
                      .arg((quint8)frameData[0], 2, 16, QChar('0'))
                      .toUpper();

      // 解析当前帧（传入绝对偏移量）
      bool parseSuccess = parseSingleFrame(frameData, protocolToUse, target,
                                           targetModel, currentAbsoluteOffset);

      qDebug() << "[parseHex] parseSingleFrame 返回:" << parseSuccess;

      if (parseSuccess) {
        hasValidFrame = true;
        processedFrames++;

        // 如果还有剩余数据，插入帧分隔符
        QByteArray nextRemainingData = remainingData.mid(frameLen);
        if (!nextRemainingData.isEmpty() && processedFrames < MAX_FRAMES) {
          ParseResultItem separator;
          separator.isFrameSeparator = true;
          separator.name =
              QString("──────── 第 %1 帧结束 ────────").arg(processedFrames);
          target->addItem(separator);
        }
      }

      // 移除已处理的帧，继续下一帧
      remainingData = remainingData.mid(frameLen);
      currentAbsoluteOffset += frameLen; // 更新绝对偏移量
    }

    // 更新解析结果状态
    if (targetModel == 1) {
      if (hasValidFrame) {
        if (processedFrames > 1) {
          m_parseResult = QString("成功解析 %1 个帧").arg(processedFrames);
        } else {
          m_parseResult = "解析成功";
        }
      } else {
        m_parseResult = "解析失败";
      }
      emit parseResultChanged();
    }

  } else {
    target->clear();
    if (targetModel == 1) {
      m_parseResult = "No valid hex detected or length too short.";
      emit parseResultChanged();
    }
  }
}

// 辅助函数：解析单个帧
bool LogBackend::parseSingleFrame(const QByteArray &data,
                                  ProtocolType protocolType,
                                  ParseResultModel *target, int targetModel,
                                  int offsetAdjustment) {
  // 根据协议类型调用对应的解析器
  switch (protocolType) {
  case ProtocolType::Protocol101: {
    Prot101Parser parser101;
    QList<Prot101Field> fields = parser101.parse(data);

    if (!fields.isEmpty()) {
      for (const auto &f : fields) {
        ParseResultItem item;
        item.name = f.name;
        item.value = f.value;
        item.description = f.description;
        item.indent = f.indent;
        item.offset =
            (f.offset >= 0) ? f.offset + offsetAdjustment : -1; // 调整偏移量
        item.length = f.length;
        target->addItem(item);
      }
      return true;
    }
    return false;
  }

  case ProtocolType::Protocol104: {
    Prot104Parser parser104;
    QList<Prot104Field> fields = parser104.parse(data);

    bool parseOk = fields.size() >= 2;
    if (parseOk) {
      for (const auto &f : fields) {
        ParseResultItem item;
        item.name = f.name;
        item.value = f.value;
        item.description = f.description;
        item.indent = f.indent;
        item.offset = (f.offset >= 0) ? f.offset + offsetAdjustment : -1;
        item.length = f.length;
        target->addItem(item);
      }
      return true;
    }
    return false;
  }

  case ProtocolType::Protocol698: {
    Prot698Parser parser698;
    Prot698ParseResult result;
    bool validApdu = false;
    if (!data.isEmpty() && static_cast<quint8>(data.front()) != 0x68) {
      int consumed = 0;
      parser698.parseApdu(data, consumed, result.fields, 0);
      validApdu = consumed == data.size() && !result.fields.isEmpty();
      for (const auto &field : result.fields)
        if (field.name == "Error" || field.name.startsWith("Unknown APDU")) validApdu = false;
    } else {
      result = parser698.parseWithAddress(data);
    }

    if (result.isValid || validApdu) {
      for (const auto &f : result.fields) {
        ParseResultItem item;
        item.name = f.name;
        item.value = f.value;
        item.description = f.description;
        item.summary = f.summary;
        item.detail = f.detail;
        item.indent = f.indent;
        item.offset = (f.offset >= 0) ? f.offset + offsetAdjustment : -1;
        item.length = f.length;
        target->addItem(item);
      }

      // 仅在主解析模型（targetModel == 1）且是第一帧时发送地址更新信号
      if (targetModel == 1 && offsetAdjustment == 0 &&
          (!result.physicalAddress.isEmpty() ||
           !result.logicAddress.isEmpty())) {
        if (!result.physicalAddress.isEmpty()) {
          m_current698PhysicalAddr = result.physicalAddress;
        }
        emit update698Address(result.physicalAddress, result.logicAddress);
      }

      return true;
    }
    return false;
  }

  case ProtocolType::Protocol3762: {
    Prot3762Parser parser3762;
    QList<Prot3762Field> fields = parser3762.parse(data);

    if (!fields.isEmpty()) {
      for (const auto &f : fields) {
        ParseResultItem item;
        item.name = f.name;
        item.value = f.value;
        item.description = f.description;
        item.indent = f.indent;
        item.offset = (f.offset >= 0) ? f.offset + offsetAdjustment : -1;
        item.length = f.length;
        target->addItem(item);
      }
      return true;
    }
    return false;
  }

  case ProtocolType::Protocol645: {
    Prot645Parser parser645;
    QList<Prot645Field> fields = parser645.parse(data);

    if (!fields.isEmpty()) {
      for (const auto &f : fields) {
        ParseResultItem item;
        item.name = f.name;
        item.value = f.value;
        item.description = f.description;
        item.indent = f.indent;
        item.offset = (f.offset >= 0) ? f.offset + offsetAdjustment : -1;
        item.length = f.length;
        target->addItem(item);
      }
      return true;
    }
    return false;
  }

  case ProtocolType::ProtocolCsgUpstream: {
    CsgUpstreamParser parser;
    const QList<CsgUpstreamField> fields = parser.parse(data);
    if (fields.isEmpty())
      return false;
    for (const auto &field : fields) {
      ParseResultItem item;
      item.name = field.name;
      item.value = field.value;
      item.description = field.description;
      item.indent = field.indent;
      item.offset = field.offset >= 0 ? field.offset + offsetAdjustment : -1;
      item.length = field.length;
      target->addItem(item);
    }
    return true;
  }

  case ProtocolType::ProtocolModuleMsg: {
    ModuleMsg::ModuleMsgParser moduleParser;
    QList<ModuleMsg::ModuleMsgField> fields = moduleParser.parseToFields(data);

    if (!fields.isEmpty()) {
      for (const auto &f : fields) {
        ParseResultItem item;
        item.name = f.name;
        item.value = f.value;
        item.description = f.description;
        item.summary = f.summary;
        item.detail = f.detail;
        item.indent = f.indent;
        item.offset = (f.offset >= 0) ? f.offset + offsetAdjustment : -1;
        item.length = f.length;
        target->addItem(item);
      }
      return true;
    }
    return false;
  }

  case ProtocolType::ProtocolAppMsg: {
    AppMsg::AppMsgParser parser;
    QList<AppMsg::AppMsgField> fields = parser.parseToFields(data);

    if (!fields.isEmpty()) {
      for (const auto &f : fields) {
        ParseResultItem item;
        item.name = f.name;
        item.value = f.value;
        item.description = f.description;
        item.summary = f.summary;
        item.detail = f.detail;
        item.indent = f.indent;
        item.offset = (f.offset >= 0) ? f.offset + offsetAdjustment : -1;
        item.length = f.length;
        target->addItem(item);
      }
      return true;
    }
    return false;
  }

  default:
    return false;
  }
}

void LogBackend::parsePopupApdu(const QString &apduHex) {
  m_popupResultModel->clear();
  if (apduHex.isEmpty())
    return;

  QString cleanText = apduHex;
  cleanText.remove("0x");
  cleanText.remove(" ");
  cleanText.remove("\n");
  cleanText.remove("\r");
  cleanText.remove("\t");

  QByteArray data = QByteArray::fromHex(cleanText.toUtf8());
  Prot698Parser parser;
  QList<Prot698Field> fields;
  int offset = 0;

  // Call the public parseApdu
  parser.parseApdu(data, offset, fields, 0);

  for (const auto &f : fields) {
    ParseResultItem item;
    item.name = f.name;
    item.value = f.value;
    item.description = f.description;
    item.summary = f.summary;
    item.detail = f.detail;
    item.indent = f.indent;
    item.offset = f.offset;
    item.length = f.length;
    m_popupResultModel->addItem(item);
  }
}

void LogBackend::compareModels() {
  int count1 = m_resultModel->rowCount();
  int count2 = m_resultModel2->rowCount();
  int maxCount = qMax(count1, count2);

  for (int i = 0; i < maxCount; ++i) {
    bool diff = false;
    if (i >= count1 || i >= count2) {
      diff = true;
    } else {
      QModelIndex idx1 = m_resultModel->index(i);
      QModelIndex idx2 = m_resultModel2->index(i);

      QString val1 =
          m_resultModel->data(idx1, ParseResultModel::ValueRole).toString();
      QString val2 =
          m_resultModel2->data(idx2, ParseResultModel::ValueRole).toString();

      if (val1 != val2) {
        diff = true;
      }
    }

    if (diff) {
      if (i < count1)
        m_resultModel->setData(m_resultModel->index(i), true,
                               ParseResultModel::IsDiffRole);
      if (i < count2)
        m_resultModel2->setData(m_resultModel2->index(i), true,
                                ParseResultModel::IsDiffRole);
    } else {
      if (i < count1)
        m_resultModel->setData(m_resultModel->index(i), false,
                               ParseResultModel::IsDiffRole);
      if (i < count2)
        m_resultModel2->setData(m_resultModel2->index(i), false,
                                ParseResultModel::IsDiffRole);
    }
  }
}

void LogBackend::clearDiffs() {
  int count1 = m_resultModel->rowCount();
  for (int i = 0; i < count1; ++i) {
    m_resultModel->setData(m_resultModel->index(i), false,
                           ParseResultModel::IsDiffRole);
  }

  int count2 = m_resultModel2->rowCount();
  for (int i = 0; i < count2; ++i) {
    m_resultModel2->setData(m_resultModel2->index(i), false,
                            ParseResultModel::IsDiffRole);
  }
}

void LogBackend::clearAllParses() {
  m_resultModel->clear();
  m_resultModel2->clear();
  m_parseResult = "";
  emit parseResultChanged();
}

int LogBackend::findNextDiff(int currentIndex) {
  int count = m_resultModel->rowCount();
  // Start searching from next index
  for (int i = currentIndex + 1; i < count; ++i) {
    QModelIndex idx = m_resultModel->index(i);
    bool isDiff =
        m_resultModel->data(idx, ParseResultModel::IsDiffRole).toBool();
    if (isDiff) {
      return i;
    }
  }
  return -1;
}

int LogBackend::findPrevDiff(int currentIndex) {
  // Start searching from prev index
  for (int i = currentIndex - 1; i >= 0; --i) {
    QModelIndex idx = m_resultModel->index(i);
    bool isDiff =
        m_resultModel->data(idx, ParseResultModel::IsDiffRole).toBool();
    if (isDiff) {
      return i;
    }
  }
  return -1;
}

QVariantMap LogBackend::testMonitorExpression(const QString &pattern, bool ignoreCase, const QString &sample) const {
  return SshMonitorMatcher::test(pattern, ignoreCase, sample);
}

void LogBackend::handleLink(const QString &link, int modelId) {
  // link is now just "offset,length" or we can keep "highlight:offset,length"
  // But since we are calling from QML directly with model data, we can
  // simplify. Let's keep the string format for compatibility or change to int
  // args. The user asked for "native table", so the click will come from
  // Delegate.

  // Let's support "highlight:offset,length"
  if (link.startsWith("highlight:")) {
    QStringList parts = link.mid(10).split(',');
    if (parts.size() == 2) {
      int byteOffset = parts[0].toInt();
      int byteLength = parts[1].toInt();

      // Update Model with highlight
      ParseResultModel *targetModel = parseModel(modelId);
      if (!targetModel) return;
      targetModel->setHighlight(byteOffset, byteLength);

      int hexCharsSeen = 0;
      int startPos = -1;
      int endPos = -1;

      int targetStartHex = byteOffset * 2;
      int targetEndHex = (byteOffset + byteLength) * 2;

      const QString targetText = m_parsedTexts.value(modelId);

      for (int i = 0; i < targetText.length(); ++i) {
        QChar c = targetText[i];
        if (QString("0123456789ABCDEFabcdef").contains(c)) {
          if (hexCharsSeen == targetStartHex) {
            startPos = i;
          }
          hexCharsSeen++;
          if (hexCharsSeen == targetEndHex) {
            endPos = i + 1; // Include this character
            break;
          }
        }
      }

      if (startPos != -1 && endPos != -1) {
        emit highlightRange(startPos, endPos, modelId);
      }
    }
  }
}

QString LogBackend::getReadmeContent() {
  QStringList paths = {
      "README.md",
      ":/docs/README.md",
      "../README.md",
      "../../README.md",
      QCoreApplication::applicationDirPath() + "/README.md",
      QCoreApplication::applicationDirPath() + "/../README.md",
      "d:/Develop/Study/LogAnalyQt/LogAnalyQt/README.md" // Fallback for this
                                                         // specific env
  };

  for (const QString &path : paths) {
    QFile file(path);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream in(&file);
      in.setEncoding(QStringConverter::Utf8);
      // In-app help covers usage; build and release instructions stay in the repository.
      return in.readAll().section(QStringLiteral("\n## 📦 构建与打包"), 0, 0);
    }
  }
  return "README.md not found.";
}

QString LogBackend::getChangelogContent() {
  QStringList paths = {
      "CHANGELOG.md",
      ":/docs/CHANGELOG.md",
      "../CHANGELOG.md",
      "../../CHANGELOG.md",
      QCoreApplication::applicationDirPath() + "/CHANGELOG.md",
      QCoreApplication::applicationDirPath() + "/../CHANGELOG.md",
      "d:/Develop/Study/LogAnalyQt/LogAnalyQt/CHANGELOG.md" // Fallback for
                                                            // this specific
                                                            // env
  };

  for (const QString &path : paths) {
    QFile file(path);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream in(&file);
      in.setEncoding(QStringConverter::Utf8);
      return in.readAll();
    }
  }
  return "CHANGELOG.md not found.";
}

QString LogBackend::getAppVersion() {
  // 如果已经缓存了版本号，直接返回
  if (!m_appVersion.isEmpty()) {
    return m_appVersion;
  }

#ifdef APP_VERSION
  m_appVersion = QStringLiteral(APP_VERSION);
  return m_appVersion;
#endif

  // 读取CHANGELOG.md内容
  QString changelogContent = getChangelogContent();
  if (changelogContent == "CHANGELOG.md not found.") {
    m_appVersion = "1.0.0"; // 默认版本
    return m_appVersion;
  }

  // 使用正则表达式匹配第一个版本号 [x.y.z]
  QRegularExpression versionRegex(R"(\[([0-9]+\.[0-9]+\.[0-9]+)\])");
  QRegularExpressionMatch match = versionRegex.match(changelogContent);

  if (match.hasMatch()) {
    m_appVersion = match.captured(1);
  } else {
    m_appVersion = "1.0.0"; // 如果没有匹配到，使用默认版本
  }

  return m_appVersion;
}

QString LogBackend::appVersion() const {
  if (m_appVersion.isEmpty()) {
    // 调用非const版本来初始化
    const_cast<LogBackend *>(this)->getAppVersion();
  }
  return m_appVersion;
}

int LogBackend::protocolType() const { return m_protocolType; }

void LogBackend::initializeSecurityControl() {
  if (!kSecurityControlEnabled) {
    clearSecurityControlEnforcement();
    return;
  }

  const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
  SecurityStateStore stateStore;
  const SecurityStateReadResult readResult = stateStore.readState(nowUtc);

  m_securityTamperCountSeed = 0;
  m_securityMinimumAllowedUtc = QDateTime();
  m_securityInstallIdHint.clear();

  if (readResult.hasTrustedState) {
    m_securityTamperCountSeed = readResult.nextTamperCount;
    m_securityMinimumAllowedUtc = readResult.state.maxSeenUtc.toUTC();
    m_securityInstallIdHint = readResult.state.installId;
  }

  if (readResult.status == SecurityStateReadStatus::Valid) {
    QString touchError;
    if (!stateStore.touchState(readResult.state, nowUtc, &touchError)) {
      startSecurityControlEnforcement(
          SecurityControlTrigger::StateWriteFailed,
          readResult.state.lastVerifiedUtc,
          QString("刷新本地安全状态失败：%1").arg(touchError));
      attemptSecurityControlVerification();
      return;
    }

    m_securityMinimumAllowedUtc = nowUtc;
    if (!isSecurityVerificationExpired(readResult.state.lastVerifiedUtc)) {
      clearSecurityControlEnforcement();
      return;
    }

    startSecurityControlEnforcement(SecurityControlTrigger::Expired,
                                    readResult.state.lastVerifiedUtc);
    attemptSecurityControlVerification();
    return;
  }

  if (readResult.status == SecurityStateReadStatus::NoState) {
    const QDateTime legacyTime = loadLegacySecurityVerificationTime();
    if (legacyTime.isValid() && !isSecurityVerificationExpired(legacyTime)) {
      QString migrationError;
      if (stateStore.persistVerifiedState(legacyTime, nowUtc, 0, QString(),
                                         &migrationError)) {
        clearLegacySecurityVerificationTime();
        m_securityMinimumAllowedUtc = nowUtc;
        clearSecurityControlEnforcement();
        return;
      }

      startSecurityControlEnforcement(
          SecurityControlTrigger::StateWriteFailed, legacyTime,
          QString("旧版本校验时间迁移失败：%1").arg(migrationError));
      attemptSecurityControlVerification();
      return;
    }

    if (legacyTime.isValid()) {
      startSecurityControlEnforcement(
          SecurityControlTrigger::Expired, legacyTime,
          QString("检测到旧版本校验记录，但仍需完成一次新的内网校验以升级本地安全状态。"));
    } else {
      startSecurityControlEnforcement(SecurityControlTrigger::NoRecord,
                                      QDateTime());
    }
    attemptSecurityControlVerification();
    return;
  }

  SecurityControlTrigger trigger = SecurityControlTrigger::StateCorrupted;
  switch (readResult.status) {
  case SecurityStateReadStatus::CopyMissing:
    trigger = SecurityControlTrigger::CopyMissing;
    break;
  case SecurityStateReadStatus::StateMismatch:
    trigger = SecurityControlTrigger::StateMismatch;
    break;
  case SecurityStateReadStatus::TimeRollback:
    trigger = SecurityControlTrigger::TimeRollback;
    break;
  case SecurityStateReadStatus::UnsupportedPlatform:
    trigger = SecurityControlTrigger::UnsupportedPlatform;
    break;
  case SecurityStateReadStatus::StateCorrupted:
    trigger = SecurityControlTrigger::StateCorrupted;
    break;
  case SecurityStateReadStatus::Valid:
  case SecurityStateReadStatus::NoState:
    break;
  }

  startSecurityControlEnforcement(trigger,
                                  readResult.hasTrustedState
                                      ? readResult.state.lastVerifiedUtc
                                      : QDateTime(),
                                  readResult.detail);
  attemptSecurityControlVerification();
}

void LogBackend::startSecurityControlEnforcement(
    SecurityControlTrigger trigger, const QDateTime &lastVerifiedTime,
    const QString &detail) {
  m_securityControlTrigger = trigger;
  m_securityControlActive = true;
  m_securityControlMessage =
      buildSecurityControlMessage(trigger, lastVerifiedTime, detail);
  m_securityControlRemainingSeconds = SECURITY_CONTROL_FORCE_EXIT_SECONDS;

  if (m_securityCountdownTimer)
    m_securityCountdownTimer->start();
  if (m_securityCloseTimer)
    m_securityCloseTimer->start(SECURITY_CONTROL_FORCE_EXIT_SECONDS * 1000);

  emit securityControlStateChanged();
  emit securityControlCountdownChanged();
}

void LogBackend::clearSecurityControlEnforcement() {
  m_securityVerificationPending = false;
  m_securityStatusProbePending = false;
  if (m_securityCloseTimer)
    m_securityCloseTimer->stop();
  if (m_securityCountdownTimer)
    m_securityCountdownTimer->stop();

  const bool wasActive = m_securityControlActive;
  const bool hadMessage = !m_securityControlMessage.isEmpty();
  const bool hadCountdown = m_securityControlRemainingSeconds != 0;

  m_securityControlActive = false;
  m_securityControlTrigger = SecurityControlTrigger::None;
  m_securityControlMessage.clear();
  m_securityControlRemainingSeconds = 0;

  if (wasActive || hadMessage)
    emit securityControlStateChanged();
  if (hadCountdown)
    emit securityControlCountdownChanged();
}

void LogBackend::attemptSecurityControlVerification() {
  if (!m_securityControlActive)
    return;

#ifdef Q_OS_MACOS
  if (m_protocolSyncManager->busy() || m_protocolSyncManager->connecting())
    return;
  m_securityStatusProbePending = true;
  if (!m_protocolSyncManager->fetchSyncStatus())
    m_securityStatusProbePending = false;
#else
  if (m_protocolSyncManager->fetchSyncStatus())
    return;

  if (m_protocolSyncManager->connecting())
    return;

  m_securityVerificationPending = true;
  m_protocolSyncManager->connectToShare();
#endif
}

void LogBackend::recordSuccessfulSecurityVerification() {
  if (!kSecurityControlEnabled)
    return;

  const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
  if (m_securityMinimumAllowedUtc.isValid() &&
      nowUtc < m_securityMinimumAllowedUtc) {
    qWarning() << "安全管控校验成功，但系统时间仍早于可信时间上界，拒绝更新本地状态";
    if (m_securityControlActive) {
      m_securityControlTrigger = SecurityControlTrigger::TimeRollback;
      m_securityControlMessage = buildSecurityControlMessage(
          SecurityControlTrigger::TimeRollback, QDateTime(),
          QString("当前系统时间仍早于最近一次可信启动时间，请先校正系统时间后再重试。"));
      emit securityControlStateChanged();
    }
    emit statusMessage("检测到系统时间异常，请校正后再完成安全校验", 5000);
    return;
  }

  SecurityStateStore stateStore;
  QString writeError;
  if (!stateStore.persistVerifiedState(nowUtc, nowUtc, m_securityTamperCountSeed,
                                       m_securityInstallIdHint, &writeError)) {
    qWarning() << "安全管控写入本地密封状态失败:" << writeError;
    if (m_securityControlActive) {
      m_securityControlTrigger = SecurityControlTrigger::StateWriteFailed;
      m_securityControlMessage = buildSecurityControlMessage(
          SecurityControlTrigger::StateWriteFailed, nowUtc,
          QString("已成功获取 XML 列表，但本地安全状态双写失败：%1")
              .arg(writeError));
      emit securityControlStateChanged();
    }
    emit statusMessage("已获取 XML 列表，但本地安全状态更新失败", 5000);
    return;
  }

  clearLegacySecurityVerificationTime();
  m_securityMinimumAllowedUtc = nowUtc;

  if (!m_securityControlActive) {
    emit statusMessage("已完成内网服务器安全校验", 3000);
    return;
  }

    clearSecurityControlEnforcement();
  emit statusMessage("已完成内网服务器安全校验", 3000);
}

bool LogBackend::isSecurityVerificationExpired(
    const QDateTime &lastVerifiedTime) const {
  if (!lastVerifiedTime.isValid())
    return true;

  const QDateTime lastVerifiedUtc = lastVerifiedTime.toUTC();
  const QDateTime now = QDateTime::currentDateTimeUtc();
  if (lastVerifiedUtc > now)
    return true;

  return lastVerifiedUtc.addDays(SECURITY_CONTROL_REQUIRED_DAYS) <= now;
}

QDateTime LogBackend::loadLegacySecurityVerificationTime() const {
  auto settings = deviceStateSettings();
  const QVariant storedValue = settings.value(kLegacySecurityLastVerifiedKey);
  if (!storedValue.isValid())
    return QDateTime();

  QDateTime verifiedTime =
      QDateTime::fromString(storedValue.toString(), Qt::ISODate);
  if (!verifiedTime.isValid())
    verifiedTime = storedValue.toDateTime();
  if (!verifiedTime.isValid())
    return QDateTime();

  return verifiedTime.toUTC();
}

void LogBackend::clearLegacySecurityVerificationTime() {
  auto settings = deviceStateSettings();
  settings.remove(kLegacySecurityLastVerifiedKey);
  settings.sync();
}

QString LogBackend::buildSecurityControlMessage(
    SecurityControlTrigger trigger, const QDateTime &lastVerifiedTime,
    const QString &detail) const {
  QString reason;
  switch (trigger) {
  case SecurityControlTrigger::NoRecord:
    reason = "当前未检测到可信的本地安全状态记录。";
    break;
  case SecurityControlTrigger::Expired:
    reason = "本地安全状态已超期，需要重新完成一次内网校验。";
    break;
  case SecurityControlTrigger::CopyMissing:
    reason = "本地安全状态副本不完整，已按疑似篡改处理。";
    break;
  case SecurityControlTrigger::StateCorrupted:
    reason = "本地安全状态损坏或无法解封，已按疑似篡改处理。";
    break;
  case SecurityControlTrigger::StateMismatch:
    reason = "程序目录主记录与用户本地副本不一致，已按疑似篡改处理。";
    break;
  case SecurityControlTrigger::TimeRollback:
    reason = "检测到系统时间回拨，当前状态不接受本次运行。";
    break;
  case SecurityControlTrigger::StateWriteFailed:
    reason = "本地安全状态写入失败，无法建立可信记录。";
    break;
  case SecurityControlTrigger::UnsupportedPlatform:
    reason = "当前平台不支持 Windows 密封状态校验。";
    break;
  case SecurityControlTrigger::None:
    reason = "本地安全状态不可用。";
    break;
  }

  QString message = QString(
      "安全管控要求：程序必须每 %1 天至少成功连接一次内网规约服务器并获取 XML 列表。"
      "若 60 秒内仍未完成可接受的校验，程序将自动关闭。")
                        .arg(SECURITY_CONTROL_REQUIRED_DAYS);

  message += QString("\n当前处理结果：%1").arg(reason);
  if (!detail.trimmed().isEmpty())
    message += QString("\n详情：%1").arg(detail.trimmed());

  if (trigger == SecurityControlTrigger::TimeRollback) {
    message += "\n请先校正系统时间，再重新执行内网校验。";
  } else {
    message += "\n可按 Ctrl+Shift+S 打开协议同步窗口完成校验。";
  }

  if (lastVerifiedTime.isValid()) {
    message += QString("\n最近一次成功校验时间：%1")
                   .arg(lastVerifiedTime.toLocalTime().toString(
                       "yyyy-MM-dd HH:mm:ss"));
  } else {
    message += "\n尚未记录到成功校验时间。";
  }

  message +=
      "\n在倒计时结束前，只要成功连接内网规约服务器并获取到 XML 列表，且本地安全状态成功写回，限制会自动解除。";

  return message;
}

QVariant LogBackend::getUiSetting(const QString &key,
                                  const QVariant &defaultValue) {
  auto settings = appSettings();
  return settings.value(QString("ui/%1").arg(key), defaultValue);
}

bool LogBackend::setUiSetting(const QString &key, const QVariant &value) {
  auto settings = appSettings();
  settings.setValue(QString("ui/%1").arg(key), value);
  settings.sync();
  return settings.status() == QSettings::NoError;
}

void LogBackend::setProtocolType(int type) {
  if (m_protocolType != type) {
    m_protocolType = type;
    emit protocolTypeChanged();
    emit currentProtocolNameChanged();

    // 保存配置
    saveSettings();
  }
}

QString LogBackend::currentProtocolName() const {
  ProtocolType type = ProtocolType::Unknown;

  // 确定当前使用的协议类型
  if (m_protocolType == 0) { // Auto
    if (m_lastDetectedProtocol == ProtocolType::Unknown) {
      return "自动检测";
    }
    type = m_lastDetectedProtocol;
  } else {
    type = static_cast<ProtocolType>(m_protocolType);
  }

  QString name = ProtocolDetector::getProtocolName(type);
  QString version;
  QString date;

  // 根据协议类型获取版本信息
  switch (type) {
  case ProtocolType::ProtocolModuleMsg:
    version = ModuleMsg::ModuleMsgLoader::instance().getVersion();
    date = ModuleMsg::ModuleMsgLoader::instance().getDate();
    break;
  case ProtocolType::ProtocolAppMsg:
    version = AppMsg::AppMsgLoader::instance().getVersion();
    date = AppMsg::AppMsgLoader::instance().getDate();
    break;
  case ProtocolType::Protocol3762:
    version = Prot3762ConfigManager::instance().getVersion();
    date = Prot3762ConfigManager::instance().getDate();
    break;
  case ProtocolType::Protocol645:
    version = Prot645ConfigManager::instance().getVersion();
    date = Prot645ConfigManager::instance().getDate();
    break;
  case ProtocolType::ProtocolCsgUpstream:
    version = CsgUpstreamConfigManager::instance().getVersion();
    date = CsgUpstreamConfigManager::instance().getDate();
    break;
  case ProtocolType::Protocol698:
    version = Prot698OadManager::instance().getVersion();
    date = Prot698OadManager::instance().getDate();
    break;
  default:
    break;
  }

  // 如果有版本信息，追加到名称后面
  if (!version.isEmpty()) {
    name += QString(" (%1 %2)").arg(version, date);
  }

  return name;
}

void LogBackend::loadSettings() {
  auto settings = appSettings();
  m_protocolType = settings.value("protocolType", 0).toInt(); // 默认自动检测
}

void LogBackend::saveSettings() {
  auto settings = appSettings();
  settings.setValue("protocolType", m_protocolType);
  settings.sync();
}

// 扫描全局预览 - 扫描整个日志文档并提取所有报文的概要信息
void LogBackend::scanGlobalPreview() {
  // 清空之前的数据
  m_globalPreviewModel->clear();

  // 重置最后检测到的协议
  m_lastDetectedProtocol = ProtocolType::Unknown;
  emit currentProtocolNameChanged();

  // 如果没有加载文件内容，直接返回
  if (m_currentFileContent.isEmpty()) {
    emit statusMessage("没有加载日志文件", 3000);
    return;
  }

  emit statusMessage("正在扫描日志文档...", 0);

  // 按行分割日志内容
  QStringList lines = m_currentFileContent.split('\n');

  // Initialize progress variables
  m_totalLines = lines.size();
  m_scannedLines = 0;
  m_scanProgress = 0.0;
  m_unknownLines = 0;
  emit scanProgressChanged();

  // 用于提取时间戳的正则表达式
  QRegularExpression timeRegex(
      R"((\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}|\[\d{2}:\d{2}:\d{2}\.\d{3}\]|\d{2}:\d{2}:\d{2}|\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3}))");

  // 用于提取十六进制数据的正则表达式
  QRegularExpression hexRegex(
      R"([\(\[\{<]?([0-9A-Fa-f]{2}(?:\s+[0-9A-Fa-f]{2})+|[0-9A-Fa-f]{4,}|E5|e5)[\)\]\}>]?)");

  int processedCount = 0;

  for (int lineNum = 0; lineNum < lines.size(); ++lineNum) {
    // Update progress
    m_scannedLines = lineNum + 1;
    m_scanProgress = (double)m_scannedLines / m_totalLines;
    if (lineNum % 100 == 0) {
      emit scanProgressChanged();
      QCoreApplication::processEvents();
    }

    QString line = lines[lineNum].trimmed();
    if (line.isEmpty())
      continue;

    // 提取时间戳
    QString timestamp;
    QRegularExpressionMatch timeMatch = timeRegex.match(line);
    if (timeMatch.hasMatch()) {
      timestamp = timeMatch.captured(1);
    }

    // 提取十六进制数据
    auto matchIterator = hexRegex.globalMatch(line);
    bool foundInLine = false;

    while (matchIterator.hasNext()) {
      QRegularExpressionMatch hexMatch = matchIterator.next();
      QString hexData = hexMatch.captured(1);

      // 清理十六进制数据
      QString cleanHex = hexData;
      cleanHex.remove(" ");
      cleanHex.remove("\n");
      cleanHex.remove("\r");
      cleanHex.remove("\t");

      // 如果清理后的长度不是偶数，或者长度太短（除了E5），跳过
      if (cleanHex.length() % 2 != 0 ||
          (cleanHex.length() < 4 && cleanHex.toUpper() != "E5")) {
        continue;
      }

      // 转换为字节数组
      QByteArray data = QByteArray::fromHex(cleanHex.toUtf8());
      if (data.isEmpty())
        continue;

      // 使用自动检测识别协议，并获取有效帧偏移量
      int frameOffset = 0;
      ProtocolType detectedType =
          ProtocolDetector::detectProtocol(data, &frameOffset);
      if (detectedType == ProtocolType::Unknown)
        continue;

      // 如果检测到的帧不是从offset=0开始，截取有效数据
      if (frameOffset > 0 && frameOffset < data.size()) {
        data = data.mid(frameOffset);
        // 同时更新cleanHex以反映截取后的数据
        cleanHex = data.toHex();
      }

      // 识别到了有效协议！
      foundInLine = true;

      // 格式化十六进制字符串用于显示
      QString formattedHex;
      for (int i = 0; i < cleanHex.length(); i += 2) {
        if (i > 0)
          formattedHex += " ";
        formattedHex += cleanHex.mid(i, 2);
      }

      // 创建概要信息项
      GlobalPreviewItem item;
      item.timestamp = timestamp;
      item.lineNumber = lineNum;
      item.rawData = formattedHex;
      item.protocol = ProtocolDetector::getProtocolName(detectedType);
      item.direction = "-";
      item.address = "-";
      item.frameSeq = "-";
      item.messageType = "已识别报文";

      // 根据不同协议提取详细信息
      switch (detectedType) {
      case ProtocolType::Protocol698: {
        ProtocolSummary summary = Prot698Parser::getSummary(data);
        item.address = summary.address;
        item.direction = summary.direction;
        item.frameSeq = summary.frameSeq;
        item.messageType = summary.messageType;
        break;
      }
      case ProtocolType::Protocol645: {
        ProtocolSummary summary = Prot645Parser::getSummary(data);
        item.address = summary.address;
        item.direction = summary.direction;
        item.frameSeq = summary.frameSeq;
        item.messageType = summary.messageType;
        break;
      }
      case ProtocolType::ProtocolCsgUpstream: {
        const ProtocolSummary summary = CsgUpstreamParser::getSummary(data);
        item.address = summary.address;
        item.direction = summary.direction;
        item.frameSeq = summary.frameSeq;
        item.messageType = summary.messageType;
        break;
      }
      case ProtocolType::Protocol104: {
        ProtocolSummary summary = Prot104Parser::getSummary(data);
        item.address = summary.address;
        item.direction = summary.direction;
        item.frameSeq = summary.frameSeq;
        item.messageType = summary.messageType;
        break;
      }
      case ProtocolType::Protocol101: {
        ProtocolSummary summary = Prot101Parser::getSummary(data);
        item.address = summary.address;
        item.direction = summary.direction;
        item.frameSeq = summary.frameSeq;
        item.messageType = summary.messageType;
        break;
      }
      case ProtocolType::Protocol3762: {
        ProtocolSummary summary = Prot3762Parser::getSummary(data);
        item.address = summary.address;
        item.direction = summary.direction;
        item.frameSeq = summary.frameSeq;
        item.messageType = summary.messageType;
        break;
      }
      case ProtocolType::ProtocolAppMsg: {
        ProtocolSummary summary = AppMsg::AppMsgParser::getSummary(data);
        item.address = summary.address;
        item.direction = summary.direction;
        item.frameSeq = summary.frameSeq;
        item.messageType = summary.messageType;
        break;
      }
      case ProtocolType::ProtocolModuleMsg: {
        ProtocolSummary summary = ModuleMsg::ModuleMsgParser::getSummary(data);
        item.address = summary.address;
        item.direction = summary.direction;
        item.frameSeq = summary.frameSeq;
        item.messageType = summary.messageType;
        break;
      }
      default:
        break;
      }

      m_globalPreviewModel->addItem(item);
      processedCount++;
    }
  }

  m_scannedLines = m_totalLines;
  m_scanProgress = 1.0;
  emit scanProgressChanged();
  emit statusMessage(QString("扫描完成，共识别 %1 条报文").arg(processedCount),
                     3000);
}

QList<int> LogBackend::getLineRange(int lineNumber) {
  if (lineNumber < 0 || m_currentFileContent.isEmpty()) {
    return {-1, -1};
  }

  int start = 0;

  // Find the start of the line
  for (int i = 0; i < lineNumber; ++i) {
    int next = m_currentFileContent.indexOf('\n', start);
    if (next == -1) {
      return {-1, -1}; // Line number out of range
    }
    start = next + 1;
  }

  // Find the end of the line
  int end = m_currentFileContent.indexOf('\n', start);
  if (end == -1) {
    end = m_currentFileContent.length();
  }

  // 处理 Windows 换行符 \r\n：排除末尾的 \r
  if (end > start && m_currentFileContent[end - 1] == '\r') {
    end--;
  }

  return {start, end};
}

void LogBackend::disableTextAreaUndo(QObject *quickTextDocument) {
  // 禁用 QML TextArea 底层 QTextDocument 的 undo/redo 功能。
  // 开启 undo 时，QTextDocument 会为每次 insert()/remove() 记录一条 undo 条目并带入完整文本岁片，
  // 实时日志每 100ms flush 一次，长期运行后 undo 历史就会占用大量内存。
  if (auto *qtd = qobject_cast<QQuickTextDocument *>(quickTextDocument)) {
    if (QTextDocument *doc = qtd->textDocument()) {
      doc->setUndoRedoEnabled(false);
    }
  }
}

// --- 预置报文管理方法实现 ---

QStringList LogBackend::getPresetList(const QString &protocol) {
  return m_presetManager->getMessagesByProtocol(protocol);
}

QString LogBackend::getPresetContent(const QString &protocol,
                                     const QString &summary) {
  return m_presetManager->getMessageContent(protocol, summary);
}

bool LogBackend::addPresetMessage(const QString &protocol,
                                  const QString &summary,
                                  const QString &content) {
  bool result = m_presetManager->addMessage(protocol, summary, content);
  if (result) {
    emit statusMessage("预置报文添加成功", 2000);
  } else {
    emit statusMessage("预置报文添加失败，可能已存在相同名称的报文", 3000);
  }
  return result;
}

bool LogBackend::removePresetMessage(const QString &protocol,
                                     const QString &summary) {
  bool result = m_presetManager->removeMessage(protocol, summary);
  if (result) {
    emit statusMessage("预置报文删除成功", 2000);
  } else {
    emit statusMessage("预置报文删除失败", 3000);
  }
  return result;
}

bool LogBackend::updatePresetMessage(const QString &protocol,
                                     const QString &oldSummary,
                                     const QString &newSummary,
                                     const QString &newContent) {
  bool result = m_presetManager->updateMessage(protocol, oldSummary, newSummary,
                                               newContent);
  if (result) {
    emit statusMessage("预置报文修改成功", 2000);
  } else {
    emit statusMessage("预置报文修改失败，可能已存在相同名称的报文", 3000);
  }
  return result;
}

QVariantList LogBackend::listSmartFramingProtocols() {
  QVariantList result;
  QVariantMap prot698;
  prot698["code"] = "698";
  prot698["name"] = "DL/T 698";
  prot698["description"] = "698 APDU 服务树";
  prot698["protocolType"] = static_cast<int>(ProtocolType::Protocol698);
  result.append(prot698);

  QVariantMap app;
  app["code"] = "app";
  app["name"] = "应用层消息";
  app["description"] = "APP IID/IOP 消息";
  app["protocolType"] = static_cast<int>(ProtocolType::ProtocolAppMsg);
  result.append(app);
  return result;
}

QVariantList LogBackend::list698ServiceTypes() {
  return Prot698ServiceSchema::serviceTypes();
}

QVariantMap LogBackend::get698ServiceSchema(const QString &serviceCode) {
  return Prot698ServiceSchema::schemaForService(serviceCode);
}

QVariantMap LogBackend::getOadSchema(const QString &oiHex, int attrId,
                                     bool isMethod, int dataIndex) {
  Prot698SubItem *definition =
      isMethod ? Prot698OadManager::instance().getMethodDataDef(oiHex, attrId)
               : Prot698OadManager::instance().getAttrDataDef(oiHex, attrId,
                                                               dataIndex);
  if (!definition)
    return {};
  return Prot698DataEncoder::subItemToSchema(*definition);
}

QVariantList LogBackend::listAllOad(const QString &filterText) {
  return Prot698OadManager::instance().listAllOi(filterText);
}

QString LogBackend::build698ApduFromTree(const QString &serviceCode,
                                         const QVariantMap &treeValue) {
  QString err;
  QVariantMap spec =
      Prot698ServiceSchema::toServiceSpec(serviceCode, treeValue, &err);
  if (!err.isEmpty())
    return QString("ERR: %1").arg(err);

  QByteArray apdu = Prot698ServiceBuilder::buildFromSpec(spec, &err);
  if (!err.isEmpty() || apdu.isEmpty())
    return QString("ERR: %1").arg(err.isEmpty() ? "APDU 构建失败" : err);
  return QString::fromLatin1(apdu.toHex().toUpper());
}

QString LogBackend::buildFullFrame(const QString &apduHex,
                                   const QVariantMap &frameOptions) {
  QString cleanHex = apduHex;
  cleanHex.remove(' ');
  cleanHex.remove('\n');
  cleanHex.remove('\r');
  cleanHex.remove('\t');
  if (cleanHex.isEmpty() || cleanHex.size() % 2 != 0 ||
      !QRegularExpression(QStringLiteral("^[0-9a-fA-F]+$")).match(cleanHex).hasMatch())
    return "ERR: APDU 必须是完整、有效的十六进制字节";
  QByteArray apdu = QByteArray::fromHex(cleanHex.toLatin1());
  if (apdu.isEmpty())
    return "ERR: APDU 为空或不是有效十六进制";

  QString err;
  FrameOptions options = frameOptions.isEmpty()
                             ? Prot698FrameBuilder::defaultClientRequestOptions(
                                   m_current698PhysicalAddr)
                             : Prot698FrameBuilder::optionsFromMap(frameOptions, &err);
  if (!err.isEmpty())
    return QString("ERR: %1").arg(err);
  if (options.serverAddr.isEmpty()) {
    const auto defaults = Prot698FrameBuilder::defaultClientRequestOptions(
        m_current698PhysicalAddr);
    if (!defaults.validationError.isEmpty())
      return QString("ERR: %1").arg(defaults.validationError);
    options.serverAddr = defaults.serverAddr;
  }

  QByteArray frame = Prot698FrameBuilder::buildFrame(apdu, options, &err);
  if (!err.isEmpty() || frame.isEmpty())
    return QString("ERR: %1").arg(err.isEmpty() ? "完整帧构建失败" : err);
  return QString::fromLatin1(frame.toHex().toUpper());
}

QVariantList LogBackend::verifyHexData(const QString &hexText) {
  QString cleanHex = hexText;
  cleanHex.remove(' ');
  cleanHex.remove('\n');
  cleanHex.remove('\r');
  cleanHex.remove('\t');
  QByteArray data = QByteArray::fromHex(cleanHex.toLatin1());
  if (data.isEmpty())
    return {};

  Prot698Parser parser;
  QList<Prot698Field> fields = parser.parse(data);
  QVariantList result;
  for (const Prot698Field &field : fields) {
    QVariantMap item;
    item["name"] = field.name;
    item["value"] = field.value;
    item["description"] = field.description;
    item["indent"] = field.indent;
    item["offset"] = field.offset;
    item["length"] = field.length;
    result.append(item);
  }
  return result;
}

QString LogBackend::getCurrent698Address() const {
  return m_current698PhysicalAddr;
}

int LogBackend::getNextPiid() {
  m_piidCounter = (m_piidCounter % 63) + 1;
  return m_piidCounter;
}

QVariantList LogBackend::listAppInterfaceGroups() {
  return AppMsg::AppMsgServiceSchema::interfaceGroups();
}

QVariantList LogBackend::listAppInterfaces(int iid, const QString &kind,
                                           const QString &filterText) {
  return AppMsg::AppMsgServiceSchema::interfaces(iid, kind, filterText);
}

QVariantMap LogBackend::getAppInterfaceSchema(int iid, int iop,
                                              const QString &kind) {
  return AppMsg::AppMsgServiceSchema::schemaForInterface(iid, iop, kind);
}

QString LogBackend::buildAppPayloadFromTree(int iid, int iop,
                                            const QString &kind,
                                            const QVariant &treeValue) {
  QString err;
  QByteArray payload = AppMsg::AppMsgBuilder::buildPayload(iid, iop, kind,
                                                           treeValue, &err);
  if (!err.isEmpty())
    return QString("ERR: %1").arg(err);
  return QString::fromLatin1(payload.toHex().toUpper());
}

QString LogBackend::buildAppMessageFromTree(int iid, int iop,
                                            const QString &kind,
                                            const QVariant &treeValue,
                                            const QVariantMap &headerOptions) {
  QString err;
  QByteArray message = AppMsg::AppMsgBuilder::buildMessage(
      iid, iop, kind, treeValue, headerOptions, &err);
  if (!err.isEmpty())
    return QString("ERR: %1").arg(err);
  return QString::fromLatin1(message.toHex().toUpper());
}

QVariantList LogBackend::verifySmartFrame(const QString &protocolCode,
                                          const QString &hexText) {
  QString code = protocolCode.trimmed().toLower();
  if (code == "698")
    return verifyHexData(hexText);

  QString cleanHex = hexText;
  cleanHex.remove(' ');
  cleanHex.remove('\n');
  cleanHex.remove('\r');
  cleanHex.remove('\t');
  QByteArray data = QByteArray::fromHex(cleanHex.toLatin1());
  if (data.isEmpty())
    return {};

  QVariantList result;
  if (code == "app") {
    AppMsg::AppMsgParser parser;
    QList<AppMsg::AppMsgField> fields = parser.parseToFields(data);
    for (const AppMsg::AppMsgField &field : fields) {
      QVariantMap item;
      item["name"] = field.name;
      item["value"] = field.value;
      item["description"] = field.description;
      item["indent"] = field.indent;
      item["offset"] = field.offset;
      item["length"] = field.length;
      result.append(item);
    }
  }
  return result;
}

QStringList LogBackend::listFramingTemplates() {
  return appSettings().value("framingTemplates").toMap().keys();
}
QString LogBackend::loadFramingTemplate(const QString &name) {
  const auto value = appSettings().value("framingTemplates").toMap().value(name);
  return value.isValid() ? QString::fromUtf8(QJsonDocument::fromVariant(value).toJson()) : QString();
}
bool LogBackend::saveFramingTemplate(const QString &name, const QString &json) {
  if (name.trimmed().isEmpty() || name.contains('/') || name.contains('\\') || name.contains("..")) return false;
  QJsonParseError parse; const auto doc = QJsonDocument::fromJson(json.toUtf8(), &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject()) return false;
  auto settings = appSettings(); settings.setValue("framingTemplates/" + name, doc.object().toVariantMap()); settings.sync();
  return settings.status() == QSettings::NoError;
}
bool LogBackend::deleteFramingTemplate(const QString &name) {
  if (name.isEmpty() || name.contains('/') || name.contains('\\') || name.contains("..")) return false;
  auto settings = appSettings(); settings.remove("framingTemplates/" + name); settings.sync();
  return settings.status() == QSettings::NoError;
}
