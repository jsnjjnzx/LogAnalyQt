#pragma once

#include <QFutureWatcher>
#include <QTimer>
#include <QVariantMap>
#include <QUrl>
#include <atomic>
#include <memory>

class SshFileTransfer : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(qint64 transferredBytes READ transferredBytes NOTIFY transferredBytesChanged)

public:
  explicit SshFileTransfer(QObject *parent = nullptr);
  explicit SshFileTransfer(QString dataRoot, QObject *parent = nullptr);
  ~SshFileTransfer() override;
  bool busy() const { return m_busy; }
  qint64 transferredBytes() const { return m_transferredBytes; }

  Q_INVOKABLE QString homePath() const;
  Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }
  Q_INVOKABLE QString parentPath(const QString &path) const;
  Q_INVOKABLE void browseLocal(const QString &path);
  Q_INVOKABLE void run(const QString &action, const QVariantMap &connection, const QString &remotePath,
                       const QString &localPath, const QString &name = {}, const QString &newName = {});
  Q_INVOKABLE void confirmHost(bool accept);
  Q_INVOKABLE void cancel();

signals:
  void busyChanged();
  void transferredBytesChanged();
  void finished(const QString &action, const QVariantMap &result);
  void hostKeyRequired(const QString &endpoint, const QString &fingerprint);

private:
  struct Job {
    QString action, host, user, password, remotePath, localPath, name, newName, acceptedFingerprint;
    int port = 0;
    bool ftp = false;
  };
  struct State {
    std::atomic_bool cancelled{false};
    std::atomic<qint64> transferred{0};
  };
  void start(Job job);
  void syncProgress();
  static QVariantMap execute(const Job &job, const QString &dataRoot, State &state);

  QString m_dataRoot;
  QFutureWatcher<QVariantMap> m_watcher;
  Job m_job;
  std::shared_ptr<State> m_state;
  QTimer m_progressTimer;
  qint64 m_transferredBytes = 0;
  bool m_busy = false;
};
