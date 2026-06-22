# Agent Status APP 使用说明

在 HoloCubic 上实时显示 **Claude Code / AI Agent 的工作状态**：用屏幕图标 + 板载 RGB 灯光呈现"思考 / 工作 / 等待授权 / 空闲"等状态；并可把 SD 卡里的自定义图片做**全屏轮播**。

状态由 Claude Code 的 **hooks** 在本机生成、通过 HTTP 推送给设备，设备只是一个被动的状态显示器。

```
Claude Code 事件  →  hook 触发  →  holocubic-status.sh  →  HTTP  →  HoloCubic 显示 + 灯光
```

---

## 1. 刷写固件

与原版一致，用 PlatformIO 编译 `AIO_Firmware_PIO` 工程并烧录（`env = HoloCubic_AIO_Releases`）。本分支已包含 `Agent Status` APP，无需额外配置。

---

## 2. 连接 WiFi（配网）

Agent Status 用 **station 模式**联网，凭据存在 SD 卡的 `/sys.cfg`，配网方式与原版完全一致：

1. 在设备上打开 **`WebServer`** APP —— 它会开启一个名为 **`HoloCubic_AIO`** 的开放热点。
2. 手机/电脑连上 `HoloCubic_AIO` 热点，浏览器打开屏幕上显示的 IP（或 `http://holocubic`）。
3. 进入 **「系统设置」**，填入你的 **2.4GHz** WiFi 名称和密码（最多可填 3 个），保存。
4. 回到 **Agent Status** APP，它会自动连接刚配置的 WiFi。

> ⚠️ 必须是 **2.4GHz** WiFi，ESP32 不支持 5GHz。
> 设备的局域网 IP 会显示在 Agent Status 的**第 3 页「信息页」**（`IP ADDRESS`），也会打印在串口。后面配置脚本要用到它。

---

## 3. 三个页面（左右拨动切换）

进入 Agent Status 后，左右拨动在三页之间切换，**停在哪页就停在哪页（不会自动翻页）**：

| 页 | 内容 | 状态如何体现 |
|---|---|---|
| **0 图片轮播页** | SD 卡 `/AgentStatus` 里的图片全屏轮播；无图时显示提示 | **仅 RGB 灯光** |
| **1 默认图标页** | 内置动态图标 + 状态文字 | 图标动画 + 文字 + 灯光 |
| **2 信息页** | 设备 IP / mDNS 域名 | —— |

---

## 4. HTTP 接口

设备在 80 端口提供：

```
GET http://<设备IP>/status?state=<状态>[&seq=<微秒时间戳>]
```

- `state`：状态字符串（见下表）。
- `seq`（可选）：单调递增的微秒时间戳，设备用它**丢弃乱序到达的旧请求**；缺省则总是接受。
- 返回 `ok` / `stale`（被 seq 判为过期）/ `400`（缺少 `state`）。

手动测试：

```sh
curl "http://192.168.1.123/status?state=working"   # 把 IP 换成你设备的
```

### 状态值 → 显示 / 灯光

| state（含别名） | 屏幕文字 | 文字色 | 灯光 |
|---|---|---|---|
| `thinking` | Thinking | 白 | 蓝色慢呼吸 |
| `working` / `coding` / `tool` | Working | 亮黄 | 黄色快呼吸 |
| `approval` / `permission` | Approval | 琥珀 | **红色快闪** |
| `idle` / `done` / `stop` | Idle | 奶白 | 暖白慢呼吸 |
| `offline` | Offline | 灰 | 灰色恒亮 |
| 其它任意字符串 | 原样显示 | 白 | 白色慢呼吸 |

> 文字与图标动画在**默认图标页**呈现；在**图片轮播页**则只用 RGB 灯光反映状态。

---

## 5. 自定义图片轮播

在 SD 卡**根目录**新建 `/AgentStatus` 目录，放入图片即可；Agent Status 启动时扫描该目录：

- **有图片** → 第 0 页全屏轮播（默认每 **5 秒**一张）。
- **没有图片** → 第 0 页显示提示文字，行为回退（仍可用其它页）。

### 图片格式要求

| 项 | 要求 |
|---|---|
| 格式 | **baseline JPEG**（`.jpg` / `.jpeg`）。**不支持 progressive JPEG**（手机/PS"存储为 Web"常导出渐进式，会被跳过，请存成基线）。 |
| 分辨率 | **任意**。固件用 TJpgDec 自动按 1/2/4/8 降采样，再缩放铺满屏幕（屏幕是正方形，**建议用方图**）。 |
| 不支持 | PNG / GIF / BMP（LVGL 对应解码器未开启）、带透明通道。 |

### 内存注意（重要）

HoloCubic 用的 ESP32-PICO **无 PSRAM**，约 320KB SRAM 要和 WiFi、LVGL 共用。解码缓冲过大（如 80KB）会**把堆吃爆 → 连不上 WiFi / 崩溃**。因此：

- 默认 `AGENT_PHOTO_MAX = 128`（解码缓冲 32KB），靠缩放铺满，画质略软但稳定。
- 代码里加了"堆不足就不分配图片缓冲（只显示提示）"的保险，启动时串口会打印一行 `[AgentPhoto] free heap=...`，方便你查看余量。
- 想更清晰可在 [`AIO_Firmware_PIO/src/app/agent_status/agent_status_photo.cpp`](../AIO_Firmware_PIO/src/app/agent_status/agent_status_photo.cpp) 调大 `AGENT_PHOTO_MAX`，但**有风险**（可能 WiFi 连不上），请对照串口余量谨慎调，失败就调回 128。

> 可以直接用本分支的 **网页文件管理器**（WebServer APP → `http://<设备IP>/fs`）把图片拖进 `/AgentStatus`，免插拔 SD 卡。

---

## 6. 配置 Claude Code hooks

让 Claude Code 在不同事件触发时调用转发脚本，把状态发给设备。

### 第 1 步：放置转发脚本

把本仓库的 [`tools/holocubic-status.sh`](../tools/holocubic-status.sh) 复制到 `~/.claude/holocubic-status.sh`，**改掉里面的 IP**，并加可执行权限：

```sh
cp tools/holocubic-status.sh ~/.claude/holocubic-status.sh
chmod +x ~/.claude/holocubic-status.sh
# 编辑 ~/.claude/holocubic-status.sh，把 IP="192.168.1.123" 改成你设备「信息页」上的 IP
```

脚本要点（已内置）：**同步发送**（不要用 `&` 放后台，否则会被 hook 进程组回收丢更新）、**原子锁 + 去重**（ESP32 并发槽位少，避免被请求洪流打垮）、**seq 防乱序**。依赖 `curl` 和 `perl`（macOS 自带；`perl` 仅用于生成微秒 seq，缺了也能降级工作）。

### 第 2 步：配置 hooks

在 `~/.claude/settings.json` 写入（已有内容则合并 `hooks` 字段）：

```json
{
  "hooks": {
    "SessionStart":     [ { "hooks": [ { "type": "command", "command": "~/.claude/holocubic-status.sh idle" } ] } ],
    "UserPromptSubmit": [ { "hooks": [ { "type": "command", "command": "~/.claude/holocubic-status.sh thinking" } ] } ],
    "PreToolUse":       [ { "matcher": "*", "hooks": [ { "type": "command", "command": "~/.claude/holocubic-status.sh working" } ] } ],
    "Stop":             [ { "hooks": [ { "type": "command", "command": "~/.claude/holocubic-status.sh idle" } ] } ],
    "Notification":     [ { "matcher": "*", "hooks": [ { "type": "command", "command": "~/.claude/holocubic-status.sh approval" } ] } ],
    "PermissionRequest":[ { "matcher": "*", "hooks": [ { "type": "command", "command": "~/.claude/holocubic-status.sh approval" } ] } ]
  }
}
```

### 事件 → 状态映射

| Claude Code hook | 触发时机 | 发送状态 |
|---|---|---|
| `SessionStart` | 会话开始 | `idle` |
| `UserPromptSubmit` | 你提交提问 | `thinking` |
| `PreToolUse` | 每次调用工具前（跑命令/改文件） | `working` |
| `PermissionRequest` / `Notification` | 需要授权 / 通知 | `approval` |
| `Stop` | 一轮回答结束 | `idle` |

> 几个要点：
> - **同步（不要加 `"async": true`）**：async 会让各 hook 后台并发抢锁，导致到达设备的顺序 ≠ 事件顺序，偶发"该红不红""红了不灭"。同步执行才能保证顺序。
> - **`Notification` 必须带 `"matcher": "*"`** 才会触发（不带 matcher 在桌面版里不触发）。`PermissionRequest` 是更精准的"请求授权"事件。
> - hooks 修改**热加载**，一般无需重启 Claude Code。

---

## 7. 常见问题

| 现象 | 排查 |
|---|---|
| 灯/屏完全不动 | 设备 IP 是否填对（看信息页）？`curl "http://<IP>/status?state=working"` 能否单独点亮？设备和电脑是否同一局域网？ |
| Agent Status 里连不上 WiFi、甚至崩溃重启 | 多半是图片解码缓冲吃爆内存。先**清空 `/AgentStatus`** 看是否恢复；恢复则说明 `AGENT_PHOTO_MAX` 调太大，调回 128。 |
| 某张图不显示 | 是不是 progressive JPEG 或非 JPEG？只支持 baseline JPEG。 |
| `agentstatus.local` 打不开 | 部分网络 mDNS 不稳，直接用信息页上的 IP。 |
| 授权红灯慢半拍 | `Notification` 事件本身有约 1~2s 延迟（上游已知），无法完全消除。 |
