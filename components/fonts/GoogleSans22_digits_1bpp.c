// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

/*******************************************************************************
 * Size: 22 px
 * Bpp: 1
 * Opts: --bpp 1 --size 22 --no-compress --stride 1 --align 1 --font GoogleSans-Bold.ttf --symbols 0123456789:.,+-= --format lvgl -o GoogleSans22_digits_1bpp.c
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



#ifndef GOOGLESANS22_DIGITS_1BPP
#define GOOGLESANS22_DIGITS_1BPP 1
#endif

#if GOOGLESANS22_DIGITS_1BPP

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+002B "+" */
    0xe, 0x1, 0xc0, 0x38, 0x7, 0xf, 0xff, 0xff,
    0xff, 0xf8, 0x70, 0xe, 0x1, 0xc0, 0x38, 0x0,

    /* U+002C "," */
    0x6f, 0xf6, 0x6c, 0x0,

    /* U+002D "-" */
    0xff, 0xff, 0xf8,

    /* U+002E "." */
    0x6f, 0xf6,

    /* U+0030 "0" */
    0xf, 0x81, 0xfe, 0xf, 0xf8, 0xf1, 0xe7, 0x7,
    0x70, 0x1f, 0x80, 0xfc, 0x7, 0xe0, 0x3f, 0x1,
    0xf8, 0xe, 0xe0, 0xe7, 0x8f, 0x1f, 0xf0, 0xff,
    0x1, 0xf0,

    /* U+0031 "1" */
    0xe, 0x3d, 0xff, 0xf6, 0xe1, 0xc3, 0x87, 0xe,
    0x1c, 0x38, 0x70, 0xe1, 0xc3, 0x87,

    /* U+0032 "2" */
    0x1e, 0x1f, 0xef, 0xff, 0x8f, 0x21, 0xc0, 0x70,
    0x1c, 0xe, 0x7, 0x3, 0x81, 0xc1, 0xe0, 0xf0,
    0x3f, 0xff, 0xff, 0xff,

    /* U+0033 "3" */
    0x1f, 0xf, 0xe7, 0xff, 0xc7, 0x1, 0xc0, 0xf1,
    0xf8, 0x7c, 0x1f, 0x80, 0xf0, 0x1d, 0x87, 0xe3,
    0xff, 0xe7, 0xf8, 0x78,

    /* U+0034 "4" */
    0x3, 0xc0, 0x3c, 0x7, 0xc0, 0xfc, 0xf, 0xc1,
    0xdc, 0x19, 0xc3, 0x9c, 0x71, 0xc7, 0x1c, 0xff,
    0xff, 0xff, 0xff, 0xf0, 0x1c, 0x1, 0xc0, 0x1c,

    /* U+0035 "5" */
    0x7f, 0xcf, 0xf9, 0xff, 0x38, 0x7, 0x0, 0xde,
    0x1f, 0xe3, 0xfe, 0x31, 0xe0, 0x1c, 0x3, 0xf0,
    0x7f, 0x1e, 0xff, 0x8f, 0xe0, 0xf8,

    /* U+0036 "6" */
    0x0, 0x0, 0xc0, 0x1c, 0x7, 0x1, 0xe0, 0x38,
    0xe, 0x1, 0xf8, 0x7f, 0xcf, 0xfb, 0xc7, 0xf0,
    0x7e, 0xf, 0xc1, 0xfc, 0x7b, 0xfe, 0x3f, 0x83,
    0xe0,

    /* U+0037 "7" */
    0xff, 0xff, 0xff, 0xfc, 0x7, 0x3, 0x80, 0xe0,
    0x70, 0x1c, 0xe, 0x3, 0x81, 0xc0, 0x70, 0x38,
    0xe, 0x7, 0x1, 0xc0, 0x20, 0x0,

    /* U+0038 "8" */
    0x1e, 0xf, 0xf3, 0xff, 0x79, 0xee, 0x1d, 0xe7,
    0x9f, 0xe1, 0xfc, 0x7f, 0xde, 0x3f, 0x83, 0xf0,
    0x7f, 0x1e, 0xff, 0x9f, 0xf0, 0xf8,

    /* U+0039 "9" */
    0x1f, 0x7, 0xf1, 0xff, 0x78, 0xfe, 0xf, 0xc1,
    0xf8, 0x3f, 0x8f, 0x7f, 0xcf, 0xf8, 0x7e, 0x1,
    0xc0, 0x70, 0x1e, 0x3, 0x80, 0xe0, 0xc, 0x0,

    /* U+003A ":" */
    0x6f, 0xf6, 0x0, 0x0, 0x6f, 0xf6,

    /* U+003D "=" */
    0xff, 0xff, 0xff, 0xfc, 0x0, 0xff, 0xff, 0xff,
    0xfc
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 205, .box_w = 11, .box_h = 11, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 16, .adv_w = 100, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 20, .adv_w = 163, .box_w = 7, .box_h = 3, .ofs_x = 1, .ofs_y = 6},
    {.bitmap_index = 23, .adv_w = 100, .box_w = 4, .box_h = 4, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 25, .adv_w = 237, .box_w = 13, .box_h = 16, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 51, .adv_w = 162, .box_w = 7, .box_h = 16, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 65, .adv_w = 195, .box_w = 10, .box_h = 16, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 85, .adv_w = 203, .box_w = 10, .box_h = 16, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 105, .adv_w = 220, .box_w = 12, .box_h = 16, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 129, .adv_w = 202, .box_w = 11, .box_h = 16, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 151, .adv_w = 203, .box_w = 11, .box_h = 18, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 176, .adv_w = 195, .box_w = 10, .box_h = 17, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 198, .adv_w = 213, .box_w = 11, .box_h = 16, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 220, .adv_w = 203, .box_w = 11, .box_h = 17, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 244, .adv_w = 100, .box_w = 4, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 250, .adv_w = 209, .box_w = 10, .box_h = 7, .ofs_x = 1, .ofs_y = 3}
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
    0, 0, -11, 0, 0, 0, 0, 0,
    0, 0, 0, -14, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -7, 0, 0,
    0, -7, 0, 0, 0, 0, 0, 0,
    -7, 0, 0, 0, 0, 0, -7, 0,
    0, 0, 0, -7, -4, 0, 0, -21,
    0, 0, 0, 0, -7, -14, 0, 0,
    -14, -11, -4, -4, 0, -11, -11, 0,
    0, -25, -7, -4, 0, 0, -14, -18,
    -39, -11, 0, -7, -7, -25, -21, 0,
    0, -18, 0, 0, -7, 0, -11, -7,
    -4, 0
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

extern const lv_font_t lv_font_montserrat_22;


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t GoogleSans22_digits_1bpp = {
#else
lv_font_t GoogleSans22_digits_1bpp = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 21,          /*The maximum line height required by the font*/
    .base_line = 3,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -4,
    .underline_thickness = 2,
#endif
    .static_bitmap = 0,
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = &lv_font_montserrat_22,
#endif
    .user_data = NULL,
};



#endif /*#if GOOGLESANS22_DIGITS_1BPP*/
