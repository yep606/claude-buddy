"""Install Claude Buddy on this PC.

    python install.py            add the hooks, start with Windows, start the bridge now
    python install.py --remove   undo all of that
    python install.py --project DIR [--remove]
                                 hooks for one project folder only (its .claude/settings.local.json),
                                 no autostart; run the bridge yourself with: python bridge.py

Adds HTTP hooks to ~/.claude/settings.json (a backup is made first) so every
Claude Code session reports to the bridge, puts a "Claude Buddy" shortcut in the
Startup folder, and writes "Pause buddy for upload.cmd" next to this file.
Run it with the Python that has pyserial installed.
"""
import base64
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.request
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
BRIDGE = HERE / "bridge.py"
SETTINGS = Path.home() / ".claude" / "settings.json"
PYTHONW = Path(sys.executable).with_name("pythonw.exe")
STARTUP = Path(os.environ["APPDATA"]) / "Microsoft" / "Windows" / "Start Menu" / "Programs" / "Startup" / "Claude Buddy.lnk"
PAUSE_CMD = HERE / "Pause buddy for upload.cmd"
HOOK_URL = "http://127.0.0.1:8737/hook"
ASK_URL = "http://127.0.0.1:8737/ask"

STATUS_EVENTS = ("SessionStart", "SessionEnd", "UserPromptSubmit", "PreToolUse", "PostToolUse",
                 "PostToolUseFailure", "PermissionDenied", "Stop", "StopFailure", "TaskCreated",
                 "TaskCompleted", "PreCompact")


def entry(url, timeout, matcher=None):
    group = {"hooks": [{"type": "http", "url": url, "timeout": timeout}]}
    if matcher:
        group["matcher"] = matcher
    return group


def our_hooks():
    hooks = {name: [entry(HOOK_URL, 5)] for name in STATUS_EVENTS}
    # Runs alongside the app's own dialog, so a long wait never blocks you
    hooks["PermissionRequest"] = [entry(HOOK_URL, 600)]
    # Questions are held by the board for up to 30 s per question
    hooks["PreToolUse"].append(entry(ASK_URL, 150, "AskUserQuestion"))
    return hooks


def strip_ours(settings):
    hooks = settings.get("hooks", {})
    for name in list(hooks):
        kept = []
        for group in hooks[name]:
            rest = [h for h in group.get("hooks", []) if h.get("url") not in (HOOK_URL, ASK_URL)]
            if rest:
                kept.append({**group, "hooks": rest})
        if kept:
            hooks[name] = kept
        else:
            del hooks[name]
    if "hooks" in settings and not hooks:
        del settings["hooks"]


def update_settings(add):
    settings = json.loads(SETTINGS.read_text(encoding="utf-8")) if SETTINGS.exists() else {}
    if SETTINGS.exists():
        backup = SETTINGS.with_name(f"{SETTINGS.name}.buddy-backup-{datetime.now():%Y%m%d-%H%M%S}")
        shutil.copy2(SETTINGS, backup)
        print(f"Backed up settings to {backup}")
    strip_ours(settings)
    if add:
        for name, groups in our_hooks().items():
            settings.setdefault("hooks", {}).setdefault(name, []).extend(groups)
    SETTINGS.parent.mkdir(parents=True, exist_ok=True)
    SETTINGS.write_text(json.dumps(settings, indent=2) + "\n", encoding="utf-8")
    print(("Added" if add else "Removed") + f" Claude Buddy hooks in {SETTINGS}")


def powershell(script):
    encoded = base64.b64encode(script.encode("utf-16-le")).decode()
    subprocess.run(["powershell", "-NoProfile", "-EncodedCommand", encoded], check=True)


def make_shortcut():
    q = lambda p: str(p).replace("'", "''")
    powershell(
        f"$s = (New-Object -ComObject WScript.Shell).CreateShortcut('{q(STARTUP)}');"
        f"$s.TargetPath = '{q(PYTHONW)}';"
        f"$s.Arguments = '\"{q(BRIDGE)}\"';"
        f"$s.WorkingDirectory = '{q(HERE)}';"
        f"$s.Description = 'Claude Buddy bridge';"
        f"$s.Save()"
    )
    print(f"Added startup shortcut {STARTUP}")


def write_pause_cmd():
    PAUSE_CMD.write_text(
        "@echo off\r\n"
        "rem Frees the board's COM port for 3 minutes so the Arduino IDE can upload.\r\n"
        f'"{sys.executable}" "%~dp0bridge.py" pause\r\n'
        "timeout /t 4 >nul\r\n",
        encoding="ascii",
    )
    print(f"Wrote {PAUSE_CMD.name}")


def stop_bridge():
    try:
        urllib.request.urlopen(urllib.request.Request("http://127.0.0.1:8737/stop", data=b"", method="POST"), timeout=3)
        time.sleep(1.5)
        print("Stopped the running bridge")
    except OSError:
        pass


def start_bridge():
    flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
    subprocess.Popen([str(PYTHONW), str(BRIDGE)], cwd=HERE, creationflags=flags, close_fds=True)
    print("Started the bridge in the background")


def main():
    global SETTINGS
    if "--project" in sys.argv:
        project = Path(sys.argv[sys.argv.index("--project") + 1]).resolve()
        SETTINGS = project / ".claude" / "settings.local.json"
        update_settings(add="--remove" not in sys.argv)
        return
    if "--remove" in sys.argv:
        update_settings(add=False)
        STARTUP.unlink(missing_ok=True)
        print("Removed the startup shortcut")
        stop_bridge()
        return
    try:
        import serial  # noqa: F401
    except ImportError:
        sys.exit(f"pyserial is missing for {sys.executable}. Run:\n  \"{sys.executable}\" -m pip install --user pyserial")
    if not PYTHONW.exists():
        sys.exit(f"Can't find {PYTHONW}; run this with a python.org Python.")
    update_settings(add=True)
    make_shortcut()
    write_pause_cmd()
    stop_bridge()
    start_bridge()
    print("\nDone. New and running Claude Code sessions now report to the board.")


if __name__ == "__main__":
    main()
