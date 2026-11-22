#include "cc_network_manager.hpp"

#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/crypto.hpp>
#include <godot_cpp/classes/hashing_context.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/marshalls.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "classes/cc_effect_instance.hpp"
#include "classes/crowd_control.hpp"
#include "json/coin_exchange.hpp"
#include "json/effect_report.hpp"
#include "json/effect_response.hpp"
#include "json/game_session.hpp"
#include "json/user_info.hpp"
#include "util/project_settings.hpp"

namespace {
const char *PUBSUB_HOST = "pubsub.crowdcontrol.live";
const char *OPENAPI_HOST = "openapi.crowdcontrol.live";
const char *USER_AGENT = "User-Agent: GodotCrowdControl/2.0 (Godot)";
const char *TOKEN_STORE_PATH = "user://crowd_control.cfg";
const char *TOKEN_STORE_SECTION = "auth";

// Tags used to route HTTP responses back to whoever issued the request
const char *TAG_TOKEN = "token";
const char *TAG_PROFILE = "profile";
const char *TAG_SESSION_START = "session_start";
const char *TAG_SESSION_STOP = "session_stop";
const char *TAG_EXTEND = "extend";
const char *TAG_CUSTOM_EFFECTS = "custom_effects";

const char *custom_effects_mode_names[] = {
	"merge",
	"replace-all",
	"replace-partial",
};
} // namespace

double CCNetworkManager::now() {
	return Time::get_singleton()->get_unix_time_from_system();
}

String CCNetworkManager::generate_uuid() {
	Ref<Crypto> crypto;
	crypto.instantiate();
	PackedByteArray bytes = crypto->generate_random_bytes(16);
	ERR_FAIL_COND_V(bytes.size() != 16, String());
	bytes[6] = (bytes[6] & 0x0f) | 0x40; // version 4
	bytes[8] = (bytes[8] & 0x3f) | 0x80; // RFC 4122 variant
	String hex = bytes.hex_encode();
	return vformat("%s-%s-%s-%s-%s", hex.substr(0, 8), hex.substr(8, 4), hex.substr(12, 4), hex.substr(16, 4), hex.substr(20, 12));
}

String CCNetworkManager::base64_url_encode(const PackedByteArray &data) {
	return Marshalls::get_singleton()->raw_to_base64(data).replace("+", "-").replace("/", "_").replace("=", "");
}

Dictionary CCNetworkManager::decode_jwt_payload(const String &p_token) {
	PackedStringArray parts = p_token.split(".");
	if (parts.size() < 2) {
		return Dictionary();
	}
	String payload = parts[1].replace("-", "+").replace("_", "/");
	while (payload.length() % 4 != 0) {
		payload += "=";
	}
	PackedByteArray raw = Marshalls::get_singleton()->base64_to_raw(payload);
	if (raw.is_empty()) {
		return Dictionary();
	}
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(raw.get_string_from_utf8()) != OK) {
		return Dictionary();
	}
	Variant data = json->get_data();
	if (data.get_type() != Variant::DICTIONARY) {
		return Dictionary();
	}
	return data;
}

bool CCNetworkManager::is_valid_game_version(const String &p_version) {
	// Mirrors the service: ^\d+(\.\d+){0,3}([-+_ ]?[0-9A-Za-z.]+)?$ and 32 characters at most
	if (p_version.is_empty() || p_version.length() > 32) {
		return false;
	}
	int i = 0;
	int groups = 0;
	while (true) {
		const int start = i;
		while (i < p_version.length() && p_version[i] >= '0' && p_version[i] <= '9') {
			i++;
		}
		if (i == start) {
			// A dot that isn't followed by digits belongs to the label (if anything)
			if (groups == 0) {
				return false;
			}
			i--;
			break;
		}
		groups++;
		if (groups == 4 || i >= p_version.length() || p_version[i] != '.') {
			break;
		}
		i++;
	}
	if (i >= p_version.length()) {
		return true;
	}
	const char32_t separator = p_version[i];
	if (separator == '-' || separator == '+' || separator == '_' || separator == ' ') {
		i++;
	}
	if (i >= p_version.length()) {
		return false;
	}
	for (; i < p_version.length(); i++) {
		const char32_t c = p_version[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.')) {
			return false;
		}
	}
	return true;
}

bool CCNetworkManager::is_connected_to_crowd_control() const {
	if (socket.is_null()) {
		return false;
	}
	return socket->get_ready_state() == WebSocketPeer::STATE_OPEN;
}

bool CCNetworkManager::has_valid_token() const {
	return !token.is_empty() && !cc_uid.is_empty() && token_expiry - TOKEN_EXPIRY_MARGIN > now();
}

Error CCNetworkManager::connect(TargetServer target) {
	ERR_FAIL_COND_V_EDMSG(socket->get_ready_state() != socket->WebSocketPeer::STATE_CLOSED, ERR_ALREADY_IN_USE, "Already connected to a server!");
	if (target != target_server) {
		// Tokens are only valid for the server that issued them
		token = "";
		cc_uid = "";
		token_expiry = 0;
	}
	target_server = target;
	app_id = GLOBAL_GET("crowd_control/common/app_id");
	game_pack_id = String(GLOBAL_GET("crowd_control/common/game_id")).strip_edges();
	game_version = String(GLOBAL_GET("crowd_control/common/game_version")).strip_edges();
	if (game_version.is_empty()) {
		game_version = String(GLOBAL_GET("application/config/version")).strip_edges();
	}
	if (!is_valid_game_version(game_version)) {
		if (!game_version.is_empty()) {
			CrowdControl::singleton->log_warn(vformat("'%s' is not a version Crowd Control understands (use something like 1.0, 1.2.3 or 1.2.3-beta), it is not sent.", game_version));
		}
		game_version = "";
	}
	auto_open_auth_url = GLOBAL_GET("crowd_control/common/auth/auto_open_url");
	persist_token = GLOBAL_GET("crowd_control/common/auth/persist_token");
	pubsub_url_override = String(GLOBAL_GET("crowd_control/debug/pubsub_url")).strip_edges();
	api_url_override = String(GLOBAL_GET("crowd_control/debug/api_url")).strip_edges();
	reconnect_time = 0;
	if (connect_thread.is_null()) {
		connect_thread.instantiate();
	}
	if (connect_thread->is_started()) {
		connect_thread->wait_to_finish();
	}
	connect_thread->start(Callable(this, "__socket_connect"));
	CrowdControl::singleton->call_deferred("emit_signal", "connecting");
	return OK;
}

void CCNetworkManager::close() {
	// The socket is closed by its poll loop once CrowdControl::quitting is set,
	// all that is left to do here is to make sure we don't try to come back.
	reconnect_time = 0;
	current_reconnect_retry_count = 0;
	auth_code = "";
	code_verifier = "";
}

void CCNetworkManager::poll_reconnect() {
	if (reconnect_time > 0 && reconnect_time <= now() && !is_connected_to_crowd_control()) {
		reconnect_time = 0;
		connect(target_server);
	}
}

void CCNetworkManager::on_socket_connected() {
	if (token.is_empty() && persist_token) {
		load_stored_token();
		if (has_valid_token() && !token_scopes.has("token:extend") && token_expiry - now() < STORED_TOKEN_MIN_REMAINING) {
			// It would run out mid session with no way to renew it, better to ask for a new login right away
			CrowdControl::singleton->log_info("The stored Crowd Control login is about to expire, a new login is needed.");
			clear_token();
		}
	}
	if (has_valid_token()) {
		on_authenticated();
	} else {
		if (!token.is_empty()) {
			CrowdControl::singleton->log_info("The stored Crowd Control login has expired.");
			clear_token();
		}
		CrowdControl::singleton->call_deferred("emit_signal", "logged_out");
	}
}

void CCNetworkManager::login() {
	if (!is_connected_to_crowd_control()) {
		CrowdControl::singleton->log_error("Cannot log in: the socket connection hasn't been established yet!");
		CrowdControl::singleton->call_deferred("emit_signal", "login_failed", "Not connected to Crowd Control.");
		return;
	}
	if (app_id.is_empty()) {
		CrowdControl::singleton->log_error("Cannot log in: no application ID set! Set 'crowd_control/common/app_id' in the project settings.");
		CrowdControl::singleton->call_deferred("emit_signal", "login_failed", "No Crowd Control application ID configured.");
		return;
	}

	// PKCE (RFC 7636): the verifier never leaves this process until the code is exchanged,
	// the service only gets to see its SHA-256 hash up front.
	Ref<Crypto> crypto;
	crypto.instantiate();
	code_verifier = base64_url_encode(crypto->generate_random_bytes(32));
	Ref<HashingContext> hashing;
	hashing.instantiate();
	hashing->start(HashingContext::HASH_SHA256);
	hashing->update(code_verifier.to_ascii_buffer());
	String code_challenge = base64_url_encode(hashing->finish());
	auth_code = "";

	Dictionary request_data;
	request_data["appID"] = app_id;
	request_data["codeChallenge"] = code_challenge;
	request_data["codeChallengeMethod"] = "S256";
	// Without explicit scopes the token gets every scope granted to the application
	PackedStringArray scopes = GLOBAL_GET("crowd_control/common/auth/scopes");
	if (!scopes.is_empty()) {
		Array scope_list;
		for (int i = 0; i < scopes.size(); i++) {
			scope_list.append(scopes[i]);
		}
		request_data["scopes"] = scope_list;
	}
	Error err = socket_send_action("generate-auth-code", request_data);
	if (err != OK) {
		CrowdControl::singleton->call_deferred("emit_signal", "login_failed", "Failed to request an auth code.");
	}
}

void CCNetworkManager::logout() {
	if (!token.is_empty()) {
		// Best effort, the token is forgotten locally either way
		client_request_authorized(HTTPClient::METHOD_DELETE, "/auth/token", Array());
	}
	clear_token();
	auth_code = "";
	code_verifier = "";
	CrowdControl::singleton->call_deferred("emit_signal", "logged_out");
}

void CCNetworkManager::subscribe() {
	ERR_FAIL_COND_EDMSG(cc_uid.is_empty(), "Cannot subscribe without being logged in!");
	Dictionary request_data;
	request_data["token"] = token;
	// pub carries everything needed to run effects. prv adds the sensitive bits (prices, coin amounts) and
	// needs the `pubsub.private:read` scope, the service simply reports it as failed if we may not have it.
	request_data["topics"] = Array::make(vformat("pub/%s", cc_uid), vformat("prv/%s", cc_uid));
	socket_send_action("subscribe", request_data);
}

void CCNetworkManager::request_token() {
	ERR_FAIL_COND_EDMSG(auth_code.is_empty() || code_verifier.is_empty(), "No login attempt in progress!");
	Dictionary body;
	body["appID"] = app_id;
	body["code"] = auth_code;
	body["codeVerifier"] = code_verifier;
	client_post("/auth/application/token", Array(), JSON::stringify(body), TAG_TOKEN);
}

bool CCNetworkManager::set_token(const String &p_token) {
	Dictionary payload = decode_jwt_payload(p_token);
	String uid = payload.get("ccUID", "");
	double expiry = payload.get("exp", 0.0);
	if (expiry > 1e11) {
		// milliseconds
		expiry /= 1000.0;
	}
	if (uid.is_empty() || expiry - TOKEN_EXPIRY_MARGIN <= now()) {
		return false;
	}
	token = p_token;
	cc_uid = uid;
	token_expiry = expiry;
	token_jti = payload.get("jti", "");
	profile_type = payload.get("profileType", "");
	origin_id = payload.get("originID", "");
	token_scopes.clear();
	if (((Variant)payload.get("app", Variant())).get_type() == Variant::DICTIONARY) {
		Variant scopes = ((Dictionary)payload["app"]).get("scopes", Array());
		if (scopes.get_type() == Variant::ARRAY) {
			Array scope_list = scopes;
			for (int i = 0; i < scope_list.size(); i++) {
				token_scopes.append(scope_list[i]);
			}
		}
	}
	return true;
}

bool CCNetworkManager::has_scope(const String &scope) const {
	return has_valid_token() && token_scopes.has(scope);
}

String CCNetworkManager::get_interact_link() const {
	if (!has_valid_token() || profile_type.is_empty() || origin_id.is_empty()) {
		return String();
	}
	return vformat("https://interact.crowdcontrol.live/#/%s/%s", profile_type, origin_id);
}

bool CCNetworkManager::extend_token() {
	if (!has_valid_token() || token_jti.is_empty() || extend_in_flight || !is_connected_to_crowd_control()) {
		return false;
	}
	Dictionary body;
	body["jti"] = token_jti;
	extend_in_flight = true;
	next_extend_attempt = now() + TOKEN_EXTEND_RETRY;
	client_post_authorized("/auth/token/extend", Array(), JSON::stringify(body), TAG_EXTEND);
	return true;
}

void CCNetworkManager::ping() {
	if (socket_send_action("ping") == OK) {
		const double time = now();
		next_ping_time = time + PING_INTERVAL;
		pong_deadline = time + PONG_TIMEOUT;
	}
}

bool CCNetworkManager::upload_custom_effects(const Dictionary &effects, CustomEffectsMode mode) {
	if (!has_valid_token() || !is_connected_to_crowd_control()) {
		return false;
	}
	Dictionary operation;
	operation["mode"] = custom_effects_mode_names[mode];
	operation["effects"] = effects;
	Dictionary body;
	body["gamePackID"] = game_pack_id;
	body["operations"] = Array::make(operation);
	client_request_authorized(HTTPClient::METHOD_PUT, "/menu/custom-effects", Array::make("Content-Type: application/json"), JSON::stringify(body), TAG_CUSTOM_EFFECTS);
	return true;
}

bool CCNetworkManager::delete_custom_effects(const PackedStringArray &effect_ids) {
	if (!has_valid_token() || !is_connected_to_crowd_control()) {
		return false;
	}
	Dictionary body;
	body["gamePackID"] = game_pack_id;
	// Leaving the IDs out deletes every custom effect of the pack
	if (!effect_ids.is_empty()) {
		Array ids;
		for (int i = 0; i < effect_ids.size(); i++) {
			ids.append(effect_ids[i]);
		}
		body["effectIDs"] = ids;
	}
	client_post_authorized("/menu/custom-effects/delete", Array(), JSON::stringify(body), TAG_CUSTOM_EFFECTS);
	return true;
}

String CCNetworkManager::get_token_store_key() const {
	switch (target_server) {
		case TargetServer::STAGING:
			return "token_staging";
		case TargetServer::DEV:
			return "token_dev";
		default:
			return "token";
	}
}

void CCNetworkManager::load_stored_token() {
	Ref<ConfigFile> config;
	config.instantiate();
	if (config->load(TOKEN_STORE_PATH) != OK) {
		return;
	}
	String stored = config->get_value(TOKEN_STORE_SECTION, get_token_store_key(), "");
	if (!stored.is_empty() && !set_token(stored)) {
		// expired or malformed, remember it so the caller can clean it up
		token = stored;
		cc_uid = "";
		token_expiry = 0;
	}
}

void CCNetworkManager::store_token() const {
	Ref<ConfigFile> config;
	config.instantiate();
	config->load(TOKEN_STORE_PATH); // may not exist yet
	if (token.is_empty()) {
		if (config->has_section_key(TOKEN_STORE_SECTION, get_token_store_key())) {
			config->erase_section_key(TOKEN_STORE_SECTION, get_token_store_key());
		} else {
			return;
		}
	} else {
		config->set_value(TOKEN_STORE_SECTION, get_token_store_key(), token);
	}
	Error err = config->save(TOKEN_STORE_PATH);
	if (err != OK) {
		CrowdControl::singleton->log_warn(vformat("Failed to save the Crowd Control login (%s)", UtilityFunctions::error_string(err)));
	}
}

void CCNetworkManager::clear_token() {
	token = "";
	cc_uid = "";
	token_expiry = 0;
	token_jti = "";
	profile_type = "";
	origin_id = "";
	token_scopes.clear();
	prv_subscribed = false;
	extend_in_flight = false;
	store_token();
}

void CCNetworkManager::on_authenticated() {
	subscribe();
	CrowdControl::singleton->call_deferred("emit_signal", "logged_in");
}

void CCNetworkManager::start_session() {
	if (!has_valid_token() || !is_connected_to_crowd_control()) {
		const String reason = has_valid_token() ? "Not connected to Crowd Control." : "Not logged in to Crowd Control.";
		CrowdControl::singleton->log_error(vformat("Cannot start a session: %s", reason));
		CrowdControl::singleton->call_deferred("emit_signal", "session_start_failed", reason);
		return;
	}
	// Hidden / unavailable effects are passed along so the menu is right from the first moment
	Dictionary body;
	body["gamePackID"] = game_pack_id;
	body["effectReportArgs"] = CrowdControl::singleton->get_effect_report_args();
	if (!game_version.is_empty()) {
		body["gameVersion"] = game_version;
	}
	session_start_requested = true;
	client_post_authorized("/game-session/start", Array(), JSON::stringify(body), TAG_SESSION_START);
}

void CCNetworkManager::stop_session() {
	client_post_authorized("/game-session/stop", Array(), JSONSerializer::serialize_string<StopSession>(StopSession(game_session_id)), TAG_SESSION_STOP);
}

void CCNetworkManager::on_session_started(const String &p_game_session_id) {
	game_session_id = p_game_session_id;
	const bool requested = session_start_requested;
	session_start_requested = false;
	if (!CrowdControl::singleton->session_active) {
		CrowdControl::singleton->_on_session_started();
		if (!requested) {
			// Started from somewhere else (e.g. the Crowd Control app), it doesn't know our menu state yet
			CrowdControl::singleton->call_deferred("__sync_effect_states");
		}
	}
}

void CCNetworkManager::on_session_stopped() {
	game_session_id = "";
	if (!CrowdControl::singleton->session_active) {
		return;
	}
	CrowdControl::singleton->session_active = false;
	// Effects call into user code, so they have to be stopped from the main thread
	CrowdControl::singleton->call_deferred("__stop_all_effects");
	CrowdControl::singleton->call_deferred("emit_signal", "session_stopped");
	CrowdControl::singleton->display_message("Stopped the Crowd Control session!");
	if (CrowdControl::singleton->quit_on_session_close) {
		CrowdControl::singleton->quitting = true;
	}
}

Error CCNetworkManager::socket_send(const String &msg) const {
	if (is_connected_to_crowd_control()) {
		socket_mutex->lock();
		Error err = socket->send_text(msg);
		socket_mutex->unlock();
		if (err != OK) {
			CrowdControl::singleton->call_deferred("emit_signal", "connection_errored", err);
		}
		return err;
	}
	return ERR_UNAVAILABLE;
}

Error CCNetworkManager::socket_send_action(const String &action, const Variant &data) const {
	Dictionary dict;
	dict["action"] = action;
	if (data.get_type() != Variant::NIL) {
		dict["data"] = data;
	}
	return socket_send(JSON::stringify(dict));
}

Error CCNetworkManager::socket_send_rpc(const String &method, const Array &args) const {
	if (token.is_empty()) {
		return ERR_UNAUTHORIZED;
	}
	Dictionary call;
	call["type"] = "call";
	call["id"] = generate_uuid();
	call["method"] = method;
	call["args"] = args;
	Dictionary data;
	data["token"] = token;
	data["call"] = call;
	return socket_send_action("rpc", data);
}

Error CCNetworkManager::send_effect_response(const String &request_id, const PackedStringArray &statuses, int64_t time_remaining_ms, const String &message) const {
	Array args;
	for (int i = 0; i < statuses.size(); i++) {
		const String status = statuses[i];
		// Only these carry a remaining time, a finished effect has none left by definition
		const bool has_time = status == "timedBegin" || status == "timedPause" || status == "timedResume";
		args.append(EffectResponseArgs(request_id, status, has_time ? MAX(time_remaining_ms, (int64_t)0) : -1, message).serialize());
	}
	return socket_send_rpc("effectResponse", args);
}

Error CCNetworkManager::send_effect_report(const PackedStringArray &ids, const String &status, const String &identifier_type, const String &message) const {
	ERR_FAIL_COND_V(ids.is_empty(), ERR_INVALID_PARAMETER);
	EffectReportArgs args(status, ids);
	args.identifier_type = identifier_type;
	args.message = message;
	return socket_send_rpc("effectReport", Array::make(args.serialize()));
}

void CCNetworkManager::client_post(const String &path, const Array &headers, const String &body, const String &tag) {
	Array temp = headers.duplicate();
	temp.append("Content-Type: application/json");
	client_request(HTTPClient::METHOD_POST, path, temp, body, tag);
}

void CCNetworkManager::client_post_authorized(const String &path, const Array &headers, const String &body, const String &tag) {
	Array temp = headers.duplicate();
	temp.append(vformat("Authorization: cc-auth-token %s", token));
	client_post(path, temp, body, tag);
}

void CCNetworkManager::client_request(HTTPClient::Method method, const String &path, const Array &headers, const String &body, const String &tag) {
	// We shouldn't send requests unless we are connected
	if (is_connected_to_crowd_control()) {
		Array temp = headers.duplicate();
		temp.append_array(Array::make(USER_AGENT, "Accept: application/json"));
		// The loop only gives up on an idle connection while holding this lock and with nothing queued,
		// so a request is either picked up by the running loop or seen as "not alive" here.
		mutex->lock();
		requests.append(Array::make(method, path, temp, body, tag));
		const bool needs_start = !client_alive;
		mutex->unlock();
		if (needs_start) {
			// Requests come from several threads, only one of them may restart the client
			client_start_mutex->lock();
			if (!client_alive) {
				if (client_poll_thread.is_null()) {
					client_poll_thread.instantiate();
				}
				if (client_poll_thread->is_started()) {
					// The previous loop is on its way out (client_alive is false), wait for it to be gone
					client_poll_thread->wait_to_finish();
				}
				client_alive = true;
				client_poll_thread->start(Callable(this, "__client_loop"));
			}
			client_start_mutex->unlock();
		}
	}
}

void CCNetworkManager::client_request_authorized(HTTPClient::Method method, const String &path, const Array &headers, const String &body, const String &tag) {
	Array temp = headers.duplicate();
	temp.append(vformat("Authorization: cc-auth-token %s", token));
	client_request(method, path, temp, body, tag);
}

void CCNetworkManager::handle_socket_message(const Dictionary &dict) {
	String domain = dict.get("domain", "");
	String type = dict.get("type", "");
	Dictionary payload;
	if (((Variant)dict.get("payload", Variant())).get_type() == Variant::DICTIONARY) {
		payload = dict["payload"];
	}

	if (domain == "direct") {
		if (type == "application-auth-code") {
			// Step 2 of the login: the user has to open the URL and sign in.
			// The code itself is not a secret, without the PKCE verifier it is useless.
			auth_code = payload.get("code", "");
			String url = payload.get("url", "");
			CrowdControl::singleton->log_info(vformat("Open %s to log in to Crowd Control (code: %s)", url, auth_code));
			CrowdControl::singleton->call_deferred("emit_signal", "auth_code_received", auth_code, url);
			if (auto_open_auth_url && !url.is_empty()) {
				OS::get_singleton()->call_deferred("shell_open", url);
			}
		} else if (type == "application-auth-code-redeemed") {
			// Step 3: the user signed in, the code can now be exchanged for a token
			String code = payload.get("code", "");
			if (!auth_code.is_empty() && code == auth_code) {
				request_token();
			}
		} else if (type == "application-auth-code-error") {
			String message = payload.get("message", "Unknown error");
			CrowdControl::singleton->log_error(vformat("Crowd Control login failed: %s", message));
			auth_code = "";
			code_verifier = "";
			CrowdControl::singleton->call_deferred("emit_signal", "login_failed", message);
		} else if (type == "subscription-result") {
			Array success = payload.get("success", Array());
			Array failure = payload.get("failure", Array());
			String pub_topic = vformat("pub/%s", cc_uid);
			String prv_topic = vformat("prv/%s", cc_uid);
			prv_subscribed = success.has(prv_topic);
			for (int i = 0; i < failure.size(); i++) {
				if (String(failure[i]) == prv_topic) {
					// Not an error, effects work without it
					CrowdControl::singleton->log_info("No access to the private topic (needs the 'pubsub.private:read' scope), coin amounts won't be reported.");
				} else {
					CrowdControl::singleton->log_error(vformat("Failed to subscribe to Crowd Control topic: %s", failure[i]));
				}
			}
			if (success.has(pub_topic)) {
				client_request_authorized(HTTPClient::METHOD_GET, "/user/profile", Array(), "", TAG_PROFILE);
				if (CrowdControl::singleton->auto_starting_session && !CrowdControl::singleton->session_active) {
					start_session();
				}
			} else if (failure.has(pub_topic)) {
				// Without the public topic we can't receive effects, the token is of no use
				clear_token();
				CrowdControl::singleton->call_deferred("emit_signal", "logged_out");
			}
		} else if (type == "bad-request") {
			CrowdControl::singleton->log_error(vformat("Crowd Control rejected a request: %s", payload.get("message", "")));
		}
	} else if (domain == "pub") {
		if (token.is_empty()) {
			// logged out, whatever is still coming in is not for us anymore
			return;
		}
		if (type == "game-session-stop") {
			CrowdControl::singleton->log_info(dict);
			// String==(Variant) seems broken so we enforce String type
			if (game_session_id == String(payload.get("gameSessionID", ""))) {
				// server ended session?
				on_session_stopped();
			} else {
				CrowdControl::singleton->log_warn(vformat("Invalid game session stop received! Was the last session not closed properly?"));
			}
		} else if (type == "game-session-start") {
			CrowdControl::singleton->log_info(dict);
			String pack = payload.get("gamePackID", "");
			if (!pack.is_empty() && pack != game_pack_id) {
				// The streamer started a session for another game
				return;
			}
			on_session_started(payload.get("gameSessionID", ""));
		} else if (type == "effect-request") {
			CrowdControl::singleton->log_info(dict);
			on_effect_requested(payload);
		} else if (type == "effect-failure") {
			// Sent when a request is cancelled (e.g. by the streamer) while we still hold it
			String request_id = payload.get("requestID", "");
			if (!request_id.is_empty()) {
				CrowdControl::singleton->call_deferred("__cancel_request", request_id);
			}
		} else if (type == "timed-effect-update") {
			// The service ends timed effects prematurely through an update with status "end".
			// This is also echoed for our own "timedEnd", in which case the effect is long gone.
			String request_id = payload.get("requestID", "");
			if (String(payload.get("status", "")) == "end" && !request_id.is_empty()) {
				CrowdControl::singleton->call_deferred("__cancel_request", request_id);
			}
		} else if (type == "game-session-pause") {
			// The streamer paused (or resumed) ordering, the session itself stays alive
			if (game_session_id == String(payload.get("gameSessionID", ""))) {
				const bool session_paused = ((Variant)payload.get("gamePausedAt", Variant())).get_type() != Variant::NIL;
				CrowdControl::singleton->call_deferred("emit_signal", "session_paused", session_paused);
			}
		} else if (type == "coin-exchange" || type == "coins-exchange") {
			// The public event has no amount. If we get the private one we wait for that instead.
			if (!prv_subscribed) {
				on_coin_exchange(payload);
			}
		}
	} else if (domain == "prv") {
		if (token.is_empty()) {
			return;
		}
		// Effect events are mirrored here with pricing details, they are already handled through pub
		if (type == "coin-exchange") {
			on_coin_exchange(payload);
		}
	}
}

void CCNetworkManager::on_coin_exchange(const Dictionary &payload) {
	int64_t amount = 0;
	Variant raw_amount = payload.get("amount", 0);
	if (raw_amount.get_type() == Variant::INT || raw_amount.get_type() == Variant::FLOAT) {
		amount = raw_amount;
	}
	User exchanger_data;
	if (((Variant)payload.get("exchanger", Variant())).get_type() == Variant::DICTIONARY) {
		exchanger_data = JSONSerializer::deserialize<User>(payload["exchanger"]);
	}
	Ref<StreamUser> exchanger = CrowdControl::singleton->get_stream_user(exchanger_data);
	CrowdControl::singleton->call_deferred("emit_signal", "coins_exchanged", amount, exchanger);
}

void CCNetworkManager::on_effect_requested(const Dictionary &payload) {
	Dictionary effect_data;
	if (((Variant)payload.get("effect", Variant())).get_type() == Variant::DICTIONARY) {
		effect_data = payload["effect"];
	}
	String request_id = payload.get("requestID", "");
	String effect_id = effect_data.get("effectID", "");
	if (request_id.is_empty()) {
		return;
	}
	// The public topic carries every effect of the channel (overlay sounds, other games, ...),
	// only the ones for this game pack are ours to answer.
	if (String(effect_data.get("type", "game")) != "game") {
		return;
	}
	if (((Variant)payload.get("gamePack", Variant())).get_type() == Variant::DICTIONARY) {
		String pack = ((Dictionary)payload["gamePack"]).get("gamePackID", "");
		if (!pack.is_empty() && pack != game_pack_id) {
			return;
		}
	}

	if (!CrowdControl::singleton->effects.has(effect_id)) {
		// unknown effect? Return fail-permanent
		PackedStringArray statuses;
		statuses.append(CrowdControl::effect_instance_status_names[FAIL_PERMANENTLY]);
		send_effect_response(request_id, statuses);
		CrowdControl::singleton->log_error(vformat("Unknown effect ID '%s' received!", effect_id));
		return;
	}

	EffectRequest request = JSONSerializer::deserialize<EffectRequest>(payload);
	if (!request.parameters.has("quantity") && request.quantity != 0) {
		request.parameters.insert("quantity", { "", "", "quantity", UtilityFunctions::str(request.quantity) });
	}
	Ref<CCEffect> effect = CrowdControl::singleton->effects[effect_id];
	CrowdControl::singleton->_on_effect_requested(effect);
	CrowdControl::singleton->queue_effect(effect, request.requester, request.request_id, request.is_test || request.is_anonymous, request.parameters, request.effect.duration, request.is_test);
}

void CCNetworkManager::handle_client_message(const String &tag, const int code, const String &message) {
	Dictionary data;
	if (!message.is_empty()) {
		Ref<JSON> json;
		json.instantiate();
		if (json->parse(message) == OK && json->get_data().get_type() == Variant::DICTIONARY) {
			data = json->get_data();
		}
	}
	const bool ok = code >= 200 && code < 300;
	// Errors look like { "message": "...", "code": "UNAUTHORIZED", ... }
	String error_message = data.get("message", code == 0 ? String("No response from Crowd Control.") : vformat("HTTP %d", code));

	if (tag == TAG_TOKEN) {
		// Step 4 of the login. The verifier is single use, forget it either way.
		auth_code = "";
		code_verifier = "";
		String new_token = data.get("token", "");
		if (ok && set_token(new_token)) {
			extend_unavailable = false;
			next_extend_attempt = 0;
			if (persist_token) {
				store_token();
			}
			on_authenticated();
		} else {
			if (ok) {
				error_message = "Received an invalid token.";
			}
			CrowdControl::singleton->log_error(vformat("Crowd Control login failed: %s", error_message));
			CrowdControl::singleton->call_deferred("emit_signal", "login_failed", error_message);
		}
		return;
	}

	if (tag == TAG_EXTEND) {
		extend_in_flight = false;
		String new_token = data.get("token", "");
		if (ok && set_token(new_token)) {
			if (persist_token) {
				store_token();
			}
			CrowdControl::singleton->log_info("Extended the Crowd Control login.");
			CrowdControl::singleton->call_deferred("emit_signal", "token_refreshed");
		} else {
			if (code == 401 || code == 403) {
				// The application may not extend tokens, the user logs in again once this one runs out
				extend_unavailable = true;
			}
			CrowdControl::singleton->log_info(vformat("Could not extend the Crowd Control login: %s", ok ? String("Received an invalid token.") : error_message));
		}
		return;
	}

	if (tag == TAG_CUSTOM_EFFECTS) {
		if (!ok) {
			CrowdControl::singleton->log_error(vformat("Updating the custom effects failed: %s", error_message));
		}
		CrowdControl::singleton->call_deferred("emit_signal", "custom_effects_updated", ok, ok ? String() : error_message);
		return;
	}

	if (code == 401 && (tag == TAG_PROFILE || tag == TAG_SESSION_START)) {
		// The token was revoked or expired early, the user has to log in again
		CrowdControl::singleton->log_warn("The Crowd Control login is no longer valid.");
		clear_token();
		CrowdControl::singleton->call_deferred("emit_signal", "logged_out");
		return;
	}

	if (tag == TAG_PROFILE) {
		// we don't print profile data to avoid leaking sensitive data
		if (ok && ((Variant)data.get("profile", Variant())).get_type() == Variant::DICTIONARY) {
			UserInfo user = JSONSerializer::deserialize<UserInfo>(data);
			CrowdControl::singleton->streamer_user = StreamUser::make(
					user.profile.type,
					user.profile.origin_data.user.login,
					user.profile.origin_data.user.display_name,
					user.profile.image_url,
					user.profile.roles,
					user.profile.subscriptions,
					user.profile.origin_id);
		} else if (!ok) {
			CrowdControl::singleton->log_warn(vformat("Failed to fetch the Crowd Control profile: %s", error_message));
		}
	} else if (tag == TAG_SESSION_START) {
		if (ok) {
			// The session is also announced through the socket, whichever arrives first wins
			String id = data.get("gameSessionID", "");
			if (!id.is_empty()) {
				on_session_started(id);
			} else {
				// The service answers without a session if there is nothing this user may play
				session_start_requested = false;
				const String reason = vformat("No session was started for game pack '%s'. Check the game pack ID and that a version of the pack is in testing or released in the developer portal.", game_pack_id);
				CrowdControl::singleton->log_error(reason);
				CrowdControl::singleton->call_deferred("emit_signal", "session_start_failed", reason);
			}
		} else {
			session_start_requested = false;
			CrowdControl::singleton->log_error(vformat("Failed to start the Crowd Control session: %s", error_message));
			CrowdControl::singleton->call_deferred("emit_signal", "session_start_failed", error_message);
		}
	} else if (tag == TAG_SESSION_STOP) {
		if (ok) {
			on_session_stopped();
		} else {
			CrowdControl::singleton->log_error(vformat("Failed to stop the Crowd Control session: %s", error_message));
			if (CrowdControl::singleton->quit_on_session_close) {
				// We were asked to close, don't keep the connection alive over this
				CrowdControl::singleton->quitting = true;
			}
		}
	} else if (!ok) {
		CrowdControl::singleton->log_warn(vformat("Crowd Control request failed (%d): %s", code, error_message));
	}
}

Ref<Texture2D> CCNetworkManager::download_image(const String &url) {
	if (!url.is_empty()) {
		String host = url.substr(0, url.find("/", url.find(":") + 3));
		String path = url.substr(url.find("/", url.find(":") + 3));
		bool done = false;
		bool request_done = false;
		PackedByteArray rb;
		PackedByteArray chunk;
		Ref<HTTPClient> http;
		http.instantiate();
		http->connect_to_host(host);
		http->poll();
		while (!done) {
			switch (http->get_status()) {
				case HTTPClient::STATUS_DISCONNECTED:
					done = true;
					break;
				case HTTPClient::STATUS_RESOLVING:
					http->poll();
					break;
				case HTTPClient::STATUS_CANT_RESOLVE:
					done = true;
					break;
				case HTTPClient::STATUS_CONNECTING:
					http->poll();
					break;
				case HTTPClient::STATUS_CANT_CONNECT:
					done = true;
					break;
				case HTTPClient::STATUS_CONNECTED:
					if (!request_done) {
						http->request(HTTPClient::METHOD_GET, path, PackedStringArray());
						request_done = true;
					}
					break;
				case HTTPClient::STATUS_REQUESTING:
					http->poll();
					break;
				case HTTPClient::STATUS_BODY:
					if (http->has_response()) {
						chunk = http->read_response_body_chunk();
						if (chunk.size() == 0) {
							// No data, but received body, let's keep waiting for the body
							OS::get_singleton()->delay_msec(20);
						} else {
							rb.append_array(chunk);
						}
					}
					http->poll();
					if (http->get_status() != HTTPClient::STATUS_BODY) {
						done = true;
					}
					break;
				case HTTPClient::STATUS_CONNECTION_ERROR:
					done = true;
					break;
				case HTTPClient::STATUS_TLS_HANDSHAKE_ERROR:
					done = true;
					break;

				default:
					break;
			}
			OS::get_singleton()->delay_msec(20);
		}
		if (rb.size() != 0) {
			Ref<Image> image;
			image.instantiate();
			// Avatars come in whatever format the platform serves
			Error image_err = ERR_FILE_UNRECOGNIZED;
			if (rb.size() > 12 && rb[0] == 0xFF && rb[1] == 0xD8) {
				image_err = image->load_jpg_from_buffer(rb);
			} else if (rb.size() > 12 && rb[0] == 'R' && rb[1] == 'I' && rb[2] == 'F' && rb[3] == 'F' && rb[8] == 'W' && rb[9] == 'E') {
				image_err = image->load_webp_from_buffer(rb);
			} else {
				image_err = image->load_png_from_buffer(rb);
			}
			if (image_err != OK || image->is_empty()) {
				return nullptr;
			}
			Ref<ImageTexture> tex;
			tex.instantiate();
			tex->set_image(image);
			return tex;
		}
	}
	return nullptr;
}

void CCNetworkManager::attempt_reconnect() {
	current_reconnect_retry_count++;
	const int max_retries = CrowdControl::singleton->reconnect_retry_count;
	const bool should_retry = max_retries == -1 || current_reconnect_retry_count <= max_retries;
	if (max_retries != 0 && should_retry && !CrowdControl::singleton->quitting) {
		reconnect_time = now() + MAX(CrowdControl::singleton->reconnect_retry_delay, 0.1f);
	} else {
		close();
		CrowdControl::singleton->call_deferred("emit_signal", "connection_errored", ERR_CANT_CONNECT);
	}
}

String CCNetworkManager::get_host_for_target(TargetServer target, const String &base_host) const {
	switch (target) {
		case TargetServer::PRODUCTION:
			return base_host;
		case TargetServer::STAGING:
			return "beta-" + base_host;
		case TargetServer::DEV:
			return "dev-" + base_host;

		default:
			ERR_FAIL_V_EDMSG("", "Invalid enum value supplied for target server");
	}
}

void CCNetworkManager::socket_connect() {
	OS::get_singleton()->set_thread_name("Crowd Control Connect Thread");

	while (unlikely(socket->get_ready_state() == WebSocketPeer::STATE_CLOSING)) {
		OS::get_singleton()->delay_msec(100);
		socket_mutex->lock();
		socket->poll();
		socket_mutex->unlock();
	}

	// A previous poll loop can only still be around if it is about to return
	if (socket_poll_thread.is_valid() && socket_poll_thread->is_started()) {
		socket_poll_thread->wait_to_finish();
	}

	const bool is_reconnect = current_reconnect_retry_count > 0;

	socket_mutex->lock();
	PackedStringArray handshake_headers;
	handshake_headers.append(USER_AGENT);
	socket->set_handshake_headers(handshake_headers);
	String url = pubsub_url_override.is_empty() ? "wss://" + get_host_for_target(target_server, PUBSUB_HOST) : pubsub_url_override;
	Error err = socket->connect_to_url(url);
	socket_mutex->unlock();
	if (unlikely(err != OK)) {
		// Malformed URL?
		CrowdControl::singleton->call_deferred("emit_signal", "connection_errored", ERR_CANT_RESOLVE);
		ERR_FAIL_EDMSG("Failed to connect to Crowd Control!");
	}

	WebSocketPeer::State state = WebSocketPeer::STATE_CONNECTING;
	while (state == WebSocketPeer::STATE_CONNECTING) {
		OS::get_singleton()->delay_msec(20);
		socket_mutex->lock();
		socket->poll();
		state = socket->get_ready_state();
		socket_mutex->unlock();
	}

	if (state != WebSocketPeer::STATE_OPEN) {
		// Failed to connect
		if (is_reconnect) {
			// keep trying until we run out of retries
			attempt_reconnect();
		} else {
			CrowdControl::singleton->call_deferred("emit_signal", "connection_errored", ERR_CANT_CONNECT);
		}
		ERR_FAIL_EDMSG("Failed to connect to Crowd Control!");
	}
	current_reconnect_retry_count = 0;
	next_ping_time = now() + PING_INTERVAL;
	pong_deadline = 0;
	CrowdControl::singleton->_on_connected();
	if (socket_poll_thread.is_null()) {
		socket_poll_thread.instantiate();
	}
	socket_poll_thread->start(Callable(this, "__socket_poll_loop"));
}

void CCNetworkManager::socket_poll_loop() {
	OS::get_singleton()->set_thread_name("Crowd Control Poll Loop");
	PackedStringArray messages;
	while (!CrowdControl::singleton->quitting) {
		OS::get_singleton()->delay_msec(50);

		// Only hold the lock to drain the socket, handling a message may send one in turn
		messages.clear();
		socket_mutex->lock();
		socket->poll();
		const WebSocketPeer::State state = socket->get_ready_state();
		if (state == WebSocketPeer::STATE_OPEN) {
			while (socket->get_available_packet_count() > 0) {
				PackedByteArray packet = socket->get_packet();
				Error err = socket->get_packet_error();
				if (unlikely(err != OK)) {
					CrowdControl::singleton->call_deferred("emit_signal", "connection_errored", err);
					continue;
				}
				messages.append(packet.get_string_from_utf8());
			}
		}
		socket_mutex->unlock();

		if (state != WebSocketPeer::STATE_OPEN) {
			if (state == WebSocketPeer::STATE_CLOSED) {
				// The service closes sockets after 10 minutes of inactivity and 2 hours after connecting
				CrowdControl::singleton->call_deferred("emit_signal", "connection_errored", ERR_CONNECTION_ERROR);
				CrowdControl::singleton->_on_disconnected();
				attempt_reconnect();
				call_deferred("__cleanup_thread", socket_poll_thread);
				ERR_FAIL_EDMSG("Crowd Control socket closed unexpectedly!");
			}
			continue;
		}

		for (int i = 0; i < messages.size(); i++) {
			// Anything coming in proves the connection is alive
			pong_deadline = 0;
			const String message = messages[i].strip_edges();
			if (message == "pong") {
				// The reply to a ping is the bare word, not a JSON document
				CrowdControl::singleton->call_deferred("emit_signal", "pong_received");
				continue;
			}
			if (message.is_empty()) {
				continue;
			}
			Ref<JSON> json;
			json.instantiate();
			if (json->parse(message) == OK && json->get_data().get_type() == Variant::DICTIONARY) {
				handle_socket_message(json->get_data());
			} else {
				CrowdControl::singleton->log_info(vformat("Received: %s", message));
			}
		}

		const double time = now();
		if (pong_deadline > 0 && time > pong_deadline) {
			// No reply to our ping, drop the connection without waiting for a handshake nobody will answer
			CrowdControl::singleton->log_warn("Crowd Control did not respond to a ping, reconnecting...");
			pong_deadline = 0;
			socket_mutex->lock();
			socket->close(-1);
			socket_mutex->unlock();
			continue;
		}
		if (time >= next_ping_time) {
			socket_send_action("ping");
			next_ping_time = time + PING_INTERVAL;
			pong_deadline = time + PONG_TIMEOUT;
		}
		if (has_valid_token() && !extend_unavailable && token_expiry - time < TOKEN_EXTEND_MARGIN && time >= next_extend_attempt) {
			// Also covers reconnects: "extend your token if necessary, then re-subscribe"
			extend_token();
		}
		if (!token.is_empty() && !cc_uid.is_empty() && !has_valid_token()) {
			// Tokens can't be refreshed by public applications, the user has to log in again
			CrowdControl::singleton->log_warn("The Crowd Control login has expired.");
			clear_token();
			CrowdControl::singleton->call_deferred("emit_signal", "logged_out");
		}
	}
	socket_mutex->lock();
	socket->close();
	socket_mutex->unlock();
	// Give the service a moment to acknowledge, then give up on a clean close
	const double close_deadline = now() + 3.0;
	while (socket->get_ready_state() == WebSocketPeer::STATE_CLOSING) {
		OS::get_singleton()->delay_msec(50);
		socket_mutex->lock();
		socket->poll();
		if (now() > close_deadline) {
			socket->close(-1);
		}
		socket_mutex->unlock();
	}
	CrowdControl::singleton->_on_disconnected();
	call_deferred("__cleanup_thread", socket_poll_thread);
}

void CCNetworkManager::client_loop() {
	CrowdControl::singleton->log_info("Starting client...");
	if (unlikely(client->get_status() != HTTPClient::STATUS_DISCONNECTED)) {
		client->close();
	}
	OS::get_singleton()->set_thread_name("Crowd Control HTTP Poll Loop");
	String host = "https://" + get_host_for_target(target_server, OPENAPI_HOST);
	int port = -1;
	if (!api_url_override.is_empty()) {
		// HTTPClient wants the port separately: "http://localhost:8080" -> "http://localhost", 8080
		host = api_url_override.trim_suffix("/");
		const int scheme_end = host.find("://");
		const int port_start = host.rfind(":");
		if (port_start > scheme_end + 2 && host.substr(port_start + 1).is_valid_int()) {
			port = host.substr(port_start + 1).to_int();
			host = host.substr(0, port_start);
		}
	}
	Error err = client->connect_to_host(host, port);
	if (err != OK) {
		// Malformed URL?
		client_alive = false;
		CrowdControl::singleton->call_deferred("emit_signal", "connection_errored", ERR_CANT_RESOLVE);
		ERR_FAIL_EDMSG("Failed to connect to Crowd Control! Was the gdextension edited?");
	}
	// State of the request currently waiting for its response
	bool in_flight = false;
	String tag;
	PackedByteArray body;
	uint64_t ticks = 0;
	Error failure = OK;
	bool idle_drop = false;
	client->poll();
	while (!CrowdControl::singleton->quitting && client_alive) {
		switch (client->get_status()) {
			case HTTPClient::STATUS_DISCONNECTED:
				idle_drop = true;
				break;
			case HTTPClient::STATUS_RESOLVING:
			case HTTPClient::STATUS_CONNECTING:
			case HTTPClient::STATUS_REQUESTING:
				client->poll();
				break;
			case HTTPClient::STATUS_CANT_RESOLVE:
				client_alive = false;
				failure = ERR_CANT_RESOLVE;
				break;
			case HTTPClient::STATUS_CANT_CONNECT:
				client_alive = false;
				failure = ERR_CANT_CONNECT;
				break;
			case HTTPClient::STATUS_CONNECTED:
				if (in_flight) {
					// The response is complete, bodies are only handled once they arrived in full
					in_flight = false;
					CrowdControl::singleton->log_info(vformat("Request completed in %s msec(s)", Time::get_singleton()->get_ticks_msec() - ticks));
					handle_client_message(tag, client->get_response_code(), body.get_string_from_utf8());
					body.clear();
				} else if (requests.size() != 0) {
					mutex->lock();
					Array request = requests.pop_front();
					mutex->unlock();
					ticks = Time::get_singleton()->get_ticks_msec();
					tag = request[4];
					body.clear();
					err = client->request((HTTPClient::Method)((int)request[0]), request[1], request[2], request[3]);
					if (err != OK) {
						CrowdControl::singleton->call_deferred("emit_signal", "connection_errored", err);
						handle_client_message(tag, 0, "");
						break;
					}
					in_flight = true;
				}
				client->poll();
				break;
			case HTTPClient::STATUS_BODY: {
				PackedByteArray chunk = client->read_response_body_chunk();
				if (chunk.size() != 0) {
					body.append_array(chunk);
				}
				break;
			}
			case HTTPClient::STATUS_CONNECTION_ERROR:
				// Keep-alive connections being dropped while idle is expected and not worth reporting
				if (in_flight) {
					client_alive = false;
					failure = ERR_CONNECTION_ERROR;
				} else {
					idle_drop = true;
				}
				break;
			case HTTPClient::STATUS_TLS_HANDSHAKE_ERROR:
				client_alive = false;
				failure = ERR_CONNECTION_ERROR;
				break;

			default:
				break;
		}
		if (idle_drop) {
			idle_drop = false;
			// Stop for now unless a request arrived in the meantime, see client_request()
			mutex->lock();
			const bool pending = !requests.is_empty();
			if (!pending) {
				client_alive = false;
			}
			mutex->unlock();
			if (pending) {
				client->close();
				if (client->connect_to_host(host, port) != OK) {
					client_alive = false;
					failure = ERR_CANT_CONNECT;
				}
			}
		}
		OS::get_singleton()->delay_msec(20);
	}
	client->close();
	if (failure != OK) {
		CrowdControl::singleton->call_deferred("emit_signal", "connection_errored", failure);
		CrowdControl::singleton->log_error(vformat("Connection to the Crowd Control API failed (%s)", UtilityFunctions::error_string(failure)));
	}
	if (in_flight) {
		// Let whoever is waiting for this response know it is not coming
		handle_client_message(tag, 0, "");
	}
	if (failure != OK) {
		// Nothing queued behind a failed connection is going to make it either
		mutex->lock();
		Array dropped = requests.duplicate();
		requests.clear();
		mutex->unlock();
		for (int i = 0; i < dropped.size(); i++) {
			handle_client_message(((Array)dropped[i])[4], 0, "");
		}
	}
	call_deferred("__cleanup_thread", client_poll_thread);
}

void CCNetworkManager::download_image_for_user(const Ref<Thread> &thread, const Ref<StreamUser> &user) {
	String url = user->get_profile_icon_url();
	Ref<Texture2D> tex = download_image(url);
	user->set_profile_icon(tex);
	user->call_deferred("emit_signal", "icon_downloaded");
	call_deferred("__cleanup_thread", thread);
}

void CCNetworkManager::cleanup_thread(const Ref<Thread> &thread) {
	// A thread that is alive again has been restarted since this was queued,
	// whoever restarted it already joined the run this call was meant for.
	if (thread.is_valid() && thread->is_started() && !thread->is_alive()) {
		thread->wait_to_finish();
	}
}

void CCNetworkManager::_bind_methods() {
	/// Due to the limitations of the exposed @see Thread class only being able to
	/// be started with @see Callable 's these functions need to be exposed to
	/// the engine. Thankfully members prefixed with `__` are considered internal
	/// and are thus not shown to the user.
	/// However this also means this class needed to be exposed to the engine as well,
	/// though marking it as abstract disables the user from ever instantiating it.
	ClassDB::bind_method(D_METHOD("__socket_connect"), &CCNetworkManager::socket_connect);
	ClassDB::bind_method(D_METHOD("__socket_poll_loop"), &CCNetworkManager::socket_poll_loop);
	ClassDB::bind_method(D_METHOD("__client_loop"), &CCNetworkManager::client_loop);
	ClassDB::bind_method(D_METHOD("__download_profile_image"), &CCNetworkManager::download_image_for_user);
	ClassDB::bind_method(D_METHOD("__cleanup_thread"), &CCNetworkManager::cleanup_thread);
}

CCNetworkManager::CCNetworkManager() {
	socket.instantiate();
	client.instantiate();
	mutex.instantiate();
	socket_mutex.instantiate();
	client_start_mutex.instantiate();
}

CCNetworkManager::~CCNetworkManager() {
	CrowdControl::singleton->quitting = true;
	if (connect_thread.is_valid() && connect_thread->is_started()) {
		connect_thread->wait_to_finish();
	}
	if (socket_poll_thread.is_valid() && socket_poll_thread->is_started()) {
		socket_poll_thread->wait_to_finish();
	}
	if (client_poll_thread.is_valid() && client_poll_thread->is_started()) {
		client_poll_thread->wait_to_finish();
	}
}
