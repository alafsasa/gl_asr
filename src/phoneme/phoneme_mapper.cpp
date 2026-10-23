#include "gl_asr/phoneme/phoneme_mapper.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <utility>

namespace gl_asr {

namespace {

constexpr double kEps = 1e-12;
constexpr double kLowSplitHz = 1000.0;   // below: vowel-ish energy
constexpr double kHighSplitHz = 4000.0;  // above: fricative-ish energy

struct FrameStats {
    double energy = 0.0;      // mean linear mel energy
    double lowRatio = 0.0;    // share of energy below kLowSplitHz
    double highRatio = 0.0;   // share of energy above kHighSplitHz
    double centroidHz = 0.0;  // energy-weighted mean frequency
};

std::vector<FrameStats> computeFrameStats(const FeatureMatrix& features,
                                          const std::vector<double>& centersHz) {
    std::vector<FrameStats> stats(static_cast<std::size_t>(features.rows));
    for (int frame = 0; frame < features.rows; ++frame) {
        double totalEnergy = 0.0;
        double lowEnergy = 0.0;
        double highEnergy = 0.0;
        double centroidSum = 0.0;
        for (int bin = 0; bin < features.cols; ++bin) {
            const double energy = std::exp(static_cast<double>(features.at(frame, bin)));
            totalEnergy += energy;
            if (centersHz[static_cast<std::size_t>(bin)] < kLowSplitHz) {
                lowEnergy += energy;
            }
            if (centersHz[static_cast<std::size_t>(bin)] >= kHighSplitHz) {
                highEnergy += energy;
            }
            centroidSum += centersHz[static_cast<std::size_t>(bin)] * energy;
        }
        FrameStats& frameStats = stats[static_cast<std::size_t>(frame)];
        frameStats.energy = totalEnergy / static_cast<double>(features.cols);
        if (totalEnergy > kEps) {
            frameStats.lowRatio = lowEnergy / totalEnergy;
            frameStats.highRatio = highEnergy / totalEnergy;
            frameStats.centroidHz = centroidSum / totalEnergy;
        }
    }
    return stats;
}

// Inclusive frame ranges of consecutive above-threshold frames.
std::vector<std::pair<int, int>> speechRegions(const std::vector<FrameStats>& stats,
                                               double threshold) {
    std::vector<std::pair<int, int>> regions;
    for (std::size_t frame = 0; frame < stats.size(); ++frame) {
        if (stats[frame].energy >= threshold) {
            if (!regions.empty() && regions.back().second == static_cast<int>(frame) - 1) {
                regions.back().second = static_cast<int>(frame);
            } else {
                regions.emplace_back(static_cast<int>(frame), static_cast<int>(frame));
            }
        }
    }
    return regions;
}

}  // namespace

PhonemeMapper::PhonemeMapper(PhonemeConfig config) : config_(config) {}

bool PhonemeMapper::map(const FeatureMatrix& features, const MelConfig& melConfig, int sampleRate,
                        double frameShiftSec, std::vector<PhonemeSegment>& out,
                        std::string& error) const {
    out.clear();
    error.clear();

    if (frameShiftSec <= 0.0) {
        error = "frame shift must be positive";
        return false;
    }
    if (sampleRate <= 0) {
        error = "invalid sample rate: " + std::to_string(sampleRate);
        return false;
    }
    if (features.rows <= 0 || features.cols <= 0 ||
        features.data.size() != static_cast<std::size_t>(features.rows) *
                                    static_cast<std::size_t>(features.cols)) {
        error = "invalid or empty feature matrix";
        return false;
    }
    if (melConfig.nMels != features.cols) {
        error = "mel config nMels (" + std::to_string(melConfig.nMels) +
                ") does not match feature columns (" + std::to_string(features.cols) + ")";
        return false;
    }
    if (config_.targetSegmentSec <= 0.0 || config_.minSegmentSec < 0.0 ||
        config_.mergeGapSec < 0.0) {
        error = "segment durations must be positive (min/merge may be zero)";
        return false;
    }
    if (config_.minSpeechEnergy < 0.0) {
        error = "minSpeechEnergy must be non-negative";
        return false;
    }
    if (config_.silenceRatio <= 0.0 || config_.silenceRatio >= 1.0) {
        error = "silenceRatio must be in (0, 1)";
        return false;
    }

    const std::vector<double> centersHz =
        FeatureExtractor::melBinCentersHz(melConfig, sampleRate);
    const std::vector<FrameStats> stats = computeFrameStats(features, centersHz);

    const double maxEnergy =
        std::max_element(stats.begin(), stats.end(),
                         [](const FrameStats& a, const FrameStats& b) {
                             return a.energy < b.energy;
                         })
            ->energy;
    // A frame counts as speech only above both the in-clip relative threshold
    // and an absolute floor; otherwise constant-level clips mark themselves
    // as speech (the max is the noise itself).
    const double speechThreshold =
        std::max(config_.silenceRatio * maxEnergy, config_.minSpeechEnergy);

    std::vector<std::pair<int, int>> regions = speechRegions(stats, speechThreshold);

    // Merge regions separated by less than mergeGapSec.
    const int mergeGapFrames =
        static_cast<int>(std::lround(config_.mergeGapSec / frameShiftSec));
    std::vector<std::pair<int, int>> merged;
    for (const std::pair<int, int>& region : regions) {
        if (!merged.empty() && region.first - merged.back().second <= mergeGapFrames) {
            merged.back().second = region.second;
        } else {
            merged.push_back(region);
        }
    }

    // Drop regions shorter than minSegmentSec.
    const int minSegmentFrames =
        static_cast<int>(std::lround(config_.minSegmentSec / frameShiftSec));
    std::vector<std::pair<int, int>> kept;
    for (const std::pair<int, int>& region : merged) {
        if (region.second - region.first + 1 >= minSegmentFrames) {
            kept.push_back(region);
        }
    }

    // No speech at all: report a single silence segment covering the clip.
    if (kept.empty()) {
        out.push_back({0.0, static_cast<double>(features.rows) * frameShiftSec, "sil"});
        return true;
    }

    const auto classify = [this, speechThreshold](double meanEnergy, double lowRatio,
                                                  double highRatio, double centroidHz) {
        if (meanEnergy < speechThreshold) {
            return "sil";
        }
        if (lowRatio >= config_.vowelLowRatio) {
            return "V";
        }
        if (highRatio >= config_.fricativeHighRatio ||
            centroidHz >= config_.fricativeCentroidHz) {
            return "F";
        }
        return "C";
    };

    int cursor = 0;
    for (const std::pair<int, int>& region : kept) {
        // Leading / inter-region silence.
        if (region.first > cursor) {
            out.push_back({static_cast<double>(cursor) * frameShiftSec,
                           static_cast<double>(region.first) * frameShiftSec, "sil"});
        }

        // Split the region into roughly targetSegmentSec slices.
        const int frames = region.second - region.first + 1;
        const int sliceCount = std::max(
            1, static_cast<int>(std::lround(static_cast<double>(frames) * frameShiftSec /
                                            config_.targetSegmentSec)));
        for (int slice = 0; slice < sliceCount; ++slice) {
            const int sliceStart =
                region.first + frames * slice / sliceCount;
            const int sliceEnd =
                region.first + frames * (slice + 1) / sliceCount - 1;

            double meanEnergy = 0.0;
            double meanLow = 0.0;
            double meanHigh = 0.0;
            double meanCentroid = 0.0;
            for (int frame = sliceStart; frame <= sliceEnd; ++frame) {
                const FrameStats& frameStats = stats[static_cast<std::size_t>(frame)];
                meanEnergy += frameStats.energy;
                meanLow += frameStats.lowRatio;
                meanHigh += frameStats.highRatio;
                meanCentroid += frameStats.centroidHz;
            }
            const double sliceFrames = static_cast<double>(sliceEnd - sliceStart + 1);
            meanEnergy /= sliceFrames;
            meanLow /= sliceFrames;
            meanHigh /= sliceFrames;
            meanCentroid /= sliceFrames;

            out.push_back({static_cast<double>(sliceStart) * frameShiftSec,
                           static_cast<double>(sliceEnd + 1) * frameShiftSec,
                           classify(meanEnergy, meanLow, meanHigh, meanCentroid)});
        }
        cursor = region.second + 1;
    }

    // Trailing silence.
    if (cursor < features.rows) {
        out.push_back({static_cast<double>(cursor) * frameShiftSec,
                       static_cast<double>(features.rows) * frameShiftSec, "sil"});
    }
    return true;
}

bool PhonemeMapper::exportPhonemes(const std::vector<PhonemeSegment>& segments,
                                   const std::string& path, std::string& error) {
    error.clear();

    std::ofstream file(path);
    if (!file) {
        error = "cannot open '" + path + "' for writing";
        return false;
    }

    file << "# GL_ASR heuristic phoneme segmentation\n"
         << "# Labels are broad phonetic classes (sil/V/C/F), not recognized\n"
         << "# phonemes; an acoustic model is required for true phoneme labels.\n"
         << "# start_sec end_sec label\n"
         << std::fixed << std::setprecision(3);

    for (const PhonemeSegment& segment : segments) {
        file << segment.startSec << ' ' << segment.endSec << ' ' << segment.label << '\n';
    }
    if (!file) {
        error = "failed while writing '" + path + "'";
        return false;
    }
    return true;
}

}  // namespace gl_asr
