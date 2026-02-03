# Robot Eyes - Claude Project Notes

## CRITICAL: Repository Structure

There are TWO folders/repos:
- **`robot-eyes/`** (PRIVATE) - Main development folder with full git history
- **`robot-eyes-public/`** (PUBLIC) - Clean mirror without git history

**Rules:**
1. **All edits happen in the private folder** (`robot-eyes/`)
2. **ASK before any git commit or push** - Do not commit or push without user approval
3. **Public folder is read-only** - Only sync from private when user requests
4. To sync public: copy files from private (excluding `.git/`), then commit as single update

---

## CRITICAL: After Every Code Change

1. **Build** - Always compile to verify changes:
   ```bash
   /Users/bmichel/.local/bin/pio run
   ```

2. **Check for device** - Look for USB serial port:
   ```bash
   ls /dev/cu.usb* 2>/dev/null || echo "No device connected"
   ```

3. **Flash if available** - Upload when device is connected:
   ```bash
   /Users/bmichel/.local/bin/pio run -t upload
   ```

4. **Commit** - After completing any code change, **ask user before committing**. Do not batch multiple features - commit each logical unit of work separately.

---

## CRITICAL: 90° CCW Screen Rotation

**THE DISPLAY IS PHYSICALLY ROTATED 90° COUNTER-CLOCKWISE. This affects ALL geometry, rendering, and touch handling.**

### Coordinate Mapping (MEMORIZE THIS)
```
Physical screen: 368×448 (portrait hardware, SCREEN_WIDTH=368, SCREEN_HEIGHT=448)
Buffer: 336×416 (COMBINED_BUF_WIDTH×COMBINED_BUF_HEIGHT)
Buffer positioned at screen (16, 16) - inside 16px progress bar margins

Buffer X (0-336) → Screen VERTICAL (up/down on screen)
Buffer Y (0-416) → Screen HORIZONTAL (left/right on screen)
```

### Practical Implications

| Concept | Buffer Coordinate | Screen Appearance |
|---------|------------------|-------------------|
| Eye "height" (tall/short) | Buffer X | Vertical on screen |
| Eye "width" (wide/narrow) | Buffer Y | Horizontal on screen |
| Gaze up/down | offsetX | Moves eye vertically on screen |
| Gaze left/right | offsetY | Moves eye horizontally on screen |
| Top eyelid | Fills from low buffer X | Covers from screen top |
| Bottom eyelid | Fills from high buffer X | Covers from screen bottom |
| Left eye position | Lower buffer Y (145) | LEFT side of screen |
| Right eye position | Higher buffer Y (290) | RIGHT side of screen |

### For Special Shapes (Star, Heart, Swirl)
When calculating polar coordinates or shape math for screen appearance:
```cpp
float dx = (float)(py - cy);  // Screen horizontal (from buffer Y)
float dy = (float)(px - cx);  // Screen vertical (from buffer X)
```

### Corner Offsets (Eyebrows)
Despite being called "innerCornerY" and "outerCornerY", these are applied to buffer X:
```cpp
float adjustedRX = (float)rx - rowYOffset;  // Applied to X, moves corners up/down on SCREEN
```

### Settings Menu Rotation
Screen-to-buffer transformation for UI rendering (SCREEN_W=416, SCREEN_H=336):
```cpp
// In settings_menu.cpp:
#define SCREEN_W 416  // buffer height becomes screen width after rotation
#define SCREEN_H 336  // buffer width becomes screen height after rotation

// screen (sx, sy) → buffer (sy, bufH - 1 - sx)
int16_t bx = sy;
int16_t by = bufH - 1 - sx;
```

---

## Buffer Layout

- Physical screen: 368×448 (portrait hardware, SCREEN_WIDTH×SCREEN_HEIGHT)
- Combined buffer: 336×416 (COMBINED_BUF_WIDTH×COMBINED_BUF_HEIGHT)
- Buffer positioned at screen (16, 16) to leave room for 16px progress bar frame
- After 90° CCW rotation: appears as 416×336 on screen (landscape view)
- Eyes side-by-side HORIZONTALLY on screen = different buffer Y positions
- PSRAM optimizations: dirty-rect clearing + partial screen blit

### Progress Bar Frame (Pomodoro)
- Thickness: 16px around all edges
- Corner radius: 42px
- Depletes clockwise: top → right → bottom → left
- Buffer starts 16px inside screen edges to avoid overlap

## Eye Dimensions

- BASE_EYE_WIDTH = 120 → appears as eye HEIGHT on screen (buffer X)
- BASE_EYE_HEIGHT = 100 → appears as eye WIDTH on screen (buffer Y)
- Left eye center: (168, 148) in buffer → (168 = COMBINED_BUF_WIDTH/2, 148 = COMBINED_BUF_HEIGHT/2 - 60)
- Right eye center: (168, 268) in buffer → (168 = COMBINED_BUF_WIDTH/2, 268 = COMBINED_BUF_HEIGHT/2 + 60)
- Eye spacing: 120px center-to-center in buffer Y (horizontal on screen)

## Settings Menu

Hierarchical menu with main menu and sub-menus:

**Main Menu (3 pages):**
- PAGE_POMODORO (0): Opens pomodoro sub-menu
- PAGE_SETTINGS (1): Opens settings sub-menu
- PAGE_EXIT (2): Closes menu

**Pomodoro Sub-Menu (7 pages):**
- POMO_PAGE_START_STOP (0): Start/stop timer
- POMO_PAGE_WORK (1): Work duration slider
- POMO_PAGE_SHORT_BREAK (2): Short break slider
- POMO_PAGE_LONG_BREAK (3): Long break slider
- POMO_PAGE_SESSIONS (4): Sessions before long break
- POMO_PAGE_TICKING (5): Toggle tick sound
- POMO_PAGE_BACK (6): Return to main menu

**Settings Sub-Menu (8 pages):**
- SETTINGS_PAGE_VOLUME (0): Volume slider
- SETTINGS_PAGE_BRIGHTNESS (1): Brightness slider
- SETTINGS_PAGE_MIC_GAIN (2): Mic gain slider
- SETTINGS_PAGE_MIC_THRESHOLD (3): Mic threshold slider
- SETTINGS_PAGE_COLOR (4): Color preset selector
- SETTINGS_PAGE_TIME (5): Set hours/minutes
- SETTINGS_PAGE_TIME_FORMAT (6): Toggle 12H/24H
- SETTINGS_PAGE_BACK (7): Return to main menu

Navigation: Swipe up/down, tap to select/toggle
Settings persisted via Preferences library

## Current Expression Count

30 expressions total (as of Jan 2026):
- Core: Neutral, Happy, Sad, Surprised, Angry, Suspicious, Sleepy, Scared, Content, Startled, Grumpy, Joyful, Focused, Confused, Yawn, ContentPetting
- Special shapes: Dazed (swirl), Dizzy (star), Love (heart), Joy
- Micro-expressions: Curious, Thinking, Mischievous, Bored, Alert
- New (curve/stretch): Smug, Dreamy, Skeptical, Squint, Wink

---

## Pomodoro Timer

State machine in `src/ui/pomodoro.h`:

```cpp
enum class PomodoroState {
    Idle,           // Not running
    Working,        // Work session (default 25 min)
    ShortBreak,     // After each work (default 5 min)
    LongBreak,      // After 4 work sessions (default 15 min)
    Celebration,    // Transition animation
    WaitingForTap   // User needs to tap to continue
};
```

**Progress bar rendering** (in main.cpp `renderPomodoroProgressBar()`):
- 16px thick frame, 42px corner radius
- Depletes clockwise: top → right → bottom → left
- `progressiveCorners=true` makes corner arcs deplete smoothly per-circle
- Uses `manageWrite` parameter for nested startWrite/endWrite calls

**Key functions:**
- `pomodoroTimer.getProgress()` → 0.0-1.0 (depletes over time)
- `pomodoroTimer.getRemainingSeconds()` → countdown value
- `pomodoroTimer.isLastMinute()` → tick sound trigger
- `settingsMenu.renderCountdown()` → MM:SS display with label

---

## IMU Tilt-Based Gaze

Gravity-aware eye tracking in `src/input/imu_handler.h`:

```cpp
// Get gaze offset based on tilt (-1 to 1 range)
float getTiltGazeX();  // Vertical gaze from forward/backward tilt
float getTiltGazeY();  // Horizontal gaze from left/right tilt

// Orientation detection
enum class Orientation { Normal, FaceDown, TiltedLong };
Orientation getOrientation();
bool isFaceDown();     // Screen facing floor
float getTiltDuration(); // Seconds tilted >45°
```

**In main.cpp gaze calculation:**
```cpp
// Axes swapped for 90° CCW rotation: TiltY → vertical (X), TiltX → horizontal (Y)
float gravityGazeX = imu.getTiltGazeY();  // Forward tilt → look down
float gravityGazeY = imu.getTiltGazeX();  // Side tilt → look sideways
```

**Thresholds:**
- `TILT_MAX_ANGLE = 45.0f` degrees for full gaze offset
- `FACE_DOWN_THRESHOLD = -0.7g` on Z axis
- `TILT_LONG_SECONDS = 5.0f` for uncomfortable expression

---

## Time-of-Day Mood

Mood modifiers in `src/behavior/time_mood.h`:

```cpp
enum class TimeMood { Morning, Afternoon, Evening, Night };

struct MoodModifiers {
    float blinkRateMultiplier;   // 0.7-1.2 (higher = more frequent)
    float gazeSpeedMultiplier;   // 0.6-1.1 (lower = slower)
    float baseLidOffset;         // 0.0-0.12 (higher = droopier)
};
```

| Period | Hours | Blink | Gaze | Lid Offset |
|--------|-------|-------|------|------------|
| Morning | 6-12 | 1.2x | 1.1x | 0.0 |
| Afternoon | 12-18 | 1.0x | 1.0x | 0.0 |
| Evening | 18-22 | 0.85x | 0.8x | 0.05 |
| Night | 22-6 | 0.7x | 0.6x | 0.12 |

Applied in main.cpp via:
```cpp
idleBehavior.setMoodModifiers(moodMods.blinkRateMultiplier, moodMods.gazeSpeedMultiplier);
leftEye.topLid += moodMods.baseLidOffset;
```

---

## WiFi & Web Server

Network module in `src/network/`:

### WiFi State Machine (wifi_manager.h)

```cpp
enum class WiFiState {
    Unconfigured,       // No saved credentials - start AP mode
    APMode,             // Running as access point for setup
    Connecting,         // Attempting to connect to saved network
    Connected,          // Successfully connected
    ConnectionFailed    // Failed, falling back to AP mode
};
```

**AP Mode Configuration:**
- SSID: `DeskBuddy-Setup`
- Password: `deskbuddy`
- IP: `192.168.4.1`
- Connect timeout: 15 seconds

**Factory Reset:**
- Hold BOOT button for 5+ seconds
- Clears saved credentials
- Device restarts in AP mode

### Web Server (web_server.h)

Uses ESP-IDF native `esp_http_server` for Arduino ESP32 3.x compatibility.

**Key Endpoints:**
```cpp
GET  /                  // Main web UI
GET  /api/settings      // All settings JSON
POST /api/settings      // Update settings (volume, brightness, micGain, micThreshold, eyeColorIndex, workMinutes, shortBreakMinutes, longBreakMinutes, sessionsBeforeLongBreak, tickingEnabled)
GET  /api/status        // Status JSON (wifi, pomodoro, time, uptimeSeconds)
POST /api/expression    // Preview expression (index: 0-29)
POST /api/pomodoro/start
POST /api/pomodoro/stop
GET  /api/wifi/scan     // Returns array of {ssid, rssi, secure}
POST /api/wifi/connect  // {ssid, password}
POST /api/wifi/forget   // Clears credentials
```

**Settings Change Detection:**
```cpp
// In main.cpp loop:
if (webServer.hasSettingsChange()) {
    audioPlayer.setVolume(settingsMenu.getVolume());
    gfx->setBrightness((settingsMenu.getBrightness() * 255) / 100);
    audioPlayer.setMicGain(settingsMenu.getMicSensitivity());
    audio.setThreshold(settingsMenu.getMicThreshold() / 100.0f);
    renderer.setColor(settingsMenu.getColorRGB565());
    webServer.clearSettingsChange();
}
```

**Expression Preview Callback:**
```cpp
void onWebExpressionPreview(int index) {
    if (index >= 0 && index < (int)Expression::COUNT) {
        setExpression((Expression)index);
    }
}
webServer.setExpressionCallback(onWebExpressionPreview);
```

### Web UI Design

- Dark theme: background `#0A0A0A`, foreground `#F2F2F2`, accent `#DFFF00` (neon yellow)
- Fonts: JetBrains Mono (labels), Inter (body)
- Tabbed navigation: Dashboard, Display, Audio, Time, WiFi, Pomodoro, Expressions
- Settings sync via polling `/api/status` every 1 second
- Color picker: 8 presets matching device `COLOR_PRESETS` array order

**Eye Color Order (must match device):**
```cpp
const EYE_COLORS = [
    { name: 'Cyan', hex: '#00FFFF' },
    { name: 'Pink', hex: '#FF00FF' },
    { name: 'Green', hex: '#00FF00' },
    { name: 'Orange', hex: '#FFA500' },
    { name: 'Purple', hex: '#8000FF' },
    { name: 'White', hex: '#FFFFFF' },
    { name: 'Red', hex: '#FF0000' },
    { name: 'Blue', hex: '#0000FF' }
];
```
