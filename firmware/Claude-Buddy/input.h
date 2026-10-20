#pragma once
// The two buttons. Which physical button is "top" is learned once (Setup screen)
// and saved; all actions are written in terms of top and bottom.
#include "demo.h"

void applyButtonMap(uint8_t topPin) {
  bool keyOnTop = topPin == PIN_KEY;
  btnTop = keyOnTop ? &btnKey : &btnBoot;
  btnBot = keyOnTop ? &btnBoot : &btnKey;
}

void startCalibration() {
  calibrating = true;
  calibBothAt = 0;
  calibFlipped = true;  // wait until both buttons are released first
}

void calibrateWith(const Button& b) {
  prefs.putUChar("top", b.pin);
  applyButtonMap(b.pin);
  calibrating = false;
  showAck("Thanks!", C_OK);
}

// Holding both buttons for 2 s on the Setup screen turns the picture upside down
void updateCalibration(uint32_t now) {
  if (btnBoot.down && btnKey.down) {
    btnBoot.handled = btnKey.handled = true;
    if (!calibBothAt) calibBothAt = now;
    if (!calibFlipped && since(calibBothAt) > CALIB_FLIP_MS) {
      rotation = rotation == 1 ? 3 : 1;
      tft.setRotation(rotation);
      prefs.putUChar("rot", rotation);
      calibFlipped = true;
    }
  } else {
    calibBothAt = 0;
    if (!btnBoot.down && !btnKey.down) calibFlipped = false;
  }
}

void answerPerm(uint8_t item) {
  static const char* WORDS[3] = {"allow", "deny", "always"};
  sendLine(String("ANS\tid=") + prompt.id + "\tc=" + WORDS[item]);
  showAck(item == PI_DENY ? "Denied" : item == PI_ALWAYS ? "Always allowed" : "Allowed", item == PI_DENY ? C_BAD : C_OK);
  prompt.active = false;
  warnUntil = 0;
  demoAnswered();
}

void answerAsk(const String& choice) {
  sendLine(String("ANS\tid=") + prompt.id + "\tc=" + choice);
  bool pc = choice == "pc";
  showAck(pc ? "Over to the PC" : "Sent", pc ? C_MID : C_OK);
  prompt.active = false;
  demoAnswered();
}

void onTopRelease() {
  if (prompt.active && !prompt.ask) {
    if (selNeedsHold()) {  // let go before the hold finished: remind, never act
      warnUntil = millis() + 1800;
      return;
    }
    answerPerm(prompt.sel);
    return;
  }
  if (prompt.active) {
    int i = prompt.sel;
    if (askIsPC(i)) {
      answerAsk("pc");
    } else if (askIsSubmit(i)) {
      String list;
      for (int k = 0; k < prompt.nopts; k++) {
        if (!((prompt.checked >> k) & 1)) continue;
        if (list.length()) list += ',';
        list += String(k + 1);
      }
      if (list.length()) answerAsk(list);
      else showAck("Tick one first", C_WARN);
    } else if (prompt.multi) {
      prompt.checked ^= (1 << i);
    } else {
      answerAsk(String(i + 1));
    }
    return;
  }
  if (demo.active) {
    demoNext();
    return;
  }
  switch (curScreen) {
    case S_OFFLINE: startDemo(); break;
    case S_STATS: setView(V_MAIN); break;
    case S_SLEEP: wake(); break;
    case S_ERR: errDismissed = true; break;
    case S_IDLE: case S_THINK: case S_WORK: case S_DONE: setView(V_STATS); break;
    default: break;
  }
}

void onBotRelease() {
  if (prompt.active) {
    prompt.sel = (prompt.sel + 1) % (prompt.ask ? askCount() : permCount());
    warnUntil = 0;
    return;
  }
  if (demo.active) {
    stopDemo();
    return;
  }
  switch (curScreen) {
    case S_IDLE: setView(V_SLEEP); break;
    case S_SLEEP: wake(); break;
    case S_STATS: setView(V_MAIN); break;
    case S_ERR: errDismissed = true; break;
    case S_DONE: status.stAt = millis() - DONE_SHOW_MS; break;  // "OK" skips the celebration
    default: break;
  }
}

void pollButton(Button& b, uint32_t now) {
  bool raw = digitalRead(b.pin) == LOW;
  if (raw != b.raw) {
    b.raw = raw;
    b.changedAt = now;
  }
  if (now - b.changedAt < 25 || raw == b.down) return;  // debounce
  b.down = raw;
  if (raw) {
    b.t0 = now;
    b.handled = false;
    return;
  }
  if (b.handled) return;
  if (calibrating) {
    calibrateWith(b);
    return;
  }
  if (prompt.active && !pressIsForPrompt(b)) return;
  if (&b == btnTop) onTopRelease();
  else onBotRelease();
}

// Things that happen while a button is still held down
void updateHolds(uint32_t now, uint32_t dt) {
  Button& T = *btnTop;
  Button& B = *btnBot;
  if (T.down && B.down) {
    T.handled = B.handled = true;  // a two-button press never answers anything
    if (!prompt.active && since(max(T.t0, B.t0)) > CALIB_ENTER_MS) startCalibration();
    return;
  }
  if (prompt.active && !prompt.ask && T.down && !T.handled && pressIsForPrompt(T) && selNeedsHold() &&
      since(T.t0) >= HOLD_MS) {
    T.handled = true;
    answerPerm(prompt.sel);
  }
  if (prompt.active) {
    bool fast = B.down && pressIsForPrompt(B) && since(B.t0) > SCROLL_HOLD_MS;
    if (fast) B.handled = true;  // a hold scrolls; it doesn't move the selection
    prompt.scroll += dt / (fast ? 200.0f : 1500.0f);
  }
}
