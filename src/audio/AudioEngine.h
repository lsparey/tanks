#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

struct ma_device;

// Sound effects for driving, firing and explosions. All three clips are
// synthesized procedurally at startup -- layered sine rumble and filtered
// noise, see the generators in AudioEngine.cpp -- rather than loaded from
// audio files, for the same reason the textures are generated: the repo
// ships no downloaded assets. Playback goes through a single miniaudio
// output device with a small hand-rolled voice mixer instead of miniaudio's
// high-level engine API, because the only nontrivial requirement here is a
// continuously looping engine drone whose pitch/volume follow the tank's
// speed, which is easiest to do sample-accurately in our own callback.
//
// Everything is off until setEnabled(true) -- the OS audio device is not
// even opened before then, so the default-off state (the terrain menu's
// SOUND toggle) leaves the audio stack completely untouched.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Starts or stops the output device. The device is opened lazily on the
    // first enable; if no usable audio device exists this quietly fails and
    // enabled() stays false, so a headless/CI run never crashes over sound.
    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }

    // Drives the looping engine sound. intensity is 0 (stationary idle) to
    // 1 (full speed or a hard pivot turn); it is smoothed internally against
    // deltaTime so the engine audibly spools up and down rather than
    // snapping between states. Call once per frame.
    void updateEngineSound(float intensity, float deltaTime);

    void playShot();
    // listenerDistance attenuates far-away impacts (distance from the
    // camera); pass 0 for full volume.
    void playExplosion(float listenerDistance);

private:
    struct Voice {
        const std::vector<float>* clip = nullptr;  // non-owning, points at a clip member below
        size_t cursor = 0;
        float gain = 1.0f;
        bool active = false;
    };

    static void dataCallback(ma_device* device, void* output, const void* input,
                             uint32_t frameCount);
    void mix(float* output, uint32_t frameCount);
    void startVoice(const std::vector<float>& clip, float gain);
    bool ensureDevice();

    // The engine sound is two independently gained/pitched loop layers
    // rather than one clip, because the CV12's audible signature is two
    // sources that scale differently with speed: the diesel's combustion
    // drone (present even at idle) and the turbocharger whine (silent at
    // idle, spooling up with a lag under load). See the matching
    // synthesizers and the gain/pitch curves in updateEngineSound.
    struct LoopLayer {
        std::vector<float> clip;  // mono PCM at kSampleRate, loop-seamless
        double cursor = 0.0;      // callback thread only
        std::atomic<float> gain{0.0f};
        std::atomic<float> pitch{1.0f};
    };
    enum LoopIndex { kCombustion = 0, kTurbo, kLoopLayerCount };
    std::array<LoopLayer, kLoopLayerCount> loops_;

    // Mono PCM at kSampleRate, generated once in the constructor.
    std::vector<float> shotClip_;
    std::vector<float> explosionClip_;

    // One-shot voice pool. Small and fixed-size so the mixer callback never
    // allocates; if every slot is busy the oldest voice is stolen, which for
    // overlapping explosion tails is inaudible.
    static constexpr size_t kMaxVoices = 12;
    std::array<Voice, kMaxVoices> voices_{};
    // Guards voices_ between the game thread (startVoice) and the device
    // callback. Both critical sections are a few microseconds, far below an
    // audio buffer period, so a plain mutex is fine here.
    std::mutex voiceMutex_;

    // Smoothed drive intensity, game thread only. The turbo tracks its own
    // slower value: real turbochargers spool well behind the throttle, and
    // that lag is a big part of why the whine reads as a turbo at all.
    float smoothedIntensity_ = 0.0f;
    float turboIntensity_ = 0.0f;

    std::unique_ptr<ma_device> device_;  // null until first enable
    bool deviceStarted_ = false;
    bool deviceFailed_ = false;  // remember a failed open; don't retry every toggle
    bool enabled_ = false;
};
