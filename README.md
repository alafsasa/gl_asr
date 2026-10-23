# GL_ASR

A C++20 automatic speech recognition project. The current MVP implements the
**front-end of an ASR pipeline**: audio decoding, preprocessing, log-mel feature
extraction, and heuristic phoneme-like segmentation. Acoustic modeling and
decoding are future work.

The pipeline produces inspectable intermediate artifacts (`spectrogram.csv`,
`phonemes.txt`) so every stage can be debugged or replaced independently.

## Pipeline

| Stage | What it does | Output |
|---|---|---|
| Load | Decodes any libsndfile-supported format, mixes channels to mono | mono `float` samples in `[-1, 1]` |
| Preprocess | DC-offset removal, 80 Hz high-pass, 7.5 kHz low-pass, RMS normalization to −20 dBFS | cleaned samples + stats |
| Features | 25 ms Hann window, 10 ms hop, 512-point FFT, 80 triangular mel filters, natural log | `spectrogram.csv` (`frames × 80`) |
| Phonemes | Energy/silence detection, speech-region merging, ~90 ms slices, spectral broad-class labels | `phonemes.txt` |

> **Note:** the labels in `phonemes.txt` (`sil`, `V`, `C`, `F`) are broad
> phonetic *classes*, not recognized phonemes. A real acoustic model is needed
> for true phoneme labels.

## Requirements

- CMake 3.20+ and a C++20 compiler
- [libsndfile](https://libsndfile.github.io/libsndfile/)
- [FFTW3](https://www.fftw.org/) (`fftw3f`), discovered through `pkg-config`
- Optional: `ffmpeg` for `scripts/convert_to_wav.sh`

On Debian/Ubuntu:

```sh
sudo apt install cmake g++ pkg-config libsndfile1-dev libfftw3-dev ffmpeg
```

## Build & Test

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Usage

```sh
# Run the full pipeline on the bundled sample (16 kHz mono WAV)
./build/gl_asr audio.wav

# Custom output directory with verbose stage details
./build/gl_asr audio.wav -o out -v

# Tune feature extraction / normalization
./build/gl_asr audio.wav --n-mels 40 --target-dbfs -23

# Run only the first two stages
./build/gl_asr audio.wav --no-features
```

Convert other audio formats to the supported 16 kHz mono 16-bit PCM layout:

```sh
scripts/convert_to_wav.sh input.m4a audio.wav
```

### Options

| Option | Description |
|---|---|
| `-o, --out <dir>` | Output directory for generated artifacts (default: `output`) |
| `--no-preprocess` | Skip denoising, filtering, and normalization |
| `--target-dbfs <db>` | RMS normalization target in dBFS (default: `-20`) |
| `--no-features` | Skip log-mel spectrogram extraction (also skips phonemes) |
| `--n-mels <n>` | Number of mel filter bins (default: `80`) |
| `--no-phonemes` | Skip heuristic phoneme segmentation |
| `-v, --verbose` | Print derived parameters and output paths |
| `-h, --help` | Show usage |
| `-V, --version` | Show version |

### Example run

```text
$ ./build/gl_asr audio.wav -v
Loaded audio.wav: 13.5467 s, 16000 Hz, 1 channel -> mono mix (216747 samples)
output: output
Preprocessed: RMS -30.2 -> -20.0 dBFS (gain +11.9 dB), peak 0.82
Features: 1353 frames x 80 mel bins
Wrote output/spectrogram.csv
window: 400 samples, hop: 160 samples, fft: 512
Phonemes: 59 segments -> output/phonemes.txt
GL_ASR 0.1.0: pipeline stops after phoneme mapping.
```

## Output artifacts

### `spectrogram.csv`

No header. One row per frame, one comma-separated natural-log mel value per bin:

```text
-12.0341,-11.7822,...
```

### `phonemes.txt`

Comment lines begin with `#`, followed by `start_sec end_sec label` rows:

```text
# start_sec end_sec label
0.000 0.610 sil
0.610 0.700 V
```

Labels:

| Label | Meaning |
|---|---|
| `sil` | silence / background |
| `V` | vowel-like — most energy below ~1 kHz |
| `F` | fricative-like — strong energy above ~4 kHz or high spectral centroid |
| `C` | other consonant-like |

## Project layout

```text
include/gl_asr/       Public headers, one subdirectory per module
src/
  cli/                Command-line parsing
  audio/              Decoding and mono mixdown
  preprocess/         DC removal, band-limiting, normalization
  features/           Framing, FFT, mel filter bank, log-mel output
  phoneme/            Heuristic segmentation and export
  pipeline/           End-to-end orchestration
  main.cpp            CLI front-end
tests/                Unit tests plus an end-to-end pipeline test
scripts/              Helper scripts
```

`gl_asr_core` contains all pipeline modules; the `gl_asr` executable is a thin
CLI over `gl_asr::Pipeline`.

## Testing

Six test suites run through CTest:

- `cli_options` — argument parsing, defaults, and error cases
- `audio_source` — WAV loading, stereo-to-mono mixdown, invalid input
- `preprocessor` — filters, normalization, configuration validation
- `feature_extractor` — framing, tone placement, mel bank, CSV export
- `phoneme_mapper` — regions, merging, splitting, export format
- `pipeline_integration` — full run from synthetic WAV to both artifacts,
  stage toggles, and failure modes

## Limitations & roadmap

Current limitations:

- Phoneme output is heuristic and uses broad classes only.
- No acoustic model, language model, or decoder yet.
- No resampling: input keeps its native sample rate. The default preprocessing
  band-limit (7.5 kHz) requires a sample rate above 15 kHz; resample first for
  8 kHz input.
- No streaming, VAD, or real-time operation.

Planned next steps:

1. Replace heuristic labeling with an acoustic model (GMM-HMM or neural).
2. Add an n-gram/neural language model and CTC/beam-search decoding.
3. Add ONNX Runtime integration, VAD, and streaming support.
