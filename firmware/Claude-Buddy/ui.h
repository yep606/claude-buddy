#pragma once
#include "state.h"

void setFont(FontId f) {
  switch (f) {
    case FT_SMALL: spr.setTextFont(2); break;
    case FT_TITLE: spr.setFreeFont(&FreeSansBold9pt7b); break;
    case FT_BIG: spr.setFreeFont(&FreeSansBold18pt7b); break;
    case FT_CLOCK: spr.setTextFont(7); break;
  }
}

int textW(const String& s, FontId f) {
  setFont(f);
  return spr.textWidth(s);
}

// y is the text baseline unless another datum is given
void text(const String& s, int x, int y, FontId f, uint16_t c, uint8_t datum = L_BASELINE) {
  setFont(f);
  spr.setTextColor(c);
  spr.setTextDatum(datum);
  spr.drawString(s, x, y);
}

// Shorten with "..." so the text fits maxW pixels
String fit(const String& s, int maxW, FontId f) {
  setFont(f);
  if (spr.textWidth(s) <= maxW) return s;
  int lo = 0, hi = s.length();
  while (lo < hi) {
    int mid = (lo + hi + 1) / 2;
    if (spr.textWidth(s.substring(0, mid) + "...") <= maxW) lo = mid;
    else hi = mid - 1;
  }
  return s.substring(0, lo) + "...";
}

// Word-wrap into out[]; words wider than the box are split. Returns the line count.
int wrapText(const String& s, int width, FontId f, String* out, int maxLines) {
  setFont(f);
  int n = 0, i = 0, len = s.length();
  String line;
  while (i <= len && n < maxLines) {
    int j = s.indexOf(' ', i);
    if (j < 0) j = len;
    String word = s.substring(i, j);
    String cand = line.length() ? line + " " + word : word;
    if (spr.textWidth(cand) <= width) {
      line = cand;
    } else {
      if (line.length()) {
        out[n++] = line;
        if (n >= maxLines) return n;
      }
      while (word.length() > 1 && spr.textWidth(word) > width && n < maxLines) {
        int k = word.length();
        while (k > 1 && spr.textWidth(word.substring(0, k)) > width) k--;
        out[n++] = word.substring(0, k);
        word = word.substring(k);
      }
      line = word;
    }
    i = j + 1;
  }
  if (line.length() && n < maxLines) out[n++] = line;
  return n;
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

String fmtTok(long n) {
  char b[16];
  if (n < 1000) snprintf(b, sizeof b, "%ld", n);
  else if (n < 100000) snprintf(b, sizeof b, "%.1fk", n / 1000.0);
  else if (n < 1000000) snprintf(b, sizeof b, "%ldk", n / 1000);
  else snprintf(b, sizeof b, "%.2fM", n / 1000000.0);
  return b;
}

String mmss(long s) {
  char b[16];
  if (s >= 3600) snprintf(b, sizeof b, "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
  else snprintf(b, sizeof b, "%ld:%02ld", s / 60, s % 60);
  return b;
}

String fmtDur(long s) {
  char b[16];
  if (s < 60) snprintf(b, sizeof b, "%lds", s);
  else if (s < 3600) snprintf(b, sizeof b, "%ldm %lds", s / 60, s % 60);
  else snprintf(b, sizeof b, "%ldh %ldm", s / 3600, (s / 60) % 60);
  return b;
}

String withClock(const String& s) {
  if (!clockHM.length()) return s;
  return s + "   " + clockHM;
}

String waitBadge(int wait) {
  if (wait <= 0) return String();
  return String("+") + wait + " waiting";
}

void topBar(const String& label, const String& badge = String()) {
  spr.fillRect(0, 0, 320, 17, C_BAR);
  String l = fit(label, badge.length() ? 110 : 190, FT_SMALL);
  text(l, 6, 13, FT_SMALL, C_DIM);
  if (badge.length()) {
    int w = textW(l, FT_SMALL), bw = textW(badge, FT_SMALL) + 10;
    spr.fillRect(12 + w, 1, bw, 15, C_BODY);
    text(badge, 17 + w, 13, FT_SMALL, C_BG);
  }
}

struct TabOpt {
  uint16_t fill = 0;   // 0 = neutral
  bool hold = false;   // amber outline + ring: this action needs a 1 s hold
  float f = 0;         // hold progress 0..1
  bool warn = false;   // pressed too briefly
  int shift = 0;       // shake
};

// Label in the top-right or bottom-right corner, next to the physical button
void tab(bool top, const String& label, const TabOpt& o = TabOpt()) {
  int ring = o.hold ? 14 : 0;
  int w = textW(label, FT_SMALL) + 22 + ring;
  int x = 320 - w + o.shift, y = top ? 0 : 152, h = 18;
  if (o.hold) {
    spr.fillRect(x, y, w, h, C_FAINT);
    if (o.f > 0) spr.fillRect(x, y, (int)(w * o.f), h, C_OK);
    uint16_t edge = o.warn ? C_BAD : C_WARN;
    spr.drawRect(x, y, w, h, edge);
    spr.drawRect(x + 1, y + 1, w - 2, h - 2, edge);
  } else {
    spr.fillRect(x, y, w, h, o.fill ? o.fill : C_FAINT);
  }
  uint16_t tc = (!o.hold && o.fill) ? C_BG : C_TX;
  text(label, x + 8 + ring, y + 14, FT_SMALL, tc);
  if (o.hold) {
    int cx = x + 13, cy = y + 9;
    spr.drawCircle(cx, cy, 4, C_TX);
    for (int a = 0; o.f > 0 && a <= (int)(o.f * 360); a += 6) {
      float r = (a - 90) * DEG_TO_RAD;
      spr.drawLine(cx, cy, cx + lroundf(cosf(r) * 4), cy + lroundf(sinf(r) * 4), C_TX);
    }
  }
  int ax = x + w - 6, ay = y + 9;
  spr.fillTriangle(ax - 3, ay - 4, ax + 1, ay, ax - 3, ay + 4, tc);
}

// Tabs for status screens; the demo reuses the buttons for Next / Exit
void mainTabs(const char* top, const char* bot) {
  if (demo.active) {
    tab(true, "Next");
    tab(false, "Exit demo");
    return;
  }
  if (top) tab(true, top);
  if (bot) tab(false, bot);
}

void row(const String& label, const String& value, int y, int x0 = 172, int x1 = 314) {
  text(label, x0, y, FT_SMALL, C_DIM);
  text(value, x1, y, FT_SMALL, C_TX, R_BASELINE);
}

void bar(int x, int y, int w, int h, float f, uint16_t c) {
  spr.fillRect(x, y, w, h, C_FAINT);
  spr.fillRect(x, y, (int)(w * constrain(f, 0.0f, 1.0f)), h, c);
}

void floorLine(int x, int w) { spr.fillRect(x, 140, w, 1, C_FAINT); }
