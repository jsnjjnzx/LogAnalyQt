#ifndef LOGMERGER_H
#define LOGMERGER_H

#include <QDateTime>
#include <QObject>
#include <QStringList>


class LogMerger : public QObject {
  Q_OBJECT
  Q_PROPERTY(QStringList fileList READ fileList NOTIFY fileListChanged)
  Q_PROPERTY(QString outputPath READ outputPath WRITE setOutputPath NOTIFY
                 outputPathChanged)
  Q_PROPERTY(bool merging READ merging NOTIFY mergingChanged)
  Q_PROPERTY(double progress READ progress NOTIFY progressChanged)

public:
  explicit LogMerger(QObject *parent = nullptr);

  QStringList fileList() const;
  QString outputPath() const;
  void setOutputPath(const QString &path);
  bool merging() const;
  double progress() const;

  // 文件池管理
  Q_INVOKABLE void addFiles(const QStringList &urls);
  Q_INVOKABLE void removeFile(int index);
  Q_INVOKABLE void clearFiles();
  Q_INVOKABLE void moveFileUp(int index);
  Q_INVOKABLE void moveFileDown(int index);

  // 合并操作
  Q_INVOKABLE void mergeFiles();

signals:
  void fileListChanged();
  void outputPathChanged();
  void mergingChanged();
  void progressChanged();
  void mergeFinished(bool success, const QString &message);
  void statusMessage(const QString &message, int timeout);

private:
  struct LogEntry {
    QString timestamp;
    QDateTime dateTime;
    QString content;
    int sourceFileIndex;
    int originalOrder;
  };

  QDateTime parseTimestamp(const QString &timestampStr);
  QList<LogEntry> parseLogFile(const QString &filePath, int fileIndex);

  QStringList m_fileList;
  QString m_outputPath;
  bool m_merging;
  double m_progress;

  // 更新进度的辅助函数
  void updateProgress(double p);
};

#endif // LOGMERGER_H
