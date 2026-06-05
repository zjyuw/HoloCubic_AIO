#include "agent_status_gui.h"
#include "lvgl.h"

static lv_obj_t *agent_status_gui = NULL;
static lv_obj_t *titleLabel = NULL; // 顶部 APP 标题
static lv_obj_t *textLabel = NULL;  // 主体文字（hello world）

static lv_style_t default_style;
static lv_style_t title_style;
static lv_style_t text_style;

// Claude 品牌色
#define CLAUDE_ORANGE 0xD97757
#define CLAUDE_CREAM 0xF5F1E9

void agent_status_gui_init(void)
{
    // 背景样式：Claude 奶白色
    lv_style_init(&default_style);
    lv_style_set_bg_color(&default_style, lv_color_hex(CLAUDE_CREAM));
    lv_style_set_bg_opa(&default_style, LV_OPA_COVER);

    // 标题样式
    lv_style_init(&title_style);
    lv_style_set_text_opa(&title_style, LV_OPA_COVER);
    lv_style_set_text_color(&title_style, lv_color_hex(CLAUDE_ORANGE));
    lv_style_set_text_font(&title_style, &lv_font_montserrat_14);

    // 主体文字样式
    lv_style_init(&text_style);
    lv_style_set_text_opa(&text_style, LV_OPA_COVER);
    lv_style_set_text_color(&text_style, lv_color_hex(0x303030));
    lv_style_set_text_font(&text_style, &lv_font_montserrat_30);
}

void display_agent_status(const char *text, lv_scr_load_anim_t anim_type)
{
    lv_obj_t *act_obj = lv_scr_act(); // 获取当前活动页
    if (act_obj == agent_status_gui)
    {
        // 页面已存在，仅更新文字
        if (NULL != textLabel)
        {
            lv_label_set_text(textLabel, text);
        }
        return;
    }

    agent_status_gui_del(); // 清空旧对象

    agent_status_gui = lv_obj_create(NULL);
    lv_obj_add_style(agent_status_gui, &default_style, LV_STATE_DEFAULT);

    // 顶部标题
    titleLabel = lv_label_create(agent_status_gui);
    lv_obj_add_style(titleLabel, &title_style, LV_STATE_DEFAULT);
    lv_label_set_text(titleLabel, "Agent Status");
    lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 26);

    // 主体文字
    textLabel = lv_label_create(agent_status_gui);
    lv_obj_add_style(textLabel, &text_style, LV_STATE_DEFAULT);
    lv_label_set_text(textLabel, text);
    lv_obj_align(textLabel, LV_ALIGN_CENTER, 0, 0);

    lv_scr_load_anim(agent_status_gui, anim_type, 300, 0, false);
}

void agent_status_gui_del(void)
{
    if (NULL != agent_status_gui)
    {
        lv_obj_clean(agent_status_gui);
        agent_status_gui = NULL;
        titleLabel = NULL;
        textLabel = NULL;
    }
}
