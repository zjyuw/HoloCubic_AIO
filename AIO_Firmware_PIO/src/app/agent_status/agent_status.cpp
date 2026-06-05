#include "agent_status.h"
#include "agent_status_gui.h"
#include "sys/app_controller.h"
#include "common.h"
#include "network.h" // WiFi / WebServer / mDNS

#define AGENT_STATUS_APP_NAME "Agent Status"
#define MDNS_HOST "agentstatus"   // -> http://agentstatus.local/
#define WIFI_ALIVE_INTERVAL 5000  // 维持wifi心跳的间隔(ms)
#define WIFI_RETRY_INTERVAL 5000  // 未连接时重试请求wifi的间隔(ms)

// 本应用自己的 HTTP 服务（与 server app 的全局 server 互不影响，前台互斥运行）
static WebServer agentServer(80);

// 动态数据
struct AgentStatusAppRunData
{
    bool server_started;            // HTTP 服务是否已启动
    bool dirty;                     // 状态是否有更新待刷新到屏幕
    char disp_text[32];             // 当前显示的状态文字
    uint32_t color;                 // 当前主题色 0xRRGGBB
    bool busy;                      // 是否显示忙碌动画
    char ip_str[28];                // 底部显示的地址
    unsigned long lastAliveMillis;  // 上次发wifi心跳
    unsigned long lastWifiTryMillis;// 上次请求wifi连接
};

static AgentStatusAppRunData *run_data = NULL;

// ---- 状态映射：把 Hook 传来的 state 字符串映射为 文字/颜色/是否忙碌 ----
static void apply_state(const char *raw)
{
    if (NULL == run_data)
        return;

    const char *text = raw;
    uint32_t color = 0xD97757; // 默认 Claude 橙
    bool busy = true;

    if (!strcmp(raw, "thinking"))
    {
        text = "Thinking";
        color = 0x4A90E2; // 蓝
        busy = true;
    }
    else if (!strcmp(raw, "working") || !strcmp(raw, "coding") || !strcmp(raw, "tool"))
    {
        text = "Working";
        color = 0x2EC4B6; // 青
        busy = true;
    }
    else if (!strcmp(raw, "approval") || !strcmp(raw, "permission"))
    {
        text = "Needs approval";
        color = 0xE5484D; // 红
        busy = false;
    }
    else if (!strcmp(raw, "idle") || !strcmp(raw, "done") || !strcmp(raw, "stop"))
    {
        text = "Idle";
        color = 0x3FB950; // 绿
        busy = false;
    }
    else if (!strcmp(raw, "offline"))
    {
        text = "Offline";
        color = 0x6E7681; // 灰
        busy = false;
    }
    // 其他未知状态：原样显示传入文字

    strncpy(run_data->disp_text, text, sizeof(run_data->disp_text) - 1);
    run_data->disp_text[sizeof(run_data->disp_text) - 1] = '\0';
    run_data->color = color;
    run_data->busy = busy;
    run_data->dirty = true;
}

// 用主题色驱动板载 RGB 灯（静态色，避免被后台HSV任务覆盖）
static void set_led_from_color(uint32_t color)
{
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    RgbParam p = {LED_MODE_RGB,
                  r, g, b,
                  r, g, b,
                  0, 0, 0,
                  200, 200, // 亮度 0~1000
                  0, 50};
    set_rgb_and_run(&p);
}

// ---- HTTP 处理 ----
static void handle_status()
{
    String st = agentServer.arg("state");
    if (st.length() == 0)
    {
        agentServer.send(400, "text/plain", "missing ?state=");
        return;
    }
    apply_state(st.c_str());
    agentServer.send(200, "text/plain", "ok");
}

static void start_http()
{
    agentServer.on("/status", handle_status);
    agentServer.on("/", []()
                   { agentServer.send(200, "text/plain",
                                      "Agent Status. Use: GET /status?state=thinking|working|approval|idle|offline"); });
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

    run_data = (AgentStatusAppRunData *)calloc(1, sizeof(AgentStatusAppRunData));
    run_data->server_started = false;
    run_data->dirty = false;
    run_data->busy = true;
    run_data->color = 0xD97757;
    strcpy(run_data->disp_text, "WiFi...");
    strcpy(run_data->ip_str, "connecting...");
    run_data->lastAliveMillis = 0;
    run_data->lastWifiTryMillis = GET_SYS_MILLIS();

    // 初始界面：连接中
    agent_status_gui_set_state(run_data->disp_text, run_data->color, run_data->busy);
    agent_status_gui_set_ip(run_data->ip_str);

    // 请求以 STA 模式连接已配置的 WiFi
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

    bool connected = (WiFi.status() == WL_CONNECTED);

    if (!run_data->server_started)
    {
        if (connected)
        {
            // WiFi 已连上 -> 启动 HTTP 服务，显示地址，进入就绪
            start_http();
            run_data->server_started = true;
            run_data->lastAliveMillis = GET_SYS_MILLIS();
            snprintf(run_data->ip_str, sizeof(run_data->ip_str),
                     "http://%s", WiFi.localIP().toString().c_str());
            agent_status_gui_set_ip(run_data->ip_str);
            apply_state("idle"); // 就绪
        }
        else
        {
            // 仍在连接：周期性重发连接请求
            if (doDelayMillisTime(WIFI_RETRY_INTERVAL, &run_data->lastWifiTryMillis, false))
            {
                sys->send_to(AGENT_STATUS_APP_NAME, CTRL_NAME,
                             APP_MESSAGE_WIFI_CONN, NULL, NULL);
            }
        }
    }
    else
    {
        // 已就绪：处理 HTTP 请求
        agentServer.handleClient();

        // 维持 WiFi 不被省电模式关闭
        if (doDelayMillisTime(WIFI_ALIVE_INTERVAL, &run_data->lastAliveMillis, false))
        {
            sys->send_to(AGENT_STATUS_APP_NAME, CTRL_NAME,
                         APP_MESSAGE_WIFI_ALIVE, NULL, NULL);
        }

        // WiFi 掉线则回到连接状态
        if (!connected)
        {
            run_data->server_started = false;
            agentServer.stop();
            strcpy(run_data->ip_str, "reconnecting...");
            apply_state("offline");
            agent_status_gui_set_ip(run_data->ip_str);
        }
    }

    // 有状态更新则刷新屏幕与灯光
    if (run_data->dirty)
    {
        agent_status_gui_set_state(run_data->disp_text, run_data->color, run_data->busy);
        set_led_from_color(run_data->color);
        run_data->dirty = false;
    }

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
    // WiFi 连接/心跳的回调，轮询已覆盖，无需特殊处理
}

APP_OBJ agent_status_app = {AGENT_STATUS_APP_NAME, &app_agent_status,
                            "Author Claude\nVersion 0.1.0\n",
                            agent_status_init, agent_status_process,
                            agent_status_background_task,
                            agent_status_exit_callback, agent_status_message_handle};
