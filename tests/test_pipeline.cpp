#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sndfile.h>

#include "gl_asr/pipeline/pipeline.hpp"

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
    const auto frames =
        static_cast<sf_count_t>(interleaved.size() / static_cast<std::size_t>(channels));
    const sf_count_t written = sf_writef_float(file, interleaved.data(), frames);
    sf_close(file);
    return written == frames;
}

std::vector<std::string> readLines(const std::filesystem::path& path) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::size_t countColumns(const std::string& line) {
    std::size_t columns = 1;
    for (char c : line) {
        if (c == ',') {
            ++columns;
        }
    }
    return columns;
}

void makeTestWav(const std::filesystem::path& path) {
    constexpr int kSampleRate = 16000;
    constexpr double kSeconds = 1.0;
    const int frames = static_cast<int>(kSampleRate * kSeconds);
    std::vector<float> samples;
    samples.reserve(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        samples.push_back(static_cast<float>(
            0.1 * std::sin(2.0 * 3.141592653589793 * 440.0 * static_cast<double>(i) /
                           kSampleRate)));
    }
    if (!writeWav(path, kSampleRate, 1, samples)) {
        ++failures;
    }
}

void testFullPipeline(const std::filesystem::path& root, const std::filesystem::path& wavPath) {
    gl_asr::PipelineConfig config;
    config.outputDir = (root / "full").string();

    gl_asr::Pipeline pipeline(config);
    gl_asr::PipelineResult result;
    std::string error;
    const bool ok = pipeline.run(wavPath.string(), result, error);
    expect(ok, "full pipeline succeeds: " + error);
    if (!ok) {
        return;
    }

    expect(result.clip.sampleRate == 16000, "pipeline preserves sample rate");
    expect(result.clip.channels == 1, "pipeline preserves channel count");
    expect(result.clip.frameCount() == 16000, "pipeline preserves frame count");

    expect(result.preprocessed, "preprocessing ran");
    expect(std::abs(result.preprocessStats.outputRmsDbfs - (-20.0)) < 0.1,
           "output RMS hits the default -20 dBFS target");
    expect(result.preprocessStats.outputPeak <= 1.0, "output peak stays in range");

    // 25 ms window / 10 ms hop at 16 kHz => 400 / 160 samples, 98 frames for 1 s.
    expect(result.featuresExtracted, "feature extraction ran");
    expect(result.features.rows == 98, "expected frame count for 1 s at 16 kHz");
    expect(result.features.cols == 80, "expected default mel bin count");
    expect(result.features.data.size() ==
               static_cast<std::size_t>(result.features.rows) *
                   static_cast<std::size_t>(result.features.cols),
           "feature matrix is fully populated");
    expect(result.featureParams.windowSamples == 400, "derived window size");
    expect(result.featureParams.hopSamples == 160, "derived hop size");
    expect(result.featureParams.fftSize == 512, "derived FFT size");

    expect(std::filesystem::is_regular_file(result.spectrogramPath),
           "spectrogram.csv was written");
    const std::vector<std::string> csvLines = readLines(result.spectrogramPath);
    expect(csvLines.size() == static_cast<std::size_t>(result.features.rows),
           "spectrogram.csv has one row per frame");
    if (!csvLines.empty()) {
        expect(countColumns(csvLines.front()) ==
                   static_cast<std::size_t>(result.features.cols),
               "spectrogram.csv has one column per mel bin");
    }

    expect(result.phonemesMapped, "phoneme mapping ran");
    expect(!result.segments.empty(), "phoneme mapping produced segments");
    expect(std::filesystem::is_regular_file(result.phonemesPath), "phonemes.txt was written");

    const std::vector<std::string> phonemeLines = readLines(result.phonemesPath);
    std::size_t dataLines = 0;
    bool formatValid = true;
    bool labelsValid = true;
    for (const std::string& line : phonemeLines) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::istringstream stream(line);
        double start = 0.0;
        double end = 0.0;
        std::string label;
        std::string extra;
        if (!(stream >> start >> end >> label) || (stream >> extra) || start < 0.0 ||
            end <= start) {
            formatValid = false;
            break;
        }
        if (label != "sil" && label != "V" && label != "C" && label != "F") {
            labelsValid = false;
        }
        ++dataLines;
    }
    expect(formatValid, "phonemes.txt rows have well-formed timestamps");
    expect(labelsValid, "phonemes.txt uses only documented labels");
    expect(dataLines == result.segments.size(), "phonemes.txt row count matches segments");

    // The mapper covers the whole clip: leading silence, speech, trailing silence.
    expect(!result.segments.empty() && result.segments.front().startSec == 0.0,
           "segments start at zero");
    const double expectedEnd =
        static_cast<double>(result.features.rows) * config.mel.frameShiftMs / 1000.0;
    expect(!result.segments.empty() &&
               std::abs(result.segments.back().endSec - expectedEnd) < 1e-9,
           "segments cover the full feature timeline");
}

void testStageToggles(const std::filesystem::path& root, const std::filesystem::path& wavPath) {
    gl_asr::PipelineConfig config;
    config.outputDir = (root / "no_stages").string();
    config.enablePreprocess = false;
    config.enableFeatures = false;
    config.enablePhonemes = false;

    gl_asr::Pipeline pipeline(config);
    gl_asr::PipelineResult result;
    std::string error;
    const bool ok = pipeline.run(wavPath.string(), result, error);
    expect(ok, "pipeline with all stages disabled succeeds: " + error);
    expect(!result.preprocessed, "preprocessing reported as skipped");
    expect(!result.featuresExtracted, "features reported as skipped");
    expect(!result.phonemesMapped, "phonemes reported as skipped");
    expect(result.clip.frameCount() == 16000, "audio still loads with stages disabled");
    expect(!std::filesystem::exists(std::filesystem::path(config.outputDir) / "spectrogram.csv"),
           "no spectrogram when features are disabled");
    expect(!std::filesystem::exists(std::filesystem::path(config.outputDir) / "phonemes.txt"),
           "no phonemes when phonemes are disabled");

    gl_asr::PipelineConfig featuresOnly;
    featuresOnly.outputDir = (root / "features_only").string();
    featuresOnly.enablePhonemes = false;
    gl_asr::Pipeline featuresPipeline(featuresOnly);
    gl_asr::PipelineResult featuresResult;
    const bool featuresOk = featuresPipeline.run(wavPath.string(), featuresResult, error);
    expect(featuresOk, "feature-only pipeline succeeds: " + error);
    expect(featuresResult.featuresExtracted, "features ran in feature-only mode");
    expect(!featuresResult.phonemesMapped, "phonemes skipped in feature-only mode");
    expect(std::filesystem::is_regular_file(featuresResult.spectrogramPath),
           "spectrogram written in feature-only mode");
    expect(!std::filesystem::exists(std::filesystem::path(featuresOnly.outputDir) /
                                    "phonemes.txt"),
           "no phonemes in feature-only mode");
}

void testFailureModes(const std::filesystem::path& root, const std::filesystem::path& wavPath) {
    gl_asr::PipelineConfig config;
    config.outputDir = (root / "missing").string();
    gl_asr::Pipeline pipeline(config);
    gl_asr::PipelineResult result;
    std::string error;
    expect(!pipeline.run((root / "does_not_exist.wav").string(), result, error),
           "missing input fails");
    expect(error.find("cannot read input file") != std::string::npos,
           "missing input reports a useful error");
    expect(result.clip.empty(), "failed run leaves result empty");

    gl_asr::PipelineConfig invalid;
    invalid.outputDir = (root / "invalid").string();
    invalid.mel.nMels = 0;
    gl_asr::Pipeline invalidPipeline(invalid);
    gl_asr::PipelineResult invalidResult;
    error.clear();
    expect(!invalidPipeline.run(wavPath.string(), invalidResult, error),
           "invalid mel configuration fails");
    expect(error.find("feature extraction failed") != std::string::npos,
           "invalid configuration error is labeled by stage");
    expect(!invalidResult.featuresExtracted, "failed run does not report features");
    expect(!std::filesystem::exists(std::filesystem::path(invalid.outputDir) /
                                    "spectrogram.csv"),
           "failed feature extraction writes no spectrogram");
}

}  // namespace

int main() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "gl_asr_pipeline_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    const std::filesystem::path wavPath = root / "tone.wav";
    makeTestWav(wavPath);

    if (failures == 0) {
        testFullPipeline(root, wavPath);
        testStageToggles(root, wavPath);
        testFailureModes(root, wavPath);
    }

    std::filesystem::remove_all(root, ec);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "all pipeline tests passed\n";
    return EXIT_SUCCESS;
}
