#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gl_asr {

// Decoded audio, always stored as mono float samples in [-1, 1].
struct AudioClip {
    std::vector<float> samples;
    int sampleRate = 0;
    int channels = 0;  // channel count of the source file

    [[nodiscard]] std::int64_t frameCount() const {
        return static_cast<std::int64_t>(samples.size());
    }
    [[nodiscard]] double durationSeconds() const;
    [[nodiscard]] bool empty() const { return samples.empty(); }
};

// Loads an audio file through libsndfile and mixes it down to mono.
class AudioSource {
public:
    // Returns false and fills `error` on failure. Any previously loaded
    // clip is cleared first, so a failed load leaves the source empty.
    bool load(const std::string& path, std::string& error);

    [[nodiscard]] const AudioClip& clip() const { return clip_; }
    [[nodiscard]] const std::vector<float>& samples() const { return clip_.samples; }
    [[nodiscard]] int sampleRate() const { return clip_.sampleRate; }

    // Quantizes the loaded mono signal to 16-bit signed PCM.
    [[nodiscard]] std::vector<std::int16_t> toPcm16() const;

private:
    AudioClip clip_;
};

}  // namespace gl_asr
