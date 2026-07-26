#pragma once

#include <godot_cpp/classes/audio_stream_mp3.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/audio_stream_randomizer.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace tiles::game {

// The one persistent presentation-only audio consumer.
//
// It connects once to the stable semantic results the exam publishes and turns
// them into sound. The dependency runs one way and only one way: the exam never
// learns that gain, playback, or a director exist, and no operation's legality,
// exact state, completion, navigation, or readiness may consult anything here.
// A failure to load or play is a presentation failure and nothing more.
//
// The background track is one voice which is started exactly once and never
// stopped, seeked, or restarted. Its audibility comes from two independent gain
// components summed in decibels:
//
//     background volume = ambient envelope + victory duck
//
// so the music drifts in and out on its own long cycle while a victory ducks
// whatever is currently audible, and neither reshapes the other. Because the
// voice keeps advancing beneath the inaudible floor, the musical position at the
// next fade-in is out of phase with both the envelope and the composition.
class AudioDirector : public godot::Node {
    GDCLASS(AudioDirector, godot::Node)

protected:
    static void _bind_methods();

public:
    // The ambient envelope's four phases, in cycle order.
    enum class AmbientPhase {
        fade_in,
        audible_hold,
        fade_out,
        absent_hold,
    };

    // The victory duck's three active phases, plus the resting state its gain
    // contribution is zero in.
    enum class DuckPhase {
        inactive,
        attack,
        hold,
        release,
    };

    // The logical sounds. This exists so the consumer is internally exhaustive
    // about what it can play; it is not an application event system, and it
    // never leaves this class. Nothing in the exam, the engine, a signal
    // payload, or a resource knows it exists.
    //
    // The two rotate cues are one player operation split by the direction it was
    // asked for, which is the one thing here a payload decides.
    enum class AudioCue {
        palette_select,
        rotate_cw,
        rotate_ccw,
        place,
        remove_undo,
        hover,
        victory,
        navigate,
    };

    static constexpr std::size_t CUE_COUNT = 8;

    void _ready() override;
    void _process(double p_delta) override;

    // The production step of every gain component. Ordinary play calls it once
    // per frame with the frame delta; a test calls the same function with
    // whatever interval it wants to observe. One unusually large delta consumes
    // the phases it completes and continues into the correct later phase instead
    // of losing the time it crossed.
    void advance(double p_delta);

    // The background track was loaded, given a full-file loop, and started once.
    bool background_started() const;
    godot::AudioStreamPlayer *background_player() const;
    godot::Ref<godot::AudioStreamMP3> background_stream() const;

    AmbientPhase ambient_phase() const;
    // The duration sampled when the current phase began.
    double ambient_duration() const;
    double ambient_elapsed() const;
    // Position within the current phase, in [0, 1].
    double ambient_progress() const;
    double ambient_db() const;

    DuckPhase duck_phase() const;
    double duck_db() const;

    // Exactly what the background player's volume is set from.
    double background_db() const;

    // Null until that cue's authored variants are supplied.
    godot::AudioStreamPlayer *effect_player(AudioCue p_cue) const;
    godot::Ref<godot::AudioStreamRandomizer> effect_randomizer(AudioCue p_cue) const;

    // The semantic results this director consumes, bound so ordinary Godot
    // connections can reach them. Each one is a result which already happened:
    // none of them may refuse, delay, or alter anything.
    void on_palette_selected(std::int64_t p_entry);
    void on_orientation_changed(std::int64_t p_entry, std::int64_t p_orientation, bool p_forward);
    void on_placement_succeeded(std::int64_t p_entry);
    void on_removal_succeeded();
    void on_undo_succeeded();
    void on_active_proposal_changed(bool p_present, std::int64_t p_proposal);
    void on_victory_reached();
    void on_problem_changed(std::int64_t p_index, std::int64_t p_number);

private:
    // One cue's voice and its authored variant list.
    struct Bucket final {
        godot::AudioStreamPlayer *player = nullptr;
        godot::Ref<godot::AudioStreamRandomizer> randomizer;
    };

    static AmbientPhase next_ambient_phase(AmbientPhase p_phase);

    bool load_background();
    void load_effects();
    // One play, every time, with no throttle, debounce, or coalescing. The
    // signals which get here are already the successful ones.
    void play_cue(AudioCue p_cue);
    void connect_semantics();
    void begin_ambient_phase(AmbientPhase p_phase);
    void advance_ambient(double p_delta);
    void advance_duck(double p_delta);
    void apply_background_volume();

    godot::AudioStreamPlayer *background_ = nullptr;
    godot::Ref<godot::AudioStreamMP3> background_stream_;
    bool background_started_ = false;

    std::array<Bucket, CUE_COUNT> effects_;

    AmbientPhase ambient_phase_ = AmbientPhase::fade_in;
    double ambient_duration_ = 0.0;
    double ambient_elapsed_ = 0.0;

    DuckPhase duck_phase_ = DuckPhase::inactive;
    double duck_duration_ = 0.0;
    double duck_elapsed_ = 0.0;
    // Where the running attack started. A victory during a duck restarts the
    // sequence from the gain it is already at rather than stacking a second one.
    double duck_from_db_ = 0.0;
};

} // namespace tiles::game
