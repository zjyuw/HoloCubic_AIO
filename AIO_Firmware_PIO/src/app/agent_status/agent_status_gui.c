#include "agent_status_gui.h"
#include "lvgl.h"

static lv_obj_t *agent_status_gui = NULL;
static lv_obj_t *titleLabel = NULL; // 顶部标题
static lv_obj_t *iconImg = NULL;    // 中央 Claude 图标（自身做动画）
static lv_obj_t *stateLabel = NULL; // 状态文字
static lv_obj_t *ipLabel = NULL;    // 底部设备地址

static lv_style_t default_style;
static lv_style_t title_style;
static lv_style_t state_style;
static lv_style_t ip_style;

static bool icon_animating = false;

// Claude 品牌色
#define CLAUDE_ORANGE 0xD97757
#define CLAUDE_CREAM 0xF5F1E9

#define ICON_ZOOM_BASE 170 // 256=100%

static void icon_anim_stop(void)
{
    if (NULL == iconImg)
        return;
    lv_anim_del(iconImg, NULL); // 删除该对象上的所有动画
    lv_img_set_angle(iconImg, 0);
    lv_img_set_zoom(iconImg, ICON_ZOOM_BASE);
    icon_animating = false;
}

static void icon_anim_start(void)
{
    if (NULL == iconImg || icon_animating)
        return;
    icon_animating = true;

    // 持续旋转
    lv_anim_t rot;
    lv_anim_init(&rot);
    lv_anim_set_var(&rot, iconImg);
    lv_anim_set_exec_cb(&rot, (lv_anim_exec_xcb_t)lv_img_set_angle);
    lv_anim_set_values(&rot, 0, 3600); // 0~360°
    lv_anim_set_time(&rot, 3200);
    lv_anim_set_repeat_count(&rot, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&rot, lv_anim_path_linear);
    lv_anim_start(&rot);

    // 呼吸缩放
    lv_anim_t breathe;
    lv_anim_init(&breathe);
    lv_anim_set_var(&breathe, iconImg);
    lv_anim_set_exec_cb(&breathe, (lv_anim_exec_xcb_t)lv_img_set_zoom);
    lv_anim_set_values(&breathe, ICON_ZOOM_BASE - 22, ICON_ZOOM_BASE + 18);
    lv_anim_set_time(&breathe, 1100);
    lv_anim_set_playback_time(&breathe, 1100);
    lv_anim_set_repeat_count(&breathe, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&breathe, lv_anim_path_ease_in_out);
    lv_anim_start(&breathe);
}

void agent_status_gui_init(void)
{
    lv_style_init(&default_style);
    lv_style_set_bg_color(&default_style, lv_color_hex(0x16130F)); // 深色背景
    lv_style_set_bg_opa(&default_style, LV_OPA_COVER);

    lv_style_init(&title_style);
    lv_style_set_text_opa(&title_style, LV_OPA_COVER);
    lv_style_set_text_color(&title_style, lv_color_hex(CLAUDE_ORANGE));
    lv_style_set_text_font(&title_style, &lv_font_montserrat_20);

    lv_style_init(&state_style);
    lv_style_set_text_opa(&state_style, LV_OPA_COVER);
    lv_style_set_text_color(&state_style, lv_color_hex(CLAUDE_CREAM));
    lv_style_set_text_font(&state_style, &lv_font_montserrat_30);

    lv_style_init(&ip_style);
    lv_style_set_text_opa(&ip_style, LV_OPA_COVER);
    lv_style_set_text_color(&ip_style, lv_color_hex(0xE8E4DA)); // 高对比浅色
    lv_style_set_text_font(&ip_style, &lv_font_montserrat_14);
}

void agent_status_gui_create(void)
{
    agent_status_gui_del();

    agent_status_gui = lv_obj_create(NULL);
    lv_obj_add_style(agent_status_gui, &default_style, LV_STATE_DEFAULT);
    lv_obj_clear_flag(agent_status_gui, LV_OBJ_FLAG_SCROLLABLE);

    // 顶部标题
    titleLabel = lv_label_create(agent_status_gui);
    lv_obj_add_style(titleLabel, &title_style, LV_STATE_DEFAULT);
    lv_label_set_text(titleLabel, "Agent Status");
    lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 12);

    // 中央 Claude 图标
    iconImg = lv_img_create(agent_status_gui);
    lv_img_set_src(iconImg, &app_agent_status);
    lv_img_set_antialias(iconImg, true);
    lv_img_set_pivot(iconImg, 64, 64); // 128px 源图中心
    lv_img_set_zoom(iconImg, ICON_ZOOM_BASE);
    lv_obj_align(iconImg, LV_ALIGN_CENTER, 0, -28);

    // 状态文字
    stateLabel = lv_label_create(agent_status_gui);
    lv_obj_add_style(stateLabel, &state_style, LV_STATE_DEFAULT);
    lv_label_set_text(stateLabel, "...");
    lv_obj_align(stateLabel, LV_ALIGN_CENTER, 0, 56);

    // 底部设备地址
    ipLabel = lv_label_create(agent_status_gui);
    lv_obj_add_style(ipLabel, &ip_style, LV_STATE_DEFAULT);
    lv_label_set_text(ipLabel, "");
    lv_obj_align(ipLabel, LV_ALIGN_BOTTOM_MID, 0, -12);

    icon_animating = false;
    lv_scr_load(agent_status_gui);
}

void agent_status_gui_set_state(const char *text, uint32_t color, bool busy)
{
    if (NULL == stateLabel)
        return;
    lv_label_set_text(stateLabel, text);
    lv_obj_set_style_text_color(stateLabel, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_align(stateLabel, LV_ALIGN_CENTER, 0, 56);

    if (busy)
        icon_anim_start();
    else
        icon_anim_stop();
}

void agent_status_gui_set_ip(const char *ip)
{
    if (NULL == ipLabel)
        return;
    lv_label_set_text(ipLabel, ip);
    lv_obj_align(ipLabel, LV_ALIGN_BOTTOM_MID, 0, -12);
}

void agent_status_gui_del(void)
{
    if (NULL != agent_status_gui)
    {
        icon_anim_stop();
        lv_obj_clean(agent_status_gui);
        agent_status_gui = NULL;
        titleLabel = NULL;
        iconImg = NULL;
        stateLabel = NULL;
        ipLabel = NULL;
    }
}
