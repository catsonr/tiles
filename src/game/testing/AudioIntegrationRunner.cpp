#include "game/testing/AudioIntegrationRunner.h"

#include "core/geometry/Coordinate.h"
#include "game/AudioDirector.h"
#include "game/Exam.h"
#include "game/LevelPlayer.h"
#include "game/ProblemState.h"

#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace tiles::game {

namespace {

const char *EXAM_SCENE = "res://exam.tscn";
const char *BACKGROUND_STREAM = "res://audio/bgm.mp3";
const char *FIXTURE_LEVEL = "res://tests/fixtures/canonical_level.tres";
const char *ACT_LEVEL = "res://levels/act0-level.tres";

constexpr float EXAM_WIDTH = 1280.0f;
constexpr float EXAM_HEIGHT = 800.0f;

// The envelope bounds this runner holds the director to. They are the contracted
// numbers restated here on purpose: the proof is that the implementation agrees
// with them, so it may not read them out of the implementation.
constexpr double AUDIBLE_DB = -6.0;
constexpr double INAUDIBLE_DB = -70.0;
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

// Comfortably tighter than any quantity under test and comfortably looser than
// double rounding over a few hundred additions.
constexpr double EPSILON = 1e-9;

bool near_db(double p_value, double p_expected) {
    return std::abs(p_value - p_expected) <= 1e-6;
}

godot::PackedStringArray manifest_of(std::initializer_list<const char *> p_paths) {
    godot::PackedStringArray manifest;
    for (const char *path : p_paths) {
        manifest.push_back(path);
    }
    return manifest;
}

godot::Vector2 centroid(const LevelPlayer &p_canvas, const Placement &p_placement) {
    double x = 0.0;
    double y = 0.0;
    const auto &vertices = p_placement.footprint().vertices();
    for (const Point &vertex : vertices) {
        const godot::Vector2 projected = p_canvas.project(vertex);
        x += projected.x;
        y += projected.y;
    }
    return godot::Vector2(
        static_cast<real_t>(x / vertices.size()), static_cast<real_t>(y / vertices.size()));
}

// Accept the exact proposal at one translation, reached the ordinary way: point
// at it, then accept whatever the ghost became.
bool accept_at(LevelPlayer &p_canvas, Point p_translation) {
    for (const LevelPlayer::Proposal &proposal : p_canvas.proposals()) {
        if (proposal.placement.translation() != p_translation) {
            continue;
        }
        p_canvas.set_pointer(centroid(p_canvas, proposal.placement));
        return p_canvas.accept_active_proposal();
    }
    return false;
}

const char *phase_name(AudioDirector::AmbientPhase p_phase) {
    switch (p_phase) {
        case AudioDirector::AmbientPhase::fade_in:
            return "fade in";
        case AudioDirector::AmbientPhase::audible_hold:
            return "audible hold";
        case AudioDirector::AmbientPhase::fade_out:
            return "fade out";
        case AudioDirector::AmbientPhase::absent_hold:
            break;
    }
    return "absent hold";
}

// The four contracted duration windows, indexed by the phase they belong to.
void phase_bounds(AudioDirector::AmbientPhase p_phase, double *r_min, double *r_max) {
    switch (p_phase) {
        case AudioDirector::AmbientPhase::fade_in:
            *r_min = FADE_IN_MIN;
            *r_max = FADE_IN_MAX;
            return;
        case AudioDirector::AmbientPhase::audible_hold:
            *r_min = AUDIBLE_HOLD_MIN;
            *r_max = AUDIBLE_HOLD_MAX;
            return;
        case AudioDirector::AmbientPhase::fade_out:
            *r_min = FADE_OUT_MIN;
            *r_max = FADE_OUT_MAX;
            return;
        case AudioDirector::AmbientPhase::absent_hold:
            break;
    }
    *r_min = ABSENT_HOLD_MIN;
    *r_max = ABSENT_HOLD_MAX;
}

AudioDirector::AmbientPhase next_phase(AudioDirector::AmbientPhase p_phase) {
    switch (p_phase) {
        case AudioDirector::AmbientPhase::fade_in:
            return AudioDirector::AmbientPhase::audible_hold;
        case AudioDirector::AmbientPhase::audible_hold:
            return AudioDirector::AmbientPhase::fade_out;
        case AudioDirector::AmbientPhase::fade_out:
            return AudioDirector::AmbientPhase::absent_hold;
        case AudioDirector::AmbientPhase::absent_hold:
            break;
    }
    return AudioDirector::AmbientPhase::fade_in;
}

// The gain a phase must be sitting at exactly when it begins.
double phase_start_db(AudioDirector::AmbientPhase p_phase) {
    switch (p_phase) {
        case AudioDirector::AmbientPhase::fade_in:
        case AudioDirector::AmbientPhase::absent_hold:
            return INAUDIBLE_DB;
        case AudioDirector::AmbientPhase::audible_hold:
        case AudioDirector::AmbientPhase::fade_out:
            break;
    }
    return AUDIBLE_DB;
}

// Advance until the given phase begins, or give up. Only the production step is
// ever used to get there.
bool run_to_phase(AudioDirector &p_director, AudioDirector::AmbientPhase p_phase) {
    for (int guard = 0; guard < 16; ++guard) {
        if (p_director.ambient_phase() == p_phase && p_director.ambient_elapsed() == 0.0) {
            return true;
        }
        p_director.advance(p_director.ambient_duration() - p_director.ambient_elapsed());
    }
    return p_director.ambient_phase() == p_phase;
}

} // namespace

void AudioIntegrationRunner::_bind_methods() {}

bool AudioIntegrationRunner::expect(bool p_condition, const char *p_description) {
    ++checks_;
    if (!p_condition) {
        ++failures_;
        godot::UtilityFunctions::push_error("[tiles] audio integration: ", p_description);
    }
    return p_condition;
}

Exam *AudioIntegrationRunner::make_exam(const godot::PackedStringArray &p_manifest) {
    const godot::Ref<godot::PackedScene> scene =
        godot::ResourceLoader::get_singleton()->load(EXAM_SCENE);
    if (!expect(scene.is_valid(), "the real exam scene loads")) {
        return nullptr;
    }
    godot::Node *node = scene->instantiate();
    auto *exam = godot::Object::cast_to<Exam>(node);
    if (!expect(exam != nullptr, "the exam scene root is an Exam")) {
        if (node != nullptr) {
            memdelete(node);
        }
        return nullptr;
    }
    if (!p_manifest.is_empty()) {
        exam->set_manifest(p_manifest);
    }
    add_child(exam);
    exam->set_size(godot::Vector2(EXAM_WIDTH, EXAM_HEIGHT));
    return exam;
}

Exam *AudioIntegrationRunner::make_silent_exam(const godot::PackedStringArray &p_manifest) {
    const godot::Ref<godot::PackedScene> scene =
        godot::ResourceLoader::get_singleton()->load(EXAM_SCENE);
    if (!expect(scene.is_valid(), "the real exam scene loads for the silent control")) {
        return nullptr;
    }
    godot::Node *node = scene->instantiate();
    auto *exam = godot::Object::cast_to<Exam>(node);
    if (exam == nullptr) {
        if (node != nullptr) {
            memdelete(node);
        }
        return nullptr;
    }
    // Removed before the scene ever enters the tree, so no director is ever
    // ready and no audio of any kind exists for this exam.
    for (std::int64_t i = exam->get_child_count() - 1; i >= 0; --i) {
        auto *director = godot::Object::cast_to<AudioDirector>(exam->get_child(i));
        if (director != nullptr) {
            exam->remove_child(director);
            memdelete(director);
        }
    }
    if (!p_manifest.is_empty()) {
        exam->set_manifest(p_manifest);
    }
    add_child(exam);
    exam->set_size(godot::Vector2(EXAM_WIDTH, EXAM_HEIGHT));
    return exam;
}

AudioDirector *AudioIntegrationRunner::director_of(Exam &p_exam, std::size_t *r_count) {
    AudioDirector *found = nullptr;
    std::size_t count = 0;
    for (std::int64_t i = 0; i < p_exam.get_child_count(); ++i) {
        auto *director = godot::Object::cast_to<AudioDirector>(p_exam.get_child(i));
        if (director == nullptr) {
            continue;
        }
        ++count;
        if (found == nullptr) {
            found = director;
        }
    }
    if (r_count != nullptr) {
        *r_count = count;
    }
    return found;
}

void AudioIntegrationRunner::finish_transition(Exam &p_exam) {
    int guard = 0;
    while (p_exam.transitioning() && guard < 8) {
        p_exam.advance_transition(1.0);
        ++guard;
    }
}

bool AudioIntegrationRunner::solve_fixture(LevelPlayer &p_canvas) {
    const Point origin { Coordinate::from_raw(0), Coordinate::from_raw(0) };
    p_canvas.select_entry(0);
    return accept_at(p_canvas, origin) && place_last_tile(p_canvas);
}

// The one placement a single undo takes back. Solving again is exactly this, so
// a second victory is reached the same ordinary way the first one was.
bool AudioIntegrationRunner::place_last_tile(LevelPlayer &p_canvas) {
    const Point domino { Coordinate::from_raw(2 * Coordinate::SCALE), Coordinate::from_raw(0) };
    p_canvas.select_entry(1);
    p_canvas.cycle_orientation(true);
    return accept_at(p_canvas, domino);
}

// --- ownership ---

void AudioIntegrationRunner::check_scene_ownership() {
    Exam *exam = make_exam(godot::PackedStringArray());
    if (exam == nullptr) {
        return;
    }
    std::size_t count = 0;
    AudioDirector *director = director_of(*exam, &count);
    expect(count == 1, "the ordinary exam scene owns exactly one audio director");
    if (!expect(director != nullptr, "the exam's audio director is an AudioDirector")) {
        exam->queue_free();
        return;
    }
    expect(exam->ready_to_play(), "the authored exam is ready to play with a director inside it");

    godot::AudioStreamPlayer *player = director->background_player();
    if (!expect(player != nullptr, "the director owns one background player")) {
        exam->queue_free();
        return;
    }
    expect(player->get_parent() == director, "the background player belongs to the director");
    const std::uint64_t director_id = director->get_instance_id();
    const std::uint64_t player_id = player->get_instance_id();

    // Navigation is the thing which must not disturb any of this: canvases are
    // built and freed around a director which never notices.
    const double before_elapsed = director->ambient_elapsed();
    const AudioDirector::AmbientPhase before_phase = director->ambient_phase();
    expect(exam->navigate(1), "the authored exam navigates forward");
    finish_transition(*exam);
    expect(exam->navigate(1), "the authored exam navigates forward again");
    finish_transition(*exam);
    expect(exam->navigate(-1), "the authored exam navigates back");
    finish_transition(*exam);

    std::size_t after_count = 0;
    AudioDirector *after = director_of(*exam, &after_count);
    expect(after_count == 1, "navigation leaves exactly one audio director");
    expect(
        after == director && after->get_instance_id() == director_id,
        "navigation reuses the same audio director instance");
    expect(
        director->background_player() == player
            && player->get_instance_id() == player_id,
        "navigation reuses the same background player instance");
    expect(
        director->background_started() && player->is_playing(),
        "the background voice is still the same playing instance after navigation");
    expect(
        director->ambient_phase() == before_phase
            && director->ambient_elapsed() == before_elapsed,
        "navigation neither restarts the background track nor resets its envelope");
    exam->queue_free();
}

// --- the background voice ---

void AudioIntegrationRunner::check_background_start() {
    expect(
        godot::ResourceLoader::get_singleton()->exists(BACKGROUND_STREAM),
        "the authored background track is a res:// resource");

    Exam *exam = make_exam(manifest_of({ FIXTURE_LEVEL }));
    if (exam == nullptr) {
        return;
    }
    AudioDirector *director = director_of(*exam, nullptr);
    if (!expect(director != nullptr, "the exam under test owns a director")) {
        exam->queue_free();
        return;
    }
    const godot::Ref<godot::AudioStreamMP3> stream = director->background_stream();
    if (!expect(stream.is_valid(), "the supplied background resource loads as an AudioStreamMP3")) {
        exam->queue_free();
        return;
    }
    expect(stream->has_loop(), "the background stream loops");
    expect(stream->get_loop_offset() == 0.0, "the background loop begins at the start of the file");

    godot::AudioStreamPlayer *player = director->background_player();
    if (!expect(player != nullptr, "the background player exists")) {
        exam->queue_free();
        return;
    }
    expect(player->get_stream() == stream, "the background player plays exactly that stream");
    expect(director->background_started(), "the background track was started once");
    expect(player->is_playing(), "the background voice is playing");
    expect(player->get_max_polyphony() == 1, "the background is one voice, not a polyphonic bank");

    // It begins at the floor and already fading in: no initial absent hold.
    expect(
        director->ambient_phase() == AudioDirector::AmbientPhase::fade_in,
        "the envelope begins in fade in");
    expect(director->ambient_elapsed() == 0.0, "the first fade in begins immediately");
    expect(near_db(director->ambient_db(), INAUDIBLE_DB), "the envelope begins at the floor");
    expect(near_db(director->duck_db(), 0.0), "no duck is running at startup");
    expect(near_db(director->background_db(), INAUDIBLE_DB), "the voice begins inaudible");
    expect(
        near_db(static_cast<double>(player->get_volume_db()), INAUDIBLE_DB),
        "the player's own volume was set to the floor before it was started");
    exam->queue_free();
}

// --- the ambient envelope ---

void AudioIntegrationRunner::check_ambient_envelope() {
    Exam *exam = make_exam(manifest_of({ FIXTURE_LEVEL }));
    if (exam == nullptr) {
        return;
    }
    AudioDirector *director = director_of(*exam, nullptr);
    if (!expect(director != nullptr, "the exam under test owns a director")) {
        exam->queue_free();
        return;
    }
    godot::AudioStreamPlayer *player = director->background_player();

    // A fade is smooth, monotonic in decibels, inside its endpoints, and it does
    // not overshoot either of them.
    bool monotone = true;
    bool bounded = true;
    double previous = director->ambient_db();
    const double fade_in = director->ambient_duration();
    for (int step = 1; step <= 40; ++step) {
        director->advance(fade_in / 40.0);
        const double now = director->ambient_db();
        if (now < previous - EPSILON) {
            monotone = false;
        }
        if (now < INAUDIBLE_DB - EPSILON || now > AUDIBLE_DB + EPSILON) {
            bounded = false;
        }
        previous = now;
    }
    expect(monotone, "a fade in rises monotonically in decibels");
    expect(bounded, "a fade never overshoots either endpoint");
    // Forty deltas of a fortieth do not sum to exactly one, so the sliver the
    // sampling may have left behind is spent here rather than pretended away.
    if (director->ambient_phase() == AudioDirector::AmbientPhase::fade_in) {
        director->advance(director->ambient_duration() - director->ambient_elapsed());
    }
    expect(
        director->ambient_phase() == AudioDirector::AmbientPhase::audible_hold,
        "a completed fade in reaches the audible hold");
    expect(near_db(director->ambient_db(), AUDIBLE_DB), "a completed fade in reaches exactly 0 db");

    // Every phase, over many whole cycles: its sampled duration is inside its
    // contracted window, it ends exactly on its endpoint, and the next phase is
    // the one the cycle names.
    bool durations_in_bounds = true;
    bool endpoints_exact = true;
    bool order_correct = true;
    bool volume_tracks = true;
    for (int phase = 0; phase < 200; ++phase) {
        const AudioDirector::AmbientPhase current = director->ambient_phase();
        double low = 0.0;
        double high = 0.0;
        phase_bounds(current, &low, &high);
        const double duration = director->ambient_duration();
        if (duration < low - EPSILON || duration > high + EPSILON) {
            durations_in_bounds = false;
            godot::UtilityFunctions::push_error(
                "[tiles] audio integration: ", phase_name(current), " sampled ", duration);
        }
        if (!near_db(director->ambient_db(), phase_start_db(current))) {
            endpoints_exact = false;
        }
        if (player != nullptr
            && !near_db(
                static_cast<double>(player->get_volume_db()), director->background_db())) {
            volume_tracks = false;
        }
        director->advance(duration - director->ambient_elapsed());
        if (director->ambient_phase() != next_phase(current)) {
            order_correct = false;
        }
    }
    expect(durations_in_bounds, "every sampled phase duration falls inside its contracted window");
    expect(endpoints_exact, "every phase begins exactly on the endpoint the previous one reached");
    expect(order_correct, "the envelope cycles fade in, audible, fade out, absent, in that order");
    expect(volume_tracks, "the player's volume is exactly the summed gain at every phase start");

    // A delta which overruns the current phase carries its remainder into the
    // next one instead of dropping it.
    const double remaining = director->ambient_duration() - director->ambient_elapsed();
    const AudioDirector::AmbientPhase crossing = director->ambient_phase();
    director->advance(remaining + 0.25);
    expect(
        director->ambient_phase() == next_phase(crossing),
        "one large delta crosses into the next phase");
    expect(
        std::abs(director->ambient_elapsed() - 0.25) <= EPSILON,
        "a crossed phase boundary loses none of the delta which crossed it");

    // A delta far larger than any single phase leaves a well-formed machine
    // rather than a stalled or overrun one.
    director->advance(10000.0);
    double low = 0.0;
    double high = 0.0;
    phase_bounds(director->ambient_phase(), &low, &high);
    expect(
        director->ambient_duration() >= low - EPSILON
            && director->ambient_duration() <= high + EPSILON,
        "an unusually large delta lands in a phase with a valid duration");
    expect(
        director->ambient_elapsed() >= 0.0
            && director->ambient_elapsed() < director->ambient_duration(),
        "an unusually large delta lands inside its phase rather than past it");
    expect(
        director->ambient_db() >= INAUDIBLE_DB - EPSILON
            && director->ambient_db() <= AUDIBLE_DB + EPSILON,
        "an unusually large delta leaves the gain inside its endpoints");

    // The voice is the same instance on the far side of an absent stretch: the
    // envelope silences the music without ever stopping it.
    if (expect(run_to_phase(*director, AudioDirector::AmbientPhase::absent_hold),
            "the envelope reaches an absent hold")) {
        expect(near_db(director->background_db(), INAUDIBLE_DB), "an absent hold is inaudible");
        expect(
            director->background_player() == player && player != nullptr && player->is_playing(),
            "the background voice keeps playing through the absent hold");
        expect(run_to_phase(*director, AudioDirector::AmbientPhase::fade_in),
            "the envelope returns to a fade in");
        expect(
            director->background_player() == player && player != nullptr && player->is_playing(),
            "the returning music is the same voice, never a restarted one");
        expect(director->background_started(), "the background track was never started twice");
    }
    exam->queue_free();
}

// --- the victory duck ---

void AudioIntegrationRunner::check_victory_duck() {
    Exam *exam = make_exam(manifest_of({ FIXTURE_LEVEL }));
    if (exam == nullptr) {
        return;
    }
    AudioDirector *director = director_of(*exam, nullptr);
    LevelPlayer *canvas = exam->current_canvas();
    if (!expect(director != nullptr && canvas != nullptr, "the fixture exam is mounted")) {
        exam->queue_free();
        return;
    }
    expect(
        run_to_phase(*director, AudioDirector::AmbientPhase::audible_hold),
        "the envelope reaches its audible hold");
    const double ambient_at_victory = director->ambient_db();
    const double elapsed_at_victory = director->ambient_elapsed();

    // A real solve through ordinary proposals, so the duck is reached through the
    // exam's own semantic signal rather than by calling the sink.
    expect(solve_fixture(*canvas), "the fixture solves through ordinary proposals");
    expect(canvas->completion_visible(), "the fixture is solved");
    expect(
        director->duck_phase() == AudioDirector::DuckPhase::attack,
        "a real victory starts the duck's attack");
    expect(near_db(director->duck_db(), 0.0), "the attack begins at the gain it was already at");
    expect(
        near_db(director->ambient_db(), ambient_at_victory)
            && director->ambient_elapsed() == elapsed_at_victory,
        "a victory neither cancels, delays, nor reshapes the ambient envelope");

    // The attack falls smoothly and monotonically to the contracted floor.
    bool monotone = true;
    bool bounded = true;
    double previous = director->duck_db();
    for (int step = 1; step <= 20; ++step) {
        director->advance(DUCK_ATTACK_SECONDS / 20.0);
        const double now = director->duck_db();
        if (now > previous + EPSILON) {
            monotone = false;
        }
        if (now < DUCK_DB - EPSILON || now > 0.0 + EPSILON) {
            bounded = false;
        }
        previous = now;
    }
    expect(monotone, "the duck attack falls monotonically");
    expect(bounded, "the duck attack never overshoots -12 db");
    expect(
        director->duck_phase() == AudioDirector::DuckPhase::hold,
        "a completed attack reaches the duck hold");
    expect(near_db(director->duck_db(), DUCK_DB), "the duck holds at exactly -12 db");
    expect(
        near_db(director->background_db(), director->ambient_db() + DUCK_DB),
        "the background gain is exactly the sum of the two components");
    expect(
        director->ambient_phase() == AudioDirector::AmbientPhase::audible_hold,
        "the duck did not move the ambient envelope out of its phase");

    // Hold, then release back to no attenuation at all.
    director->advance(DUCK_HOLD_SECONDS);
    expect(
        director->duck_phase() == AudioDirector::DuckPhase::release,
        "the duck hold lasts exactly two seconds");
    expect(near_db(director->duck_db(), DUCK_DB), "the release begins at -12 db");
    director->advance(DUCK_RELEASE_SECONDS * 0.5);
    const double mid_release = director->duck_db();
    expect(
        mid_release > DUCK_DB + EPSILON && mid_release < 0.0 - EPSILON,
        "the release recovers through the gain between -12 db and 0 db");

    // A second victory during the duck restarts it from where it is. It never
    // stacks a second -12 db, and it never jumps.
    expect(canvas->undo(), "the solved fixture can be taken apart again");
    expect(place_last_tile(*canvas), "the fixture can be solved a second time");
    expect(
        director->duck_phase() == AudioDirector::DuckPhase::attack,
        "a second victory restarts the duck's attack");
    expect(
        near_db(director->duck_db(), mid_release),
        "the restarted attack begins from the gain the duck was already at");
    bool never_stacked = true;
    for (int step = 1; step <= 20; ++step) {
        director->advance(DUCK_ATTACK_SECONDS / 20.0);
        if (director->duck_db() < DUCK_DB - EPSILON) {
            never_stacked = false;
        }
    }
    expect(never_stacked, "a restarted duck never attenuates past -12 db");
    expect(near_db(director->duck_db(), DUCK_DB), "the restarted duck reaches exactly -12 db");

    // A victory while the music is absent stays inaudible: the sum, not the duck
    // alone, is what the voice hears.
    director->advance(DUCK_HOLD_SECONDS + DUCK_RELEASE_SECONDS);
    expect(
        director->duck_phase() == AudioDirector::DuckPhase::inactive,
        "a completed duck contributes nothing");
    expect(near_db(director->duck_db(), 0.0), "a completed duck rests at 0 db");
    if (expect(run_to_phase(*director, AudioDirector::AmbientPhase::absent_hold),
            "the envelope reaches an absent hold")) {
        expect(canvas->undo(), "the fixture can be taken apart once more");
        expect(place_last_tile(*canvas), "the fixture can be solved once more");
        director->advance(DUCK_ATTACK_SECONDS);
        expect(
            near_db(director->background_db(), INAUDIBLE_DB + DUCK_DB),
            "a victory during an absent hold ducks an already inaudible voice");
        expect(
            director->background_player() != nullptr
                && director->background_player()->is_playing(),
            "ducking never stops the background track");
    }
    exam->queue_free();
}

// --- audio cannot reach exact play ---

void AudioIntegrationRunner::check_play_is_unaffected() {
    // The same exam, once with its director and once with the director removed
    // before it could ever become ready. Every exact outcome must agree.
    Exam *heard = make_exam(manifest_of({ FIXTURE_LEVEL, ACT_LEVEL }));
    Exam *silent = make_silent_exam(manifest_of({ FIXTURE_LEVEL, ACT_LEVEL }));
    if (heard == nullptr || silent == nullptr) {
        return;
    }
    std::size_t silent_count = 1;
    director_of(*silent, &silent_count);
    expect(silent_count == 0, "the control exam has no audio at all");
    expect(
        heard->ready_to_play() && silent->ready_to_play(),
        "readiness to play is the same with and without audio");
    expect(
        heard->problem_count() == silent->problem_count(),
        "the problem count is the same with and without audio");

    LevelPlayer *heard_canvas = heard->current_canvas();
    LevelPlayer *silent_canvas = silent->current_canvas();
    if (!expect(
            heard_canvas != nullptr && silent_canvas != nullptr,
            "both exams mounted their first problem")) {
        heard->queue_free();
        silent->queue_free();
        return;
    }
    expect(
        solve_fixture(*heard_canvas) == solve_fixture(*silent_canvas),
        "the same ordinary solve succeeds identically with and without audio");
    expect(
        heard_canvas->completion_visible() == silent_canvas->completion_visible(),
        "completion is the same with and without audio");
    expect(
        heard->problem(0)->session().state().arrangement().entries().size()
            == silent->problem(0)->session().state().arrangement().entries().size(),
        "the exact arrangement is the same with and without audio");
    expect(
        heard->problem(0)->session().undo_depth() == silent->problem(0)->session().undo_depth(),
        "the exact undo depth is the same with and without audio");
    expect(
        heard_canvas->undo() == silent_canvas->undo(),
        "undo behaves identically with and without audio");
    expect(
        heard->navigate(1) == silent->navigate(1),
        "navigation is refused or accepted identically with and without audio");
    finish_transition(*heard);
    finish_transition(*silent);
    expect(
        heard->current_index() == silent->current_index(),
        "navigation lands on the same problem with and without audio");
    heard->queue_free();
    silent->queue_free();

    // A director with nothing to listen to reports its own failure and stays
    // inert. The error below is expected.
    godot::UtilityFunctions::print(
        "[tiles] audio integration: the next error is expected: a director with no exam");
    auto *orphan = memnew(AudioDirector);
    add_child(orphan);
    expect(
        orphan->background_started(),
        "a director which found no exam still owns its own background voice");
    expect(
        orphan->duck_phase() == AudioDirector::DuckPhase::inactive,
        "a director which found no exam receives no cue");
    orphan->advance(1.0);
    expect(
        orphan->ambient_elapsed() > 0.0,
        "a director which found no exam keeps its envelope running rather than retrying");
    orphan->queue_free();
}

void AudioIntegrationRunner::_ready() {
    check_scene_ownership();
    check_background_start();
    check_ambient_envelope();
    check_victory_duck();
    check_play_is_unaffected();

    if (failures_ == 0) {
        godot::UtilityFunctions::print(
            "[tiles] audio integration: ",
            static_cast<std::int64_t>(checks_),
            " checks passed");
    } else {
        godot::UtilityFunctions::push_error(
            "[tiles] audio integration: ",
            static_cast<std::int64_t>(failures_),
            " of ",
            static_cast<std::int64_t>(checks_),
            " checks failed");
    }
    get_tree()->quit(failures_ == 0 ? 0 : 1);
}

} // namespace tiles::game
