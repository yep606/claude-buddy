#pragma once
#include <Arduino.h>

// LilyGO T-Display-S3 pins
constexpr uint8_t PIN_POWER = 15;      // LCD/peripheral power, must be HIGH
constexpr uint8_t PIN_BACKLIGHT = 38;
constexpr uint8_t PIN_BOOT = 0;        // both buttons read LOW when pressed
constexpr uint8_t PIN_KEY = 14;

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Palette (same as the mockups)
constexpr uint16_t C_BG = rgb(0x14, 0x13, 0x12);
constexpr uint16_t C_BAR = rgb(0x21, 0x1F, 0x1D);
constexpr uint16_t C_BODY = rgb(0xD9, 0x77, 0x57);    // Claude orange
constexpr uint16_t C_DARK = rgb(0x7A, 0x43, 0x30);
constexpr uint16_t C_EYE = rgb(0x0E, 0x0D, 0x0C);
constexpr uint16_t C_TX = rgb(0xEC, 0xEA, 0xE4);
constexpr uint16_t C_DIM = rgb(0x8C, 0x88, 0x7F);
constexpr uint16_t C_FAINT = rgb(0x35, 0x32, 0x2E);
constexpr uint16_t C_MID = rgb(0x5E, 0x5A, 0x54);
constexpr uint16_t C_OK = rgb(0x5F, 0xA8, 0x66);
constexpr uint16_t C_BAD = rgb(0xD9, 0x53, 0x4B);
constexpr uint16_t C_BADDARK = rgb(0x6E, 0x2A, 0x26);
constexpr uint16_t C_WARN = rgb(0xE9, 0xB0, 0x4A);
constexpr uint16_t C_BUB = rgb(0xEF, 0xED, 0xE6);
constexpr uint16_t C_BLUSH = rgb(0xF2, 0xA0, 0x8C);
constexpr uint16_t C_DROP = rgb(0x8E, 0xC5, 0xFF);
constexpr uint16_t C_RISKYTX = rgb(0xF2, 0xB8, 0xB2);
constexpr uint16_t C_NIGHT = rgb(0x0B, 0x0A, 0x0A);
constexpr uint16_t C_NIGHTTX = rgb(0x4A, 0x47, 0x43);
constexpr uint16_t C_SLEEPBODY = rgb(0x6A, 0x3F, 0x31);

// Timing
constexpr uint32_t FRAME_MS = 33;               // ~30 fps
constexpr uint32_t HOLD_MS = 1000;              // hold to allow a risky command / save a rule
constexpr uint32_t SCROLL_HOLD_MS = 400;        // bottom held longer than this scrolls
constexpr uint32_t PROMPT_GUARD_MS = 300;       // ignore presses that began before a prompt appeared
constexpr uint32_t LINK_TIMEOUT_MS = 7000;      // no message from the PC for this long = offline
constexpr uint32_t DONE_SHOW_MS = 8000;         // celebrate, then show "Your turn"
constexpr uint32_t IDLE_SLEEP_MS = 10UL * 60 * 1000;
constexpr uint32_t STATS_SHOW_MS = 20000;
constexpr uint32_t CALIB_FLIP_MS = 2000;
constexpr uint32_t CALIB_ENTER_MS = 3000;

// Backlight levels (analogWrite on GPIO 38)
constexpr uint8_t BL_FULL = 255;
constexpr uint8_t BL_NORMAL = 170;
constexpr uint8_t BL_DIM = 25;
