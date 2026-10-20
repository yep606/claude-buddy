#pragma once
#include <TFT_eSPI.h>
#include <Preferences.h>
#include "config.h"

TFT_eSPI tft;
TFT_eSprite spr(&tft);
Preferences prefs;

enum Arm : uint8_t { A_SIDE, A_DOWN, A_MID, A_UP };
enum Legs : uint8_t { L_STAND, L_W1, L_W2, L_TUCK };
enum Eyes : uint8_t { E_OPEN, E_BLINK, E_CLOSED, E_WIDE, E_HAPPY, E_X };

struct Pose {
  uint8_t aL = A_SIDE, aR = A_SIDE, legs = L_STAND, eyes = E_OPEN;
  int8_t dy = 0;   // whole body offset (jumping), in character pixels
  int8_t by = 0;   // body offset over the legs (breathing, squatting)
  int8_t ex = 0, ey = 0;  // where the eyes look
  uint16_t col = C_BODY;
  bool blush = false;
};

// Where a character was drawn, so props (bubbles, sparkles) line up with it
struct GuyFrame {
  int ox, oy, s, dy;
};

enum FontId : uint8_t { FT_SMALL, FT_TITLE, FT_BIG, FT_CLOCK };

enum Screen : uint8_t {
  S_CALIB, S_OFFLINE, S_IDLE, S_THINK, S_WORK, S_PERM, S_ASK, S_DONE, S_ERR, S_STATS, S_SLEEP
};
enum View : uint8_t { V_MAIN, V_STATS, V_SLEEP };
enum PermItem : uint8_t { PI_ALLOW, PI_DENY, PI_ALWAYS };

constexpr int MAX_LINES = 24;
constexpr int MAX_OPTS = 8;

struct Status {
  String st = "idle";   // idle | think | work | done | err
  String proj, tool, target, step1, step2, model, msg;
  int wait = 0, tdone = 0, ttotal = 0, ctx = -1;
  long el = 0, dur = 0, tok = 0;
  uint32_t rxAt = 0;    // when this status arrived
  uint32_t stAt = 0;    // when `st` last changed
};

struct Prompt {
  bool active = false;
  bool ask = false;     // false = permission, true = question
  String id, proj, title, desc, body, always, q;
  String opts[MAX_OPTS];
  int nopts = 0;
  bool risky = false, multi = false;
  int n = 1, of = 1, tmo = 30, wait = 0;
  uint32_t at = 0;
  int sel = 0;
  uint16_t checked = 0; // multi-select ticks
  float scroll = 0;     // in lines; grows by itself, faster while the bottom button is held
  String lines[MAX_LINES];  // command or question, wrapped to the screen
  int nlines = 0;
};

struct Stats {
  bool have = false;
  long tot = 0, out = 0, inp = 0, cache = 0;
  long h[12] = {0};     // tokens per hour, oldest first
};

struct Button {
  uint8_t pin;
  bool raw = false, down = false, handled = false;
  uint32_t changedAt = 0, t0 = 0;
  explicit Button(uint8_t p) : pin(p) {}
};

struct Demo {
  bool active = false;
  int step = 0;
  uint32_t stepAt = 0;
};

Status status;
Prompt prompt;
Stats stats;
Demo demo;
String clockHM;               // "23:41", sent by the PC
View view = V_MAIN;
uint32_t viewAt = 0;
bool linkUp = false;
uint32_t lastRx = 0;
bool errDismissed = false;

bool calibrating = false;
bool calibFlipped = false;
uint32_t calibBothAt = 0;
uint8_t rotation = 1;

Button btnBoot(PIN_BOOT), btnKey(PIN_KEY);
Button* btnTop = &btnBoot;
Button* btnBot = &btnKey;

uint32_t warnUntil = 0;       // "hold the button" reminder
String ackText;               // short confirmation after an answer
uint16_t ackColor = 0;
uint32_t ackUntil = 0;

Screen curScreen = S_OFFLINE;
uint32_t screenAt = 0;
uint8_t backlight = 0;

// Milliseconds since t. Safe when t was taken a moment *after* an older "now"
// (plain unsigned subtraction would wrap to ~49 days), and across millis() rollover.
uint32_t since(uint32_t t) {
  int32_t d = (int32_t)(millis() - t);
  return d < 0 ? 0 : (uint32_t)d;
}
