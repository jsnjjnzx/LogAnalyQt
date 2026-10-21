#include "SshTerminal.h"
#include "SshPerformance.h"
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QWheelEvent>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QSaveFile>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <functional>
#include <memory>
#include <utility>
#include <thread>
#include <vector>
#ifdef LOGANALYQT_FULL_APP_BENCHMARK
#include "LogBackend.h"
#endif
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#include <sys/resource.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#else
#include <QFile>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {
QString dataRoot;
class MemoryCredentials final : public SecureCredentialStore {
public:
  MemoryCredentials() : SecureCredentialStore(Domain::Ssh) {}
  bool contains(const QString &, bool *exists, QString *) const override { *exists = false; return true; }
  bool read(const QString &, QByteArray *value, QString *) const override { value->clear(); return true; }
};

double cpuSeconds() {
#ifdef Q_OS_WIN
  FILETIME created, exited, kernel, user;
  GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
  const auto seconds = [](FILETIME t) { return (double(t.dwHighDateTime) * 4294967296.0 + t.dwLowDateTime) / 1e7; };
  return seconds(kernel) + seconds(user);
#else
  rusage usage{}; getrusage(RUSAGE_SELF, &usage);
  return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 + usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
#endif
}
qint64 residentBytes() {
#ifdef Q_OS_MACOS
  mach_task_basic_info info{}; mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  return task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS
      ? qint64(info.resident_size) : 0;
#elif defined(Q_OS_WIN)
  PROCESS_MEMORY_COUNTERS info{};
  return GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info)) ? qint64(info.WorkingSetSize) : 0;
#else
  QFile file("/proc/self/statm");
  if (!file.open(QIODevice::ReadOnly)) return 0;
  return file.readAll().split(' ').value(1).toLongLong() * sysconf(_SC_PAGESIZE);
#endif
}

// Fixed-size histograms keep measurement memory constant, even in an hour-long run.
struct Histogram {
  std::array<quint64, 60002> bins{};
  quint64 count = 0;
  qint64 maximum = 0;
  void add(qint64 ms) {
    ms = qMax(qint64(0), ms);
    ++bins[size_t(qMin(ms, qint64(bins.size() - 1)))]; ++count;
    maximum = qMax(maximum, ms);
  }
  qint64 percentile(double p) const {
    if (!count) return 0;
    const auto target = quint64(std::ceil(count * p));
    quint64 total = 0;
    for (size_t i = 0; i < bins.size(); ++i) if ((total += bins[i]) >= target) return qint64(i);
    return maximum;
  }
  QJsonObject json() const {
    return {{"samples", qint64(count)}, {"p50_ms", percentile(.50)}, {"p95_ms", percentile(.95)},
            {"p99_ms", percentile(.99)}, {"max_ms", maximum}};
  }
};
struct Session {
  SshTerminal *terminal = nullptr;
  qint64 last = -1;
  qint64 received = 0;
  bool done = false;
  qint64 lastCommitMs = 0;
  qint64 maxCommitGapMs = 0;
};

struct InputEvent : QEvent {
  explicit InputEvent(QEvent::Type type) : QEvent(type), queued(std::chrono::steady_clock::now()) {}
  std::chrono::steady_clock::time_point queued;
};

// Include GUI-queue waiting and actual terminal-key dispatch to the isolated SSH peer.
class InputProbe : public QObject {
public:
  InputProbe(QObject *target, SshTerminal *terminal) : m_target(target), m_type(QEvent::Type(QEvent::registerEventType())) {
    connect(terminal, &SshTerminal::sigSendCommand, this, [this](const QString &text, bool newline, const QString &) {
      if (text == m_expected && !newline) ++m_rawKeys;
    });
    target->installEventFilter(this);
    connect(target, &QObject::destroyed, this, [this] { stop(); m_target = nullptr; });
    m_thread = std::thread([this, target] {
      while (!m_stopping.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (!m_stopping.load()) QCoreApplication::postEvent(target, new InputEvent(m_type));
      }
    });
  }
  ~InputProbe() override { stop(); if (m_target) m_target->removeEventFilter(this); }
  QJsonObject report() const {
    auto result = m_latency.json(); result["failed_keystrokes"] = m_errors;
    result["ascii_samples"] = m_asciiSamples; result["ime_samples"] = m_imeSamples;
    return result;
  }
protected:
  bool eventFilter(QObject *target, QEvent *event) override {
    if (event->type() != m_type) return false;
    const auto before = m_rawKeys;
    const bool ime = m_asciiSamples > m_imeSamples;
    m_expected = ime ? QString::fromUtf8("中文") : QString("x");
    if (ime) {
      QInputMethodEvent commit; commit.setCommitString(m_expected);
      QCoreApplication::sendEvent(target, &commit); ++m_imeSamples;
    } else {
      QKeyEvent press(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, "x");
      QKeyEvent release(QEvent::KeyRelease, Qt::Key_X, Qt::NoModifier, "x");
      QCoreApplication::sendEvent(target, &press); QCoreApplication::sendEvent(target, &release); ++m_asciiSamples;
    }
    if (m_rawKeys != before + 1 || !target->property("text").toString().isEmpty()) ++m_errors;
    m_expected.clear();
    m_latency.add(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - static_cast<InputEvent *>(event)->queued).count());
    return true;
  }
private:
  void stop() { m_stopping.store(true); if (m_thread.joinable()) m_thread.join(); }
  QObject *m_target;
  QEvent::Type m_type;
  std::atomic<bool> m_stopping{false};
  std::thread m_thread;
  Histogram m_latency;
  int m_errors = 0;
  int m_rawKeys = 0;
  int m_asciiSamples = 0, m_imeSamples = 0;
  QString m_expected;
};

#ifdef LOGANALYQT_FULL_APP_BENCHMARK
// Test-only queued actions measured through the frame that synchronizes their changes.
class InteractionProbe final : public QObject {
public:
  InteractionProbe(QQuickWindow *window, QObject *page, SshTerminal *terminal)
      : m_window(window), m_page(page), m_terminal(terminal),
        m_type(QEvent::Type(QEvent::registerEventType())) {
    connect(terminal, &SshTerminal::sigSendCommand, this, [this](const QString &text, bool newline, const QString &) {
      if (text == "x" && !newline) ++m_rawKeys;
    });
    m_list = find(window->contentItem(), [](QQuickItem *i) { return i->objectName() == "ssh-log-list"; });
    m_pause = find(window->contentItem(), [](QQuickItem *i) { return i->property("text").toString() == QString::fromUtf8("⏸ 暂停"); });
    m_search = find(window->contentItem(), [](QQuickItem *i) { return i->property("placeholderText").toString() == QString::fromUtf8("输入关键字…"); });
    m_away = find(window->contentItem(), [](QQuickItem *i) { return i->objectName() == "nav-10"; });
    m_back = find(window->contentItem(), [](QQuickItem *i) { return i->objectName() == "nav-5"; });
    if (!m_list || !m_pause || !m_search || !m_away || !m_back) qFatal("Full application interaction target missing");
    window->installEventFilter(this);
    const auto frames = m_frames;
    const QPointer<InteractionProbe> guard(this);
    connect(window, &QQuickWindow::beforeSynchronizing, this, [frames] {
      frames->synced.store(frames->submitted.load());
    }, Qt::DirectConnection);
    connect(window, &QQuickWindow::frameSwapped, this, [frames, guard, window] {
      const auto token = frames->synced.load();
      const auto shown = std::chrono::steady_clock::now();
      QMetaObject::invokeMethod(window, [guard, token, shown] {
        if (guard) guard->recordFrame(token, shown);
      }, Qt::QueuedConnection);
    }, Qt::DirectConnection);
    m_thread = std::thread([this] {
      while (!m_stopping.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        if (!m_stopping.load()) QCoreApplication::postEvent(m_window, new InputEvent(m_type));
      }
    });
  }
  ~InteractionProbe() override {
    m_stopping.store(true); if (m_thread.joinable()) m_thread.join();
    m_window->removeEventFilter(this); QObject::disconnect(m_window, nullptr, this, nullptr);
  }
  QJsonObject report() const {
    QJsonObject result;
    for (int i = 0; i < Names.size(); ++i) result[Names[i]] = m_latency[size_t(i)].json();
    result["failures"] = m_failures; result["failed_action"] = m_failedAction;
    return result;
  }
protected:
  bool eventFilter(QObject *, QEvent *event) override {
    if (event->type() != m_type) return false;
    if (m_pending) { ++m_failures; m_failedAction = Names[m_action]; }
    m_action = (m_action + 1) % Names.size();
    m_queued = static_cast<InputEvent *>(event)->queued;
    m_pending = true; ++m_token; m_verify = [] { return false; };
    switch (m_action) {
    case 0: {
      auto *input = m_page->property("terminalInput").value<QObject *>();
      if (!input) input = m_page->property("terminalInput").value<QJSValue>().toQObject();
      if (input) {
        const auto expected = m_rawKeys + 1;
        QKeyEvent key(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, "x");
        QCoreApplication::sendEvent(input, &key);
        m_verify = [this, input, expected] { return m_rawKeys >= expected && input->property("text").toString().isEmpty(); };
      }
      break;
    }
    case 1: click(m_pause); m_verify = [this] { return !m_page->property("autoScroll").toBool(); }; break;
    case 2: {
      const auto before = m_list->property("contentY").toDouble();
      const auto point = m_list->mapToScene(QPointF(m_list->width() / 2, m_list->height() / 2));
      QWheelEvent wheel(point, m_window->mapToGlobal(point.toPoint()), QPoint(0, 100), QPoint(0, 120),
                        Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
      QCoreApplication::sendEvent(m_window, &wheel);
      m_verify = [this, before] { return m_list->property("contentY").toDouble() < before - 1; };
      break;
    }
    case 3: click(m_pause); m_verify = [this] { return m_page->property("autoScroll").toBool(); }; break;
    case 4:
      m_page->setProperty("findBarVisible", true); m_search->setProperty("text", QString::fromUtf8("监控"));
      m_verify = [this] { return m_page->property("searchKeyword").toString() == QString::fromUtf8("监控"); }; break;
    case 5:
      m_search->setProperty("text", ""); m_page->setProperty("findBarVisible", false);
      m_verify = [this] { return m_page->property("searchKeyword").toString().isEmpty(); }; break;
    case 6: click(m_away); m_verify = [this] { return m_window->property("currentPage").toInt() == 10; }; break;
    case 7: click(m_back); m_verify = [this] { return m_window->property("currentPage").toInt() == 5; }; break;
    case 8: {
      auto *row = find(m_list, [this](QQuickItem *i) {
        if (!i->objectName().startsWith("ssh-log-line-") || !i->isVisible()) return false;
        const auto y = i->mapToItem(m_list, QPointF()).y();
        return y > 0 && y + i->height() < m_list->height() - 40;
      });
      if (row) { const auto id = row->property("stableId").toLongLong(); click(row);
        m_verify = [this, id] { return m_page->property("selectedLineId").toLongLong() == id; }; }
      break;
    }
    case 9: {
      m_terminal->linesAboutToChange();
      m_terminal->lineModel()->appendLines({"RX 05 01 02 40 00 02 00 00"}); m_terminal->lineCountChanged();
      QMetaObject::invokeMethod(m_page, "selectLineAndExtractHex", Q_ARG(QVariant, m_terminal->currentLineCount() - 1));
      QMetaObject::invokeMethod(m_page, "parseSelectedHexWithProtocol", Q_ARG(QVariant, 0));
      m_verify = [this] {
        auto *backend = m_window->findChild<LogBackend *>("analysisBackend");
        if (!backend || !m_page->property("parseWindowVisible").toBool()) return false;
        auto *model = backend->parseModel(m_page->property("parseSessionId").toInt());
        if (!model) return false;
        for (int row = 0; row < model->rowCount(); ++row)
          if (model->data(model->index(row), ParseResultModel::ValueRole).toString().remove(' ').contains("40000200")) return true;
        return false;
      };
      break;
    }
    case 10: m_page->setProperty("parseWindowVisible", false);
      m_verify = [this] { return !m_page->property("parseWindowVisible").toBool(); }; break;
    }
    m_frames->submitted.store(m_token); m_window->update();
    return true;
  }
private:
  void recordFrame(quint64 token, std::chrono::steady_clock::time_point shown) {
    if (!m_pending || token != m_token || !m_verify()) return;
    m_latency[size_t(m_action)].add(std::chrono::duration_cast<std::chrono::milliseconds>(shown - m_queued).count());
    m_pending = false;
  }
  static QQuickItem *find(QQuickItem *item, const std::function<bool(QQuickItem *)> &matches) {
    if (matches(item)) return item;
    for (auto *child : item->childItems()) if (auto *hit = find(child, matches)) return hit;
    return nullptr;
  }
  void click(QQuickItem *item) {
    if (!item || !item->isVisible()) return;
    const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    QMouseEvent press(QEvent::MouseButtonPress, point, m_window->mapToGlobal(point.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, point, m_window->mapToGlobal(point.toPoint()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(m_window, &press); QCoreApplication::sendEvent(m_window, &release);
  }
  const QStringList Names{"input", "pause", "scroll", "resume", "search", "search_clear", "page_away", "page_back", "select", "parse_698", "parse_close"};
  QQuickWindow *m_window;
  QObject *m_page;
  SshTerminal *m_terminal;
  QQuickItem *m_list = nullptr, *m_pause = nullptr, *m_search = nullptr, *m_away = nullptr, *m_back = nullptr;
  QEvent::Type m_type;
  std::atomic<bool> m_stopping{false};
  struct FrameState { std::atomic<quint64> submitted{0}, synced{0}; };
  std::shared_ptr<FrameState> m_frames = std::make_shared<FrameState>();
  std::thread m_thread;
  std::array<Histogram, 11> m_latency;
  std::function<bool()> m_verify;
  std::chrono::steady_clock::time_point m_queued;
  quint64 m_token = 0;
  bool m_pending = false;
  int m_action = -1, m_failures = 0;
  int m_rawKeys = 0;
  QString m_failedAction;
};
#endif
}

// Only the constructor is substituted: transport, decoder, model and the QML page are production code.
class BenchmarkTerminal : public SshTerminal {
  Q_OBJECT
public:
  explicit BenchmarkTerminal(QObject *parent = nullptr)
      : SshTerminal(std::make_shared<MemoryCredentials>(), dataRoot, parent) {}
};

void registerSshPerformanceTerminal(const QString &root) {
  dataRoot = root + "/settings"; QDir().mkpath(dataRoot);
  qmlRegisterType<BenchmarkTerminal>("LogAnalyQt", 1, 2, "SshTerminal");
}

int runSshPerformance(QGuiApplication &app, const QStringList &args, QQmlApplicationEngine *fullApplication) {
  if (args.size() != 10) { QTextStream(stderr) << "Use tests/run_ssh_performance.py\n"; return 2; }
  const int port = args[1].toInt();
  const auto fingerprint = args[2];
  const int seconds = args[5].toInt(), rate = args[6].toInt(), count = args[7].toInt();
  const bool search = args[8] == "1", headless = args[9] == "1";
  const auto viewState = qEnvironmentVariable("LOGANALYQT_SSH_PERF_VIEW_STATE", "live");
  if (count < 1 || count > 3 || rate < 512 || seconds < 1) return 2;
  app.setQuitOnLastWindowClosed(false);
  std::unique_ptr<QQmlApplicationEngine> isolatedEngine;
  if (!fullApplication) isolatedEngine = std::make_unique<QQmlApplicationEngine>();
  auto &engine = fullApplication ? *fullApplication : *isolatedEngine;
  engine.addImportPath(args[3] + "/imports");
  QQuickWindow *window = nullptr;
  QObject *page = nullptr;
  QObject *monitorWorkspace = nullptr;
  std::vector<QObject *> monitorPages;
  std::vector<Session> sessions(static_cast<size_t>(count));
  if (!headless) {
    if (!fullApplication) engine.load(QUrl::fromLocalFile(args[4]));
    if (engine.rootObjects().isEmpty()) return 3;
    window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (fullApplication) {
      window->resize(1440, 900);
      QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, 5));
    }
    page = window->findChild<QObject *>(fullApplication ? "page-5" : "benchmark-page");
    if (page && fullApplication) {
      monitorWorkspace = page;
      const auto active = page->property("activeSession");
      page = active.value<QObject *>();
      if (!page) page = active.value<QJSValue>().toQObject();
    }
    sessions[0].terminal = page ? page->findChild<SshTerminal *>("ssh-terminal-backend") : nullptr;
    if (!page || !sessions[0].terminal) return 4;
    if (fullApplication) monitorPages.push_back(page);
    if (search) {
      page->setProperty("searchKeyword", "监控");
      QMetaObject::invokeMethod(page, "refreshSearchResult");
    }
  }
  std::vector<std::unique_ptr<SshTerminal>> owned;
  for (int i = headless ? 0 : 1; i < count; ++i) {
    if (monitorWorkspace) {
      if (!QMetaObject::invokeMethod(monitorWorkspace, "addSession", Q_ARG(QVariant, QVariant()),
                                    Q_ARG(QVariant, monitorWorkspace->property("nextSessionId")))) return 4;
      const auto active = monitorWorkspace->property("activeSession");
      auto *sessionPage = active.value<QObject *>();
      if (!sessionPage) sessionPage = active.value<QJSValue>().toQObject();
      if (!sessionPage) return 4;
      monitorPages.push_back(sessionPage);
      sessions[size_t(i)].terminal = sessionPage->findChild<SshTerminal *>("ssh-terminal-backend");
      if (!sessions[size_t(i)].terminal) return 4;
    } else {
      const auto directory = dataRoot + QString::number(i); QDir().mkpath(directory);
      owned.push_back(std::make_unique<SshTerminal>(std::make_shared<MemoryCredentials>(), directory));
      sessions[size_t(i)].terminal = owned.back().get();
    }
  }
  if (monitorWorkspace) monitorWorkspace->setProperty("currentIndex", 0);

  auto lineLatency = std::make_unique<Histogram>();
  auto frameLatency = std::make_unique<Histogram>();
  auto modelToFrame = std::make_unique<Histogram>();
  auto eventDelay = std::make_unique<Histogram>();
  QMutex frameMutex;
  qint64 pendingSenderMs = 0, pendingModelMs = 0, frameSenderMs = 0, frameModelMs = 0;
  qint64 errors = 0, overflows = 0, modelResets = 0, modelCommits = 0;
  qint64 rssInitial = 0, rssPeak = 0;
  qint64 windowSamples = 0, activeSamples = 0, exposedSamples = 0;
  int tabSwitchChecks = 0, tabSwitchErrors = 0;
  QJsonArray rssSamples;
  QJsonObject firstTenMinutes;
  const auto progressPath = qEnvironmentVariable("LOGANALYQT_SSH_PERF_PROGRESS");
  int progressMinute = 0;
  bool started = false, finishing = false;
  QElapsedTimer elapsed, heartbeatClock;
  double startCpu = 0;
  qint64 lastHeartbeat = 0;
  std::unique_ptr<InputProbe> inputProbe;
#ifdef LOGANALYQT_FULL_APP_BENCHMARK
  std::unique_ptr<InteractionProbe> interactionProbe;
#endif

  if (window) {
    QObject::connect(window, &QQuickWindow::beforeSynchronizing, window, [&] {
      QMutexLocker lock(&frameMutex);
      frameSenderMs = std::exchange(pendingSenderMs, 0); frameModelMs = std::exchange(pendingModelMs, 0);
    }, Qt::DirectConnection);
    QObject::connect(window, &QQuickWindow::frameSwapped, window, [&] {
      if (frameSenderMs) {
        QMutexLocker lock(&frameMutex);
        const auto now = QDateTime::currentMSecsSinceEpoch();
        frameLatency->add(now - frameSenderMs); modelToFrame->add(now - frameModelMs);
        frameSenderMs = 0;
      }
    }, Qt::DirectConnection);
  }

  const auto metrics = [&] {
    const double wall = elapsed.elapsed() / 1000.0;
    const double cpu = cpuSeconds() - startCpu;
    qint64 received = 0;
    QJsonArray sessionProgress;
    for (const auto &s : sessions) {
      received += s.received;
      sessionProgress.append(QJsonObject{{"received_lines", s.received}, {"max_commit_gap_ms", s.maxCommitGapMs}});
    }
    QJsonObject result{{"seconds_requested", seconds}, {"rate_per_session", rate}, {"sessions", count},
      {"search", search}, {"ui", !headless}, {"elapsed_seconds", wall},
      {"cpu_seconds", cpu}, {"cpu_percent_one_core", cpu * 100 / wall},
      {"received_lines", received}, {"payload_mib_per_second", received * 512 / (1048576.0 * wall)},
      {"session_progress", sessionProgress},
      {"content_errors", errors}, {"overflow_signals", overflows}, {"model_resets", modelResets},
      {"model_commits", modelCommits},
      {"full_application", fullApplication != nullptr},
      {"ui_session_count", int(monitorPages.size())}, {"tab_switch_checks", tabSwitchChecks}, {"tab_switch_errors", tabSwitchErrors},
      {"view_state", viewState},
      {"window_samples", windowSamples}, {"window_active_samples", activeSamples},
      {"window_exposed_samples", exposedSamples},
      {"window_width", window ? window->width() : 0}, {"window_height", window ? window->height() : 0},
      {"device_pixel_ratio", window ? window->devicePixelRatio() : 0},
      {"rss_initial_mib", rssInitial / 1048576.0}, {"rss_peak_mib", rssPeak / 1048576.0},
      {"rss_final_mib", residentBytes() / 1048576.0}, {"rss_samples", rssSamples},
      {"sender_to_model", lineLatency->json()}, {"event_loop_delay", eventDelay->json()},
      {"qt_version", QString::fromLatin1(qVersion())}, {"platform", app.platformName()}};
    {
      QMutexLocker lock(&frameMutex);
      result["latest_visible_sender_to_frame"] = frameLatency->json();
      result["model_to_next_frame"] = modelToFrame->json();
    }
    if (inputProbe) result["input_response"] = inputProbe->report();
#ifdef LOGANALYQT_FULL_APP_BENCHMARK
    if (interactionProbe) result["interaction_response"] = interactionProbe->report();
#endif
    return result;
  };
  const auto finish = [&] {
    auto result = metrics();
    if (!firstTenMinutes.isEmpty()) result["first_600s"] = firstTenMinutes;
    inputProbe.reset();
#ifdef LOGANALYQT_FULL_APP_BENCHMARK
    interactionProbe.reset();
#endif
    for (auto &s : sessions) s.terminal->disconnectFromHost();
    QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << '\n';
    app.quit();
  };

  const auto observe = [&](int identity) {
    if (!started || finishing) return;
    ++modelCommits;
    auto &session = sessions[size_t(identity)];
    auto *model = session.terminal->lineModel();
    std::vector<QString> fresh;
    for (int row = model->rowCount() - 1; row >= 0; --row) {
      auto line = model->lineAt(row);
      if (line.startsWith("DONE|")) { session.done = true; continue; }
      if (!line.startsWith("LQ|")) continue;
      const auto sequence = QStringView(line).mid(6, 12).toLongLong();
      if (sequence <= session.last) break;
      fresh.push_back(std::move(line));
    }
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (!fresh.empty()) {
      const auto commit = elapsed.elapsed();
      session.maxCommitGapMs = qMax(session.maxCommitGapMs, commit - session.lastCommitMs);
      session.lastCommitMs = commit;
    }
    for (auto it = fresh.rbegin(); it != fresh.rend(); ++it) {
      const auto sequence = QStringView(*it).mid(6, 12).toLongLong();
      if (sequence != session.last + 1 || QStringView(*it).mid(3, 2).toInt() != identity ||
          !it->endsWith(QString::fromUtf8(" 中文监控 68 09 00 43 05 01 00 00 00 00 00 00 16 ")) || it->contains(QChar::ReplacementCharacter)) ++errors;
      session.last = sequence; ++session.received;
      const auto timestamp = QStringView(*it).mid(19, 13).toLongLong();
      lineLatency->add(now - timestamp);
    }
    if (identity == 0 && window && page->property("visible").toBool() && page->property("autoScroll").toBool() && !fresh.empty()) {
      QMutexLocker lock(&frameMutex);
      pendingSenderMs = QStringView(fresh.front()).mid(19, 13).toLongLong(); pendingModelMs = now;
    }
    if (headless && search && !fresh.empty()) session.terminal->findLineIndexes("监控");
    if (std::all_of(sessions.begin(), sessions.end(), [](const auto &s) { return s.done; })) {
      finishing = true;
      QTimer::singleShot(200, &app, finish);
    }
  };

  for (int i = 0; i < count; ++i) {
    auto *terminal = sessions[size_t(i)].terminal;
    if (monitorWorkspace) {
      for (const auto &field : QList<QPair<QString, QString>>{{"ssh-host", "localhost"}, {"ssh-port", QString::number(port)},
              {"ssh-username", "loganalyqt-test"}, {"ssh-password-draft", "synthetic-loopback-password"}})
        monitorPages[size_t(i)]->findChild<QObject *>(field.first)->setProperty("text", field.second);
    }
    terminal->setHost("localhost"); terminal->setPort(port); terminal->setUsername("loganalyqt-test");
    terminal->setPassword("synthetic-loopback-password"); terminal->setAutoReconnectEnabled(false);
    terminal->setMaxBufferLines(5000);
    QObject::connect(terminal, &SshTerminal::hostKeyConfirmationRequested, &app,
      [&, terminal](const QString &, int, const QString &, const QString &actual, const QString &id) {
        if (actual != fingerprint) { QTextStream(stderr) << "isolated peer fingerprint mismatch\n"; app.exit(5); return; }
        QTimer::singleShot(0, &app, [terminal, actual, id] { terminal->confirmHostKey(id, actual, true); });
      });
    QObject::connect(terminal, &SshTerminal::connectionError, &app, [&, terminal](const QString &message) {
      QTextStream(stderr) << terminal->host() << ':' << terminal->port() << ' ' << message << '\n'; app.exit(6);
    });
    QObject::connect(terminal, &SshTerminal::sharedBufferOverflowed, &app, [&] { ++overflows; });
    QObject::connect(terminal->lineModel(), &QAbstractItemModel::modelReset, &app, [&, i] { ++modelResets; observe(i); });
    QObject::connect(terminal->lineModel(), &QAbstractItemModel::rowsInserted, &app, [&, i] { observe(i); });
    QObject::connect(terminal->lineModel(), &QAbstractItemModel::dataChanged, &app, [&, i] { observe(i); });
  }
  QTimer heartbeat;
  heartbeat.setTimerType(Qt::PreciseTimer); heartbeat.setInterval(10);
  QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] {
    if (!started) return;
    const auto now = heartbeatClock.elapsed();
    eventDelay->add(now - lastHeartbeat - 10); lastHeartbeat = now;
  });
  QTimer memory;
  memory.setInterval(1000);
  QObject::connect(&memory, &QTimer::timeout, &app, [&] {
    if (!started) return;
    const auto rss = residentBytes(); rssPeak = qMax(rssPeak, rss);
    if (window) { ++windowSamples; activeSamples += window->isActive(); exposedSamples += window->isExposed(); }
    if (monitorPages.size() > 1) {
      const int selected = (monitorWorkspace->property("currentIndex").toInt() + 1) % count;
      monitorWorkspace->setProperty("currentIndex", selected);
      ++tabSwitchChecks;
      const auto active = monitorWorkspace->property("activeSession");
      auto *selectedPage = active.value<QObject *>();
      if (!selectedPage) selectedPage = active.value<QJSValue>().toQObject();
      if (selectedPage != monitorPages[size_t(selected)]) ++tabSwitchErrors;
      for (int i = 0; i < count; ++i)
        if (!sessions[size_t(i)].terminal->isConnected()
            || monitorPages[size_t(i)]->findChild<SshTerminal *>("ssh-terminal-backend") != sessions[size_t(i)].terminal) ++tabSwitchErrors;
    }
    // One number each second, rather than a copy of any log data.
    rssSamples.append(QJsonArray{elapsed.elapsed() / 1000, rss / 1048576.0});
    if (seconds > 600 && elapsed.elapsed() >= 600000 && firstTenMinutes.isEmpty()) firstTenMinutes = metrics();
    const int minute = int(elapsed.elapsed() / 60000);
    if (!progressPath.isEmpty() && minute > progressMinute) {
      progressMinute = minute;
      auto summary = metrics(); summary.remove("rss_samples");
      if (!firstTenMinutes.isEmpty()) { auto first = firstTenMinutes; first.remove("rss_samples"); summary["first_600s"] = first; }
      QSaveFile progress(progressPath);
      if (progress.open(QIODevice::WriteOnly)) {
        progress.write(QJsonDocument(summary).toJson(QJsonDocument::Compact)); progress.commit();
      }
    }
  });
  QTimer startup;
  startup.setInterval(20);
  QElapsedTimer warmup; warmup.start();
  QObject::connect(&startup, &QTimer::timeout, &app, [&] {
    for (auto &s : sessions) {
      if (s.terminal->credentialBusy()) return;
      if (s.terminal->connectionState() == SshTerminal::Idle ||
          s.terminal->connectionState() == SshTerminal::Disconnected) s.terminal->connectToHost();
    }
    if (!std::all_of(sessions.begin(), sessions.end(), [](const auto &s) { return s.terminal->isConnected(); })) return;
    if (window && (!window->isActive() || !window->isExposed())) { window->raise(); window->requestActivate(); return; }
    if (warmup.elapsed() < 5000) return;
    startup.stop();
    started = true; elapsed.start(); heartbeatClock.start(); startCpu = cpuSeconds();
    rssInitial = rssPeak = residentBytes(); heartbeat.start(); memory.start();
    if (fullApplication && viewState == "background") QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, 10));
    if (fullApplication && viewState == "paused")
      QTimer::singleShot(3000, &app, [page] { page->setProperty("autoScroll", false); });
    for (int i = 0; i < count; ++i)
      sessions[size_t(i)].terminal->sendCommand(QString("BENCH %1 %2 %3").arg(i).arg(rate).arg(seconds));
    if (page && qEnvironmentVariableIsSet("LOGANALYQT_SSH_PERF_INPUT_PROBE")) {
      QTimer::singleShot(1000, &app, [&] {
        const auto field = page->property("terminalInput");
        auto *target = field.value<QObject *>();
        if (!target) target = field.value<QJSValue>().toQObject();
        if (!target) { QTextStream(stderr) << "terminal input unavailable for input probe\n"; app.exit(8); return; }
        inputProbe = std::make_unique<InputProbe>(target, sessions[0].terminal);
      });
    }
#ifdef LOGANALYQT_FULL_APP_BENCHMARK
    if (fullApplication && qEnvironmentVariableIsSet("LOGANALYQT_SSH_PERF_INTERACTION_PROBE"))
      QTimer::singleShot(1000, &app, [&] { interactionProbe = std::make_unique<InteractionProbe>(window, page, sessions[0].terminal); });
#endif
  });
  startup.start();
  QTimer::singleShot(seconds * 4000 + 30000, &app, [&] { QTextStream(stderr) << "benchmark timed out\n"; app.exit(7); });
  const int result = app.exec();
  inputProbe.reset();
#ifdef LOGANALYQT_FULL_APP_BENCHMARK
  interactionProbe.reset();
#endif
  for (auto &session : sessions) {
    QObject::disconnect(session.terminal, nullptr, &app, nullptr);
    QObject::disconnect(session.terminal->lineModel(), nullptr, &app, nullptr);
    session.terminal->disconnectFromHost();
  }
  if (window) {
    QObject::disconnect(window, &QQuickWindow::beforeSynchronizing, window, nullptr);
    QObject::disconnect(window, &QQuickWindow::frameSwapped, window, nullptr);
    window->hide();
  }
  return result;
}

#ifndef LOGANALYQT_FULL_APP_BENCHMARK
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  if (argc != 10) return 2;
  registerSshPerformanceTerminal(app.arguments()[3]);
  return runSshPerformance(app, app.arguments());
}
#endif

#include "ssh_performance.moc"
