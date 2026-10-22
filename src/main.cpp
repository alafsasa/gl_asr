#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "gl_asr/audio/audio_source.hpp"
#include "gl_asr/cli/options.hpp"
#include "gl_asr/features/feature_extractor.hpp"
#include "gl_asr/preprocess/preprocessor.hpp"
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

    gl_asr::AudioSource source;
    if (!source.load(options.inputPath, error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }

    const gl_asr::AudioClip& clip = source.clip();
    const int channelCount = clip.channels;
    std::cout << "Loaded " << options.inputPath << ": " << clip.durationSeconds() << " s, "
              << clip.sampleRate << " Hz, " << channelCount << " channel"
              << (channelCount == 1 ? "" : "s") << " -> mono mix (" << clip.frameCount()
              << " samples)\n";

    if (options.verbose) {
        std::cout << "output: " << options.outputDir << "\n";
    }

    std::vector<float> samples = source.samples();

    if (!options.disablePreprocess) {
        gl_asr::PreprocessConfig config;
        if (options.targetDbfs.has_value()) {
            config.targetDbfs = *options.targetDbfs;
        }
        gl_asr::Preprocessor preprocessor(config);
        gl_asr::PreprocessStats stats;

        if (!preprocessor.process(samples, clip.sampleRate, &stats, error)) {
            std::cerr << "error: preprocessing failed: " << error << "\n";
            return 1;
        }

        std::cout << std::fixed << std::setprecision(1)
                  << "Preprocessed: RMS " << stats.inputRmsDbfs << " -> "
                  << stats.outputRmsDbfs << " dBFS (gain " << (stats.gainDb >= 0 ? "+" : "")
                  << stats.gainDb << " dB)" << std::setprecision(2)
                  << ", peak " << stats.outputPeak << "\n";
    } else {
        std::cout << "Preprocessing disabled.\n";
    }

    if (!options.disableFeatures) {
        gl_asr::MelConfig melConfig;
        if (options.nMels.has_value()) {
            melConfig.nMels = *options.nMels;
        }
        const gl_asr::FeatureExtractor extractor(melConfig);
        gl_asr::FeatureMatrix features;

        if (!extractor.extract(samples, clip.sampleRate, features, error)) {
            std::cerr << "error: feature extraction failed: " << error << "\n";
            return 1;
        }

        std::cout << "Features: " << features.rows << " frames x " << features.cols
                  << " mel bins\n";

        const std::filesystem::path spectrogramPath =
            std::filesystem::path(options.outputDir) / "spectrogram.csv";
        if (!gl_asr::FeatureExtractor::dumpCsv(features, spectrogramPath.string(), error)) {
            std::cerr << "error: " << error << "\n";
            return 1;
        }
        std::cout << "Wrote " << spectrogramPath.string() << "\n";

        if (options.verbose) {
            const gl_asr::DerivedParams params = extractor.derivedParams(clip.sampleRate);
            std::cout << "window: " << params.windowSamples << " samples, hop: "
                      << params.hopSamples << " samples, fft: " << params.fftSize << "\n";
        }
    } else {
        std::cout << "Feature extraction disabled.\n";
    }

    std::cout << "GL_ASR " << GL_ASR_VERSION << ": pipeline stops after feature extraction.\n";
    return 0;
}
