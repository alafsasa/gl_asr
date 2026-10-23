#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "gl_asr/phoneme/phoneme_mapper.hpp"

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

constexpr int kCols = 80;
constexpr double kFloorLog = -23.025851;  // log(1e-10)
constexpr double kToneLog = 6.907755;     // log(1000)

gl_asr::FeatureMatrix silentMatrix(int rows) {
    gl_asr::FeatureMatrix features;
    features.rows = rows;
    features.cols = kCols;
    features.data.assign(static_cast<std::size_t>(rows) * kCols,
                         static_cast<float>(kFloorLog));
    return features;
}

// Fills [frameStart, frameEnd] x [binStart, binEnd] with tone-level energy.
void fillBand(gl_asr::FeatureMatrix& features, int frameStart, int frameEnd, int binStart,
              int binEnd) {
    for (int frame = frameStart; frame <= frameEnd; ++frame) {
        for (int bin = binStart; bin <= binEnd; ++bin) {
            features.data[static_cast<std::size_t>(frame) * kCols +
                          static_cast<std::size_t>(bin)] = static_cast<float>(kToneLog);
        }
    }
}

gl_asr::MelConfig melConfig() {
    gl_asr::MelConfig config;  // defaults: 80 mels, 20 Hz..Nyquist
    config.nMels = kCols;
    return config;
}

std::vector<gl_asr::PhonemeSegment> mapMatrix(const gl_asr::FeatureMatrix& features,
                                              std::string& error) {
    const gl_asr::PhonemeMapper mapper;
    std::vector<gl_asr::PhonemeSegment> segments;
    if (!mapper.map(features, melConfig(), 16000, 0.01, segments, error)) {
        return {};
    }
    return segments;
}

void testSilenceOnlyYieldsSingleSil() {
    std::string error;
    const std::vector<gl_asr::PhonemeSegment> segments = mapMatrix(silentMatrix(10), error);
    expect(error.empty(), "silence-only maps without error: " + error);
    expect(segments.size() == 1, "silence-only has exactly one segment");
    if (segments.size() == 1) {
        expect(segments[0].label == "sil", "silence-only is labeled sil");
        expect(std::abs(segments[0].endSec - 0.10) < 1e-6, "silence-only spans the clip");
    }
}

void testVowelRegionWithEdgeSilence() {
    gl_asr::FeatureMatrix features = silentMatrix(30);
    fillBand(features, 5, 24, 0, 9);  // energy below ~300 Hz -> vowel-like

    std::string error;
    const std::vector<gl_asr::PhonemeSegment> segments = mapMatrix(features, error);
    expect(error.empty(), "vowel matrix maps: " + error);
    expect(segments.size() == 4, "sil + two V slices + sil (got " +
                                      std::to_string(segments.size()) + ")");
    if (segments.size() == 4) {
        expect(segments[0].label == "sil", "leading silence");
        expect(segments[1].label == "V" && segments[2].label == "V",
               "speech region split into V slices");
        expect(segments[3].label == "sil", "trailing silence");
        expect(std::abs(segments[1].startSec - 0.05) < 1e-6, "region starts at 0.05 s");
        expect(std::abs(segments[2].endSec - 0.25) < 1e-6, "region ends at 0.25 s");
    }
}

void testFricativeRegion() {
    gl_asr::FeatureMatrix features = silentMatrix(30);
    fillBand(features, 5, 24, 62, 79);  // energy above ~4 kHz -> fricative-like

    std::string error;
    const std::vector<gl_asr::PhonemeSegment> segments = mapMatrix(features, error);
    expect(error.empty(), "fricative matrix maps: " + error);
    bool hasFricative = false;
    for (const gl_asr::PhonemeSegment& segment : segments) {
        if (segment.label == "F") {
            hasFricative = true;
        }
    }
    expect(hasFricative, "high-frequency region labeled F");
}

void testConsonantRegion() {
    gl_asr::FeatureMatrix features = silentMatrix(30);
    fillBand(features, 5, 24, 30, 45);  // mid-frequency band -> neither V nor F

    std::string error;
    const std::vector<gl_asr::PhonemeSegment> segments = mapMatrix(features, error);
    expect(error.empty(), "consonant matrix maps: " + error);
    bool hasConsonant = false;
    for (const gl_asr::PhonemeSegment& segment : segments) {
        if (segment.label == "C") {
            hasConsonant = true;
        }
    }
    expect(hasConsonant, "mid-frequency region labeled C");
}

void testSmallGapsMerged() {
    gl_asr::FeatureMatrix features = silentMatrix(30);
    fillBand(features, 0, 9, 0, 9);    // region A: 0..0.10 s
    fillBand(features, 13, 22, 0, 9);  // region B: 0.13..0.23 s, 30 ms gap

    std::string error;
    const std::vector<gl_asr::PhonemeSegment> segments = mapMatrix(features, error);
    expect(error.empty(), "merge matrix maps: " + error);

    bool silBetween = false;
    for (const gl_asr::PhonemeSegment& segment : segments) {
        if (segment.label == "sil" && segment.startSec > 0.005 &&
            segment.endSec < 0.225) {
            silBetween = true;
        }
    }
    expect(!silBetween, "30 ms gap is merged, no sil inserted between regions");
}

void testLargeGapsSplit() {
    gl_asr::FeatureMatrix features = silentMatrix(40);
    fillBand(features, 0, 9, 0, 9);    // region A
    fillBand(features, 20, 29, 0, 9);  // region B, 100 ms gap

    std::string error;
    const std::vector<gl_asr::PhonemeSegment> segments = mapMatrix(features, error);
    expect(error.empty(), "split matrix maps: " + error);

    bool silBetween = false;
    for (const gl_asr::PhonemeSegment& segment : segments) {
        if (segment.label == "sil" && segment.startSec >= 0.09 &&
            segment.endSec <= 0.21) {
            silBetween = true;
        }
    }
    expect(silBetween, "100 ms gap produces a sil segment between regions");
}

void testShortRegionDropped() {
    gl_asr::FeatureMatrix features = silentMatrix(30);
    fillBand(features, 10, 11, 0, 9);  // 20 ms blip < 30 ms minimum

    std::string error;
    const std::vector<gl_asr::PhonemeSegment> segments = mapMatrix(features, error);
    expect(error.empty(), "blip matrix maps: " + error);
    expect(segments.size() == 1 && segments[0].label == "sil",
           "sub-minimum region is dropped, leaving silence only");
}

void testExportPhonemes() {
    gl_asr::FeatureMatrix features = silentMatrix(30);
    fillBand(features, 5, 24, 0, 9);

    std::string error;
    const std::vector<gl_asr::PhonemeSegment> segments = mapMatrix(features, error);
    expect(!segments.empty(), "matrix maps for export");

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "gl_asr_test_phonemes.txt";
    expect(gl_asr::PhonemeMapper::exportPhonemes(segments, path.string(), error),
           "export succeeds: " + error);

    std::ifstream file(path);
    expect(file.good(), "phonemes.txt is readable");

    std::vector<std::string> dataLines;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line[0] != '#') {
            dataLines.push_back(line);
        }
    }
    expect(dataLines.size() == segments.size(), "one data row per segment");

    double start = 0.0;
    double end = 0.0;
    char label[16] = {0};
    expect(std::sscanf(dataLines[0].c_str(), "%lf %lf %15s", &start, &end, label) == 3,
           "rows parse as start end label");
    expect(std::abs(start - segments[0].startSec) < 1e-3, "first row start matches");
    expect(std::string(label) == segments[0].label, "first row label matches");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void testInvalidInputs() {
    const gl_asr::PhonemeMapper mapper;
    std::vector<gl_asr::PhonemeSegment> segments;
    std::string error;

    gl_asr::FeatureMatrix broken = silentMatrix(10);
    broken.data.pop_back();
    expect(!mapper.map(broken, melConfig(), 16000, 0.01, segments, error),
           "mismatched data size fails");

    expect(!mapper.map(silentMatrix(10), melConfig(), 16000, 0.0, segments, error),
           "zero frame shift fails");

    gl_asr::MelConfig wrongMels = melConfig();
    wrongMels.nMels = 40;
    expect(!mapper.map(silentMatrix(10), wrongMels, 16000, 0.01, segments, error),
           "nMels/columns mismatch fails");

    expect(!mapper.map(silentMatrix(0), melConfig(), 16000, 0.01, segments, error),
           "empty matrix fails");
}

}  // namespace

int main() {
    testSilenceOnlyYieldsSingleSil();
    testVowelRegionWithEdgeSilence();
    testFricativeRegion();
    testConsonantRegion();
    testSmallGapsMerged();
    testLargeGapsSplit();
    testShortRegionDropped();
    testExportPhonemes();
    testInvalidInputs();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "all phoneme mapper tests passed\n";
    return EXIT_SUCCESS;
}
