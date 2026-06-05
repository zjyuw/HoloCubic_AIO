#ifndef APP_AGENT_STATUS_GUI_H
#define APP_AGENT_STATUS_GUI_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "lvgl.h"
#define ANIEND                      \
    while (lv_anim_count_running()) \
        lv_task_handler(); // 等待动画完成

    void agent_status_gui_init(void);
    void display_agent_status(const char *text, lv_scr_load_anim_t anim_type);
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
