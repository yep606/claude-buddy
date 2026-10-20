#pragma once
/* Messages over USB serial: one per line, fields separated by TAB as key=value.
   The PC sends plain ASCII only (the screen fonts have no other characters).

   PC -> board
     HELLO                     bridge connected; the board answers HELLO
     PING     hm=23:41         keep-alive every ~2 s, carries the clock
     STATUS   st=idle|think|work|done|err proj= tool= target= step1= step2=
              tdone= ttotal= el= tok= ctx= model= dur= msg=
     PERM     id= proj= wait= title= desc= body= risky=0|1 always=<rule, or empty>
     ASK      id= proj= wait= n= of= tmo= multi=0|1 q= o1= .. o8=
     CLEAR    id=              prompt answered on the PC, or timed out
     STATS    tot= out= inp= cache= h=v1,v2,..,v12
     DEMO, CALIB               handy from the Arduino Serial Monitor
     DBG                       board replies with its current state
   board -> PC
     HELLO    v=1
     DBG      st= proj= screen= view= prompt= link= demo= calib= lines= last=
     ANS      id= c=allow|deny|always | 1..8 | 1,3 (multi) | pc
*/
#include "input.h"

String field(const String& line, const char* key) {
  String pat = String('\t') + key + '=';
  int i = line.indexOf(pat);
  if (i < 0) return String();
  i += pat.length();
  int j = line.indexOf('\t', i);
  return j < 0 ? line.substring(i) : line.substring(i, j);
}

long fieldL(const String& line, const char* key, long def = 0) {
  String v = field(line, key);
  return v.length() ? v.toInt() : def;
}

void dropLink() {
  linkUp = false;
  prompt.active = false;
  status = Status();
  stats = Stats();
  clockHM = "";
  setView(V_MAIN);
}

void onStatus(const String& line, uint32_t now) {
  String st = field(line, "st");
  if (!st.length()) st = "idle";
  if (st != status.st || status.stAt == 0) {
    status.stAt = now;
    errDismissed = false;
    if (view == V_SLEEP && st != "idle") setView(V_MAIN);
  }
  status.st = st;
  status.proj = field(line, "proj");
  status.tool = field(line, "tool");
  status.target = field(line, "target");
  status.step1 = field(line, "step1");
  status.step2 = field(line, "step2");
  status.model = field(line, "model");
  status.msg = field(line, "msg");
  status.tdone = fieldL(line, "tdone");
  status.ttotal = fieldL(line, "ttotal");
  status.ctx = fieldL(line, "ctx", -1);
  status.el = fieldL(line, "el");
  status.dur = fieldL(line, "dur");
  status.tok = fieldL(line, "tok");
  status.rxAt = now;
}

void onPrompt(const String& line, bool ask, uint32_t now) {
  prompt = Prompt();
  prompt.active = true;
  prompt.ask = ask;
  prompt.id = field(line, "id");
  prompt.proj = field(line, "proj");
  prompt.wait = fieldL(line, "wait");
  prompt.at = now;
  if (ask) {
    prompt.q = field(line, "q");
    prompt.multi = fieldL(line, "multi") == 1;
    prompt.n = fieldL(line, "n", 1);
    prompt.of = fieldL(line, "of", 1);
    prompt.tmo = fieldL(line, "tmo", 30);
    for (int i = 0; i < MAX_OPTS; i++) {
      String o = field(line, (String("o") + (i + 1)).c_str());
      if (!o.length()) break;
      prompt.opts[prompt.nopts++] = o;
    }
    prompt.nlines = wrapText(prompt.q, 214, FT_SMALL, prompt.lines, MAX_LINES);
  } else {
    prompt.title = field(line, "title");
    prompt.desc = field(line, "desc");
    prompt.body = field(line, "body");
    prompt.always = field(line, "always");
    prompt.risky = fieldL(line, "risky") == 1;
    prompt.nlines = wrapText(prompt.body, 200, FT_SMALL, prompt.lines, MAX_LINES);
  }
  btnBoot.handled = btnKey.handled = true;  // a press already in progress can't answer this
  warnUntil = 0;
  if (view != V_MAIN) setView(V_MAIN);
}

void onStats(const String& line) {
  stats.have = true;
  stats.tot = fieldL(line, "tot");
  stats.out = fieldL(line, "out");
  stats.inp = fieldL(line, "inp");
  stats.cache = fieldL(line, "cache");
  String h = field(line, "h");
  int idx = 0, start = 0;
  while (idx < 12 && start <= (int)h.length()) {
    int c = h.indexOf(',', start);
    if (c < 0) c = h.length();
    stats.h[idx++] = h.substring(start, c).toInt();
    start = c + 1;
  }
}

uint32_t rxLines = 0;
String rxLast;  // command and length of the previous line, for DBG

void onLine(const String& line) {
  int tabAt = line.indexOf('\t');
  String cmd = tabAt < 0 ? line : line.substring(0, tabAt);
  uint32_t now = millis();
  if (cmd == "DBG") {
    Serial.print(String("DBG\tst=") + status.st + "\tproj=" + status.proj + "\tscreen=" + (int)curScreen +
                 "\tview=" + (int)view + "\tprompt=" + (prompt.active ? 1 : 0) + "\tlink=" + (linkUp ? 1 : 0) +
                 "\tdemo=" + (demo.active ? 1 : 0) + "\tcalib=" + (calibrating ? 1 : 0) + "\tlines=" + rxLines +
                 "\tlast=" + rxLast + "\n");
    return;
  }
  rxLines++;
  rxLast = cmd + ":" + line.length();
  if (cmd == "DEMO") {
    startDemo();
    return;
  }
  if (cmd == "CALIB") {
    startCalibration();
    return;
  }
  lastRx = now;
  linkUp = true;
  if (demo.active) stopDemo();

  if (cmd == "HELLO") {
    sendLine("HELLO\tv=1");
  } else if (cmd == "PING") {
    String hm = field(line, "hm");
    if (hm.length()) clockHM = hm;
  } else if (cmd == "STATUS") {
    onStatus(line, now);
  } else if (cmd == "PERM") {
    onPrompt(line, false, now);
  } else if (cmd == "ASK") {
    onPrompt(line, true, now);
  } else if (cmd == "CLEAR") {
    if (prompt.active && field(line, "id") == prompt.id) prompt.active = false;
  } else if (cmd == "STATS") {
    onStats(line);
  }
}

void readSerial() {
  static char buf[2048];
  static size_t len = 0;
  static bool overflow = false;
  int budget = 4096;  // don't starve the animation
  while (Serial.available() && budget-- > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      if (!overflow && len) {
        buf[len] = 0;
        onLine(String(buf));
      }
      len = 0;
      overflow = false;
    } else if (c != '\r') {
      if (len < sizeof(buf) - 1) buf[len++] = c;
      else overflow = true;
    }
  }
}
