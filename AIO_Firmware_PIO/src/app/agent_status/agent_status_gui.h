#ifndef APP_AGENT_STATUS_GUI_H
#define APP_AGENT_STATUS_GUI_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "lvgl.h"
#include <stdbool.h>

// 每种状态对应的图标动画模式
enum AGENT_ANIM_MODE
{
    AGENT_ANIM_STATIC = 0, // 静止
    AGENT_ANIM_SPIN_SLOW,  // 慢速旋转（thinking）
    AGENT_ANIM_SPIN_FAST,  // 快速旋转（working）
    AGENT_ANIM_PULSE,      // 循环缩放-快（approval）
    AGENT_ANIM_PULSE_SLOW, // 循环缩放-缓慢（idle）
};

    void agent_status_gui_init(void);
    // 创建双页界面（主页：图标+状态文字；第二页：设备信息）
    void agent_status_gui_create(void);
    // 更新主页状态：文字、主题色(0xRRGGBB)、动画模式
    void agent_status_gui_set_state(const char *text, uint32_t color, int anim_mode);
    // 更新信息页的设备信息
    void agent_status_gui_set_info(const char *ip, const char *host);
    // 切换到指定页（0=状态 1=天气 2=信息）
    void agent_status_gui_goto_page(int page);
    // 取天气页所在的 tile（供天气模块构建内容）
    lv_obj_t *agent_status_gui_weather_tile(void);
    void agent_status_gui_del(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#include "lvgl.h"
    extern const lv_img_dsc_t app_agent_status;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
