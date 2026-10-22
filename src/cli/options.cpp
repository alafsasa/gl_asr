#include "gl_asr/cli/options.hpp"

#include <string>
#include <vector>

namespace gl_asr {

namespace {

constexpr const char* kProgramName = "gl_asr";

bool parseDouble(const std::string& text, double& value) {
    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(text, &consumed);
        if (consumed != text.size()) {
            return false;
        }
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

bool parseOptions(const std::vector<std::string>& args, Options& options, std::string& error) {
    options = Options{};

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];

        if (arg == "-h" || arg == "--help") {
            options.showHelp = true;
            return true;
        }
        if (arg == "-V" || arg == "--version") {
            options.showVersion = true;
            return true;
        }
        if (arg == "-v" || arg == "--verbose") {
            options.verbose = true;
            continue;
        }
        if (arg == "--no-preprocess") {
            options.disablePreprocess = true;
            continue;
        }
        if (arg == "--target-dbfs") {
            if (i + 1 >= args.size()) {
                error = "missing value for " + arg;
                return false;
            }
            double value = 0.0;
            if (!parseDouble(args[++i], value)) {
                error = "invalid value for --target-dbfs: " + args[i];
                return false;
            }
            options.targetDbfs = value;
            continue;
        }
        if (arg.rfind("--target-dbfs=", 0) == 0) {
            double value = 0.0;
            if (!parseDouble(arg.substr(14), value)) {
                error = "invalid value for --target-dbfs: " + arg.substr(14);
                return false;
            }
            options.targetDbfs = value;
            continue;
        }
        if (arg == "-o" || arg == "--out") {
            if (i + 1 >= args.size()) {
                error = "missing value for " + arg;
                return false;
            }
            options.outputDir = args[++i];
            continue;
        }
        if (arg.rfind("--out=", 0) == 0) {
            options.outputDir = arg.substr(6);
            if (options.outputDir.empty()) {
                error = "missing value for --out";
                return false;
            }
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            error = "unknown option: " + arg;
            return false;
        }
        if (!options.inputPath.empty()) {
            error = "unexpected positional argument: " + arg;
            return false;
        }
        options.inputPath = arg;
    }

    if (options.inputPath.empty()) {
        error = "no input audio file provided";
        return false;
    }

    return true;
}

std::string usageText() {
    return std::string("Usage: ") + kProgramName + " <input.wav> [options]\n" +
           "\n"
           "Options:\n"
           "  -o, --out <dir>        Output directory for generated artifacts (default: output)\n"
           "      --no-preprocess    Skip denoising, filtering, and normalization\n"
           "      --target-dbfs <db> RMS normalization target in dBFS (default: -20)\n"
           "  -v, --verbose          Enable verbose logging\n"
           "  -h, --help             Show this help message\n"
           "  -V, --version          Show version information\n";
}

}  // namespace gl_asr
