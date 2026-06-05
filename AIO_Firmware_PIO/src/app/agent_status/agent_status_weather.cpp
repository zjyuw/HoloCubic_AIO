#include "agent_status_weather.h"
#include "app/weather/weather_gui.h" // struct Weather / TimeStr / FORECAST_DAYS
#include "common.h"                  // g_flashCfg / analyseParam / GET_SYS_MILLIS
#include "network.h"                 // WiFi / HTTPClient / TIMEZERO_OFFSIZE
#include "ArduinoJson.h"
#include "ESP32Time.h"
#include <map>

// ---- 复用天气 app 已编译进固件的资源（需 APP_WEATHER_USE=1）----
LV_FONT_DECLARE(lv_font_ibmplex_115);
LV_FONT_DECLARE(lv_font_ibmplex_64);
LV_FONT_DECLARE(ch_font20);
extern const void *weaImage_map[];     // 天气图标映射（weather_gui.c）
extern const void *manImage_map[];     // 走路小人映射（weather_gui.c）
extern const lv_img_dsc_t temp;        // 温度图标（weather_image.h）
extern const lv_img_dsc_t humi;        // 湿度图标（weather_image.h）
extern std::map<String, int> weatherMap; // 天气现象->图标码（weather.cpp）

// 高德接口（与天气 app 一致）
#define W_LIVES_API "http://restapi.amap.com/v3/weather/weatherInfo?key=%s&city=%s&extensions=base"
#define W_DAILY_API "http://restapi.amap.com/v3/weather/weatherInfo?key=%s&city=%s&extensions=all"
#define W_TIME_API "https://acs.m.taobao.com/gw/mtop.common.getTimestamp/"
#define W_CONFIG_PATH "/weather_2111.cfg" // 复用天气 app 的配置
#define W_REFRESH_MS 900000               // 15min

static const char weekDayCh[7][4] = {"日", "一", "二", "三", "四", "五", "六"};
static const char airQualityCh[6][10] = {"优", "良", "轻度", "中度", "重度", "严重"};

// 配置 + 数据
static String w_city = "北京", w_key = "";
static Weather wea;
static ESP32Time g_rtc;
static long long preNetTimestamp = 1577808000000LL;
static long long preLocalTimestamp = 0;
static bool fetched_once = false;
static unsigned long preWeatherFetch = 0, preNtpFetch = 0, preClockTick = 0, preManTick = 0;
static int manIndex = 0;

// GUI 对象
static lv_obj_t *weatherImg = NULL, *cityLabel = NULL, *airBtn = NULL, *airLabel = NULL;
static lv_obj_t *txtLabel = NULL, *clock1 = NULL, *clock2 = NULL, *dateLabel = NULL;
static lv_obj_t *tempImg = NULL, *tempBar = NULL, *tempLabel = NULL;
static lv_obj_t *humiImg = NULL, *humiBar = NULL, *humiLabel = NULL, *spaceImg = NULL;

static lv_style_t ch_style, numSmall_style, numBig_style, btn_style, bar_style;
static bool styles_inited = false;

static int w_extract_num(const char *s)
{
    int r = 0;
    for (; *s; ++s)
        if (*s >= '0' && *s <= '9')
            r = r * 10 + (*s - '0');
    return r;
}
static int w_air_level(const char *q)
{
    int n = w_extract_num(q);
    if (n <= 3)
        return 0;
    if (n <= 4)
        return 1;
    if (n <= 6)
        return 2;
    if (n <= 8)
        return 3;
    if (n <= 9)
        return 4;
    return 5;
}

static void w_read_config(void)
{
    char info[128] = {0};
    uint16_t size = g_flashCfg.readFile(W_CONFIG_PATH, (uint8_t *)info);
    info[size] = 0;
    if (size == 0)
        return; // 用默认
    char *param[5] = {0};
    analyseParam(info, 5, param);
    // param: [url, city, key, weatherInterval, timeInterval]
    if (param[1])
        w_city = param[1];
    if (param[2])
        w_key = param[2];
}

// ---- 数据获取（同步，与天气 app 一致）----
static void w_get_weather(void)
{
    if (WL_CONNECTED != WiFi.status() || w_key.length() == 0)
        return;
    HTTPClient http;
    http.setTimeout(1000);
    char api[160] = {0};
    snprintf(api, sizeof(api), W_LIVES_API, w_key.c_str(), w_city.c_str());
    http.begin(api);
    int code = http.GET();
    if (code == HTTP_CODE_OK || code == HTTP_CODE_MOVED_PERMANENTLY)
    {
        String payload = http.getString();
        DynamicJsonDocument doc(768);
        deserializeJson(doc, payload);
        if (doc.containsKey("lives"))
        {
            JsonObject L = doc["lives"][0];
            strncpy(wea.cityname, L["city"].as<String>().c_str(), sizeof(wea.cityname) - 1);
            wea.cityname[sizeof(wea.cityname) - 1] = 0;
            wea.temperature = L["temperature"].as<int>();
            wea.humidity = L["humidity"].as<int>();
            wea.weather_code = weatherMap[L["weather"].as<String>()];
            strncpy(wea.weather, L["weather"].as<String>().c_str(), sizeof(wea.weather) - 1);
            wea.weather[sizeof(wea.weather) - 1] = 0;
            strncpy(wea.windDir, L["winddirection"].as<String>().c_str(), sizeof(wea.windDir) - 1);
            wea.windDir[sizeof(wea.windDir) - 1] = 0;
            strncpy(wea.windpower, L["windpower"].as<String>().c_str(), sizeof(wea.windpower) - 1);
            wea.windpower[sizeof(wea.windpower) - 1] = 0;
            wea.airQulity = w_air_level(wea.windpower);
        }
    }
    http.end();
}

static void w_get_daily(void)
{
    if (WL_CONNECTED != WiFi.status() || w_key.length() == 0)
        return;
    HTTPClient http;
    http.setTimeout(1000);
    char api[160] = {0};
    snprintf(api, sizeof(api), W_DAILY_API, w_key.c_str(), w_city.c_str());
    http.begin(api);
    int code = http.GET();
    if (code == HTTP_CODE_OK || code == HTTP_CODE_MOVED_PERMANENTLY)
    {
        String payload = http.getString();
        DynamicJsonDocument doc(4096);
        deserializeJson(doc, payload);
        if (doc.containsKey("forecasts"))
        {
            JsonObject F = doc["forecasts"][0];
            for (int i = 0; i < FORECAST_DAYS; i++)
            {
                wea.daily_max[i] = F["casts"][i]["daytemp"].as<int>();
                wea.daily_min[i] = F["casts"][i]["nighttemp"].as<int>();
            }
        }
    }
    http.end();
}

static void w_sync_time(void)
{
    if (WL_CONNECTED != WiFi.status())
        return;
    HTTPClient http;
    http.setTimeout(1000);
    http.begin(W_TIME_API);
    int code = http.GET();
    if (code == HTTP_CODE_OK)
    {
        String payload = http.getString();
        int i = payload.indexOf("\"t\":\"") + 5;
        int e = payload.indexOf("\"", i);
        if (i >= 5 && e > i)
        {
            String t = payload.substring(i, e);
            preNetTimestamp = atoll(t.c_str()) + 2 + TIMEZERO_OFFSIZE;
            preLocalTimestamp = GET_SYS_MILLIS();
        }
    }
    http.end();
}

static long long w_now(void)
{
    preNetTimestamp += (GET_SYS_MILLIS() - preLocalTimestamp);
    preLocalTimestamp = GET_SYS_MILLIS();
    return preNetTimestamp;
}

// ---- GUI ----
static void w_init_styles(void)
{
    if (styles_inited)
        return;
    styles_inited = true;

    lv_style_init(&ch_style);
    lv_style_set_text_opa(&ch_style, LV_OPA_COVER);
    lv_style_set_text_color(&ch_style, lv_color_hex(0xffffff));
    lv_style_set_text_font(&ch_style, &ch_font20);

    lv_style_init(&numSmall_style);
    lv_style_set_text_color(&numSmall_style, lv_color_hex(0xffffff));
    lv_style_set_text_font(&numSmall_style, &lv_font_ibmplex_64);

    lv_style_init(&numBig_style);
    lv_style_set_text_color(&numBig_style, lv_color_hex(0xffffff));
    lv_style_set_text_font(&numBig_style, &lv_font_ibmplex_115);

    lv_style_init(&btn_style);
    lv_style_set_border_width(&btn_style, 0);

    lv_style_init(&bar_style);
    lv_style_set_bg_color(&bar_style, lv_color_hex(0x000000));
    lv_style_set_border_width(&bar_style, 2);
    lv_style_set_border_color(&bar_style, lv_color_hex(0xFFFFFF));
    lv_style_set_pad_all(&bar_style, 1);
}

void agent_weather_build(lv_obj_t *parent)
{
    w_init_styles();
    w_read_config();
    preLocalTimestamp = GET_SYS_MILLIS(); // 避免首次 w_now() 用到 0 基准
    memset(&wea, 0, sizeof(wea));
    strcpy(wea.cityname, "--");
    strcpy(wea.weather, "--");
    strcpy(wea.windDir, "--");
    strcpy(wea.windpower, "0");

    weatherImg = lv_img_create(parent);
    lv_img_set_src(weatherImg, weaImage_map[0]);

    cityLabel = lv_label_create(parent);
    lv_obj_add_style(cityLabel, &ch_style, LV_STATE_DEFAULT);
    lv_label_set_recolor(cityLabel, true);
    lv_label_set_text(cityLabel, "--");

    airBtn = lv_btn_create(parent);
    lv_obj_add_style(airBtn, &btn_style, LV_STATE_DEFAULT);
    lv_obj_set_pos(airBtn, 90, 15);
    lv_obj_set_size(airBtn, 50, 25);
    lv_obj_set_style_bg_color(airBtn, lv_palette_main(LV_PALETTE_ORANGE), LV_STATE_DEFAULT);
    airLabel = lv_label_create(airBtn);
    lv_obj_add_style(airLabel, &ch_style, LV_STATE_DEFAULT);
    lv_obj_align(airLabel, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(airLabel, airQualityCh[0]);

    txtLabel = lv_label_create(parent);
    lv_obj_add_style(txtLabel, &ch_style, LV_STATE_DEFAULT);
    lv_label_set_text(txtLabel, "   --   ");
    lv_obj_set_size(txtLabel, 120, 30);
    lv_label_set_long_mode(txtLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);

    clock1 = lv_label_create(parent);
    lv_obj_add_style(clock1, &numBig_style, LV_STATE_DEFAULT);
    lv_label_set_recolor(clock1, true);
    lv_label_set_text(clock1, "00#ffa500 00#");

    clock2 = lv_label_create(parent);
    lv_obj_add_style(clock2, &numSmall_style, LV_STATE_DEFAULT);
    lv_label_set_recolor(clock2, true);
    lv_label_set_text(clock2, "00");

    dateLabel = lv_label_create(parent);
    lv_obj_add_style(dateLabel, &ch_style, LV_STATE_DEFAULT);
    lv_label_set_text(dateLabel, "--");

    tempImg = lv_img_create(parent);
    lv_img_set_src(tempImg, &temp);
    lv_img_set_zoom(tempImg, 180);
    tempBar = lv_bar_create(parent);
    lv_obj_add_style(tempBar, &bar_style, LV_STATE_DEFAULT);
    lv_bar_set_range(tempBar, -50, 50);
    lv_obj_set_size(tempBar, 60, 12);
    lv_obj_set_style_bg_color(tempBar, lv_palette_main(LV_PALETTE_RED), LV_PART_INDICATOR);
    lv_bar_set_value(tempBar, 0, LV_ANIM_OFF);
    tempLabel = lv_label_create(parent);
    lv_obj_add_style(tempLabel, &ch_style, LV_STATE_DEFAULT);
    lv_label_set_text(tempLabel, "--°C");

    humiImg = lv_img_create(parent);
    lv_img_set_src(humiImg, &humi);
    lv_img_set_zoom(humiImg, 180);
    humiBar = lv_bar_create(parent);
    lv_obj_add_style(humiBar, &bar_style, LV_STATE_DEFAULT);
    lv_bar_set_range(humiBar, 0, 100);
    lv_obj_set_size(humiBar, 60, 12);
    lv_obj_set_style_bg_color(humiBar, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
    lv_bar_set_value(humiBar, 0, LV_ANIM_OFF);
    humiLabel = lv_label_create(parent);
    lv_obj_add_style(humiLabel, &ch_style, LV_STATE_DEFAULT);
    lv_label_set_text(humiLabel, "--%");

    spaceImg = lv_img_create(parent);
    lv_img_set_src(spaceImg, manImage_map[0]);

    // 对齐（与天气 app 主页面一致）
    lv_obj_align(weatherImg, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_align(cityLabel, LV_ALIGN_TOP_LEFT, 20, 25);
    lv_obj_align(txtLabel, LV_ALIGN_TOP_LEFT, 10, 50);
    lv_obj_align(tempImg, LV_ALIGN_LEFT_MID, 10, 70);
    lv_obj_align(tempBar, LV_ALIGN_LEFT_MID, 35, 70);
    lv_obj_align(tempLabel, LV_ALIGN_LEFT_MID, 103, 70);
    lv_obj_align(humiImg, LV_ALIGN_LEFT_MID, 0, 100);
    lv_obj_align(humiBar, LV_ALIGN_LEFT_MID, 35, 100);
    lv_obj_align(humiLabel, LV_ALIGN_LEFT_MID, 103, 100);
    lv_obj_align(spaceImg, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_align(clock1, LV_ALIGN_LEFT_MID, 0, 10);
    lv_obj_align(clock2, LV_ALIGN_LEFT_MID, 165, 9);
    lv_obj_align(dateLabel, LV_ALIGN_LEFT_MID, 10, 38);
}

static void w_update_weather(void)
{
    if (NULL == cityLabel)
        return;
    lv_label_set_text(cityLabel, wea.cityname);
    int aq = wea.airQulity;
    if (aq < 0)
        aq = 0;
    if (aq > 5)
        aq = 5;
    lv_label_set_text(airLabel, airQualityCh[aq]);
    int wc = wea.weather_code;
    if (wc < 0 || wc > 8)
        wc = 0;
    lv_img_set_src(weatherImg, weaImage_map[wc]);
    lv_label_set_text_fmt(txtLabel, "   今日天气:%s, %s风%s级.              ",
                          wea.weather, wea.windDir, wea.windpower);
    lv_bar_set_value(tempBar, wea.temperature, LV_ANIM_ON);
    lv_label_set_text_fmt(tempLabel, "%2d°C", wea.temperature);
    lv_bar_set_value(humiBar, wea.humidity, LV_ANIM_ON);
    lv_label_set_text_fmt(humiLabel, "%d%%", wea.humidity);
}

static void w_update_time(void)
{
    if (NULL == clock1)
        return;
    g_rtc.setTime(w_now() / 1000);
    lv_label_set_text_fmt(clock1, "%02d#ffa500 %02d#", g_rtc.getHour(true), g_rtc.getMinute());
    lv_label_set_text_fmt(clock2, "%02d", g_rtc.getSecond());
    lv_label_set_text_fmt(dateLabel, "%2d月%2d日   周%s",
                          g_rtc.getMonth() + 1, g_rtc.getDay(), weekDayCh[g_rtc.getDayofWeek()]);
}

static void w_update_man(void)
{
    if (NULL == spaceImg)
        return;
    lv_img_set_src(spaceImg, manImage_map[manIndex]);
    manIndex = (manIndex + 1) % 10;
}

void agent_weather_enter(void)
{
    if (!fetched_once)
    {
        w_sync_time();
        w_get_weather();
        w_get_daily();
        fetched_once = true;
        preWeatherFetch = GET_SYS_MILLIS();
        preNtpFetch = GET_SYS_MILLIS();
    }
    w_update_weather();
    w_update_time();
}

void agent_weather_tick(void)
{
    unsigned long now = GET_SYS_MILLIS();
    if (now - preClockTick > 500)
    {
        preClockTick = now;
        w_update_time();
    }
    if (now - preManTick > 120)
    {
        preManTick = now;
        w_update_man();
    }
    if (fetched_once && now - preWeatherFetch > W_REFRESH_MS)
    {
        preWeatherFetch = now;
        w_get_weather();
        w_get_daily();
        w_update_weather();
    }
    if (fetched_once && now - preNtpFetch > W_REFRESH_MS)
    {
        preNtpFetch = now;
        w_sync_time();
    }
}

void agent_weather_destroy(void)
{
    weatherImg = NULL;
    cityLabel = NULL;
    airBtn = NULL;
    airLabel = NULL;
    txtLabel = NULL;
    clock1 = NULL;
    clock2 = NULL;
    dateLabel = NULL;
    tempImg = NULL;
    tempBar = NULL;
    tempLabel = NULL;
    humiImg = NULL;
    humiBar = NULL;
    humiLabel = NULL;
    spaceImg = NULL;
    fetched_once = false;
}
