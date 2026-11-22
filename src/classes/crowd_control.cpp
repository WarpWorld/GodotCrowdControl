#include "crowd_control.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "classes/cc_network_manager.hpp"
#include "classes/stream_user.hpp"
#include "json/effect_report.hpp"
#include "json/game_session.hpp"
#include "json/manifest.hpp"
#include "json/request_effect.hpp"
#include "util/binding_macros.hpp"
#include "util/json_serializer.hpp"
#include "util/project_settings.hpp"

CrowdControl *CrowdControl::singleton = nullptr;

const char *CrowdControl::effect_instance_status_names[] = {
	"success",
	"failTemporary",
	"failPermanent",
	"timedBegin",
	"timedPause",
	"timedResume",
	"timedEnd",
};

void CrowdControl::_bind_methods() {
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "crowd_control/common/game_id"), "GodotDemo");
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "crowd_control/common/game_name"), "Godot Demo");
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "crowd_control/common/app_id"), "");
	// The build of the game (e.g. "1.2.0"), sent when a session starts so the pack version made for it is used.
	// Falls back to `application/config/version` if left empty.
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "crowd_control/common/game_version"), "");
	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "crowd_control/common/auth/auto_open_url"), true);
	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "crowd_control/common/auth/persist_token"), true);
	GLOBAL_DEF(PropertyInfo(Variant::PACKED_STRING_ARRAY, "crowd_control/common/auth/scopes"), PackedStringArray());
	GLOBAL_DEF(PropertyInfo(Variant::INT, "crowd_control/common/retry_count"), -1);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "crowd_control/common/retry_delay"), 5.0f);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "crowd_control/common/delay_between_effects", PROPERTY_HINT_RANGE, "0,10,0.001,suffix:s"), 0.5f);
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "crowd_control/common/effects", PROPERTY_HINT_FILE, "*.tres"), "res://effects.tres");

	GLOBAL_DEF_RST(PropertyInfo(Variant::INT, "crowd_control/common/process/process_mode", PROPERTY_HINT_ENUM, "Internal,Process,Physics Process"), 1);
	GLOBAL_DEF_RST(PropertyInfo(Variant::BOOL, "crowd_control/common/process/use_scene_tree_pause"), false);

	GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "crowd_control/visuals/temp_user_icon", PROPERTY_HINT_FILE, "*.png,*.svg,*.jpg"), "res://addons/godotcrowdcontrol/assets/icons/user.png");
	GLOBAL_DEF(PropertyInfo(Variant::COLOR, "crowd_control/visuals/temp_user_color"), Color(0.0f, 0.0f, 0.0f, 0.6f));
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "crowd_control/visuals/crowd_user_icon", PROPERTY_HINT_FILE, "*.png,*.svg,*.jpg"), "res://addons/godotcrowdcontrol/assets/icons/user_crowd.png");
	GLOBAL_DEF(PropertyInfo(Variant::COLOR, "crowd_control/visuals/crowd_user_color"), Color(0.094117f, 0.466666f, 0.937254f));
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "crowd_control/visuals/error_user_icon", PROPERTY_HINT_FILE, "*.png,*.svg,*.jpg"), "res://addons/godotcrowdcontrol/assets/icons/user_error.png");
	GLOBAL_DEF(PropertyInfo(Variant::COLOR, "crowd_control/visuals/error_user_color"), Color(0.77647f, 0.0f, 0.0f));

	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "crowd_control/debug/log_info"), true);
	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "crowd_control/debug/log_warn"), true);
	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "crowd_control/debug/log_error"), true);
	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "crowd_control/debug/log_exception"), true);
	// Point the plugin at another deployment of the service (or a local mock), e.g. "wss://..." and "https://...".
	// Leave empty to use the official endpoints.
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "crowd_control/debug/pubsub_url"), "");
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "crowd_control/debug/api_url"), "");

	ClassDB::bind_method(D_METHOD("connect_to_crowd_control"), &CrowdControl::connect_to_crowd_control);
	ClassDB::bind_method(D_METHOD("login"), &CrowdControl::login);
	ClassDB::bind_method(D_METHOD("logout"), &CrowdControl::logout);
	ClassDB::bind_method(D_METHOD("is_logged_in"), &CrowdControl::is_logged_in);
	ClassDB::bind_method(D_METHOD("extend_token"), &CrowdControl::extend_token);
	ClassDB::bind_method(D_METHOD("get_interact_link"), &CrowdControl::get_interact_link);
	ClassDB::bind_method(D_METHOD("launch_interact_link"), &CrowdControl::launch_interact_link);
	ClassDB::bind_method(D_METHOD("ping"), &CrowdControl::ping);

	ClassDB::bind_method(D_METHOD("show_effects", "effect_ids", "message"), &CrowdControl::show_effects, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("hide_effects", "effect_ids", "message"), &CrowdControl::hide_effects, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("enable_effects", "effect_ids", "message"), &CrowdControl::enable_effects, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("disable_effects", "effect_ids", "message"), &CrowdControl::disable_effects, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("show_all_effects", "message"), &CrowdControl::show_all_effects, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("hide_all_effects", "message"), &CrowdControl::hide_all_effects, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("enable_all_effects", "message"), &CrowdControl::enable_all_effects, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("disable_all_effects", "message"), &CrowdControl::disable_all_effects, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("get_effect_report_args"), &CrowdControl::get_effect_report_args);
	ClassDB::bind_method(D_METHOD("stop_all_effects"), &CrowdControl::stop_all_effects);

	ClassDB::bind_method(D_METHOD("trigger_event", "event_id", "args"), &CrowdControl::trigger_event, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("update_metadata", "key", "value"), &CrowdControl::update_metadata);
	ClassDB::bind_method(D_METHOD("update_metadata_values", "values"), &CrowdControl::update_metadata_values);

	ClassDB::bind_method(D_METHOD("can_upload_custom_effects"), &CrowdControl::can_upload_custom_effects);
	ClassDB::bind_method(D_METHOD("upload_custom_effects", "mode"), &CrowdControl::upload_custom_effects, DEFVAL(CUSTOM_EFFECTS_MERGE));
	ClassDB::bind_method(D_METHOD("delete_custom_effects", "effect_ids"), &CrowdControl::delete_custom_effects);
	ClassDB::bind_method(D_METHOD("delete_all_custom_effects"), &CrowdControl::delete_all_custom_effects);
	ClassDB::bind_method(D_METHOD("__sync_effect_states"), &CrowdControl::sync_effect_states);

	BIND_ENUM_CONSTANT(CUSTOM_EFFECTS_MERGE);
	BIND_ENUM_CONSTANT(CUSTOM_EFFECTS_REPLACE_ALL);
	BIND_ENUM_CONSTANT(CUSTOM_EFFECTS_REPLACE_PARTIAL);
	ClassDB::bind_method(D_METHOD("close"), &CrowdControl::close);
	ClassDB::bind_method(D_METHOD("is_session_active"), &CrowdControl::is_session_active);
	ClassDB::bind_method(D_METHOD("is_connected_to_crowd_control"), &CrowdControl::is_connected_to_crowd_control);
	ClassDB::bind_method(D_METHOD("start_session"), &CrowdControl::start_session);
	ClassDB::bind_method(D_METHOD("stop_session"), &CrowdControl::stop_session);
	ClassDB::bind_method(D_METHOD("get_streamer_user"), &CrowdControl::get_streamer_user);
	ClassDB::bind_method(D_METHOD("__thread_process"), &CrowdControl::thread_process);
	ClassDB::bind_method(D_METHOD("__tree_process"), &CrowdControl::tree_process);
	ClassDB::bind_method(D_METHOD("__tree_physics_process"), &CrowdControl::tree_physics_process);
	ClassDB::bind_method(D_METHOD("__process", "delta"), &CrowdControl::_process);
	ClassDB::bind_method(D_METHOD("__stop_all_effects"), &CrowdControl::stop_all_effects);
	ClassDB::bind_method(D_METHOD("__cancel_request", "request_id"), &CrowdControl::cancel_request);
	ClassDB::bind_method(D_METHOD("stop_effect", "effect"), &CrowdControl::stop_effect);
	ClassDB::bind_method(D_METHOD("resume_effect", "effect"), &CrowdControl::resume_effect);
	ClassDB::bind_method(D_METHOD("pause_effect", "effect"), &CrowdControl::pause_effect);
	ClassDB::bind_method(D_METHOD("reset_effect", "effect"), &CrowdControl::reset_effect);
	ClassDB::bind_method(D_METHOD("test_effect", "effect"), &CrowdControl::test_effect);
	ClassDB::bind_method(D_METHOD("test_effect_remotely", "effect"), &CrowdControl::test_effect_remotely);
	ClassDB::bind_method(D_METHOD("get_effect", "id"), &CrowdControl::get_effect);

	ClassDB::bind_method(D_METHOD("set_staging", "enable"), &CrowdControl::set_staging);
	ClassDB::bind_method(D_METHOD("is_staging"), &CrowdControl::is_staging);

	ClassDB::bind_method(D_METHOD("set_paused", "enable"), &CrowdControl::set_paused);
	ClassDB::bind_method(D_METHOD("is_paused"), &CrowdControl::is_paused);

	ClassDB::bind_method(D_METHOD("set_auto_starting_session", "enable"), &CrowdControl::set_auto_starting_session);
	ClassDB::bind_method(D_METHOD("is_auto_starting_session"), &CrowdControl::is_auto_starting_session);

	//ClassDB::bind_method(D_METHOD("set_broadcaster_type", "type"), &CrowdControl::set_broadcaster_type);
	//ClassDB::bind_method(D_METHOD("get_broadcaster_type"), &CrowdControl::get_broadcaster_type);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "staging"), "set_staging", "is_staging");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "paused"), "set_paused", "is_paused");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_starting_session"), "set_auto_starting_session", "is_auto_starting_session");
	//ADD_PROPERTY(PropertyInfo(Variant::INT, "broadcaster_type", PROPERTY_HINT_ENUM, "Community,Affiliate,Partner"), "set_broadcaster_type", "get_broadcaster_type");

	//BIND_ENUM_CONSTANT(COMMUNITY);
	//BIND_ENUM_CONSTANT(AFFILIATE);
	//BIND_ENUM_CONSTANT(PARTNER);

	ADD_SIGNAL(MethodInfo("connecting"));
	// TODO: Should we add an error source (HTTPClient vs. WebSocket) ?
	ADD_SIGNAL(MethodInfo("connection_errored", PropertyInfo(Variant::INT, "err", PROPERTY_HINT_RESOURCE_TYPE, "Error")));
	ADD_SIGNAL(MethodInfo("connected"));
	ADD_SIGNAL(MethodInfo("disconnected"));

	ADD_SIGNAL(MethodInfo("session_started"));
	ADD_SIGNAL(MethodInfo("session_stopped"));
	ADD_SIGNAL(MethodInfo("session_start_failed", PropertyInfo(Variant::STRING, "message")));
	// The streamer paused / resumed ordering for the running session
	ADD_SIGNAL(MethodInfo("session_paused", PropertyInfo(Variant::BOOL, "paused")));

	ADD_SIGNAL(MethodInfo("logged_in"));
	ADD_SIGNAL(MethodInfo("logged_out"));
	// Emitted during login with the URL the user has to open in a browser to sign in
	ADD_SIGNAL(MethodInfo("auth_code_received", PropertyInfo(Variant::STRING, "code"), PropertyInfo(Variant::STRING, "url")));
	ADD_SIGNAL(MethodInfo("login_failed", PropertyInfo(Variant::STRING, "message")));
	// The login was extended, see extend_token()
	ADD_SIGNAL(MethodInfo("token_refreshed"));
	ADD_SIGNAL(MethodInfo("pong_received"));
	ADD_SIGNAL(MethodInfo("custom_effects_updated", PropertyInfo(Variant::BOOL, "success"), PropertyInfo(Variant::STRING, "message")));

	ADD_SIGNAL(MethodInfo("coins_exchanged", PropertyInfo(Variant::INT, "amount"), PropertyInfo(Variant::OBJECT, "exchanger", PROPERTY_HINT_RESOURCE_TYPE, "StreamUser")));
	ADD_SIGNAL(MethodInfo("message_display_requested", PropertyInfo(Variant::STRING, "message"), PropertyInfo(Variant::FLOAT, "duration"), PropertyInfo(Variant::OBJECT, "icon", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D")));

	ADD_SIGNAL(MethodInfo("effect_queued", PropertyInfo(Variant::OBJECT, "effect", PROPERTY_HINT_RESOURCE_TYPE, "CCEffectInstance")));
	ADD_SIGNAL(MethodInfo("effect_dequeued", PropertyInfo(Variant::STRING, "id"), PropertyInfo(Variant::INT, "result", PROPERTY_HINT_RESOURCE_TYPE, "EffectResult")));

	ADD_SIGNAL(MethodInfo("effect_triggered", PropertyInfo(Variant::OBJECT, "effect", PROPERTY_HINT_RESOURCE_TYPE, "CCEffectInstance")));
	ADD_SIGNAL(MethodInfo("effect_started", PropertyInfo(Variant::OBJECT, "effect", PROPERTY_HINT_RESOURCE_TYPE, "CCEffectInstanceTimed")));
	ADD_SIGNAL(MethodInfo("effect_stopped", PropertyInfo(Variant::OBJECT, "effect", PROPERTY_HINT_RESOURCE_TYPE, "CCEffectInstanceTimed")));
	ADD_SIGNAL(MethodInfo("effect_paused", PropertyInfo(Variant::OBJECT, "effect", PROPERTY_HINT_RESOURCE_TYPE, "CCEffectInstanceTimed")));
	ADD_SIGNAL(MethodInfo("effect_resumed", PropertyInfo(Variant::OBJECT, "effect", PROPERTY_HINT_RESOURCE_TYPE, "CCEffectInstanceTimed")));
	ADD_SIGNAL(MethodInfo("effect_reset", PropertyInfo(Variant::OBJECT, "effect", PROPERTY_HINT_RESOURCE_TYPE, "CCEffectInstanceTimed")));

	ADD_SIGNAL(MethodInfo("effect_requested", PropertyInfo(Variant::OBJECT, "effect", PROPERTY_HINT_RESOURCE_TYPE, "CCEffect")));
}

CrowdControl *CrowdControl::get_singleton() {
	if (singleton == nullptr) {
		memnew(CrowdControl);
	}
	return singleton;
}

Error CrowdControl::connect_to_crowd_control() {
	quitting = false;
	process_mode = GLOBAL_GET("crowd_control/common/process/process_mode");
	debug_info = GLOBAL_GET("crowd_control/debug/log_info");
	debug_warn = GLOBAL_GET("crowd_control/debug/log_warn");
	debug_error = GLOBAL_GET("crowd_control/debug/log_error");
	debug_exception = GLOBAL_GET("crowd_control/debug/log_exception");
	reconnect_retry_count = GLOBAL_GET("crowd_control/common/retry_count");
	reconnect_retry_delay = GLOBAL_GET("crowd_control/common/retry_delay");
	delay_between_effects = GLOBAL_GET("crowd_control/common/delay_between_effects");
	if (process_mode == 0) {
		if (process_thread.is_null()) {
			process_thread.instantiate();
		}
		if (!process_thread->is_alive()) {
			if (process_thread->is_started()) {
				process_thread->wait_to_finish();
			}
			process_thread->start(Callable(this, "__thread_process"));
		}
	} else {
		MainLoop *main = Engine::get_singleton()->get_main_loop();
		scene_tree = Object::cast_to<SceneTree>(main);
		ERR_FAIL_COND_V_EDMSG(scene_tree == nullptr, FAILED, "MainLoop does not extend SceneTree! Cannot connect to process signals!");
		if (process_mode == 1) {
			if (!scene_tree->is_connected("process_frame", Callable(this, "__tree_process"))) {
				scene_tree->connect("process_frame", Callable(this, "__tree_process"));
			}
		} else if (process_mode == 2) {
			if (!scene_tree->is_connected("physics_frame", Callable(this, "__tree_physics_process"))) {
				scene_tree->connect("physics_frame", Callable(this, "__tree_physics_process"));
			}
		}
	}
	// Effects are loaded here, on the main thread, so they can be looked up and hidden before the login is done
	load_effects();
#ifdef DEV_MODE
	return connection->connect(DEV);
#else
	if (staging) {
		return connection->connect(STAGING);
	}
	return connection->connect(PRODUCTION);
#endif
}

void CrowdControl::close() {
	connection->close();
	if (session_active) {
		// The connection is closed once the service confirmed that the session ended
		quit_on_session_close = true;
		stop_session();
	} else {
		quitting = true;
	}
}

void CrowdControl::login() {
	connection->login();
}

void CrowdControl::logout() {
	if (session_active) {
		stop_session();
	}
	connection->logout();
}

bool CrowdControl::is_logged_in() const {
	return connection.is_valid() && connection->has_valid_token();
}

void CrowdControl::set_paused(bool p_paused) {
	if (paused == p_paused) {
		return;
	}
	paused = p_paused;
	// Timers don't advance while paused, let the service know so viewers see the same thing
	for (KeyValue<StringName, Ref<CCEffectInstanceTimed>> kv : running_effects) {
		if (!kv.value->is_paused()) {
			respond(kv.value, paused ? TIMED_PAUSE : TIMED_RESUME);
		}
	}
}

bool CrowdControl::is_connected_to_crowd_control() const {
	if (connection.is_null()) {
		return false;
	}
	return connection->is_connected_to_crowd_control();
}

bool CrowdControl::is_session_active() const {
	return session_active;
}

void CrowdControl::start_session() {
	if (!session_active) {
		quit_on_session_close = false;
		connection->start_session();
	} else {
		log_warn("A session is already active!");
	}
}

void CrowdControl::stop_session() {
	if (session_active) {
		connection->stop_session();
	}
}

void CrowdControl::test_effect(const Ref<CCEffect> &effect) {
	HashMap<String, ParameterEntry> parameters = HashMap<String, ParameterEntry>();
	if (effect->get_effect_type() == CCEffect::PARAMETERS) {
		Ref<CCEffectParameters> effect_params = Object::cast_to<CCEffectParameters>(effect.ptr());
		for (KeyValue<String, CCEffectParameters::ParameterEntry> kvp : effect_params->get_parameter_entries_by_id()) {
			ParameterEntry entry = ParameterEntry();
			entry.name = kvp.value.name;
			entry.value = kvp.value.name;
			parameters.insert(kvp.value.name, entry);
		}
	}
	// Local tests never reach the service, so there is nobody to report their outcome to
	queue_effect(effect, User(), vformat("local-%d", (int64_t)(Time::get_singleton()->get_unix_time_from_system() * 1000)), true, parameters, 0, true, true);
}

void CrowdControl::test_effect_remotely(const Ref<CCEffect> &effect) {
	connection->client_post_authorized("/game-session/effect-request", Array(), JSONSerializer::serialize_string<RequestEffect>(RequestEffect(connection->get_game_session_id(), effect->get_id())));
}

void CrowdControl::display_message(String message, float duration) {
	call_deferred("emit_signal", "message_display_requested", message, duration, streamer_user.is_valid() ? streamer_user->get_profile_icon() : nullptr);
}

void CrowdControl::register_effect(const Ref<CCEffect> &effect) {
	effects.insert(effect->get_id(), effect);
	log_info(vformat("Registered Effect %s!", effect->get_id()));
}

Ref<CCEffect> CrowdControl::get_effect(const StringName &name) const {
	if (effects.has(name)) {
		return effects[name];
	}
	return Ref<CCEffect>();
}

void CrowdControl::thread_process() {
	double last_run = Time::get_singleton()->get_unix_time_from_system();
	while (!quitting) {
		// One tick per frame (in microseconds), at least 5 a second if the frame rate isn't known yet
		float delta_usec = 1000000.0 / MAX(Engine::get_singleton()->get_frames_per_second(), 5.0);
		OS::get_singleton()->delay_usec(delta_usec);
		float delta = Time::get_singleton()->get_unix_time_from_system() - last_run;
		_process(delta);
		last_run = Time::get_singleton()->get_unix_time_from_system();
	}
	if (process_thread.is_valid() && process_thread->is_started()) {
		connection->call_deferred("__cleanup_thread", process_thread);
	}
	_on_disconnected();
}

void CrowdControl::tree_process() {
	ERR_FAIL_NULL_EDMSG(scene_tree, "MainLoop does not extend SceneTree!");
	if (GLOBAL_GET("crowd_control/common/process/use_scene_tree_pause")) {
		bool tree_pause = scene_tree->is_paused();
		if (tree_pause && !paused) {
			set_paused(tree_pause);
		} else if (!tree_pause && paused) {
			set_paused(tree_pause);
		}
	}
	if (!quitting) {
		double delta = scene_tree->get_root()->get_process_delta_time();
		_process(delta);
	}
}

void CrowdControl::tree_physics_process() {
	if (GLOBAL_GET("crowd_control/common/process/use_scene_tree_pause")) {
		bool tree_pause = scene_tree->is_paused();
		if (tree_pause && !paused) {
			set_paused(tree_pause);
		} else if (!tree_pause && paused) {
			set_paused(tree_pause);
		}
	}
	ERR_FAIL_NULL_EDMSG(scene_tree, "MainLoop does not extend SceneTree!");
	if (!quitting) {
		double delta = scene_tree->get_root()->get_physics_process_delta_time();
		_process(delta);
	}
}

void CrowdControl::handle_pending() {
	if (effect_queue.size() == 0) {
		return;
	}
	queue_mutex->lock();
	Ref<CCEffectInstance> effect = effect_queue.pop_front();
	if (effect->get_effect()->get_effect_type() == CCEffect::EffectType::TIMED && running_effects.has(effect->get_effect_id())) {
		String effectID = effect->get_effect_id();
		if (!halted_timers.has(effectID)) {
			halted_timers.insert(effectID, TypedArray<CCEffectInstanceTimed>());
		}
		halted_timers[effectID].append(effect);
	} else if (!try_start(effect)) {
		effect_queue.push_back(effect);
	}
	queue_mutex->unlock();
}

void CrowdControl::expire_stale_requests() {
	const double deadline = Time::get_singleton()->get_unix_time_from_system() - REQUEST_TIMEOUT;
	queue_mutex->lock();
	for (int i = effect_queue.size() - 1; i >= 0; i--) {
		Ref<CCEffectInstance> instance = effect_queue[i];
		if (!instance->is_local() && instance->get_queue_time() < deadline) {
			effect_queue.remove_at(i);
			cancel_effect(instance);
		}
	}
	for (KeyValue<StringName, TypedArray<CCEffectInstanceTimed>> &kv : halted_timers) {
		for (int i = kv.value.size() - 1; i >= 0; i--) {
			Ref<CCEffectInstance> instance = kv.value[i];
			if (!instance->is_local() && instance->get_queue_time() < deadline) {
				kv.value.remove_at(i);
				cancel_effect(instance);
			}
		}
	}
	queue_mutex->unlock();
}

void CrowdControl::stop_all_effects() {
	queue_mutex->lock();
	for (int i = 0; i < effect_queue.size(); i++) {
		cancel_effect(effect_queue[i]);
	}
	effect_queue.clear();
	for (KeyValue<StringName, TypedArray<CCEffectInstanceTimed>> &kv : halted_timers) {
		for (int i = 0; i < kv.value.size(); i++) {
			cancel_effect(kv.value[i]);
		}
	}
	halted_timers.clear();
	queue_mutex->unlock();
	// Ending an effect removes it from the map, so take them one at a time
	while (running_effects.size() != 0) {
		Ref<CCEffectInstanceTimed> instance = running_effects.begin()->value;
		end_effect(instance, true);
	}
}

void CrowdControl::cancel_request(const String &p_request_id) {
	bool found = false;
	queue_mutex->lock();
	for (int i = effect_queue.size() - 1; i >= 0 && !found; i--) {
		Ref<CCEffectInstance> instance = effect_queue[i];
		if (instance->get_id() == p_request_id) {
			effect_queue.remove_at(i);
			dequeue_effect(instance, CCEffect::EffectResult::FAILURE);
			found = true;
		}
	}
	for (KeyValue<StringName, TypedArray<CCEffectInstanceTimed>> &kv : halted_timers) {
		for (int i = kv.value.size() - 1; i >= 0 && !found; i--) {
			Ref<CCEffectInstance> instance = kv.value[i];
			if (instance->get_id() == p_request_id) {
				kv.value.remove_at(i);
				dequeue_effect(instance, CCEffect::EffectResult::FAILURE);
				found = true;
			}
		}
	}
	queue_mutex->unlock();
	if (found) {
		return;
	}
	for (KeyValue<StringName, Ref<CCEffectInstanceTimed>> kv : running_effects) {
		if (kv.value->get_id() == p_request_id) {
			// The service already knows, it is the one that ended the effect
			end_effect(kv.value, false);
			break; // the map was edited, it's no longer safe to continue
		}
	}
}

void CrowdControl::respond(const Ref<CCEffectInstance> &instance, EffectInstanceStatus status, bool after_success, const String &message) {
	if (instance.is_null() || instance->is_local()) {
		return;
	}
	PackedStringArray statuses;
	if (after_success) {
		statuses.append(effect_instance_status_names[SUCCESS]);
	}
	statuses.append(effect_instance_status_names[status]);
	int64_t time_remaining = -1;
	Ref<CCEffectInstanceTimed> timed = Object::cast_to<CCEffectInstanceTimed>(instance.ptr());
	if (timed.is_valid()) {
		// seconds -> milliseconds
		time_remaining = (int64_t)(timed->get_time_left() * 1000.0);
	}
	connection->send_effect_response(instance->get_id(), statuses, time_remaining, message);
}

bool CrowdControl::has_running_conflict(const Ref<CCEffect> &effect) const {
	PackedStringArray conflicts = effect->get_conflicts();
	for (int i = 0; i < conflicts.size(); i++) {
		if (running_effects.has(conflicts[i])) {
			return true;
		}
	}
	return false;
}

bool CrowdControl::try_start(const Ref<CCEffectInstance> &instance) {
	if (time_until_next_effect > 0) {
		return false;
	}
	double time = Time::get_singleton()->get_unix_time_from_system();
	if (instance->get_effect()->get_delay_until() > time) {
		return false;
	}
	return instance->get_start_time() <= time && start_effect(instance);
}

bool CrowdControl::stop_effect(const Ref<CCEffectInstanceTimed> &instance) {
	return end_effect(instance, true);
}

bool CrowdControl::end_effect(const Ref<CCEffectInstanceTimed> &p_instance, bool notify) {
	// Keep the instance alive, the caller may have handed us the reference stored in the map
	Ref<CCEffectInstanceTimed> instance = p_instance;
	ERR_FAIL_COND_V(instance.is_null(), false);
	String effectID = instance->get_effect_id();
	instance->get_effect_timed()->stop(instance, true);
	call_deferred("emit_signal", "effect_stopped", instance);
	if (notify) {
		respond(instance, TIMED_END);
	}
	running_effects.erase(effectID);
	if (halted_timers.has(effectID) && halted_timers[effectID].size() != 0) {
		start_effect(halted_timers[effectID].pop_front());
	}
	return true;
}

bool CrowdControl::start_effect(const Ref<CCEffectInstance> &instance) {
	bool dequeue = true;
	Ref<CCEffect> effect = instance->get_effect();
	CCEffect::EffectResult result;
	Ref<CCEffectInstanceTimed> timed = Object::cast_to<CCEffectInstanceTimed>(instance.ptr());
	if (effect->get_effect_type() == CCEffect::EffectType::TIMED) {
		// This might even need a crash, not sure how we savely recover from this
		ERR_FAIL_NULL_V_EDMSG(timed, true, "Timed effect found on non-timed effect instance!");
		if (!timed->is_active()) {
			result = CCEffect::EffectResult::RETRY;
			//Retry
			retry_start_effect(effect, instance, dequeue, result);
			finish_start_effect(effect, instance, dequeue);
			return dequeue;
		}
	}

	if (has_running_conflict(effect)) {
		// Rejected right away, waiting it out is the viewer's call
		dequeue_effect(instance, CCEffect::EffectResult::FAILURE);
		respond(instance, FAIL_TEMPORARILY, false, "A conflicting effect is running.");
		return dequeue;
	}

	result = effect->can_run() ? effect->trigger(instance) : CCEffect::EffectResult::RETRY;
	switch (result) {
		default:
			// Don't leave the viewer waiting for an answer that would never come
			log_error(vformat("Effect '%s' returned an unsupported EffectResult (%d), the request was failed!", effect->get_id(), (int64_t)result));
			dequeue_effect(instance, CCEffect::EffectResult::FAILURE);
			respond(instance, FAIL_TEMPORARILY);
			break;
		case CCEffect::EffectResult::FAILURE:
			dequeue_effect(instance, result);
			respond(instance, FAIL_TEMPORARILY);
			break;
		case CCEffect::EffectResult::UNAVAILABLE:
			dequeue_effect(instance, result);
			respond(instance, FAIL_PERMANENTLY);
			break;
		case CCEffect::EffectResult::SUCCESS:
			time_until_next_effect = delay_between_effects;
			call_deferred("emit_signal", "effect_triggered", instance);
			dequeue_effect(instance, CCEffect::EffectResult::SUCCESS);
			respond(instance, SUCCESS);
			break;
		case CCEffect::EffectResult::RUNNING:
			time_until_next_effect = delay_between_effects;
			running_effects.insert(instance->get_effect_id(), timed);
			call_deferred("emit_signal", "effect_started", timed);
			dequeue_effect(instance, CCEffect::EffectResult::SUCCESS);
			// A timed effect is acknowledged with `success` followed by `timedBegin` in a single call
			respond(instance, TIMED_BEGIN, true);
			break;
		case CCEffect::EffectResult::RETRY:
			// Retry
			retry_start_effect(effect, instance, dequeue, result);
			finish_start_effect(effect, instance, dequeue);
			break;
	}

	return dequeue;
}

void CrowdControl::retry_start_effect(const Ref<CCEffect> &effect, const Ref<CCEffectInstance> &instance, bool &dequeue, CCEffect::EffectResult &result) {
	instance->set_retry_count(instance->get_retry_count() + 1);
	if (instance->get_retry_count() > effect->get_max_retries()) {
		result = CCEffect::EffectResult::FAILURE;
		cancel_effect(instance);
	} else {
		instance->set_start_time(effect->get_retry_delay() + Time::get_singleton()->get_unix_time_from_system());
		dequeue = false;
	}
}

void CrowdControl::finish_start_effect(const Ref<CCEffect> &effect, const Ref<CCEffectInstance> &instance, bool &dequeue) {
	if (dequeue) {
		effect->set_delay_until(effect->get_pending_delay() + Time::get_singleton()->get_unix_time_from_system());
	} else {
		effect->set_delay_until(instance->get_start_time());
	}
}

void CrowdControl::resume_effect(const Ref<CCEffectTimed> &effect) {
	if (running_effects.has(effect->get_id())) {
		Ref<CCEffectInstanceTimed> timed = running_effects[effect->get_id()];
		effect->resume(timed);
		call_deferred("emit_signal", "effect_resumed", timed);
		respond(timed, TIMED_RESUME);
	}
}

void CrowdControl::pause_effect(const Ref<CCEffectTimed> &effect) {
	if (running_effects.has(effect->get_id())) {
		Ref<CCEffectInstanceTimed> timed = running_effects[effect->get_id()];
		effect->pause(timed);
		call_deferred("emit_signal", "effect_paused", timed);
		respond(timed, TIMED_PAUSE);
	}
}

void CrowdControl::reset_effect(const Ref<CCEffectTimed> &effect) {
	if (running_effects.has(effect->get_id())) {
		Ref<CCEffectInstanceTimed> timed = running_effects[effect->get_id()];
		effect->reset(timed);
		call_deferred("emit_signal", "effect_reset", timed);
		// There is no dedicated status for this, a resume carrying the new remaining time keeps timers in sync
		if (!timed->is_paused() && !paused) {
			respond(timed, TIMED_RESUME);
		}
	}
}

void CrowdControl::load_effects() {
	if (effect_entries.is_valid()) {
		return;
	}
	const String path = GLOBAL_GET("crowd_control/common/effects");
	Ref<CCEffectEntries> entries;
	if (ResourceLoader::get_singleton()->exists(path)) {
		entries = ResourceLoader::get_singleton()->load(path);
	}
	if (entries.is_null()) {
		log_error(vformat("No CCEffectEntries resource found at '%s', no effects are available! Set 'crowd_control/common/effects' in the project settings.", path));
		return;
	}
	effect_entries = entries;
	TypedArray<CCEffect> list = effect_entries->get_effects();
	for (int i = 0; i < list.size(); i++) {
		Ref<CCEffect> effect = list[i];
		if (effect.is_null()) {
			log_error(vformat("Entry %d of '%s' is empty!", i, path));
			continue;
		}
		if (String(effect->get_id()).is_empty()) {
			log_error(vformat("Effect '%s' has no ID and can't be used!", effect->get_display_name()));
			continue;
		}
		if (effects.has(effect->get_id())) {
			log_error(vformat("Duplicate effect ID '%s', only the first one is used!", effect->get_id()));
			continue;
		}
		register_effect(effect);
	}
}

void CrowdControl::_on_connected() {
	call_deferred("emit_signal", "connected");
	// Continues with the stored login if there is one, otherwise `logged_out` asks the game to call login()
	connection->on_socket_connected();
}

void CrowdControl::_on_disconnected() {
	call_deferred("emit_signal", "disconnected");
}

void CrowdControl::_on_effect_requested(const Ref<CCEffect> &effect) {
	call_deferred("emit_signal", "effect_requested", effect);
}

void CrowdControl::_on_session_started() {
	CrowdControl::singleton->display_message("Started the Crowd Control session!");
	call_deferred("emit_signal", "session_started");
	CrowdControl::singleton->session_active = true;
}

void CrowdControl::queue_effect(const Ref<CCEffect> &effect, User requester, String requestID, bool is_anonymous, HashMap<String, ParameterEntry> parameters, double duration, bool is_test, bool is_local) {
	Ref<StreamUser> display_user;
	if (!is_anonymous) {
		display_user = get_stream_user(requester);
	} else {
		display_user = anonymous_user;
	}
	if (effect->get_effect_type() == CCEffect::EffectType::TIMED) {
		create_effect_instance<CCEffectInstanceTimed>(display_user, effect, requester, requestID, parameters, duration, is_test, is_local);
	} else if (effect->get_effect_type() == CCEffect::EffectType::BID_WAR) {
		create_effect_instance<CCEffectInstanceBidWar>(display_user, effect, requester, requestID, parameters, duration, is_test, is_local);
	} else if (effect->get_effect_type() == CCEffect::EffectType::PARAMETERS) {
		create_effect_instance<CCEffectInstanceParameters>(display_user, effect, requester, requestID, parameters, duration, is_test, is_local);
	} else if (effect->get_effect_type() == CCEffect::EffectType::NORMAL) {
		create_effect_instance<CCEffectInstance>(display_user, effect, requester, requestID, parameters, duration, is_test, is_local);
	}
}

void CrowdControl::dequeue_effect(const Ref<CCEffectInstance> &instance, CCEffect::EffectResult result) {
	call_deferred("emit_signal", "effect_dequeued", instance->get_effect_id(), result);
}

void CrowdControl::cancel_effect(const Ref<CCEffectInstance> &instance) {
	respond(instance, FAIL_TEMPORARILY);
	dequeue_effect(instance, CCEffect::EffectResult::FAILURE);
}

Ref<StreamUser> CrowdControl::get_stream_user(const User &user) {
	Ref<StreamUser> out;
	String user_name = user.name;
	if (!stream_users.has(user_name)) {
		out = StreamUser::make(user_name, user.image_url, user.roles);
		stream_users[user_name] = out;
		Ref<Thread> thread;
		thread.instantiate();
		thread->start(Callable(connection.ptr(), "__download_profile_image").bindv(Array::make(thread, out)));
	} else {
		out = stream_users[user_name];
	}
	return out;
}

template <class T>
void CrowdControl::create_effect_instance(const Ref<StreamUser> &user, const Ref<CCEffect> &effect, User requester, String requestID, HashMap<String, ParameterEntry> parameters, double duration, bool is_test, bool is_local) {
	Ref<T> out;
	out.instantiate();
	out->set_id(requestID);
	out->set_user(user);
	out->set_effect(effect);
	out->set_test(is_test);
	out->set_local(is_local);
	out->set_retry_count(0);
	out->set_start_time(Time::get_singleton()->get_unix_time_from_system());
	out->set_queue_time(out->get_start_time());
	if (duration > 0) {
		// Streamers can change the duration in their menu, the request has the final say
		Ref<CCEffectInstanceTimed> timed = Object::cast_to<CCEffectInstanceTimed>(out.ptr());
		if (timed.is_valid()) {
			timed->set_duration(duration);
		}
	}

	String effectID = effect->get_id();
	if (effect->get_effect_type() == CCEffect::EffectType::PARAMETERS) {
		if (parameters.size() == 0) {
			cancel_effect(out);
			return;
		}
		Array arr = Array();
		for (KeyValue<String, ParameterEntry> kv : parameters) {
			arr.append(kv.key);
		}
		log_info(arr);
		if (parameters.has("quantity")) {
			// The service knows a single quantity per effect, make it available under the ID the effect gave it
			Ref<CCEffectParameters> effect_params = Object::cast_to<CCEffectParameters>(effect.ptr());
			for (int i = 0; i < effect_params->get_parameter_count(); i++) {
				if (effect_params->get_parameter_kind(i) == CCEffectParameters::ParameterEntryKind::RANGE) {
					String range_id = effect_params->get_parameter_id(i);
					if (!parameters.has(range_id)) {
						parameters.insert(range_id, parameters["quantity"]);
					}
					break;
				}
			}
		}
		((Ref<CCEffectInstanceParameters>)out)->assign_parameters(parameters);
	} else if (effect->get_effect_type() == CCEffect::EffectType::BID_WAR) {
		Ref<CCEffectBidWar> bid_war = Object::cast_to<CCEffectBidWar>(effect.ptr());
		Ref<CCEffectInstanceBidWar> instance = Object::cast_to<CCEffectInstanceBidWar>(out.ptr());
		instance->set_bid_id(parameters[vformat("%s_options", bid_war->get_id())].value);
	}
	queue_mutex->lock();
	effect_queue.push_back(out);
	queue_mutex->unlock();
	call_deferred("emit_signal", "effect_queued", out);
}

/*template <class... Args>
void CrowdControl::log_info(const Variant &arg1, const Args &...args) {
	if (debug_info) {
		UtilityFunctions::print_rich(LOG_PREFIX, arg1, args...);
	}
}

template <class... Args>
void CrowdControl::log_warn(const Variant &arg1, const Args &...args) {
	if (debug_warn) {
		UtilityFunctions::push_warning(LOG_PREFIX, arg1, args...);
	}
}

template <class... Args>
void CrowdControl::log_error(const Variant &arg1, const Args &...args) {
	if (debug_error) {
		UtilityFunctions::push_error(LOG_PREFIX, arg1, args...);
	}
}

template <class... Args>
void CrowdControl::log_exception(const Variant &arg1, const Args &...args) {
	if (debug_exception) {
		UtilityFunctions::push_error(LOG_PREFIX, arg1, args...);
	}
}*/

void CrowdControl::log_info(const Variant &arg1) {
	if (debug_info) {
		UtilityFunctions::print_rich(LOG_PREFIX, arg1);
	}
}

void CrowdControl::log_warn(const Variant &arg1) {
	if (debug_warn) {
		UtilityFunctions::push_warning(LOG_PREFIX, arg1);
	}
}

void CrowdControl::log_error(const Variant &arg1) {
	if (debug_error) {
		UtilityFunctions::push_error(LOG_PREFIX, arg1);
	}
}

void CrowdControl::log_exception(const Variant &arg1) {
	if (debug_exception) {
		UtilityFunctions::push_error(LOG_PREFIX, arg1);
	}
}

void CrowdControl::_process(float delta) {
	// Housekeeping that has to keep going while the game is paused
	connection->poll_reconnect();
	expire_stale_requests();
	if (paused) {
		return;
	}
	time_until_next_effect -= delta;
	handle_pending();
	for (KeyValue<StringName, Ref<CCEffectInstanceTimed>> kv : running_effects) {
		if (!kv.value->is_paused()) {
			kv.value->set_time_left(kv.value->get_time_left() - delta);
			if (kv.value->get_time_left() < 0) {
				end_effect(kv.value, true);
				break; // We may be editing the map here, so it's no longer safe to continue.
			}
		}
	}
	if (connection->is_connected_to_crowd_control()) {
		if (!streamer_user.is_null() && !stream_users.has(streamer_user->get_display_name())) {
			stream_users[streamer_user->get_display_name()] = streamer_user;
			Ref<Thread> thread;
			thread.instantiate();
			thread->start(Callable(connection.ptr(), "__download_profile_image").bindv(Array::make(thread, streamer_user)));
		}
	}
}

void CrowdControl::update_effect_status(const Ref<CCEffect> &p_effect, const String &command) {
	// Outside of a session there is no menu to update, the state is sent when the next one starts
	if (!session_active || p_effect.is_null() || !effects.has(p_effect->get_id())) {
		return;
	}
	PackedStringArray ids;
	ids.append(p_effect->get_id());
	connection->send_effect_report(ids, command);
}

PackedStringArray CrowdControl::get_all_effect_ids() const {
	PackedStringArray ids;
	for (KeyValue<StringName, Ref<CCEffect>> kv : effects) {
		ids.append(kv.key);
	}
	return ids;
}

Array CrowdControl::get_effect_report_args() const {
	PackedStringArray hidden;
	PackedStringArray unavailable;
	for (KeyValue<StringName, Ref<CCEffect>> kv : effects) {
		if (!kv.value->is_visible()) {
			hidden.append(kv.key);
		}
		if (!kv.value->is_sellable()) {
			unavailable.append(kv.key);
		}
	}
	// Visibility and availability are tracked separately, so one entry each
	Array out;
	if (!hidden.is_empty()) {
		out.append(EffectReportArgs(CCEffect::effect_menu_status_names[CCEffect::MENU_HIDDEN], hidden).serialize());
	}
	if (!unavailable.is_empty()) {
		out.append(EffectReportArgs(CCEffect::effect_menu_status_names[CCEffect::MENU_UNAVAILABLE], unavailable).serialize());
	}
	return out;
}

void CrowdControl::sync_effect_states() {
	Array args = get_effect_report_args();
	if (session_active && !args.is_empty()) {
		connection->socket_send_rpc("effectReport", args);
	}
}

bool CrowdControl::report_effects(const PackedStringArray &p_ids, CCEffect::EffectMenuStatus p_status, const String &p_message) {
	PackedStringArray known;
	for (int i = 0; i < p_ids.size(); i++) {
		if (!effects.has(p_ids[i])) {
			log_warn(vformat("Unknown effect ID '%s'!", p_ids[i]));
			continue;
		}
		Ref<CCEffect> effect = effects[p_ids[i]];
		// Keep the resources in sync without having each of them send its own report
		switch (p_status) {
			case CCEffect::MENU_VISIBLE:
				effect->set_menu_state_silent(true, effect->is_sellable());
				break;
			case CCEffect::MENU_HIDDEN:
				effect->set_menu_state_silent(false, effect->is_sellable());
				break;
			case CCEffect::MENU_AVAILABLE:
				effect->set_menu_state_silent(effect->is_visible(), true);
				break;
			case CCEffect::MENU_UNAVAILABLE:
				effect->set_menu_state_silent(effect->is_visible(), false);
				break;
		}
		known.append(p_ids[i]);
	}
	if (known.is_empty()) {
		return false;
	}
	if (!session_active) {
		// Remembered, sent along when the next session starts
		return true;
	}
	return connection->send_effect_report(known, CCEffect::effect_menu_status_names[p_status], "effect", p_message) == OK;
}

bool CrowdControl::show_effects(const PackedStringArray &p_ids, const String &p_message) {
	return report_effects(p_ids, CCEffect::MENU_VISIBLE, p_message);
}

bool CrowdControl::hide_effects(const PackedStringArray &p_ids, const String &p_message) {
	return report_effects(p_ids, CCEffect::MENU_HIDDEN, p_message);
}

bool CrowdControl::enable_effects(const PackedStringArray &p_ids, const String &p_message) {
	return report_effects(p_ids, CCEffect::MENU_AVAILABLE, p_message);
}

bool CrowdControl::disable_effects(const PackedStringArray &p_ids, const String &p_message) {
	return report_effects(p_ids, CCEffect::MENU_UNAVAILABLE, p_message);
}

bool CrowdControl::show_all_effects(const String &p_message) {
	return report_effects(get_all_effect_ids(), CCEffect::MENU_VISIBLE, p_message);
}

bool CrowdControl::hide_all_effects(const String &p_message) {
	return report_effects(get_all_effect_ids(), CCEffect::MENU_HIDDEN, p_message);
}

bool CrowdControl::enable_all_effects(const String &p_message) {
	return report_effects(get_all_effect_ids(), CCEffect::MENU_AVAILABLE, p_message);
}

bool CrowdControl::disable_all_effects(const String &p_message) {
	return report_effects(get_all_effect_ids(), CCEffect::MENU_UNAVAILABLE, p_message);
}

bool CrowdControl::trigger_event(const String &p_event_id, const Dictionary &p_args) {
	ERR_FAIL_COND_V_EDMSG(p_event_id.is_empty(), false, "An event needs an ID!");
	Dictionary event;
	event["id"] = p_event_id;
	if (!p_args.is_empty()) {
		event["args"] = p_args;
	}
	return connection->socket_send_rpc("gameEvent", Array::make(event)) == OK;
}

bool CrowdControl::update_metadata(const String &p_key, const Variant &p_value) {
	Dictionary values;
	values[p_key] = p_value;
	return update_metadata_values(values);
}

bool CrowdControl::update_metadata_values(const Dictionary &p_values) {
	if (p_values.is_empty()) {
		return false;
	}
	return connection->socket_send_rpc("packMetadataChanged", Array::make(connection->get_game_pack_id(), p_values)) == OK;
}

bool CrowdControl::can_upload_custom_effects() const {
	return connection.is_valid() && connection->has_scope("custom-effects:write");
}

bool CrowdControl::upload_custom_effects(CustomEffectsMode p_mode) {
	if (!can_upload_custom_effects()) {
		log_error("Uploading custom effects needs a login with the 'custom-effects:write' scope!");
		return false;
	}
	Array all_effects;
	for (KeyValue<StringName, Ref<CCEffect>> kv : effects) {
		all_effects.append(kv.value);
	}
	Dictionary definitions = Manifest::effect_definitions(all_effects, true);
	if (definitions.is_empty()) {
		log_warn("No effects are flagged as custom, nothing to upload.");
		return false;
	}
	return connection->upload_custom_effects(definitions, p_mode);
}

bool CrowdControl::delete_custom_effects(const PackedStringArray &p_ids) {
	if (p_ids.is_empty()) {
		// An empty list would delete everything, that has its own method
		return false;
	}
	if (!can_upload_custom_effects()) {
		log_error("Deleting custom effects needs a login with the 'custom-effects:write' scope!");
		return false;
	}
	return connection->delete_custom_effects(p_ids);
}

bool CrowdControl::delete_all_custom_effects() {
	if (!can_upload_custom_effects()) {
		log_error("Deleting custom effects needs a login with the 'custom-effects:write' scope!");
		return false;
	}
	return connection->delete_custom_effects(PackedStringArray());
}

bool CrowdControl::extend_token() {
	return connection->extend_token();
}

String CrowdControl::get_interact_link() const {
	return connection->get_interact_link();
}

void CrowdControl::launch_interact_link() const {
	String link = connection->get_interact_link();
	if (!link.is_empty()) {
		OS::get_singleton()->shell_open(link);
	}
}

void CrowdControl::ping() {
	connection->ping();
}

void CrowdControl::post_effect_update(const String &p_data) {
	if (!streamer_user.is_null()) {
		connection->client_post_authorized("/menu/effects", Array(), p_data);
	}
}

CrowdControl::CrowdControl() {
	ERR_FAIL_COND(singleton != nullptr);
	singleton = this;

	connection.instantiate();

	crowd_user = StreamUser::make("The Crowd", crowd_user_icon);
	anonymous_user = StreamUser::make("Anonymous", temp_user_icon);

	stream_users[crowd_user->get_name()] = crowd_user;
	stream_users[anonymous_user->get_name()] = anonymous_user;
	queue_mutex.instantiate();
}

CrowdControl::~CrowdControl() {
	if (connection->is_connected_to_crowd_control()) {
		close();
	}
	ERR_FAIL_COND(singleton != this);
}