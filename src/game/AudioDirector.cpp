#include "game/AudioDirector.h"

#include "game/Exam.h"

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>

namespace tiles::game {

namespace {

// The one authored background track. It is a seamless full-file loop, so the
// only thing playback ever does to it is start it.
const char *BACKGROUND_STREAM = "res://audio/bgm.mp3";

// One authored variant list per cue, in the order AudioCue declares them. The
// list of variants inside one of these is resource data: adding or removing a
// sound is an edit to the .tres, never an edit to this file. A cue whose
// resource is not there yet stays silent and says so once.
const char *CUE_RANDOMIZERS[] = {
    "res://audio/palette_select/palette_select.tres",
    "res://audio/rotate/rotate_cw.tres",
    "res://audio/rotate/rotate_ccw.tres",
    "res://audio/place/place.tres",
    "res://audio/undo/undo.tres",
    "res://audio/hover/hover.tres",
    "res://audio/victory/victory.tres",
    "res://audio/scroll_lock.tres",
};

// The two ends of the ambient envelope. The floor is inaudible rather than
// silent on purpose: the voice is never stopped, so the composition keeps
// advancing through every absent stretch.
//
// The audible plateau is half the track's authored amplitude. A halved linear
// gain is what -6 db means; -10 db would be half the *perceived* loudness, which
// is a much bigger cut than this.
constexpr double AUDIBLE_DB = -6.0;
constexpr double INAUDIBLE_DB = -70.0;

// Music is the ordinary state and silence is the brief gap in it, not the other
// way round: about four seconds of playing for every one of absence.
constexpr double FADE_IN_MIN = 6.0;
constexpr double FADE_IN_MAX = 12.0;
constexpr double AUDIBLE_HOLD_MIN = 60.0;
constexpr double AUDIBLE_HOLD_MAX = 180.0;
constexpr double FADE_OUT_MIN = 8.0;
constexpr double FADE_OUT_MAX = 16.0;
constexpr double ABSENT_HOLD_MIN = 15.0;
constexpr double ABSENT_HOLD_MAX = 45.0;

constexpr double DUCK_DB = -12.0;
constexpr double DUCK_ATTACK_SECONDS = 0.15;
constexpr double DUCK_HOLD_SECONDS = 2.0;
constexpr double DUCK_RELEASE_SECONDS = 3.0;

// Every effect voice is configured identically. Variation is the randomizer's
// job, and it is the same variation for every cue.
constexpr int EFFECT_POLYPHONY = 8;
constexpr float EFFECT_PITCH_SEMITONES = 2.0f;
constexpr float EFFECT_VOLUME_OFFSET_DB = 1.0f;

// Smooth and monotonic on [0, 1], exact at both ends, and it never overshoots
// either endpoint. That is the whole requirement on a fade shape.
double smooth(double p_t) {
    const double t = std::clamp(p_t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double interpolate_db(double p_from, double p_to, double p_t) {
    return p_from + (p_to - p_from) * smooth(p_t);
}

} // namespace

void AudioDirector::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_palette_selected", "entry"), &AudioDirector::on_palette_selected);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_orientation_changed", "entry", "orientation", "forward"),
        &AudioDirector::on_orientation_changed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_placement_succeeded", "entry"),
        &AudioDirector::on_placement_succeeded);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_removal_succeeded"), &AudioDirector::on_removal_succeeded);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_undo_succeeded"), &AudioDirector::on_undo_succeeded);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_active_proposal_changed", "present", "proposal"),
        &AudioDirector::on_active_proposal_changed);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_victory_reached"), &AudioDirector::on_victory_reached);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_problem_changed", "index", "number"),
        &AudioDirector::on_problem_changed);
}

// --- startup ---

void AudioDirector::_ready() {
    set_process(true);
    // Every resource and player first, then a valid envelope, then playback, and
    // only then the semantic connection. Nothing may reach a cue before the
    // thing that would play it exists.
    const bool loaded = load_background();
    load_effects();
    begin_ambient_phase(AmbientPhase::fade_in);
    apply_background_volume();
    if (loaded && background_ != nullptr) {
        // From the beginning of the file, at the inaudible floor, already fading
        // in. There is no initial absent hold and no start click.
        background_->play(0.0);
        background_started_ = true;
    }
    connect_semantics();
}

bool AudioDirector::load_background() {
    background_stream_ = godot::ResourceLoader::get_singleton()->load(BACKGROUND_STREAM);
    if (background_stream_.is_null()) {
        godot::UtilityFunctions::push_error(
            "[tiles] audio: the background track failed to load: ", BACKGROUND_STREAM);
        return false;
    }
    // The authored file is the loop. Looping it from its own beginning is what
    // makes one voice enough: no second copy, no crossfade, no seek.
    background_stream_->set_loop(true);
    background_stream_->set_loop_offset(0.0);
    background_ = memnew(godot::AudioStreamPlayer);
    background_->set_name("Background");
    background_->set_stream(background_stream_);
    add_child(background_);
    return true;
}

// Each bucket is one randomizer resource and one voice. The playback policy is
// applied here rather than trusted to the resource, so a hand-edited .tres can
// gain or lose a variant without ever being able to change how a cue is played.
void AudioDirector::load_effects() {
    for (std::size_t cue = 0; cue < CUE_COUNT; ++cue) {
        const char *path = CUE_RANDOMIZERS[cue];
        // Asked before loading, because a cue whose sounds have not been
        // authored yet is not a broken build and should not read like one. Said
        // once, at startup, and never retried.
        if (!godot::ResourceLoader::get_singleton()->exists(path)) {
            godot::UtilityFunctions::push_warning(
                "[tiles] audio: no variants supplied for ", path, "; that cue is silent");
            continue;
        }
        godot::Ref<godot::AudioStreamRandomizer> randomizer =
            godot::ResourceLoader::get_singleton()->load(path);
        if (randomizer.is_null()) {
            godot::UtilityFunctions::push_error(
                "[tiles] audio: ", path, " failed to load; that cue is silent");
            continue;
        }
        randomizer->set_playback_mode(godot::AudioStreamRandomizer::PLAYBACK_RANDOM);
        randomizer->set_random_pitch_semitones(EFFECT_PITCH_SEMITONES);
        randomizer->set_random_volume_offset_db(EFFECT_VOLUME_OFFSET_DB);
        // However many variants the resource carries, they are equally likely.
        for (std::int32_t i = 0; i < randomizer->get_streams_count(); ++i) {
            randomizer->set_stream_probability_weight(i, 1.0f);
        }
        auto *player = memnew(godot::AudioStreamPlayer);
        player->set_stream(randomizer);
        // Bounded polyphony, and nothing else: overlapping voices are the point,
        // and Godot already decides what happens past the eighth.
        player->set_max_polyphony(EFFECT_POLYPHONY);
        player->set_pitch_scale(1.0f);
        player->set_volume_db(0.0f);
        add_child(player);
        effects_[cue].randomizer = randomizer;
        effects_[cue].player = player;
    }
}

void AudioDirector::play_cue(AudioCue p_cue) {
    const Bucket &bucket = effects_[static_cast<std::size_t>(p_cue)];
    if (bucket.player == nullptr) {
        return;
    }
    bucket.player->play();
}

void AudioDirector::connect_semantics() {
    auto *exam = godot::Object::cast_to<Exam>(get_parent());
    if (exam == nullptr) {
        godot::UtilityFunctions::push_error(
            "[tiles] audio: the director has no exam to listen to; it will stay silent");
        return;
    }
    // Connected to the exam, which outlives every canvas, and connected exactly
    // once. Navigation never reconstructs this.
    const auto listen = [&](const char *p_signal, const char *p_sink) {
        exam->connect(
            godot::StringName(p_signal), godot::Callable(this, godot::StringName(p_sink)));
    };
    listen("palette_selected", "on_palette_selected");
    listen("orientation_changed", "on_orientation_changed");
    listen("placement_succeeded", "on_placement_succeeded");
    listen("removal_succeeded", "on_removal_succeeded");
    listen("undo_succeeded", "on_undo_succeeded");
    listen("active_proposal_changed", "on_active_proposal_changed");
    listen("victory_reached", "on_victory_reached");
    listen("problem_changed", "on_problem_changed");
}

// --- semantic results ---
//
// The payloads are deliberately unread. Which entry was selected, which
// orientation was reached, and which proposal appeared change nothing about
// what is played: the variant is the randomizer's choice, not the game's.

void AudioDirector::on_palette_selected(std::int64_t) {
    play_cue(AudioCue::palette_select);
}

// The only cue whose bucket a payload chooses. The turn the player asked for is
// the sound they hear; where the orientation index landed is still ignored.
void AudioDirector::on_orientation_changed(std::int64_t, std::int64_t, bool p_forward) {
    play_cue(p_forward ? AudioCue::rotate_cw : AudioCue::rotate_ccw);
}

void AudioDirector::on_placement_succeeded(std::int64_t) {
    play_cue(AudioCue::place);
}

// Taking a tile back is one sound whichever way the player asked for it.
void AudioDirector::on_removal_succeeded() {
    play_cue(AudioCue::remove_undo);
}

void AudioDirector::on_undo_succeeded() {
    play_cue(AudioCue::remove_undo);
}

// A ghost appearing is a cue; a ghost disappearing is not.
void AudioDirector::on_active_proposal_changed(bool p_present, std::int64_t) {
    if (!p_present) {
        return;
    }
    play_cue(AudioCue::hover);
}

// Emitted once per completed handoff, so this is the sound of arriving on a new
// problem rather than of asking for one. A refused navigation stays silent.
void AudioDirector::on_problem_changed(std::int64_t, std::int64_t) {
    play_cue(AudioCue::navigate);
}

// --- the ambient envelope ---

AudioDirector::AmbientPhase AudioDirector::next_ambient_phase(AmbientPhase p_phase) {
    switch (p_phase) {
        case AmbientPhase::fade_in:
            return AmbientPhase::audible_hold;
        case AmbientPhase::audible_hold:
            return AmbientPhase::fade_out;
        case AmbientPhase::fade_out:
            return AmbientPhase::absent_hold;
        case AmbientPhase::absent_hold:
            break;
    }
    return AmbientPhase::fade_in;
}

// One new duration is sampled when its phase begins, so no two cycles are the
// same length. The sequence is ordinary runtime randomness: it is never saved,
// never seeded, and never consulted by anything but this envelope.
void AudioDirector::begin_ambient_phase(AmbientPhase p_phase) {
    ambient_phase_ = p_phase;
    ambient_elapsed_ = 0.0;
    switch (p_phase) {
        case AmbientPhase::fade_in:
            ambient_duration_ = godot::UtilityFunctions::randf_range(FADE_IN_MIN, FADE_IN_MAX);
            return;
        case AmbientPhase::audible_hold:
            ambient_duration_ =
                godot::UtilityFunctions::randf_range(AUDIBLE_HOLD_MIN, AUDIBLE_HOLD_MAX);
            return;
        case AmbientPhase::fade_out:
            ambient_duration_ = godot::UtilityFunctions::randf_range(FADE_OUT_MIN, FADE_OUT_MAX);
            return;
        case AmbientPhase::absent_hold:
            ambient_duration_ =
                godot::UtilityFunctions::randf_range(ABSENT_HOLD_MIN, ABSENT_HOLD_MAX);
            return;
    }
}

void AudioDirector::advance_ambient(double p_delta) {
    double remaining = std::max(0.0, p_delta);
    while (remaining > 0.0) {
        if (ambient_duration_ <= 0.0) {
            return;
        }
        const double left = ambient_duration_ - ambient_elapsed_;
        if (remaining < left) {
            ambient_elapsed_ += remaining;
            return;
        }
        // The completed phase is consumed exactly, and the overshoot is carried
        // into the next one rather than discarded. A frame the machine slept
        // through therefore lands where it would have without the stall.
        remaining -= left;
        begin_ambient_phase(next_ambient_phase(ambient_phase_));
    }
}

double AudioDirector::ambient_progress() const {
    if (ambient_duration_ <= 0.0) {
        return 0.0;
    }
    return std::clamp(ambient_elapsed_ / ambient_duration_, 0.0, 1.0);
}

double AudioDirector::ambient_db() const {
    switch (ambient_phase_) {
        case AmbientPhase::fade_in:
            return interpolate_db(INAUDIBLE_DB, AUDIBLE_DB, ambient_progress());
        case AmbientPhase::audible_hold:
            return AUDIBLE_DB;
        case AmbientPhase::fade_out:
            return interpolate_db(AUDIBLE_DB, INAUDIBLE_DB, ambient_progress());
        case AmbientPhase::absent_hold:
            break;
    }
    return INAUDIBLE_DB;
}

// --- the victory duck ---

void AudioDirector::on_victory_reached() {
    play_cue(AudioCue::victory);
    // A second victory while a duck is running is not a second duck: the attack
    // simply restarts from wherever the gain currently is and heads for the same
    // floor, so nothing ever sums past it.
    duck_from_db_ = duck_db();
    duck_phase_ = DuckPhase::attack;
    duck_duration_ = DUCK_ATTACK_SECONDS;
    duck_elapsed_ = 0.0;
}

void AudioDirector::advance_duck(double p_delta) {
    double remaining = std::max(0.0, p_delta);
    while (duck_phase_ != DuckPhase::inactive && remaining > 0.0) {
        const double left = duck_duration_ - duck_elapsed_;
        if (remaining < left) {
            duck_elapsed_ += remaining;
            return;
        }
        remaining -= left;
        duck_elapsed_ = 0.0;
        switch (duck_phase_) {
            case DuckPhase::attack:
                duck_phase_ = DuckPhase::hold;
                duck_duration_ = DUCK_HOLD_SECONDS;
                break;
            case DuckPhase::hold:
                duck_phase_ = DuckPhase::release;
                duck_duration_ = DUCK_RELEASE_SECONDS;
                break;
            case DuckPhase::release:
            case DuckPhase::inactive:
                duck_phase_ = DuckPhase::inactive;
                duck_duration_ = 0.0;
                break;
        }
    }
}

double AudioDirector::duck_db() const {
    const double t =
        duck_duration_ > 0.0 ? std::clamp(duck_elapsed_ / duck_duration_, 0.0, 1.0) : 0.0;
    switch (duck_phase_) {
        case DuckPhase::attack:
            return interpolate_db(duck_from_db_, DUCK_DB, t);
        case DuckPhase::hold:
            return DUCK_DB;
        case DuckPhase::release:
            return interpolate_db(DUCK_DB, 0.0, t);
        case DuckPhase::inactive:
            break;
    }
    return 0.0;
}

// --- the step ---

void AudioDirector::_process(double p_delta) {
    advance(p_delta);
}

void AudioDirector::advance(double p_delta) {
    // Two independent components advanced by the same delta. Neither one may
    // cancel, delay, or reshape the other; only their sum reaches the voice.
    advance_ambient(p_delta);
    advance_duck(p_delta);
    apply_background_volume();
}

void AudioDirector::apply_background_volume() {
    if (background_ == nullptr) {
        return;
    }
    background_->set_volume_db(static_cast<float>(background_db()));
}

// --- read-only state ---

bool AudioDirector::background_started() const {
    return background_started_;
}

godot::AudioStreamPlayer *AudioDirector::background_player() const {
    return background_;
}

godot::Ref<godot::AudioStreamMP3> AudioDirector::background_stream() const {
    return background_stream_;
}

godot::AudioStreamPlayer *AudioDirector::effect_player(AudioCue p_cue) const {
    return effects_[static_cast<std::size_t>(p_cue)].player;
}

godot::Ref<godot::AudioStreamRandomizer> AudioDirector::effect_randomizer(AudioCue p_cue) const {
    return effects_[static_cast<std::size_t>(p_cue)].randomizer;
}

AudioDirector::AmbientPhase AudioDirector::ambient_phase() const {
    return ambient_phase_;
}

double AudioDirector::ambient_duration() const {
    return ambient_duration_;
}

double AudioDirector::ambient_elapsed() const {
    return ambient_elapsed_;
}

AudioDirector::DuckPhase AudioDirector::duck_phase() const {
    return duck_phase_;
}

double AudioDirector::background_db() const {
    return ambient_db() + duck_db();
}

} // namespace tiles::game
