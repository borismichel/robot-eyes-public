/**
 * @file version.h
 * @brief Firmware version information for OTA updates
 */

#ifndef VERSION_H
#define VERSION_H

#define FIRMWARE_VERSION "2.1.1"
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__

// Release notes for current version (shown in System tab)
#define FIRMWARE_RELEASE_NOTES \
    "• TTS pre-buffer: ~500ms PSRAM buffer absorbs WiFi jitter\n" \
    "• Fix: TTS silent after chime playback\n" \
    "• Fix: Watchdog crash during TTS streaming"

#endif // VERSION_H
