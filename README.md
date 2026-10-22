This is a GL_ASR(Automatic Speech Recognition) project.

## Build & Run

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure

./build/gl_asr audio.wav -v
```