#pragma once

#include <string>
#include <vector>

#include "gl_asr/audio/audio_source.hpp"
#include "gl_asr/features/feature_extractor.hpp"
#include "gl_asr/phoneme/phoneme_mapper.hpp"
#include "gl_asr/preprocess/preprocessor.hpp"

namespace gl_asr {

// End-to-end configuration. Stages can be disabled independently; phoneme
// mapping requires feature extraction, so it is skipped when features are off.
struct PipelineConfig {
    std::string outputDir = "output";
    bool enablePreprocess = true;
    bool enableFeatures = true;
    bool enablePhonemes = true;

    PreprocessConfig preprocess;
    MelConfig mel;
    PhonemeConfig phoneme;
};

// Stage outputs and generated artifact paths from a pipeline run.
struct PipelineResult {
    AudioClip clip;

    bool preprocessed = false;
    PreprocessStats preprocessStats;

    bool featuresExtracted = false;
    FeatureMatrix features;
    DerivedParams featureParams;
    std::string spectrogramPath;

    bool phonemesMapped = false;
    std::vector<PhonemeSegment> segments;
    std::string phonemesPath;
};

// Runs the full pipeline (load -> preprocess -> log-mel features -> heuristic
// phonemes) and writes `spectrogram.csv` / `phonemes.txt` into
// `config.outputDir`. This is the single orchestration point used by both the
// CLI and integration tests.
class Pipeline {
public:
    explicit Pipeline(PipelineConfig config = {});

    // Returns false and fills `error` at the first failing stage. `out` is
    // reset at the start, so a failed run never exposes partial state.
    bool run(const std::string& inputPath, PipelineResult& out, std::string& error) const;

    [[nodiscard]] const PipelineConfig& config() const { return config_; }
    void setConfig(PipelineConfig config) { config_ = config; }

private:
    PipelineConfig config_;
};

}  // namespace gl_asr
