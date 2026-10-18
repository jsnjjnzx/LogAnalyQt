#pragma once

#include <array>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stdio.h>
#include <unistd.h>

namespace MacDataMigration {
namespace fs = std::filesystem;

inline void fail(const std::string &message, const fs::path &path) {
  throw std::runtime_error(message + ": " + path.string());
}

inline fs::file_status checkedStatus(const fs::path &path) {
  std::error_code error;
  const auto value = fs::symlink_status(path, error);
  if (error && error != std::errc::no_such_file_or_directory)
    fail("Cannot inspect migration path", path);
  if (fs::is_symlink(value)) fail("Migration refuses symbolic links", path);
  return value;
}

inline bool sameFile(const fs::path &first, const fs::path &second) {
  std::ifstream a(first, std::ios::binary), b(second, std::ios::binary);
  if (!a) fail("Cannot read existing data", first);
  if (!b) fail("Cannot read existing data", second);
  std::array<char, 65536> left{}, right{};
  do {
    a.read(left.data(), left.size()); b.read(right.data(), right.size());
    if (a.bad() || b.bad()) fail("Cannot finish reading existing data", first);
    if (a.gcount() != b.gcount() ||
        !std::equal(left.begin(), left.begin() + a.gcount(), right.begin())) return false;
  } while (a.gcount());
  return true;
}

inline void ensureDirectory(const fs::path &path) {
  const auto value = checkedStatus(path);
  if (fs::exists(value) && !fs::is_directory(value)) fail("Expected a directory", path);
  if (!fs::exists(value)) {
    fs::create_directories(path);
    fs::permissions(path, fs::perms::owner_all);
  }
}

inline void initialize(const std::vector<fs::path> &legacyRoots,
                       const fs::path &dataRoot, const fs::path &logRoot, bool resourcesOnly = false) {
  if (!dataRoot.is_absolute() || !logRoot.is_absolute())
    throw std::runtime_error("Migration requires absolute data and log paths");
  ensureDirectory(dataRoot);
  const fs::path lockPath = dataRoot / ".migration.lock";
  const int lock = ::open(lockPath.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (lock < 0) fail("Cannot lock application data", lockPath);
  struct Lock { int fd; ~Lock() { ::close(fd); } } held{lock};
  if (::flock(lock, LOCK_EX | LOCK_NB) != 0) fail("Another data migration is running", dataRoot);

  const fs::path marker = dataRoot / ".legacy-data-imported-v1";
  const std::string markerText = "LogAnalyQt macOS data migration v1\n";
  if (fs::exists(checkedStatus(marker))) {
    if (!fs::is_regular_file(checkedStatus(marker))) fail("Invalid migration marker", marker);
    if (fs::file_size(marker) != markerText.size()) fail("Invalid migration marker", marker);
    std::ifstream input(marker, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    if (!input || text != markerText) fail("Invalid migration marker", marker);
    ensureDirectory(logRoot);
    return;
  }

  std::map<fs::path, fs::path> files;
  std::set<fs::path> directories{dataRoot, logRoot};
  const auto addTree = [&](const auto &self, const fs::path &source, const fs::path &target) -> void {
    const auto sourceStatus = checkedStatus(source);
    if (!fs::exists(sourceStatus)) return;
    const auto targetStatus = checkedStatus(target);
    if (fs::is_directory(sourceStatus)) {
      if (files.count(target)) fail("Legacy directory conflicts with another file", source);
      if (fs::exists(targetStatus) && !fs::is_directory(targetStatus)) fail("Migration destination conflicts", target);
      directories.insert(target);
      for (const auto &entry : fs::directory_iterator(source)) {
        if (resourcesOnly && entry.path().filename() == "preset_messages.xml") continue;
        self(self, entry.path(), target / entry.path().filename());
      }
      return;
    }
    if (!fs::is_regular_file(sourceStatus)) fail("Unsupported legacy data file", source);
    if (directories.count(target)) fail("Legacy file conflicts with another directory", source);
    std::ifstream input(source, std::ios::binary);
    if (!input) fail("Cannot read legacy data", source);
    if (fs::exists(targetStatus) &&
        (!fs::is_regular_file(targetStatus) || !sameFile(source, target))) fail("Existing data differs; both copies were preserved", target);
    const auto previous = files.find(target);
    if (previous != files.end() && !sameFile(previous->second, source)) fail("Legacy data sources conflict", source);
    files.emplace(target, source);
  };
  for (const auto &root : legacyRoots) {
    if (!fs::exists(checkedStatus(root))) continue;
    if (!fs::is_directory(checkedStatus(root))) fail("Legacy data root is not a directory", root);
    for (const std::string name : {"LogAnalyQt.ini", "protocol_xml", "prompts", "preset", "ssh-known-hosts.json", "log"}) {
      if (resourcesOnly && (name == "LogAnalyQt.ini" || name == "preset")) continue;
      const auto value = checkedStatus(root / name);
      const bool file = name == "LogAnalyQt.ini" || name == "ssh-known-hosts.json";
      if (fs::exists(value) && (file ? !fs::is_regular_file(value) : !fs::is_directory(value)))
        fail("Legacy data has the wrong file type", root / name);
      addTree(addTree, root / name, name == "log" ? logRoot : dataRoot / name);
    }
  }
  // Validate all conflicts before copying; interrupted imports can safely resume.
  for (const auto &directory : directories) ensureDirectory(directory);
  for (const auto &[target, source] : files) {
    const auto value = checkedStatus(target);
    if (fs::exists(value)) {
      if (!fs::is_regular_file(value) || !sameFile(source, target)) fail("Destination changed during migration", target);
      continue;
    }
    std::string pattern = target.string() + ".migration-XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end()); name.push_back(0);
    const int reserved = ::mkstemp(name.data());
    if (reserved < 0) fail("Cannot create migration temporary file", target);
    ::close(reserved);
    const fs::path temporary(name.data());
    try {
      fs::copy_file(source, temporary, fs::copy_options::overwrite_existing);
      if (!fs::is_regular_file(checkedStatus(source))) fail("Legacy file changed type", source);
      fs::last_write_time(temporary, fs::last_write_time(source));
      if (!sameFile(source, temporary)) fail("Legacy data changed during migration", source);
      const int copied = ::open(temporary.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
      const bool durable = copied >= 0 && ::fsync(copied) == 0;
      if (copied >= 0) ::close(copied);
      if (!durable) fail("Cannot flush migrated data", temporary);
      if (::renamex_np(temporary.c_str(), target.c_str(), RENAME_EXCL) != 0) fail("Cannot commit migrated data without overwriting", target);
    } catch (...) {
      std::error_code ignored; fs::remove(temporary, ignored);
      throw;
    }
  }
  const fs::path temporary = marker.string() + "." + std::to_string(::getpid());
  const int output = ::open(temporary.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (output < 0) fail("Cannot create migration marker", temporary);
  const bool written = ::write(output, markerText.data(), markerText.size()) == static_cast<ssize_t>(markerText.size()) && ::fsync(output) == 0;
  ::close(output);
  if (!written || ::renamex_np(temporary.c_str(), marker.c_str(), RENAME_EXCL) != 0) {
    std::error_code ignored; fs::remove(temporary, ignored);
    fail("Cannot commit migration marker", marker);
  }
}
} // namespace MacDataMigration
