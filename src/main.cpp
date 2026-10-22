#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "gl_asr/cli/options.hpp"
#include "gl_asr/version.hpp"

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);

    gl_asr::Options options;
    std::string error;
    if (!gl_asr::parseOptions(args, options, error)) {
        std::cerr << "error: " << error << "\n\n" << gl_asr::usageText();
        return 1;
    }

    if (options.showHelp) {
        std::cout << gl_asr::usageText();
        return 0;
    }

    if (options.showVersion) {
        std::cout << GL_ASR_PROJECT_NAME << " " << GL_ASR_VERSION << "\n";
        return 0;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(options.inputPath, ec)) {
        std::cerr << "error: cannot read input file: " << options.inputPath << "\n";
        return 1;
    }

    std::filesystem::create_directories(options.outputDir, ec);
    if (ec) {
        std::cerr << "error: cannot create output directory " << options.outputDir << ": "
                  << ec.message() << "\n";
        return 1;
    }

    if (options.verbose) {
        std::cout << "input:  " << options.inputPath << "\n";
        std::cout << "output: " << options.outputDir << "\n";
    }

    std::cout << "GL_ASR " << GL_ASR_VERSION << ": pipeline not implemented yet.\n";
    return 0;
}
