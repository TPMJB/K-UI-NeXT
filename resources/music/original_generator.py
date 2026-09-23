#!/usr/bin/env python3
"""Render five original K-UI synth loops with no sampled material.

Uses only Python's standard library. All notes, instruments and echoes are defined here; no music
service, copyrighted recording or external sound bank is involved.
"""
from pathlib import Path
import argparse
from array import array
import math
import random
import sys
import wave

RATE = 22050
BPM = 80
BEAT = 60 / BPM
SECONDS = 32 * BEAT
TRACKS = ('menu.wav', 'neon-circuit.wav', 'orbital-drift.wav',
          'midnight-vector.wav', 'chrome-horizon.wav')


def compose():
    size = round(RATE * SECONDS)
    out = array('d', [0.0]) * size

    def add(start, note, seconds, gain, instrument):
        hz = 440 * 2 ** ((note - 69) / 12)
        first = round(start * RATE)
        for i in range(round(seconds * RATE)):
            t = i / RATE
            phase = 2 * math.pi * hz * t
            if instrument == 'pad':
                tone = (math.sin(phase) + .24 * math.sin(phase * 2 + .15)
                        + .1 * math.sin(phase * 3))
                envelope = min(t / .55, 1) * min((seconds - t) / 1.1, 1)
                tone *= .88 + .12 * math.sin(2 * math.pi * .22 * t)
            elif instrument == 'bell':
                tone = math.sin(phase + 1.1 * math.exp(-t * 3) * math.sin(2 * phase))
                envelope = (1 - math.exp(-t * 90)) * math.exp(-t * 1.35)
                envelope *= min((seconds - t) / .15, 1)
            else:
                tone = math.sin(phase) + .12 * math.sin(phase * 2)
                envelope = min(t / .08, 1) * min((seconds - t) / .3, 1)
            # Wrap releases into the beginning: the loop keeps its ambience.
            out[(first + i) % size] += gain * tone * max(0,min(envelope,1))

    # Dmaj9 -> Bm9 -> Gmaj9 -> A6/9, two bars per chord.
    chords = [(38,[62,66,69,73,76]), (35,[62,66,69,73,74]),
              (31,[59,62,66,69,74]), (33,[61,64,66,69,71])]
    for c, (root, notes) in enumerate(chords):
        at = c * 8 * BEAT
        for j, note in enumerate(notes):
            add(at + j * .018, note, 8 * BEAT + .85, .041, 'pad')
        for beat in (0,4):
            add(at + beat * BEAT, root, 3.6 * BEAT, .055, 'bass')
    melody = [(0.5,78), (2.5,76), (5,73), (7,69),
              (9,74), (11.5,73), (14,69),
              (16.5,71), (19,74), (21.5,78),
              (24.5,76), (27,73), (29,71), (31,69)]
    for beat, note in melody:
        add(beat * BEAT, note, 2.7, .052, 'bell')
    dry = array('d', out)
    for delay, gain in ((.375,.18),(.75,.11),(1.125,.055),(1.77,.035)):
        shift = round(delay * RATE)
        for i in range(size):
            out[i] += gain * dry[(i-shift) % size]
    mean = sum(out) / size
    factor = .44 / max(abs(value-mean) for value in out)
    pcm = array('h', (round((value-mean)*factor*32767) for value in out))
    if sys.byteorder != 'little':
        pcm.byteswap()
    return pcm


def synthwave(style):
    """Four arrangements, band-limited additive synths and synthesized drums.

    Events and delay tails wrap around the exact eight-bar loop. Nothing is
    synthesized on the Dreamcast; this runs only on the build host.
    """
    bpm, roots, brightness, percussion = (
        (100, (42, 38, 45, 40), .65, .8),  # Neon Circuit: F# minor, driving arp
        (80, (40, 36, 43, 38), .25, .28),  # Orbital Drift: E minor, sparse pulse
        (110, (45, 41, 48, 43), .85, 1.0), # Midnight Vector: A minor, electro
        (90, (38, 35, 31, 33), .45, .5),   # Chrome Horizon: D major, warm/FM
    )[style]
    beat = 60 / bpm
    size = round(RATE * 32 * beat)
    out = array('d', [0.0]) * size
    rng = random.Random(0x4b5549 + style)

    def note(at, midi, duration, gain, voice):
        hz = 440 * 2 ** ((midi - 69) / 12)
        first = round(at * RATE)
        for i in range(round(duration * RATE)):
            t = i / RATE
            phase = math.tau * hz * t
            if voice == 'pad':
                tone = (math.sin(phase) + .45 * math.sin(phase * 1.003)
                        + brightness * .22 * math.sin(phase * 2))
                env = min(t / .3, 1) * min((duration - t) / .8, 1)
            elif voice == 'arp':
                tone = (math.sin(phase) + brightness * math.sin(2 * phase) * .38
                        + brightness * math.sin(3 * phase) * .17)
                env = min(t / .012, 1) * math.exp(-t * 5) * min((duration-t)/.06, 1)
            elif voice == 'lead':
                tone = math.sin(phase + .8 * math.sin(2 * phase) * math.exp(-t*3))
                env = min(t / .018, 1) * math.exp(-t * 1.8) * min((duration-t)/.1, 1)
            else:
                tone = math.sin(phase) + .25 * math.sin(phase * 2) + .08 * math.sin(phase * 3)
                env = min(t / .01, 1) * min((duration-t)/.05, 1)
            out[(first+i) % size] += gain * tone * max(0, env)

    def drum(at, kind, gain):
        duration = .32 if kind == 'kick' else .22 if kind == 'snare' else .055
        first = round(at * RATE)
        previous = 0.0
        for i in range(round(duration * RATE)):
            t = i / RATE
            noise = rng.uniform(-1, 1)
            if kind == 'kick':
                phase = math.tau * (48*t + 95*.025*(1-math.exp(-t/.025)))
                tone = math.sin(phase) * math.exp(-t*15)
            elif kind == 'snare':
                tone = (.7*noise + .3*math.sin(math.tau*180*t))*math.exp(-t*23)
            else:
                tone = (noise-previous)*.5*math.exp(-t*65)
            previous = noise
            env = min(t/.001, 1) * min((duration-t)/.008, 1)
            out[(first+i) % size] += gain * tone * max(0, env)

    for c, root in enumerate(roots):
        at = c * 8 * beat
        third = 4 if (style == 3 and c != 1) or (style != 3 and c in (1,2)) else 3
        chord = [root+24, root+24+third, root+31, root+38]
        for pitch in chord:
            note(at, pitch, beat*8+.7, .025, 'pad')
        for step in range(16):
            note(at+step*beat/2, root+(12 if step%4==3 else 0), beat*.38, .07, 'bass')
            if style != 1 or step%2==0:
                note(at+step*beat/2, chord[(step+(step//4))%4], beat*.8, .045, 'arp')
        for step,pitch in ((1,chord[2]+12),(3.5,chord[1]+12),(6,chord[0]+12)):
            note(at+step*beat, pitch, beat*1.7, .027, 'lead')
    for b in range(32):
        if b%4 in (0,2) or (style == 2 and b%8==7):
            drum(b*beat,'kick',.19*percussion)
        if b%4 in (1,3):
            drum(b*beat,'snare',.095*percussion)
        for half in (0,.5):
            drum((b+half)*beat,'hat',(.026 if half else .018)*percussion)
    dry = array('d', out)
    for delay,gain in ((beat*.75,.16),(beat*1.5,.065)):
        shift = round(delay*RATE)
        for i in range(size):
            out[i] += dry[(i-shift)%size]*gain
    mean = sum(out)/size
    factor = .44 / max(abs(v-mean) for v in out)
    pcm = array('h', (round((v-mean)*factor*32767) for v in out))
    if sys.byteorder != 'little':
        pcm.byteswap()
    return pcm


def write_track(path, pcm):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'wb') as file:
        file.setparams((1,2,RATE,0,'NONE','not compressed'))
        file.writeframes(pcm.tobytes())
    print(f'{path}: {len(pcm)/RATE:.1f}s, mono PCM16, {RATE}Hz, '
          f'{path.stat().st_size:,} bytes')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, help='Render only After Hours to this WAV (legacy option)')
    parser.add_argument('--directory', type=Path, default=Path(__file__).resolve().parents[1] /
                        'applications/launch_app/music')
    args = parser.parse_args()
    if args.output:
        write_track(args.output, compose())
    else:
        for i, name in enumerate(TRACKS):
            write_track(args.directory/name, compose() if i == 0 else synthwave(i-1))


if __name__ == '__main__':
    main()
