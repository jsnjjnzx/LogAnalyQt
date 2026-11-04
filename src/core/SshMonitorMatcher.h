#pragma once

#include "SshLogStream.h"
#include <QRegularExpression>
#include <QVariantMap>
#include <QVariantList>
#include <QSet>
#include <utility>

struct SshMonitorHit {
  quint64 revision = 0;
  quint64 logicalId = 0;
  qsizetype offset = 0;
  QVariantMap data;
};

// Owned by the SSH worker, so matching never blocks the GUI or scans old history.
class SshMonitorMatcher {
public:
  static QRegularExpression expression(const QString &pattern, bool ignoreCase) {
    // Bound pathological backtracking in both live matching and the editor's test preview.
    QRegularExpression re(QStringLiteral("(*LIMIT_MATCH=100000)(*LIMIT_DEPTH=1000)") + pattern,
        QRegularExpression::UseUnicodePropertiesOption |
        (ignoreCase ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption));
    if (pattern.isEmpty() || pattern.size() > 4096) return QRegularExpression(QStringLiteral("("));
    return re;
  }
  static QVariantMap test(const QString &pattern, bool ignoreCase, const QString &sample) {
    const auto re = expression(pattern, ignoreCase);
    QVariantMap result{{"valid", re.isValid()}, {"error", re.isValid() ? QString() :
        pattern.isEmpty() ? QStringLiteral("请输入正则表达式") : re.errorString()}, {"matches", QVariantList{}}};
    if (!re.isValid()) return result;
    QVariantList matches;
    qsizetype base = 0;
    const auto text = sample.left(65536);
    for (const auto &line : text.split('\n')) {
      auto iterator = re.globalMatch(line);
      if (!iterator.isValid()) { result["error"] = QStringLiteral("表达式运行超出匹配限制，请简化表达式"); break; }
      while (iterator.hasNext() && matches.size() < 128) {
        const auto match = iterator.next();
        matches.append(QVariantMap{{"start", base + match.capturedStart()}, {"end", base + match.capturedEnd()}});
      }
      base += line.size() + 1;
      if (matches.size() == 128) break;
    }
    result["matches"] = matches;
    result["truncated"] = sample.size() > text.size() || matches.size() == 128;
    return result;
  }
  static bool validRules(const QVariantList &rules) {
    if (rules.size() > 256) return false;
    QList<int> ids;
    for (const auto &entry : rules) {
      const auto rule = entry.toMap();
      const int id = rule.value("id").toInt();
      if (id < 1 || rule.value("id").toDouble() != id || ids.contains(id)
          || rule.value("name").metaType().id() != QMetaType::QString || rule.value("pattern").metaType().id() != QMetaType::QString
          || rule.value("ignoreCase").metaType().id() != QMetaType::Bool
          || (rule.contains("pauseOnMatch") && rule.value("pauseOnMatch").metaType().id() != QMetaType::Bool)
          || (rule.contains("highlightWholeLine") && rule.value("highlightWholeLine").metaType().id() != QMetaType::Bool)
          || rule.value("name").toString().trimmed().isEmpty() || rule.value("name").toString().size() > 100
          || (rule.contains("color") && !QRegularExpression(QStringLiteral("^#[0-9a-fA-F]{6}$")).match(rule.value("color").toString()).hasMatch())
          || !expression(rule.value("pattern").toString(), rule.value("ignoreCase").toBool()).isValid()) return false;
      ids.append(id);
    }
    return true;
  }
  void configure(const QVariantList &rules, quint64 revision, quint64 logicalId,
                 const QString &partial, qsizetype segmentOffset) {
    m_rules.clear();
    for (const auto &entry : rules) {
      const auto rule = entry.toMap();
      m_rules.push_back({rule.value("id").toInt(), rule.value("name").toString(), rule.value("color", QStringLiteral("#e8b76a")).toString(),
          expression(rule.value("pattern").toString(), rule.value("ignoreCase").toBool()), rule.value("pauseOnMatch", true).toBool(),
          rule.value("highlightWholeLine", true).toBool()});
    }
    m_revision = revision; m_logicalId = logicalId; m_baseline = partial.isEmpty() ? -1 : partial.size();
    m_segmentOffset = segmentOffset; m_prefix.clear(); m_hit = {}; m_latched = m_rules.empty();
    m_notified.clear();
  }
  void feed(std::vector<SshLogLine> &lines, SshLogLine &partial) {
    if (m_latched) return;
    m_hit = {};
    for (auto &line : lines) {
      advanceLine(line.logicalId);
      m_prefix += line.text;
      if (m_prefix.size() > SshLogStream::HistoryBytes / qsizetype(sizeof(QChar))) {
        fail(QStringLiteral("当前日志行超过检测缓存上限，匹配已停止")); return;
      }
      if (!line.continues) {
        mark(line, m_prefix, m_prefix.size() - line.text.size());
        if (m_latched) return;
      }
    }
    advanceLine(partial.logicalId);
    if (!partial.text.isEmpty()) {
      const auto text = m_prefix + partial.text;
      mark(partial, text, m_prefix.size());
    }
  }
  const SshMonitorHit &hit() const { return m_hit; }
  SshMonitorHit takeHit() { return std::exchange(m_hit, {}); }

private:
  struct Rule { int id; QString name; QString color; QRegularExpression re; bool pause; bool wholeLine; };
  void mark(SshLogLine &line, const QString &text, qsizetype offset) {
    const Rule *hitRule = nullptr;
    QRegularExpressionMatch hitMatch;
    bool hitPause = false;
    QList<SshMonitorMark> marks;
    for (const auto &rule : m_rules) {
      auto iterator = rule.re.globalMatch(text);
      if (!iterator.isValid()) { fail(QStringLiteral("规则「%1」运行超出匹配限制，请简化表达式").arg(rule.name)); return; }
      bool matched = false;
      QRegularExpressionMatch firstMatch;
      QList<QPair<int, int>> ranges;
      while (iterator.hasNext()) {
        const auto found = iterator.next();
        if (found.capturedStart() < m_baseline || found.capturedEnd() <= m_baseline) continue;
        if (!matched) { matched = true; firstMatch = found; }
        if (!rule.wholeLine && found.capturedLength() > 0)
          ranges.append({int(m_segmentOffset + found.capturedStart()), int(found.capturedLength())});
        if (rule.wholeLine) break;
      }
      if (!matched) continue;
      marks.append({rule.id, rule.name, rule.color, rule.wholeLine, m_segmentOffset + offset, ranges});
      if (!hitRule || (rule.pause && !hitPause)) {
        hitRule = &rule;
        hitMatch = firstMatch;
        hitPause = rule.pause;
      }
    }
    line.setMonitorMarks(std::move(marks));
    if (!hitRule || m_notified.contains(hitRule->id)) return;
    m_notified.insert(hitRule->id);
    const auto start = qMax(qsizetype(0), hitMatch.capturedStart() - 1024);
    QVariantList hitRanges;
    for (const auto &mark : line.monitorMarks)
      if (mark.ruleId == hitRule->id) { hitRanges = mark.visibleRanges(line.text); break; }
    m_hit = {m_revision, m_logicalId, m_segmentOffset + hitMatch.capturedStart(),
        {{"ruleId", hitRule->id}, {"ruleName", hitRule->name}, {"color", hitRule->color}, {"pauseOnMatch", hitRule->pause},
         {"highlightWholeLine", hitRule->wholeLine}, {"ranges", hitRanges},
         {"text", text.mid(start, 4096)}, {"matchStart", hitMatch.capturedStart() - start}, {"matchLength", hitMatch.capturedLength()},
         {"truncated", start > 0 || text.size() > start + 4096}}};
    m_latched = hitRule->pause;
  }
  void advanceLine(quint64 logicalId) {
    if (logicalId == m_logicalId) return;
    m_logicalId = logicalId; m_baseline = -1; m_segmentOffset = 0; m_prefix.clear(); m_notified.clear();
  }
  void fail(const QString &error) {
    m_latched = true;
    m_hit = {m_revision, m_logicalId, 0, {{"error", error}}};
  }
  std::vector<Rule> m_rules;
  quint64 m_revision = 0, m_logicalId = 0;
  qsizetype m_baseline = 0, m_segmentOffset = 0;
  QString m_prefix;
  bool m_latched = true;
  SshMonitorHit m_hit;
  QSet<int> m_notified;
};
