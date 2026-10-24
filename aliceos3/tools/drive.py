#!/usr/bin/env python3
"""Drive AliceOS in a headless QEMU: move the mouse, click, type, take screenshots.
usage: drive.py <iso> <outdir> <script>
script lines: wait S | move X Y | click | dblclick | down | up | drag X Y | type TEXT | key NAME | shot NAME
"""
import socket, subprocess, sys, time, os
from PIL import Image

iso, outdir, script = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(outdir, exist_ok=True)
sock = "/tmp/aliceos-mon.sock"
if os.path.exists(sock): os.remove(sock)
REC = os.environ.get("REC")          # directory: record frames for a video
rsock = "/tmp/aliceos-rec.sock"
args = ["qemu-system-i386", "-cdrom", iso, "-m", "256", "-display", "none",
        "-monitor", f"unix:{sock},server,nowait", "-rtc", "base=localtime"]
if REC:
    if os.path.exists(rsock): os.remove(rsock)
    args += ["-monitor", f"unix:{rsock},server,nowait"]
q = subprocess.Popen(args)
for _ in range(50):
    if os.path.exists(sock): break
    time.sleep(0.1)
s = socket.socket(socket.AF_UNIX); s.connect(sock); s.settimeout(0.3)

frames = []
recording = [bool(REC)]
if REC:
    import threading
    os.makedirs(REC, exist_ok=True)
    def recorder():
        for _ in range(50):
            if os.path.exists(rsock): break
            time.sleep(0.1)
        r = socket.socket(socket.AF_UNIX); r.connect(rsock); r.settimeout(5)
        buf = b""
        def wait_prompt():
            nonlocal buf
            while b"(qemu)" not in buf:
                buf += r.recv(65536)
            buf = b""
        wait_prompt()
        n = 0
        while recording[0]:
            f = os.path.join(REC, f"f{n:05d}.ppm")
            r.send(f"screendump {f}\n".encode()); wait_prompt()
            frames.append((time.time(), f)); n += 1
            time.sleep(0.04)
    th = threading.Thread(target=recorder, daemon=True); th.start()


def mon(c, pause=0.02):
    s.send((c + "\n").encode()); time.sleep(pause)
    try: s.recv(65536)
    except Exception: pass


pos = [512, 384]


def move(x, y):
    dx, dy = x - pos[0], y - pos[1]
    while dx or dy:
        st = 14 if REC else 40
        sx = max(-st, min(st, dx)); sy = max(-st, min(st, dy))
        mon(f"mouse_move {sx} {sy}", 0.03 if REC else 0.015)
        dx -= sx; dy -= sy
    pos[0], pos[1] = x, y
    time.sleep(0.15)


KEYS = {' ': 'spc', '\n': 'ret', '-': 'minus', '=': 'equal', '*': 'shift-8', '+': 'shift-equal', '/': 'slash',
        '(': 'shift-9', ')': 'shift-0', '.': 'dot', ',': 'comma', '!': 'shift-1', ':': 'shift-semicolon',
        "'": 'apostrophe', '%': 'shift-5', '@': 'shift-2', '?': 'shift-slash',
        '>': 'shift-dot', '<': 'shift-comma', '~': 'shift-grave_accent', '|': 'shift-backslash', '"': 'shift-apostrophe',
        '_': 'shift-minus', '&': 'shift-7', '#': 'shift-3', '$': 'shift-4', '^': 'shift-6'}


def typ(t):
    for ch in t:
        k = KEYS.get(ch, ch)
        if ch.isupper(): k = 'shift-' + ch.lower()
        mon("sendkey " + k, 0.08 if REC else 0.03)


for line in open(script).read().splitlines():
    line = line.strip()
    if not line or line.startswith('#'): continue
    cmd, _, arg = line.partition(' ')
    if cmd == 'wait': time.sleep(float(arg))
    elif cmd == 'move': x, y = map(int, arg.split()); move(x, y)
    elif cmd == 'click': mon("mouse_button 1", .08); mon("mouse_button 0", .25)
    elif cmd == 'rclick': mon("mouse_button 2", .08); mon("mouse_button 0", .3)
    elif cmd == 'wheel': mon(f"mouse_move 0 0 {arg}", .2)
    elif cmd == 'dblclick':
        mon("mouse_button 1", .05); mon("mouse_button 0", .08); mon("mouse_button 1", .05); mon("mouse_button 0", .4)
    elif cmd == 'down': mon("mouse_button 1", .1)
    elif cmd == 'up': mon("mouse_button 0", .2)
    elif cmd == 'drag':
        x, y = map(int, arg.split())
        mon("mouse_button 1", .1)
        dx, dy = x - pos[0], y - pos[1]
        while dx or dy:
            sx = max(-12, min(12, dx)); sy = max(-12, min(12, dy))
            s.send(f"mouse_move {sx} {sy}\n".encode()); time.sleep(0.03)
            try: s.recv(65536)
            except Exception: pass
            dx -= sx; dy -= sy
        pos[0], pos[1] = x, y
        time.sleep(0.2); mon("mouse_button 0", .3)
    elif cmd == 'alttab':
        mon("sendkey alt-tab 1600", 0.6)
        tmp = "/tmp/aliceos-shot.ppm"; mon(f"screendump {tmp}", 0.4)
        Image.open(tmp).save(os.path.join(outdir, arg + ".png")); print("shot", arg); time.sleep(1.2)
    elif cmd == 'type': typ(arg.replace('\\n', '\n'))
    elif cmd == 'key': mon("sendkey " + arg, .05)
    elif cmd == 'shot':
        tmp = "/tmp/aliceos-shot.ppm"
        mon(f"screendump {tmp}", 0.6)
        Image.open(tmp).save(os.path.join(outdir, arg + ".png"))
        print("shot", arg)
if REC:
    recording[0] = False; time.sleep(1)
    with open(os.path.join(REC, "list.txt"), "w") as L:
        for i, (t, f) in enumerate(frames):
            d = (frames[i + 1][0] - t) if i + 1 < len(frames) else 1.5
            L.write(f"file '{f}'\nduration {d:.3f}\n")
        L.write(f"file '{frames[-1][1]}'\n")
    print("frames", len(frames))
q.kill()
