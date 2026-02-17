/**
 * @file version.h
 * @brief Firmware version information for OTA updates
 */

#ifndef VERSION_H
#define VERSION_H

#define FIRMWARE_VERSION "2.1.0"
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__

// Release notes for current version (shown in System tab)
#define FIRMWARE_RELEASE_NOTES \
    "• Streaming TTS: audio starts in ~1-2s (was 10+s)\n" \
    "• Ghost touch filter: prevents phantom triggers\n" \
    "• Mic gain control with ES8311 REG17\n" \
    "• STT noise gate (crest factor rejection)\n" \
    "• OAuth 2.1 client for MCP servers"

#endif // VERSION_H
