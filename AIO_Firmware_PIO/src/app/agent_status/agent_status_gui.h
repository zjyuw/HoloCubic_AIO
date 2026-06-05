#ifndef APP_AGENT_STATUS_GUI_H
#define APP_AGENT_STATUS_GUI_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "lvgl.h"
#include <stdbool.h>
#define ANIEND                      \
    while (lv_anim_count_running()) \
        lv_task_handler(); // 等待动画完成

    void agent_status_gui_init(void);
    // 创建状态界面（图标 + 旋转动画 + 状态文字 + 底部IP）
    void agent_status_gui_create(void);
    // 更新状态：文字、主题色(0xRRGGBB)、是否显示忙碌动画
    void agent_status_gui_set_state(const char *text, uint32_t color, bool busy);
    // 更新底部显示的设备地址
    void agent_status_gui_set_ip(const char *ip);
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
