#pragma once

#include <string>
#include <vector>

namespace gl_asr {

struct MelConfig {
    double frameLengthMs = 25.0;  // analysis window length
    double frameShiftMs = 10.0;   // hop between frames
    int nMels = 80;               // number of triangular mel filters
    double fminHz = 20.0;         // lowest filter edge
    double fmaxHz = 0.0;          // highest filter edge; 0 = use Nyquist
    int fftSize = 0;              // 0 = next power of two >= window size
    double logFloor = 1e-10;      // energy floor before taking the log
};

// Row-major log-mel spectrogram: rows = frames, cols = mel bins.
struct FeatureMatrix {
    std::vector<float> data;
    int rows = 0;
    int cols = 0;

    [[nodiscard]] float at(int row, int col) const {
        return data[static_cast<std::size_t>(row) * static_cast<std::size_t>(cols) +
                    static_cast<std::size_t>(col)];
    }
};

struct DerivedParams {
    int windowSamples = 0;
    int hopSamples = 0;
    int fftSize = 0;
    int spectrumBins = 0;  // fftSize / 2 + 1
};

// Converts a mono float signal into a log-mel spectrogram:
// framing -> Hann window -> FFT (fftw3f) -> power spectrum -> mel filter
// bank -> natural log with floor.
class FeatureExtractor {
public:
    explicit FeatureExtractor(MelConfig config = {});

    // Returns false and fills `error` on invalid configuration or input.
    bool extract(const std::vector<float>& samples, int sampleRate, FeatureMatrix& out,
                 std::string& error) const;

    [[nodiscard]] DerivedParams derivedParams(int sampleRate) const;

    // Writes the spectrogram as CSV: one row per frame, comma-separated mel values.
    static bool dumpCsv(const FeatureMatrix& features, const std::string& path,
                        std::string& error);

    [[nodiscard]] const MelConfig& config() const { return config_; }
    void setConfig(MelConfig config) { config_ = config; }

private:
    [[nodiscard]] bool validate(int sampleRate, std::string& error) const;

    MelConfig config_;
};

}  // namespace gl_asr
