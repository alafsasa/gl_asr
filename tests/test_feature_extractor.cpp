#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "gl_asr/features/feature_extractor.hpp"

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

std::vector<float> sine(double frequencyHz, double amplitude, int sampleRate, double seconds) {
    const int frames = static_cast<int>(static_cast<double>(sampleRate) * seconds);
    std::vector<float> samples;
    samples.reserve(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(sampleRate);
        samples.push_back(
            static_cast<float>(amplitude * std::sin(2.0 * M_PI * frequencyHz * t)));
    }
    return samples;
}

bool extractDefaults(const std::vector<float>& samples, int sampleRate,
                     gl_asr::FeatureMatrix& features, std::string& error) {
    const gl_asr::FeatureExtractor extractor;
    return extractor.extract(samples, sampleRate, features, error);
}

void testDerivedParams() {
    const gl_asr::FeatureExtractor extractor;
    const gl_asr::DerivedParams params = extractor.derivedParams(16000);
    expect(params.windowSamples == 400, "25 ms window at 16 kHz is 400 samples");
    expect(params.hopSamples == 160, "10 ms shift at 16 kHz is 160 samples");
    expect(params.fftSize == 512, "fft size is next power of two (512)");
    expect(params.spectrumBins == 257, "spectrum bins = fft/2 + 1");
}

void testFrameCount() {
    std::vector<float> samples = sine(440.0, 0.1, 16000, 0.1);  // 1600 samples
    gl_asr::FeatureMatrix features;
    std::string error;
    expect(extractDefaults(samples, 16000, features, error), "extraction succeeds: " + error);
    expect(features.rows == 8, "frame count is 1 + (n - win)/hop = 8 (got " +
                                    std::to_string(features.rows) + ")");
    expect(features.cols == 80, "columns equal nMels (80)");
    expect(features.data.size() == 8 * 80, "matrix is dense");
}

void testShortAudioSingleFrame() {
    std::vector<float> samples(100, 0.1f);
    gl_asr::FeatureMatrix features;
    std::string error;
    expect(extractDefaults(samples, 16000, features, error), "short clip extracts: " + error);
    expect(features.rows == 1, "audio shorter than one window yields a single frame");
}

void testSineEnergyConcentratesNearTone() {
    std::vector<float> samples = sine(1000.0, 0.5, 16000, 0.5);
    gl_asr::FeatureMatrix features;
    std::string error;
    expect(extractDefaults(samples, 16000, features, error), "tone extracts: " + error);

    int bestCol = 0;
    for (int frame = 0; frame < features.rows; ++frame) {
        for (int col = 0; col < features.cols; ++col) {
            if (features.at(frame, col) > features.at(frame, bestCol)) {
                bestCol = col;
            }
        }
    }
    // mel(1000 Hz) falls between filter centers ~27 and ~28 for the defaults.
    expect(bestCol >= 25 && bestCol <= 31,
           "1 kHz tone peaks near its mel filter (col=" + std::to_string(bestCol) + ")");
}

void testSilenceHitsLogFloor() {
    std::vector<float> samples(1600, 0.0f);
    gl_asr::FeatureMatrix features;
    std::string error;
    expect(extractDefaults(samples, 16000, features, error), "silence extracts: " + error);

    const double expected = std::log(1e-10);
    bool allFloor = true;
    for (float value : features.data) {
        if (std::abs(static_cast<double>(value) - expected) > 1e-3) {
            allFloor = false;
            break;
        }
    }
    expect(allFloor, "silence maps to log floor everywhere");
}

void testAmplitudeScalesLogPower() {
    gl_asr::FeatureMatrix loud;
    gl_asr::FeatureMatrix quiet;
    std::string error;
    expect(extractDefaults(sine(440.0, 0.5, 16000, 0.1), 16000, loud, error),
           "loud extracts: " + error);
    expect(extractDefaults(sine(440.0, 0.25, 16000, 0.1), 16000, quiet, error),
           "quiet extracts: " + error);

    // Halving amplitude quarters power: peak log-energy drops by ln(4) ~ 1.386.
    const double loudMax = *std::max_element(loud.data.begin(), loud.data.end());
    const double quietMax = *std::max_element(quiet.data.begin(), quiet.data.end());
    expect(std::abs((loudMax - quietMax) - std::log(4.0)) < 0.05,
           "halving amplitude drops peak log-power by ln(4) (diff=" +
               std::to_string(loudMax - quietMax) + ")");
}

void testDumpCsv() {
    std::vector<float> samples = sine(440.0, 0.1, 16000, 0.1);
    gl_asr::FeatureMatrix features;
    std::string error;
    expect(extractDefaults(samples, 16000, features, error), "extraction succeeds: " + error);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "gl_asr_test_spectrogram.csv";
    expect(gl_asr::FeatureExtractor::dumpCsv(features, path.string(), error),
           "csv dump succeeds: " + error);

    std::ifstream file(path);
    expect(file.good(), "csv file is readable");
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    expect(lines.size() == static_cast<std::size_t>(features.rows),
           "csv row count matches frames");
    const int columns = static_cast<int>(
        std::count(lines[0].begin(), lines[0].end(), ',') + 1);
    expect(columns == features.cols, "csv column count matches mel bins");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void testInvalidConfigs() {
    std::vector<float> samples = sine(440.0, 0.1, 16000, 0.05);
    gl_asr::FeatureMatrix features;
    std::string error;

    gl_asr::MelConfig config;
    config.fmaxHz = 9000.0;  // above the 8 kHz Nyquist
    const gl_asr::FeatureExtractor badFmax(config);
    expect(!badFmax.extract(samples, 16000, features, error), "fmax above Nyquist fails");

    config = gl_asr::MelConfig{};
    config.nMels = 0;
    const gl_asr::FeatureExtractor badMels(config);
    expect(!badMels.extract(samples, 16000, features, error), "nMels = 0 fails");

    config = gl_asr::MelConfig{};
    config.fftSize = 128;  // smaller than the 400-sample window
    const gl_asr::FeatureExtractor badFft(config);
    expect(!badFft.extract(samples, 16000, features, error), "fft smaller than window fails");

    gl_asr::FeatureMatrix emptyOut;
    const gl_asr::FeatureExtractor goodExtractor;
    expect(!goodExtractor.extract({}, 16000, emptyOut, error), "empty samples fail");
}

}  // namespace

int main() {
    testDerivedParams();
    testFrameCount();
    testShortAudioSingleFrame();
    testSineEnergyConcentratesNearTone();
    testSilenceHitsLogFloor();
    testAmplitudeScalesLogPower();
    testDumpCsv();
    testInvalidConfigs();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "all feature extractor tests passed\n";
    return EXIT_SUCCESS;
}
