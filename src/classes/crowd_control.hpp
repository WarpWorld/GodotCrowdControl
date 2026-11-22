#ifndef CROWD_CONTROL_CROWD_CONTROL_HPP
#define CROWD_CONTROL_CROWD_CONTROL_HPP

#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/templates/hash_map.hpp>

#include "classes/cc_effect/cc_effect.hpp"
#include "classes/cc_effect/cc_effect_bid_war.hpp"
#include "classes/cc_effect/cc_effect_parameters.hpp"
#include "classes/cc_effect/cc_effect_timed.hpp"
#include "classes/cc_effect_instance.hpp"
#include "json/effect_request.hpp"
#include "util/cc_enums.hpp"

using namespace godot;

class StreamUser;
class CCNetworkManager;

/// @brief The main management class for Crowd Control.
/// @details
/// In an attempt to make this as close as possible to a native Godot module
/// this is a singleton registered to the @see Engine singleton.
/// The user in GDScript can use the exposed signals to react to the various
/// events during a session.
///
/// A limitation of the @see ProjectSettings system is that resources cannot be
/// directly loaded in the built-in menu.
/// Instead the user specifies the path to a @see EffectEntries resource that is
/// is loaded when the session starts, which comes with its own set of issues.
///
/// As a lot of the processing happens on the threads monitoring the socket/http client
/// most signals are deferred to the next process frame to allow users to write
/// their code as without having to account for this.
///
/// Because Godot allows to use custom @see MainLoop s that do not rely on
/// @see Node s this is implemented as a standalone module meaning we can not use
/// the `_process` or `_physics_process` functions to handle events or use
/// @SceneTree.paused, choosing to  use threads instead.
/// The `crowd_control/common/process_mode` and `crowd_control/common/use_scene_tree_pause`
/// project settings will allow the user to override this to use the @see SceneTree instead.
class CrowdControl : public Object {
	GDCLASS(CrowdControl, Object)

	friend class CCNetworkManager;

public:
	static const char *effect_instance_status_names[];

private:
	static CrowdControl *singleton;
	const String LOG_PREFIX = "[CC] ";
	/// @brief Seconds a request may be held before it has to be answered.
	/// @details
	/// The service expects an answer within 60 seconds, we give up a little earlier
	/// to leave room for the response to make it there.
	static constexpr double REQUEST_TIMEOUT = 55.0;

	Ref<StreamUser> crowd_user;
	Ref<StreamUser> anonymous_user;
	Ref<StreamUser> streamer_user;
	HashMap<String, Ref<StreamUser>> stream_users = HashMap<String, Ref<StreamUser>>();
	HashMap<StringName, Ref<CCEffect>> effects = HashMap<StringName, Ref<CCEffect>>();
	// For some reason references get lost when using List<>, need to investigate
	TypedArray<CCEffectInstance> effect_queue = TypedArray<CCEffectInstance>();
	HashMap<StringName, TypedArray<CCEffectInstanceTimed>> halted_timers = HashMap<StringName, TypedArray<CCEffectInstanceTimed>>();
	HashMap<StringName, Ref<CCEffectInstanceTimed>> running_effects = HashMap<StringName, Ref<CCEffectInstanceTimed>>();

	// instance properties accessible via bindings
	bool staging = false;
	bool paused = false;
	BroadcasterType broadcaster_type = COMMUNITY;
	bool auto_starting_session = false;

	// ProjectSettings/common
	String game_name = "Godot Demo";
	int reconnect_retry_count = -1;
	float reconnect_retry_delay = 5.0f;
	float delay_between_effects = 0.5f;
	Ref<CCEffectEntries> effect_entries;
	// ProjectSettings/common/process
	int process_mode = 0;
	// Reference to scenetree if not using internal processing
	SceneTree *scene_tree = nullptr;

	// ProjectSettings/visuals
	Ref<Texture2D> temp_user_icon;
	Color temp_user_color = Color(0.0f, 0.0f, 0.0f, 0.6f);
	Ref<Texture2D> crowd_user_icon;
	Color crowd_user_color = Color(0.094117f, 0.466666f, 0.937254f);
	Ref<Texture2D> error_user_icon;
	Color error_user_color = Color(0.77647f, 0.0f, 0.0f);

	// ProjectSettings/debug
	bool debug_info = true;
	bool debug_warn = true;
	bool debug_error = true;
	bool debug_exception = true;

	bool quitting = false;
	bool quit_on_session_close = false;
	bool session_active = false;

	Ref<CCNetworkManager> connection;
	Ref<Thread> process_thread;
	/// @brief Mutex to ensure we don't perform unsafe methods on the effect queue (resizing is not thread-safe)
	Ref<Mutex> queue_mutex;

	float time_until_next_effect = 0;

private:
	void thread_process();
	void tree_process();
	void tree_physics_process();

	void handle_pending();
	/// @brief Answers requests that have been waiting for too long with a temporary failure.
	void expire_stale_requests();

	bool try_start(const Ref<CCEffectInstance> &instance);
	bool stop_effect(const Ref<CCEffectInstanceTimed> &instance);
	/// @brief Stops a running timed effect.
	/// @param notify whether the service should be told, false if it was the one asking for the stop
	bool end_effect(const Ref<CCEffectInstanceTimed> &instance, bool notify);
	/// @brief Stops every running effect and fails every pending request, e.g. because the session ended.
	void stop_all_effects();
	/// @brief Drops a request the service no longer wants fulfilled.
	void cancel_request(const String &p_request_id);
	/// @brief Reports the state of a request to the service.
	/// @param after_success sends `success` first as part of the same call, as expected when a timed effect begins
	void respond(const Ref<CCEffectInstance> &instance, EffectInstanceStatus status, bool after_success = false, const String &message = "");
	/// @brief Whether an effect this one conflicts with is currently running.
	bool has_running_conflict(const Ref<CCEffect> &effect) const;
	/// @brief Applies a menu state to a set of effects and reports it in a single call.
	bool report_effects(const PackedStringArray &p_ids, CCEffect::EffectMenuStatus p_status, const String &p_message);
	PackedStringArray get_all_effect_ids() const;
	/// @brief Reports every hidden / unavailable effect, for sessions that were not started by the game.
	void sync_effect_states();
	bool start_effect(const Ref<CCEffectInstance> &instance);
	void retry_start_effect(const Ref<CCEffect> &effect, const Ref<CCEffectInstance> &instance, bool &dequeue, CCEffect::EffectResult &result);
	void finish_start_effect(const Ref<CCEffect> &effect, const Ref<CCEffectInstance> &instance, bool &dequeue);
	void resume_effect(const Ref<CCEffectTimed> &effect);
	void pause_effect(const Ref<CCEffectTimed> &effect);
	void reset_effect(const Ref<CCEffectTimed> &effect);

	void load_effects();

	void _on_connected();
	void _on_disconnected();
	void _on_effect_requested(const Ref<CCEffect> &effect);
	void _on_session_started();

	void queue_effect(const Ref<CCEffect> &effect, User requester, String requestID, bool is_anonymous, HashMap<String, ParameterEntry> parameters = HashMap<String, ParameterEntry>(), double duration = 0, bool is_test = false, bool is_local = false);
	void dequeue_effect(const Ref<CCEffectInstance> &instance, CCEffect::EffectResult result);

	template <class T>
	void create_effect_instance(const Ref<StreamUser> &user, const Ref<CCEffect> &effect, User requester, String requestID, HashMap<String, ParameterEntry> parameters = HashMap<String, ParameterEntry>(), double duration = 0, bool is_test = false, bool is_local = false);
	void cancel_effect(const Ref<CCEffectInstance> &instance);

	Ref<StreamUser> get_stream_user(const User &user);

protected:
	static void _bind_methods();

public:
	static CrowdControl *get_singleton();

	String get_game_name() const { return game_name; }
	void set_game_name(const String &p_game_name) { game_name = p_game_name; }

	bool is_staging() const { return staging; }
	void set_staging(bool p_staging) { staging = p_staging; }

	BroadcasterType get_broadcaster_type() const { return broadcaster_type; }
	void set_broadcaster_type(BroadcasterType p_broadcaster_type) { broadcaster_type = p_broadcaster_type; }

	int get_reconnect_retry_count() const { return reconnect_retry_count; }
	void set_reconnect_retry_count(int p_reconnect_retry_count) { reconnect_retry_count = p_reconnect_retry_count; }

	float get_reconnect_retry_delay() const { return reconnect_retry_delay; }
	void set_reconnect_retry_delay(float p_reconnect_retry_delay) { reconnect_retry_delay = p_reconnect_retry_delay; }

	Ref<Texture2D> get_temp_user_icon() const { return temp_user_icon; }
	void set_temp_user_icon(const Ref<Texture2D> &p_temp_user_icon) { temp_user_icon = p_temp_user_icon; }

	Color get_temp_user_color() const { return temp_user_color; }
	void set_temp_user_color(Color p_temp_user_color) { temp_user_color = p_temp_user_color; }

	Ref<Texture2D> get_crowd_user_icon() const { return crowd_user_icon; }
	void set_crowd_user_icon(const Ref<Texture2D> &p_crowd_user_icon) { crowd_user_icon = p_crowd_user_icon; }

	Color get_crowd_user_color() const { return crowd_user_color; }
	void set_crowd_user_color(Color p_crowd_user_color) { crowd_user_color = p_crowd_user_color; }

	Ref<Texture2D> get_error_user_icon() const { return error_user_icon; }
	void set_error_user_icon(const Ref<Texture2D> &p_error_user_icon) { error_user_icon = p_error_user_icon; }

	Color get_error_user_color() const { return error_user_color; }
	void set_error_user_color(Color p_error_user_color) { error_user_color = p_error_user_color; }

	float get_delay_between_effects() const { return delay_between_effects; }
	void set_delay_between_effects(float p_delay_between_effects) { delay_between_effects = p_delay_between_effects; }

	bool is_debug_info() const { return debug_info; }
	void set_debug_info(bool p_debug_info) { debug_info = p_debug_info; }

	bool is_debug_warn() const { return debug_warn; }
	void set_debug_warn(bool p_debug_warn) { debug_warn = p_debug_warn; }

	bool is_debug_error() const { return debug_error; }
	void set_debug_error(bool p_debug_error) { debug_error = p_debug_error; }

	bool is_debug_exception() const { return debug_exception; }
	void set_debug_exception(bool p_debug_exception) { debug_exception = p_debug_exception; }

	bool is_paused() const { return paused; }
	void set_paused(bool p_paused);

	bool is_auto_starting_session() const { return auto_starting_session; }
	void set_auto_starting_session(bool p_auto_starting_session) { auto_starting_session = p_auto_starting_session; }

	Ref<StreamUser> get_streamer_user() const { return streamer_user; }

	// for some reason more than one argument results in "unresolved externals" errors, need to investigate
	// use vformat(...) instead to format the string into a single object
	/*template <class... Args>
	void log_info(const Variant &arg1, const Args &...args);
	template <class... Args>
	void log_warn(const Variant &arg1, const Args &...args);
	template <class... Args>
	void log_error(const Variant &arg1, const Args &...args);
	template <class... Args>
	void log_exception(const Variant &arg1, const Args &...args);*/

	void log_info(const Variant &arg1);
	void log_warn(const Variant &arg1);
	void log_error(const Variant &arg1);
	void log_exception(const Variant &arg1);

	Error connect_to_crowd_control();
	void close();
	/// @brief Starts the login. Emits `auth_code_received` with the URL the user has to sign in at,
	/// followed by `logged_in` or `login_failed`.
	void login();
	/// @brief Logs the user out and forgets the stored login.
	void logout();
	bool is_logged_in() const;
	/// @brief Asks the service for a later expiry of the login. Done automatically when it is about to run out,
	/// but only applications with the `token:extend` scope may do so. Emits `token_refreshed` on success.
	bool extend_token();
	/// @brief The link viewers use to interact with the streamer, empty if not logged in.
	String get_interact_link() const;
	/// @brief Opens the interact link in the browser.
	void launch_interact_link() const;
	/// @brief Sends a ping, `pong_received` is emitted when the service answers.
	void ping();

	/* MENU CONTROL */
	// "Show" / "hide" control whether effects are listed at all, "enable" / "disable" whether listed effects can
	// be bought. Changes made outside of a session are remembered and applied when the next one starts.

	bool show_effects(const PackedStringArray &p_ids, const String &p_message = "");
	bool hide_effects(const PackedStringArray &p_ids, const String &p_message = "");
	bool enable_effects(const PackedStringArray &p_ids, const String &p_message = "");
	bool disable_effects(const PackedStringArray &p_ids, const String &p_message = "");
	bool show_all_effects(const String &p_message = "");
	bool hide_all_effects(const String &p_message = "");
	bool enable_all_effects(const String &p_message = "");
	bool disable_all_effects(const String &p_message = "");
	/// @brief The `effectReportArgs` describing every hidden / unavailable effect.
	Array get_effect_report_args() const;

	/// @brief Tells the service that an event defined in the game pack (`meta.events`) occurred.
	bool trigger_event(const String &p_event_id, const Dictionary &p_args = Dictionary());
	/// @brief Updates metadata values of the game pack, e.g. for effect names using `{{key}}` placeholders.
	bool update_metadata(const String &p_key, const Variant &p_value);
	bool update_metadata_values(const Dictionary &p_values);

	/* CUSTOM EFFECTS (development tool) */
	// Needs a login with the `custom-effects:write` scope and a game pack with `allowCustomEffects` enabled.
	// The result is reported through `custom_effects_updated`.

	bool can_upload_custom_effects() const;
	/// @brief Uploads the definitions of all registered effects flagged as `custom`.
	bool upload_custom_effects(CustomEffectsMode p_mode = CUSTOM_EFFECTS_MERGE);
	bool delete_custom_effects(const PackedStringArray &p_ids);
	bool delete_all_custom_effects();
	bool is_connected_to_crowd_control() const;
	bool is_session_active() const;

	void start_session();
	void stop_session();

	void test_effect(const Ref<CCEffect> &effect);
	void test_effect_remotely(const Ref<CCEffect> &effect);

	void display_message(String message, float duration = 5.0);

	void register_effect(const Ref<CCEffect> &effect);
	Ref<CCEffect> get_effect(const StringName &name) const;

	void _process(float delta);

	void update_effect_status(const Ref<CCEffect> &p_effect, const String &command);
	void post_effect_update(const String &p_data = "");

	CrowdControl();
	~CrowdControl();
};

#endif //CROWD_CONTROL_CROWD_CONTROL_HPP