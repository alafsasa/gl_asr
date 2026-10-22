#include "gl_asr/preprocess/preprocessor.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace gl_asr {

namespace {

constexpr double kEps = 1e-12;

double toDbfs(double linear) {
    return 20.0 * std::log10(std::max(linear, kEps));
}

// One-pole coefficient for the RBJ-style DC-blocking high-pass:
//   y[n] = a * (y[n-1] + x[n] - x[n-1])
double highpassAlpha(double cutoffHz, int sampleRate) {
    const double dt = 1.0 / static_cast<double>(sampleRate);
    const double rc = 1.0 / (2.0 * std::numbers::pi * cutoffHz);
    return rc / (rc + dt);
}

// One-pole smoothing coefficient for the exponential low-pass:
//   y[n] = y[n-1] + a * (x[n] - y[n-1])
double lowpassAlpha(double cutoffHz, int sampleRate) {
    const double dt = 1.0 / static_cast<double>(sampleRate);
    const double rc = 1.0 / (2.0 * std::numbers::pi * cutoffHz);
    return dt / (rc + dt);
}

void applyHighPass(std::vector<float>& samples, double cutoffHz, int stages, int sampleRate) {
    const double alpha = highpassAlpha(cutoffHz, sampleRate);
    for (int stage = 0; stage < stages; ++stage) {
        double previousInput = 0.0;
        double previousOutput = 0.0;
        for (float& sample : samples) {
            const double input = static_cast<double>(sample);
            const double output = alpha * (previousOutput + input - previousInput);
            previousInput = input;
            previousOutput = output;
            sample = static_cast<float>(output);
        }
    }
}

void applyLowPass(std::vector<float>& samples, double cutoffHz, int stages, int sampleRate) {
    const double alpha = lowpassAlpha(cutoffHz, sampleRate);
    for (int stage = 0; stage < stages; ++stage) {
        double state = 0.0;
        for (float& sample : samples) {
            state += alpha * (static_cast<double>(sample) - state);
            sample = static_cast<float>(state);
        }
    }
}

double meanOf(const std::vector<float>& samples) {
    double sum = 0.0;
    for (float sample : samples) {
        sum += static_cast<double>(sample);
    }
    return sum / static_cast<double>(samples.size());
}

double rmsOf(const std::vector<float>& samples) {
    double sum = 0.0;
    for (float sample : samples) {
        const double value = static_cast<double>(sample);
        sum += value * value;
    }
    return std::sqrt(sum / static_cast<double>(samples.size()));
}

double peakOf(const std::vector<float>& samples) {
    double peak = 0.0;
    for (float sample : samples) {
        peak = std::max(peak, std::abs(static_cast<double>(sample)));
    }
    return peak;
}

}  // namespace

Preprocessor::Preprocessor(PreprocessConfig config) : config_(config) {}

bool Preprocessor::process(std::vector<float>& samples, int sampleRate, PreprocessStats* stats,
                           std::string& error) {
    error.clear();

    if (samples.empty()) {
        error = "no samples to preprocess";
        return false;
    }
    if (sampleRate <= 0) {
        error = "invalid sample rate: " + std::to_string(sampleRate);
        return false;
    }
    if (config_.highpassStages < 0 || config_.lowpassStages < 0) {
        error = "filter stage counts must be non-negative";
        return false;
    }
    if (config_.highpassCutoffHz < 0.0 || config_.lowpassCutoffHz < 0.0) {
        error = "filter cutoffs must be non-negative";
        return false;
    }

    const double nyquistHz = static_cast<double>(sampleRate) / 2.0;
    if (config_.highpassCutoffHz > 0.0 && config_.highpassCutoffHz >= nyquistHz) {
        error = "high-pass cutoff must be below Nyquist (" + std::to_string(sampleRate / 2) + " Hz)";
        return false;
    }
    if (config_.lowpassCutoffHz > 0.0 && config_.lowpassCutoffHz >= nyquistHz) {
        error = "low-pass cutoff must be below Nyquist (" + std::to_string(sampleRate / 2) + " Hz)";
        return false;
    }

    const double inputRms = rmsOf(samples);
    const double inputPeak = peakOf(samples);

    if (config_.removeDcOffset) {
        const double mean = meanOf(samples);
        for (float& sample : samples) {
            sample = static_cast<float>(static_cast<double>(sample) - mean);
        }
    }

    if (config_.highpassCutoffHz > 0.0) {
        applyHighPass(samples, config_.highpassCutoffHz, config_.highpassStages, sampleRate);
    }
    if (config_.lowpassCutoffHz > 0.0) {
        applyLowPass(samples, config_.lowpassCutoffHz, config_.lowpassStages, sampleRate);
    }

    double gain = 1.0;
    if (config_.normalizeMode == NormalizeMode::Rms) {
        const double target = std::pow(10.0, config_.targetDbfs / 20.0);
        const double currentRms = rmsOf(samples);
        if (currentRms > kEps) {
            gain = target / currentRms;
        }
    } else if (config_.normalizeMode == NormalizeMode::Peak) {
        const double currentPeak = peakOf(samples);
        if (currentPeak > kEps) {
            gain = config_.targetPeak / currentPeak;
        }
    }

    if (gain != 1.0) {
        const double maxGain = std::pow(10.0, config_.maxGainDb / 20.0);
        gain = std::min(gain, maxGain);
        for (float& sample : samples) {
            const double scaled = static_cast<double>(sample) * gain;
            sample = static_cast<float>(std::clamp(scaled, -1.0, 1.0));
        }
    }

    if (stats != nullptr) {
        stats->inputRmsDbfs = toDbfs(inputRms);
        stats->outputRmsDbfs = toDbfs(rmsOf(samples));
        stats->inputPeak = inputPeak;
        stats->outputPeak = peakOf(samples);
        stats->gainDb = 20.0 * std::log10(std::max(gain, kEps));
    }
    return true;
}

}  // namespace gl_asr
