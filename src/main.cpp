#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "gl_asr/cli/options.hpp"
#include "gl_asr/pipeline/pipeline.hpp"
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

    gl_asr::PipelineConfig config;
    config.outputDir = options.outputDir;
    config.enablePreprocess = !options.disablePreprocess;
    config.enableFeatures = !options.disableFeatures;
    config.enablePhonemes = !options.disablePhonemes;
    if (options.targetDbfs.has_value()) {
        config.preprocess.targetDbfs = *options.targetDbfs;
    }
    if (options.nMels.has_value()) {
        config.mel.nMels = *options.nMels;
    }

    const gl_asr::Pipeline pipeline(config);
    gl_asr::PipelineResult result;
    if (!pipeline.run(options.inputPath, result, error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }

    const gl_asr::AudioClip& clip = result.clip;
    std::cout << "Loaded " << options.inputPath << ": " << clip.durationSeconds() << " s, "
              << clip.sampleRate << " Hz, " << clip.channels << " channel"
              << (clip.channels == 1 ? "" : "s") << " -> mono mix (" << clip.frameCount()
              << " samples)\n";

    if (options.verbose) {
        std::cout << "output: " << options.outputDir << "\n";
    }

    if (result.preprocessed) {
        const gl_asr::PreprocessStats& stats = result.preprocessStats;
        std::cout << std::fixed << std::setprecision(1) << "Preprocessed: RMS " << stats.inputRmsDbfs
                  << " -> " << stats.outputRmsDbfs << " dBFS (gain "
                  << (stats.gainDb >= 0 ? "+" : "") << stats.gainDb << " dB)"
                  << std::setprecision(2) << ", peak " << stats.outputPeak << "\n";
    } else {
        std::cout << "Preprocessing disabled.\n";
    }

    if (result.featuresExtracted) {
        std::cout << "Features: " << result.features.rows << " frames x " << result.features.cols
                  << " mel bins\n";
        std::cout << "Wrote " << result.spectrogramPath << "\n";

        if (options.verbose) {
            const gl_asr::DerivedParams& params = result.featureParams;
            std::cout << "window: " << params.windowSamples << " samples, hop: "
                      << params.hopSamples << " samples, fft: " << params.fftSize << "\n";
        }

        if (result.phonemesMapped) {
            std::cout << "Phonemes: " << result.segments.size() << " segments -> "
                      << result.phonemesPath << "\n";
        } else {
            std::cout << "Phoneme mapping disabled.\n";
        }
    } else {
        std::cout << "Feature extraction disabled.\n";
    }

    std::cout << "GL_ASR " << GL_ASR_VERSION << ": pipeline stops after phoneme mapping.\n";
    return 0;
}
