# 任务清单

任务：共用解析表格与 v5.0.0 发布。需求来源：[spec.md](spec.md)。建立于 2026-10-26，已完成事项按实际时间补录。

| 任务 | 对应需求 | 当前记录与验收 |
|---|---|---|
| T01 共用解析表格 | PARSE-01 | 2026-10-25 已实现：主解析区和独立窗口使用同一组件 |
| T02 层级与说明主次 | PARSE-02、03、06 | 2026-10-25 已实现；模型、1,000 项集合及特殊字符检查已有本地证据 |
| T03 交互与列宽兼容 | PARSE-04、05 | 2026-10-25 已实现；字节定位、复制、重解析、对比行高、多页及设置恢复已有本地证据 |
| T04 固定目录本地测试包 | PARSE-01～06 | 2026-10-25 已交付 4.9.1 本地测试包；126 个随包二进制、隔离成品 Cocoa 回归通过；不代表正式发布 |
| T05 建立六份流程文档 | PROCESS-01 | 2026-10-26 已建立并逐一核对，见本文件及 test-report.md / review-record.md |
| T06 准备 v5.0.0 版本与主要说明 | RELEASE-01 | 版本一致；现有生成器在临时仓库验证主要说明通过；实际项目标签仍受 T08 门禁约束 |
| T07 提交前回归与审查 | PARSE-01～06、PROCESS-01 | 自审发现两项并已处理；构建、38 项发布工具、83 / 76 项 QML、4 项应用/协议回归通过，见测试与审查记录 |
| T08 推送并验证双平台构建 | RELEASE-02 | 发布执行任务；完整提交、Actions run、双平台状态与产物来源见下方发布执行记录 |
| T09 标签、正式发布与公开更新验证 | RELEASE-03 | 发布执行任务；依赖 T08 成功，标签、Release、附件摘要与公开更新结果见下方发布执行记录；不得用计划代替成功证据 |

## 发布执行记录

远端仓库：`https://github.com/jsnjjnzx/LogAnalyQt`。目标标签：`v5.0.0`。发布执行与产物校验记录放在 `release_package/v5.0.0-release-verification.json`（本地生成验收附件，不纳入源代码提交），公开结果以 GitHub 对应提交的构建、标签发布与公开更新工作流为准。不得用前一版本的成功状态替代本次验收。

该执行记录随推送、构建、发布与更新验收更新；源代码文档记录已完成的本地事实和强制门禁，远端结果引用该附件及其 GitHub 链接。正式页面：[v5.0.0 Release](https://github.com/jsnjjnzx/LogAnalyQt/releases/tag/v5.0.0)；工作流：[main 构建](https://github.com/jsnjjnzx/LogAnalyQt/actions/workflows/build.yml)、[标签发布与公开更新](https://github.com/jsnjjnzx/LogAnalyQt/actions/workflows/release.yml)。本条引用不代表在页面实际发布前已有正式版本。

保留现有未跟踪 `validation/ssh-monitor-windows/`，不纳入本次提交。
