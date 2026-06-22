#include "agent_status.h"
#include "agent_status_gui.h"
#include "agent_status_weather.h"
#include "agent_status_photo.h"
#include "sys/app_controller.h"
#include "common.h"
#include "network.h" // WiFi / WebServer / mDNS

#define AGENT_STATUS_APP_NAME "Agent Status"
#define MDNS_HOST "agentstatus" // -> http://agentstatus.local/
#define MDNS_HOST_FULL "agentstatus.local"
#define WIFI_ALIVE_INTERVAL 5000 // 维持wifi心跳的间隔(ms)
#define WIFI_RETRY_INTERVAL 5000 // 未连接时重试请求wifi的间隔(ms)

// 页面索引（左右滑动：图片轮播 / 默认图标 / 信息）—— 天气页暂时移除（排查卡顿）
#define PAGE_PHOTO 0 // 图片轮播（无文字，状态仅靠灯光）
#define PAGE_ICON 1  // 默认动态图标 + 状态文字
#define PAGE_INFO 2  // 设备信息
#define PAGE_COUNT 3
#define PHOTO_CAROUSEL_MS 5000 // 自定义图片轮播间隔

// 本应用自己的 HTTP 服务
static WebServer agentServer(80);

// 灯光行为
enum LedMode
{
    LED_STATIC = 0,    // 恒亮
    LED_BREATHE_SLOW,  // 慢呼吸
    LED_BREATHE_FAST,  // 快呼吸
    LED_BLINK_FAST,    // 快速闪烁
};

struct AgentStatusAppRunData
{
    bool server_started;
    bool dirty;         // 状态有更新待刷新
    char disp_text[32]; // 状态文字
    uint32_t text_color; // 屏幕文字色 0xRRGGBB（亮色系）
    int screen_anim;    // 图标动画模式
    uint8_t led_h;      // 灯光色相(FastLED 0~255)
    uint8_t led_s;      // 灯光饱和度
    int led_mode;       // 灯光行为
    int cur_page;       // 当前页 0=状态 1=天气 2=信息
    char ip_str[24];    // IP
    unsigned long lastAliveMillis;
    unsigned long lastWifiTryMillis;
    // 自动翻页
    bool is_idle;                    // 当前是否 idle 状态
    bool snap_to_status;             // 收到状态事件 -> 切回状态页
    unsigned long lastEventMillis;   // 上次状态事件/交互的时间
    // 自定义图片轮播
    bool photo_mode;                 // /AgentStatus 有图片 -> 用图片替代动态图标
    unsigned long lastPhotoMillis;   // 上次切图的时间
};

static AgentStatusAppRunData *run_data = NULL;

// ---- 状态映射：state 字符串 -> 屏幕(文字/亮色/动画) + 灯光(色相/行为) ----
static void apply_state(const char *raw)
{
    if (NULL == run_data)
        return;

    const char *text = raw;
    uint32_t color = 0xFFFFFF;          // 屏幕字默认白
    int anim = AGENT_ANIM_SPIN_SLOW;    // 屏幕动画
    uint8_t lh = 0, ls = 0;             // 灯光 HSV 色相/饱和
    int lmode = LED_STATIC;             // 灯光行为
    bool idle = false;                  // 是否 idle

    if (!strcmp(raw, "thinking"))
    {
        text = "Thinking";
        color = 0xFFFFFF;               // 白字
        anim = AGENT_ANIM_SPIN_SLOW;    // 慢转+轻呼吸
        lh = 160; ls = 255;             // 蓝
        lmode = LED_BREATHE_SLOW;       // 慢呼吸
    }
    else if (!strcmp(raw, "working") || !strcmp(raw, "coding") || !strcmp(raw, "tool"))
    {
        text = "Working";
        color = 0xFFDD66;               // 亮黄字
        anim = AGENT_ANIM_SPIN_FAST;    // 快转+呼吸
        lh = 64; ls = 255;              // 黄
        lmode = LED_BREATHE_FAST;       // 快呼吸
    }
    else if (!strcmp(raw, "approval") || !strcmp(raw, "permission"))
    {
        text = "Approval";
        color = 0xFFA640;               // 亮琥珀字
        anim = AGENT_ANIM_PULSE;        // 快脉冲
        lh = 0; ls = 255;               // 正红
        lmode = LED_BLINK_FAST;         // 快闪
    }
    else if (!strcmp(raw, "idle") || !strcmp(raw, "done") || !strcmp(raw, "stop"))
    {
        text = "Idle";
        color = 0xF5F1E9;               // 奶白字
        anim = AGENT_ANIM_PULSE_SLOW;   // 缓慢脉冲
        lh = 28; ls = 55;               // 奶白(暖低饱和)
        lmode = LED_BREATHE_SLOW;       // 慢呼吸
        idle = true;
    }
    else if (!strcmp(raw, "offline"))
    {
        text = "Offline";
        color = 0x9A948A;               // 浅灰字
        anim = AGENT_ANIM_STATIC;       // 静止
        lh = 0; ls = 0;                 // 灰(无饱和)
        lmode = LED_STATIC;             // 恒亮
    }
    // 其他未知状态：原样显示，慢转，灯白慢呼吸
    else
    {
        ls = 0; lmode = LED_BREATHE_SLOW;
    }

    strncpy(run_data->disp_text, text, sizeof(run_data->disp_text) - 1);
    run_data->disp_text[sizeof(run_data->disp_text) - 1] = '\0';
    run_data->text_color = color;
    run_data->screen_anim = anim;
    run_data->led_h = lh;
    run_data->led_s = ls;
    run_data->led_mode = lmode;
    run_data->dirty = true;

    // 自动翻页：收到状态事件 -> 标记切回状态页 + 重置闲置计时
    run_data->is_idle = idle;
    run_data->snap_to_status = true;
    run_data->lastEventMillis = GET_SYS_MILLIS();
}

// 驱动板载 RGB 灯：HSV 模式，固定色相，振荡明度V做呼吸/闪烁（V用有符号步进，反向正常）
static void set_led(uint8_t h, uint8_t s, int mode)
{
    RgbParam p;
    p.mode = LED_MODE_HSV;
    p.min_value_h = h; p.max_value_h = h; p.step_h = 0; // 固定色相
    p.min_value_s = s; p.max_value_s = s; p.step_s = 0; // 固定饱和
    p.min_brightness = 900; p.max_brightness = 900;     // 背光恒定(绕开brightness_step溢出bug)
    p.brightness_step = 0;
    switch (mode)
    {
    case LED_BREATHE_SLOW:
        p.min_value_v = 20; p.max_value_v = 255; p.step_v = 3; p.time = 16;
        break;
    case LED_BREATHE_FAST:
        p.min_value_v = 20; p.max_value_v = 255; p.step_v = 10; p.time = 16;
        break;
    case LED_BLINK_FAST:
        p.min_value_v = 0; p.max_value_v = 255; p.step_v = 120; p.time = 70;
        break;
    case LED_STATIC:
    default:
        p.min_value_v = 120; p.max_value_v = 120; p.step_v = 0; p.time = 80;
        break;
    }
    set_rgb_and_run(&p);
}

// ---- HTTP ----
static uint64_t last_seq = 0; // 已接受的最大序号（用于丢弃乱序到达的旧更新）

static void handle_status()
{
    String st = agentServer.arg("state");
    if (st.length() == 0)
    {
        agentServer.send(400, "text/plain", "missing ?state=");
        return;
    }
    // 序号保护：异步 HTTP 请求可能乱序到达，丢弃比已见过的更旧（更小）的更新。
    // seq 为单调递增的微秒时间戳；缺省(无seq)则始终接受（向后兼容）。
    String sq = agentServer.arg("seq");
    if (sq.length() > 0)
    {
        uint64_t seq = strtoull(sq.c_str(), NULL, 10);
        if (seq != 0 && seq <= last_seq)
        {
            agentServer.send(200, "text/plain", "stale");
            return;
        }
        last_seq = seq;
    }
    apply_state(st.c_str());
    agentServer.send(200, "text/plain", "ok");
}

static void start_http()
{
    agentServer.on("/status", handle_status);
    agentServer.on("/", []()
                   { agentServer.send(200, "text/plain",
                                      "Agent Status. GET /status?state=thinking|working|approval|idle|offline"); });
    agentServer.onNotFound([]()
                           { agentServer.send(404, "text/plain", "not found"); });
    agentServer.begin();
    MDNS.begin(MDNS_HOST);
    MDNS.addService("http", "tcp", 80);
    Serial.println("[AgentStatus] HTTP server started");
}

static int agent_status_init(AppController *sys)
{
    agent_status_gui_init();
    agent_status_gui_create();
    // 天气页暂时移除（排查切页卡顿）：不再 agent_weather_build()

    run_data = (AgentStatusAppRunData *)calloc(1, sizeof(AgentStatusAppRunData));
    run_data->server_started = false;
    run_data->dirty = false;
    run_data->cur_page = 0;
    run_data->text_color = 0xFFFFFF;
    run_data->screen_anim = AGENT_ANIM_SPIN_SLOW;
    run_data->led_h = 0;
    run_data->led_s = 0;
    run_data->led_mode = LED_BREATHE_SLOW;
    strcpy(run_data->disp_text, "WiFi");
    strcpy(run_data->ip_str, "...");
    run_data->lastAliveMillis = 0;
    run_data->lastWifiTryMillis = GET_SYS_MILLIS();
    run_data->is_idle = false;
    run_data->snap_to_status = false;
    run_data->lastEventMillis = GET_SYS_MILLIS();

    agent_status_gui_set_state(run_data->disp_text, run_data->text_color, run_data->screen_anim);
    set_led(run_data->led_h, run_data->led_s, run_data->led_mode);
    agent_status_gui_set_info(run_data->ip_str, MDNS_HOST_FULL);

    // 扫描 SD 卡 /AgentStatus：有图片则在轮播页轮播，无图片则显示提示
    if (agent_photo_scan() > 0)
    {
        run_data->photo_mode = true;
        run_data->lastPhotoMillis = GET_SYS_MILLIS();
        const lv_img_dsc_t *d = agent_photo_next();
        if (NULL != d)
            agent_status_gui_set_photo(d);
        else
            agent_status_gui_photo_empty();
    }
    else
    {
        agent_status_gui_photo_empty();
    }

    sys->send_to(AGENT_STATUS_APP_NAME, CTRL_NAME,
                 APP_MESSAGE_WIFI_CONN, NULL, NULL);
    return 0;
}

static void agent_status_process(AppController *sys,
                                 const ImuAction *act_info)
{
    if (RETURN == act_info->active)
    {
        sys->app_exit();
        return;
    }
    else if (TURN_LEFT == act_info->active || TURN_RIGHT == act_info->active)
    {
        int np = run_data->cur_page + (TURN_LEFT == act_info->active ? 1 : -1);
        if (np >= 0 && np < PAGE_COUNT)
        {
            run_data->cur_page = np;
            agent_status_gui_goto_page(np);
            run_data->lastEventMillis = GET_SYS_MILLIS(); // 手动操作重置闲置计时
        }
    }

    bool connected = (WiFi.status() == WL_CONNECTED);

    if (!run_data->server_started)
    {
        if (connected)
        {
            start_http();
            run_data->server_started = true;
            run_data->lastAliveMillis = GET_SYS_MILLIS();
            snprintf(run_data->ip_str, sizeof(run_data->ip_str),
                     "%s", WiFi.localIP().toString().c_str());
            agent_status_gui_set_info(run_data->ip_str, MDNS_HOST_FULL);
            apply_state("idle");
        }
        else if (doDelayMillisTime(WIFI_RETRY_INTERVAL, &run_data->lastWifiTryMillis, false))
        {
            sys->send_to(AGENT_STATUS_APP_NAME, CTRL_NAME,
                         APP_MESSAGE_WIFI_CONN, NULL, NULL);
        }
    }
    else
    {
        agentServer.handleClient();

        if (doDelayMillisTime(WIFI_ALIVE_INTERVAL, &run_data->lastAliveMillis, false))
        {
            sys->send_to(AGENT_STATUS_APP_NAME, CTRL_NAME,
                         APP_MESSAGE_WIFI_ALIVE, NULL, NULL);
        }

        if (!connected)
        {
            run_data->server_started = false;
            agentServer.stop();
            strcpy(run_data->ip_str, "reconnecting");
            agent_status_gui_set_info(run_data->ip_str, MDNS_HOST_FULL);
            apply_state("offline");
        }
    }

    if (run_data->dirty)
    {
        agent_status_gui_set_state(run_data->disp_text, run_data->text_color, run_data->screen_anim);
        set_led(run_data->led_h, run_data->led_s, run_data->led_mode);
        run_data->dirty = false;
    }

    // ---- 自定义图片轮播（仅在图片页时切图，省得在其他页白解码）----
    if (run_data->photo_mode && PAGE_PHOTO == run_data->cur_page &&
        doDelayMillisTime(PHOTO_CAROUSEL_MS, &run_data->lastPhotoMillis, false))
    {
        const lv_img_dsc_t *d = agent_photo_next();
        if (NULL != d)
            agent_status_gui_set_photo(d);
    }

    // 不再自动翻页：停在用户停留的页；状态变化只由灯光（及默认图标页的文字/动画）反映。
    // 天气页暂时移除（排查切页卡顿）：不再 agent_weather_tick()

    delay(5);
}

static void agent_status_background_task(AppController *sys,
                                         const ImuAction *act_info)
{
}

static int agent_status_exit_callback(void *param)
{
    agentServer.stop();
    agentServer.close();
    MDNS.end();

    agent_status_gui_del();
    agent_photo_free(); // 释放图片列表与解码缓冲

    if (NULL != run_data)
    {
        free(run_data);
        run_data = NULL;
    }
    return 0;
}

static void agent_status_message_handle(const char *from, const char *to,
                                        APP_MESSAGE_TYPE type, void *message,
                                        void *ext_info)
{
}

APP_OBJ agent_status_app = {AGENT_STATUS_APP_NAME, &app_agent_status,
                            "Author Claude\nVersion 0.2.0\n",
                            agent_status_init, agent_status_process,
                            agent_status_background_task,
                            agent_status_exit_callback, agent_status_message_handle};
