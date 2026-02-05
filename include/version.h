/**
 * @file version.h
 * @brief Firmware version information for OTA updates
 */

#ifndef VERSION_H
#define VERSION_H

#define FIRMWARE_VERSION "1.0.0"
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__

// Release notes for current version (shown in System tab)
#define FIRMWARE_RELEASE_NOTES \
    "• OTA firmware updates via web UI\n" \
    "• NTP time sync with timezone support\n" \
    "• Current mood display on dashboard\n" \
    "• Audio test button\n" \
    "• Signature verification for secure updates"

#endif // VERSION_H
