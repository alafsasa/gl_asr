#pragma once

#include <optional>
#include <string>
#include <vector>

namespace gl_asr {

struct Options {
    std::string inputPath;
    std::string outputDir = "output";
    bool verbose = false;
    bool showHelp = false;
    bool showVersion = false;

    // Preprocessing controls.
    bool disablePreprocess = false;
    std::optional<double> targetDbfs;  // RMS normalization target when provided

    // Feature extraction controls.
    bool disableFeatures = false;
    std::optional<int> nMels;  // number of mel filter bins when provided
};

// Parses command-line arguments. Returns true on success; on failure,
// `error` contains a human-readable message.
bool parseOptions(const std::vector<std::string>& args, Options& options, std::string& error);

std::string usageText();

}  // namespace gl_asr
