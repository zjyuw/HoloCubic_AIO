#include "agent_status_gui.h"
#include "lvgl.h"

static lv_obj_t *agent_status_gui = NULL; // 屏幕
static lv_obj_t *tileview = NULL;
static lv_obj_t *tile_status = NULL; // 第1页：状态
static lv_obj_t *tile_info = NULL;   // 第2页：设备信息

static lv_obj_t *iconImg = NULL;       // 圆形 Claude 图标（自身动画）
static lv_obj_t *stateLabel = NULL;    // 状态大字
static lv_obj_t *ipValueLabel = NULL;  // 第2页：IP
static lv_obj_t *hostLabel = NULL;     // 第2页：mDNS 域名

static lv_style_t bg_style;
static lv_style_t state_style;
static lv_style_t info_cap_style; // 小标题
static lv_style_t ip_style;       // IP 大字
static lv_style_t host_style;

#define ICON_ZOOM_BASE 170 // 256=100%

// ---- 图标动画：按状态切换不同模式 ----
static void icon_apply_anim(int mode)
{
    if (NULL == iconImg)
        return;
    lv_anim_del(iconImg, NULL);
    lv_img_set_angle(iconImg, 0);
    lv_img_set_zoom(iconImg, ICON_ZOOM_BASE);

    if (AGENT_ANIM_SPIN_SLOW == mode || AGENT_ANIM_SPIN_FAST == mode)
    {
        int turn = (AGENT_ANIM_SPIN_FAST == mode) ? 1600 : 3600;
        lv_anim_t rot;
        lv_anim_init(&rot);
        lv_anim_set_var(&rot, iconImg);
        lv_anim_set_exec_cb(&rot, (lv_anim_exec_xcb_t)lv_img_set_angle);
        lv_anim_set_values(&rot, 0, 3600);
        lv_anim_set_time(&rot, turn);
        lv_anim_set_repeat_count(&rot, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&rot, lv_anim_path_linear);
        lv_anim_start(&rot);

        lv_anim_t br;
        lv_anim_init(&br);
        lv_anim_set_var(&br, iconImg);
        lv_anim_set_exec_cb(&br, (lv_anim_exec_xcb_t)lv_img_set_zoom);
        lv_anim_set_values(&br, ICON_ZOOM_BASE - 12, ICON_ZOOM_BASE + 10);
        lv_anim_set_time(&br, 1000);
        lv_anim_set_playback_time(&br, 1000);
        lv_anim_set_repeat_count(&br, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&br, lv_anim_path_ease_in_out);
        lv_anim_start(&br);
    }
    else if (AGENT_ANIM_PULSE == mode)
    {
        // 明显的循环缩放（approval：吸引注意）
        lv_anim_t pulse;
        lv_anim_init(&pulse);
        lv_anim_set_var(&pulse, iconImg);
        lv_anim_set_exec_cb(&pulse, (lv_anim_exec_xcb_t)lv_img_set_zoom);
        lv_anim_set_values(&pulse, ICON_ZOOM_BASE - 42, ICON_ZOOM_BASE + 36);
        lv_anim_set_time(&pulse, 600);
        lv_anim_set_playback_time(&pulse, 600);
        lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&pulse, lv_anim_path_ease_in_out);
        lv_anim_start(&pulse);
    }
    // AGENT_ANIM_STATIC: 不加动画
}

static void style_transparent(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

void agent_status_gui_init(void)
{
    lv_style_init(&bg_style);
    lv_style_set_bg_color(&bg_style, lv_color_hex(0x000000)); // 纯黑（半透明屏更通透）
    lv_style_set_bg_opa(&bg_style, LV_OPA_COVER);

    lv_style_init(&state_style);
    lv_style_set_text_opa(&state_style, LV_OPA_COVER);
    lv_style_set_text_color(&state_style, lv_color_hex(0xFFFFFF));
    lv_style_set_text_font(&state_style, &lv_font_montserrat_40);

    lv_style_init(&info_cap_style);
    lv_style_set_text_opa(&info_cap_style, LV_OPA_COVER);
    lv_style_set_text_color(&info_cap_style, lv_color_hex(0xFFC24B)); // 亮黄
    lv_style_set_text_font(&info_cap_style, &lv_font_montserrat_20);

    lv_style_init(&ip_style);
    lv_style_set_text_opa(&ip_style, LV_OPA_COVER);
    lv_style_set_text_color(&ip_style, lv_color_hex(0xFFFFFF));
    lv_style_set_text_font(&ip_style, &lv_font_montserrat_30);

    lv_style_init(&host_style);
    lv_style_set_text_opa(&host_style, LV_OPA_COVER);
    lv_style_set_text_color(&host_style, lv_color_hex(0xF5F1E9));
    lv_style_set_text_font(&host_style, &lv_font_montserrat_24);
}

void agent_status_gui_create(void)
{
    agent_status_gui_del();

    agent_status_gui = lv_obj_create(NULL);
    lv_obj_add_style(agent_status_gui, &bg_style, LV_STATE_DEFAULT);
    lv_obj_clear_flag(agent_status_gui, LV_OBJ_FLAG_SCROLLABLE);

    tileview = lv_tileview_create(agent_status_gui);
    lv_obj_set_size(tileview, 240, 240);
    lv_obj_center(tileview);
    style_transparent(tileview);

    tile_status = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_HOR);
    tile_info = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);
    style_transparent(tile_status);
    style_transparent(tile_info);

    // ---------- 第1页：状态 ----------
    iconImg = lv_img_create(tile_status);
    lv_img_set_src(iconImg, &app_agent_status);
    lv_img_set_antialias(iconImg, true);
    lv_img_set_pivot(iconImg, 64, 64);
    lv_img_set_zoom(iconImg, ICON_ZOOM_BASE);
    lv_obj_align(iconImg, LV_ALIGN_CENTER, 0, -34);

    stateLabel = lv_label_create(tile_status);
    lv_obj_add_style(stateLabel, &state_style, LV_STATE_DEFAULT);
    lv_label_set_text(stateLabel, "...");
    lv_obj_align(stateLabel, LV_ALIGN_CENTER, 0, 62);

    // ---------- 第2页：设备信息 ----------
    lv_obj_t *ipCap = lv_label_create(tile_info);
    lv_obj_add_style(ipCap, &info_cap_style, LV_STATE_DEFAULT);
    lv_label_set_text(ipCap, "IP ADDRESS");
    lv_obj_align(ipCap, LV_ALIGN_CENTER, 0, -58);

    ipValueLabel = lv_label_create(tile_info);
    lv_obj_add_style(ipValueLabel, &ip_style, LV_STATE_DEFAULT);
    lv_label_set_text(ipValueLabel, "...");
    lv_obj_align(ipValueLabel, LV_ALIGN_CENTER, 0, -26);

    lv_obj_t *hostCap = lv_label_create(tile_info);
    lv_obj_add_style(hostCap, &info_cap_style, LV_STATE_DEFAULT);
    lv_label_set_text(hostCap, "mDNS");
    lv_obj_align(hostCap, LV_ALIGN_CENTER, 0, 24);

    hostLabel = lv_label_create(tile_info);
    lv_obj_add_style(hostLabel, &host_style, LV_STATE_DEFAULT);
    lv_label_set_text(hostLabel, "agentstatus.local");
    lv_obj_align(hostLabel, LV_ALIGN_CENTER, 0, 56);

    lv_scr_load(agent_status_gui);
}

void agent_status_gui_set_state(const char *text, uint32_t color, int anim_mode)
{
    if (NULL == stateLabel)
        return;
    lv_label_set_text(stateLabel, text);
    lv_obj_set_style_text_color(stateLabel, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_align(stateLabel, LV_ALIGN_CENTER, 0, 62);
    icon_apply_anim(anim_mode);
}

void agent_status_gui_set_info(const char *ip, const char *host)
{
    if (NULL != ipValueLabel)
    {
        lv_label_set_text(ipValueLabel, ip);
        lv_obj_align(ipValueLabel, LV_ALIGN_CENTER, 0, -26);
    }
    if (NULL != hostLabel && NULL != host)
    {
        lv_label_set_text(hostLabel, host);
        lv_obj_align(hostLabel, LV_ALIGN_CENTER, 0, 56);
    }
}

void agent_status_gui_goto_page(int page)
{
    if (NULL == tileview)
        return;
    lv_obj_set_tile_id(tileview, page, 0, LV_ANIM_ON);
}

void agent_status_gui_del(void)
{
    if (NULL != agent_status_gui)
    {
        if (NULL != iconImg)
            lv_anim_del(iconImg, NULL);
        lv_obj_clean(agent_status_gui);
        agent_status_gui = NULL;
        tileview = NULL;
        tile_status = NULL;
        tile_info = NULL;
        iconImg = NULL;
        stateLabel = NULL;
        ipValueLabel = NULL;
        hostLabel = NULL;
    }
}
