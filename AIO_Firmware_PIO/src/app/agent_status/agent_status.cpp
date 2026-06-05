#include "agent_status.h"
#include "agent_status_gui.h"
#include "sys/app_controller.h"
#include "common.h"

#define AGENT_STATUS_APP_NAME "Agent Status"

// 动态数据，APP 的生命周期结束时需要释放它
struct AgentStatusAppRunData
{
    bool need_render; // 是否需要刷新界面
};

static AgentStatusAppRunData *run_data = NULL;

static int agent_status_init(AppController *sys)
{
    // 初始化 GUI 样式
    agent_status_gui_init();

    // 初始化运行时参数
    run_data = (AgentStatusAppRunData *)calloc(1, sizeof(AgentStatusAppRunData));
    run_data->need_render = true;

    return 0;
}

static void agent_status_process(AppController *sys,
                                 const ImuAction *act_info)
{
    if (RETURN == act_info->active)
    {
        sys->app_exit(); // 退出 APP
        return;
    }

    // 首次进入时渲染 hello world 界面
    if (run_data->need_render)
    {
        display_agent_status("hello world", LV_SCR_LOAD_ANIM_NONE);
        run_data->need_render = false;
    }

    delay(30);
}

static void agent_status_background_task(AppController *sys,
                                         const ImuAction *act_info)
{
    // 后台任务，暂时不需要处理
}

static int agent_status_exit_callback(void *param)
{
    // 释放 GUI 资源
    agent_status_gui_del();

    // 释放运行时数据
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
    // 暂无消息处理需求
}

APP_OBJ agent_status_app = {AGENT_STATUS_APP_NAME, &app_agent_status,
                            "Author Claude\nVersion 0.0.1\n",
                            agent_status_init, agent_status_process,
                            agent_status_background_task,
                            agent_status_exit_callback, agent_status_message_handle};
