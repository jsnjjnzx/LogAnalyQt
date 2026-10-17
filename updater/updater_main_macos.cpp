/**
 * LogAnalyQt macOS updater.
 *
 * The release archive must contain exactly one signed .app bundle. The updater
 * extracts and validates it before stopping the old application, then replaces
 * the bundle with rollback protection and starts the new version.
 */

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <spawn.h>
#include <string>
#include <sys/stdio.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace fs = std::filesystem;

namespace {

struct Options {
  pid_t pid = 0;
  fs::path source;
  fs::path target;
  bool launch = false;
  int timeoutMs = 30000;
};

int runCommand(const std::vector<std::string> &arguments) {
  if (arguments.empty())
    return -1;

  std::vector<char *> argv;
  argv.reserve(arguments.size() + 1);
  for (const std::string &argument : arguments)
    argv.push_back(const_cast<char *>(argument.c_str()));
  argv.push_back(nullptr);

  pid_t child = 0;
  const int spawnResult =
      posix_spawn(&child, arguments.front().c_str(), nullptr, nullptr,
                  argv.data(), environ);
  if (spawnResult != 0)
    return spawnResult;

  int status = 0;
  if (waitpid(child, &status, 0) < 0)
    return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void showError(const std::string &message) {
  std::cerr << "LogAnalyQt updater: " << message << '\n';
  const std::string script =
      "on run argv\n"
      "display alert \"LogAnalyQt 更新失败\" message (item 1 of argv) "
      "as critical buttons {\"确定\"} default button \"确定\"\n"
      "end run";
  runCommand({"/usr/bin/osascript", "-e", script, message});
}

bool parseArguments(int argc, char *argv[], Options &options,
                    std::string &error) {
  try {
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--pid" && i + 1 < argc) {
        options.pid = static_cast<pid_t>(std::stol(argv[++i]));
      } else if (argument == "--source" && i + 1 < argc) {
        options.source = argv[++i];
      } else if (argument == "--target" && i + 1 < argc) {
        options.target = argv[++i];
      } else if (argument == "--timeout" && i + 1 < argc) {
        options.timeoutMs = std::stoi(argv[++i]);
      } else if (argument == "--launch") {
        options.launch = true;
      } else {
        error = "未知或不完整的参数: " + argument;
        return false;
      }
    }
  } catch (const std::exception &) {
    error = "命令行参数格式无效";
    return false;
  }

  if (options.source.empty() || options.target.empty()) {
    error = "需要 --source 和 --target 参数";
    return false;
  }
  if (options.timeoutMs <= 0) {
    error = "--timeout 必须大于 0";
    return false;
  }
  return true;
}

bool waitForProcessExit(pid_t pid, int timeoutMs) {
  if (pid <= 0)
    return true;

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    if (kill(pid, 0) != 0 && errno == ESRCH)
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return kill(pid, 0) != 0 && errno == ESRCH;
}

fs::path findExtractedApp(const fs::path &directory) {
  fs::path found;
  std::error_code error;
  fs::recursive_directory_iterator iterator(
      directory, fs::directory_options::skip_permission_denied, error);
  const fs::recursive_directory_iterator end;
  for (; !error && iterator != end; iterator.increment(error)) {
    if (!iterator->is_directory(error))
      continue;
    if (iterator->path().extension() != ".app")
      continue;
    if (!found.empty())
      return {};
    found = iterator->path();
    iterator.disable_recursion_pending();
  }
  return found;
}

void copyLegacyItemIfNeeded(const fs::path &source,
                            const fs::path &destination) {
  std::error_code error;
  if (!fs::exists(source, error) || fs::exists(destination, error))
    return;

  fs::create_directories(destination.parent_path(), error);
  if (error)
    throw fs::filesystem_error("create data directory", destination, error);

  if (fs::is_directory(source, error)) {
    fs::copy(source, destination, fs::copy_options::recursive, error);
  } else {
    fs::copy_file(source, destination, error);
  }
  if (error)
    throw fs::filesystem_error("migrate application data", source,
                               destination, error);
}

void migrateLegacyData(const fs::path &oldApp) {
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0')
    return;

  const fs::path oldData = oldApp / "Contents/MacOS";
  const fs::path newData =
      fs::path(home) / "Library/Application Support/LogAnalyQt";
  copyLegacyItemIfNeeded(oldData / "LogAnalyQt.ini",
                         newData / "LogAnalyQt.ini");
  copyLegacyItemIfNeeded(oldData / "protocol_xml",
                         newData / "protocol_xml");
  copyLegacyItemIfNeeded(oldData / "preset", newData / "preset");
  copyLegacyItemIfNeeded(oldData / "log", newData / "log");
}

} // namespace

int main(int argc, char *argv[]) {
  Options options;
  std::string argumentError;
  if (!parseArguments(argc, argv, options, argumentError)) {
    showError(argumentError);
    return 1;
  }

  std::error_code error;
  options.source = fs::absolute(options.source, error);
  options.target = fs::absolute(options.target, error);
  if (error || !fs::is_regular_file(options.source) ||
      options.source.extension() != ".zip") {
    showError("更新包不存在或不是 ZIP 文件");
    return 1;
  }
  if (!fs::is_directory(options.target) ||
      options.target.extension() != ".app") {
    showError("目标路径不是有效的 macOS 应用包");
    return 1;
  }

  const fs::path parent = options.target.parent_path();
  const fs::path staging =
      parent / (".LogAnalyQt-update-" + std::to_string(getpid()));
  const fs::path backup =
      parent / (options.target.filename().string() + ".backup");

  fs::remove_all(staging, error);
  error.clear();
  fs::create_directories(staging, error);
  if (error) {
    showError("无法在应用目录创建更新暂存目录，请检查写入权限");
    return 1;
  }

  if (runCommand({"/usr/bin/ditto", "-x", "-k", options.source.string(),
                  staging.string()}) != 0) {
    fs::remove_all(staging, error);
    showError("无法解压更新包");
    return 1;
  }

  const fs::path newApp = findExtractedApp(staging);
  if (newApp.empty() ||
      !fs::exists(newApp / "Contents/MacOS/appLogAnalyQt")) {
    fs::remove_all(staging, error);
    showError("更新包必须包含且只能包含一个完整的 LogAnalyQt.app");
    return 1;
  }

  if (runCommand({"/usr/bin/codesign", "--verify", "--deep", "--strict",
                  newApp.string()}) != 0) {
    fs::remove_all(staging, error);
    showError("新版本应用的代码签名校验失败");
    return 1;
  }

  if (!waitForProcessExit(options.pid, options.timeoutMs)) {
    fs::remove_all(staging, error);
    showError("等待主程序退出超时，更新已取消");
    return 1;
  }

  try {
    migrateLegacyData(options.target);
  } catch (const fs::filesystem_error &exception) {
    fs::remove_all(staging, error);
    showError(std::string("迁移旧版本数据失败: ") + exception.what());
    return 1;
  }

  if (renamex_np(options.target.c_str(), newApp.c_str(), RENAME_SWAP) != 0) {
    // 非 APFS/HFS+ 卷可能不支持原子交换；回退到备份和恢复流程。
    fs::remove_all(backup, error);
    error.clear();
    fs::rename(options.target, backup, error);
    if (error) {
      fs::remove_all(staging, error);
      showError("无法备份旧应用，请检查应用目录写入权限");
      return 1;
    }

    fs::rename(newApp, options.target, error);
    if (error) {
      std::error_code restoreError;
      fs::rename(backup, options.target, restoreError);
      fs::remove_all(staging, restoreError);
      showError(restoreError ? "安装新版本失败，且旧版本恢复失败"
                             : "安装新版本失败，已恢复旧版本");
      return 1;
    }
    fs::remove_all(backup, error);
  }

  fs::remove_all(staging, error);
  fs::remove(options.source, error);

  if (options.launch &&
      runCommand({"/usr/bin/open", options.target.string()}) != 0) {
    showError("更新成功，但无法自动启动新版本，请手动打开应用");
    return 1;
  }

  return 0;
}
