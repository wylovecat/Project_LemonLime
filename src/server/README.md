# LemonLime Online — 嵌入式 HTTP 提交服务

让 LemonLime 自身在比赛期间扮演"在线提交服务端"的角色，机房学生通过浏览器访问 `http://老师机IP:8080` 即可登录并提交代码。所有提交直接落到 `<contest>/source/<username>/` 目录，老师在桌面端用既有的"全部评测"流程出成绩，无需改动 LemonLime 的评测逻辑。

## 目录约定

```
<contest>/
├── contest.cdf                 # LemonLime 既有
├── source/                     # 学生提交源码（自动生成）
├── data/                       # LemonLime 既有
├── statement.pdf               # 可选：题面 PDF（合订本，单文件）
├── online_users.json           # 自动生成：账号表（PBKDF2-SHA256）
├── online_submissions.log      # 自动生成：审计日志
└── online_users_passwords.csv  # 批量生成账号时自动落盘的明文清单
```

> **注意**：`online_users_passwords.csv` 是唯一保留明文密码的地方，且只在执行"批量生成"时才更新。请妥善保管，分发完即建议删除。

## 教师工作流

1. 用 LemonLime 桌面端打开比赛
2. 菜单 `Tools → Online Submission Service...`
3. 在弹出的对话框中：
   - 选择监听网卡（默认 `0.0.0.0`，即所有网卡都对外暴露）
   - 设端口（默认 `8080`）
   - 点 `Start`
4. （首次）点 `Batch Generate`，输入人数和用户名前缀，确认后会：
   - 在比赛目录写入 `online_users.json`
   - 自动落盘 `online_users_passwords.csv`（明文密码）
   - 屏幕表格也能看到全部账号
5. 把 CSV 里的账号密码分发给学生
6. 比赛结束 → 桌面端原生"全部评测"

## 学生工作流

- 打开浏览器访问 `http://<老师机IP>:8080`
- 输入账号密码登录
- 看到题目列表 + （若存在）右上角"题面"PDF 下载按钮
- 点某题进入提交页：可粘贴/编写代码，点 `提交代码` 即可
- 同一题多次提交，**以最后一次为准**（覆盖写入）
- 浏览器关闭/刷新不会丢稿（草稿暂存于 localStorage）

## 设计取舍

- **HTTP only**：机房局域网，避免自签证书警告；如需 HTTPS 需引入 OpenSSL 静态依赖并改 `SubmissionServer::start` 用 `QSslServer`
- **PBKDF2-SHA256 (100k iter)**：使用 Qt6 自带的 `QPasswordDigestor`，无第三方依赖；强度足以应对班级规模
- **textarea + 行号编辑器**：MVP 选择，编辑体验已能应付"从 IDE 粘贴提交"的主流场景。后续若要换 CodeMirror 6，只需替换 `src/server/assets/js/editor.js` 中 `LemonEditor.mount` 的实现即可，HTML 接口不变
- **不开放注册**：账号必须通过桌面端"批量生成"或"添加单个用户"创建；HTTP 服务端没有任何 `/register` 路由

## 依赖

- Qt 6.8 或更高
- Qt 模块：`Core` `Gui` `Widgets` `Network` `HttpServer` `Concurrent`
- 各发行版包名：
  - Arch: `qt6-httpserver`
  - Debian/Ubuntu: `libqt6httpserver6-dev`
  - Fedora: `qt6-qthttpserver-devel`
  - Windows / macOS: aqtinstall `qthttpserver` 模块

## 安全注意

- 默认监听 `0.0.0.0`：对整个局域网暴露。若机房有别人的电脑也能访问，请改绑特定网卡，或配合机房交换机/防火墙做隔离
- Cookie 用 `HttpOnly; SameSite=Lax`，但**没有** `Secure` 标志（因为是 HTTP）。若机房 Wi-Fi 不可信，建议有线接入，或后续启用 HTTPS
- 单次提交限制：默认 64 KB，可在 `SubmissionServer::setMaxSourceBytes` 调整
- 没有限速 / 反爆破：班级规模不需要；如需对外开放再加 `QHash<IP, count>` 滑窗
