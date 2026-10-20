"""Claude Buddy bridge: connects Claude Code to the T-Display-S3.

Claude Code's HTTP hooks POST every event to http://127.0.0.1:8737. The bridge
keeps track of each session, shows the most relevant one on the board over USB
serial, and turns permission prompts and questions into board prompts that are
answered with the two buttons. Nothing leaves this PC.

    pythonw bridge.py          run hidden (what autostart uses)
    python bridge.py           run with a console, for watching the log
    python bridge.py pause     release the COM port for 3 minutes (Arduino upload)
    python bridge.py resume    take the port back
    python bridge.py status    print what the bridge knows
    python bridge.py stop      quit the running bridge
    python bridge.py --debug   also log every line sent to and from the board

The serial protocol is described at the top of firmware/Claude-Buddy/protocol.h.
"""
import json
import logging
import os
import re
import select
import socket
import sys
import threading
import time
import unicodedata
import urllib.request
from collections import deque
from datetime import datetime, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from logging.handlers import RotatingFileHandler
from pathlib import Path
from urllib.parse import urlparse

import serial
from serial.tools import list_ports

PORT = 8737
ESPRESSIF_VID = 0x303A
ASK_TIMEOUT_S = 30            # how long the board holds a question before the app shows it
PAUSE_S = 180                 # "pause" releases the COM port this long
STALE_TURN_S = 15 * 60        # a turn with no events for this long is treated as over
FORGET_SESSION_S = 3 * 3600   # sessions quiet for this long are dropped
HERE = Path(__file__).resolve().parent
PROJECTS_DIR = Path.home() / ".claude" / "projects"

log = logging.getLogger("buddy")
lock = threading.RLock()      # guards Hub state; the board has its own write lock


# ---------------------------------------------------------------- text helpers

PUNCT = str.maketrans({
    "‘": "'", "’": "'", "“": '"', "”": '"', "–": "-", "—": "-",
    "…": "...", " ": " ", "→": "->", "←": "<-", "•": "*", "×": "x",
})


def ascii_text(value, limit=None):
    """The board fonts are ASCII only, and tabs/newlines would break the line protocol."""
    text = str(value if value is not None else "").translate(PUNCT)
    text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode()
    text = re.sub(r"\s+", " ", text).strip()
    if limit and len(text) > limit:
        text = text[: limit - 3] + "..."
    return text


def board_msg(cmd, **fields):
    return "\t".join([cmd] + [f"{k}={ascii_text(v)}" for k, v in fields.items() if v is not None])


def parse_board_line(line):
    cmd, *parts = line.split("\t")
    return cmd, dict(p.split("=", 1) for p in parts if "=" in p)


def model_name(model_id):
    """claude-opus-5-5 -> Opus 5.5"""
    m = re.match(r"claude-([a-z]+)-(\d+)(?:-(\d{1,2}))?(?!\d)", model_id or "")
    if not m:
        return ascii_text(model_id, 16)
    family, major, minor = m.groups()
    return f"{family.title()} {major}" + (f".{minor}" if minor else "")


def tool_label(tool):
    if tool.startswith("mcp__"):
        return tool.split("__")[-1][:14]
    names = {"TodoWrite": "Tasks", "Task": "Agent", "NotebookEdit": "Notebook",
             "ExitPlanMode": "Plan", "AskUserQuestion": "Question"}
    return names.get(tool, tool)[:14]


def tool_target(tool, ti):
    for key in ("file_path", "notebook_path"):
        if ti.get(key):
            return Path(str(ti[key])).name
    if tool in ("Bash", "PowerShell"):
        return ti.get("description") or ti.get("command", "")
    if tool in ("Grep", "Glob"):
        return ti.get("pattern", "")
    if tool == "WebFetch":
        return urlparse(ti.get("url", "")).netloc
    if tool == "WebSearch":
        return ti.get("query", "")
    if tool in ("Task", "Agent"):
        return ti.get("description", "")
    if tool == "Skill":
        return ti.get("skill", "")
    if tool in ("TodoWrite", "AskUserQuestion"):
        return ""
    return next((v for v in ti.values() if isinstance(v, str)), "")


RISKY = re.compile(r"""(?ix)
    \brm\s+(-\w*[rf]|--recursive|--force) | \brmdir\b | \bdel\s | \berase\s | Remove-Item | \brd\s+/s |
    \bgit\s+(push\b.*\s(--force|-f)\b | reset\s+--hard | clean\s+-\w*f | branch\s+-D | checkout\s+--\s) |
    \b(mkfs|diskpart|shutdown|reboot)\b | \bformat\s+[a-z]: | \bdd\s+if= | \bchmod\s+-R | \bchown\s+-R |
    \b(curl|wget|iwr|Invoke-WebRequest)\b.*\|\s*(sh|bash|iex|python) | \bsudo\b | Set-ExecutionPolicy |
    \breg\s+delete\b | \b(npm|pnpm|yarn)\s+publish\b | \bdrop\s+(table|database)\b
""")


def is_risky(tool, ti):
    return tool in ("Bash", "PowerShell") and bool(RISKY.search(ti.get("command", "")))


def perm_title(tool):
    if tool in ("Edit", "Write", "NotebookEdit", "MultiEdit"):
        return "Allow edit?"
    if tool == "ExitPlanMode":
        return "Approve plan?"
    return f"Allow {tool_label(tool)}?"


def perm_desc(tool, ti):
    if tool in ("Bash", "PowerShell"):
        return ti.get("description", "")
    if tool.startswith("mcp__"):
        return f"{tool.split('__')[1]} tool"
    if tool == "ExitPlanMode":
        return "Claude's plan"
    return tool_target(tool, ti) if tool not in ("Edit", "Write") else f"{tool} {tool_target(tool, ti)}"


def perm_body(tool, ti):
    if tool in ("Bash", "PowerShell"):
        return ti.get("command", "")
    if ti.get("file_path"):
        return ti["file_path"]
    if tool == "WebFetch":
        return ti.get("url", "")
    if tool == "ExitPlanMode":
        return ti.get("plan", "")
    return json.dumps(ti, ensure_ascii=False)


def rule_text(rule):
    content = rule.get("ruleContent")
    return f"{rule.get('toolName', '')}({content})" if content else rule.get("toolName", "")


FAILURES = {
    "rate_limit": "Usage limit reached. Try again later.",
    "overloaded": "Claude is overloaded. Try again in a bit.",
    "authentication_failed": "Sign-in expired. Log in again in the app.",
    "billing_error": "There is a billing problem on the account.",
    "server_error": "Server error. Try again.",
    "max_output_tokens": "The reply hit the length limit.",
}


def client_gone(conn):
    """True once Claude Code closed the hook request (it gave up waiting)."""
    try:
        readable, _, _ = select.select([conn], [], [], 0)
        return bool(readable) and conn.recv(1, socket.MSG_PEEK) == b""
    except OSError:
        return True


# ---------------------------------------------------------------- transcripts

class Transcript:
    """Reads a session's .jsonl log incrementally: token usage and finished tool calls."""

    def __init__(self, path):
        self.path = path
        self.offset = 0
        self.msgs = {}                    # message id -> (epoch, input, output, cache_read, cache_create)
        self.results = deque(maxlen=300)  # tool_use_ids that already have a result
        self.last_ctx = 0
        self.max_ctx = 0
        self.model = ""

    def update(self):
        try:
            size = os.path.getsize(self.path)
        except OSError:
            return
        if size < self.offset:  # rewritten: start over
            self.__init__(self.path)
        if size == self.offset:
            return
        with open(self.path, "rb") as f:
            f.seek(self.offset)
            data = f.read(size - self.offset)
        end = data.rfind(b"\n")
        if end < 0:
            return
        self.offset += end + 1
        for raw in data[: end + 1].splitlines():
            try:
                rec = json.loads(raw)
            except ValueError:
                continue
            self._take(rec)

    def _take(self, rec):
        msg = rec.get("message") if isinstance(rec.get("message"), dict) else {}
        if rec.get("type") == "assistant":
            usage = msg.get("usage")
            mid = msg.get("id") or rec.get("uuid")
            if usage and mid:
                inp = usage.get("input_tokens") or 0
                out = usage.get("output_tokens") or 0
                cread = usage.get("cache_read_input_tokens") or 0
                ccreate = usage.get("cache_creation_input_tokens") or 0
                self.msgs[mid] = (parse_ts(rec.get("timestamp")), inp, out, cread, ccreate)
                self.last_ctx = inp + cread + ccreate
                self.max_ctx = max(self.max_ctx, self.last_ctx)
            if msg.get("model") and not msg["model"].startswith("<"):
                self.model = msg["model"]
        elif rec.get("type") == "user" and isinstance(msg.get("content"), list):
            for part in msg["content"]:
                if isinstance(part, dict) and part.get("type") == "tool_result":
                    self.results.append(part.get("tool_use_id"))

    def tokens(self):
        """New tokens processed: input + cache writes + output (cache reads are shown separately)."""
        return sum(i + o + cc for _, i, o, _, cc in self.msgs.values())

    def context_percent(self):
        if not self.last_ctx:
            return None
        window = 1_000_000 if self.max_ctx > 200_000 or "1m" in self.model.lower() else 200_000
        return min(100, round(100 * self.last_ctx / window))


def parse_ts(value):
    try:
        return datetime.fromisoformat(str(value).replace("Z", "+00:00")).timestamp()
    except ValueError:
        return time.time()


class Tokens:
    def __init__(self):
        self.files = {}
        self.lock = threading.Lock()

    def get(self, path):
        with self.lock:
            t = self.files.get(path)
            if t is None:
                t = self.files[path] = Transcript(path)
            t.update()
            return t

    def today(self):
        """Totals for today across every transcript, plus tokens per hour for the last 12 hours."""
        cutoff = time.time() - 24 * 3600
        for path in PROJECTS_DIR.rglob("*.jsonl"):
            try:
                if path.stat().st_mtime > cutoff:
                    self.get(str(path))
            except OSError:
                pass
        now = datetime.now()
        midnight = now.replace(hour=0, minute=0, second=0, microsecond=0).timestamp()
        hour_start = now.replace(minute=0, second=0, microsecond=0)
        first_hour = (hour_start - timedelta(hours=11)).timestamp()
        tot = out = inp = cache = 0
        hours = [0] * 12
        with self.lock:
            for t in self.files.values():
                for ts, i, o, cr, cc in t.msgs.values():
                    if ts >= midnight:
                        tot += i + o + cc
                        out += o
                        inp += i + cc
                        cache += cr
                    if ts >= first_hour:
                        hours[min(11, int((ts - first_hour) // 3600))] += i + o + cc
        return dict(tot=tot, out=out, inp=inp, cache=cache, h=",".join(map(str, hours)))


# ---------------------------------------------------------------- the board

class Board:
    """Finds the T-Display-S3 by its USB id, keeps the port open, reconnects when needed."""

    def __init__(self):
        self.ser = None
        self.wlock = threading.Lock()
        self.paused_until = 0
        self.on_connect = lambda: None
        self.on_line = lambda line: None

    def connected(self):
        return self.ser is not None

    def send(self, line):
        with self.wlock:
            if not self.ser:
                return False
            try:
                self.ser.write((line + "\n").encode("ascii", "replace"))
                if not line.startswith("PING"):
                    log.debug("-> %s", line[:160])
                return True
            except (serial.SerialException, OSError) as e:
                log.warning("board write failed: %s", e)
                self._close()
                return False

    def _close(self):
        if self.ser:
            try:
                self.ser.close()
            except (serial.SerialException, OSError):
                pass
            self.ser = None
            log.info("board disconnected")

    def pause(self, seconds):
        self.paused_until = time.time() + seconds
        with self.wlock:
            self._close()
        log.info("COM port released for %d s", seconds)

    def resume(self):
        self.paused_until = 0

    def _open(self):
        device = next((p.device for p in list_ports.comports() if p.vid == ESPRESSIF_VID), None)
        if not device:
            return False
        ser = serial.Serial()
        ser.port = device
        ser.baudrate = 115200
        ser.timeout = 0.2
        ser.write_timeout = 1
        ser.dtr = False  # don't toggle the reset lines when opening
        ser.rts = False
        try:
            ser.open()
        except (serial.SerialException, OSError) as e:
            log.debug("can't open %s yet: %s", device, e)
            return False
        with self.wlock:
            self.ser = ser
        log.info("board connected on %s", device)
        return True

    def run(self):
        while True:
            if time.time() < self.paused_until:
                time.sleep(0.5)
                continue
            if not self.ser:
                if self._open():
                    self.on_connect()
                else:
                    time.sleep(2)
                continue
            try:
                raw = self.ser.readline()
            except (serial.SerialException, OSError, AttributeError) as e:
                log.warning("board read failed: %s", e)
                with self.wlock:
                    self._close()
                continue
            line = raw.decode("ascii", "replace").strip()
            if line:
                log.debug("<- %s", line)
                self.on_line(line)


# ---------------------------------------------------------------- sessions and prompts

class Session:
    def __init__(self, sid):
        self.sid = sid
        self.proj = ""
        self.transcript = None
        self.st = "idle"
        self.tool = self.target = self.msg = ""
        self.steps = deque(maxlen=2)
        self.turn_start = None
        self.dur = 0
        self.todo = None              # (done, total) from TodoWrite
        self.tasks = [0, 0]           # (done, created) from TaskCreated/TaskCompleted
        self.tool_ids = {}            # (tool, input) -> tool_use_id, to match PermissionRequest
        self.last = time.time()


class Pending:
    """A permission prompt or one question, waiting for the board (or for Claude to move on)."""

    def __init__(self, kind, sid, fields, tool_use_id=None):
        self.id = ""
        self.kind = kind              # "perm" or "ask"
        self.sid = sid
        self.fields = fields
        self.tool_use_id = tool_use_id
        self.always = []              # permission updates for "Always allow"
        self.answer = None
        self.done = threading.Event()

    def message(self, wait):
        return board_msg("PERM" if self.kind == "perm" else "ASK", id=self.id, wait=wait, **self.fields)


def tool_key(tool, ti):
    return tool + json.dumps(ti, sort_keys=True)


class Hub:
    def __init__(self, board, tokens):
        self.board = board
        self.tokens = tokens
        self.sessions = {}
        self.queue = []               # prompts in arrival order; the first is on the board
        self.shown = None             # id of the prompt the board is showing
        self.next_id = 1
        self.last_status = None

    # -- what the board shows

    def focus(self):
        if self.queue:
            return self.sessions.get(self.queue[0].sid)
        if not self.sessions:
            return None
        rank = {"work": 3, "think": 3, "err": 2, "done": 2}
        return max(self.sessions.values(), key=lambda s: (rank.get(s.st, 1), s.last))

    def send_status(self, force=False):
        s = self.focus()
        if not s:
            line = board_msg("STATUS", st="idle")
        else:
            t = self.tokens.get(s.transcript) if s.transcript else None
            done, total = s.todo or tuple(s.tasks)
            ctx = t.context_percent() if t else None
            line = board_msg(
                "STATUS", st=s.st, proj=s.proj, tool=s.tool, target=ascii_text(s.target, 60),
                step1=ascii_text(s.steps[0], 40) if len(s.steps) > 0 else "",
                step2=ascii_text(s.steps[1], 40) if len(s.steps) > 1 else "",
                tdone=done, ttotal=total,
                el=int(time.time() - s.turn_start) if s.turn_start and s.st in ("think", "work") else 0,
                tok=t.tokens() if t else 0, ctx=-1 if ctx is None else ctx,
                model=model_name(t.model) if t and t.model else "", dur=int(s.dur), msg=s.msg,
            )
        # "el" changes every second; the board counts it up by itself, so compare without it
        key = re.sub(r"\tel=\d+", "", line)
        if force or key != self.last_status:
            self.last_status = key
            self.board.send(line)

    def refresh(self):
        head = self.queue[0] if self.queue else None
        if head and self.shown != head.id:
            self.shown = head.id
            self.board.send(head.message(len(self.queue) - 1))
        elif not head and self.shown:
            self.board.send(board_msg("CLEAR", id=self.shown))
            self.shown = None
        self.send_status()

    def send_everything(self):
        with lock:
            self.board.send(board_msg("PING", hm=datetime.now().strftime("%H:%M")))
            self.shown = None
            self.send_status(force=True)
            self.refresh()
        self.board.send(board_msg("STATS", **self.tokens.today()))

    # -- prompts

    def resolve(self, p, answer, why):
        with lock:
            if p.done.is_set():
                return
            if p in self.queue:
                self.queue.remove(p)
            p.answer = answer
            p.done.set()
            if self.shown == p.id and why != "board":
                self.board.send(board_msg("CLEAR", id=p.id))
                self.shown = None
            if self.shown == p.id:
                self.shown = None
            log.info("prompt %s (%s) -> %s [%s]", p.id, p.kind, answer, why)
            self.refresh()

    def resolve_where(self, why, match):
        for p in [p for p in self.queue if match(p)]:
            self.resolve(p, None, why)

    def wait_for(self, p, conn, deadline=None):
        with lock:
            p.id = str(self.next_id)
            self.next_id += 1
            self.queue.append(p)
            log.info("prompt %s (%s) %s", p.id, p.kind, p.fields.get("title") or p.fields.get("q"))
            self.refresh()
        while not p.done.wait(0.2):
            if client_gone(conn):
                self.resolve(p, None, "hook closed")
            elif deadline and time.time() > deadline:
                self.resolve(p, None, "timeout")
            elif p.kind == "ask" and not self.board.connected():
                self.resolve(p, None, "no board")
        return p.answer

    def permission(self, s, ev, conn):
        tool, ti = ev.get("tool_name", ""), ev.get("tool_input") or {}
        always = [u for u in ev.get("permission_suggestions") or []
                  if u.get("type") == "addRules" and u.get("behavior", "allow") == "allow"]
        risky = is_risky(tool, ti)
        rules = ", ".join(rule_text(r) for u in always for r in u.get("rules", []))
        p = Pending("perm", s.sid, dict(
            proj=s.proj, title=perm_title(tool), desc=ascii_text(perm_desc(tool, ti), 80),
            body=ascii_text(perm_body(tool, ti), 600), risky=int(risky), always="" if risky else ascii_text(rules, 80),
        ), tool_use_id=s.tool_ids.get(tool_key(tool, ti)))
        answer = self.wait_for(p, conn)
        if answer in ("allow", "always"):
            decision = {"behavior": "allow"}
            if answer == "always" and always and not risky:
                decision["updatedPermissions"] = always
            return {"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": decision}}
        if answer == "deny":
            return {"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": {
                "behavior": "deny", "message": "Denied on the Claude Buddy board."}}}
        return {}  # answered in the app, or it gave up

    def question(self, s, ev, conn):
        if not self.board.connected():
            return {}  # no board: let the app ask right away
        ti = dict(ev.get("tool_input") or {})
        questions = ti.get("questions") or []
        answers = {}
        for n, q in enumerate(questions, 1):
            labels = [o.get("label", "") for o in q.get("options", [])][:8]
            fields = dict(proj=s.proj, n=n, of=len(questions), tmo=ASK_TIMEOUT_S,
                          multi=int(bool(q.get("multiSelect"))), q=ascii_text(q.get("question", ""), 300))
            fields.update({f"o{i}": ascii_text(label, 60) for i, label in enumerate(labels, 1)})
            answer = self.wait_for(Pending("ask", s.sid, fields), conn, deadline=time.time() + ASK_TIMEOUT_S)
            picked = [labels[int(i) - 1] for i in (answer or "").split(",") if i.isdigit() and 0 < int(i) <= len(labels)]
            if not picked:
                return {}  # "Answer on PC", timeout, or no board: the app shows the question
            answers[q.get("question", "")] = ", ".join(picked)
        ti["answers"] = answers
        return {"hookSpecificOutput": {"hookEventName": "PreToolUse", "permissionDecision": "allow", "updatedInput": ti}}

    # -- hook events

    def handle(self, ev, conn, ask_endpoint=False):
        name = ev.get("hook_event_name", "")
        tool = ev.get("tool_name", "")
        if name in ("SubagentStart", "SubagentStop") and not ev.get("agent_type"):
            return {}  # Claude Code's own helpers
        with lock:
            s = self.sessions.get(ev.get("session_id", "?"))
            if s is None:
                s = self.sessions[ev.get("session_id", "?")] = Session(ev.get("session_id", "?"))
            s.last = time.time()
            if ev.get("cwd"):
                s.proj = Path(ev["cwd"]).name
            if ev.get("transcript_path"):
                s.transcript = ev["transcript_path"]
            if not ask_endpoint:
                self.apply(s, name, tool, ev)
                self.refresh()
        if ask_endpoint:
            return self.question(s, ev, conn) if tool == "AskUserQuestion" else {}
        if name == "PermissionRequest":
            return self.permission(s, ev, conn)
        return {}

    def apply(self, s, name, tool, ev):
        ti = ev.get("tool_input") or {}
        now = time.time()
        if name == "UserPromptSubmit":
            self.resolve_where("new prompt", lambda p: p.sid == s.sid)
            s.st, s.turn_start, s.msg, s.tool, s.target = "think", now, "", "", ""
            s.steps.clear()
            if s.todo and s.todo[0] == s.todo[1]:
                s.todo = None
            if s.tasks[0] >= s.tasks[1]:
                s.tasks = [0, 0]
        elif name == "PreToolUse":
            s.st, s.tool, s.target = "work", tool_label(tool), tool_target(tool, ti)
            if s.turn_start is None:
                s.turn_start = now
            if ev.get("tool_use_id"):
                s.tool_ids[tool_key(tool, ti)] = ev["tool_use_id"]
                while len(s.tool_ids) > 50:
                    s.tool_ids.pop(next(iter(s.tool_ids)))
            if tool == "TodoWrite":
                todos = ti.get("todos") or []
                s.todo = (sum(t.get("status") == "completed" for t in todos), len(todos))
        elif name in ("PostToolUse", "PostToolUseFailure"):
            s.st = "think"
            s.steps.appendleft(f"{tool_label(tool)}  {tool_target(tool, ti)}".strip())
            tid = ev.get("tool_use_id")
            self.resolve_where("answered in the app", lambda p: p.kind == "perm" and p.sid == s.sid and p.tool_use_id == tid)
        elif name == "PermissionDenied":
            tid = ev.get("tool_use_id") or s.tool_ids.get(tool_key(tool, ti))
            self.resolve_where("denied", lambda p: p.kind == "perm" and p.sid == s.sid and p.tool_use_id == tid)
        elif name == "Stop":
            self.resolve_where("turn ended", lambda p: p.sid == s.sid)
            s.st, s.dur, s.turn_start = "done", (now - s.turn_start) if s.turn_start else 0, None
        elif name == "StopFailure":
            self.resolve_where("turn failed", lambda p: p.sid == s.sid)
            err = ev.get("error", "")
            s.st, s.msg, s.turn_start = "err", FAILURES.get(err, f"Stopped: {err}"), None
        elif name == "SessionEnd":
            self.resolve_where("session ended", lambda p: p.sid == s.sid)
            self.sessions.pop(s.sid, None)
        elif name == "TaskCreated":
            s.tasks[1] += 1
        elif name == "TaskCompleted":
            s.tasks[0] += 1
        elif name == "PreCompact":
            s.st, s.tool, s.target = "work", "Compact", "conversation"

    # -- background upkeep

    def tick(self):
        """Every second: notice prompts answered in the app, end stale turns, keep the board fresh."""
        with lock:
            for p in list(self.queue):
                s = self.sessions.get(p.sid)
                if p.kind == "perm" and p.tool_use_id and s and s.transcript:
                    if p.tool_use_id in self.tokens.get(s.transcript).results:
                        self.resolve(p, None, "answered in the app")
            now = time.time()
            for s in list(self.sessions.values()):
                if s.st in ("think", "work") and now - s.last > STALE_TURN_S:
                    s.st, s.turn_start = "idle", None
                if now - s.last > FORGET_SESSION_S and not any(p.sid == s.sid for p in self.queue):
                    del self.sessions[s.sid]  # closed without a SessionEnd
            self.send_status()


# ---------------------------------------------------------------- HTTP side

class Server(ThreadingHTTPServer):
    allow_reuse_address = False  # on Windows reuse would let two bridges share the port
    daemon_threads = True


class Handler(BaseHTTPRequestHandler):
    hub = None
    board = None

    def do_POST(self):
        raw = self.rfile.read(int(self.headers.get("Content-Length") or 0))
        path = self.path.split("?")[0]
        if path in ("/hook", "/ask"):
            try:
                ev = json.loads(raw or b"{}")
                reply = self.hub.handle(ev, self.connection, ask_endpoint=path == "/ask")
            except Exception:
                log.exception("hook failed")
                reply = {}
        elif path == "/pause":
            self.board.pause(PAUSE_S)
            reply = {"ok": True, "paused_s": PAUSE_S}
        elif path == "/resume":
            self.board.resume()
            reply = {"ok": True}
        elif path == "/stop":
            reply = {"ok": True}
            threading.Thread(target=lambda: (time.sleep(0.3), os._exit(0)), daemon=True).start()
        else:
            self.send_error(404)
            return
        self.reply(reply)

    def do_GET(self):
        if self.path.split("?")[0] != "/status":
            self.send_error(404)
            return
        with lock:
            reply = {
                "board": self.board.ser.port if self.board.ser else None,
                "paused": self.board.paused_until > time.time(),
                "sessions": [{"proj": s.proj, "st": s.st, "tool": s.tool, "target": s.target} for s in self.hub.sessions.values()],
                "prompts": [{"id": p.id, "kind": p.kind, **p.fields} for p in self.hub.queue],
            }
        self.reply(reply)

    def reply(self, obj):
        body = json.dumps(obj).encode()
        try:
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except OSError:
            pass  # Claude Code stopped waiting; nothing to do

    def log_message(self, *_):
        pass


# ---------------------------------------------------------------- main

def control(command):
    method = "GET" if command == "status" else "POST"
    req = urllib.request.Request(f"http://127.0.0.1:{PORT}/{command}", method=method, data=None if method == "GET" else b"")
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            print(json.dumps(json.load(r), indent=2))
    except OSError:
        print("The bridge isn't running.")
        sys.exit(1)


def setup_logging():
    log.setLevel(logging.DEBUG if "--debug" in sys.argv else logging.INFO)
    fmt = logging.Formatter("%(asctime)s %(message)s", "%H:%M:%S")
    handler = RotatingFileHandler(HERE / "bridge.log", maxBytes=1_000_000, backupCount=1, encoding="utf-8")
    handler.setFormatter(fmt)
    log.addHandler(handler)
    if sys.stdout:
        console = logging.StreamHandler(sys.stdout)
        console.setFormatter(fmt)
        log.addHandler(console)


def main():
    commands = [a for a in sys.argv[1:] if not a.startswith("--")]
    if commands:
        control(commands[0])
        return
    setup_logging()
    try:
        server = Server(("127.0.0.1", PORT), Handler)
    except OSError:
        log.info("Another bridge is already running on port %d.", PORT)
        return

    board, tokens = Board(), Tokens()
    hub = Hub(board, tokens)
    Handler.hub, Handler.board = hub, board

    def on_line(line):
        cmd, fields = parse_board_line(line)
        if cmd == "HELLO":
            hub.send_everything()
        elif cmd == "ANS":
            with lock:
                p = next((p for p in hub.queue if p.id == fields.get("id")), None)
            if p:
                hub.resolve(p, fields.get("c"), "board")

    board.on_line = on_line
    board.on_connect = hub.send_everything
    threading.Thread(target=board.run, daemon=True).start()

    def upkeep():
        n = 0
        while True:
            time.sleep(1)
            n += 1
            try:
                hub.tick()
                if n % 2 == 0:
                    board.send(board_msg("PING", hm=datetime.now().strftime("%H:%M")))
                if n % 30 == 0:
                    board.send(board_msg("STATS", **tokens.today()))
            except Exception:
                log.exception("upkeep failed")

    threading.Thread(target=upkeep, daemon=True).start()
    log.info("Claude Buddy bridge listening on http://127.0.0.1:%d", PORT)
    server.serve_forever(poll_interval=0.5)


if __name__ == "__main__":
    main()
