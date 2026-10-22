#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "gl_asr/cli/options.hpp"

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

void testBasicParse() {
    gl_asr::Options options;
    std::string error;
    const bool ok = gl_asr::parseOptions({"audio.wav"}, options, error);
    expect(ok, "basic parse succeeds");
    expect(options.inputPath == "audio.wav", "input path is captured");
    expect(options.outputDir == "output", "default output dir");
}

void testFlags() {
    gl_asr::Options options;
    std::string error;
    const bool ok = gl_asr::parseOptions({"audio.wav", "-v", "--out", "results"}, options, error);
    expect(ok, "flags parse succeeds");
    expect(options.verbose, "verbose is enabled");
    expect(options.outputDir == "results", "output dir override");
}

void testOutEqualsForm() {
    gl_asr::Options options;
    std::string error;
    const bool ok = gl_asr::parseOptions({"audio.wav", "--out=results"}, options, error);
    expect(ok, "equals form parses");
    expect(options.outputDir == "results", "equals form output dir");
}

void testHelpShortCircuits() {
    gl_asr::Options options;
    std::string error;
    const bool ok = gl_asr::parseOptions({"--help"}, options, error);
    expect(ok, "help parses without input");
    expect(options.showHelp, "help flag is set");
}

void testMissingInput() {
    gl_asr::Options options;
    std::string error;
    const bool ok = gl_asr::parseOptions({}, options, error);
    expect(!ok, "no input fails");
    expect(!error.empty(), "missing input reports an error");
}

void testUnknownOption() {
    gl_asr::Options options;
    std::string error;
    const bool ok = gl_asr::parseOptions({"audio.wav", "--bogus"}, options, error);
    expect(!ok, "unknown option fails");
    expect(error.find("unknown option") != std::string::npos, "unknown option error message");
}

void testMissingOutValue() {
    gl_asr::Options options;
    std::string error;
    const bool ok = gl_asr::parseOptions({"audio.wav", "--out"}, options, error);
    expect(!ok, "missing --out value fails");
}

void testUnexpectedPositional() {
    gl_asr::Options options;
    std::string error;
    const bool ok = gl_asr::parseOptions({"a.wav", "b.wav"}, options, error);
    expect(!ok, "second positional fails");
}

void testPreprocessFlags() {
    gl_asr::Options options;
    std::string error;
    const bool ok = gl_asr::parseOptions({"audio.wav", "--no-preprocess", "--target-dbfs=-16"},
                                         options, error);
    expect(ok, "preprocess flags parse: " + error);
    expect(options.disablePreprocess, "no-preprocess is set");
    expect(options.targetDbfs.has_value() && *options.targetDbfs == -16.0,
           "target-dbfs equals form parses");
}

void testTargetDbfsValue() {
    gl_asr::Options options;
    std::string error;
    const bool ok = gl_asr::parseOptions({"audio.wav", "--target-dbfs", "-23.5"}, options, error);
    expect(ok, "target-dbfs value parses: " + error);
    expect(options.targetDbfs.has_value() && *options.targetDbfs == -23.5,
           "target-dbfs value is captured");
}

void testTargetDbfsInvalid() {
    gl_asr::Options options;
    std::string error;
    const bool ok = gl_asr::parseOptions({"audio.wav", "--target-dbfs", "abc"}, options, error);
    expect(!ok, "invalid target-dbfs fails");
    expect(error.find("invalid value") != std::string::npos, "invalid target-dbfs message");
}

void testTargetDbfsMissingValue() {
    gl_asr::Options options;
    std::string error;
    const bool ok = gl_asr::parseOptions({"audio.wav", "--target-dbfs"}, options, error);
    expect(!ok, "missing target-dbfs value fails");
}

}  // namespace

int main() {
    testBasicParse();
    testFlags();
    testOutEqualsForm();
    testHelpShortCircuits();
    testMissingInput();
    testUnknownOption();
    testMissingOutValue();
    testUnexpectedPositional();
    testPreprocessFlags();
    testTargetDbfsValue();
    testTargetDbfsInvalid();
    testTargetDbfsMissingValue();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "all tests passed\n";
    return EXIT_SUCCESS;
}
