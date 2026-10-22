# Architecture

GL_ASR is organized as a set of small, composable stages. Each stage has a clear
input/output contract so it can be tested and replaced independently (e.g. swapping
the placeholder phoneme mapper for a real acoustic model later).

## Layout

```
include/gl_asr/        Public headers (one subdirectory per module)
src/                   Implementation files
  cli/                 Command-line parsing
  audio/               WAV I/O, PCM conversion        (Phase 1)
  preprocess/          Denoise, filter, normalize     (Phase 2)
  features/            Framing, FFT, mel filter bank  (Phase 3)
  phoneme/             Phoneme segmentation/mapping   (Phase 4)
  pipeline/            End-to-end orchestration       (Phase 5)
tests/                 Unit and integration tests
data/                  Sample audio and fixtures
docs/                  Design and usage documentation
scripts/               Helper scripts (conversion, analysis)
```

## Module contracts

- **AudioSource** — loads a file and exposes mono `float` samples in `[-1, 1]`
  plus the sample rate. Input format conversion (16-bit PCM etc.) is handled here.
- **Preprocessor** — takes samples + sample rate, applies noise removal, band
  filtering, and normalization. Stages are individually configurable.
- **FeatureExtractor** — takes samples + sample rate and produces a log-mel
  spectrogram (`frames x n_mels`).
- **PhonemeMapper** — takes features (and later acoustic model scores) and
  produces timed phoneme segments.
- **Pipeline** — wires the stages together and writes artifacts to the output
  directory.

## Dependencies

Both are available system-wide on the development machine:

- `libsndfile` — robust WAV/other audio decoding.
- `FFTW3` (`fftw3f`) — FFT for the spectrogram stage.

`libsamplerate` is also installed and may be used if resampling is needed.

## Build

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/gl_asr audio.wav -v
```
