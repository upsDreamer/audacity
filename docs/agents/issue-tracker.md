# Issue tracker：本地 Markdown

本仓库的问题（issue）与规格（spec）以 Markdown 文件形式存放在 `.scratch/` 目录中。

## 约定

- 每个功能一个目录：`.scratch/<feature-slug>/`
- 规格文档为 `.scratch/<feature-slug>/spec.md`
- 实现问题每个工单一个文件：`.scratch/<feature-slug>/issues/<NN>-<slug>.md`，从 `01` 开始编号，绝不合并为单个工单汇总文件
- 分诊状态记录在每个问题文件顶部附近的 `Status:` 行中（角色字符串见 `triage-labels.md`）
- 评论与会话历史追加到文件底部 `## Comments` 标题之下

## 当技能要求"发布到问题跟踪器"时

在 `.scratch/<feature-slug>/` 下创建新文件（必要时先创建目录）。

## 当技能要求"获取相关工单"时

读取所引用路径的文件。用户通常会直接给出路径或问题编号。

## Wayfinder 操作

供 `/wayfinder` 使用。**map** 是一个文件，每个工单对应一个 **child** 文件。

- **Map**：`.scratch/<effort>/map.md`（Notes / Decisions-so-far / Fog 正文）。
- **Child 工单**：`.scratch/<effort>/issues/NN-<slug>.md`，从 `01` 开始编号，正文写明要研究的问题。`Type:` 行记录工单类型（`research`/`prototype`/`grilling`/`task`）；`Status:` 行记录 `claimed`/`resolved`。
- **Blocking**：顶部附近的 `Blocked by: NN, NN` 行。当其列出的每个文件都为 `resolved` 时，该工单解除阻塞。
- **Frontier**：扫描 `.scratch/<effort>/issues/` 中处于打开、未阻塞、未认领状态的文件；编号最小者优先。
- **Claim**：开始任何工作前，先设置 `Status: claimed` 并保存。
- **Resolve**：在 `## Answer` 标题下追加答案，设置 `Status: resolved`，然后在 `map.md` 的 Decisions-so-far 中追加上下文指针（要点 + 链接）。
