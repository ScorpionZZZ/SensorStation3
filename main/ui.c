// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "ui.h"

void ui_init(void)
{
    ui_main_create();
    ui_settings_create();
    lv_scr_load(ui_main_screen());
}

void ui_show_main(void)
{
    lv_scr_load_anim(ui_main_screen(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}

void ui_show_settings(void)
{
    lv_scr_load_anim(ui_settings_screen(), LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}