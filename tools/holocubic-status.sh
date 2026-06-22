#!/bin/sh
# 把 Claude Code 的状态推送到 HoloCubic 的 "Agent Status" APP。
# 用法: holocubic-status.sh <state>
#   state: thinking | working | approval | idle | offline （其它字符串会原样显示）
#
# 配合 ~/.claude/settings.json 的 hooks 使用，把这个脚本放到 ~/.claude/holocubic-status.sh。
# 详见 docs/AgentStatus.md。
#
# 设备固件接口: GET http://<IP>/status?state=...&seq=...
#
# 设计要点：
# - 同步发送（不要用 "&" 放后台）：hook 进程组结束会把后台 curl 回收，导致丢更新。
# - 锁 + 去重：ESP32 WebServer 的并发 TCP 槽位很少，并发请求会把它打垮；用原子
#   mkdir 当互斥锁串行化，并跳过未变化的状态，使一串相同状态只发一个请求。
# - seq：单调递增的微秒时间戳，设备据此丢弃乱序到达的旧请求。

# ↓↓↓ 改成你设备的局域网 IP（Agent Status 第 3 页"信息页"上有显示）↓↓↓
IP="192.168.1.123"

STATE="$1"
[ -z "$STATE" ] && exit 0

DIR="${HOME}/.claude"
LOCK="${DIR}/.holocubic.lock" # mkdir 是原子操作 -> 当互斥锁
LAST="${DIR}/.holocubic.last" # 上次实际发出的状态

# --- 取锁；若上一次进程被杀没释放，约 3s 后破锁 ---
i=0
until mkdir "$LOCK" 2>/dev/null; do
    i=$((i + 1))
    if [ "$i" -ge 60 ]; then
        rmdir "$LOCK" 2>/dev/null
        i=0
    fi
    sleep 0.05
done
trap 'rmdir "$LOCK" 2>/dev/null' EXIT INT TERM

# --- 去重：状态没变就什么都不发 ---
prev=$(cat "$LAST" 2>/dev/null)
if [ "$STATE" = "$prev" ]; then
    exit 0
fi
printf '%s' "$STATE" > "$LAST"

# --- 发送（已串行化；seq 让设备丢弃乱序到达的旧请求）---
SEQ=$(perl -MTime::HiRes -e 'printf "%d", Time::HiRes::time()*1000000' 2>/dev/null)
curl -s --connect-timeout 1 -m 2 "http://$IP/status?state=$STATE&seq=$SEQ" >/dev/null 2>&1

exit 0
