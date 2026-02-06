/**
 * @file version.h
 * @brief Firmware version information for OTA updates
 */

#ifndef VERSION_H
#define VERSION_H

#define FIRMWARE_VERSION "1.2.0"
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__

// Release notes for current version (shown in System tab)
#define FIRMWARE_RELEASE_NOTES \
    "• Voice assistant (Claude/OpenAI) with tool use\n" \
    "• MCP server + client for external integration\n" \
    "• Countdown timer and timed reminders (up to 20)\n" \
    "• Settings control via MCP (volume, brightness, color)\n" \
    "• Wake word: Hey Buddy (ESP-SR local detection)"

#endif // VERSION_H
