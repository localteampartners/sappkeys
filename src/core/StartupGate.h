#pragma once
// StartupGate — pre-state note-on gate (sapptune #21).
//
// A freshly constructed plugin instance holds the construction-default
// diagnostic instrument until the host's setStateInformation() restores the
// SFZ the user actually saved — and that restore finishes on a background
// thread. Stray MIDI arriving in that window (Live feeding "All Ins" during
// session load) would sound the diagnostic instrument: an altogether default
// sound, not the loaded SFZ. An instrument must not be playable before it
// knows what it is, so note-ons are suppressed until one of:
//
//   * the load initiated by a state restore completes (loadCompleted with
//     constructionDefault == false), or
//   * no state restore arrives within kGraceMs of construction (fresh insert:
//     the diagnostic instrument IS the intended sound, so it becomes playable
//     after the grace window).
//
// A state restore beginning DISARMS the gate again — even one arriving after
// the grace window (slow session load) — because the instrument is about to
// become something else. Note-offs, CCs, pitch bend and panic messages always
// pass; only note-ons are held.
//
// Threading: beginStateRestore / loadCompleted / tick run on the message
// thread; armed() is read from the audio thread. All state is atomic.
// Framework-independent (no JUCE) so the policy is unit-testable.

#include <atomic>

namespace sapp::keys {

class StartupGate {
public:
    // A fresh instance (no host state restore seen) becomes playable this many
    // milliseconds after construction. Hosts restore state within milliseconds
    // of instantiation; a human cannot insert a plugin and play it this fast.
    static constexpr double kGraceMs = 1500.0;

    // Message thread: a host state restore has begun. Disarms until the
    // restored instrument is installed, and permanently disables the grace
    // path — from here on only a completed post-construction load arms.
    void beginStateRestore() noexcept
    {
        armed_.store(false, std::memory_order_release);
        restoreSeen_.store(true, std::memory_order_release);
    }

    // Message thread: an instrument install completed. The construction-default
    // diagnostic load (the one the constructor kicks off) never arms; any other
    // successful install — restored SFZ, restore fallback, user or preset
    // load — means the instrument now knows what it is.
    void loadCompleted(bool constructionDefault) noexcept
    {
        if (!constructionDefault)
            armed_.store(true, std::memory_order_release);
    }

    // Message thread, periodic. Arms a fresh insert once the grace window has
    // passed with no state restore seen.
    void tick(double msSinceConstruction) noexcept
    {
        if (!restoreSeen_.load(std::memory_order_acquire)
            && msSinceConstruction >= kGraceMs)
            armed_.store(true, std::memory_order_release);
    }

    // Audio thread: note-ons may pass only when armed.
    bool armed() const noexcept { return armed_.load(std::memory_order_acquire); }

    bool restoreSeen() const noexcept
    {
        return restoreSeen_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> armed_{false};
    std::atomic<bool> restoreSeen_{false};
};

} // namespace sapp::keys
