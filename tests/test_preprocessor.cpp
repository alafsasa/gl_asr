#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "gl_asr/preprocess/preprocessor.hpp"

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

std::vector<float> sine(double frequencyHz, double amplitude, int sampleRate, double seconds,
                        double offset = 0.0) {
    const int frames = static_cast<int>(static_cast<double>(sampleRate) * seconds);
    std::vector<float> samples;
    samples.reserve(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(sampleRate);
        samples.push_back(static_cast<float>(offset + amplitude * std::sin(2.0 * M_PI * frequencyHz * t)));
    }
    return samples;
}

double rmsOf(const std::vector<float>& samples, std::size_t skip = 0) {
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = skip; i < samples.size(); ++i) {
        sum += static_cast<double>(samples[i]) * samples[i];
        ++count;
    }
    return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}

double meanOf(const std::vector<float>& samples) {
    double sum = 0.0;
    for (float sample : samples) {
        sum += static_cast<double>(sample);
    }
    return samples.empty() ? 0.0 : sum / static_cast<double>(samples.size());
}

void testDcOffsetRemoved() {
    gl_asr::PreprocessConfig config;
    config.normalizeMode = gl_asr::NormalizeMode::None;
    gl_asr::Preprocessor preprocessor(config);

    std::vector<float> samples = sine(440.0, 0.1, 16000, 0.5, 0.5);
    gl_asr::PreprocessStats stats;
    std::string error;
    expect(preprocessor.process(samples, 16000, &stats, error), "dc-offset clip processes: " + error);
    expect(std::abs(meanOf(samples)) < 1e-3, "DC offset is removed");
}

void testHighPassAttenuatesRumble() {
    gl_asr::PreprocessConfig config;
    config.normalizeMode = gl_asr::NormalizeMode::None;
    gl_asr::Preprocessor preprocessor(config);

    const double inputRms = 0.5 / std::sqrt(2.0);

    std::vector<float> rumble = sine(40.0, 0.5, 16000, 1.0);
    gl_asr::PreprocessStats stats;
    std::string error;
    expect(preprocessor.process(rumble, 16000, &stats, error), "rumble clip processes: " + error);
    const double rumbleRms = rmsOf(rumble, 1600);  // skip filter transient
    expect(rumbleRms < 0.3 * inputRms, "40 Hz rumble is attenuated (rms=" +
                                            std::to_string(rumbleRms) + ")");

    std::vector<float> speech = sine(440.0, 0.5, 16000, 1.0);
    expect(preprocessor.process(speech, 16000, &stats, error), "speech clip processes: " + error);
    const double speechRms = rmsOf(speech, 1600);
    expect(speechRms > 0.9 * inputRms, "440 Hz tone survives the high-pass (rms=" +
                                            std::to_string(speechRms) + ")");
}

void testLowPassAttenuatesHiss() {
    gl_asr::PreprocessConfig config;
    config.normalizeMode = gl_asr::NormalizeMode::None;
    config.highpassCutoffHz = 0.0;  // isolate the low-pass behaviour
    config.lowpassCutoffHz = 2000.0;
    gl_asr::Preprocessor preprocessor(config);

    const double inputRms = 0.5 / std::sqrt(2.0);

    std::vector<float> hiss = sine(6000.0, 0.5, 16000, 1.0);
    gl_asr::PreprocessStats stats;
    std::string error;
    expect(preprocessor.process(hiss, 16000, &stats, error), "hiss clip processes: " + error);
    const double hissRms = rmsOf(hiss, 160);
    expect(hissRms < 0.5 * inputRms, "6 kHz tone is attenuated (rms=" + std::to_string(hissRms) + ")");

    std::vector<float> low = sine(100.0, 0.5, 16000, 1.0);
    expect(preprocessor.process(low, 16000, &stats, error), "low clip processes: " + error);
    const double lowRms = rmsOf(low, 160);
    expect(lowRms > 0.95 * inputRms, "100 Hz tone survives the low-pass (rms=" +
                                          std::to_string(lowRms) + ")");
}

void testRmsNormalization() {
    gl_asr::Preprocessor preprocessor;  // defaults: Rms, -20 dBFS

    std::vector<float> samples = sine(440.0, 0.01, 16000, 0.5);
    gl_asr::PreprocessStats stats;
    std::string error;
    expect(preprocessor.process(samples, 16000, &stats, error), "quiet clip processes: " + error);

    const double outputRms = rmsOf(samples);
    expect(std::abs(outputRms - 0.1) < 0.01, "RMS is normalized to -20 dBFS (rms=" +
                                                  std::to_string(outputRms) + ")");
    expect(std::abs(stats.gainDb - 20.0 * std::log10(0.1 / (0.01 / std::sqrt(2.0)))) < 1.0,
           "reported gain matches expectation");
    expect(stats.outputPeak < 1.0, "normalized signal does not clip");
}

void testMaxGainClamp() {
    gl_asr::Preprocessor preprocessor;  // defaults cap gain at +30 dB

    std::vector<float> samples = sine(440.0, 1e-5, 16000, 0.5);
    gl_asr::PreprocessStats stats;
    std::string error;
    expect(preprocessor.process(samples, 16000, &stats, error), "near-silence processes: " + error);

    expect(stats.gainDb > 29.9 && stats.gainDb < 30.1, "gain is capped at +30 dB (gain=" +
                                                            std::to_string(stats.gainDb) + ")");
    expect(rmsOf(samples) < 0.001, "near-silence is not amplified into full scale");
}

void testPeakNormalization() {
    gl_asr::PreprocessConfig config;
    config.normalizeMode = gl_asr::NormalizeMode::Peak;
    gl_asr::Preprocessor preprocessor(config);

    std::vector<float> samples = sine(440.0, 0.25, 16000, 0.5);
    gl_asr::PreprocessStats stats;
    std::string error;
    expect(preprocessor.process(samples, 16000, &stats, error), "peak clip processes: " + error);
    expect(std::abs(stats.outputPeak - 0.9) < 0.01, "peak is normalized to 0.9 (peak=" +
                                                         std::to_string(stats.outputPeak) + ")");
}

void testEmptyInputFails() {
    gl_asr::Preprocessor preprocessor;
    std::vector<float> samples;
    gl_asr::PreprocessStats stats;
    std::string error;
    expect(!preprocessor.process(samples, 16000, &stats, error), "empty input fails");
    expect(!error.empty(), "empty input reports an error");
}

void testCutoffAboveNyquistFails() {
    gl_asr::PreprocessConfig config;
    config.lowpassCutoffHz = 9000.0;  // Nyquist is 8000 Hz at 16 kHz
    gl_asr::Preprocessor preprocessor(config);

    std::vector<float> samples = sine(440.0, 0.1, 16000, 0.1);
    gl_asr::PreprocessStats stats;
    std::string error;
    expect(!preprocessor.process(samples, 16000, &stats, error), "cutoff above Nyquist fails");
    expect(error.find("Nyquist") != std::string::npos, "Nyquist error is reported");
}

}  // namespace

int main() {
    testDcOffsetRemoved();
    testHighPassAttenuatesRumble();
    testLowPassAttenuatesHiss();
    testRmsNormalization();
    testMaxGainClamp();
    testPeakNormalization();
    testEmptyInputFails();
    testCutoffAboveNyquistFails();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "all preprocessor tests passed\n";
    return EXIT_SUCCESS;
}
