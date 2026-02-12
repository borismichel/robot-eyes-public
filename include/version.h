/**
 * @file version.h
 * @brief Firmware version information for OTA updates
 */

#ifndef VERSION_H
#define VERSION_H

#define FIRMWARE_VERSION "2.0.0"
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__

// Release notes for current version (shown in System tab)
#define FIRMWARE_RELEASE_NOTES \
    "• Architecture rewrite: modular class-based design\n" \
    "• main.cpp: 3,216 → 1,464 lines (55% reduction)\n" \
    "• 8 new modules: ExpressionManager, BlinkController,\n" \
    "  MicroExpressions, ReactiveAnimations, EyeAnimator,\n" \
    "  ScreenCompositor, TimerVisuals, NetworkSetup\n" \
    "• Shared TextRenderer eliminates 360 lines of duplication\n" \
    "• Web UI HTML extracted to separate header"

#endif // VERSION_H
