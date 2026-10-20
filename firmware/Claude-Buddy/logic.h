#pragma once
#include "mascot.h"

void sendLine(const String& s) {
  if (demo.active) return;  // the demo never talks to the PC
  Serial.print(s);
  Serial.print('\n');
}

void showAck(const String& s, uint16_t c) {
  ackText = s;
  ackColor = c;
  ackUntil = millis() + 700;
}

void setView(View v) {
  view = v;
  viewAt = millis();
}

// Back to the main screen; also restarts the idle-sleep timer
void wake() {
  setView(V_MAIN);
  status.stAt = millis();
}

long elapsed() { return status.el + (long)(since(status.rxAt) / 1000); }

int permCount() { return (prompt.always.length() && !prompt.risky) ? 3 : 2; }
int askCount() { return prompt.nopts + (prompt.multi ? 1 : 0) + 1; }  // options, [Send], Answer on PC
bool askIsSubmit(int i) { return prompt.multi && i == prompt.nopts; }
bool askIsPC(int i) { return i == askCount() - 1; }

long askRemaining() {
  long r = prompt.tmo - (long)(since(prompt.at) / 1000);
  return r < 0 ? 0 : r;
}

// A risky "Allow" and "Always allow" only act after a 1 s hold
bool selNeedsHold() {
  return prompt.active && !prompt.ask && ((prompt.sel == PI_ALLOW && prompt.risky) || prompt.sel == PI_ALWAYS);
}

// A press only counts for a prompt if it started after the prompt appeared,
// so a click meant for another screen can never approve anything.
bool pressIsForPrompt(const Button& b) { return b.t0 >= prompt.at + PROMPT_GUARD_MS; }

float holdProgress() {
  const Button& b = *btnTop;
  if (!selNeedsHold() || !b.down || b.handled || !pressIsForPrompt(b)) return 0;
  return min(1.0f, since(b.t0) / (float)HOLD_MS);
}
