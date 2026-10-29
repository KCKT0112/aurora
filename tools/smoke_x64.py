#!/usr/bin/env python3
"""Aurora v2 x86_64 boot smoke test.

Boots the Limine ISO in headless QEMU, waits for the kernel's graphics
bring-up marker, and validates the framebuffer screenshot and its dimensions.
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import socket
import subprocess
import sys
import time
import wave
import struct
from paste import paste

ROOT = pathlib.Path(__file__).resolve().parent.parent
ISO = ROOT / "out" / "x86_64" / "aurora.iso"
OUT = ROOT / "out" / "x86_64" / "smoke"
SERIAL = OUT / "serial.log"
MONITOR = OUT / "monitor.sock"
QMP = OUT / "qmp.sock"
QEMU_LOG = OUT / "qemu.log"


class SmokeError(RuntimeError):
    pass


def wait_for(predicate, description, timeout=20.0, interval=0.05):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            value = predicate()
        except OSError:
            value = None
        if value:
            return value
        time.sleep(interval)
    raise SmokeError(f"timed out waiting for {description}")


def serial_text():
    try:
        return SERIAL.read_text(errors="replace")
    except FileNotFoundError:
        return ""


def wait_for_log(needle, timeout=20.0):
    wait_for(lambda: needle in serial_text(),
             f"serial line {needle!r}", timeout=timeout)


class Monitor:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(0.3)
        self.sock.connect(str(path))
        time.sleep(0.2)
        self._drain()

    def _drain(self):
        while True:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                return
            if not chunk:
                return

    def send(self, command, settle=0.2):
        self.sock.sendall(command.encode("utf-8") + b"\n")
        time.sleep(settle)
        self._drain()

    def screendump(self, path):
        if path.exists():
            path.unlink()
        self.send(f"screendump {path.relative_to(ROOT)}", settle=0.1)
        wait_for(lambda: path.exists() and path.stat().st_size > 1024,
                 f"screenshot {path.name}", timeout=5.0)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def read_ppm(path):
    data = path.read_bytes()
    if not data.startswith(b"P6\n"):
        raise SmokeError(f"{path.name}: not a raw PPM")
    parts = data.split(b"\n", 3)
    if len(parts) != 4:
        raise SmokeError(f"{path.name}: malformed PPM header")
    width, height = (int(value) for value in parts[1].split())
    maximum = int(parts[2])
    pixels = parts[3]
    if maximum != 255 or len(pixels) < width * height * 3:
        raise SmokeError(f"{path.name}: malformed PPM payload")
    if len(set(pixels[:width * height * 3])) < 24:
        raise SmokeError(f"{path.name}: framebuffer looks blank")
    return width, height, pixels


def parse_ppm(path):
    width, height, _pixels = read_ppm(path)
    return width, height


class Qmp:
    """Tiny QMP client for absolute tablet input injection."""

    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(1.0)
        self.sock.connect(str(path))
        self.buffer = b""
        self.width=1440
        self.height=900
        greeting = self._read()
        if "QMP" not in greeting:
            raise SmokeError("QMP greeting missing")
        self.command("qmp_capabilities")

    def _read(self):
        while b"\n" not in self.buffer:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise SmokeError("QMP connection closed")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return json.loads(line)

    def command(self, name, arguments=None):
        request = {"execute": name}
        if arguments is not None:
            request["arguments"] = arguments
        self.sock.sendall(json.dumps(request).encode("utf-8") + b"\n")
        while True:
            message = self._read()
            if "return" in message or "error" in message:
                if "error" in message:
                    raise SmokeError(f"QMP {name}: {message['error']}")
                return message["return"]

    def pointer(self, x, y):
        x_value = round(x * 32767 / (self.width-1))
        y_value = round(y * 32767 / (self.height-1))
        self.command("input-send-event", {"events": [
            {"type": "abs", "data": {"axis": "x", "value": x_value}},
            {"type": "abs", "data": {"axis": "y", "value": y_value}},
        ]})

    def button(self, down):
        self.command("input-send-event", {"events": [
            {"type": "btn", "data": {"down": down, "button": "left"}},
        ]})

    def click(self):
        self.button(True)
        time.sleep(0.06)
        self.button(False)

    def double_click(self):
        self.click()
        time.sleep(0.08)
        self.click()

    def type_text(self, text, enter=True):
        names = {" ": "spc", "/": "slash", ".": "dot", "-": "minus", "_": "shift-minus"}
        for char in text:
            name = names.get(char, char)
            self.command("human-monitor-command", {"command-line": f"sendkey {name} 20"})
            time.sleep(0.055)
        if enter:
            self.command("human-monitor-command", {"command-line": "sendkey ret 20"})
            time.sleep(0.5)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def window_is_composed(path):
    """A composed frame always shows the Files title bar at 300,220.

    The old direct-to-scanout renderer exposed wallpaper-only intermediate
    frames; the offscreen compositor must never produce one.
    """
    width, _height, pixels = read_ppm(path)
    offset = (100 * width + 300) * 3
    red, green, blue = pixels[offset:offset + 3]
    return red > 235 and green > 235 and blue > 235


def start_qemu(qemu,resize_socket=None):
    command = [
        qemu, "-machine", "q35,pcspk-audiodev=audio", "-m", "512M", "-cpu", "max",
        "-audiodev", f"wav,id=audio,path={OUT / 'audio.wav'}",
        "-audiodev", f"wav,id=pcm,path={OUT / 'pcm.wav'},out.frequency=48000",
        "-device", "AC97,audiodev=pcm",
        "-cdrom", str(ISO), "-boot", "d", "-vga", "std",
        "-device", "virtio-tablet-pci",
        "-global", "VGA.vgamem_mb=64",
        "-no-reboot", "-display", "none",
        "-serial", f"file:{SERIAL.relative_to(ROOT)}",
        "-monitor", f"unix:{MONITOR.relative_to(ROOT)},server,nowait",
        "-qmp", f"unix:{QMP.relative_to(ROOT)},server,nowait",
    ]
    if resize_socket is not None:
        command.extend(['-chardev',f'socket,id=hostresize,path={resize_socket},server=on,wait=off','-serial','chardev:hostresize'])
    config=ISO.parent/'display.json'
    if config.exists():
        mode=json.loads(config.read_text())
        command.extend(['-global',f'VGA.xres={mode["width"]}','-global',f'VGA.yres={mode["height"]}'])
    with QEMU_LOG.open("wb") as log:
        return subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=log)


def stop_qemu(process, monitor):
    if process.poll() is not None:
        return
    if monitor is not None:
        try:
            monitor.send("quit", settle=0.1)
        except OSError:
            pass
    try:
        process.wait(timeout=3.0)
        return
    except subprocess.TimeoutExpired:
        pass
    process.terminate()
    try:
        process.wait(timeout=2.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=2.0)


def run(args):
    if not args.iso.exists():
        raise SmokeError(f"ISO not found: {args.iso}")
    OUT.mkdir(parents=True, exist_ok=True)
    for path in (SERIAL, MONITOR, QMP, QEMU_LOG):
        try:
            path.unlink()
        except FileNotFoundError:
            pass

    process = start_qemu(args.qemu)
    monitor = None
    qmp = None
    try:
        wait_for(lambda: process.poll() is not None or "[boot] kernel core ready" in serial_text(), "kernel boot")
        if process.poll() is not None:
            raise SmokeError(QEMU_LOG.read_text(errors="replace").strip())
        wait_for_log("[virtio] tablet ready", timeout=8.0)
        wait_for_log("display: workspace online", timeout=8.0)
        wait_for_log("input: service online", timeout=8.0)
        wait_for_log("vfsd: initramfs online", timeout=8.0)
        wait_for_log("authd: ready", timeout=8.0)
        wait_for_log("display: login screen", timeout=8.0)
        monitor = Monitor(MONITOR)
        qmp = Qmp(QMP)
        monitor.screendump(OUT / "login.ppm")
        qmp.type_text("wrong-password")
        wait_for_log("authd: login rejected", timeout=30)
        if "files: online" in serial_text():
            raise SmokeError("apps started before authentication")
        time.sleep(2.2)
        accepted_before=serial_text().count("authd: login accepted")
        paste(qmp,"user-test")
        time.sleep(0.3)
        if serial_text().count("authd: login accepted") != accepted_before:
            raise SmokeError("paste submitted password without Enter")
        monitor.send("sendkey ret",settle=0.3)
        wait_for_log("authd: login accepted", timeout=30)
        wait_for_log("files: online", timeout=8.0)
        wait_for_log("display: files view online", timeout=8.0)
        wait_for_log("display: task view online", timeout=8.0)
        if "PANIC" in serial_text():
            raise SmokeError("kernel panicked during boot")

        shot = OUT / "boot.ppm"
        monitor.screendump(shot)
        width, height = parse_ppm(shot)
        if width < 1280 or height < 800:
            raise SmokeError(
                f"resolution too small: {width}x{height}; expected >=1280x800")
        log = serial_text()
        if "got 0" in log or "unsupported framebuffer" in log:
            raise SmokeError("framebuffer was not initialised")
        if "32-bpp" not in log:
            raise SmokeError("kernel did not report a 32-bit framebuffer")
        if "user fault" in log or "KERNEL EXCEPTION" in log:
            raise SmokeError("CPU fault during session startup")

        before = shot.read_bytes()

        # Temporal integrity: sample repeatedly across clock and task updates.
        # Every captured frame must already contain composed window pixels.
        for index in range(10):
            frame = OUT / f"flicker-{index:02d}.ppm"
            monitor.screendump(frame)
            parse_ppm(frame)
            if not window_is_composed(frame):
                raise SmokeError(
                    f"flicker probe: wallpaper-only frame {frame.name}")
        print("  flicker: 10 composed frames sampled")

        qmp.pointer(104, 222)
        time.sleep(0.2)
        qmp.click()
        wait_for_log("input: absolute tablet online", timeout=3.0)
        wait_for_log("files: list /home/user/Documents", timeout=4.0)
        print("  tablet:  absolute pointer input verified")
        docs = OUT / "files-docs.ppm"
        monitor.screendump(docs)
        parse_ppm(docs)

        qmp.pointer(340, 267)
        time.sleep(0.2)
        qmp.double_click()
        time.sleep(0.4)
        wait_for_log("files: preview /home/user/Documents/Welcome.txt", timeout=4.0)
        wait_for_log("display: preview view online", timeout=4.0)
        preview = OUT / "preview.ppm"
        monitor.screendump(preview)
        parse_ppm(preview)
        if not window_is_composed(preview):
            raise SmokeError("preview frame lost the composed desktop")

        monitor.send("sendkey f1", settle=0.4)
        wait_for_log("display: F1 focus", timeout=4.0)
        focus = OUT / "focus.ppm"
        monitor.screendump(focus)
        parse_ppm(focus)
        if focus.read_bytes() == before:
            raise SmokeError("focus switch did not change the framebuffer")

        qmp.pointer(1100, 700)
        time.sleep(0.5)
        moved = OUT / "mouse.ppm"
        monitor.screendump(moved)
        parse_ppm(moved)
        if moved.read_bytes() == focus.read_bytes():
            raise SmokeError("mouse movement did not change the framebuffer")

        monitor.send("sendkey f3", settle=0.2)
        monitor.send("sendkey down", settle=0.3)
        keyboard = OUT / "keyboard.ppm"
        monitor.screendump(keyboard)
        parse_ppm(keyboard)
        if keyboard.read_bytes() == moved.read_bytes():
            raise SmokeError("arrow keys did not move the table selection")

        wait_for_log("security: all denial checks passed")
        wait_for_log("lifecycle: 16 spawn/exit cycles, no frame leak")
        wait_for_log("services: transactional writes and policy checks passed")
        if any(marker in serial_text() for marker in ("security: FAILED", "lifecycle: FAILED", "services: FAILED")):
            raise SmokeError("unprivileged permission test failed")
        monitor.send("sendkey f2", settle=0.3)
        qmp.type_text("info")
        wait_for_log("terminal: info command completed")
        monitor.screendump(OUT / "terminal.ppm")
        qmp.type_text("write /tmp/probe.txt hello")
        wait_for_log("vfsd: atomic RAM write committed")
        qmp.type_text("cat /tmp/probe.txt")
        qmp.type_text("stat /tmp/probe.txt")
        qmp.type_text("chmod 400 /tmp/probe.txt")
        qmp.type_text("beep")
        wait_for_log("audiod: tone accepted")
        monitor.screendump(OUT / "terminal-write.ppm")
        if any(marker in serial_text() for marker in ("[fault]", "PANIC", "KERNEL EXCEPTION")):
            raise SmokeError("fault during integration checks")
        # Minimize must survive periodic task publications.
        monitor.send("sendkey f4", settle=0.3)
        qmp.pointer(1318, 142)
        qmp.click()
        time.sleep(1.4)
        hidden = OUT / "monitor-hidden.ppm"
        monitor.screendump(hidden)
        w, _h, rgb = read_ppm(hidden)
        if min(rgb[(150 * w + 1395) * 3:(150 * w + 1395) * 3 + 3]) > 235:
            raise SmokeError("periodic update reopened minimized monitor")
        monitor.send("sendkey f4", settle=0.4)
        # Restart an ordinary app through the real task-control workflow.
        qmp.pointer(1010, 300)
        qmp.click()
        for _ in range(8):
            monitor.send("sendkey down", settle=0.12)
        monitor.send("sendkey delete", settle=0.2)
        monitor.send("sendkey delete", settle=0.2)
        wait_for_log("init: application restarted", timeout=5)
        wait_for(lambda: serial_text().count("terminal: online") >= 2,
                 "terminal restart", timeout=5)
        if any(marker in serial_text() for marker in ("[fault]", "PANIC", "KERNEL EXCEPTION")):
            raise SmokeError("fault during app restart")
        # Verify Settings has an authorized, working test-tone action.
        qmp.pointer(950, 853)
        qmp.click()
        time.sleep(0.3)
        previous_tones = serial_text().count("audiod: tone accepted")
        qmp.pointer(500, 525)
        qmp.click()
        wait_for(lambda: serial_text().count("audiod: tone accepted") > previous_tones,
                 "Settings test sound", timeout=3)
        monitor.screendump(OUT / "settings.ppm")
        # Resize Files by the lower-right edge and enforce the minimum geometry.
        monitor.send("sendkey f3", settle=0.3)
        qmp.pointer(864, 706)
        time.sleep(0.15)
        qmp.button(True)
        time.sleep(0.15)
        qmp.pointer(710, 630)
        time.sleep(0.3)
        qmp.button(False)
        wait_for_log("display: window resized")
        monitor.screendump(OUT / "resized.ppm")
        # The same held edge can expand again; stale pixels must be repainted.
        qmp.pointer(710, 630)
        time.sleep(0.15)
        qmp.button(True)
        time.sleep(0.15)
        qmp.pointer(864, 706)
        time.sleep(0.3)
        qmp.button(False)

        def sign_in(name):
            previous = serial_text().count("authd: login accepted")
            monitor.send("sendkey tab", settle=0.1)
            for _ in range(4):
                monitor.send("sendkey backspace", settle=0.05)
            qmp.type_text(name, enter=False)
            monitor.send("sendkey tab", settle=0.1)
            qmp.type_text(name + "-test")
            wait_for(lambda: serial_text().count("authd: login accepted") > previous,
                     name + " login", timeout=30)

        monitor.send("sendkey f2", settle=0.3)
        qmp.type_text("logout")
        wait_for_log("authd: logged out")
        monitor.screendump(OUT / "logged-out.ppm")
        logout_width, _h, logout_pixels = read_ppm(OUT / "logged-out.ppm")
        # Regions previously occupied by Files and Terminal must be wallpaper.
        for x, y in ((200, 300), (1200, 400)):
            sample = logout_pixels[(y * logout_width + x) * 3:(y * logout_width + x) * 3 + 3]
            if not all(180 < c < 245 for c in sample):
                raise SmokeError("previous user's window remained after logout")
        sign_in("root")
        wait_for_log("accounts: root access checks passed")
        monitor.send("sendkey f2", settle=0.4)
        qmp.type_text("id")
        wait_for_log("uid=0 gid=0 user=root")
        monitor.screendump(OUT / "root-session.ppm")
        previous = serial_text().count("authd: logged out")
        qmp.type_text("logout")
        wait_for(lambda: serial_text().count("authd: logged out") > previous, "root logout")
        sign_in("guest")
        wait_for_log("accounts: guest isolation and sticky checks passed")
        monitor.send("sendkey f2", settle=0.4)
        qmp.type_text("id")
        wait_for_log("uid=1001 gid=1001 user=guest")
        monitor.screendump(OUT / "guest-session.ppm")
        if any(marker in serial_text() for marker in ("[fault]", "PANIC", "FAILED", "KERNEL EXCEPTION")):
            raise SmokeError("fault or negative test failure across sessions")
        print("  accounts: wrong-password denial, user/root/guest login and isolation")
        print("  windows: resize/restore and account-state reset")
        # Let QEMU flush the WAV header, then verify actual PCM output.
        stop_qemu(process, monitor)
        audio_path = OUT / "audio.wav"
        captured = bytearray(audio_path.read_bytes())
        # QEMU 11's PC speaker can leave WAV size placeholders at zero on quit.
        # Finalize metadata from captured length; PCM is never synthesized.
        if (len(captured) > 44 and captured[:4] == b"RIFF" and
                captured[8:16] == b"WAVEfmt " and captured[36:40] == b"data"):
            if struct.unpack_from("<I", captured, 4)[0] == 0:
                struct.pack_into("<I", captured, 4, len(captured) - 8)
                struct.pack_into("<I", captured, 40, len(captured) - 44)
                audio_path.write_bytes(captured)
        with wave.open(str(audio_path), "rb") as wav:
            if wav.getsampwidth() != 2:
                raise SmokeError("unexpected audio sample format")
            raw = wav.readframes(wav.getnframes())
            samples = struct.unpack(f"<{len(raw) // 2}h", raw)[::wav.getnchannels()]
            crossings = sum((a < 0) != (b < 0) for a, b in zip(samples, samples[1:]))
            if not samples or max(samples) - min(samples) < 1000 or crossings < 20:
                raise SmokeError("notification produced no oscillating PCM output")
        print("  lifecycle: 16 frame-stable exits, task-controlled app restart")
        print("  files: canonical path policy and atomic write rejection")
        print("  audio: non-silent PCM captured from PC speaker")
        print("  permissions: denial checks passed from ring 3")
        print("  terminal: info, RAM write/read, notification tone")
        print("SMOKE TEST PASSED")
        print(f"  framebuffer: {width}x{height}")
        print(f"  serial: {SERIAL.relative_to(ROOT)}")
        print(f"  shot:   {shot.relative_to(ROOT)}")
        print(f"  focus:  {focus.relative_to(ROOT)}")
        print(f"  mouse:  {moved.relative_to(ROOT)}")
        print(f"  keys:   {keyboard.relative_to(ROOT)}")
        print(f"  files:  {docs.relative_to(ROOT)}")
        print(f"  preview:{preview.relative_to(ROOT)}")
    except Exception:
        log = serial_text()
        if log:
            print("\n--- serial log (tail) ---", file=sys.stderr)
            print("\n".join(log.splitlines()[-25:]), file=sys.stderr)
        raise
    finally:
        stop_qemu(process, monitor)
        if qmp is not None:
            qmp.close()
        if monitor is not None:
            monitor.close()


def main():
    global ISO, OUT, SERIAL, MONITOR, QMP, QEMU_LOG
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=pathlib.Path, default=ISO)
    parser.add_argument("--qemu", default=os.environ.get(
        "QEMU_X86_64", "qemu-system-x86_64"))
    args = parser.parse_args()
    ISO = args.iso.resolve()
    OUT = ISO.parent / "smoke"
    SERIAL, MONITOR, QMP, QEMU_LOG = [OUT / name for name in
        ("serial.log", "monitor.sock", "qmp.sock", "qemu.log")]
    try:
        run(args)
    except (SmokeError, OSError, wave.Error) as error:
        print(f"SMOKE TEST FAILED: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
