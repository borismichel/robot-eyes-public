# Robo Eyes

Expressive robot eyes for ESP32-S3 with AMOLED display, inspired by Anki's Cozmo robot.

## Overview

This project implements a parametric eye animation system that creates lifelike, expressive robot eyes. The eyes respond to touch, motion (IMU), and can express a wide range of emotions through smooth animations.

**Target Hardware:** [Waveshare ESP32-S3-Touch-AMOLED-1.8](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm)

## Features

### Expressions
- **30 emotion presets** with smooth transitions between any states
- **Core expressions**: Neutral, Happy, Sad, Surprised, Angry, Suspicious, Sleepy, Scared, Content, Startled, Grumpy, Joyful, Focused, Confused, Yawn, ContentPetting
- **Special shapes**: Dazed (swirl), Dizzy (star), Love (heart), Joy (curved crescents)
- **Micro-expressions**: Curious, Thinking, Mischievous, Bored, Alert
- **Curve/stretch expressions**: Smug, Dreamy, Skeptical, Squint, Wink
- **Asymmetric expressions** for added personality (Suspicious, Confused, Wink, Mischievous)
- **Variable transition timing**: Fast reactions for surprise/startle (0.08s), slow transitions for sleepy/dreamy (0.35s)

### Behaviors
- **Idle animations**: Random gaze scanning, micro-movements, natural blinking (6-10 blinks/min)
- **Touch response**: Tap to cycle expressions, hold 2+ seconds to "pet" the robot
- **Motion response**: Scared when picked up, Confused when shaken
- **Gravity-aware gaze**: Eyes drift toward gravity direction when device is tilted
- **Orientation expressions**: Face-down triggers hiding/shy, prolonged tilt (>5s) triggers uncomfortable squint
- **Sound response**: Shows irritated expression when environment gets too loud
- **Time-of-day mood**: Personality shifts based on internal clock (energetic mornings, relaxed evenings, sleepy nights)
- **Sleep cycle**: Drowsy → yawn → sleep with breathing animation after 30 minutes of inactivity
- **Pomodoro timer**: Focus timer with work/break cycles and visual progress bar around screen
- **Audio feedback**: MP3 sounds for happy (when petted), confused (when shaken), yawn, and timer ticks
- **Settings menu**: 2-finger tap opens swipeable settings (pomodoro, volume, brightness, mic, color, time)

### Technical Features
- 30fps rendering using software per-pixel evaluation
- Parametric shape system with 17 adjustable parameters per eye
- Smooth interpolation with expression-specific timing (0.08s-0.35s)
- Simple amplitude-based sound detection
- Special shape rendering: stars, hearts, spirals
- Dual-core processing (display on Core 1, audio on Core 0)
- RGB565 framebuffer rendering (336×416 buffer, positioned inside 16px progress bar margins)
- **PSRAM optimizations**:
  - Dirty-rect clearing: Only clears previous eye bounding boxes (~24KB vs 280KB full buffer)
  - Partial screen blit: Only pushes changed regions to display
  - Shape-aware bounds: Computes correct dirty rects for stars, hearts, swirls, circles
  - Full blit on state transitions (menu close, sleep wake)
- **Pomodoro progress bar**: 16px thick frame with 42px corner radius, depletes clockwise

## Hardware Requirements

- Waveshare ESP32-S3-Touch-AMOLED-1.8 development board
  - 368x448 AMOLED display
  - Capacitive touch
  - QMI8658 6-axis IMU
  - ES8311 audio codec with speaker
  - 16MB Flash, 8MB PSRAM

## Project Structure

```
robot-eyes/
├── src/
│   ├── main.cpp              # Main application loop
│   ├── eyes/
│   │   ├── eye_shape.h       # Parametric eye shape definition
│   │   ├── eye_renderer.h    # Software renderer interface
│   │   └── eye_renderer.cpp  # Per-pixel rendering implementation
│   ├── behavior/
│   │   ├── expressions.h     # Expression presets (30 emotions)
│   │   ├── idle_behavior.*   # Gaze scanning, blinking, yawn
│   │   ├── sleep_behavior.*  # Drowsy/sleep state machine
│   │   └── time_mood.h       # Time-of-day mood modifiers
│   ├── input/
│   │   ├── imu_handler.*     # Motion/tilt detection, gravity gaze
│   │   └── audio_handler.*   # Microphone level detection
│   ├── audio/
│   │   └── audio_player.*    # MP3 playback via ES8311
│   ├── ui/
│   │   ├── settings_menu.*   # Swipeable settings with sub-menus
│   │   └── pomodoro.*        # Pomodoro timer state machine
│   └── animation/
│       └── tweener.*         # Smooth value transitions
├── data/                     # LittleFS audio files
│   ├── happy.mp3
│   ├── confused.mp3
│   ├── yawn.mp3
│   └── tick.mp3              # Pomodoro countdown tick
├── lib/                      # Waveshare GFX library
├── include/
│   └── lv_conf.h            # LVGL configuration
└── platformio.ini           # Build configuration
```

## Building and Flashing

### Prerequisites
- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- USB-C cable

### Build Commands

```bash
# Build firmware
pio run

# Upload firmware
pio run -t upload

# Upload audio files (required for sounds)
pio run -t uploadfs

# Monitor serial output
pio device monitor
```

## Usage

### Touch Gestures
| Gesture | Action |
|---------|--------|
| Tap | Cycle through expressions |
| Hold 2+ seconds | Enter "petting" mode (happy expression + sound) |
| Release | Return to previous expression |
| 2-finger tap | Open settings menu (swipe up/down to navigate pages) |

### Motion Triggers
| Motion | Response |
|--------|----------|
| Pick up | Scared expression |
| Shake | Confused expression + sound |
| Tilt device | Eyes drift toward gravity direction |
| Face-down | Hiding/shy expression (heavy lids) |
| Tilted >45° for 5s | Uncomfortable squint expression |

### Sound Response
| Trigger | Response |
|---------|----------|
| Too loud | Grumpy/irritated expression for 3 seconds |

### Sleep Cycle
After 30 minutes of no interaction:
1. Yawn expression with sound
2. Transition to drowsy (heavy lids)
3. Fall asleep (breathing bar animation)

Wake triggers: Touch, shake, or pick up

### Settings Menu

Open with 2-finger tap. Swipe up/down to navigate between main menu pages:

**Main Menu:**
| Page | Entry | Action |
|------|-------|--------|
| 1 | Pomodoro | Tap to open pomodoro sub-menu |
| 2 | Settings | Tap to open settings sub-menu |
| 3 | Exit | Tap to close menu |

**Pomodoro Sub-Menu:**
| Page | Setting | Control |
|------|---------|---------|
| 1 | Start/Stop | Tap to start work session or stop timer |
| 2 | Work Duration | Horizontal slider (1-60 min, default 25) |
| 3 | Short Break | Horizontal slider (1-30 min, default 5) |
| 4 | Long Break | Horizontal slider (1-60 min, default 15) |
| 5 | Sessions | Horizontal slider (1-8, default 4 before long break) |
| 6 | Ticking | Tap to toggle tick sound in last 60 seconds |
| 7 | Back | Tap to return to main menu |

**Settings Sub-Menu:**
| Page | Setting | Control |
|------|---------|---------|
| 1 | Volume | Horizontal slider (0-100%) |
| 2 | Brightness | Horizontal slider (0-100%) |
| 3 | Mic Gain | Horizontal slider with 0dB center marker |
| 4 | Mic Threshold | Horizontal slider with live level meter |
| 5 | Eye Color | Swipe left/right to cycle 8 color presets |
| 6 | Time | Tap to set hours/minutes for internal clock |
| 7 | Time Format | Tap to toggle 12H/24H format |
| 8 | Back | Tap to return to main menu |

All settings are persisted to flash memory.

### Pomodoro Timer

Focus timer using the classic Pomodoro Technique:

**Work Cycle:**
1. Work session (default 25 minutes) with focused expression
2. Short break (default 5 minutes) with relaxed expression
3. Repeat for 4 work sessions
4. Long break (default 15 minutes) after completing 4 sessions

**Visual Feedback:**
- Large MM:SS countdown display in center of screen
- 16px progress bar frame around entire screen edge (depletes clockwise)
- Progress bar corners deplete progressively (no flashing)
- Label above timer shows current phase (WORK, SHORT BRK, LONG BRK)
- Celebration expression + animation between phases

**Ticking:**
- Optional tick sound plays in the last 60 seconds of each session
- Can be toggled on/off in settings

**Controls:**
- Tap during countdown: No effect (prevents accidental stops)
- Long-hold or open menu: Exit pomodoro mode
- Tap when "WAITING": Start next phase

### Time-of-Day Mood

The internal clock (set via Settings > Time) influences the robot's personality:

| Period | Hours | Mood | Effects |
|--------|-------|------|---------|
| Morning | 6am-12pm | Energetic | Faster blinks (1.2x), faster gaze (1.1x), wide-awake eyes |
| Afternoon | 12pm-6pm | Balanced | Normal baseline behavior |
| Evening | 6pm-10pm | Relaxed | Slower blinks (0.85x), slower gaze (0.8x), slightly heavier lids |
| Night | 10pm-6am | Sleepy | Slow blinks (0.7x), very slow gaze (0.6x), heavy droopy lids |

The mood smoothly affects:
- Blink frequency (morning blinks more often, night less)
- Gaze movement speed (evening/night moves slower)
- Base eyelid position (night has heavier "droopy" lids)

## Eye Shape Parameters

The `EyeShape` structure controls all visual aspects:

| Parameter | Range | Description |
|-----------|-------|-------------|
| `width` | 0.5-1.5 | Eye width multiplier |
| `height` | 0.5-1.5 | Eye height multiplier |
| `cornerRadius` | 0.0-2.0 | Corner roundness |
| `offsetX/Y` | -1.0-1.0 | Gaze direction |
| `topLid/bottomLid` | 0.0-1.0 | Eyelid closure |
| `innerCornerY` | -1.0-1.0 | Inner corner vertical offset |
| `outerCornerY` | -1.0-1.0 | Outer corner vertical offset |
| `openness` | 0.0-1.0 | Overall eye openness (blink) |
| `topPinch/bottomPinch` | 0.0-1.0 | Edge pinch for pointed shapes |
| `topCurve/bottomCurve` | 0.0-1.0 | Edge curve for crescents |
| `stretch` | 0.5-1.5 | Horizontal stretch multiplier |
| `squash` | 0.5-1.5 | Vertical squash multiplier |

## Adding New Expressions

1. Add enum value to `Expression` in `expressions.h`
2. Create preset function in `ExpressionPresets` namespace
3. Add case to `getExpressionShape()` switch
4. Add name to `getExpressionName()` switch

Example:
```cpp
inline EyeShape myExpression() {
    EyeShape s;
    s.height = 0.8f;
    s.outerCornerY = 0.2f;  // Raised outer corners
    return s;
}
```

## Coordinate System Note

The display is physically rotated 90° counter-clockwise (CCW):
- Physical screen: 368×448 pixels (portrait hardware)
- Buffer: 336×416 pixels (inside 16px progress bar margins)
- Buffer positioned at screen (16, 16)
- Buffer X (0-336) → Screen vertical (up/down on screen)
- Buffer Y (0-416) → Screen horizontal (left/right on screen)
- "Top lid" in code → appears at top of screen
- Eye "width" in buffer → vertical extent on screen
- Eyes side-by-side horizontally = different buffer Y positions

## Dependencies

Managed via PlatformIO:
- `lvgl/lvgl@^8.4.0` - Graphics library (for display driver)
- `earlephilhower/ESP8266Audio@^1.9.7` - MP3 decoding

Local libraries (in `lib/`):
- GFX Library for Arduino (Waveshare customized)
- ES8311 codec driver

## Configuration

Key settings in `platformio.ini`:
- Display: 448x368 pixels
- Flash: 16MB with LittleFS partition
- PSRAM: Enabled for framebuffer allocation

Behavioral timing in source files:
- Blink interval: 6-10 seconds
- Gaze shift: 1.5-3 seconds
- Sleep timeout: 30 minutes

## License

This project is provided for educational and personal use.

## Acknowledgments

- Inspired by Anki's Cozmo robot eye animations
- Built for Waveshare ESP32-S3-Touch-AMOLED-1.8 hardware
- Uses ESP8266Audio library for MP3 playback
