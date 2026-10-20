#pragma once
// One function per screen. Layouts match the approved mockups (320 x 170).
#include "logic.h"

void drawIdle(uint32_t t) {
  topBar(withClock(status.proj));
  floorLine(14, 140);
  uint32_t c = t % 6000;
  Pose p;
  if ((c > 2000 && c < 2140) || (c > 5200 && c < 5340)) p.eyes = E_BLINK;
  if (c > 3000 && c < 3800) p.ex = -1;
  else if (c > 3800 && c < 4600) p.ex = 1;
  drawGuy(30, 80, 3, p);
  text("Your turn", 172, 50, FT_TITLE, C_TX);
  text("Waiting for you", 172, 68, FT_SMALL, C_DIM);
  if (status.dur > 0) row("Last task", fmtDur(status.dur), 96);
  if (status.tok > 0) row("Session", fmtTok(status.tok) + " tokens", 113);
  if (status.ctx >= 0) {
    text("Context", 172, 130, FT_SMALL, C_DIM);
    bar(232, 123, 82, 6, status.ctx / 100.0f, C_BODY);
  }
  mainTabs("Stats", "Sleep");
}

void drawThink(uint32_t t) {
  topBar(status.proj + "   Thinking");
  floorLine(14, 140);
  Pose p;
  p.ex = 1;
  p.ey = -1;
  p.by = (t / 600) % 2;
  GuyFrame f = drawGuy(30, 80, 3, p);
  thought(f, 22, -16, t);
  text("Thinking...", 172, 50, FT_TITLE, C_TX);
  row("Elapsed", mmss(elapsed()), 80);
  row("Tokens", fmtTok(status.tok), 97);
  if (status.model.length()) row("Model", fit(status.model, 90, FT_SMALL), 114);
  mainTabs("Stats", nullptr);
}

void drawWork(uint32_t t) {
  topBar(status.proj + "   Working");
  floorLine(14, 140);
  float ph = (t % 4200) / 4200.0f, tri = ph < 0.5f ? ph * 2 : 2 - ph * 2;
  bool st = (t / 170) % 2;
  Pose p;
  p.legs = st ? L_W1 : L_W2;
  p.aL = st ? A_MID : A_SIDE;
  p.aR = st ? A_SIDE : A_MID;
  p.ex = ph < 0.5f ? 1 : -1;
  drawGuy(16 + 3 * lroundf(tri * 10), 80, 3, p);

  String tool = status.tool.length() ? status.tool : String("Tool");
  int cw = textW(tool, FT_SMALL) + 12;
  spr.fillRect(172, 26, cw, 18, C_BODY);
  text(tool, 172 + cw / 2, 40, FT_SMALL, C_BG, C_BASELINE);
  text(fit(status.target, 314 - (178 + cw), FT_SMALL), 178 + cw, 40, FT_SMALL, C_TX);

  const String* steps[2] = {&status.step1, &status.step2};
  for (int i = 0; i < 2; i++) {
    if (!steps[i]->length()) continue;
    int y = 64 + i * 16;
    spr.fillRect(172, y - 8, 6, 6, C_OK);
    text(fit(*steps[i], 130, FT_SMALL), 184, y, FT_SMALL, C_DIM);
  }
  if (status.ttotal > 0) {
    row("Tasks", String(status.tdone) + " / " + status.ttotal, 104);
    bar(172, 110, 142, 5, (float)status.tdone / status.ttotal, C_BODY);
  }
  text(mmss(elapsed()) + "   " + fmtTok(status.tok) + " tokens", 172, 136, FT_SMALL, C_DIM);
  mainTabs("Stats", nullptr);
}

void drawPerm(uint32_t t) {
  uint32_t now = millis();
  bool warn = now < warnUntil;
  topBar(prompt.proj, waitBadge(prompt.wait));

  floorLine(8, 80);
  int j = warn ? 0 : hop(t, 900, 6);
  bool air = j < -1;
  Pose p;
  p.eyes = E_WIDE;
  p.dy = j;
  p.legs = air ? L_TUCK : L_STAND;
  p.aL = p.aR = air ? A_UP : A_SIDE;
  if (warn) p.ex = (t / 110) % 2 ? -1 : 1;  // shakes its head
  GuyFrame f = drawGuy(12, 100, 2, p);
  bubble(f, 28, -14, '!');

  String title = fit(prompt.title, prompt.risky ? 160 : 214, FT_TITLE);
  text(title, 100, 38, FT_TITLE, C_TX);
  if (prompt.risky) {
    int w = textW(title, FT_TITLE);
    spr.fillRect(106 + w, 25, 40, 17, C_BAD);
    text("risky", 126 + w, 38, FT_SMALL, C_BG, C_BASELINE);
  }
  text(fit(prompt.desc, 214, FT_SMALL), 100, 55, FT_SMALL, C_DIM);

  // The command, scrolling by itself when it is longer than 3 lines
  spr.fillRect(98, 61, 216, 52, C_BAR);
  int vis = 3, mo = max(0, prompt.nlines - vis);
  int off = mo ? min((int)prompt.scroll % (mo + 3), mo) : 0;
  for (int i = 0; i < vis && off + i < prompt.nlines; i++) {
    text(prompt.lines[off + i], 104, 76 + i * 16, FT_SMALL, prompt.risky ? C_RISKYTX : C_TX);
  }
  if (mo) {
    int th = max(6, 48 * vis / prompt.nlines);
    spr.fillRect(309, 63, 3, 48, C_FAINT);
    spr.fillRect(309, 63 + (48 - th) * off / mo, 3, th, C_DIM);
  }

  // Choices: bottom button moves, top button picks
  static const char* LABELS[3] = {"Allow", "Deny", "Always"};
  static const uint16_t COLORS[3] = {C_OK, C_BAD, C_BODY};
  int x = 98;
  for (int i = 0; i < permCount(); i++) {
    int w = textW(LABELS[i], FT_SMALL) + 18;
    bool on = i == prompt.sel;
    if (on) spr.fillRect(x, 119, w, 19, COLORS[i]);
    else spr.drawRect(x, 119, w, 19, C_FAINT);
    text(LABELS[i], x + w / 2, 133, FT_SMALL, on ? C_BG : C_DIM, C_BASELINE);
    x += w + 6;
  }

  String hint;
  if (prompt.sel == PI_ALWAYS) hint = String("Rule: ") + prompt.always;
  else if (mo) hint = "Hold Next to scroll";
  if (hint.length()) text(fit(hint, 160, FT_SMALL), 98, 165, FT_SMALL, C_DIM);

  float hp = holdProgress();
  if (selNeedsHold()) {
    TabOpt o;
    o.hold = true;
    o.f = hp;
    o.warn = warn;
    o.shift = (warn && (t / 60) % 2) ? -3 : 0;
    tab(true, hp > 0 ? "Keep holding" : (prompt.sel == PI_ALWAYS ? "Hold to save" : "Hold to allow"), o);
  } else {
    TabOpt o;
    o.fill = prompt.sel == PI_ALLOW ? C_OK : C_BAD;
    tab(true, prompt.sel == PI_ALLOW ? "Allow" : "Deny", o);
  }
  tab(false, "Next");

  if (warn) {
    spr.fillRect(100, 119, 212, 20, C_WARN);
    text("Hold the top button for 1 s", 206, 133, FT_SMALL, C_BG, C_BASELINE);
  }
  bool pulse = (t / 450) % 2;
  uint16_t edge = prompt.risky ? (pulse ? C_BAD : C_BADDARK) : (pulse ? C_BODY : C_DARK);
  spr.drawRect(0, 0, 320, 170, edge);
  spr.drawRect(1, 1, 318, 168, edge);
}

void drawAsk(uint32_t t) {
  String label = prompt.proj + "   Q" + prompt.n + "/" + prompt.of + "   " + askRemaining() + "s";
  topBar(label, waitBadge(prompt.wait));

  floorLine(8, 80);
  Pose p;
  p.ex = 1;
  p.aR = A_MID;
  p.by = (t / 700) % 2;
  GuyFrame f = drawGuy(12, 100, 2, p);
  bubble(f, 28, -14, '?');

  int vis = 2, mo = max(0, prompt.nlines - vis);
  int off = mo ? min((int)prompt.scroll % (mo + 3), mo) : 0;
  for (int i = 0; i < vis && off + i < prompt.nlines; i++) text(prompt.lines[off + i], 98, 34 + i * 16, FT_SMALL, C_TX);

  int n = askCount(), rows = 5;
  int first = clampi(prompt.sel - rows + 1, 0, max(0, n - rows));
  for (int r = 0; r < rows && first + r < n; r++) {
    int i = first + r, y = 58 + r * 18;
    bool on = i == prompt.sel, extra = askIsPC(i) || askIsSubmit(i);
    String s;
    if (askIsPC(i)) s = "Answer on PC";
    else if (askIsSubmit(i)) s = "Send answers";
    else if (prompt.multi) s = String((prompt.checked >> i) & 1 ? "[x] " : "[ ] ") + prompt.opts[i];
    else s = String(i + 1) + "   " + prompt.opts[i];
    if (on) spr.fillRect(94, y, 222, 18, C_BODY);
    text(fit(s, 210, FT_SMALL), 100, y + 14, FT_SMALL, on ? C_BG : (extra ? C_MID : C_TX));
  }

  int i = prompt.sel;
  const char* action = askIsPC(i) ? "On PC" : askIsSubmit(i) ? "Send" : prompt.multi ? "Tick" : "Choose";
  TabOpt o;
  o.fill = C_BODY;
  tab(true, action, o);
  tab(false, "Next");
}

void drawDone(uint32_t t) {
  topBar(status.proj + "   Done");
  floorLine(14, 140);
  int dy = hop(t, 1000, 7);
  bool air = dy < -1;
  Pose p;
  p.eyes = E_HAPPY;
  p.blush = true;
  p.dy = dy;
  p.legs = air ? L_TUCK : L_STAND;
  p.aL = p.aR = A_UP;
  GuyFrame f = drawGuy(30, 80, 3, p);
  static const int8_t SP[5][3] = {{-3, -6, 0}, {39, -4, 1}, {-6, 8, 1}, {42, 10, 0}, {18, -10, 1}};
  for (const auto& s : SP) spark(f, s[0], s[1], t, s[2]);
  text("All done", 172, 50, FT_TITLE, C_OK);
  row("Time", fmtDur(status.dur), 80);
  row("Tokens", fmtTok(status.tok), 97);
  if (status.ttotal > 0) row("Tasks", String(status.tdone) + " / " + status.ttotal, 114);
  mainTabs("Stats", "OK");
}

void drawErr(uint32_t t) {
  topBar(status.proj + "   Stopped");
  floorLine(14, 140);
  int sh = (t % 2200) < 450 ? ((t / 70) % 2 ? 3 : -3) : 0;
  Pose p;
  p.eyes = E_X;
  p.aL = p.aR = A_DOWN;
  p.by = 1;
  GuyFrame f = drawGuy(30 + sh, 80, 3, p);
  sweatDrop(f, 33, -3);
  text("Stopped", 172, 50, FT_TITLE, C_BAD);
  String lines[4];
  int n = wrapText(status.msg.length() ? status.msg : String("Something went wrong"), 142, FT_SMALL, lines, 4);
  for (int i = 0; i < n; i++) text(lines[i], 172, 70 + i * 16, FT_SMALL, C_DIM);
  mainTabs(nullptr, "OK");
}

void drawStats(uint32_t t) {
  topBar(withClock("Today"));
  if (!stats.have) {
    text("No data yet", 12, 60, FT_TITLE, C_DIM);
  } else {
    text(fmtTok(stats.tot), 12, 58, FT_BIG, C_TX);
    text("tokens today", 12, 76, FT_SMALL, C_DIM);
    row("Output", fmtTok(stats.out), 98, 12, 150);
    row("Input", fmtTok(stats.inp), 115, 12, 150);
    row("Cache read", fmtTok(stats.cache), 132, 12, 150);
    text("Last 12 hours", 176, 36, FT_SMALL, C_DIM);
    long mx = 1;
    for (long v : stats.h) mx = max(mx, v);
    for (int i = 0; i < 12; i++) {
      int hh = (int)(stats.h[i] * 60 / mx);
      spr.fillRect(176 + i * 11, 114 - hh, 8, hh + 1, i == 11 ? C_BODY : C_DARK);
    }
    spr.fillRect(176, 115, 130, 1, C_FAINT);
  }
  Pose p;
  p.eyes = E_HAPPY;
  p.aR = (t / 400) % 2 ? A_UP : A_SIDE;
  drawGuy(268, 124, 1, p);
  mainTabs("Back", "Back");
}

void drawNightBase(uint32_t t) {
  spr.fillSprite(C_NIGHT);
  floorLine(14, 140);
  Pose p;
  p.eyes = E_CLOSED;
  p.aL = p.aR = A_DOWN;
  p.by = (t / 1500) % 2;
  p.col = C_SLEEPBODY;
  GuyFrame f = drawGuy(30, 80, 3, p);
  zzz(f, 32, -6, t);
}

void drawSleep(uint32_t t) {
  drawNightBase(t);
  text(clockHM.length() ? clockHM : String("--:--"), 172, 26, FT_CLOCK, C_MID, TL_DATUM);
  text("Nothing running", 172, 98, FT_SMALL, C_NIGHTTX);
  if (demo.active) mainTabs(nullptr, nullptr);
}

void drawOffline(uint32_t t) {
  drawNightBase(t);
  text("Waiting for PC", 172, 52, FT_TITLE, C_MID);
  text("Start bridge.py on", 172, 74, FT_SMALL, C_NIGHTTX);
  text("your PC, or try the demo", 172, 90, FT_SMALL, C_NIGHTTX);
  tab(true, "Demo");
}

void drawCalib(uint32_t t) {
  spr.fillSprite(C_BG);
  text("Setup", 14, 30, FT_TITLE, C_TX);
  text("Press the TOP button", 14, 62, FT_TITLE, C_BODY);
  text("(the one next to this corner)", 14, 82, FT_SMALL, C_DIM);
  text("Upside down? Hold both", 14, 114, FT_SMALL, C_DIM);
  text("buttons for 2 s to flip.", 14, 130, FT_SMALL, C_DIM);
  int b = (t / 300) % 2 ? 2 : 0;  // arrow nudges towards the corner
  for (int k = -1; k <= 1; k++) spr.drawLine(262 + b + k, 58 - b, 302 + b + k, 18 - b, C_BODY);
  spr.fillTriangle(310 + b, 10 - b, 292 + b, 14 - b, 306 + b, 28 - b, C_BODY);
  Pose p;
  p.eyes = E_HAPPY;
  p.aR = (t / 400) % 2 ? A_UP : A_SIDE;
  drawGuy(210, 104, 2, p);
}

void drawAck() {
  if ((int32_t)(millis() - ackUntil) >= 0) return;
  int w = textW(ackText, FT_TITLE) + 30;
  spr.fillRect(160 - w / 2, 66, w, 34, ackColor);
  text(ackText, 160, 89, FT_TITLE, C_BG, C_BASELINE);
}

Screen pickScreen(uint32_t now) {
  if (calibrating) return S_CALIB;
  if (prompt.active) return prompt.ask ? S_ASK : S_PERM;
  if (!linkUp && !demo.active) return S_OFFLINE;
  if (view == V_STATS && !demo.active && since(viewAt) > STATS_SHOW_MS) setView(V_MAIN);
  if (view == V_STATS) return S_STATS;
  if (view == V_SLEEP) return S_SLEEP;
  const String& st = status.st;
  if (st == "think") return S_THINK;
  if (st == "work") return S_WORK;
  if (st == "done") return since(status.stAt) < DONE_SHOW_MS ? S_DONE : S_IDLE;
  if (st == "err") return errDismissed ? S_IDLE : S_ERR;
  if (!demo.active && since(status.stAt) > IDLE_SLEEP_MS) return S_SLEEP;
  return S_IDLE;
}

void setBacklight(uint8_t level) {
  if (level == backlight) return;
  backlight = level;
  analogWrite(PIN_BACKLIGHT, level);
}

void render(uint32_t now) {
  Screen s = pickScreen(now);
  if (s != curScreen) {
    curScreen = s;
    screenAt = now;
  }
  uint32_t t = now - screenAt;
  spr.fillSprite(C_BG);
  switch (s) {
    case S_CALIB: drawCalib(t); break;
    case S_OFFLINE: drawOffline(t); break;
    case S_IDLE: drawIdle(t); break;
    case S_THINK: drawThink(t); break;
    case S_WORK: drawWork(t); break;
    case S_PERM: drawPerm(t); break;
    case S_ASK: drawAsk(t); break;
    case S_DONE: drawDone(t); break;
    case S_ERR: drawErr(t); break;
    case S_STATS: drawStats(t); break;
    case S_SLEEP: drawSleep(t); break;
  }
  if (demo.active) text("DEMO", 6, 165, FT_SMALL, C_WARN);
  drawAck();
  spr.pushSprite(0, 0);
  setBacklight(s == S_PERM || s == S_ASK ? BL_FULL : (s == S_SLEEP || s == S_OFFLINE) ? BL_DIM : BL_NORMAL);
}
