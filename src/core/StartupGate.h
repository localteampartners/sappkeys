#pragma once
// StartupGate — note-on gate around async instrument loads (sapptune #21,
// sappkeys #2).
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
// sappkeys #2 extends the same policy to EVERY async instrument load, not
// only the restore path: a mid-session program change, preset-parameter move,
// user-preset load or SFZ pick each begins its own load window (beginLoad),
// and until that load's instrument is installed the instance again does not
// know what it is.
//
// BEHAVIOR CHOICE (deliberate, was an accident of timing before): note-ons
// arriving inside any load window are SUPPRESSED — silence — until the new
// instrument is installed, exactly as the gate already behaved at startup.
// The alternative (keep the outgoing instrument sounding and swap at a note
// boundary) was rejected: the engine swaps snapshots at a block boundary and
// steal-fades old voices, so "old instrument keeps sounding" would require a
// second live snapshot path the engine doesn't have, and it re-creates the
// wrong-instrument fault this gate exists to close. Notes ALREADY sounding
// are not cut — the engine's adoption steal-fade retires them at install.
//
// Failure policy: if a load FAILS while a real (non-construction-default)
// instrument is still installed, the gate re-arms — a corrupt SFZ pick must
// not brick a playing session; the still-installed previous instrument is a
// sound the user chose, and the SappKeys-audio-source log names it. If no
// real instrument was ever installed (a failed state restore over the
// construction diagnostic), the gate stays closed: silence with the failure
// named in the status beats sounding the diagnostic (unchanged #21
// semantics). On a fresh insert (no restore seen) the grace path may still
// arm afterwards — there the diagnostic IS the intended sound.
//
// A state restore beginning DISARMS the gate again — even one arriving after
// the grace window (slow session load) — because the instrument is about to
// become something else. Note-offs, CCs, pitch bend and panic messages always
// pass; only note-ons are held.
//
// Threading: beginStateRestore / beginLoad / loadCompleted / loadFailed /
// tick run on the message thread; armed() is read from the audio thread. All
// state is atomic. Framework-independent (no JUCE) so the policy is
// unit-testable.

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

    // Message thread: an async instrument load has begun (restore's own load,
    // program change, preset parameter, user preset, SFZ pick, diagnostic
    // reload). Note-ons are suppressed until the load resolves — the
    // instrument is about to become something else. Loads may supersede each
    // other; only the newest one reports back (the processor's generation
    // guard), so the window stays closed across a churn of picks.
    void beginLoad() noexcept
    {
        armed_.store(false, std::memory_order_release);
        loadPending_.store(true, std::memory_order_release);
    }

    // Message thread: an instrument install completed. The construction-default
    // diagnostic load (the one the constructor kicks off) never arms; any other
    // successful install — restored SFZ, restore fallback, user or preset
    // load — means the instrument now knows what it is.
    void loadCompleted(bool constructionDefault) noexcept
    {
        loadPending_.store(false, std::memory_order_release);
        if (!constructionDefault) {
            realInstalled_.store(true, std::memory_order_release);
            armed_.store(true, std::memory_order_release);
        }
    }

    // Message thread: the newest load failed and installed nothing. Re-arm
    // only if a real instrument (from an earlier successful load) is still
    // installed — see the failure policy above.
    void loadFailed() noexcept
    {
        loadPending_.store(false, std::memory_order_release);
        if (realInstalled_.load(std::memory_order_acquire))
            armed_.store(true, std::memory_order_release);
    }

    // Message thread, periodic. Arms a fresh insert once the grace window has
    // passed with no state restore seen — but never while a load is in
    // flight: a user picking an SFZ inside the grace window must not have the
    // diagnostic armed under the pick's load (sappkeys #2).
    void tick(double msSinceConstruction) noexcept
    {
        if (!restoreSeen_.load(std::memory_order_acquire)
            && !loadPending_.load(std::memory_order_acquire)
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
    std::atomic<bool> loadPending_{false};
    std::atomic<bool> realInstalled_{false};
};

} // namespace sapp::keys
