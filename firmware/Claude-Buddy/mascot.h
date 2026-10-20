#pragma once
// The little Claude Code character, drawn from rectangles on a 36x20 grid
// (each grid pixel becomes s x s screen pixels).
#include "ui.h"

struct R8 {
  int8_t x, y, w, h;
};

void gr(const GuyFrame& f, int x, int y, int w, int h, uint16_t c, bool fixed = false) {
  spr.fillRect(f.ox + x * f.s, f.oy + (y + (fixed ? 0 : f.dy)) * f.s, w * f.s, h * f.s, c);
}

GuyFrame drawGuy(int ox, int oy, int s, const Pose& p) {
  GuyFrame f{ox, oy, s, p.dy};

  static const int8_t LEG_X[4] = {8, 12, 22, 26};
  for (int i = 0; i < 4; i++) {
    bool lifted = p.legs == L_TUCK || (p.legs == L_W1 && i % 2 == 0) || (p.legs == L_W2 && i % 2 == 1);
    gr(f, LEG_X[i], 16, 2, lifted ? 2 : 4, p.col);
  }

  gr(f, 6, p.by, 24, 16, p.col);  // body

  static const R8 ARMS[4][2] = {
    {{2, 8, 4, 4}, {0, 0, 0, 0}},   // side
    {{2, 11, 4, 4}, {0, 0, 0, 0}},  // down
    {{2, 6, 4, 4}, {0, 0, 0, 0}},   // mid
    {{2, 6, 4, 4}, {0, 2, 4, 4}},   // up
  };
  for (int side = 0; side < 2; side++) {
    for (const R8& r : ARMS[side ? p.aR : p.aL]) {
      if (!r.w) continue;
      gr(f, side ? 36 - r.x - r.w : r.x, r.y + p.by, r.w, r.h, p.col);
    }
  }

  static const R8 EYES[6][5] = {
    {{0, 4, 2, 4}},                                                // open
    {{0, 7, 2, 1}},                                                // blink
    {{-1, 7, 4, 1}},                                               // closed
    {{0, 3, 2, 5}},                                                // wide
    {{-1, 6, 1, 2}, {0, 5, 2, 1}, {2, 6, 1, 2}},                   // happy ^ ^
    {{-1, 4, 1, 1}, {2, 4, 1, 1}, {0, 5, 2, 2}, {-1, 7, 1, 1}, {2, 7, 1, 1}},  // x x
  };
  for (int ex : {10, 24}) {
    for (const R8& r : EYES[p.eyes]) {
      if (r.w) gr(f, ex + r.x + p.ex, r.y + p.ey + p.by, r.w, r.h, C_EYE);
    }
  }
  if (p.blush) {
    gr(f, 7, 10 + p.by, 2, 1, C_BLUSH);
    gr(f, 27, 10 + p.by, 2, 1, C_BLUSH);
  }
  return f;
}

// Speech bubble with "!" or "?"
void bubble(const GuyFrame& f, int bx, int by, char ch) {
  gr(f, bx + 1, by, 8, 10, C_BUB);
  gr(f, bx, by + 1, 10, 8, C_BUB);
  gr(f, bx + 1, by + 10, 2, 2, C_BUB);
  static const R8 BANG[] = {{4, 2, 2, 4}, {4, 7, 2, 2}};
  static const R8 QUEST[] = {{4, 2, 2, 1}, {3, 3, 1, 1}, {6, 3, 1, 2}, {5, 5, 1, 1}, {4, 6, 1, 1}, {4, 8, 1, 1}};
  if (ch == '!') {
    for (const R8& r : BANG) gr(f, bx + r.x, by + r.y, r.w, r.h, C_BODY);
  } else {
    for (const R8& r : QUEST) gr(f, bx + r.x, by + r.y, r.w, r.h, C_BODY);
  }
}

// Thought bubble with three dots lighting up in turn
void thought(const GuyFrame& f, int bx, int by, uint32_t t) {
  gr(f, bx, by + 12, 2, 2, C_MID);
  gr(f, bx + 3, by + 8, 3, 3, C_MID);
  gr(f, bx + 7, by, 14, 8, C_BUB);
  gr(f, bx + 6, by + 1, 16, 6, C_BUB);
  for (int i = 0; i < 3; i++) gr(f, bx + 9 + i * 4, by + 3, 2, 2, (int)((t / 350) % 4) > i ? C_BODY : C_FAINT);
}

void zzz(const GuyFrame& f, int bx, int by, uint32_t t) {
  for (int i = 0; i < 3; i++) {
    float ph = fmodf(t / 1400.0f + i / 3.0f, 1.0f);
    int x = bx + lroundf(ph * 6), y = by - lroundf(ph * 14);
    uint16_t c = ph > 0.75f ? C_FAINT : C_MID;
    gr(f, x, y, 4, 1, c, true);
    gr(f, x + 2, y + 1, 1, 1, c, true);
    gr(f, x + 1, y + 2, 1, 1, c, true);
    gr(f, x, y + 3, 4, 1, c, true);
  }
}

void spark(const GuyFrame& f, int x, int y, uint32_t t, int k) {
  if ((t / 260 + k) % 2) {
    gr(f, x, y - 1, 1, 3, C_WARN, true);
    gr(f, x - 1, y, 3, 1, C_WARN, true);
  } else {
    gr(f, x, y, 1, 1, C_WARN, true);
  }
}

void sweatDrop(const GuyFrame& f, int x, int y) {
  gr(f, x, y, 1, 1, C_DROP);
  gr(f, x - 1, y + 1, 3, 2, C_DROP);
}

// Jump height for a hop every `period` ms (negative = up)
int hop(uint32_t t, uint32_t period, int height) {
  float ph = (t % period) / (float)period;
  return -lroundf(fmaxf(0, sinf(ph * TWO_PI)) * height);
}
