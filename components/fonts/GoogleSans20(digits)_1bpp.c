// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

/*******************************************************************************
 * Size: 20 px
 * Bpp: 1
 * Opts: --bpp 1 --size 20 --no-compress --stride 1 --align 1 --font GoogleSans-Bold.ttf --symbols 0123456789:.,+-= --format lvgl -o GoogleSans20(digits)_1bpp.c
 ******************************************************************************/

#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif



#ifndef GOOGLESANS20(DIGITS)_1BPP
#define GOOGLESANS20(DIGITS)_1BPP 1
#endif

#if GOOGLESANS20(DIGITS)_1BPP

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+002B "+" */
    0xe, 0x3, 0x80, 0xe0, 0x38, 0xff, 0xff, 0xf0,
    0xe0, 0x38, 0xe, 0x3, 0x80,

    /* U+002C "," */
    0xff, 0xad, 0x0,

    /* U+002D "-" */
    0xff, 0xfc,

    /* U+002E "." */
    0xfc,

    /* U+0030 "0" */
    0xf, 0x3, 0xfc, 0x79, 0xe7, 0xe, 0xe0, 0x7e,
    0x7, 0xe0, 0x7e, 0x7, 0xe0, 0x7e, 0x7, 0xe0,
    0x77, 0xe, 0x79, 0xe3, 0xfc, 0xf, 0x0,

    /* U+0031 "1" */
    0x1c, 0xff, 0xff, 0x5c, 0x71, 0xc7, 0x1c, 0x71,
    0xc7, 0x1c, 0x71, 0xc0,

    /* U+0032 "2" */
    0x3e, 0x3f, 0xb9, 0xe8, 0x70, 0x38, 0x1c, 0x1e,
    0x1e, 0xe, 0xf, 0xf, 0xf, 0xf, 0x7, 0xff,
    0xfe,

    /* U+0033 "3" */
    0x3e, 0x3f, 0xb9, 0xec, 0x70, 0x38, 0x38, 0x78,
    0x3e, 0x7, 0x1, 0xc0, 0xf8, 0x7e, 0x7b, 0xf8,
    0xf8,

    /* U+0034 "4" */
    0x3, 0xc0, 0x3c, 0x7, 0xc0, 0x7c, 0xf, 0xc1,
    0xdc, 0x1d, 0xc3, 0x9c, 0x31, 0xc7, 0x1c, 0xff,
    0xff, 0xff, 0x1, 0xc0, 0x1c, 0x1, 0xc0,

    /* U+0035 "5" */
    0x7f, 0x9f, 0xe6, 0x1, 0x80, 0x60, 0x3f, 0x8f,
    0xf8, 0x8e, 0x1, 0xc0, 0x74, 0x1f, 0x87, 0xe3,
    0x9f, 0xe1, 0xe0,

    /* U+0036 "6" */
    0x4, 0x3, 0x80, 0xe0, 0x70, 0x18, 0xe, 0x7,
    0xf1, 0xfe, 0xf3, 0xb8, 0x7e, 0x1f, 0x87, 0xe1,
    0xdc, 0xe7, 0xf8, 0x78,

    /* U+0037 "7" */
    0xff, 0xff, 0xc0, 0xe0, 0xe0, 0x70, 0x70, 0x38,
    0x38, 0x1c, 0x1c, 0xe, 0x6, 0x7, 0x3, 0x0,
    0x80,

    /* U+0038 "8" */
    0x1e, 0x1f, 0xef, 0x3f, 0x87, 0xe1, 0xdc, 0xe3,
    0xf1, 0xfe, 0x73, 0xb8, 0x7e, 0x1f, 0x87, 0xf3,
    0xdf, 0xe1, 0xe0,

    /* U+0039 "9" */
    0x1e, 0x1f, 0xe7, 0x3b, 0x87, 0xe1, 0xf8, 0x7f,
    0x3d, 0xfe, 0x3f, 0x81, 0xc0, 0xf0, 0x38, 0x1c,
    0x7, 0x0, 0x80,

    /* U+003A ":" */
    0xfc, 0x0, 0x0, 0x1f, 0x80,

    /* U+003D "=" */
    0xff, 0xff, 0xc0, 0x0, 0xf, 0xff, 0xfc
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 186, .box_w = 10, .box_h = 10, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 13, .adv_w = 91, .box_w = 3, .box_h = 6, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 16, .adv_w = 148, .box_w = 7, .box_h = 2, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 18, .adv_w = 91, .box_w = 3, .box_h = 2, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 19, .adv_w = 215, .box_w = 12, .box_h = 15, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 42, .adv_w = 147, .box_w = 6, .box_h = 15, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 54, .adv_w = 177, .box_w = 9, .box_h = 15, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 71, .adv_w = 185, .box_w = 9, .box_h = 15, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 88, .adv_w = 200, .box_w = 12, .box_h = 15, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 111, .adv_w = 184, .box_w = 10, .box_h = 15, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 130, .adv_w = 185, .box_w = 10, .box_h = 16, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 150, .adv_w = 178, .box_w = 9, .box_h = 15, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 167, .adv_w = 193, .box_w = 10, .box_h = 15, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 186, .adv_w = 185, .box_w = 10, .box_h = 15, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 205, .adv_w = 91, .box_w = 3, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 210, .adv_w = 190, .box_w = 9, .box_h = 6, .ofs_x = 1, .ofs_y = 2}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint8_t glyph_id_ofs_list_0[] = {
    0, 1, 2, 3, 0, 4, 5, 6,
    7, 8, 9, 10, 11, 12, 13, 14,
    0, 0, 15
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 43, .range_length = 19, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = glyph_id_ofs_list_0, .list_length = 19, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Map glyph_ids to kern left classes*/
static const uint8_t kern_left_class_mapping[] =
{
    0, 1, 2, 1, 2, 3, 0, 4,
    5, 6, 7, 8, 9, 5, 10, 0,
    0
};

/*Map glyph_ids to kern right classes*/
static const uint8_t kern_right_class_mapping[] =
{
    0, 0, 1, 0, 1, 2, 3, 4,
    5, 6, 0, 7, 8, 2, 9, 0,
    0
};

/*Kern values between classes*/
static const int8_t kern_class_values[] =
{
    0, 0, -10, 0, 0, 0, 0, 0,
    0, 0, 0, -13, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -6, 0, 0,
    0, -6, 0, 0, 0, 0, 0, 0,
    -6, 0, 0, 0, 0, 0, -6, 0,
    0, 0, 0, -6, -3, 0, 0, -19,
    0, 0, 0, 0, -6, -13, 0, 0,
    -13, -10, -3, -3, 0, -10, -10, 0,
    0, -22, -6, -3, 0, 0, -13, -16,
    -35, -10, 0, -6, -6, -22, -19, 0,
    0, -16, 0, 0, -6, 0, -10, -6,
    -3, 0
};


/*Collect the kern class' data in one place*/
static const lv_font_fmt_txt_kern_classes_t kern_classes =
{
    .class_pair_values   = kern_class_values,
    .left_class_mapping  = kern_left_class_mapping,
    .right_class_mapping = kern_right_class_mapping,
    .left_class_cnt      = 10,
    .right_class_cnt     = 9,
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = &kern_classes,
    .kern_scale = 16,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 1,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif

};

extern const lv_font_t lv_font_montserrat_20;


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t GoogleSans20(digits)_1bpp = {
#else
lv_font_t GoogleSans20(digits)_1bpp = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 18,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -3,
    .underline_thickness = 2,
#endif
    .static_bitmap = 0,
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = &lv_font_montserrat_20,
#endif
    .user_data = NULL,
};



#endif /*#if GOOGLESANS20(DIGITS)_1BPP*/
