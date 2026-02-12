# Robot Eyes - Claude Project Notes

## CRITICAL: Repository Structure

There are TWO folders/repos:
- **`robot-eyes/`** (PRIVATE) - Main development folder with full git history
- **`robot-eyes-public/`** (PUBLIC) - Clean mirror without git history

**Rules:**
1. **All edits happen in the private folder** (`robot-eyes/`)
2. **ASK before any git commit or push** - Do not commit or push without user approval
3. **Public folder is read-only** - Only sync from private when user requests
4. To sync public: `rsync -av --delete --exclude='.git/' --exclude='releases/' robot-eyes/ robot-eyes-public/`
   - Excludes `.git/` (keep public repo's own git history)
   - Excludes `releases/` (binaries distributed via GitHub Releases instead)

---

## Firmware Versioning & Releases

### Version File
Version is defined in `include/version.h`:
```cpp
#define FIRMWARE_VERSION "2.0.0"
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__
```

### Semantic Versioning
Use [SemVer](https://semver.org/): `MAJOR.MINOR.PATCH`
- **MAJOR**: Breaking changes, major new features, architecture changes
- **MINOR**: New features, backward-compatible enhancements
- **PATCH**: Bug fixes, small improvements

### Release Process
When creating a new release:

1. **Update version** in `include/version.h`
2. **Build firmware**: `pio run`
3. **Copy to releases**:
   ```bash
   cp .pio/build/esp32s3-amoled/firmware.bin releases/deskbuddy-vX.Y.Z.bin
   ```
4. **Update changelog** in `releases/CHANGELOG.md`:
   - Add new version section at TOP (newest first)
   - Use categories: Added, Changed, Fixed, Removed, Security
   - Include date in `[X.Y.Z] - YYYY-MM-DD` format
5. **Commit** with message: `Release vX.Y.Z - brief description`
6. **Tag** (optional): `git tag -a vX.Y.Z -m "Release vX.Y.Z"`

### Release Directory Structure
```
releases/
├── CHANGELOG.md           # Version history (newest at top)
├── deskbuddy-v2.0.0.bin   # Current release
├── deskbuddy-v1.3.0.bin   # Previous release
└── ...
```

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

## Architecture Overview (v2.0.0)

The firmware uses a modular class-based architecture. Each module follows the `begin(deps...)`/`update()` lifecycle pattern and owns its own state. `main.cpp` (~1,464 lines) serves as the orchestrator: it wires modules together in `setup()` and runs the frame loop in `loop()`.

### Module Dependency Graph
```
main.cpp
  ├── ExpressionManager ← (owns eye shape tweeners)
  │   ├── MicroExpressionSystem
  │   ├── ReactiveAnimations
  │   └── TimerVisuals
  ├── BlinkController ← (driven by IdleBehavior)
  ├── EyeAnimator ← (ExpressionManager, BlinkController, IdleBehavior, ImuHandler, MicroExpressions, ReactiveAnimations)
  ├── ScreenCompositor ← (Arduino_TFT*, EyeRenderer)
  ├── NetworkSetup ← (WiFiManager, CaptivePortal, SettingsMenu)
  └── TimerVisuals ← (ExpressionManager, AudioPlayer, ReactiveAnimations)
```

### Module Reference

| Module | Files | Responsibility |
|--------|-------|----------------|
| `ExpressionManager` | `behavior/expression_manager.h/.cpp` | Priority stack for expression control. 8 priority levels (Base...Sleep). Token-based: `request()`->token, `update()`, `release()`->auto-revert. |
| `BlinkController` | `animation/blink_controller.h/.cpp` | Self-contained blink state machine. Single/double blinks, variable speed. |
| `MicroExpressionSystem` | `behavior/micro_expressions.h/.cpp` | Idle personality: winks, glances, squints, eye widens. Random scheduling between micro-expressions. |
| `ReactiveAnimations` | `behavior/reactive_animations.h/.cpp` | Timed reactions: joy bounce, love hearts, irritated grump, IMU scared/dazed, orientation change, breathing post-completion. |
| `EyeAnimator` | `eyes/eye_animator.h/.cpp` | Composes final eye shapes from: base expression + gaze offsets + blink openness + micro-expression overlays + touch tracking + mood modifiers. |
| `ScreenCompositor` | `display/screen_compositor.h/.cpp` | Owns PSRAM framebuffer, eye positions, dirty rect tracking. Renders progress bars (pomodoro/breathing), sleep bars. Manages all screen blits (full, partial, safe-center). |
| `TimerVisuals` | `ui/timer_visuals.h/.cpp` | Timer expression side-effects: pomodoro concentrate animation (Sleepy->Alert->Focused), celebration joy, break content. Countdown celebration/expiry. Tick sounds. Progress bar clear animation. |
| `NetworkSetup` | `network/network_setup.h/.cpp` | WiFi lifecycle: enable/disable toggle, NTP sync on connect, captive portal start/stop, timezone change re-sync, first-boot setup/choice flow. |
| `TextRenderer` | `ui/text_renderer.h/.cpp` | Shared font & drawing namespace. 5x7 pixel font (0-9, A-Z, punctuation). `drawText`, `drawCenteredText`, `drawWrappedText`, `drawLargeDigit`. All handle 90deg CCW rotation. |

### Expression Priority Levels
```cpp
enum class ExprPriority : uint8_t {
    Base = 0,      // Mood drift base expression
    MicroExpr,     // Idle personality (wink, glance)
    Timer,         // Pomodoro/countdown effects
    Behavior,      // Joy, love, irritated, orientation, breathing
    Gesture,       // Petting (ContentPetting)
    IMU,           // Scared, dazed, dizzy, yawn
    Assistant,     // Voice assistant state
    Sleep          // Highest — overrides all
};
```

### main.cpp Structure
```
setup():
  Hardware init (display, touch, IMU, audio, codec)
  -> compositor.begin() -> expressionManager.begin()
  -> All module begin() calls
  -> Wire callbacks (web server, device tools, assistant)
  -> Apply saved settings -> initial expression

loop():
  Frame timing (30fps target)
  -> networkSetup.update() -> web server settings sync
  -> Touch input processing -> gesture detection
  -> Timer updates (pomodoro, countdown, reminder, breathing)
  -> timerVisuals.updatePomodoro() / updateCountdown()
  -> Behavior updates (sleep, mood, micro-expressions, reactive)
  -> blinkController.update() -> eyeAnimator.update()
  -> Render mode dispatch -> compositor blits
```

---

## CRITICAL: 90deg CCW Screen Rotation

**THE DISPLAY IS PHYSICALLY ROTATED 90deg COUNTER-CLOCKWISE. This affects ALL geometry, rendering, and touch handling.**

### Coordinate Mapping (MEMORIZE THIS)
```
Physical screen: 368x448 (portrait hardware, SCREEN_WIDTH=368, SCREEN_HEIGHT=448)
Buffer: 336x416 (COMBINED_BUF_WIDTH x COMBINED_BUF_HEIGHT)
Buffer positioned at screen (16, 16) - inside 16px progress bar margins

Buffer X (0-336) -> Screen VERTICAL (up/down on screen)
Buffer Y (0-416) -> Screen HORIZONTAL (left/right on screen)
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

**IMPORTANT:** Inner/outer corners are swapped for left eye due to screen rotation:
- Left eye's "outer" corner is on screen LEFT (buffer Y=0)
- Left eye's "inner" corner is on screen RIGHT (toward center)
- Right eye's "inner" corner is on screen LEFT (toward center)
- Right eye's "outer" corner is on screen RIGHT

The `eye_renderer.cpp` swaps `innerOffset` and `outerOffset` when `isLeftEye=true`.

### TextRenderer Rotation
All TextRenderer functions handle the 90deg CCW rotation internally:
```cpp
// screen (sx, sy) -> buffer (sy, bufH - 1 - sx)
int16_t bx = sy;
int16_t by = bufH - 1 - sx;
```

---

## Buffer Layout

- Physical screen: 368x448 (portrait hardware, SCREEN_WIDTH x SCREEN_HEIGHT)
- Combined buffer: 336x416 (COMBINED_BUF_WIDTH x COMBINED_BUF_HEIGHT)
- Buffer positioned at screen (16, 16) to leave room for 16px progress bar frame
- After 90deg CCW rotation: appears as 416x336 on screen (landscape view)
- Eyes side-by-side HORIZONTALLY on screen = different buffer Y positions
- PSRAM framebuffer managed by `ScreenCompositor`
- Dirty-rect optimization: only clears/blits changed eye regions

### Progress Bar Frame
- Managed by `ScreenCompositor::renderPomodoroProgressBar()` and `renderBreathingProgressBar()`
- Thickness: 16px around all edges
- Corner radius: 42px
- Pomodoro: depletes clockwise (top -> right -> bottom -> left)
- Breathing: fills/empties with pulse blending
- `lastRenderedFilledLen` cache avoids redundant redraws

## Eye Dimensions

- BASE_EYE_WIDTH = 120 -> appears as eye HEIGHT on screen (buffer X)
- BASE_EYE_HEIGHT = 100 -> appears as eye WIDTH on screen (buffer Y)
- Left eye center: (168, 148) in buffer
- Right eye center: (168, 268) in buffer
- Eye spacing: 120px center-to-center in buffer Y (horizontal on screen)
- Eye positions managed by `ScreenCompositor::initEyePositions()`

## Settings Menu

Hierarchical menu with main menu and sub-menus, rendered by `SettingsMenu` class using `TextRenderer`:

**Main Menu (4 pages):**
- PAGE_POMODORO (0): Opens pomodoro sub-menu
- PAGE_MINDFULNESS (1): Opens mindfulness sub-menu
- PAGE_SETTINGS (2): Opens settings sub-menu
- PAGE_EXIT (3): Closes menu

**Pomodoro Sub-Menu (7 pages):**
- POMO_PAGE_START_STOP (0): Start/stop timer
- POMO_PAGE_WORK (1): Work duration slider
- POMO_PAGE_SHORT_BREAK (2): Short break slider
- POMO_PAGE_LONG_BREAK (3): Long break slider
- POMO_PAGE_SESSIONS (4): Sessions before long break
- POMO_PAGE_TICKING (5): Toggle tick sound
- POMO_PAGE_BACK (6): Return to main menu

**Settings Sub-Menu (10 pages):**
- SETTINGS_PAGE_VOLUME (0): Volume slider
- SETTINGS_PAGE_BRIGHTNESS (1): Brightness slider
- SETTINGS_PAGE_MIC_GAIN (2): Mic gain slider
- SETTINGS_PAGE_MIC_THRESHOLD (3): Mic threshold slider
- SETTINGS_PAGE_COLOR (4): Color preset selector
- SETTINGS_PAGE_TIME (5): Set hours/minutes
- SETTINGS_PAGE_TIME_FORMAT (6): Toggle 12H/24H
- SETTINGS_PAGE_TIMEZONE (7): GMT offset (-12 to +14 hours)
- SETTINGS_PAGE_WIFI (8): Toggle WiFi on/off
- SETTINGS_PAGE_BACK (9): Return to main menu

**Mindfulness Sub-Menu (5 pages):**
- MINDFUL_PAGE_BREATHE_NOW (0): Start breathing exercise now
- MINDFUL_PAGE_SCHEDULE (1): Enable/disable scheduled reminders
- MINDFUL_PAGE_INTERVAL (2): Hours between reminders (1-8)
- MINDFUL_PAGE_SOUND (3): Toggle reminder sound
- MINDFUL_PAGE_BACK (4): Return to main menu

Navigation: Swipe up/down, tap to select/toggle. Settings persisted via Preferences library.

## Current Expression Count

32 expressions total:
- Core: Neutral, Happy, Sad, Surprised, Angry, Suspicious, Sleepy, Scared, Content, Startled, Grumpy, Joyful, Focused, Confused, Yawn, ContentPetting
- Special shapes: Dazed (swirl), Dizzy (star), Love (heart), Joy
- Micro-expressions: Curious, Thinking, Mischievous, Bored, Alert
- Curve/stretch: Smug, Dreamy, Skeptical, Squint, Wink
- Breathing: BreathingPrompt, Relaxed (for post-exercise calm state)

---

## Voice Assistant

Voice assistant stack in `src/assistant/`:

### Architecture
- **LLM Client** (`llm_client.h`): Supports Claude Sonnet 4 or GPT-4o, configurable via web UI
- **STT Client** (`stt_client.h`): OpenAI Whisper, 16kHz mono streaming
- **TTS Client** (`tts_client.h`): OpenAI TTS
- **Wake Word** (`wake_word.h`): ESP-SR local detection for "Hey Buddy" (no API needed)
- **Voice Input** (`voice_input.h`): Ring buffer for microphone audio (16kHz, 2-second capacity)
- **Assistant** (`assistant.h`): Main orchestrator tying STT -> LLM -> TTS together

### Device Tools (`device_tools.h`)

14 tools registered for both LLM tool use and MCP server:

| Tool | Description | Parameters |
|------|-------------|------------|
| `set_expression` | Change facial expression | expression, duration_ms |
| `set_timer` | Start countdown timer | duration_seconds, name |
| `cancel_timer` | Stop countdown timer | - |
| `start_pomodoro` | Start Pomodoro session | work_minutes, break_minutes |
| `stop_pomodoro` | Stop Pomodoro session | - |
| `get_device_info` | Device status | - |
| `play_sound` | Play audio | sound (happy/sad/alert/confirm/error) |
| `set_reminder` | Create timed reminder | hour, minute, message, recurring |
| `cancel_reminder` | Remove reminder by text | message (partial match) |
| `list_reminders` | List all reminders | - |
| `start_breathing` | Start breathing exercise | exercise (box/nadi) |
| `set_volume` | Set speaker volume | volume (0-100) |
| `set_brightness` | Set screen brightness | brightness (0-100) |
| `set_eye_color` | Change eye color | color (cyan/pink/green/orange/purple/white/red/blue) |

**Callback pattern**: `DeviceToolCallbacks` struct in `device_tools.h` with `std::function` members. Wired in `main.cpp setup()`. `executeDeviceTool()` dispatches by tool name.

### MCP Server (`mcp_server.h`)

- Runs on dedicated FreeRTOS task (separate from main loop)
- HTTP+SSE transport on port 3001
- 15s keepalive interval
- Tool executor set via `mcpServer.setToolExecutor()` lambda in main.cpp

### MCP Client (`mcp_client.h`)

- Connects to external MCP servers for tool discovery
- Up to 8 MCP servers, 16 tools per server
- 10s HTTP timeout
- Configurable via web UI Assistant tab

---

## Timer System

### Pomodoro Timer (`ui/pomodoro.h`)
```cpp
enum class PomodoroState {
    Idle, Working, ShortBreak, LongBreak, Celebration, WaitingForTap
};
```

### Countdown Timer (`ui/countdown_timer.h`)
```cpp
enum class CountdownState {
    Idle, Running, Celebration, Expired
};
```

### TimerVisuals (`ui/timer_visuals.h`)
Manages all timer expression side-effects, separated from main.cpp:
- **Pomodoro transitions**: Working->concentrate animation (Sleepy->Alert->Focused), Break->Content, Celebration->Joy, Idle->release
- **Countdown transitions**: Running->token, Celebration->Happy+joy, Expired->release, Idle->clear
- **Tick sounds**: Last 60 seconds of active timers
- **Progress bar clear animation**: 500ms deplete-to-zero when timer stops

### Progress Bar Rendering (ScreenCompositor)
- `renderPomodoroProgressBar(progress, manageWrite, progressiveCorners)`: 16px frame, 42px corners, clockwise depletion
- `renderBreathingProgressBar(progress, pulseBlend, reverse)`: Bidirectional with pulse color blending
- `advanceClearAnimation()` in TimerVisuals drives the clear-to-black animation

---

## Network Module

### NetworkSetup (`network/network_setup.h`)
Orchestrates all WiFi lifecycle concerns, extracted from main.cpp:
- `begin()`: WiFi init (disable/connect/AP mode/portal/first-boot detection)
- `update()`: WiFi state machine, NTP sync on connect, captive portal lifecycle, timezone re-sync, WiFi enable/disable toggle
- `checkAPClientConnected()`: First-boot screen transitions
- `handleChoiceTouch()`: Configure WiFi vs Use Offline selection

### WiFi Manager (`network/wifi_manager.h`)
```cpp
enum class WiFiState {
    Disabled, Unconfigured, APMode, Connecting, Connected, ConnectionFailed
};
```

**AP Mode**: SSID `DeskBuddy-Setup`, password `deskbuddy`, IP `192.168.4.1`

### Web Server (`network/web_server.h`)
Uses ESP-IDF native `esp_http_server`. Web UI HTML extracted to `web_ui_html.h` (~2,600 lines of HTML/CSS/JS as PROGMEM constant).

**All Endpoints:**
```
GET  /                      Main web UI
GET  /api/settings          All settings JSON
POST /api/settings          Update settings
GET  /api/status            Status JSON
POST /api/expression        Preview expression (index: 0-31)
POST /api/audio/test        Play test sound
POST /api/pomodoro/start    Start Pomodoro
POST /api/pomodoro/stop     Stop Pomodoro
POST /api/timer/start       Start countdown timer
POST /api/timer/stop        Stop countdown timer
GET  /api/reminders         List all reminders
POST /api/reminders         Add reminder
POST /api/reminders/delete  Delete reminder by index
POST /api/breathing/start   Start breathing exercise
GET  /api/assistant/status  Voice assistant status
POST /api/assistant/clear   Clear conversation history
GET  /api/assistant/settings  Get assistant config
POST /api/assistant/settings  Update assistant config
GET  /api/mcp/servers       List MCP servers
POST /api/mcp/servers       Add/update MCP server
POST /api/mcp/discover      Discover MCP server tools
GET  /api/wifi/scan         Scan WiFi networks
POST /api/wifi/connect      Connect to network
POST /api/wifi/forget       Clear saved credentials
POST /api/wifi/disable      Disable WiFi completely
GET  /api/time              Get current time
POST /api/time              Set time
GET  /api/system/info       Version, build date, heap, partition info
POST /api/ota/upload        Upload firmware binary
GET  /api/ota/status        OTA progress
POST /api/ota/cancel        Cancel OTA upload
POST /api/system/restart    Restart device
POST /api/system/rollback   Rollback to previous firmware
```

### Web UI Design
- Dark theme: background `#0A0A0A`, foreground `#F2F2F2`, accent `#DFFF00` (neon yellow)
- Fonts: JetBrains Mono (labels), Inter (body)
- Tabbed navigation: Dashboard, Productivity, Mindfulness, Assistant, Settings, Expressions

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

---

## Timed Reminders (`ui/reminder_manager.h`)

### Data Model
```cpp
struct Reminder {
    uint8_t hour;       // 0-23
    uint8_t minute;     // 0-59
    char message[49];   // 48 chars + null
    bool recurring;     // daily repeat
    bool enabled;       // active flag
};
```

### State Machine
```
Idle -> Showing -> (Dismiss -> Idle) or (Snooze -> Idle, re-triggers in 5 min)
```

### Features
- Max 20 reminders, persisted to NVS as JSON
- Full-screen takeover: large text rendered via TextRenderer
- Sound: plays `joy.mp3` on trigger
- Auto-snooze after 60 seconds, capped at 3 consecutive (then auto-dismiss)
- Blocked during Pomodoro, countdown timer, breathing exercise, or settings menu
- `removeByMessage(substring)` for fuzzy cancel via MCP

---

## Breathing Exercise (`behavior/breathing_exercise.h`)

### State Machine
```cpp
enum class BreathingState {
    Idle, ShowingPrompt, Confirmation, Inhale, HoldIn, Exhale, HoldOut, Complete
};
```

### Exercise Types
- **Box Breathing**: 5-5-5-5 pattern, 3 cycles = 60 seconds
- **Nadi Shodhana**: Alternate nostril, 6-phase cycle, 3 full cycles = 72 seconds

### Post-Exercise Animation
1. Complete -> Wait 1s
2. Content expression for 3s (via ReactiveAnimations)
3. Relaxed expression for 60s (blocks other behaviors)
4. Return to Neutral

---

## IMU Tilt-Based Gaze (`input/imu_handler.h`)

```cpp
Orientation getOrientation();  // Normal, FaceDown, TiltedLong
float getTiltGazeX();          // Vertical gaze from forward/backward tilt
float getTiltGazeY();          // Horizontal gaze from left/right tilt
```

**In EyeAnimator gaze calculation:**
```cpp
// Axes swapped for 90deg CCW rotation
float gravityGazeX = imu.getTiltGazeY();
float gravityGazeY = imu.getTiltGazeX();
```

**Thresholds:**
- `TILT_MAX_ANGLE = 45.0f` degrees for full gaze offset
- `FACE_DOWN_THRESHOLD = -0.7g` on Z axis
- `TILT_LONG_SECONDS = 5.0f` for uncomfortable expression

---

## Time-of-Day Mood (`behavior/time_mood.h`)

```cpp
struct MoodModifiers {
    float blinkRateMultiplier;   // 0.7-1.2
    float gazeSpeedMultiplier;   // 0.6-1.1
    float baseLidOffset;         // 0.0-0.12
};
```

| Period | Hours | Blink | Gaze | Lid Offset |
|--------|-------|-------|------|------------|
| Morning | 6-12 | 1.2x | 1.1x | 0.0 |
| Afternoon | 12-18 | 1.0x | 1.0x | 0.0 |
| Evening | 18-22 | 0.85x | 0.8x | 0.05 |
| Night | 22-6 | 0.7x | 0.6x | 0.12 |

Applied via `EyeAnimator` using mood modifiers from `IdleBehavior`.
