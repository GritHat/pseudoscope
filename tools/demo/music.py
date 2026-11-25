#!/usr/bin/env python3
"""The music under the video. It is written here, note by note, so it belongs
to nobody else: a pad, a bass, an arpeggio, a few bells and soft drums, in D,
at eighty beats a minute. It takes a breath where a chapter begins, and each
chapter has its own chords.

Usage: music.py OUTPUT.wav SECONDS [CHAPTER_SECONDS ...]      (needs numpy)
"""
import sys
import wave

import numpy as np

RATE = 48000
BEAT = 0.75
BAR = 4 * BEAT
LOUDNESS = 0.105  # of the whole, as a root mean square: about -18 LUFS

# name: the bass, the notes of the pad, the notes the arpeggio and the bells choose from
CHORDS = {
    "D": (38, (54, 57, 61, 64), (62, 66, 69, 73, 76, 81)),
    "Bm": (47, (54, 57, 59, 62), (59, 62, 66, 69, 73, 78)),
    "G": (43, (55, 59, 62, 66), (62, 67, 71, 74, 78, 81)),
    "A": (45, (57, 59, 64, 66), (64, 69, 71, 73, 76, 78)),
    "F#m": (42, (54, 57, 61, 64), (61, 66, 69, 73, 76, 81)),
    "Em": (40, (55, 59, 62, 66), (64, 67, 71, 74, 78, 79)),
    "G+": (43, (55, 59, 61, 66), (62, 67, 71, 73, 78, 81)),
    "D/F#": (42, (54, 57, 62, 64), (62, 66, 69, 74, 76, 81)),
}
SONGS = {
    "home": ("D", "Bm", "G", "A"),
    "lift": ("G", "A", "F#m", "Bm"),
    "doubt": ("Bm", "G", "Em", "F#m"),
    "float": ("G+", "D/F#", "Em", "A"),
}
# Which of its notes the arpeggio plays on each eighth of two bars.
ARPS = {
    "wave": (0, 2, 3, 1, 2, 4, 3, 2, 0, 2, 4, 3, 5, 4, 2, 1),
    "fall": (5, 3, 4, 2, 3, 1, 2, 0, 4, 3, 2, 1, 3, 2, 1, 0),
    "gaps": (0, None, 2, 3, None, 4, None, 2, 1, None, 3, None, 4, 2, None, 1),
    "slow": (0, None, 2, None, 3, None, 1, None, 2, None, 4, None, 3, None, 2, None),
}
# What each chapter sounds like: its songs in turn, how the arpeggio moves, the
# drums (0 none, 1 light, 2 with a kick), how many bells, and whether it is low.
MOODS = [
    dict(songs=("home", "home", "lift", "home", "lift"), arp="wave", drums=1, bells=0.5),
    dict(songs=("lift",), arp="fall", drums=1, bells=0.4),
    dict(songs=("float",), arp="gaps", drums=1, bells=0.8),
    dict(songs=("home", "lift"), arp="wave", drums=2, bells=0.4),
    dict(songs=("lift", "float", "home"), arp="gaps", drums=1, bells=0.6),
    dict(songs=("float",), arp="slow", drums=0, bells=0.9),
    dict(songs=("home", "lift"), arp="wave", drums=2, bells=0.4),
    dict(songs=("doubt",), arp="fall", drums=2, bells=0.2, low=True),
    dict(songs=("doubt",), arp="gaps", drums=1, bells=0.5),
    dict(songs=("lift", "home"), arp="wave", drums=2, bells=0.5),
    dict(songs=("doubt",), arp="slow", drums=0, bells=0.3, low=True),
    dict(songs=("home",), arp="wave", drums=2, bells=0.6),
]
ENDING = dict(songs=("home",), arp="slow", drums=0, bells=1.0)


def hz(note):
    return 440.0 * 2 ** ((note - 69) / 12)


def clock(seconds):
    return np.arange(int(seconds * RATE)) / RATE


def swell(n, attack, release):
    """An envelope that rises and falls without corners."""
    env = np.ones(n)
    a, r = min(n, int(attack * RATE)), min(n, int(release * RATE))
    env[:a] = 0.5 - 0.5 * np.cos(np.pi * np.arange(a) / a)
    env[n - r:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(r) / r)
    return env


# ---- the instruments: each gives one note ----

def pad(notes, seconds, rng):
    """A chord that comes and goes slowly: three voices a little out of tune, left, middle and right."""
    t = clock(seconds)
    out = np.zeros((2, len(t)))
    for note in notes:
        for cents, left in ((-6, 0.85), (0, 0.6), (7, 0.3)):
            w = 2 * np.pi * hz(note) * 2 ** (cents / 1200) * t + rng.uniform(0, 6.28)
            voice = np.sin(w) + 0.4 * np.sin(2 * w + rng.uniform(0, 6.28)) + 0.2 * np.sin(3 * w + rng.uniform(0, 6.28))
            voice += 0.1 * np.sin(4 * w + rng.uniform(0, 6.28)) + 0.05 * np.sin(5 * w + rng.uniform(0, 6.28))
            voice *= 1 + 0.07 * np.sin(2 * np.pi * rng.uniform(0.09, 0.2) * t + rng.uniform(0, 6.28))
            out[0] += voice * left
            out[1] += voice * (1 - left)
    return out * swell(len(t), 1.3, 2.0) / (3 * len(notes))


def bass(note, seconds):
    t = clock(seconds)
    w = 2 * np.pi * hz(note) * t
    tone = np.tanh(1.4 * (np.sin(w) + 0.5 * np.sin(2 * w) + 0.25 * np.sin(3 * w) + 0.1 * np.sin(4 * w)))
    return tone * (0.72 + 0.28 * np.exp(-t / 0.3)) * swell(len(t), 0.018, min(0.25, seconds / 3))


def key(note, strength):
    """An electric piano, more or less: bright when struck, then round."""
    f = hz(note)
    t = clock(1.2 + 110 / f)
    w = 2 * np.pi * f * t
    tone = np.sin(w + (0.45 + 2.3 * strength * np.exp(-t / 0.3)) * np.sin(w))
    tone += 0.22 * strength * np.exp(-t / 0.07) * np.sin(7 * w)
    return tone * np.exp(-t / (0.32 + 70 / f)) * swell(len(t), 0.004, 0.12)


def bell(note):
    t = clock(3.2)
    w = 2 * np.pi * hz(note) * t
    tone = np.exp(-t / 1.1) * np.sin(w) + 0.3 * np.exp(-t / 0.6) * np.sin(2 * w) + 0.12 * np.exp(-t / 0.3) * np.sin(3.01 * w)
    return (tone + 0.05 * np.exp(-t / 0.15) * np.sin(4.2 * w)) * swell(len(t), 0.003, 0.4)


def kick():
    t = clock(0.5)
    return np.sin(2 * np.pi * (46 * t - 70 * 0.04 * np.exp(-t / 0.04))) * np.exp(-t / 0.14) * swell(len(t), 0.002, 0.1)


def hiss(seconds, low, high, decay, rng):
    """Noise between two frequencies, dying away: a hat, or a tap on the rim."""
    n = int(seconds * RATE)
    spectrum = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1 / RATE)
    spectrum *= (f / low) ** 2 / (1 + (f / low) ** 2) / (1 + (f / high) ** 4)
    return np.fft.irfft(spectrum, n) * np.exp(-clock(seconds)[:n] / decay) * swell(n, 0.001, seconds / 3)


def room(rng):
    """How a large, soft room answers a clap."""
    t = clock(2.4)
    answer = rng.standard_normal((2, len(t))) * np.exp(-t / 0.36)
    f = np.fft.rfftfreq(len(t), 1 / RATE)
    answer = np.fft.irfft(np.fft.rfft(answer) * (f / 180) ** 2 / (1 + (f / 180) ** 2) / (1 + (f / 5000) ** 2), len(t))
    answer *= swell(len(t), 0.012, 0.5)
    return answer / np.sqrt((answer ** 2).sum(axis=1, keepdims=True))


def in_room(sound, answer):
    """Each channel as the room gives it back (a convolution, done in pieces)."""
    piece, size = 1 << 17, 1 << 18
    out = np.zeros((2, sound.shape[1] + size))
    for channel in range(2):
        shape = np.fft.rfft(answer[channel], size)
        for start in range(0, sound.shape[1], piece):
            out[channel, start:start + size] += np.fft.irfft(np.fft.rfft(sound[channel, start:start + piece], size) * shape, size)
    return out[:, :sound.shape[1]]


class Tape:
    """Where the notes are laid down: a dry track, one that goes to the room, one that also echoes."""

    def __init__(self, seconds):
        self.n = int(seconds * RATE)
        self.dry = np.zeros((2, self.n), dtype=np.float32)
        self.far = np.zeros((2, self.n), dtype=np.float32)
        self.echo = np.zeros((2, self.n), dtype=np.float32)

    def put(self, track, at, sound, gain=1.0, left=0.5):
        start = int(at * RATE)
        if start < 0 or start >= self.n:
            return
        if sound.ndim == 1:
            angle = (1 - left) * np.pi / 2
            sound = np.stack([sound * np.cos(angle), sound * np.sin(angle)])
        length = min(sound.shape[1], self.n - start)
        track[:, start:start + length] += (sound[:, :length] * gain).astype(np.float32)


def lay(seconds, chapters, rng):
    """Every note in its place."""
    tape = Tape(seconds)
    pads, keys, bells = {}, {}, {}
    hat, rim, drum = hiss(0.12, 7000, 12000, 0.022, rng), hiss(0.2, 1100, 3200, 0.04, rng), kick()

    def ring(note):
        if note not in bells:
            bells[note] = bell(note)
        return bells[note]

    bars = int(np.ceil(seconds / BAR))
    starts = sorted({round(c / BAR) for c in chapters} | {0})
    for s, first in enumerate(starts):
        last = starts[s + 1] if s + 1 < len(starts) else bars
        mood = ENDING if s and s + 1 == len(starts) else MOODS[s % len(MOODS)]
        chords = [c for song in mood["songs"] for c in SONGS[song]]
        shift = -12 if mood.get("low") else 0
        for k, bar in enumerate(range(first, last, 2)):
            name = chords[k % len(chords)]
            root, held, tones = CHORDS[name]
            length = min(2, last - bar)
            at = bar * BAR
            breath = k == 0  # the title of the chapter is on the screen: let it be read
            if (name, length) not in pads:
                pads[name, length] = pad(held, length * BAR + 1.6, rng)
            tape.put(tape.far, max(0, at - 0.7), pads[name, length], 0.5)
            tape.put(tape.dry, max(0, at - 0.7), pads[name, length], 0.5)

            # the bass: the root, and something to lean on before the next bar
            for b in range(length):
                tape.put(tape.dry, at + b * BAR, bass(root, 2.6 * BEAT if not breath else 3.6 * BEAT), 0.26 if breath else 0.32)
                if not breath and mood["drums"] == 2 and (bar + b) % 2:
                    tape.put(tape.dry, at + b * BAR + 3.5 * BEAT, bass(root + 7, 0.45 * BEAT), 0.2)

            if breath:  # two bells, a fourth apart: the sound of a new chapter
                up = mood["songs"][0] != "doubt"
                tape.put(tape.far, at + 0.02, ring(81 if up else 83), 0.28, 0.4)
                tape.put(tape.far, at + 1.5 * BEAT, ring(86 if up else 78), 0.25, 0.6)
            else:
                pattern = ARPS[mood["arp"]]
                for i in range(8 * length):
                    step = pattern[i % 16]
                    if step is None or rng.random() < 0.08:
                        continue
                    note = tones[step % len(tones)] + shift
                    strength = (1.0, 0.55, 0.75, 0.55, 0.9, 0.55, 0.7, 0.6)[i % 8] * rng.uniform(0.88, 1.0)
                    if (note, round(strength, 1)) not in keys:
                        keys[note, round(strength, 1)] = key(note, round(strength, 1))
                    when = at + i * BEAT / 2 + (0.03 if i % 2 else 0) + rng.uniform(-0.004, 0.004)
                    tape.put(tape.echo, when, keys[note, round(strength, 1)], 0.42 * strength, 0.5 + 0.22 * np.sin(i * 1.9))
                if rng.random() < 0.6 * mood["bells"] and (s or k >= 4):
                    for i in rng.choice((3, 7, 11, 13), size=1 + (rng.random() < 0.35), replace=False):
                        if i < 8 * length:
                            note = int(rng.choice(tones[2:])) + (12 if rng.random() < 0.5 else 0)
                            tape.put(tape.far, at + i * BEAT / 2 + 0.03, ring(note if note <= 88 else note - 12), 0.17, rng.uniform(0.25, 0.75))

            # the drums come in after the arpeggio, later still in the first chapter
            if mood["drums"] and k >= (4 if s == 0 else 2):
                for b in range(length):
                    here = at + b * BAR
                    for i in range(8):
                        when = here + i * BEAT / 2 + (0.03 if i % 2 else 0)
                        tape.put(tape.dry, when, hat, (0.5, 0.22, 0.36, 0.22, 0.5, 0.22, 0.36, 0.27)[i] * 0.42, 0.42 + 0.16 * (i % 2))
                        if i in (2, 6):
                            tape.put(tape.dry, when, rim, 0.3, 0.56)
                            tape.put(tape.far, when, rim, 0.16, 0.56)
                        if mood["drums"] == 2 and (i in (0, 4) or (i == 7 and (bar + b) % 4 == 3)):
                            tape.put(tape.dry, when, drum, 0.62 if i != 7 else 0.36)

    return tape


def compose(seconds, chapters):
    rng = np.random.default_rng(1957)
    tape = lay(seconds, chapters, rng)
    # the arpeggio comes back from one side, then the other, each time fainter
    echoed = tape.echo.copy()
    for turn in range(1, 5):
        shift = int(turn * 0.75 * BEAT * RATE)
        to = turn % 2
        echoed[to, shift:] += tape.echo[1 - to, :-shift] * 0.42 ** turn
        echoed[1 - to, shift:] += tape.echo[to, :-shift] * 0.42 ** turn * 0.3
    whole = tape.dry + echoed + 0.75 * in_room(tape.far + 0.45 * echoed, room(rng))
    whole -= whole.mean(axis=1, keepdims=True)
    whole *= LOUDNESS / np.sqrt((whole ** 2).mean())
    whole = np.tanh(whole * 1.6) / 1.6  # the loudest moments, rounded off instead of cut
    whole[:, :int(1.5 * RATE)] *= np.linspace(0, 1, int(1.5 * RATE)) ** 2
    tail = min(whole.shape[1], int(5.5 * RATE))
    whole[:, -tail:] *= (0.5 + 0.5 * np.cos(np.pi * np.arange(tail) / tail))
    return whole


def write(path, seconds, chapters=()):
    whole = compose(seconds, chapters)
    rng = np.random.default_rng(7)
    samples = whole.T * 32767 + rng.uniform(-0.5, 0.5, whole.T.shape) + rng.uniform(-0.5, 0.5, whole.T.shape)
    with wave.open(path, "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(np.clip(np.round(samples), -32768, 32767).astype("<i2").tobytes())


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    write(sys.argv[1], float(sys.argv[2]), [float(a) for a in sys.argv[3:]])
