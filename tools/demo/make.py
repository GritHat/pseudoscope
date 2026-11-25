#!/usr/bin/env python3
"""Makes the demo video: the real interface, driven by keys, with captions.

Usage: make.py OUTPUT.mp4 [THUMBNAIL.png] [--music=FILE|none] [--lang=CODE]
       make.py --preview OUTPUT.gif         (the short one, which plays by itself in a README)
       make.py --thumbnail OUTPUT.png       (only the picture that stands for the video)
       make.py --soundtrack OUTPUT.wav      (only the music, to listen to it)
       make.py --check                      (durations and chapters only)

Needs Pillow, numpy and ffmpeg. The scenes in a terminal run the real commands:
the last chapter needs rustc, java and python3. The music is written by
music.py, so it is nobody else's; --music takes a file instead, or none.

Everything is in English. --lang=it films the interface in Italian with the
captions of story_it.py; a language without captions of its own keeps the
English ones.
"""
import glob
import importlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent.parent
BIN = ROOT / "bin" / "pseudoscope"
sys.path.insert(0, str(Path(__file__).parent))
import logo  # noqa: E402
import music  # noqa: E402
from story import STORY, TRAILER  # noqa: E402

# The language of the interface and of the captions.
LANG = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--lang=")), "en")
try:
    WORDS = importlib.import_module("story_" + LANG).WORDS
except ImportError:
    WORDS = {}


def say(text):
    """A text of the story, in the language of the video."""
    return WORDS.get(text, text)


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
        out = subprocess.run([str(BIN), "ui", str(ROOT / path), f"--size={COLS}x{ROWS}", "--color=always", "--keys=" + keys, "--lang=" + LANG, *options],
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
        done = subprocess.run(["bash", "-c", text], cwd=ROOT, env=dict(os.environ, PATH=path, PSEUDOSCOPE_LANG=LANG), capture_output=True, text=True)
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

KEYNAMES = {"<right>": "→", "<left>": "←", "<up>": "↑", "<down>": "↓", "<enter>": "Enter", "<esc>": "Esc",
            "<tab>": "Tab", "<space>": "Space", "<home>": "Home", "<end>": "End", "<back>": "⌫"}
KEYNAMES.update({"it": {"<enter>": "Invio", "<space>": "Spazio", "<home>": "Inizio", "<end>": "Fine"},
                 "es": {"<enter>": "Intro", "<space>": "Espacio", "<home>": "Inicio", "<end>": "Fin"}}.get(LANG, {}))


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


def caption(text):
    """The band with the words; drawn once per caption."""
    im = Image.new("RGB", (W - 520, H - BAND - 14), BG)
    d = ImageDraw.Draw(im)
    lines = wrap(d, text, BOLD[38], im.width - 40)
    if len(lines) > 2:
        raise SystemExit(f"demo: too long: {text!r}")
    d.rounded_rectangle([0, 4, 8, im.height - 10], 4, fill=YELLOW)
    y = 24 if len(lines) == 1 else 2
    for line in lines:
        d.text((30, y), line, font=BOLD[38], fill=TEXT)
        y += 50
    return im


def reading_time(text):
    """Seconds needed to read a caption and look at what it is about."""
    return max(3.2, 1.5 + len(text) * 0.062)


def ease(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


class Film:
    def __init__(self, story, output, preview=False, sound=None):
        self.story = story
        self.time = 0.0
        self.frames = 0
        self.output = output
        self.preview = preview  # a GIF: cuts, since each frame of a fade would weigh as much as a picture
        self.pipe = None
        self.total = 1.0
        self.marks = []
        self.chapters = []
        self.raw = None
        if output:
            os.makedirs(os.path.dirname(os.path.abspath(output)), exist_ok=True)
        source = ["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-"]
        if output and preview:
            self.raw = os.path.join(WORK, "preview.mkv")
            self.pipe = subprocess.Popen(source + ["-c:v", "libx264rgb", "-preset", "ultrafast", "-crf", "0", self.raw], stdin=subprocess.PIPE)
        elif output:
            heard = ["-i", sound] if sound else []
            mixed = ["-c:a", "aac", "-b:a", "160k", "-map", "0:v", "-map", "1:a"] if sound else []
            self.pipe = subprocess.Popen(
                source + heard + ["-c:v", "libx264", "-preset", "veryslow", "-crf", "31", "-g", "300", "-pix_fmt", "yuv420p",
                                  "-tune", "animation"] + mixed + ["-movflags", "+faststart", output], stdin=subprocess.PIPE)

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
        if scene.get("logo"):
            size = 160 if y >= 300 else 120
            logo.draw(im, W // 2 - size // 2, y - size - 30, size)
        if scene.get("tag"):
            w = d.textlength(say(scene["tag"]), font=BOLD[34]) + 60
            d.rounded_rectangle([(W - w) / 2, y, (W + w) / 2, y + 62], 31, fill=scene.get("color", BLUE))
            d.text((W / 2, y + 31), say(scene["tag"]), font=BOLD[34], fill=(16, 16, 20), anchor="mm")
            y += 110
        for text, fnt in ((say(scene["title"]), BOLD[84]), (say(scene["sub"]), SANS[44])):
            if d.textlength(text, font=fnt) > W - 160:
                raise SystemExit(f"demo: too long for a card: {text!r}")
        d.text((W / 2, y + 45), say(scene["title"]), font=BOLD[84], fill=TEXT, anchor="mm")
        y += 130
        d.text((W / 2, y), say(scene["sub"]), font=SANS[44], fill=YELLOW, anchor="mm")
        y += 58
        y += 64
        for left, right in scene.get("lines", []):
            d.text((W / 2 - 30, y), left, font=MONOB, fill=BLUE, anchor="rm")
            d.text((W / 2 + 10, y), say(right), font=SANS[30], fill=TEXT, anchor="lm")
            y += 58
        self.progress(d)
        text = say(scene["title"]) + say(scene["sub"]) + "".join(a + say(b) for a, b in scene.get("lines", []))
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
        settle = step.get("hold", 1.6 if acted or spot else 0)
        self.put(self.last, max(settle, needed - (self.time - start)))
        if spot:
            after = self.frame(term, words, label, False) if self.pipe else None
            self.fade(self.last, after, 0.25)
            if self.pipe:
                self.last = after

    def shell_(self, scene):
        """Steps in a terminal: caption, a command typed, what it prints."""
        self.chapter = say(scene["name"])
        self.marks_now(scene["name"])
        history, first = [], True
        for step in scene["steps"]:
            words = caption(say(step["say"]))
            needed = step.get("seconds") or reading_time(say(step["say"]))
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
                self.last = self.frame(term, words, [KEYNAMES["<enter>"]], True)
            self.put(self.last, 0.5)
            self.settle(term, rows, words, [KEYNAMES["<enter>"]], step, start, needed, True)

    def chapter_(self, scene):
        """Steps: caption, keys, what to look at."""
        session = (scene["open"], tuple(scene.get("options", ())))
        self.chapter = say(scene["name"])
        self.marks_now(scene["name"])
        typed, pressed = scene.get("start", ""), []
        term = paint(screen(session, typed)) if self.pipe else None
        first = True
        for step in scene["steps"]:
            rows = screen(session, typed)
            words = caption(say(step["say"]))
            keys = re.findall(r"<\w+>|.", step.get("keys", ""))
            pace = step.get("pace", 0.55)
            needed = step.get("seconds") or reading_time(say(step["say"]))
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

    def marks_now(self, name):
        if not self.pipe:
            self.marks.append((self.time, name))

    def run(self):
        self.last = Image.new("RGB", (W, H), BG)
        for scene in self.story:
            if "title" in scene:
                if not self.pipe:
                    self.chapters.append((self.time, say(scene.get("chapter", scene["title"]))))
                    self.time += 0.4
                    text = say(scene["title"]) + say(scene["sub"]) + "".join(a + say(b) for a, b in scene.get("lines", []))
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


def thumbnail(path):
    """The picture that stands for the video where it is published: the mark and the name,
    large enough to be read when it is small, over the screen of the step marked `poster`."""
    term = None
    for scene in STORY:
        typed = scene.get("start", "")
        for step in scene.get("steps", []):
            typed += step.get("keys", "")
            if step.get("poster"):
                term = paint(screen((scene["open"], tuple(scene.get("options", ()))), typed))
    w, h = 2560, 1440  # twice the size, then reduced
    scale = max(w / term.width, h / term.height) * 1.12
    back = term.resize((round(term.width * scale), round(term.height * scale)), Image.LANCZOS)
    back = back.crop((60, 150, 60 + w, 150 + h)).filter(ImageFilter.GaussianBlur(5))
    im = Image.blend(back, Image.new("RGB", (w, h), BG), 0.74)
    d = ImageDraw.Draw(im)
    name, said = font("DejaVuSans-Bold.ttf", 236), font("DejaVuSans.ttf", 92)
    small = font("DejaVuSansMono-Bold.ttf", 60)
    mark, gap = 420, 70
    width = mark + gap + d.textlength("Pseudoscope", font=name)
    x, y = (w - width) / 2, 330
    logo.draw(im, x, y, mark)
    d.text((x + mark + gap, y + mark / 2 - 16), "Pseudoscope", font=name, fill=TEXT, anchor="lm")
    d.text((w / 2, y + mark + 130), "Pseudocode, brought to life", font=said, fill=YELLOW, anchor="mm")
    # what it speaks, each on its plate
    names = ["C", "C++", "Java", "Rust", "TypeScript", "JavaScript", "Python"]
    plates = [d.textlength(n, font=small) + 72 for n in names]
    x, y = (w - sum(plates) - 28 * (len(names) - 1)) / 2, y + mark + 290
    for n, plate in zip(names, plates):
        d.rounded_rectangle([x, y, x + plate, y + 116], 58, fill=PANEL, outline=BLUE, width=5)
        d.text((x + plate / 2, y + 58), n, font=small, fill=TEXT, anchor="mm")
        x += plate + 28
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    im.resize((1280, 720), Image.LANCZOS).save(path, optimize=True)


def soundtrack(choice, seconds, starts):
    """The music, as long as the video: the one written here, or somebody else's file."""
    path = os.path.join(WORK, "music.wav")
    if choice == "none":
        return None
    if choice:
        fades = f"afade=t=in:d=1.5,afade=t=out:st={seconds - 5:.2f}:d=5"
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-stream_loop", "-1", "-i", choice, "-t", f"{seconds:.3f}",
                        "-af", fades, "-ar", "48000", "-ac", "2", path], check=True)
    else:
        music.write(path, seconds, starts)
    return path


def clock(t):
    return f"{int(t // 60)}:{int(t % 60):02d}"


def chapters(plan):
    """Where each part begins, as the sites that show videos want it: none shorter than ten seconds."""
    kept = []
    for t, name in plan.chapters:
        if kept and t - kept[-1][0] < 10:
            t = kept.pop()[0]
        kept.append((t, name))
    return kept


def main():
    words = [w for w in sys.argv[1:] if not w.startswith(("--music=", "--lang="))]
    chosen = [w.split("=", 1)[1] for w in sys.argv[1:] if w.startswith("--music=")]
    mode = words.pop(0) if words and words[0].startswith("--") else ""
    if mode not in ("", "--preview", "--thumbnail", "--soundtrack", "--check") or (mode != "--check" and not words):
        raise SystemExit(__doc__)
    if mode == "--thumbnail":
        thumbnail(words[0])
        return
    preview = mode == "--preview"
    story = TRAILER if preview else STORY
    plan = Film(story, None, preview=preview)
    plan.run()
    print(f"{int(plan.time // 60)}:{plan.time % 60:04.1f}")
    for chapter in chapters(plan):
        print(f"{clock(chapter[0])} {chapter[1]}")
    starts = [chapter[0] for chapter in chapters(plan)]
    if mode == "--check":
        return
    if mode == "--soundtrack":
        music.write(words[0], plan.time, starts)
        return
    sound = None if preview else soundtrack(chosen[0] if chosen else "", plan.time, starts)
    film = Film(story, words[0], preview, sound)
    film.total, film.marks = plan.time, plan.marks
    film.run()
    if len(words) > 1 and not preview:
        thumbnail(words[1])


try:
    main()
finally:
    shutil.rmtree(WORK, ignore_errors=True)
