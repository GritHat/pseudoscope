#!/usr/bin/env python3
"""Makes the demo video: the real interface, driven by keys, with captions.

Usage: make.py OUTPUT.mp4 [POSTER.png]      (needs Pillow and ffmpeg)
       make.py --preview OUTPUT.gif         (the short one, which plays by itself in a README)
       make.py --check                      (durations and chapters only)

The scenes in a terminal run the real commands: the last chapter needs rustc,
java and python3.
"""
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent.parent
BIN = ROOT / "bin" / "pseudoscope"
sys.path.insert(0, str(Path(__file__).parent))
from story import STORY, TRAILER  # noqa: E402

# Programs that write files write them here, not among the examples.
WORK = tempfile.mkdtemp(prefix="pseudoscope-demo-")

W, H, FPS = 1920, 1080, 30
COLS, ROWS, CW, CH = 124, 30, 15, 28
TX, TY = (W - COLS * CW) // 2, 60
BAND = TY + ROWS * CH + 16

BG, PANEL, TEXT, DIM, BLUE, YELLOW, GREEN = ((13, 14, 18), (24, 26, 33), (240, 240, 242), (140, 145, 158),
                                            (95, 175, 255), (255, 215, 95), (135, 215, 135))


def font(name, size):
    path = [f for d in ("/usr/share/fonts", "/usr/local/share/fonts") for f in glob.glob(d + "/**/" + name, recursive=True)]
    return ImageFont.truetype(path[0], size)


MONO, MONOB = font("DejaVuSansMono.ttf", 25), font("DejaVuSansMono-Bold.ttf", 25)
SANS = {s: font("DejaVuSans.ttf", s) for s in (22, 26, 30, 34, 44)}
BOLD = {s: font("DejaVuSans-Bold.ttf", s) for s in (22, 24, 34, 38, 40, 56, 84)}

# ---- the terminal ----

BASE = [(0, 0, 0), (205, 49, 49), (13, 188, 121), (229, 229, 16), (36, 114, 200), (188, 63, 188), (17, 168, 205),
        (229, 229, 229), (102, 102, 102), (241, 76, 76), (35, 209, 139), (245, 245, 67), (59, 142, 234),
        (214, 112, 214), (41, 184, 219), (255, 255, 255)]


def color(c):
    if c < 16:
        return BASE[c]
    if c < 232:
        c, steps = c - 16, [0, 95, 135, 175, 215, 255]
        return steps[c // 36], steps[c // 6 % 6], steps[c % 6]
    return (8 + 10 * (c - 232),) * 3


def screen(session, keys, cache={}):
    """The screen after these keys: rows of (character, fg, bg, attributes)."""
    path, options = session
    key = (path, options, keys)
    if key not in cache:
        out = subprocess.run([str(BIN), "ui", str(ROOT / path), f"--size={COLS}x{ROWS}", "--color=always", "--keys=" + keys, *options],
                             cwd=WORK, env=dict(os.environ, HOME=str(ROOT.parent)), capture_output=True, text=True, check=True).stdout
        rows = []
        fg = bg = None
        at = set()
        for line in out.rstrip("\n").split("\n"):
            row = []
            for part in re.split(r"(\x1b\[[0-9;]*m)", line):
                if part.startswith("\x1b["):
                    p = [int(x or 0) for x in part[2:-1].split(";")]
                    k = 0
                    while k < len(p):
                        if p[k] == 0:
                            fg, bg, at = None, None, set()
                        elif p[k] in (38, 48):
                            if p[k] == 38:
                                fg = color(p[k + 2])
                            else:
                                bg = color(p[k + 2])
                            k += 2
                        else:
                            at = at | {p[k]}
                        k += 1
                else:
                    row += [(ch, fg, bg, frozenset(at)) for ch in part]
            rows.append(row)
        cache[key] = rows
    return cache[key]


def command(text, cache={}):
    """What a command prints, run for real from the folder of the project."""
    if text not in cache:
        path = os.pathsep.join([str(ROOT / "bin"), str(Path.home() / ".cargo" / "bin"), os.environ.get("PATH", "")])
        done = subprocess.run(["bash", "-c", text], cwd=ROOT, env=dict(os.environ, PATH=path), capture_output=True, text=True)
        out = (done.stdout + done.stderr).rstrip("\n")
        if "cannot run" in out or "not found" in out:
            raise SystemExit(f"demo: {text!r} needs a tool that is not installed:\n{out}")
        cache[text] = out.split("\n")
    return cache[text]


def cells(text, fg=None, bold=False):
    return [(ch, fg, None, frozenset({1} if bold else ())) for ch in text]


def printed(line):
    """A line a command printed, with its marks in colour."""
    mark = line.lstrip()[:1]
    if mark in "✓✗" and mark:
        at = len(line) - len(line.lstrip())
        return cells(" " + line[:at]) + cells(mark, GREEN if mark == "✓" else (255, 135, 120), True) + cells(line[at + 1:])
    return cells(" " + line)


def paint(rows):
    im = Image.new("RGB", (COLS * CW, ROWS * CH), (18, 18, 22))
    d = ImageDraw.Draw(im)
    for y, row in enumerate(rows[:ROWS]):
        for x, (ch, fg, bg, at) in enumerate(row[:COLS]):
            if bg:
                d.rectangle([x * CW, y * CH, x * CW + CW - 1, y * CH + CH - 1], fill=bg)
            if ch != " ":
                fg = fg or (212, 212, 212)
                if 2 in at:
                    fg = tuple(v * 6 // 10 for v in fg)
                if ch in "│┃║":
                    d.rectangle([x * CW + 6 - (ch == "┃"), y * CH, x * CW + 8 + (ch == "┃"), y * CH + CH - 1], fill=fg)
                    continue
                d.text((x * CW, y * CH - 1), ch, font=MONOB if 1 in at else MONO, fill=fg)
            if 4 in at:
                d.line([x * CW, y * CH + CH - 3, x * CW + CW - 1, y * CH + CH - 3], fill=fg)
    return im


def find(rows, text):
    """Where this text is, in cells."""
    for y, row in enumerate(rows):
        x = "".join(c[0] for c in row).find(text)
        if x >= 0:
            return x, y
    raise SystemExit(f"demo: {text!r} is not on the screen")


def region(rows, spot):
    """(x, y, w, h) in cells, or (text, dx, dy, w, h) from where a text is."""
    if isinstance(spot[0], str):
        x, y = find(rows, spot[0])
        return x + spot[1], y + spot[2], spot[3], spot[4]
    return spot


# ---- overlays ----

KEYNAMES = {"<right>": "→", "<left>": "←", "<up>": "↑", "<down>": "↓", "<enter>": "Invio", "<esc>": "Esc",
            "<tab>": "Tab", "<space>": "Spazio", "<home>": "Inizio", "<end>": "Fine", "<back>": "⌫"}


def wrap(d, text, fnt, width):
    lines, now = [], ""
    for word in text.split():
        trial = (now + " " + word).strip()
        if d.textlength(trial, font=fnt) > width and now:
            lines.append(now)
            now = word
        else:
            now = trial
    return lines + [now]


def keycap(d, x, y, text, hot):
    w = max(52, int(d.textlength(text, font=BOLD[24])) + 30)
    d.rounded_rectangle([x, y + 4, x + w, y + 56], 10, fill=(60, 64, 75))
    d.rounded_rectangle([x, y, x + w, y + 50], 10, fill=YELLOW if hot else (205, 208, 215))
    d.text((x + w / 2, y + 25), text, font=BOLD[24], fill=(20, 20, 24), anchor="mm")
    return w


def caption(it, en):
    """The band with the words; drawn once per caption."""
    im = Image.new("RGB", (W - 520, H - BAND - 14), BG)
    d = ImageDraw.Draw(im)
    size = 38
    lines = wrap(d, it, BOLD[38], im.width - 40)
    if len(lines) > 2:
        raise SystemExit(f"demo: too long: {it!r}")
    d.rounded_rectangle([0, 4, 8, im.height - 10], 4, fill=YELLOW)
    y = 0
    for line in lines:
        d.text((30, y), line, font=BOLD[size], fill=TEXT)
        y += 46
    small = wrap(d, en, SANS[26], im.width - 40)
    for line in small[:1]:
        d.text((30, y + 4), line, font=SANS[26], fill=DIM)
    if len(small) > 1:
        raise SystemExit(f"demo: too long: {en!r}")
    return im


def reading_time(it):
    """Seconds needed to read a caption and look at what it is about."""
    return max(3.2, 1.5 + len(it) * 0.062)


def ease(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


class Film:
    def __init__(self, story, output, poster=None, preview=False):
        self.story = story
        self.time = 0.0
        self.frames = 0
        self.output = output
        self.poster = poster
        self.preview = preview  # a GIF: cuts, since each frame of a fade would weigh as much as a picture
        self.pipe = None
        self.total = 1.0
        self.marks = []
        self.chapters = []
        self.raw = None
        for path in (output, poster):
            if path:
                os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        source = ["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-"]
        if output and preview:
            self.raw = os.path.join(WORK, "preview.mkv")
            self.pipe = subprocess.Popen(source + ["-c:v", "libx264rgb", "-preset", "ultrafast", "-crf", "0", self.raw], stdin=subprocess.PIPE)
        elif output:
            self.pipe = subprocess.Popen(
                source + ["-c:v", "libx264", "-preset", "veryslow", "-crf", "31", "-g", "300", "-pix_fmt", "yuv420p",
                          "-tune", "animation", "-movflags", "+faststart", output], stdin=subprocess.PIPE)

    def put(self, im, seconds):
        n = round((self.time + seconds) * FPS) - self.frames
        self.time += seconds
        self.frames += n
        if self.pipe and n > 0:
            data = im.tobytes()
            for _ in range(n):
                self.pipe.stdin.write(data)

    def fade(self, a, b, seconds=0.4):
        n = max(1, round(seconds * FPS))
        for i in range(n):
            if self.preview:
                self.put(b, 1 / FPS)
            else:
                self.put(Image.blend(a, b, ease((i + 1) / n)) if self.pipe else None, 1 / FPS)

    def progress(self, d):
        d.rectangle([0, H - 8, W, H], fill=(30, 32, 40))
        d.rectangle([0, H - 8, W * self.time / self.total, H], fill=BLUE)
        for t, _ in self.marks:
            x = W * t / self.total
            d.rectangle([x - 1, H - 8, x + 1, H], fill=BG)

    # -- scenes

    def card(self, scene):
        im = Image.new("RGB", (W, H), BG)
        d = ImageDraw.Draw(im)
        y = 300 - 30 * len(scene.get("lines", []))
        if scene.get("tag"):
            w = d.textlength(scene["tag"], font=BOLD[34]) + 60
            d.rounded_rectangle([(W - w) / 2, y, (W + w) / 2, y + 62], 31, fill=scene.get("color", BLUE))
            d.text((W / 2, y + 31), scene["tag"], font=BOLD[34], fill=(16, 16, 20), anchor="mm")
            y += 110
        d.text((W / 2, y + 45), scene["title"], font=BOLD[84], fill=TEXT, anchor="mm")
        y += 130
        d.text((W / 2, y), scene["sub"], font=SANS[44], fill=YELLOW, anchor="mm")
        y += 58
        d.text((W / 2, y), scene["en"], font=SANS[30], fill=DIM, anchor="mm")
        y += 90
        for left, right in scene.get("lines", []):
            d.text((W / 2 - 30, y), left, font=MONOB, fill=BLUE, anchor="rm")
            d.text((W / 2 + 10, y), right, font=SANS[30], fill=TEXT, anchor="lm")
            y += 58
        self.progress(d)
        text = scene["title"] + scene["sub"] + "".join(a + b for a, b in scene.get("lines", []))
        seconds = scene.get("seconds") or 1.6 + len(text) * 0.04
        previous = self.last if self.frames else Image.new("RGB", (W, H), BG)
        self.fade(previous, im)
        self.put(im, seconds)
        self.last = im

    def frame(self, term, words, keys, hot, hole=None, strength=0.0, shade=None):
        im = Image.new("RGB", (W, H), BG)
        d = ImageDraw.Draw(im)
        d.text((TX, 14), self.chapter, font=BOLD[22], fill=BLUE)
        d.text((W - TX, 14), "Pseudoscope", font=BOLD[22], fill=DIM, anchor="ra")
        if hole and strength > 0:
            dark = Image.blend(term, Image.new("RGB", term.size, (6, 6, 9)), 0.62 * strength)
            x, y, w, h = hole
            box = (max(0, x * CW - 8), max(0, y * CH - 5), min(term.width, (x + w) * CW + 8), min(term.height, (y + h) * CH + 5))
            dark.paste(term.crop(box), box[:2])
            dd = ImageDraw.Draw(dark)
            edge = tuple(int(18 + (c - 18) * strength) for c in YELLOW)
            dd.rounded_rectangle([box[0], box[1], box[2] - 1, box[3] - 1], 8, outline=edge, width=3)
            term = dark
        d.rounded_rectangle([TX - 8, TY - 8, TX + COLS * CW + 8, TY + ROWS * CH + 8], 10, fill=(18, 18, 22), outline=(50, 54, 66), width=2)
        im.paste(term, (TX, TY))
        if words:
            im.paste(words, (TX, BAND))
        x = W - TX
        caps = []
        for k in keys[-4:]:
            caps.append((k, max(52, int(d.textlength(k, font=BOLD[24])) + 30)))
        x -= sum(w + 12 for _, w in caps)
        for i, (k, w) in enumerate(caps):
            keycap(d, x, BAND + 30, k, hot and i == len(caps) - 1)
            x += w + 12
        self.progress(d)
        return im

    def arrive(self, term, words, first):
        """The words come first, then what happens."""
        now = self.frame(term, words, [], False) if self.pipe else None
        self.fade(self.last, now, 0.35 if not first else 0.6)
        if self.pipe:
            self.last = now

    def settle(self, term, rows, words, label, step, start, needed, acted):
        """What to look at, and enough time to read and to look."""
        spot = step.get("spot")
        if spot:
            hole = region(rows, spot)
            n = round(0.35 * FPS)
            for i in range(n):
                if self.pipe:
                    self.last = self.frame(term, words, label, False, hole, ease((i + 1) / n))
                self.put(self.last, 1 / FPS)
        elif self.pipe and acted:
            self.last = self.frame(term, words, label, False)
        if step.get("poster") and self.poster and self.pipe:
            self.save_poster(term)
        settle = step.get("hold", 1.6 if acted or spot else 0)
        self.put(self.last, max(settle, needed - (self.time - start)))
        if spot:
            after = self.frame(term, words, label, False) if self.pipe else None
            self.fade(self.last, after, 0.25)
            if self.pipe:
                self.last = after

    def shell_(self, scene):
        """Steps in a terminal: caption, a command typed, what it prints."""
        self.chapter = scene["name"]
        self.marks_now(scene["name"])
        history, first = [], True
        for step in scene["steps"]:
            words = caption(step["it"], step["en"])
            needed = step.get("seconds") or reading_time(step["it"])
            start = self.time
            if step.get("clear"):
                history = []
            line = cells(" $ ", GREEN, True)
            history.append(line)
            rows = [[]] + history[-(ROWS - 2):]
            term = paint(rows) if self.pipe else None
            self.arrive(term, words, first)
            first = False
            self.put(self.last, min(1.2, needed * 0.3))
            text = step["run"]
            for i, ch in enumerate(text):  # two letters per frame
                line.append((ch, TEXT, None, frozenset({1})))
                if i % 2 and i + 1 < len(text):
                    continue
                if self.pipe:
                    term = paint(rows)
                    self.last = self.frame(term, words, [], False)
                self.put(self.last, 2 / FPS)
            self.put(self.last, 0.35)
            out = command(text) if self.pipe or step.get("spot") else []
            if step.get("lines") and len(out) > step["lines"]:
                out = out[:step["lines"]] + ["…"]
            history += [printed(o) for o in out] + [[]]
            rows = [[]] + history[-(ROWS - 2):]
            if self.pipe:
                term = paint(rows)
                self.last = self.frame(term, words, ["Invio"], True)
            self.put(self.last, 0.5)
            self.settle(term, rows, words, ["Invio"], step, start, needed, True)

    def chapter_(self, scene):
        """Steps: caption, keys, what to look at."""
        session = (scene["open"], tuple(scene.get("options", ())))
        self.chapter = scene["name"]
        self.marks_now(scene["name"])
        typed, pressed = scene.get("start", ""), []
        term = paint(screen(session, typed)) if self.pipe else None
        first = True
        for step in scene["steps"]:
            rows = screen(session, typed)
            words = caption(step["it"], step["en"])
            keys = re.findall(r"<\w+>|.", step.get("keys", ""))
            pace = step.get("pace", 0.55)
            needed = step.get("seconds") or reading_time(step["it"])
            start = self.time
            lead = min(1.6, needed * 0.35) if keys else 0
            if not keys and step.get("spot"):
                lead = 0
            self.arrive(term, words, first)
            first = False
            self.put(self.last if self.pipe else None, lead)
            pressed = []
            for i, k in enumerate(keys):
                typed += k
                pressed.append(KEYNAMES.get(k, k))
                if pressed[-2:] == [pressed[-1]] * 2:
                    pressed = pressed[:-1]
                    count = getattr(self, "repeat", 1) + 1
                else:
                    count = 1
                self.repeat = count
                label = pressed[:-1] + [pressed[-1] + (f" ×{count}" if count > 1 else "")]
                rows = screen(session, typed)
                if self.pipe:
                    term = paint(rows)
                    self.last = self.frame(term, words, label, True)
                self.put(self.last if self.pipe else None, pace)
            label = (pressed[:-1] + [pressed[-1] + (f" ×{self.repeat}" if self.repeat > 1 else "")]) if pressed else []
            self.settle(term, rows, words, label, step, start, needed, bool(keys))

    def save_poster(self, term):
        """The picture that stands for the video where it is published: a screen, and the name."""
        self.chapter, chapter = "", self.chapter
        im = self.frame(term, None, [], False)
        self.chapter = chapter
        d = ImageDraw.Draw(im)
        d.rectangle([0, BAND - 6, W, H], fill=BG)
        d.text((TX, BAND + 2), "Pseudoscope", font=BOLD[84], fill=TEXT)
        x = TX + d.textlength("Pseudoscope", font=BOLD[84]) + 44
        d.text((x, BAND + 14), "Pseudocodice e progetti che prendono vita", font=SANS[34], fill=YELLOW)
        d.text((x, BAND + 62), "Pseudocode and designs, brought to life in the terminal", font=SANS[30], fill=DIM)
        im.resize((1280, 720), Image.LANCZOS).quantize(128).save(self.poster, optimize=True)

    def marks_now(self, name):
        if not self.pipe:
            self.marks.append((self.time, name))

    def run(self):
        self.last = Image.new("RGB", (W, H), BG)
        for scene in self.story:
            if "title" in scene:
                if not self.pipe:
                    self.chapters.append((self.time, scene.get("chapter", scene["title"]), scene.get("title_en", scene["title"])))
                    self.time += 0.4
                    text = scene["title"] + scene["sub"] + "".join(a + b for a, b in scene.get("lines", []))
                    self.time += scene.get("seconds") or 1.6 + len(text) * 0.04
                    self.frames = round(self.time * FPS)
                else:
                    self.card(scene)
            elif "open" in scene:
                self.chapter_(scene)
            else:
                self.shell_(scene)
        if not self.preview:
            self.fade(self.last, Image.new("RGB", (W, H), BG), 0.8)
        if self.pipe:
            self.pipe.stdin.close()
            if self.pipe.wait():
                raise SystemExit("demo: ffmpeg failed")
        if self.raw:
            self.gif()

    def gif(self):
        """Few colours, chosen once, and only what changes from a frame to the next."""
        look = "fps=10,scale=960:-1:flags=lanczos"
        palette = os.path.join(WORK, "palette.png")
        quiet = ["ffmpeg", "-loglevel", "error", "-y", "-i", self.raw]
        subprocess.run(quiet + ["-vf", look + ",palettegen=max_colors=128:stats_mode=full", palette], check=True)
        subprocess.run(quiet + ["-i", palette, "-lavfi", look + "[x];[x][1:v]paletteuse=dither=none:diff_mode=rectangle",
                                "-loop", "0", self.output], check=True)


def clock(t):
    return f"{int(t // 60)}:{int(t % 60):02d}"


def chapters(plan):
    """Where each part begins, as the sites that show videos want it: none shorter than ten seconds."""
    kept = []
    for t, it, en in plan.chapters:
        if kept and t - kept[-1][0] < 10:
            t = kept.pop()[0]
        kept.append((t, it, en))
    return kept


def main():
    preview = sys.argv[1:2] == ["--preview"]
    story = TRAILER if preview else STORY
    plan = Film(story, None, preview=preview)
    plan.run()
    print(f"{int(plan.time // 60)}:{plan.time % 60:04.1f}")
    for which in (1, 2):
        print()
        for chapter in chapters(plan):
            print(f"{clock(chapter[0])} {chapter[which]}")
    if sys.argv[1:] == ["--check"]:
        return
    film = Film(story, sys.argv[2] if preview else sys.argv[1], sys.argv[2] if len(sys.argv) > 2 and not preview else None, preview)
    film.total, film.marks = plan.time, plan.marks
    film.run()


try:
    main()
finally:
    shutil.rmtree(WORK, ignore_errors=True)
