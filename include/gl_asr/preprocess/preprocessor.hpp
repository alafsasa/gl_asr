#pragma once

#include <string>
#include <vector>

namespace gl_asr {

enum class NormalizeMode {
    None,
    Peak,
    Rms,
};

struct PreprocessConfig {
    // DC offset removal: subtracts the mean of the whole clip before filtering.
    bool removeDcOffset = true;

    // Rumble removal: cascaded one-pole high-pass sections.
    // 0 disables the high-pass.
    double highpassCutoffHz = 80.0;
    int highpassStages = 2;  // 2 cascaded sections ~ 12 dB/oct

    // Hiss removal: cascaded one-pole low-pass sections.
    // 0 disables the low-pass.
    double lowpassCutoffHz = 7500.0;
    int lowpassStages = 1;

    NormalizeMode normalizeMode = NormalizeMode::Rms;
    double targetDbfs = -20.0;  // target RMS level for Rms mode
    double targetPeak = 0.9;    // target absolute peak for Peak mode
    double maxGainDb = 30.0;    // cap so near-silence is not amplified into noise
};

struct PreprocessStats {
    double inputRmsDbfs = 0.0;
    double outputRmsDbfs = 0.0;
    double inputPeak = 0.0;
    double outputPeak = 0.0;
    double gainDb = 0.0;
};

// Cleans a mono float signal in place: DC removal, band-limiting, and
// level normalization. Spectral denoising is deferred until the FFT
// stage of the pipeline exists.
class Preprocessor {
public:
    explicit Preprocessor(PreprocessConfig config = {});

    // Processes `samples` in place. Returns false and fills `error` when the
    // configuration is invalid (e.g. a cutoff at or above Nyquist).
    bool process(std::vector<float>& samples, int sampleRate, PreprocessStats* stats,
                 std::string& error);

    [[nodiscard]] const PreprocessConfig& config() const { return config_; }
    void setConfig(PreprocessConfig config) { config_ = config; }

private:
    PreprocessConfig config_;
};

}  // namespace gl_asr
