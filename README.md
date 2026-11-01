# StemDeck

A stem player for DJs: two decks, each playing a track split into four stems, and a mixer that
sends every stem to one of five buses. It is part of the [A³ Audio](https://github.com/a3-audio/a3-system)
system: buses 1–4 and AUX feed A³ Core, where each stem gets its own channel and can move round
the room. Each stem also has a CUE key that sends it to a separate stereo pair (JACK ports `cue_L`,
`cue_R`) for pre-listening: taken after the stem knob, before the mute and the channel fader, with
any number of stems at once. Core is not told about it. StemDeck can also be the tempo master of a Pro DJ Link network when there are no CDJs.

It makes its own stems, too: drop a stereo track on the library and Demucs (`htdemucs`) splits it
into drums, bass, other and vocals while the decks play on. See
[Making stems from a stereo track](https://a3-audio.github.io/a3-doc/user/stemdeck.html#stemdeck-stem-creator).

![Four StemDecks around the A³ desk and A³ Motion, each deck playing its own track](https://a3-audio.github.io/img/a3-setup.jpg)

![StemDeck main window](https://a3-audio.github.io/a3-doc/_images/stemdeck-one-deck-playing.png)

**Documentation: [StemDeck](https://a3-audio.github.io/a3-doc/user/stemdeck.html)** and
[StemDeck × A³ Motion](https://a3-audio.github.io/a3-doc/user/stemdeck-with-motion.html)
(stem sets, decks, mixer, sync, audio routing, the rig service, troubleshooting).
Everything else: https://a3-audio.github.io/a3-doc/

## Build and run

Needs CMake ≥ 3.22, a C++17 compiler, pkg-config, **JUCE 9.0.3** (the A³ pin, `JUCE_VERSION` in a3-system's `installer/roles/base.py`; found via `CMAKE_PREFIX_PATH`,
default `~/local/juce`) with its Linux dependencies, and on Debian:

```sh
apt install libjack-jackd2-dev libflac-dev libvorbis-dev libogg-dev librubberband-dev \
    libavformat-dev libavcodec-dev libavutil-dev
```

FFmpeg's libraries decode NI Stems files (`.stem.mp4`), which StemDeck plays straight from the library.

```sh
./start.sh
```

It configures a Release build in `build/` on the first run, rebuilds what changed and starts
StemDeck on PipeWire's JACK API (`pw-jack` where it is not system-wide) or ALSA. The binary is `build/StemDeck_artefacts/Release/StemDeck`.
Details: [Build and start](https://a3-audio.github.io/a3-doc/user/stemdeck.html#stemdeck-build).

The stem creator needs a one-time separator setup (about 1 GB):

```sh
sudo apt install ffmpeg python3-venv
tools/setup-separator.sh
```

Stem sets for the library can also be made off the rig, on a rented cloud GPU, by
[a3-stem-separator](https://github.com/a3-audio/a3-stem-separator): this repository's submodule
`a3-stem-separator` (since 2026-10-31; `git submodule update --init a3-stem-separator`).
StemDeck's build does not need it.

## Audio

PipeWire is the only audio server; StemDeck is a JACK client of it. On a machine without the
A³ Core, `stemdeck-without-core.xml` (QjackCtl patchbay, started by `qjackctl-stemdeck.service`)
sends the buses to the Core over AES67 and takes the return back the same way.

## Tests

The C++ unit tests need GoogleTest (`libgtest-dev`):

```sh
export CMAKE_PREFIX_PATH=$HOME/local/juce   # the top-level CMakeLists still needs JUCE
cmake -S . -B build -DSTEMDECK_TESTS=ON
cmake --build build --target stemdeck-tests
ctest --test-dir build
```

`ctest` runs what was built; it does not build. The Python tools have their own tests:
`python3 -m unittest discover -s tools/tests`.

How often the analysis finds the bar's one where a DJ put it by hand: with the tests configured,
`cmake --build build --target stemdeck-downbeat-eval` builds a measuring tool (not part of `ctest`,
it needs real tracks) that reads the corrected grids from an `analysis.xml` and analyses the sets
it finds under a stems folder again:

```sh
build/tools/downbeat-eval/stemdeck-downbeat-eval_artefacts/Release/stemdeck-downbeat-eval \
    ~/.config/StemDeck/analysis.xml ~/stems [folder for decoded audio] [-v]
```

## Controller, GRID, AUTO DJ, session

The Stanton SCS.3d mapping, GRID, AUTO DJ, the MASTER handover and the session restore are in
the documentation: [StemDeck](https://a3-audio.github.io/a3-doc/user/stemdeck.html)
([SCS.3d](https://a3-audio.github.io/a3-doc/user/stemdeck.html#stemdeck-scs3d),
[GRID](https://a3-audio.github.io/a3-doc/user/stemdeck.html#stemdeck-grid),
[AUTO DJ](https://a3-audio.github.io/a3-doc/user/stemdeck.html#stemdeck-autodj),
[Session](https://a3-audio.github.io/a3-doc/user/stemdeck.html#stemdeck-session)).

## Pro DJ Link

Pro DJ Link and rekordbox are trademarks of AlphaTheta Corporation; Pioneer DJ is a trademark
of Pioneer Corporation; CDJ is a product name of theirs. A³ is not affiliated with, endorsed or
certified by AlphaTheta or Pioneer. StemDeck's Pro DJ Link support is an independent
implementation for interoperability, built from public documentation of the protocol:
Deep Symmetry's [DJ Link analysis](https://djl-analysis.deepsymmetry.org/) and
[prolink-connect](https://github.com/EvanPurkhiser/prolink-connect).

Using it on a network you do not run is at your own risk: ask the venue before joining their
DJ Link network. See [Trademarks and Pro DJ Link](https://a3-audio.github.io/a3-doc/ressources/trademarks.html).

## License

GPL-3.0-or-later. REUSE-compliant: the licenses are in `LICENSES/`, which file has
which is in `.reuse/dep5`.

Key lock links the [Rubber Band Library](https://breakfastquay.com/rubberband/)
(`librubberband`, GPL-2.0-or-later), which may be used under GPL-3.0-or-later as StemDeck is.
