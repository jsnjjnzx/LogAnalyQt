#include "LogMerger.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTextStream>
#include <QUrl>
#include <algorithm>

LogMerger::LogMerger(QObject *parent)
    : QObject(parent), m_merging(false), m_progress(0.0) {}

QStringList LogMerger::fileList() const { return m_fileList; }

QString LogMerger::outputPath() const { return m_outputPath; }

void LogMerger::setOutputPath(const QString &path) {
  if (m_outputPath != path) {
    m_outputPath = path;
    emit outputPathChanged();
  }
}

bool LogMerger::merging() const { return m_merging; }

double LogMerger::progress() const { return m_progress; }

void LogMerger::updateProgress(double p) {
  if (m_progress != p) {
    m_progress = p;
    emit progressChanged();
  }
}

void LogMerger::addFiles(const QStringList &urls) {
  bool changed = false;
  for (const QString &urlStr : urls) {
    QUrl url(urlStr);
    QString localPath = url.isLocalFile() ? url.toLocalFile() : urlStr;

    // 清理路径中的 "file:///" 前缀（如果是无效的 URL 但包含前缀）
    if (localPath.startsWith("file:///")) {
      localPath = localPath.mid(8);
    }

    if (!localPath.isEmpty() && !m_fileList.contains(localPath)) {
      m_fileList.append(localPath);
      changed = true;
    }
  }

  if (changed) {
    emit fileListChanged();
  }
}

void LogMerger::removeFile(int index) {
  if (index >= 0 && index < m_fileList.size()) {
    m_fileList.removeAt(index);
    emit fileListChanged();
  }
}

void LogMerger::clearFiles() {
  if (!m_fileList.isEmpty()) {
    m_fileList.clear();
    emit fileListChanged();
  }
}

void LogMerger::moveFileUp(int index) {
  if (index > 0 && index < m_fileList.size()) {
    m_fileList.swapItemsAt(index, index - 1);
    emit fileListChanged();
  }
}

void LogMerger::moveFileDown(int index) {
  if (index >= 0 && index < m_fileList.size() - 1) {
    m_fileList.swapItemsAt(index, index + 1);
    emit fileListChanged();
  }
}

QDateTime LogMerger::parseTimestamp(const QString &timestampStr) {
  QDateTime dt;
  QString cleanStr = timestampStr.trimmed();

  // 移除方括号 (e.g., [08:55:24.123] -> 08:55:24.123)
  if (cleanStr.startsWith("[") && cleanStr.endsWith("]")) {
    cleanStr = cleanStr.mid(1, cleanStr.length() - 2);
  }

  // 1. yyyy-MM-dd HH:mm:ss.zzz (or .zzzzzz)
  if (cleanStr.length() >= 23 && cleanStr[4] == '-' && cleanStr[7] == '-' &&
      cleanStr[10] == ' ' && cleanStr[13] == ':' && cleanStr[16] == ':' &&
      cleanStr[19] == '.') {
    QString formatStr = cleanStr;
    // 如果有微秒，截取到毫秒
    if (cleanStr.length() > 23) {
      formatStr = cleanStr.left(23);
    }
    dt = QDateTime::fromString(formatStr, "yyyy-MM-dd HH:mm:ss.zzz");
    if (dt.isValid())
      return dt;
  }

  // 2. yyyy-MM-dd HH:mm:ss
  if (cleanStr.length() == 19 && cleanStr[4] == '-' && cleanStr[7] == '-' &&
      cleanStr[10] == ' ' && cleanStr[13] == ':' && cleanStr[16] == ':') {
    dt = QDateTime::fromString(cleanStr, "yyyy-MM-dd HH:mm:ss");
    if (dt.isValid())
      return dt;
  }

  // 3. MM-dd HH:mm:ss.zzz
  if (cleanStr.length() == 18 && cleanStr[2] == '-' && cleanStr[5] == ' ' &&
      cleanStr[8] == ':' && cleanStr[11] == ':' && cleanStr[14] == '.') {
    dt = QDateTime::fromString(cleanStr, "MM-dd HH:mm:ss.zzz");
    if (dt.isValid()) {
      // 补充当前年份
      QDate cd = QDate::currentDate();
      QDate d = dt.date();
      dt.setDate(QDate(cd.year(), d.month(), d.day()));
      return dt;
    }
  }

  // 4. HH:mm:ss.zzz
  if (cleanStr.length() == 12 && cleanStr[2] == ':' && cleanStr[5] == ':' &&
      cleanStr[8] == '.') {
    QTime t = QTime::fromString(cleanStr, "HH:mm:ss.zzz");
    if (t.isValid()) {
      dt.setDate(
          QDate(1970, 1, 1)); // Placeholder date to allow sorting by time
      dt.setTime(t);
      return dt;
    }
  }

  // 5. HH:mm:ss
  if (cleanStr.length() == 8 && cleanStr[2] == ':' && cleanStr[5] == ':') {
    QTime t = QTime::fromString(cleanStr, "HH:mm:ss");
    if (t.isValid()) {
      dt.setDate(QDate(1970, 1, 1)); // Placeholder date
      dt.setTime(t);
      return dt;
    }
  }

  return dt;
}

QList<LogMerger::LogEntry> LogMerger::parseLogFile(const QString &filePath,
                                                   int fileIndex) {
  QList<LogEntry> entries;
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return entries;
  }

  // 检查编码 (BOM detection and fallback)
  QByteArray header = file.read(4);
  file.seek(0);

  auto encoding = QStringConverter::Utf8;
  if (header.startsWith("\xEF\xBB\xBF")) {
    encoding = QStringConverter::Utf8;
  } else if (header.startsWith("\xFF\xFE") || header.startsWith("\xFE\xFF")) {
    encoding = QStringConverter::Utf16;
  } else {
    // GBK fallback logic could be added here, but QTextStream defaults to local
    // 8-bit or UTF-8. Using System for generic logs is often safer on Windows.
    encoding = QStringConverter::System;
  }

  QTextStream in(&file);
  in.setEncoding(encoding);

  // 支持 6 种格式的正则
  // 1. yyyy-MM-dd HH:mm:ss.zzzzzz (微秒)
  // 2. yyyy-MM-dd HH:mm:ss.zzz (毫秒)
  // 3. yyyy-MM-dd HH:mm:ss (无毫秒)
  // 4. MM-dd HH:mm:ss.zzz (无年份)
  // 5. [HH:mm:ss.zzz] (方括号)
  // 6. HH:mm:ss (纯时间)
  QRegularExpression timeRegex(
      R"((\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3,6}|\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}|\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3}|\[\d{2}:\d{2}:\d{2}\.\d{3}\]|\d{2}:\d{2}:\d{2}))");

  int lineOrder = 0;

  while (!in.atEnd()) {
    QString line = in.readLine();
    // macOS / Windows newline handling (QTextStream usually handles \r\n, but
    // just in case)
    if (line.endsWith('\r')) {
      line.chop(1);
    }

    QRegularExpressionMatch match = timeRegex.match(line);

    if (match.hasMatch()) {
      // 找到了带有时间戳的行
      QString timestampStr = match.captured(1);
      QDateTime dt = parseTimestamp(timestampStr);

      if (dt.isValid()) {
        LogEntry entry;
        entry.timestamp = timestampStr;
        entry.dateTime = dt;
        entry.content = line + "\n"; // 保留原始换行
        entry.sourceFileIndex = fileIndex;
        entry.originalOrder = lineOrder++;
        entries.append(entry);
      } else {
        // 如果正则匹配但解析失败，视为无时间戳行丢弃
      }
    } else {
      // 无时间戳行直接丢弃，不执行任何操作
    }

    // 偶尔更新进度避免 UI 冻结 (每个文件占总体进度的 1/N，内部再按读取比例)
    // 注意：这里只是粗略估计
  }

  return entries;
}

void LogMerger::mergeFiles() {
  if (m_merging)
    return;

  if (m_fileList.isEmpty()) {
    emit statusMessage("没有需要合并的文件", 3000);
    return;
  }

  // 清理 outputPath URI （从 file:/// 转换）
  QString outPath = m_outputPath;
  if (outPath.startsWith("file:///")) {
    outPath = outPath.mid(8);
  }

  if (outPath.isEmpty()) {
    emit statusMessage("请选择输出位置", 3000);
    return;
  }

  m_merging = true;
  emit mergingChanged();
  updateProgress(0.0);
  emit statusMessage("正在解析文件...", 0);
  QCoreApplication::processEvents();

  QList<LogEntry> allEntries;
  int totalFiles = m_fileList.size();

  // 阶段 1：解析文件
  for (int i = 0; i < totalFiles; ++i) {
    QList<LogEntry> entries = parseLogFile(m_fileList[i], i);
    allEntries.append(entries);

    // 解析阶段占总进度的 50%
    updateProgress(0.5 * (i + 1) / totalFiles);
    QCoreApplication::processEvents();
  }

  emit statusMessage(
      QString("共解析 %1 条日志，正在合并排序...").arg(allEntries.size()), 0);
  QCoreApplication::processEvents();

  // 阶段 2：排序
  std::stable_sort(allEntries.begin(), allEntries.end(),
                   [](const LogEntry &a, const LogEntry &b) {
                     if (a.dateTime != b.dateTime)
                       return a.dateTime < b.dateTime;
                     if (a.sourceFileIndex != b.sourceFileIndex)
                       return a.sourceFileIndex < b.sourceFileIndex;
                     return a.originalOrder < b.originalOrder;
                   });

  updateProgress(0.7);
  QCoreApplication::processEvents();
  emit statusMessage("正在输出合并结果...", 0);

  // 阶段 3：写入输出文件
  QFile outFile(outPath);
  if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    m_merging = false;
    emit mergingChanged();
    updateProgress(0.0);
    emit mergeFinished(false, "无法创建输出文件: " + outPath);
    return;
  }

  // 写入 BOM
  outFile.write("\xEF\xBB\xBF");
  QTextStream outStream(&outFile);
  outStream.setEncoding(QStringConverter::Utf8);

  int totalEntries = allEntries.size();
  for (int i = 0; i < totalEntries; ++i) {
    outStream << allEntries[i].content;

    if (i % 1000 == 0) {
      // 输出阶段占总进度的最后 30% (从 0.7 到 1.0)
      updateProgress(0.7 + 0.3 * i / totalEntries);
      QCoreApplication::processEvents();
    }
  }

  outFile.close();

  m_merging = false;
  emit mergingChanged();
  updateProgress(1.0);
  emit mergeFinished(true,
                     QString("合并完成！总计 %1 条日志。").arg(totalEntries));
}
