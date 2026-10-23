#include "gl_asr/pipeline/pipeline.hpp"

#include <filesystem>
#include <system_error>
#include <utility>

namespace gl_asr {

namespace fs = std::filesystem;

Pipeline::Pipeline(PipelineConfig config) : config_(std::move(config)) {}

bool Pipeline::run(const std::string& inputPath, PipelineResult& out, std::string& error) const {
    out = PipelineResult{};
    error.clear();

    std::error_code ec;
    if (!fs::is_regular_file(inputPath, ec)) {
        error = "cannot read input file: " + inputPath;
        return false;
    }

    fs::create_directories(config_.outputDir, ec);
    if (ec) {
        error = "cannot create output directory " + config_.outputDir + ": " + ec.message();
        return false;
    }

    AudioSource source;
    if (!source.load(inputPath, error)) {
        return false;
    }
    out.clip = source.clip();

    if (config_.enablePreprocess) {
        Preprocessor preprocessor(config_.preprocess);
        if (!preprocessor.process(out.clip.samples, out.clip.sampleRate, &out.preprocessStats,
                                  error)) {
            error = "preprocessing failed: " + error;
            return false;
        }
        out.preprocessed = true;
    }

    if (!config_.enableFeatures) {
        return true;
    }

    const FeatureExtractor extractor(config_.mel);
    if (!extractor.extract(out.clip.samples, out.clip.sampleRate, out.features, error)) {
        error = "feature extraction failed: " + error;
        return false;
    }
    out.featuresExtracted = true;
    out.featureParams = extractor.derivedParams(out.clip.sampleRate);

    const fs::path spectrogramPath = fs::path(config_.outputDir) / "spectrogram.csv";
    if (!FeatureExtractor::dumpCsv(out.features, spectrogramPath.string(), error)) {
        return false;
    }
    out.spectrogramPath = spectrogramPath.string();

    if (!config_.enablePhonemes) {
        return true;
    }

    const PhonemeMapper mapper(config_.phoneme);
    const double frameShiftSec = config_.mel.frameShiftMs / 1000.0;
    if (!mapper.map(out.features, config_.mel, out.clip.sampleRate, frameShiftSec, out.segments,
                    error)) {
        error = "phoneme mapping failed: " + error;
        return false;
    }
    out.phonemesMapped = true;

    const fs::path phonemesPath = fs::path(config_.outputDir) / "phonemes.txt";
    if (!PhonemeMapper::exportPhonemes(out.segments, phonemesPath.string(), error)) {
        return false;
    }
    out.phonemesPath = phonemesPath.string();
    return true;
}

}  // namespace gl_asr
