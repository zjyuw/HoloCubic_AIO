#ifndef AGENT_STATUS_WEATHER_H
#define AGENT_STATUS_WEATHER_H

// 天气页：复刻天气 app 主页面，渲染进 Agent Status 的一个 tile。
// 依赖天气 app 已编译进固件（APP_WEATHER_USE=1）——复用其字体/图标/天气映射表。

#ifdef __cplusplus
extern "C"
{
#endif

#include "lvgl.h"

    // 把天气主页面构建进指定父对象（tileview 的某个 tile）
    void agent_weather_build(lv_obj_t *parent);
    // 切到天气页时调用：必要时同步天气/时间并刷新
    void agent_weather_enter(void);
    // 停留在天气页时每轮调用：走时钟、小人动画、周期刷新
    void agent_weather_tick(void);
    // 释放（对象随屏幕清理而销毁，这里只复位指针/状态）
    void agent_weather_destroy(void);

#ifdef __cplusplus
}
#endif

#endif
