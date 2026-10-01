# 在线提交前端 · 功能缺口 TODO

> **全部 45 项已完成（2026-09-30）。** 实施方式：不再手写增量，而是把部署版
> `E:\LemonLime-Online-windows\lemon.exe` 内嵌的那套 Web 资产**原样移植**进
> `src/server/assets/`（`default/{login,index,submit}.html`、`beijing/{login,index,submit}.html`、
> `css/app.css`、`js/app.js`、`js/editor.js`），服务端无需任何改动（第 0 组已把
> `/api/source`、`submitMode`、`preContest`、`pageStyle` 契约补齐）。
> 另修掉参考实现自身的 2 处缺陷：两个 `index.html` 补上 `#toast` 容器、
> 两个 `submit.html` 的 `字符数` 标签改为 `字节数`（`app.js` 计入的是 UTF-8 字节数）。
> 验收：编译通过 → 换 exe 重启 → GUI 切 `pageStyle=beijing` → 浏览器全流程走通
> （登录 / 题目列表 / 查看源码弹窗 / 提交页 / 考试须知弹窗 / POST `/api/submit`）。
> **来源**：`E:\LemonLime-Online-windows\lemon.exe`（部署版，**Qt 6.9.3** 构建，文件时间 2026/5/6 20:41）
> 它内嵌的 Web 资产比本仓库完整得多。本文件把「多出来的功能」逐条登记为待办。
>
> **取证副本**（从部署版二进制提取，已与 exe 内嵌资源字节级核对一致）：
> - `E:\Project_LemonLime\.ref\index_auth.html`     5387 B（登录后的题目列表页）
> - `E:\Project_LemonLime\.ref\assets_css_app.css`  24441 B（唯一被 zlib 压缩的 Web 资产）
> - `E:\Project_LemonLime\.ref\assets_js_app.js`    18293 B
> - `E:\Project_LemonLime\.ref\assets_js_editor.js` 1688 B
>
> **体量对照**
>
> | 文件 | 部署版 | 本仓库 | 说明 |
> |---|---|---|---|
> | `index.html` | 5387 B | 1686 B | 仓库版是「顶栏 + 表格」极简页 |
> | `css/app.css` | 24441 B（760 行） | 14163 B（433 行） | 含刚移植的北京风登录页样式 |
> | `js/app.js` | 18293 B | 9085 B | 部署版多出一倍客户端逻辑 |
> | `submit.html` | 6157 B（beijing 版） | 2977 B | 仓库版无主题、无文件模式 |
> | `login.html` | —（beijing 版） | 4640 B | **登录页移植已完成**，不计入差距 |
>
> CSS：部署版是仓库的**严格超集**（仓库独有选择器仅 2 条，且都被更宽的选择器覆盖）→ 主题 CSS 可整体替换，低风险。

---

## 0. 先决条件：主题目录模型 + 服务端契约（**已完成：2026-09-30 实现、编译、部署并通过 HTTP 验收**）

部署版把页面按主题分目录存放，由配置项 `pageStyle` 选择：

- [x] **`pageStyle` 配置项**（`default` | `beijing`）— 决定用哪套页面
      —证据：部署版 exe 字符串 `pageStyle` @2905472；`SubmissionServer.cpp:127-159` 的 `loadConfig/saveConfig` 是白名单式逐键读写，**无此键** — 依赖：`online_config.json` 模型 + 读写两处都要改
- [x] **主题化资源解析 + `default` 回退**
      —证据：exe 字符串 `:/online/%1/index.html` @2906400 与回退 `:/online/default/index.html` @2906448（login/submit 同构）— 依赖：改 `handleLoginPage/handleIndex/handleSubmitPage`
- [x] **重排 `server-assets.qrc` 为多主题目录**
      —证据：`src/server/server-assets.qrc` 只有 6 条扁平 alias（`login.html/index.html/submit.html/css/app.css/js/app.js/js/editor.js`），无 `default/`、`beijing/` 子目录 — 依赖：新增 `assets/default/*`、`assets/beijing/*`
- [x] **补齐 `default` 主题三件套** — 仓库**完全没有** default 版页面
      —证据：exe `default/login.html` @1357545（1379 B，`<body class="auth-body" data-theme="default">`）、`default/index.html` @1358929（2563 B）、`default/submit.html` @1361497（3619 B）— 依赖：`pageStyle=default` 回退需要
- [x] **新增 `GET /api/source/<arg>`** — 返回已提交源码供「查看」弹窗使用
      —证据：exe 字符串 `/api/source/` @1357808、响应键 `filename`/`content`/`submittedAt`/`bytes`、无记录时 `未找到提交记录` @2906954；`.ref/assets_js_app.js` 的 `openCodeModal()` → `fetch('/api/source/'+taskId,{credentials:'same-origin'})`；仓库 `SubmissionServer.cpp:216 setupRoutes()` 无此路由 — 依赖：**新增路由**
- [x] **`/api/tasks` 返回 `preContest`** — 赛前锁定题目列表
      —证据：exe 字符串 `preContest` @1354939；`SubmissionServer.cpp` 中 `preContest` 完全不存在；`.ref/assets_js_app.js` 的 `renderIndex()/renderSubmit()` 读 `data.preContest` — 依赖：扩展 `/api/tasks`
- [x] **`submitMode` 配置项 + `/api/tasks` 返回它**（`paste` | `file`）— 决定提交页用代码编辑器还是文件上传
      —证据：exe 字符串 `submitMode` @1361758 + 值 `paste`/`file`；仓库 `loadConfig/saveConfig` 无此键 — 依赖：配置模型 + `/api/tasks`
- [x] **HTML 响应加缓存头 `no-cache, must-revalidate`** — 否则切换主题/改配置后浏览器仍用旧页
      —证据：exe 字符串 @2906000；仓库 `handleLoginPage/handleIndex/handleSubmitPage/handleStatementPdf` 均未设 HTML 缓存头（静态资源是 `private, max-age=300`）— 依赖：仅改响应头

## A. 首页 `index.html`（11 项，**已完成**）

- [x] **`<body data-theme="beijing">` 主题开关** — 缺此属性则所有 `bj-*` 样式与 `#bjClock` 逻辑整体失效
      —证据：`.ref/index_auth.html:9` vs 仓库 `src/server/assets/index.html` 无该属性 — 依赖：无
- [x] **`bj-header` 顶栏 + `bj-header-logout-form`/`bj-logout-btn` 退出登录按钮**
      —证据：`.ref/index_auth.html`；`.ref/assets_css_app.css` 的 `.bj-header-logout-form`/`.bj-logout-btn` — 依赖：无（`/logout` 已存在）
- [x] **`bj-sidebar` 左侧固定栏**（240px 版式 + body 左内边距，新增变量 `--bj-side-w`）
      —证据：`.ref/index_auth.html` 的 `.bj-sidebar`/`.bj-sb-menu`/`.bj-sb-row`/`.bj-sb-item`/`.bj-sb-icon` — 依赖：无
- [x] **`#bjClock` 侧栏实时时钟**
      —证据：`.ref/assets_js_app.js` 的 `bootBjClock()` `bjClockTick()`（仅当 `document.body.dataset.theme === 'beijing'` 时启动）— 依赖：无
- [x] **`#bjUser` 侧栏当前用户**
      —证据：`.ref/assets_js_app.js` 的 `bjPopulateUser(displayName)` — 依赖：无（复用已有 displayName）
- [x] **`#bjStatementLink` 题面下载 + `bj-sb-disabled` 无题面禁用态**
      —证据：`.ref/assets_js_app.js` 的 `bjUpdateStatementLink(hasStatement)`（无题面时一次性 click 拦截 `preventDefault`）— 依赖：无（复用 `/statement` 与 hasStatement）
- [x] **侧栏「考试须知」入口 `[data-bj-action="notice"]` → 打开 `#noticeModal`**
      —证据：`.ref/index_auth.html` 的 `#noticeModal`；`.ref/assets_js_app.js` 的 `closeAnyModal()` — 依赖：无
- [x] **侧栏「消息」入口 `[data-bj-action="messages"]` + `#bjMsgCount`**
      —证据：`.ref/index_auth.html` 的 `#bjMsgCount`（exe @1372235）
      —⚠️ **参考实现里计数恒为静态 `0`、点击只 toast「暂无未读消息」，未读统计根本没实现** → 不需要新 API，照抄即可
- [x] **侧栏 `is-active` / `aria-current="page"` 当前页高亮**
      —证据：`.ref/assets_css_app.css` 的 `[data-theme="beijing"] .bj-sb-item.is-active` — 依赖：无
- [x] **`.bj-deco` 装饰层**（内联 data-URI SVG `#circuit` 图案 + 六边形，无外部依赖）
      —证据：`.ref/index_auth.html`；`.ref/assets_css_app.css` 的 `.bj-deco` — 依赖：无
- [x] **`#bjCountdownLabel`/`#bjCountdownText` 行内倒计时**（beijing 下隐藏 `#countdown`）
      —证据：`.ref/assets_js_app.js` 的 `renderCountdown()` 接受**元素数组**并对 `bjCountdownText` 特判只输出数字；`.ref/assets_css_app.css` 的 `[data-theme="beijing"] #countdown { display:none }` — 依赖：无

## B. 首页题目列表 + 代码查看弹窗（6 项，**已完成**）

- [x] **第 7 列「查看」`view-col` + `colspan="7"`**
      —证据：仓库 `src/server/assets/index.html` 是 6 列 / `colspan="6"`；`.ref/index_auth.html` 为 7 列 — 依赖：无
- [x] **`.row-action` 查看链接 + 行点击/键盘守卫**
      —证据：`.ref/assets_js_app.js` 行渲染 `data-view`、`if (e.target.closest('[data-view]')) return;`、`tbody.querySelectorAll('[data-view]')` — 依赖：`/api/source/<arg>`
- [x] **`#codeModal` 代码查看弹窗**（overlay/card/head/meta/`#codeFilename`/`#codeSubmittedAt`/`#codeBytes`/`#codeContent`）
      —证据：`.ref/index_auth.html` 的 `#codeModal` `#codeContent` `#codeFilename` `#codeBytes`；`.ref/assets_js_app.js` 的 `openCodeModal()`（401 → `/login`；失败读 `j.error`）— 依赖：**`/api/source/<arg>`**
- [x] **`#codeCopyBtn` 一键复制**
      —证据：`.ref/assets_js_app.js` 的 `navigator.clipboard?.writeText(#codeContent.textContent)`，成功/失败双 toast — 依赖：无
- [x] **弹窗通用关闭机制**：`[data-modal-close]` 委托 + Escape 键 + `document.body.style.overflow` 锁定/恢复
      —证据：`.ref/assets_js_app.js` 的 `closeAnyModal(target)` — 依赖：无
- [x] **赛前锁定提示行 `locked-cell`**（`colspan="7"` + 「比赛尚未开始」+ 副标题）
      —证据：`.ref/assets_js_app.js` 的 `renderIndex()`；`.ref/assets_css_app.css` 的 `.locked-cell`/`.locked-title`/`.locked-sub` — 依赖：`preContest`

## C. 提交页 `submit.html`（10 项，**已完成**）

- [x] **`#pasteMode` / `#fileMode` 双模式容器与切换**
      —证据：部署版 `submit.html`；`.ref/assets_js_app.js` 的 `const mode = (data.submitMode === 'file') ? 'file' : 'paste'` + hidden 切换 — 依赖：`submitMode`
- [x] **`#fileMode .file-zone` 上传区 DOM**（`file-zone-inner`/`file-zone-icon`/`file-zone-title`/`file-zone-sub`）
      —证据：`.ref/assets_css_app.css` 的 `.file-zone*` — 依赖：无
- [x] **文件选择与拖拽交互**：`#fileInput` accept=`.cpp,.cc,.cxx,.c,.py,.pas,.pp,.txt`、`#filePickBtn`→`fileInput.click()`、`#fileDrop` click、dragenter/dragover→`.is-drag`、dragleave/drop→移除、drop→`ingestFile`
      —证据：`.ref/assets_js_app.js` — 依赖：无
- [x] **`ingestFile(file)` 校验与状态回显**：空文件「文件为空」、>64 KB「文件超过 64 KB」、读取失败「读取文件失败：」、`#fileTitle`「已选择：name」、`#fileSub`「N 字节 · 点击或拖入可替换」、`.has-file` 类
      —证据：`.ref/assets_js_app.js`；`.ref/assets_css_app.css` 的 `.file-zone-inner.has-file` — 依赖：无
- [x] **`extToLang` 映射 + `pickLangFromExt()`** 按扩展名自动选语言（cpp/cc/cxx/cp→cpp；c→c；h→cpp；py→python；pas/pp→pascal）
      —证据：`.ref/assets_js_app.js` — 依赖：无
- [x] **`getCurrentSource()` 模式抽象**（paste 取编辑器 / file 取 `uploadedSource`）
      —证据：`.ref/assets_js_app.js` — 依赖：无
- [x] **`#fontSizeField`**（字号 label 增加 id）仅 paste 模式可见
      —证据：`.ref/assets_js_app.js` 的 `#fontSizeField.style.display = mode === 'paste' ? '' : 'none'` — 依赖：无
- [x] **`doSubmit` 模式化空值提示**（file →「请选择文件」/ paste →「请输入代码」）
      —证据：`.ref/assets_js_app.js` — 依赖：无
- [x] **提交页 `preContest` 拦截并跳回首页**
      —证据：`.ref/assets_js_app.js` 的 `if (data.preContest) { location.href = '/'; return; }` — 依赖：`preContest`
- [x] **beijing 版提交页整套**（`bj-header`+退出登录 / `bj-sidebar`+时钟+用户名+菜单 / `.bj-deco` / `#bjCountdownLabel` 行内倒计时 / `#noticeModal`）
      —证据：exe `beijing/submit.html` @1394750（6157 B）— 依赖：`pageStyle`

## D. 共享 JS 行为（2 项，**已完成**）

- [x] **全局 `[data-bj-action]` click 委托**（`notice` → `#noticeModal.classList.add('is-open')`；`messages` → `showToast('暂无未读消息')`）
      —证据：`.ref/assets_js_app.js` — 依赖：无
- [x] **`renderCountdown` 多元素支持**（`.filter(Boolean)` + 逐元素 hidden/class + `bjCountdownText` 特判 `已结束`）
      —证据：`.ref/assets_js_app.js` — 依赖：无

## E. 主题 CSS（8 项，**已完成**：参考实现 CSS 整体替换）

- [x] 弹窗样式块 `.modal`/`.modal.is-open`/`.modal-overlay`/`.modal-card`/`.modal-head`/`.modal-close`/`.modal-meta`/`.modal-code`/`.modal-notice` + `@keyframes modal-fade`/`modal-pop`
      —证据：`.ref/assets_css_app.css` — 依赖：无
- [x] 文件上传区 `.file-zone`/`.file-zone-inner`/`.is-drag`/`.has-file`/`.file-zone-icon`/`.file-zone-title`/`.file-zone-sub` — 依赖：无
- [x] 锁定单元格 `.locked-cell`/`.locked-title`/`.locked-sub` — 依赖：无
- [x] 查看列与链接 `.task-table .view-col`/`.task-table tbody tr.no-hover:hover`/`.row-action`/`.row-action:hover` — 依赖：无
- [x] beijing 布局层 `body[data-theme="beijing"]`（padding/背景/侧栏避让）+ `.container` 覆盖 + 新增变量 `--bj-side-w`/`--bj-side-bg`/`--bj-side-bg-2`/`--bj-side-text` + 响应式（窄屏侧栏转横排、`.bj-sb-item{flex:1 0 50%}`）— 依赖：无
- [x] 侧栏样式族 `.bj-sidebar`/`.bj-sb-row`/`.bj-sb-item`/`.bj-sb-icon`/`.bj-sb-clock`/`.bj-sb-user`/`.bj-sb-menu`/`.bj-sb-item.is-active`/`:hover`/`.bj-sb-disabled`/`.bj-header-logout-form`/`.bj-logout-btn` — 依赖：无
- [x] `.topbar` 主题可见性切换（`[data-theme="default"] .topbar{display:block}` / `[data-theme="beijing"] .topbar{display:none}`）— 依赖：无
- [x] beijing 组件强调色（`contest-banner`/`contest-title`/`card` 顶边/`task-table thead th`/`tbody tr:hover`/`status-pill.is-submitted`/`row-action`/`task-meta-card`/`editor-toolbar`/`editor-foot`/`line-numbers`/`toast`/`toast.is-error`/`modal-card`/`modal-meta`/表单 `:focus-visible`）— 依赖：无

---

## 注意事项

1. **必须先做服务端，否则前端全是死代码。** `openCodeModal()` 硬依赖 `GET /api/source/<taskId>`；`handleApiTasks`（`src/server/SubmissionServer.cpp:358-405`）不返回 `preContest`/`submitMode`。只移植 HTML/CSS/JS 的结果是：点「查看」必然 404、赛前锁定行永不出现、文件模式永不激活。
2. **有两处是数据模型扩展，不是纯前端改动。** `online_config.json` 需新增 `submitMode`、`pageStyle`；`/api/tasks` 需新增 `preContest`、`submitMode`。`loadConfig/saveConfig` 是逐键白名单，加键必须同时改这两处及所有配置写入路径。
3. **资源路径模型变了。** 部署版按 `pageStyle` 取 `:/online/<pageStyle>/{login,index,submit}.html`，失败回退 `:/online/default/...`；仓库是扁平的 `:/online/index.html`。若只保留 beijing，`pageStyle=default` 会直接 404。
4. **`default` 主题三件套在仓库中完全不存在**（含 `default/login.html`，1379 B）。想实现 `pageStyle` 开关就得把这 3 个页面一并补上，工作量不止「补差」。
5. **参考实现自身有 3 处缺陷（前两处已修，第三处保留）：**
   - （**已修**）`index.html` 没有 `#toast` 容器 → 首页所有 `showToast` 静默退化为 `console.log`，`#codeCopyBtn` 的复制提示永远看不见。已给 default/beijing 两个 `index.html` 补 `<div class="toast" id="toast" hidden></div>`。
   - （**已修**）`#sizeText` 标签写死「字符数」，而 `app.js` 的 `refreshCount()` 计入的是 UTF-8 字节数 → 两个 `submit.html` 的标签改为「字节数」。
   - （保留）`#bjMsgCount` 恒为静态 `0`，「消息」入口只有 toast 占位；服务端本就没有消息接口，属装饰性占位。
6. **主题开关依赖真实的 `data-theme` 写入。** `bjClockTick` 只在 `document.body.dataset.theme === 'beijing'` 时启动。若页面由服务端模板拼接却忘了输出该属性，时钟与侧栏样式会整体失效**且无报错**——最容易漏、最难排查的点。
7. **HTML 缓存头不同。** 部署版对 HTML 用 `no-cache, must-revalidate`；仓库对 HTML 不设缓存头、对静态资源用 `private, max-age=300`。切主题/改配置后不改这里，浏览器可能继续用旧页面。
8. **`.ref/` 下是我自己的取证工作目录**（参考资产副本、预览架 `preview/`、截图 `*.png`、`build.bat`/`pack.bat`），不是仓库内容，也不属于部署版功能。
9. **不要用旧部署目录直接换 exe。** `E:\LemonLime-Online-windows\` 里的 Qt 运行库是 **6.9.3**，而当前构建环境是 **6.11.0**（`C:\Qt\6.11.0\msvc2022_64`），换 exe 会因 ABI 不匹配启动失败，必须整目录重新打包。
10. **未逐行比对**：`assets/js/editor.js`（部署版 1688 B vs 仓库版）、`assets/default/submit.html` 与仓库 `submit.html` 的细节差异。