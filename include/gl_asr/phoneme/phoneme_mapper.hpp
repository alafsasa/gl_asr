#pragma once

#include <string>
#include <vector>

#include "gl_asr/features/feature_extractor.hpp"

namespace gl_asr {

// Labels produced by the heuristic segmenter. A real acoustic model will
// replace these broad classes with proper phoneme symbols later:
//   sil - silence / background
//   V   - vowel-like      (energy concentrated below ~1 kHz)
//   F   - fricative-like  (strong energy above ~4 kHz or very high centroid)
//   C   - other consonant-like
struct PhonemeSegment {
    double startSec = 0.0;
    double endSec = 0.0;
    std::string label;
};

struct PhonemeConfig {
    double targetSegmentSec = 0.09;    // desired pseudo-phoneme length
    double mergeGapSec = 0.05;         // merge speech regions separated by less
    double minSegmentSec = 0.03;       // drop speech regions shorter than this
    double silenceRatio = 0.1;         // frame energy < ratio * max => silence
    double minSpeechEnergy = 1e-9;     // absolute floor so constant-level clips
                                       // (noise at the log floor) are not read
                                       // as speech by the relative threshold
    double vowelLowRatio = 0.55;       // sub-1kHz energy share for vowel-like
    double fricativeHighRatio = 0.35;  // above-4kHz energy share for fricative-like
    double fricativeCentroidHz = 2500.0;
};

// Segments a log-mel spectrogram into timed phoneme-like units using energy
// and spectral heuristics. This is the intended extension point: the future
// acoustic model replaces the heuristic frame labeling while the segmentation
// and export machinery stays.
class PhonemeMapper {
public:
    explicit PhonemeMapper(PhonemeConfig config = {});

    // `frameShiftSec` converts frame indices to seconds. Returns false and
    // fills `error` on invalid input or configuration.
    bool map(const FeatureMatrix& features, const MelConfig& melConfig, int sampleRate,
             double frameShiftSec, std::vector<PhonemeSegment>& out, std::string& error) const;

    // Writes segments as `start_sec end_sec label` rows with a commented header.
    static bool exportPhonemes(const std::vector<PhonemeSegment>& segments,
                               const std::string& path, std::string& error);

    [[nodiscard]] const PhonemeConfig& config() const { return config_; }
    void setConfig(PhonemeConfig config) { config_ = config; }

private:
    PhonemeConfig config_;
};

}  // namespace gl_asr
