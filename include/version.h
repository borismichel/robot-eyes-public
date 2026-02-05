/**
 * @file version.h
 * @brief Firmware version information for OTA updates
 */

#ifndef VERSION_H
#define VERSION_H

#define FIRMWARE_VERSION "1.1.0"
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__

// Release notes for current version (shown in System tab)
#define FIRMWARE_RELEASE_NOTES \
    "• Breathing exercise with box breathing pattern\n" \
    "• Mindfulness menu and scheduled reminders\n" \
    "• Improved first-boot WiFi setup (two-phase flow)\n" \
    "• Fixed eye corner rendering for left eye\n" \
    "• Post-exercise Content → Relaxed animation"

#endif // VERSION_H
