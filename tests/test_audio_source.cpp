#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <sndfile.h>

#include "gl_asr/audio/audio_source.hpp"

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

bool writeWav(const std::filesystem::path& path, int sampleRate, int channels,
              const std::vector<float>& interleaved) {
    SF_INFO info{};
    info.samplerate = sampleRate;
    info.channels = channels;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SNDFILE* file = sf_open(path.string().c_str(), SFM_WRITE, &info);
    if (file == nullptr) {
        std::cerr << "cannot create test wav: " << sf_strerror(nullptr) << "\n";
        return false;
    }
    const auto frames = static_cast<sf_count_t>(interleaved.size() / static_cast<std::size_t>(channels));
    const sf_count_t written = sf_writef_float(file, interleaved.data(), frames);
    sf_close(file);
    return written == frames;
}

void testStereoIsMixedToMono(const std::filesystem::path& path) {
    constexpr int kSampleRate = 16000;
    constexpr int kFrames = 1600;
    std::vector<float> interleaved;
    interleaved.reserve(static_cast<std::size_t>(kFrames) * 2);
    for (int i = 0; i < kFrames; ++i) {
        const float value = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f *
                                            static_cast<float>(i) / kSampleRate);
        interleaved.push_back(value);
        interleaved.push_back(0.0f);
    }

    if (!writeWav(path, kSampleRate, 2, interleaved)) {
        ++failures;
        return;
    }

    gl_asr::AudioSource source;
    std::string error;
    expect(source.load(path.string(), error), "stereo wav loads: " + error);

    const gl_asr::AudioClip& clip = source.clip();
    expect(clip.sampleRate == kSampleRate, "sample rate is preserved");
    expect(clip.channels == 2, "source channel count is preserved");
    expect(clip.frameCount() == kFrames, "frame count matches");
    expect(std::abs(clip.durationSeconds() - 0.1) < 1e-6, "duration is computed");

    bool mixDownMatches = true;
    for (int i = 0; i < kFrames; ++i) {
        const float expected = (interleaved[static_cast<std::size_t>(i) * 2] +
                                interleaved[static_cast<std::size_t>(i) * 2 + 1]) /
                               2.0f;
        if (std::abs(clip.samples[static_cast<std::size_t>(i)] - expected) > 1e-3f) {
            mixDownMatches = false;
            break;
        }
    }
    expect(mixDownMatches, "channels are averaged into mono");

    const std::vector<std::int16_t> pcm = source.toPcm16();
    expect(pcm.size() == clip.samples.size(), "pcm16 size matches samples");
}

void testMonoLoads(const std::filesystem::path& path) {
    std::vector<float> samples(800, 0.25f);
    if (!writeWav(path, 8000, 1, samples)) {
        ++failures;
        return;
    }

    gl_asr::AudioSource source;
    std::string error;
    expect(source.load(path.string(), error), "mono wav loads: " + error);
    expect(source.sampleRate() == 8000, "mono sample rate");
    expect(source.clip().channels == 1, "mono channel count");
    expect(source.clip().frameCount() == 800, "mono frame count");
}

void testMissingFileFails() {
    gl_asr::AudioSource source;
    std::string error;
    const bool ok = source.load("/tmp/gl_asr_does_not_exist.wav", error);
    expect(!ok, "missing file fails");
    expect(!error.empty(), "missing file reports an error");
    expect(source.clip().empty(), "failed load leaves source empty");
}

}  // namespace

int main() {
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path();
    const std::filesystem::path stereoPath = tempDir / "gl_asr_test_stereo.wav";
    const std::filesystem::path monoPath = tempDir / "gl_asr_test_mono.wav";

    testStereoIsMixedToMono(stereoPath);
    testMonoLoads(monoPath);
    testMissingFileFails();

    std::error_code ec;
    std::filesystem::remove(stereoPath, ec);
    std::filesystem::remove(monoPath, ec);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "all audio tests passed\n";
    return EXIT_SUCCESS;
}
