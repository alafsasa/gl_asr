#include "gl_asr/audio/audio_source.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include <sndfile.h>

namespace gl_asr {

namespace {

constexpr sf_count_t kChunkFrames = 4096;

}  // namespace

double AudioClip::durationSeconds() const {
    if (sampleRate <= 0) {
        return 0.0;
    }
    return static_cast<double>(samples.size()) / static_cast<double>(sampleRate);
}

bool AudioSource::load(const std::string& path, std::string& error) {
    clip_ = AudioClip{};
    error.clear();

    SF_INFO info{};
    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &info);
    if (file == nullptr) {
        error = "cannot open '" + path + "': " + sf_strerror(nullptr);
        return false;
    }

    if (info.channels <= 0 || info.samplerate <= 0) {
        error = "unsupported audio stream in '" + path + "'";
        sf_close(file);
        return false;
    }

    const auto channels = static_cast<std::size_t>(info.channels);
    std::vector<float> interleaved(static_cast<std::size_t>(kChunkFrames) * channels);
    std::vector<float> mono;
    if (info.frames > 0) {
        mono.reserve(static_cast<std::size_t>(info.frames));
    }

    float sampleScale = 1.0f / static_cast<float>(channels);
    sf_count_t read = 0;
    while ((read = sf_readf_float(file, interleaved.data(), kChunkFrames)) > 0) {
        const auto frames = static_cast<std::size_t>(read);
        mono.reserve(mono.size() + frames);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            float sum = 0.0f;
            for (std::size_t channel = 0; channel < channels; ++channel) {
                sum += interleaved[frame * channels + channel];
            }
            mono.push_back(sum * sampleScale);
        }
    }

    const int readError = sf_error(file);
    sf_close(file);

    if (readError != SF_ERR_NO_ERROR) {
        error = "failed to read '" + path + "': " + sf_error_number(readError);
        return false;
    }
    if (mono.empty()) {
        error = "audio file contains no samples: '" + path + "'";
        return false;
    }

    clip_.samples = std::move(mono);
    clip_.sampleRate = info.samplerate;
    clip_.channels = info.channels;
    return true;
}

std::vector<std::int16_t> AudioSource::toPcm16() const {
    std::vector<std::int16_t> pcm;
    pcm.reserve(clip_.samples.size());
    for (float sample : clip_.samples) {
        const float clamped = std::clamp(sample, -1.0f, 1.0f);
        const long quantized = std::lrintf(clamped * 32767.0f);
        pcm.push_back(static_cast<std::int16_t>(quantized));
    }
    return pcm;
}

}  // namespace gl_asr
