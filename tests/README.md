# raylib tests

## audio_music_loop_points

Tests for sample-accurate `Music` loop points (`SetMusicLoopPoints()` / `GetMusicLoopPoints()`):

- WAV `smpl` chunk and Ogg Vorbis `LOOPSTART`/`LOOPLENGTH` comment parsing (file and memory loaders)
- Seamless, sample-exact wrapping from loop end to loop start while streaming
  (including loops smaller than a stream sub-buffer and loops crossing sub-buffer boundaries)
- Full-stream looping unchanged (sample-identical) when no loop points are set
- Invalid loop points: reversed, zero-length and out-of-bounds ranges
- Seeking with loop points active (seek is clamped to the loop region)
- Formats without loop point support (QOA, XM) rejecting them gracefully
- Pause/resume preserving the stream position

The test includes `raudio.c` directly and drives `UpdateMusicStream()` manually,
simulating the audio device, so it runs deterministically without an audio device.

Fixtures in `resources/` are generated from the repository root with:

```sh
python3 tests/generate_fixtures.py
```

Build and run from the repository root:

```sh
gcc -o tests/audio_music_loop_points tests/audio_music_loop_points.c -Isrc -Isrc/external -lm -lpthread -ldl
./tests/audio_music_loop_points
```
