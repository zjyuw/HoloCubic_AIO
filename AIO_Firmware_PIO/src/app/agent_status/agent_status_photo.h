#ifndef APP_AGENT_STATUS_PHOTO_H
#define APP_AGENT_STATUS_PHOTO_H

// SD 卡 /AgentStatus 目录里的自定义图片轮播。
// 仅支持 baseline JPEG(.jpg/.jpeg);用 TJpgDec 解码进一块复用的定长 RGB565
// 缓冲(自动 1/2/4/8 降采样到 <= AGENT_PHOTO_MAX 像素),再包成 lv_img_dsc 交给 LVGL。
// 该模块为 C++(要用 TJpgDec),仅由 agent_status.cpp(C++)调用。

#include "lvgl.h"

// 扫描 /AgentStatus,返回找到的图片数量(同时建立内部文件列表)。
int agent_photo_scan(void);

// 是否扫描到可用图片。
bool agent_photo_available(void);

// 解码下一张图片到内存,成功返回可用于 lv_img 的 dsc,失败返回 NULL。
// 返回的 dsc 指针在下一次调用前保持有效(复用同一缓冲)。
const lv_img_dsc_t *agent_photo_next(void);

// 释放文件列表与解码缓冲。
void agent_photo_free(void);

#endif
