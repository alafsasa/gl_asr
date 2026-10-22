#pragma once

#include <string>
#include <vector>

namespace gl_asr {

struct Options {
    std::string inputPath;
    std::string outputDir = "output";
    bool verbose = false;
    bool showHelp = false;
    bool showVersion = false;
};

// Parses command-line arguments. Returns true on success; on failure,
// `error` contains a human-readable message.
bool parseOptions(const std::vector<std::string>& args, Options& options, std::string& error);

std::string usageText();

}  // namespace gl_asr
