/**
 * Pin Configuration for Waveshare ESP32-S3-Touch-AMOLED-1.8
 * Based on official Waveshare demo code
 */

#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

#define XPOWERS_CHIP_AXP2101

// =============================================================================
// LCD QSPI Pins (SH8601 AMOLED via QSPI)
// =============================================================================
#define LCD_SDIO0   4
#define LCD_SDIO1   5
#define LCD_SDIO2   6
#define LCD_SDIO3   7
#define LCD_SCLK    11
#define LCD_CS      12
#define LCD_RST     -1  // Controlled via IO expander (GFX_NOT_DEFINED = -1)

// =============================================================================
// Display Dimensions (Native Portrait)
// =============================================================================
#define LCD_WIDTH   368
#define LCD_HEIGHT  448

// =============================================================================
// I2C Bus Pins
// =============================================================================
#define IIC_SDA     15
#define IIC_SCL     14

// =============================================================================
// Touch Controller (FT3168)
// =============================================================================
#define TP_INT      21
#define FT3168_DEVICE_ADDRESS 0x38

// =============================================================================
// IMU Sensor (QMI8658)
// =============================================================================
#define QMI8658_ADDRESS 0x6B

// =============================================================================
// I2S Audio (ES8311)
// =============================================================================
#define I2S_MCK_IO  16
#define I2S_BCK_IO  9
#define I2S_DI_IO   10
#define I2S_WS_IO   45
#define I2S_DO_IO   8

#define MCLKPIN     16
#define BCLKPIN     9
#define WSPIN       45
#define DOPIN       10
#define DIPIN       8
#define PA          46

// =============================================================================
// Power Management (AXP2101)
// =============================================================================
#define AXP2101_SLAVE_ADDRESS 0x34

// =============================================================================
// RTC (PCF85063)
// =============================================================================
#define PCF85063_SLAVE_ADDRESS 0x51

// =============================================================================
// SD Card (SDMMC)
// =============================================================================
#define SDMMC_CLK   2
#define SDMMC_CMD   1
#define SDMMC_DATA  3

#endif // PIN_CONFIG_H
