/**
 * @file device_tools.h
 * @brief Tool definitions for LLM and MCP device control
 *
 * Defines 14 tools available via both LLM tool use and MCP server:
 * - Expression control (set_expression)
 * - Timer management (set_timer, cancel_timer)
 * - Productivity (start_pomodoro, stop_pomodoro)
 * - Reminders (set_reminder, cancel_reminder, list_reminders)
 * - Wellness (start_breathing)
 * - Settings (set_volume, set_brightness, set_eye_color)
 * - System info (get_device_info)
 * - Audio (play_sound)
 */

#ifndef DEVICE_TOOLS_H
#define DEVICE_TOOLS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "llm_client.h"
#include "mcp_server.h"

//=============================================================================
// Tool JSON Schemas
//=============================================================================

// JSON schema for set_expression tool
static const char* SET_EXPRESSION_SCHEMA = R"({
    "type": "object",
    "properties": {
        "expression": {
            "type": "string",
            "description": "The expression to show. Valid values: neutral, happy, sad, surprised, angry, suspicious, sleepy, scared, content, focused, confused, curious, thinking, alert, listening, love, excited, relaxed"
        },
        "duration_ms": {
            "type": "integer",
            "description": "How long to show the expression in milliseconds. 0 for indefinite.",
            "default": 0
        }
    },
    "required": ["expression"]
})";

// JSON schema for set_timer tool
static const char* SET_TIMER_SCHEMA = R"({
    "type": "object",
    "properties": {
        "duration_seconds": {
            "type": "integer",
            "description": "Timer duration in seconds"
        },
        "name": {
            "type": "string",
            "description": "Optional name for the timer",
            "default": "Timer"
        }
    },
    "required": ["duration_seconds"]
})";

// JSON schema for start_pomodoro tool
static const char* START_POMODORO_SCHEMA = R"({
    "type": "object",
    "properties": {
        "work_minutes": {
            "type": "integer",
            "description": "Work duration in minutes",
            "default": 25
        },
        "break_minutes": {
            "type": "integer",
            "description": "Short break duration in minutes",
            "default": 5
        }
    }
})";

// JSON schema for get_device_info tool
static const char* GET_DEVICE_INFO_SCHEMA = R"({
    "type": "object",
    "properties": {}
})";

// JSON schema for play_sound tool
static const char* PLAY_SOUND_SCHEMA = R"({
    "type": "object",
    "properties": {
        "sound": {
            "type": "string",
            "description": "Sound to play: happy, sad, alert, confirm, error"
        }
    },
    "required": ["sound"]
})";

// JSON schema for set_reminder tool
static const char* SET_REMINDER_SCHEMA = R"({
    "type": "object",
    "properties": {
        "hour": {
            "type": "integer",
            "description": "Hour (0-23) to trigger the reminder"
        },
        "minute": {
            "type": "integer",
            "description": "Minute (0-59) to trigger the reminder"
        },
        "message": {
            "type": "string",
            "description": "Reminder message (max 48 chars), shown on screen in large text"
        },
        "recurring": {
            "type": "boolean",
            "description": "If true, reminder repeats daily. Default false (one-shot).",
            "default": false
        }
    },
    "required": ["hour", "minute", "message"]
})";

// JSON schema for cancel_reminder tool
static const char* CANCEL_REMINDER_SCHEMA = R"({
    "type": "object",
    "properties": {
        "message": {
            "type": "string",
            "description": "Partial text match to find and remove the reminder"
        }
    },
    "required": ["message"]
})";

// JSON schema for set_volume tool
static const char* SET_VOLUME_SCHEMA = R"json({
    "type": "object",
    "properties": {
        "volume": {
            "type": "integer",
            "description": "Volume level (0-100)",
            "minimum": 0,
            "maximum": 100
        }
    },
    "required": ["volume"]
})json";

// JSON schema for set_brightness tool
static const char* SET_BRIGHTNESS_SCHEMA = R"json({
    "type": "object",
    "properties": {
        "brightness": {
            "type": "integer",
            "description": "Screen brightness (0-100)",
            "minimum": 0,
            "maximum": 100
        }
    },
    "required": ["brightness"]
})json";

// JSON schema for set_eye_color tool
static const char* SET_EYE_COLOR_SCHEMA = R"({
    "type": "object",
    "properties": {
        "color": {
            "type": "string",
            "description": "Eye color name: cyan, pink, green, orange, purple, white, red, blue"
        }
    },
    "required": ["color"]
})";

//=============================================================================
// Tool Registration
//=============================================================================

/**
 * @brief Register all device control tools with the LLM client
 * @param llm The LLM client to register tools with
 */
inline void registerDeviceTools(LLMClient& llm) {
    // Set Expression - Control the robot's facial expression
    llm.addTool(
        "set_expression",
        "Change the robot's facial expression. Use this to show emotions "
        "that match your response, or to react to what the user says. "
        "For example, show 'happy' when giving good news, 'thinking' when "
        "processing a complex question, or 'curious' when asking questions.",
        SET_EXPRESSION_SCHEMA
    );

    // Set Timer - Create a countdown timer
    llm.addTool(
        "set_timer",
        "Set a countdown timer. The robot will display the countdown and "
        "alert the user when time is up. Useful for reminders, cooking timers, "
        "or any timed activity.",
        SET_TIMER_SCHEMA
    );

    // Cancel Timer
    llm.addTool(
        "cancel_timer",
        "Cancel the currently running countdown timer.",
        GET_DEVICE_INFO_SCHEMA
    );

    // Start Pomodoro - Begin a productivity session
    llm.addTool(
        "start_pomodoro",
        "Start a Pomodoro productivity timer. This begins a work session "
        "followed by a short break. The robot will show focused expression "
        "during work and relaxed expression during breaks.",
        START_POMODORO_SCHEMA
    );

    // Stop Pomodoro - Cancel the current pomodoro session
    llm.addTool(
        "stop_pomodoro",
        "Stop the current Pomodoro session. Use when the user wants to "
        "cancel their productivity timer.",
        GET_DEVICE_INFO_SCHEMA  // Empty schema, no parameters
    );

    // Get Device Info - Query device status
    llm.addTool(
        "get_device_info",
        "Get information about the device's current state including "
        "battery level, WiFi status, current expression, and active timers.",
        GET_DEVICE_INFO_SCHEMA
    );

    // Play Sound - Play an audio feedback sound
    llm.addTool(
        "play_sound",
        "Play a sound effect. Use for audio feedback like confirmations, "
        "alerts, or emotional expressions.",
        PLAY_SOUND_SCHEMA
    );

    // Set Reminder - Create a timed reminder
    llm.addTool(
        "set_reminder",
        "Set a timed reminder. The robot will show the message on screen "
        "and play an alert sound at the specified time. Message max 48 characters. "
        "Use recurring=true for daily reminders.",
        SET_REMINDER_SCHEMA
    );

    // Cancel Reminder - Remove a reminder by message text
    llm.addTool(
        "cancel_reminder",
        "Cancel a reminder by matching part of its message text.",
        CANCEL_REMINDER_SCHEMA
    );

    // List Reminders - Show all active reminders
    llm.addTool(
        "list_reminders",
        "List all active reminders with their times and messages.",
        GET_DEVICE_INFO_SCHEMA
    );

    // Start Breathing - Begin a guided breathing exercise
    llm.addTool(
        "start_breathing",
        "Start a guided box breathing exercise (5s inhale, 5s hold, 5s exhale, "
        "5s hold, 3 cycles = 60 seconds). Use when the user seems stressed or "
        "asks to relax.",
        GET_DEVICE_INFO_SCHEMA
    );

    // Set Volume
    llm.addTool(
        "set_volume",
        "Set the device speaker volume (0-100).",
        SET_VOLUME_SCHEMA
    );

    // Set Brightness
    llm.addTool(
        "set_brightness",
        "Set the screen brightness (0-100).",
        SET_BRIGHTNESS_SCHEMA
    );

    // Set Eye Color
    llm.addTool(
        "set_eye_color",
        "Change the eye color. Available colors: cyan, pink, green, orange, "
        "purple, white, red, blue.",
        SET_EYE_COLOR_SCHEMA
    );
}

/**
 * @brief Register all device control tools with the MCP server
 * @param mcp The MCP server to register tools with
 */
inline void registerMcpDeviceTools(MCPServer& mcp) {
    mcp.addTool("set_expression",
        "Change the robot's facial expression. Valid expressions: neutral, happy, sad, "
        "surprised, angry, suspicious, sleepy, scared, content, focused, confused, "
        "curious, thinking, alert, listening, love, excited, relaxed",
        SET_EXPRESSION_SCHEMA);

    mcp.addTool("set_timer",
        "Set a countdown timer. The robot will display the countdown on screen with a "
        "progress bar, tick in the last 60 seconds, and celebrate with a happy animation when done.",
        SET_TIMER_SCHEMA);

    mcp.addTool("cancel_timer",
        "Cancel the currently running countdown timer.",
        GET_DEVICE_INFO_SCHEMA);

    mcp.addTool("start_pomodoro",
        "Start a Pomodoro productivity timer with work and break sessions.",
        START_POMODORO_SCHEMA);

    mcp.addTool("stop_pomodoro",
        "Stop the current Pomodoro session.",
        GET_DEVICE_INFO_SCHEMA);

    mcp.addTool("get_device_info",
        "Get device status: current expression, WiFi, active timers, uptime.",
        GET_DEVICE_INFO_SCHEMA);

    mcp.addTool("play_sound",
        "Play a sound effect: happy, sad, alert, confirm, error.",
        PLAY_SOUND_SCHEMA);

    mcp.addTool("set_reminder",
        "Set a timed reminder. Shows message on screen with alert sound at the specified time. "
        "Message max 48 characters. Set recurring=true for daily reminders.",
        SET_REMINDER_SCHEMA);

    mcp.addTool("cancel_reminder",
        "Cancel a reminder by matching part of its message text.",
        CANCEL_REMINDER_SCHEMA);

    mcp.addTool("list_reminders",
        "List all active reminders with their times and messages.",
        GET_DEVICE_INFO_SCHEMA);

    mcp.addTool("start_breathing",
        "Start a guided box breathing exercise (5s inhale, 5s hold, 5s exhale, 5s hold, 3 cycles).",
        GET_DEVICE_INFO_SCHEMA);

    mcp.addTool("set_volume",
        "Set the device speaker volume (0-100).",
        SET_VOLUME_SCHEMA);

    mcp.addTool("set_brightness",
        "Set the screen brightness (0-100).",
        SET_BRIGHTNESS_SCHEMA);

    mcp.addTool("set_eye_color",
        "Change the eye color: cyan, pink, green, orange, purple, white, red, blue.",
        SET_EYE_COLOR_SCHEMA);
}

//=============================================================================
// Tool Execution Callbacks
//=============================================================================

/**
 * @struct DeviceToolCallbacks
 * @brief Callbacks for tool execution
 *
 * Set these callbacks to connect tools to actual device functionality.
 */
struct DeviceToolCallbacks {
    std::function<void(const char* expression, int durationMs)> onSetExpression;
    std::function<void(int seconds, const char* name)> onSetTimer;
    std::function<void()> onCancelTimer;
    std::function<void(int workMin, int breakMin)> onStartPomodoro;
    std::function<void()> onStopPomodoro;
    std::function<String()> onGetDeviceInfo;
    std::function<void(const char* sound)> onPlaySound;
    std::function<bool(int hour, int minute, const char* message, bool recurring)> onSetReminder;
    std::function<bool(const char* message)> onCancelReminder;
    std::function<String()> onListReminders;
    std::function<void()> onStartBreathing;
    std::function<void(int)> onSetVolume;
    std::function<void(int)> onSetBrightness;
    std::function<bool(const char* color)> onSetEyeColor;
};

// Global callbacks instance
extern DeviceToolCallbacks deviceToolCallbacks;

/**
 * @brief Execute a tool call from Claude
 * @param toolName Name of the tool to execute
 * @param input JSON string with tool parameters
 * @return JSON string with tool result
 */
inline String executeDeviceTool(const char* toolName, const char* input) {
    JsonDocument doc;
    JsonDocument result;

    DeserializationError error = deserializeJson(doc, input);
    if (error) {
        result["error"] = "Invalid JSON input";
        String output;
        serializeJson(result, output);
        return output;
    }

    // Set Expression
    if (strcmp(toolName, "set_expression") == 0) {
        const char* expression = doc["expression"] | "neutral";
        int durationMs = doc["duration_ms"] | 0;

        if (deviceToolCallbacks.onSetExpression) {
            deviceToolCallbacks.onSetExpression(expression, durationMs);
            result["success"] = true;
            result["expression"] = expression;
        } else {
            result["error"] = "Expression control not available";
        }
    }
    // Set Timer
    else if (strcmp(toolName, "set_timer") == 0) {
        int seconds = doc["duration_seconds"] | 60;
        const char* name = doc["name"] | "Timer";

        if (deviceToolCallbacks.onSetTimer) {
            deviceToolCallbacks.onSetTimer(seconds, name);
            result["success"] = true;
            result["timer_name"] = name;
            result["duration_seconds"] = seconds;
        } else {
            result["error"] = "Timer not available";
        }
    }
    // Cancel Timer
    else if (strcmp(toolName, "cancel_timer") == 0) {
        if (deviceToolCallbacks.onCancelTimer) {
            deviceToolCallbacks.onCancelTimer();
            result["success"] = true;
        } else {
            result["error"] = "Timer not available";
        }
    }
    // Start Pomodoro
    else if (strcmp(toolName, "start_pomodoro") == 0) {
        int workMin = doc["work_minutes"] | 25;
        int breakMin = doc["break_minutes"] | 5;

        if (deviceToolCallbacks.onStartPomodoro) {
            deviceToolCallbacks.onStartPomodoro(workMin, breakMin);
            result["success"] = true;
            result["work_minutes"] = workMin;
            result["break_minutes"] = breakMin;
        } else {
            result["error"] = "Pomodoro not available";
        }
    }
    // Stop Pomodoro
    else if (strcmp(toolName, "stop_pomodoro") == 0) {
        if (deviceToolCallbacks.onStopPomodoro) {
            deviceToolCallbacks.onStopPomodoro();
            result["success"] = true;
        } else {
            result["error"] = "Pomodoro not available";
        }
    }
    // Get Device Info
    else if (strcmp(toolName, "get_device_info") == 0) {
        if (deviceToolCallbacks.onGetDeviceInfo) {
            String info = deviceToolCallbacks.onGetDeviceInfo();
            // Parse the returned JSON string
            JsonDocument infoDoc;
            deserializeJson(infoDoc, info);
            result["device_info"] = infoDoc;
            result["success"] = true;
        } else {
            result["error"] = "Device info not available";
        }
    }
    // Play Sound
    else if (strcmp(toolName, "play_sound") == 0) {
        const char* sound = doc["sound"] | "confirm";

        if (deviceToolCallbacks.onPlaySound) {
            deviceToolCallbacks.onPlaySound(sound);
            result["success"] = true;
            result["sound"] = sound;
        } else {
            result["error"] = "Sound playback not available";
        }
    }
    // Set Reminder
    else if (strcmp(toolName, "set_reminder") == 0) {
        int hour = doc["hour"] | 0;
        int minute = doc["minute"] | 0;
        const char* message = doc["message"] | "";
        bool recurring = doc["recurring"] | false;

        if (deviceToolCallbacks.onSetReminder) {
            bool ok = deviceToolCallbacks.onSetReminder(hour, minute, message, recurring);
            if (ok) {
                result["success"] = true;
                result["hour"] = hour;
                result["minute"] = minute;
                result["message"] = message;
                result["recurring"] = recurring;
            } else {
                result["error"] = "Failed to add reminder (max 20 reached or invalid)";
            }
        } else {
            result["error"] = "Reminders not available";
        }
    }
    // Cancel Reminder
    else if (strcmp(toolName, "cancel_reminder") == 0) {
        const char* message = doc["message"] | "";

        if (deviceToolCallbacks.onCancelReminder) {
            bool ok = deviceToolCallbacks.onCancelReminder(message);
            if (ok) {
                result["success"] = true;
            } else {
                result["error"] = "No matching reminder found";
            }
        } else {
            result["error"] = "Reminders not available";
        }
    }
    // List Reminders
    else if (strcmp(toolName, "list_reminders") == 0) {
        if (deviceToolCallbacks.onListReminders) {
            String info = deviceToolCallbacks.onListReminders();
            JsonDocument infoDoc;
            deserializeJson(infoDoc, info);
            result["reminders"] = infoDoc;
            result["success"] = true;
        } else {
            result["error"] = "Reminders not available";
        }
    }
    // Start Breathing
    else if (strcmp(toolName, "start_breathing") == 0) {
        if (deviceToolCallbacks.onStartBreathing) {
            deviceToolCallbacks.onStartBreathing();
            result["success"] = true;
            result["exercise"] = "box_breathing";
            result["duration_seconds"] = 60;
        } else {
            result["error"] = "Breathing exercise not available";
        }
    }
    // Set Volume
    else if (strcmp(toolName, "set_volume") == 0) {
        int volume = doc["volume"] | 50;
        volume = constrain(volume, 0, 100);

        if (deviceToolCallbacks.onSetVolume) {
            deviceToolCallbacks.onSetVolume(volume);
            result["success"] = true;
            result["volume"] = volume;
        } else {
            result["error"] = "Volume control not available";
        }
    }
    // Set Brightness
    else if (strcmp(toolName, "set_brightness") == 0) {
        int brightness = doc["brightness"] | 50;
        brightness = constrain(brightness, 0, 100);

        if (deviceToolCallbacks.onSetBrightness) {
            deviceToolCallbacks.onSetBrightness(brightness);
            result["success"] = true;
            result["brightness"] = brightness;
        } else {
            result["error"] = "Brightness control not available";
        }
    }
    // Set Eye Color
    else if (strcmp(toolName, "set_eye_color") == 0) {
        const char* color = doc["color"] | "cyan";

        if (deviceToolCallbacks.onSetEyeColor) {
            bool ok = deviceToolCallbacks.onSetEyeColor(color);
            if (ok) {
                result["success"] = true;
                result["color"] = color;
            } else {
                result["error"] = "Unknown color. Use: cyan, pink, green, orange, purple, white, red, blue";
            }
        } else {
            result["error"] = "Eye color control not available";
        }
    }
    // Unknown tool
    else {
        result["error"] = "Unknown tool";
        result["tool_name"] = toolName;
    }

    String output;
    serializeJson(result, output);
    return output;
}

#endif // DEVICE_TOOLS_H
