#!/usr/bin/env python3
"""The music, one General MIDI loop per cue (CC0, ours).

    midi.py [--out build/dos]     BRIEF.MID BAR.MID LAUNCH.MID WIN.MID LOSE.MID QUARTERS.MID LOUNGE.MID

Each is a loop in a type 1 Standard MIDI File: a tempo track, then a track
per instrument.  The four room songs are written out in tools/songs.py for
our OPL3 bank (tools/oplbank.py); LAUNCH, WIN and LOSE are still the short
placeholders below.
"""
import argparse
import os
import struct

PPQ = 480
NOTE = {n: i for i, n in enumerate(['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'])}


def pitch(name):
    """'A3' -> 57"""
    n, octave = name[:-1], int(name[-1])
    return 12 * (octave + 1) + NOTE[n]


def chord(root, kind):
    steps = {'maj': [0, 4, 7], 'min': [0, 3, 7], 'm7': [0, 3, 7, 10], 'maj7': [0, 4, 7, 11], '7': [0, 4, 7, 10],
             'sus': [0, 5, 7], '5': [0, 7, 12]}[kind]
    return [pitch(root) + s for s in steps]


def varlen(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.insert(0, (v & 0x7F) | 0x80)
        v >>= 7
    return bytes(out)


def track(events):
    """events: (tick, bytes) -> an MTrk chunk with an end-of-track"""
    events = sorted(events, key=lambda e: (e[0], e[1][0] & 0xF0 == 0x90))   # offs before ons at one tick
    data, last = b'', 0
    for t, msg in events:
        data += varlen(t - last) + msg
        last = t
    data += varlen(0) + b'\xFF\x2F\x00'
    return b'MTrk' + struct.pack('>L', len(data)) + data


class Song:
    def __init__(self, bpm, beats=4):
        self.bpm, self.beats = bpm, beats
        self.tracks = {}

    def program(self, ch, prog, vol=100):
        self.tracks.setdefault(ch, []).extend([(0, bytes((0xC0 | ch, prog))), (0, bytes((0xB0 | ch, 7, vol)))])

    def note(self, ch, beat, dur, p, vel=90):
        t0, t1 = int(beat * PPQ), int((beat + dur) * PPQ) - 1
        ev = self.tracks.setdefault(ch, [])
        ev.append((t0, bytes((0x90 | ch, p, vel))))
        ev.append((t1, bytes((0x80 | ch, p, 0))))

    def write(self, path):
        tempo = int(60_000_000 / self.bpm)
        meta = [(0, b'\xFF\x51\x03' + tempo.to_bytes(3, 'big')),
                (0, bytes((0xFF, 0x58, 4, self.beats, 2, 24, 8)))]
        chunks = [track(meta)] + [track(ev) for _ch, ev in sorted(self.tracks.items())]
        with open(path, 'wb') as f:
            f.write(b'MThd' + struct.pack('>LHHH', 6, 1, len(chunks), PPQ) + b''.join(chunks))


def pad(song, ch, chords, beats_each, vel=70, octave_shift=0):
    for i, (root, kind) in enumerate(chords):
        for p in chord(root, kind):
            song.note(ch, i * beats_each, beats_each, p + 12 * octave_shift, vel)


def bass(song, ch, roots, beats_each, pattern, vel=95):
    for i, root in enumerate(roots):
        for off, dur, step in pattern:
            song.note(ch, i * beats_each + off, dur, pitch(root) + step, vel)


def drums(song, bars, kick, snare, hat, vel=80, beats=4):
    for b in range(bars):
        for x in kick:
            song.note(9, b * beats + x, 0.25, 36, vel + 10)
        for x in snare:
            song.note(9, b * beats + x, 0.25, 38, vel)
        for x in hat:
            song.note(9, b * beats + x, 0.2, 42, vel - 25)


def launch_song():
    s = Song(136)
    s.program(0, 38, 100)     # synth bass
    s.program(1, 30, 80)      # overdriven guitar
    prog = ['E2', 'E2', 'C2', 'D2'] * 2
    bass(s, 0, prog, 4, [(x * 0.5, 0.4, 0) for x in range(8)], 100)
    for i, r in enumerate(prog):
        for p in chord(r[:-1] + '3', '5'):
            s.note(1, i * 4, 2, p, 85)
            s.note(1, i * 4 + 2.5, 1.5, p, 80)
    drums(s, 8, [0, 1.5, 2, 3.5], [1, 3], [x * 0.5 for x in range(8)], 95)
    return s


def win_song():
    s = Song(110)
    s.program(0, 61, 100)     # brass section
    s.program(1, 56, 110)     # trumpet
    prog = [('C3', 'maj'), ('F3', 'maj'), ('G3', 'maj'), ('C3', 'maj')] * 2
    pad(s, 0, prog, 4, 80)
    melody = ['G4', 'C5', 'E5', 'G5', 'F5', 'E5', 'D5', 'E5'] * 2
    for i, m in enumerate(melody):
        s.note(1, i * 2, 1.8, pitch(m), 100)
    drums(s, 8, [0, 2], [1, 3], [], 85)
    return s


def lose_song():
    s = Song(66)
    s.program(0, 48, 90)      # strings
    s.program(1, 68, 80)      # oboe
    prog = [('A2', 'min'), ('F2', 'maj'), ('D2', 'min'), ('E2', 'sus')] * 2
    pad(s, 0, prog, 4, 64, 1)
    line = ['E5', 'D5', 'C5', 'B4', 'A4', 'G4', 'A4', 'B4']
    for i, m in enumerate(line):
        s.note(1, i * 4, 3.5, pitch(m), 70)
    return s


SONGS = {'LAUNCH.MID': launch_song, 'WIN.MID': win_song, 'LOSE.MID': lose_song}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'dos'))
    args = ap.parse_args()
    import songs                        # (it imports this module: not at the top)
    SONGS.update(songs.SONGS)
    os.makedirs(args.out, exist_ok=True)
    for name, make in SONGS.items():
        make().write(os.path.join(args.out, name))
    print('%d songs in %s' % (len(SONGS), args.out))


if __name__ == '__main__':
    main()
