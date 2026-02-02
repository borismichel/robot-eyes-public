/**
 * Display Driver Implementation for Waveshare ESP32-S3-Touch-AMOLED-1.8
 * SH8601 AMOLED controller via QSPI using Arduino_GFX library
 */

#include "display_driver.h"
#include "pin_config.h"
#include <Arduino_GFX_Library.h>

#ifndef RGB565_CYAN
#define RGB565_CYAN 0x07FF
#endif

// Display state
static lv_disp_t *disp = nullptr;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static uint32_t flush_count = 0;

// Arduino_GFX objects - created at global scope like the working demo
static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS,     // CS
    LCD_SCLK,   // SCK
    LCD_SDIO0,  // SDIO0
    LCD_SDIO1,  // SDIO1
    LCD_SDIO2,  // SDIO2
    LCD_SDIO3   // SDIO3
);

// Use rotation=1 (landscape) for robot eyes
static Arduino_SH8601 *gfx = new Arduino_SH8601(
    bus,
    -1,         // RST (GFX_NOT_DEFINED)
    1,          // rotation (1=landscape for eyes)
    LCD_WIDTH,  // width (368)
    LCD_HEIGHT  // height (448)
);

// Forward declarations
static void display_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);

bool display_init() {
    Serial.println("Initializing SH8601 AMOLED display via QSPI...");

    // Initialize the display
    if (!gfx->begin()) {
        Serial.println("ERROR: Display initialization failed!");
        return false;
    }

    // Set brightness to max
    gfx->setBrightness(255);

    // Clear screen to black
    gfx->fillScreen(RGB565_BLACK);

    Serial.printf("Display initialized: %dx%d\n", gfx->width(), gfx->height());

    // Initialize LVGL
    lv_init();
    Serial.println("LVGL initialized");

    // Use buffer size for landscape mode
    size_t buf_size = LCD_HEIGHT * 20;  // 20 lines at a time (448 pixels wide)

    // Allocate display buffers in PSRAM if available
    buf1 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!buf1) {
        Serial.println("PSRAM allocation failed, using internal RAM");
        buf1 = (lv_color_t *)malloc(buf_size * sizeof(lv_color_t));
    }
    buf2 = nullptr;

    if (!buf1) {
        Serial.println("ERROR: Display buffer allocation failed!");
        return false;
    }

    // Zero out buffer to avoid noise
    memset(buf1, 0, buf_size * sizeof(lv_color_t));

    Serial.printf("Display buffer allocated: %d pixels, %d bytes\n",
                  buf_size, buf_size * sizeof(lv_color_t));

    // Initialize display buffer (single buffer mode)
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_size);

    // Initialize display driver - use landscape dimensions since rotation=1
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_HEIGHT;  // 448 (landscape width)
    disp_drv.ver_res = LCD_WIDTH;   // 368 (landscape height)
    disp_drv.flush_cb = display_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 1;  // Try full refresh mode

    Serial.printf("LVGL driver: %dx%d\n", disp_drv.hor_res, disp_drv.ver_res);

    // Register display
    disp = lv_disp_drv_register(&disp_drv);

    if (!disp) {
        Serial.println("ERROR: LVGL display registration failed!");
        return false;
    }

    Serial.println("LVGL display driver registered successfully");
    return true;
}

void display_set_brightness(uint8_t brightness) {
    if (gfx) {
        gfx->setBrightness(brightness);
    }
}

lv_disp_t* display_get() {
    return disp;
}

void display_update() {
    lv_timer_handler();
}

/**
 * LVGL flush callback - sends pixels to display
 */
static void display_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    // Debug output for first few flushes
    if (flush_count < 5) {
        Serial.printf("Flush #%d: (%d,%d)-(%d,%d) size=%dx%d\n",
                      flush_count, area->x1, area->y1, area->x2, area->y2, w, h);
        flush_count++;
    }

    // Use Arduino_GFX to draw the bitmap
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);

    lv_disp_flush_ready(drv);
}
