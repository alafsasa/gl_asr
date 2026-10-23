#include "gl_asr/features/feature_extractor.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <numbers>

#include <fftw3.h>

namespace gl_asr {

namespace {

constexpr double kEps = 1e-12;

int nextPowerOfTwo(int value) {
    int power = 1;
    while (power < value) {
        power <<= 1;
    }
    return power;
}

double hzToMel(double hz) {
    return 2595.0 * std::log10(1.0 + hz / 700.0);
}

double melToHz(double mel) {
    return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0);
}

// Builds the nMels+2 mel-scale triangle corner points in Hz used by the
// filter bank: points[i] is the i-th corner, equally spaced in mel.
std::vector<double> melCenterPointsHz(const MelConfig& config, int sampleRate) {
    const double fmaxHz =
        config.fmaxHz > 0.0 ? config.fmaxHz : static_cast<double>(sampleRate) / 2.0;
    const double melLow = hzToMel(config.fminHz);
    const double melHigh = hzToMel(fmaxHz);

    std::vector<double> centersHz(static_cast<std::size_t>(config.nMels) + 2, 0.0);
    for (int point = 0; point < config.nMels + 2; ++point) {
        const double mel =
            melLow + (melHigh - melLow) * static_cast<double>(point) /
                         static_cast<double>(config.nMels + 1);
        centersHz[static_cast<std::size_t>(point)] = melToHz(mel);
    }
    return centersHz;
}

// Builds a triangular mel filter bank of shape [nMels x spectrumBins].
// Filter centers are equally spaced on the mel scale between fmin and fmax.
std::vector<std::vector<float>> buildMelFilterBank(const MelConfig& config, int sampleRate,
                                                   int fftSize, int spectrumBins) {
    const std::vector<double> centersHz = melCenterPointsHz(config, sampleRate);

    std::vector<std::vector<float>> bank(static_cast<std::size_t>(config.nMels),
                                         std::vector<float>(static_cast<std::size_t>(spectrumBins),
                                                            0.0f));
    for (int filter = 0; filter < config.nMels; ++filter) {
        const double left = centersHz[static_cast<std::size_t>(filter)];
        const double center = centersHz[static_cast<std::size_t>(filter) + 1];
        const double right = centersHz[static_cast<std::size_t>(filter) + 2];
        std::vector<float>& weights = bank[static_cast<std::size_t>(filter)];

        for (int bin = 0; bin < spectrumBins; ++bin) {
            const double freqHz =
                static_cast<double>(bin) * static_cast<double>(sampleRate) /
                static_cast<double>(fftSize);
            if (freqHz <= left || freqHz >= right) {
                continue;
            }
            const double weight = freqHz <= center ? (freqHz - left) / std::max(center - left, kEps)
                                                   : (right - freqHz) / std::max(right - center, kEps);
            weights[static_cast<std::size_t>(bin)] = static_cast<float>(weight);
        }
    }
    return bank;
}

}  // namespace

FeatureExtractor::FeatureExtractor(MelConfig config) : config_(config) {}

std::vector<double> FeatureExtractor::melBinCentersHz(const MelConfig& config, int sampleRate) {
    const std::vector<double> points = melCenterPointsHz(config, sampleRate);
    // Bin m is covered by filter m, whose center is corner point m + 1.
    return std::vector<double>(points.begin() + 1, points.end() - 1);
}

bool FeatureExtractor::validate(int sampleRate, std::string& error) const {
    if (sampleRate <= 0) {
        error = "invalid sample rate: " + std::to_string(sampleRate);
        return false;
    }
    if (config_.frameLengthMs <= 0.0 || config_.frameShiftMs <= 0.0) {
        error = "frame length and shift must be positive";
        return false;
    }
    if (config_.nMels <= 0) {
        error = "nMels must be positive";
        return false;
    }
    if (config_.fminHz < 0.0 || config_.logFloor <= 0.0) {
        error = "fminHz must be non-negative and logFloor positive";
        return false;
    }

    const double nyquistHz = static_cast<double>(sampleRate) / 2.0;
    if (config_.fminHz >= nyquistHz) {
        error = "fminHz must be below Nyquist (" + std::to_string(sampleRate / 2) + " Hz)";
        return false;
    }
    if (config_.fmaxHz > 0.0 && (config_.fmaxHz > nyquistHz || config_.fmaxHz <= config_.fminHz)) {
        error = "fmaxHz must be in (" + std::to_string(config_.fminHz) + ", " +
                std::to_string(sampleRate / 2) + "] Hz";
        return false;
    }

    const int windowSamples =
        static_cast<int>(config_.frameLengthMs * static_cast<double>(sampleRate) / 1000.0);
    if (config_.fftSize > 0 && config_.fftSize < windowSamples) {
        error = "fftSize must be at least the window size (" + std::to_string(windowSamples) + ")";
        return false;
    }
    return true;
}

DerivedParams FeatureExtractor::derivedParams(int sampleRate) const {
    DerivedParams params;
    params.windowSamples =
        std::max(1, static_cast<int>(config_.frameLengthMs * static_cast<double>(sampleRate) /
                                     1000.0));
    params.hopSamples =
        std::max(1, static_cast<int>(config_.frameShiftMs * static_cast<double>(sampleRate) /
                                     1000.0));
    params.fftSize =
        config_.fftSize > 0 ? config_.fftSize : nextPowerOfTwo(params.windowSamples);
    params.spectrumBins = params.fftSize / 2 + 1;
    return params;
}

bool FeatureExtractor::extract(const std::vector<float>& samples, int sampleRate,
                               FeatureMatrix& out, std::string& error) const {
    out = FeatureMatrix{};
    error.clear();

    if (!validate(sampleRate, error)) {
        return false;
    }
    if (samples.empty()) {
        error = "no samples to extract features from";
        return false;
    }

    const DerivedParams params = derivedParams(sampleRate);
    const std::size_t windowSamples = static_cast<std::size_t>(params.windowSamples);
    const std::size_t hopSamples = static_cast<std::size_t>(params.hopSamples);
    const std::size_t totalFrames =
        samples.size() < windowSamples ? 1 : 1 + (samples.size() - windowSamples) / hopSamples;

    // Hann window.
    std::vector<float> window(windowSamples, 1.0f);
    if (windowSamples > 1) {
        for (std::size_t i = 0; i < windowSamples; ++i) {
            window[i] = static_cast<float>(
                0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * static_cast<double>(i) /
                                      static_cast<double>(windowSamples - 1))));
        }
    }

    const std::vector<std::vector<float>> filterBank =
        buildMelFilterBank(config_, sampleRate, params.fftSize, params.spectrumBins);

    float* fftInput = fftwf_alloc_real(static_cast<std::size_t>(params.fftSize));
    fftwf_complex* fftOutput =
        fftwf_alloc_complex(static_cast<std::size_t>(params.spectrumBins));
    fftwf_plan plan =
        fftwf_plan_dft_r2c_1d(params.fftSize, fftInput, fftOutput, FFTW_ESTIMATE);
    if (plan == nullptr) {
        fftwf_free(fftInput);
        fftwf_free(fftOutput);
        error = "failed to create FFT plan";
        return false;
    }

    // The FFT input is larger than the analysis window; keep the padding
    // region zeroed (fftwf_alloc_real returns uninitialized memory).
    for (std::size_t i = windowSamples; i < static_cast<std::size_t>(params.fftSize); ++i) {
        fftInput[i] = 0.0f;
    }

    std::vector<double> power(static_cast<std::size_t>(params.spectrumBins), 0.0);
    out.rows = static_cast<int>(totalFrames);
    out.cols = config_.nMels;
    out.data.assign(static_cast<std::size_t>(totalFrames) *
                        static_cast<std::size_t>(config_.nMels),
                    0.0f);

    for (std::size_t frame = 0; frame < totalFrames; ++frame) {
        const std::size_t offset = frame * hopSamples;
        for (std::size_t i = 0; i < windowSamples; ++i) {
            const float sample = offset + i < samples.size() ? samples[offset + i] : 0.0f;
            fftInput[i] = sample * window[i];
        }

        fftwf_execute(plan);

        for (int bin = 0; bin < params.spectrumBins; ++bin) {
            const float re = fftOutput[bin][0];
            const float im = fftOutput[bin][1];
            power[static_cast<std::size_t>(bin)] =
                static_cast<double>(re) * re + static_cast<double>(im) * im;
        }

        float* frameOut = out.data.data() + frame * static_cast<std::size_t>(config_.nMels);
        for (int filter = 0; filter < config_.nMels; ++filter) {
            const std::vector<float>& weights = filterBank[static_cast<std::size_t>(filter)];
            double energy = 0.0;
            for (int bin = 0; bin < params.spectrumBins; ++bin) {
                energy += static_cast<double>(weights[static_cast<std::size_t>(bin)]) *
                          power[static_cast<std::size_t>(bin)];
            }
            frameOut[filter] = static_cast<float>(std::log(std::max(energy, config_.logFloor)));
        }
    }

    fftwf_destroy_plan(plan);
    fftwf_free(fftInput);
    fftwf_free(fftOutput);
    return true;
}

bool FeatureExtractor::dumpCsv(const FeatureMatrix& features, const std::string& path,
                               std::string& error) {
    error.clear();
    if (features.rows <= 0 || features.cols <= 0 ||
        features.data.size() != static_cast<std::size_t>(features.rows) *
                                    static_cast<std::size_t>(features.cols)) {
        error = "cannot dump invalid feature matrix";
        return false;
    }

    std::ofstream file(path);
    if (!file) {
        error = "cannot open '" + path + "' for writing";
        return false;
    }

    for (int row = 0; row < features.rows; ++row) {
        for (int col = 0; col < features.cols; ++col) {
            if (col > 0) {
                file << ',';
            }
            file << features.at(row, col);
        }
        file << '\n';
    }
    if (!file) {
        error = "failed while writing '" + path + "'";
        return false;
    }
    return true;
}

}  // namespace gl_asr
