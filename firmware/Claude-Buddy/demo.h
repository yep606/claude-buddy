#pragma once
// Demo mode: cycles through every screen with made-up data, no PC needed.
// Status screens advance by themselves; prompts wait for you to try the buttons.
#include "screens.h"

enum DemoStep : uint8_t { D_IDLE, D_THINK, D_WORK, D_PERM, D_RISKY, D_LONG, D_ASK, D_DONE, D_ERR, D_STATS, D_SLEEP, D_COUNT };

void demoPrompt(bool ask) {
  prompt = Prompt();
  prompt.active = true;
  prompt.ask = ask;
  prompt.id = "demo";
  prompt.proj = "electronics";
  prompt.wait = 1;
  prompt.at = millis();
}

void demoLoad(int step) {
  uint32_t now = millis();
  prompt.active = false;
  setView(V_MAIN);
  errDismissed = false;
  warnUntil = 0;
  status = Status();
  status.proj = "electronics";
  status.model = "Opus 5.5";
  status.tok = 48213;
  status.ctx = 31;
  status.dur = 134;
  status.rxAt = status.stAt = now;
  clockHM = "14:32";

  switch (step) {
    case D_THINK:
      status.st = "think";
      status.el = 12;
      status.tok = 3100;
      break;
    case D_WORK:
      status.st = "work";
      status.tool = "Edit";
      status.target = "main.ino";
      status.step1 = "Read  lilygo.md";
      status.step2 = "Grep  setRotation";
      status.tdone = 3;
      status.ttotal = 7;
      status.el = 65;
      status.tok = 12400;
      break;
    case D_PERM:
      status.st = "work";
      demoPrompt(false);
      prompt.title = "Allow Bash?";
      prompt.desc = "Push commits to GitHub";
      prompt.body = "git push origin main";
      prompt.always = "Bash(git push *)";
      break;
    case D_RISKY:
      status.st = "work";
      demoPrompt(false);
      prompt.title = "Allow Bash?";
      prompt.desc = "Delete build folders";
      prompt.body = "rm -rf build/ dist/ .cache/";
      prompt.risky = true;
      break;
    case D_LONG:
      status.st = "work";
      demoPrompt(false);
      prompt.title = "Allow Bash?";
      prompt.desc = "Compile, flash, restart bridge";
      prompt.body = "arduino-cli compile --fqbn esp32:esp32:esp32s3 Claude-Buddy && arduino-cli upload -p COM8 "
                    "--fqbn esp32:esp32:esp32s3 Claude-Buddy && python bridge.py --restart";
      prompt.always = "Bash(arduino-cli compile *)";
      break;
    case D_ASK:
      status.st = "work";
      demoPrompt(true);
      prompt.q = "Which connection should we build first?";
      prompt.opts[0] = "USB serial";
      prompt.opts[1] = "Wi-Fi";
      prompt.opts[2] = "Bluetooth LE";
      prompt.nopts = 3;
      prompt.of = 2;
      break;
    case D_DONE:
      status.st = "done";
      status.tdone = status.ttotal = 7;
      break;
    case D_ERR:
      status.st = "err";
      status.msg = "You denied Bash. Claude is waiting for your next message.";
      break;
    case D_STATS: {
      setView(V_STATS);
      static const long H[12] = {3, 5, 2, 0, 0, 1, 4, 8, 6, 9, 7, 5};
      stats.have = true;
      stats.tot = 1240000;
      stats.out = 41200;
      stats.inp = 82000;
      stats.cache = 1120000;
      for (int i = 0; i < 12; i++) stats.h[i] = H[i] * 10000;
      break;
    }
    case D_SLEEP:
      setView(V_SLEEP);
      break;
  }
  if (prompt.active) {
    const String& src = prompt.ask ? prompt.q : prompt.body;
    prompt.nlines = wrapText(src, prompt.ask ? 214 : 200, FT_SMALL, prompt.lines, MAX_LINES);
  }
  demo.step = step;
  demo.stepAt = now;
}

void startDemo() {
  demo.active = true;
  demoLoad(D_IDLE);
}

void stopDemo() {
  demo.active = false;
  prompt.active = false;
  status = Status();
  stats = Stats();
  clockHM = "";
  setView(V_MAIN);
}

void demoNext() { demoLoad((demo.step + 1) % D_COUNT); }

// After an answer, move on shortly so the confirmation stays visible
void demoAnswered() {
  if (demo.active) demo.stepAt = millis() - 3500;
}

void updateDemo(uint32_t now) {
  if (!demo.active) return;
  uint32_t limit = prompt.active ? 25000 : 5000;
  if (since(demo.stepAt) > limit) demoNext();
}
