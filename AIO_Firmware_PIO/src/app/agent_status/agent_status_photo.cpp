#include "agent_status_photo.h"
#include "common.h" // SdCard tf
#include "FS.h"
#include <Arduino.h> // ESP.getFreeHeap / Serial
#include <TJpg_Decoder.h>
#include <vector>
#include <string.h>
#include <stdlib.h>

#define AGENT_PHOTO_DIR "/AgentStatus"
// 解码后单边最大像素;缓冲 = MAX*MAX*2。zoom 缩放铺满与解码尺寸无关,所以小尺寸也能满屏(略糊)。
// 无 PSRAM,SRAM 堆(~320KB)要和 WiFi/LVGL 共用,缓冲太大会把堆吃爆 -> 连不上 WiFi / LVGL 崩溃。
// 128 -> 32KB(安全);若想更清晰可调大,但要看串口打印的空闲堆余量,别再吃爆。
#define AGENT_PHOTO_MAX 128
// 分配缓冲后至少要给 WiFi/LVGL 留这么多堆,否则放弃图片(只显示提示),宁可不显示也不饿死系统。
#define AGENT_PHOTO_MIN_FREE (80 * 1024)

static std::vector<String> s_files; // /AgentStatus 下的图片全路径
static int s_idx = -1;
static uint16_t *s_buf = NULL; // 复用的 RGB565 解码缓冲
static int s_outW = 0, s_outH = 0;
static lv_img_dsc_t s_dsc;

// TJpgDec 回调:把一个解码块拷进 s_buf(裁剪到 s_outW x s_outH 内)
static bool photo_jpg_cb(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap)
{
    if (NULL == s_buf)
        return false;
    for (uint16_t row = 0; row < h; row++)
    {
        int dy = y + row;
        if (dy < 0 || dy >= s_outH)
            continue;
        uint16_t *dst = s_buf + (uint32_t)dy * s_outW;
        uint16_t *src = bitmap + (uint32_t)row * w;
        for (uint16_t col = 0; col < w; col++)
        {
            int dx = x + col;
            if (dx < 0 || dx >= s_outW)
                continue;
            dst[dx] = src[col];
        }
    }
    return true;
}

static bool is_jpg(const String &name)
{
    String n = name;
    n.toLowerCase();
    return n.endsWith(".jpg") || n.endsWith(".jpeg");
}

int agent_photo_scan(void)
{
    s_files.clear();
    s_idx = -1;
    File dir = tf.open(AGENT_PHOTO_DIR);
    if (!dir || !dir.isDirectory())
    {
        if (dir)
            dir.close();
        return 0;
    }
    File f = dir.openNextFile();
    while (f)
    {
        String full = f.name(); // 该 core 下 name() 返回全路径
        bool isdir = f.isDirectory();
        f.close();
        if (!isdir && full.length() && is_jpg(full))
            s_files.push_back(full);
        f = dir.openNextFile();
    }
    dir.close();
    return (int)s_files.size();
}

bool agent_photo_available(void)
{
    return !s_files.empty();
}

static bool ensure_buf(void)
{
    if (NULL != s_buf)
        return true;
    size_t need = (size_t)AGENT_PHOTO_MAX * AGENT_PHOTO_MAX * 2;
    uint32_t freeHeap = ESP.getFreeHeap();
    Serial.printf("[AgentPhoto] free heap=%u, need=%u, reserve=%u\n",
                  (unsigned)freeHeap, (unsigned)need, (unsigned)AGENT_PHOTO_MIN_FREE);
    // 堆不够就不分配：宁可不显示图片，也不能把 WiFi/LVGL 饿死导致连不上网/崩溃
    if (freeHeap < need + AGENT_PHOTO_MIN_FREE)
    {
        Serial.println("[AgentPhoto] low heap -> skip photo buffer (keep WiFi/LVGL alive)");
        return false;
    }
    s_buf = (uint16_t *)malloc(need);
    if (NULL == s_buf)
        Serial.println("[AgentPhoto] malloc failed");
    return (NULL != s_buf);
}

const lv_img_dsc_t *agent_photo_next(void)
{
    if (s_files.empty())
        return NULL;
    if (!ensure_buf())
        return NULL; // 内存不足 -> 上层回退到默认图标

    s_idx = (s_idx + 1) % (int)s_files.size();
    const char *path = s_files[s_idx].c_str();

    uint16_t w = 0, h = 0;
    if (JDR_OK != TJpgDec.getSdJpgSize(&w, &h, path) || 0 == w || 0 == h)
        return NULL; // 非 baseline JPEG / 读失败

    // 选 1/2/4/8 降采样,使解码后 <= AGENT_PHOTO_MAX
    uint8_t scale = 1;
    while (scale < 8 && (w / scale > AGENT_PHOTO_MAX || h / scale > AGENT_PHOTO_MAX))
        scale <<= 1;
    s_outW = w / scale;
    s_outH = h / scale;
    if (s_outW < 1)
        s_outW = 1;
    if (s_outH < 1)
        s_outH = 1;
    if (s_outW > AGENT_PHOTO_MAX)
        s_outW = AGENT_PHOTO_MAX;
    if (s_outH > AGENT_PHOTO_MAX)
        s_outH = AGENT_PHOTO_MAX;

    memset(s_buf, 0, (size_t)AGENT_PHOTO_MAX * AGENT_PHOTO_MAX * 2); // 黑底

    TJpgDec.setSwapBytes(false); // LV_COLOR_16_SWAP=0 -> 不交换字节
    TJpgDec.setJpgScale(scale);
    TJpgDec.setCallback(photo_jpg_cb);
    JRESULT r = TJpgDec.drawSdJpg(0, 0, path);
    TJpgDec.setSwapBytes(true); // 还原为 TFT 直绘路径(picture/media)期望的默认
    if (JDR_OK != r)
        return NULL;

    memset(&s_dsc, 0, sizeof(s_dsc));
    s_dsc.header.always_zero = 0;
    s_dsc.header.w = s_outW;
    s_dsc.header.h = s_outH;
    s_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_dsc.data_size = (uint32_t)s_outW * s_outH * 2;
    s_dsc.data = (const uint8_t *)s_buf;
    return &s_dsc;
}

void agent_photo_free(void)
{
    s_files.clear();
    s_idx = -1;
    if (NULL != s_buf)
    {
        free(s_buf);
        s_buf = NULL;
    }
}
