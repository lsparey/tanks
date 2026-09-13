#include "AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

// Only the low-level device layer is used (one playback device + callback);
// decoding/encoding pull in file-format machinery this project never touches
// since every clip is synthesized in memory below.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr float kPi = 3.14159265358979f;

// One-pole lowpass smoothing coefficient for a given cutoff. Most of the
// "filtering" below is this single pole -- crude by DSP standards, but the
// clips only need to read as engine/gun/blast, not fool an audio engineer.
float lowpassAlpha(float cutoffHz) {
    return 1.0f - std::exp(-2.0f * kPi * cutoffHz / static_cast<float>(kSampleRate));
}

// Two-pole resonator: rings at centerHz when driven, decaying over roughly
// decaySeconds. Driven by noise it makes a narrowband hiss/whistle; driven
// by an impulse it makes a metallic ping -- both used below.
struct Resonator {
    float y1 = 0.0f, y2 = 0.0f, a1 = 0.0f, a2 = 0.0f;

    void tune(float centerHz, float decaySeconds) {
        const float r = std::exp(-1.0f / (decaySeconds * kSampleRate));
        a1 = 2.0f * r * std::cos(2.0f * kPi * centerHz / kSampleRate);
        a2 = -r * r;
    }
    float process(float input) {
        const float y = a1 * y1 + a2 * y2 + input;
        y2 = y1;
        y1 = y;
        return y;
    }
};

void normalize(std::vector<float>& samples, float peak) {
    float maxAbs = 0.0f;
    for (float s : samples) maxAbs = std::max(maxAbs, std::abs(s));
    if (maxAbs < 1e-6f) return;
    const float scale = peak / maxAbs;
    for (float& s : samples) s *= scale;
}

// Linear fade over the clip's final fadeSeconds so one-shots end at exactly
// zero instead of clicking.
void fadeOutTail(std::vector<float>& samples, float fadeSeconds) {
    const size_t fade = std::min(samples.size(),
                                 static_cast<size_t>(fadeSeconds * kSampleRate));
    for (size_t i = 0; i < fade; ++i) {
        samples[samples.size() - fade + i] *= 1.0f - static_cast<float>(i) / fade;
    }
}

// Loop-seam treatment for the drone layers: the synthesizers generate
// loopLength + fade samples, and this crossfades the aperiodic tail (noise,
// resonator ring-outs) into the head before truncating. Periodic components
// are chosen to complete integer cycles over loopLength, so they need no
// help.
void makeSeamless(std::vector<float>& samples, size_t loopLength) {
    const size_t fade = samples.size() - loopLength;
    for (size_t i = 0; i < fade; ++i) {
        const float w = static_cast<float>(i) / fade;
        samples[i] = samples[i] * w + samples[loopLength + i] * (1.0f - w);
    }
    samples.resize(loopLength);
}

// -------------------------------------------------------------------------
// Engine layers. Tuned to the Challenger 2's Perkins CV12-6A: a 26-litre
// four-stroke V12 firing six times per revolution, so idle around 650 rpm
// puts the firing fundamental near 65 Hz -- much smoother and higher than a
// truck diesel's lumpy chug. The loop is synthesized at idle and the
// playback-rate range in updateEngineSound revs it upward. (The real rev
// span to the 2300 rpm governor is ~3.5x, but resampling noise that far
// chipmunks it; ~1.5x reads better, the usual game compromise.)

std::vector<float> synthesizeCombustionLoop() {
    constexpr float kSeconds = 2.0f;  // periodic components below are multiples of 0.5 Hz
    constexpr float kFiringHz = 65.0f;
    const size_t count = static_cast<size_t>(kSampleRate * kSeconds);
    const size_t fade = kSampleRate / 8;
    std::vector<float> samples(count + fade);

    std::mt19937 rng(0x7A6E51u);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    float knockLp = 0.0f, brown = 0.0f;
    const float knockAlpha = lowpassAlpha(1400.0f);
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        // Firing-order stack plus the half-order (crankshaft) rumble every
        // big diesel carries under the firing tone.
        float tones = std::sin(2.0f * kPi * kFiringHz * 0.5f * t) * 0.22f +
                      std::sin(2.0f * kPi * kFiringHz * t) * 0.50f +
                      std::sin(2.0f * kPi * kFiringHz * 2.0f * t + 1.3f) * 0.30f +
                      std::sin(2.0f * kPi * kFiringHz * 3.0f * t + 0.7f) * 0.17f +
                      std::sin(2.0f * kPi * kFiringHz * 4.0f * t + 2.1f) * 0.10f +
                      std::sin(2.0f * kPi * kFiringHz * 5.0f * t + 0.4f) * 0.06f;
        // Slight slow unevenness so twelve cylinders don't sound like a
        // synthesizer holding a chord.
        tones *= 1.0f + 0.06f * std::sin(2.0f * kPi * 4.5f * t + 0.8f) +
                 0.05f * std::sin(2.0f * kPi * 11.0f * t);
        // Diesel knock: a midband noise tick gated at the firing rate. The
        // high power on the gate narrows each tick to a click rather than a
        // half-cycle whoosh.
        knockLp += knockAlpha * (white(rng) - knockLp);
        const float gate = std::pow(0.5f + 0.5f * std::sin(2.0f * kPi * kFiringHz * t - 0.6f), 6.0f);
        const float knock = knockLp * gate * 1.1f;
        // Broadband exhaust wash.
        brown = brown * 0.988f + white(rng) * 0.09f;
        samples[i] = std::tanh(tones * 1.15f + knock + brown * 0.9f);
    }
    makeSeamless(samples, count);
    normalize(samples, 0.75f);
    return samples;
}

// Turbocharger whine: a vibrato'd whistle around 1.1 kHz with a faint
// second harmonic, thickened by a resonator hissing at the same center so
// it isn't a bare test tone. The vibrato rate (6.5 Hz) and mean frequency
// both complete integer cycles over the loop, keeping the sine seamless.
std::vector<float> synthesizeTurboLoop() {
    constexpr float kSeconds = 2.0f;
    const size_t count = static_cast<size_t>(kSampleRate * kSeconds);
    const size_t fade = kSampleRate / 8;
    std::vector<float> samples(count + fade);

    std::mt19937 rng(0x7B0B01u);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    Resonator hiss;
    hiss.tune(1100.0f, 0.008f);
    double phase = 0.0;
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        const float whineHz = 1100.0f + 18.0f * std::sin(2.0f * kPi * 6.5f * t);
        phase += 2.0 * kPi * whineHz / kSampleRate;
        const float p = static_cast<float>(phase);
        const float whine = std::sin(p) * 0.55f + std::sin(2.0f * p) * 0.14f;
        samples[i] = whine + hiss.process(white(rng) * 0.02f);
    }
    makeSeamless(samples, count);
    normalize(samples, 0.5f);
    return samples;
}

// -------------------------------------------------------------------------
// One-shots.

// L30A1 120 mm report. The dominant features of a real tank-gun recording:
// a near-instantaneous overpressure spike (the muzzle blast's N-wave), a
// wideband crack collapsing into a deep boom within a couple hundred
// milliseconds, then a long low rumble with discrete ground/terrain echoes
// -- outdoors, the tail is what says "big gun" rather than "rifle".
std::vector<float> synthesizeShot() {
    constexpr float kSeconds = 2.3f;
    const size_t count = static_cast<size_t>(kSampleRate * kSeconds);
    std::vector<float> dry(count);

    std::mt19937 rng(0x5A07u);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    float crackLp = 0.0f, brown = 0.0f;
    double boomPhase = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        // N-wave: ~0.6 ms positive overpressure then a longer, shallower
        // negative phase. This asymmetric spike is what makes the attack
        // feel like pressure rather than a synth click.
        float nwave = 0.0f;
        if (t < 0.0006f) nwave = 1.6f * (1.0f - t / 0.0006f);
        else if (t < 0.0032f) nwave = -0.9f * (1.0f - (t - 0.0006f) / 0.0026f);
        // Crack brightness collapses fast: cutoff sweeps ~10 kHz -> 900 Hz
        // so within ~100 ms only the boom is left.
        crackLp += lowpassAlpha(900.0f + 9500.0f * std::exp(-t * 30.0f)) * (white(rng) - crackLp);
        const float crack = crackLp * std::exp(-t * 28.0f) * 2.6f;
        const float boomHz = 45.0f + 195.0f * std::exp(-t * 5.5f);
        boomPhase += 2.0 * kPi * boomHz / kSampleRate;
        const float boom = std::sin(static_cast<float>(boomPhase)) * std::exp(-t * 4.5f);
        // Slow rumble tail, faded in over the first ~40 ms so it sits under
        // the boom instead of thickening the attack.
        brown = brown * 0.992f + white(rng) * 0.06f;
        const float rumble = brown * (1.0f - std::exp(-t * 25.0f)) * std::exp(-t * 1.6f) * 2.2f;
        dry[i] = std::tanh(nwave * 1.4f + crack + boom + rumble);
    }

    // Discrete early reflections stand in for ground/treeline echo -- flat
    // delayed copies are enough at these gains, and they're most of the
    // difference between "gun in a booth" and "gun on a range".
    std::vector<float> samples = dry;
    constexpr struct { float delaySeconds, gain; } kReflections[] = {
        {0.09f, 0.30f}, {0.21f, 0.16f}, {0.38f, 0.09f}};
    for (const auto& reflection : kReflections) {
        const size_t delay = static_cast<size_t>(reflection.delaySeconds * kSampleRate);
        for (size_t i = delay; i < count; ++i) samples[i] += dry[i - delay] * reflection.gain;
    }
    fadeOutTail(samples, 0.3f);
    normalize(samples, 0.95f);
    return samples;
}

// Shell impact: a sub-bass thump sweeping down to ~30 Hz plus heavily
// lowpassed noise whose brightness and level both decay -- a fast initial
// blast envelope over a slower rumble tail.
std::vector<float> synthesizeExplosion() {
    constexpr float kSeconds = 2.4f;
    const size_t count = static_cast<size_t>(kSampleRate * kSeconds);
    std::vector<float> samples(count);

    std::mt19937 rng(0xB0031u);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    float noiseLp = 0.0f;
    double subPhase = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        const float subHz = 30.0f + 130.0f * std::exp(-t * 3.0f);
        subPhase += 2.0 * kPi * subHz / kSampleRate;
        const float sub = std::sin(static_cast<float>(subPhase)) * std::exp(-t * 2.2f);
        noiseLp += lowpassAlpha(120.0f + 2600.0f * std::exp(-t * 2.0f)) * (white(rng) - noiseLp);
        const float noiseEnv = std::exp(-t * 2.8f) * 1.3f + std::exp(-t * 1.1f) * 0.35f;
        samples[i] = std::tanh(sub * 1.1f + noiseLp * noiseEnv * 2.4f);
    }
    fadeOutTail(samples, 0.25f);
    normalize(samples, 0.95f);
    return samples;
}

}  // namespace

AudioEngine::AudioEngine()
    : shotClip_(synthesizeShot()), explosionClip_(synthesizeExplosion()) {
    loops_[kCombustion].clip = synthesizeCombustionLoop();
    loops_[kTurbo].clip = synthesizeTurboLoop();
}

AudioEngine::~AudioEngine() {
    if (device_) ma_device_uninit(device_.get());
}

void AudioEngine::dataCallback(ma_device* device, void* output, const void* /*input*/,
                               uint32_t frameCount) {
    auto* self = static_cast<AudioEngine*>(device->pUserData);
    self->mix(static_cast<float*>(output), frameCount);
}

void AudioEngine::mix(float* output, uint32_t frameCount) {
    float loopGains[kLoopLayerCount], loopPitches[kLoopLayerCount];
    for (size_t layer = 0; layer < kLoopLayerCount; ++layer) {
        loopGains[layer] = loops_[layer].gain.load(std::memory_order_relaxed);
        loopPitches[layer] = loops_[layer].pitch.load(std::memory_order_relaxed);
    }

    std::lock_guard<std::mutex> lock(voiceMutex_);
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        float sample = 0.0f;

        // Loop layers: pitch is a plain playback-rate change (linear
        // interpolation between loop samples), which conveniently deepens
        // the idle and raises full throttle just like a real rev range.
        for (size_t layer = 0; layer < kLoopLayerCount; ++layer) {
            LoopLayer& loop = loops_[layer];
            const size_t length = loop.clip.size();
            if (length == 0) continue;
            const size_t index = static_cast<size_t>(loop.cursor);
            const size_t next = index + 1 < length ? index + 1 : 0;
            const float frac = static_cast<float>(loop.cursor - static_cast<double>(index));
            sample += (loop.clip[index] * (1.0f - frac) + loop.clip[next] * frac) *
                      loopGains[layer];
            loop.cursor += loopPitches[layer];
            if (loop.cursor >= static_cast<double>(length))
                loop.cursor -= static_cast<double>(length);
        }

        for (Voice& voice : voices_) {
            if (!voice.active) continue;
            sample += (*voice.clip)[voice.cursor] * voice.gain;
            if (++voice.cursor >= voice.clip->size()) voice.active = false;
        }

        // Soft limiter: simultaneous explosions sum past [-1, 1]; tanh
        // rounds that off instead of hard-clipping.
        sample = std::tanh(sample * 0.9f);
        output[frame * 2] = sample;
        output[frame * 2 + 1] = sample;
    }
}

bool AudioEngine::ensureDevice() {
    if (device_) return true;
    if (deviceFailed_) return false;

    auto device = std::make_unique<ma_device>();
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = kSampleRate;
    config.dataCallback = &AudioEngine::dataCallback;
    config.pUserData = this;
    if (ma_device_init(nullptr, &config, device.get()) != MA_SUCCESS) {
        deviceFailed_ = true;
        std::cerr << "No usable audio playback device; sound stays off\n";
        return false;
    }
    device_ = std::move(device);
    return true;
}

void AudioEngine::setEnabled(bool enabled) {
    if (enabled == enabled_) return;
    if (enabled) {
        if (!ensureDevice()) return;
        if (!deviceStarted_) {
            if (ma_device_start(device_.get()) != MA_SUCCESS) {
                std::cerr << "Audio device failed to start; sound stays off\n";
                return;
            }
            deviceStarted_ = true;
        }
        enabled_ = true;
    } else {
        if (device_ && deviceStarted_) {
            ma_device_stop(device_.get());
            deviceStarted_ = false;
        }
        // Silence everything so re-enabling later doesn't resume a stale
        // explosion tail from whenever sound was switched off.
        std::lock_guard<std::mutex> lock(voiceMutex_);
        for (Voice& voice : voices_) voice.active = false;
        for (LoopLayer& loop : loops_) loop.gain.store(0.0f, std::memory_order_relaxed);
        smoothedIntensity_ = 0.0f;
        turboIntensity_ = 0.0f;
        enabled_ = false;
    }
}

void AudioEngine::updateEngineSound(float intensity, float deltaTime) {
    if (!enabled_) return;
    intensity = std::clamp(intensity, 0.0f, 1.0f);
    // ~quarter-second spool for the block itself: fast enough to track a
    // tapped W key, slow enough that the pitch glides between idle and speed
    // instead of stepping. The turbo chases the same target at less than
    // half that rate -- audible spool-up lag after the revs rise.
    smoothedIntensity_ +=
        (intensity - smoothedIntensity_) * std::min(1.0f, deltaTime * 4.0f);
    turboIntensity_ += (intensity - turboIntensity_) * std::min(1.0f, deltaTime * 1.7f);

    const float revs = smoothedIntensity_;
    loops_[kCombustion].gain.store(0.20f + 0.34f * revs, std::memory_order_relaxed);
    loops_[kCombustion].pitch.store(1.0f + 0.48f * revs, std::memory_order_relaxed);
    // Squared gain keeps the whine inaudible at idle and through gentle
    // manoeuvring; it only sings under real load.
    const float turbo = turboIntensity_;
    loops_[kTurbo].gain.store(0.30f * turbo * turbo, std::memory_order_relaxed);
    loops_[kTurbo].pitch.store(1.0f + 0.85f * turbo, std::memory_order_relaxed);
}

void AudioEngine::playShot() {
    if (!enabled_) return;
    startVoice(shotClip_, 0.9f);
}

void AudioEngine::playExplosion(float listenerDistance) {
    if (!enabled_) return;
    // Simple inverse-distance rolloff referenced to the chase camera's
    // typical ~10-unit standoff, so the player's own point-blank impacts
    // play near full volume and boundary-distance ones fade well back.
    const float gain = 0.95f * std::min(1.0f, 14.0f / (14.0f + listenerDistance));
    startVoice(explosionClip_, gain);
}

void AudioEngine::startVoice(const std::vector<float>& clip, float gain) {
    std::lock_guard<std::mutex> lock(voiceMutex_);
    Voice* slot = nullptr;
    size_t oldestCursor = 0;
    for (Voice& voice : voices_) {
        if (!voice.active) { slot = &voice; break; }
        // All slots busy: steal the voice furthest through its clip, whose
        // remaining tail is the quietest thing playing.
        if (voice.cursor >= oldestCursor) {
            oldestCursor = voice.cursor;
            slot = &voice;
        }
    }
    slot->clip = &clip;
    slot->cursor = 0;
    slot->gain = gain;
    slot->active = true;
}
