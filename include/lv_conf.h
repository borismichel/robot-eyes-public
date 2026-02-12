/**
 * LVGL Configuration for Robot Eyes
 * ESP32-S3-Touch-AMOLED-1.8 (368x448 -> 448x368 landscape)
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

/* Color depth: 16 (RGB565) for AMOLED */
#define LV_COLOR_DEPTH 16

/* Swap the 2 bytes of RGB565 color for SPI displays */
#define LV_COLOR_16_SWAP 0

/*====================
   MEMORY SETTINGS
 *====================*/

/* Size of the memory available for `lv_malloc()` in bytes (>= 2kB) */
#define LV_MEM_SIZE (128U * 1024U)

/* Use the standard `malloc` and `free` from the C library */
#define LV_MEM_CUSTOM 0

/* Number of the intermediate memory buffer used during rendering */
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN 4

/*====================
   HAL SETTINGS
 *====================*/

/* Default display refresh period in milliseconds */
#define LV_DEF_REFR_PERIOD 16  /* ~60fps */

/* Input device read period in milliseconds */
#define LV_INDEV_DEF_READ_PERIOD 30

/* Use a custom tick source */
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

/*====================
   FEATURE CONFIGURATION
 *====================*/

/*-------------
 * Drawing
 *-----------*/

/* Enable complex draw engine */
#define LV_DRAW_COMPLEX 1

/* Allow buffering some shadow calculation */
#define LV_SHADOW_CACHE_SIZE 0

/* Set number of maximally cached circle data */
#define LV_CIRCLE_CACHE_SIZE 4

/*-------------
 * GPU
 *-----------*/

/* Use ESP32 DMA2D (not available, keep disabled) */
#define LV_USE_GPU_ESP32 0

/*-------------
 * Logging
 *-----------*/

/* Enable the log module */
#define LV_USE_LOG 1
#if LV_USE_LOG
    /* Log level: TRACE, INFO, WARN, ERROR, USER, NONE */
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    /* Print logs with 'printf' */
    #define LV_LOG_PRINTF 1
#endif

/*-------------
 * Asserts
 *-----------*/

#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/*-------------
 * Others
 *-----------*/

/* Define a custom attribute for large constant arrays */
#define LV_ATTRIBUTE_LARGE_CONST

/* Prefix for exported symbols (for dlopen) */
#define LV_EXPORT_CONST_INT(int_value) struct _silence_gcc_warning

/* Prefix for large arrays */
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY

/* Place performance critical functions into faster memory */
#define LV_ATTRIBUTE_FAST_MEM

/* Export integer constant to binding */
#define LV_EXPORT_CONST_INT(int_value) struct _silence_gcc_warning

/* Enable float usage (needed for animations) */
#define LV_USE_FLOAT 1

/*====================
 * COMPILER SETTINGS
 *====================*/

/* For big endian systems set to 1 */
#define LV_BIG_ENDIAN_SYSTEM 0

/* Align stride of all rendered images to this bytes */
#define LV_DRAW_BUF_STRIDE_ALIGN 1

/* Align start address of allocated buffers */
#define LV_DRAW_BUF_ALIGN 4

/*====================
   FONT USAGE
 *====================*/

/* Montserrat fonts with ASCII range and some symbols */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

/* Default font */
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Enable handling large font and/or fonts with a lot of characters */
#define LV_FONT_FMT_TXT_LARGE 0

/* Support subpixel rendering */
#define LV_FONT_SUBPX_BGR 0

/*====================
   WIDGETS
 *====================*/

/* Only enable widgets we need for eye rendering */
#define LV_USE_ARC        0
#define LV_USE_BAR        0
#define LV_USE_BTN        0
#define LV_USE_BTNMATRIX  0
#define LV_USE_CANVAS     1  /* For custom eye drawing */
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMG        1
#define LV_USE_LABEL      1  /* For debug */
#define LV_USE_LINE       0
#define LV_USE_ROLLER     0
#define LV_USE_SLIDER     0
#define LV_USE_SWITCH     0
#define LV_USE_TEXTAREA   0
#define LV_USE_TABLE      0

/*====================
   EXTRA COMPONENTS
 *====================*/

/* Animations (essential for smooth eye movements) */
#define LV_USE_ANIM 1

/* Layouts */
#define LV_USE_FLEX 0
#define LV_USE_GRID 0

/* Others */
#define LV_USE_MSG 0
#define LV_USE_FRAGMENT 0
#define LV_USE_IMGFONT 0
#define LV_USE_SNAPSHOT 0
#define LV_USE_MONKEY 0

/*====================
   EXTRA WIDGETS (Disable all - we don't need them)
 *====================*/

#define LV_USE_ANIMIMG    0
#define LV_USE_CALENDAR   0
#define LV_USE_CHART      0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN     0
#define LV_USE_KEYBOARD   0
#define LV_USE_LED        0
#define LV_USE_LIST       0
#define LV_USE_MENU       0
#define LV_USE_METER      0
#define LV_USE_MSGBOX     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0
#define LV_USE_TABVIEW    0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0

/*====================
   THEMES
 *====================*/

/* No theme needed - we draw custom eyes */
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE  0
#define LV_USE_THEME_MONO    0

#endif /* LV_CONF_H */
