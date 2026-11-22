#ifndef CROWD_CONTROL_CC_NETWORK_MANAGER_HPP
#define CROWD_CONTROL_CC_NETWORK_MANAGER_HPP

#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/classes/web_socket_peer.hpp>
#include <godot_cpp/templates/hash_map.hpp>

#include "util/binding_macros.hpp"
#include "util/cc_enums.hpp"

class CCEffect;
class CCEffectInstance;
class CrowdControl;
class StreamUser;

using namespace godot;

/// @brief Manages the communication between the engine and Crowd Control, among other internet utilities.
/// @details
/// This is mainly a helper class to seperate the network implementation from the main class.
///
/// Implements the Crowd Control 2.0 PubSub protocol (https://developer.crowdcontrol.live/sockets/):
/// - Authentication uses the application auth code flow with PKCE, so no secret is shipped with the game.
///   `generate-auth-code` -> `application-auth-code` (user signs in in their browser) ->
///   `application-auth-code-redeemed` -> `POST /auth/application/token`.
/// - The resulting JWT is used to subscribe to `pub/<ccUID>`, to start / stop game sessions through
///   the HTTP API and to answer effect requests through `rpc` calls.
class CCNetworkManager : public RefCounted {
	GDCLASS(CCNetworkManager, RefCounted)

private:
	/// @brief Seconds between keepalive pings, the service drops idle sockets after 10 minutes.
	static constexpr double PING_INTERVAL = 300.0;
	/// @brief Seconds to wait for a reply to a ping before the socket is considered dead.
	static constexpr double PONG_TIMEOUT = 15.0;
	/// @brief Tokens expiring within this many seconds are treated as already expired.
	static constexpr double TOKEN_EXPIRY_MARGIN = 60.0;
	/// @brief Tokens are extended once they have less than this many seconds left.
	static constexpr double TOKEN_EXTEND_MARGIN = 3600.0;
	/// @brief Seconds to wait before asking for an extension again.
	static constexpr double TOKEN_EXTEND_RETRY = 600.0;
	/// @brief A stored login that can't be extended is only reused if it lasts at least this many seconds longer.
	/// @details
	/// Tokens are valid for 24 hours and public applications may not extend them. Asking for a fresh login
	/// up front beats logging the streamer out in the middle of their session.
	static constexpr double STORED_TOKEN_MIN_REMAINING = 8.0 * 3600.0;

	Ref<WebSocketPeer> socket;
	Ref<HTTPClient> client;

	bool client_alive = false;

	/// @brief Mutex to ensure we don't perform unsafe methods on the requests array (resizing is not thread-safe)
	Ref<Mutex> mutex;
	/// @brief Mutex guarding the socket, it is polled on its own thread while messages are sent from others.
	Ref<Mutex> socket_mutex;
	/// @brief Mutex guarding the (re)start of the HTTP client thread.
	Ref<Mutex> client_start_mutex;

	Ref<Thread> connect_thread;
	Ref<Thread> socket_poll_thread;
	Ref<Thread> client_poll_thread;

	TargetServer target_server = PRODUCTION;

	/// @brief The JWT of the logged in user, empty if not logged in.
	String token;
	/// @brief Crowd Control user ID decoded from the token.
	String cc_uid;
	/// @brief Unix time (seconds) at which the token expires.
	double token_expiry = 0;
	/// @brief ID of the token, needed to extend it.
	String token_jti;
	/// @brief Platform (twitch, youtube, ...) and platform user ID of the logged in user.
	String profile_type;
	String origin_id;
	PackedStringArray token_scopes;

	bool extend_in_flight = false;
	/// @brief Set once the service refused to extend a token, public applications usually may not.
	bool extend_unavailable = false;
	double next_extend_attempt = 0;

	/// @brief Whether the private topic (prices, coin amounts) could be subscribed to.
	bool prv_subscribed = false;
	/// @brief Whether the session about to start was requested by us (and thus got the menu state already).
	bool session_start_requested = false;

	/// @brief The auth code of the login attempt currently in progress.
	String auth_code;
	/// @brief The PKCE verifier of the login attempt currently in progress.
	String code_verifier;

	// Settings cached on the main thread so the network threads don't have to touch ProjectSettings
	String app_id;
	String game_pack_id;
	/// @brief The build of the game, lets the service pick the pack version made for it. Empty if unknown.
	String game_version;
	bool auto_open_auth_url = true;
	bool persist_token = true;
	// Optional overrides of the service endpoints, see the `crowd_control/debug/*_url` project settings
	String pubsub_url_override;
	String api_url_override;

	Array requests;

	String game_session_id = "";

	double next_ping_time = 0;
	/// @brief Unix time until which a reply to the last ping is expected, 0 if none is pending.
	double pong_deadline = 0;
	/// @brief Unix time at which the next reconnect attempt should happen, 0 if none is scheduled.
	double reconnect_time = 0;
	int current_reconnect_retry_count = 0;

private:
	String get_host_for_target(TargetServer target, const String &base_host) const;
	String get_token_store_key() const;

	void socket_connect();
	void socket_poll_loop();
	void client_loop();

	void handle_socket_message(const Dictionary &dict);
	void handle_client_message(const String &tag, const int code, const String &message);

	void on_effect_requested(const Dictionary &payload);
	void on_coin_exchange(const Dictionary &payload);

	/// @brief Exchanges the redeemed auth code of the current login attempt for a user token.
	void request_token();
	/// @brief Decodes and stores a token.
	/// @return false if the token is malformed or expired.
	bool set_token(const String &p_token);
	void load_stored_token();
	void store_token() const;
	/// @brief Forgets the current token, including the stored copy.
	void clear_token();
	/// @brief Called once a valid token is available on an open socket.
	void on_authenticated();

	void on_session_started(const String &p_game_session_id);
	void on_session_stopped();

	/// @brief Downloads an image from the specified url
	/// @param url the url to download the image from
	/// @return The requested image if the request is successful, else a nullptr reference
	Ref<Texture2D> download_image(const String &url);

	void attempt_reconnect();

	static double now();

protected:
	static void _bind_methods();

public:
	/// @brief Generates a random (version 4) UUID.
	static String generate_uuid();
	/// @brief Encodes data as unpadded base64url (RFC 4648 section 5).
	static String base64_url_encode(const PackedByteArray &data);
	/// @brief Whether a string is a version the service accepts, e.g. `1.0`, `1.2.3` or `1.2.3-beta`.
	static bool is_valid_game_version(const String &p_version);
	/// @brief Decodes the payload of a JWT without verifying its signature.
	static Dictionary decode_jwt_payload(const String &p_token);

	const String get_game_session_id() const { return game_session_id; }
	const String get_game_pack_id() const { return game_pack_id; }

	/// @brief Whether the current token was granted a scope, e.g. `custom-effects:write`.
	bool has_scope(const String &scope) const;
	/// @brief The link viewers use to interact with the logged in streamer, empty if not logged in.
	String get_interact_link() const;
	/// @brief Asks the service for a token with a later expiry. Needs the `token:extend` scope.
	/// @return false if no request was sent
	bool extend_token();
	/// @brief Sends a keepalive ping right away, `pong_received` is emitted on the reply.
	void ping();

	/// @brief Uploads custom effect definitions (effect ID -> definition) for the game pack.
	bool upload_custom_effects(const Dictionary &effects, CustomEffectsMode mode);
	/// @brief Deletes custom effects of the game pack, all of them if no IDs are given.
	bool delete_custom_effects(const PackedStringArray &effect_ids);

	bool is_connected_to_crowd_control() const;
	/// @brief Whether a user token is available that has not expired yet.
	bool has_valid_token() const;

	Error connect(TargetServer target);
	void close();
	/// @brief Reconnects if the connection was lost and a reconnect attempt is due.
	void poll_reconnect();
	/// @brief Called when the socket connection has been established.
	void on_socket_connected();

	/// @brief Starts the PKCE login flow by requesting an application auth code.
	void login();
	/// @brief Invalidates and forgets the current token.
	void logout();
	void subscribe();

	/// @brief Starts a game session for the configured game pack.
	void start_session();
	/// @brief Stops the current game session.
	void stop_session();

	Error socket_send(const String &msg) const;
	/// @brief Sends an action as JSON data to the socket server
	/// @param action the action to be performed
	/// @param data data to be send as parameter to the action, omitted if null
	/// @return returns a non OK value when the request failed to be send
	Error socket_send_action(const String &action, const Variant &data = Variant()) const;
	/// @brief Sends a remote procedure call to the Crowd Control service.
	Error socket_send_rpc(const String &method, const Array &args) const;

	void client_post(const String &path, const Array &headers, const String &body = "", const String &tag = "");
	void client_post_authorized(const String &path, const Array &headers, const String &body = "", const String &tag = "");
	void client_request(HTTPClient::Method method, const String &path, const Array &headers, const String &body = "", const String &tag = "");
	void client_request_authorized(HTTPClient::Method method, const String &path, const Array &headers, const String &body = "", const String &tag = "");

	/// @brief Reports the state of an effect request to the service.
	/// @param request_id the ID of the request the response is for
	/// @param statuses one or more statuses, sent in order as part of the same call
	/// @param time_remaining_ms the remaining time of a timed effect in milliseconds,
	/// only sent for timed statuses that carry it, negative to omit
	Error send_effect_response(const String &request_id, const PackedStringArray &statuses, int64_t time_remaining_ms = -1, const String &message = "") const;
	/// @brief Reports a change of an effect's menu state (visibility / availability) to the service.
	/// @param identifier_type what the IDs refer to: `effect`, `category` or `group`
	Error send_effect_report(const PackedStringArray &ids, const String &status, const String &identifier_type = "effect", const String &message = "") const;

	void download_image_for_user(const Ref<Thread> &thread, const Ref<StreamUser> &user);
	/// @brief waits for a thread to finish to perform cleanup
	/// @param thread the thread to wait on.
	/// @details
	/// Godot requires `wait_to_finish` to be called on every thread at some point.
	/// However as a thread can not wait for itself we use this method via deferred call
	/// to have the main thread perform this.
	/// @note Only call this on threads that are finished, otherwise the main thread will stall.
	void cleanup_thread(const Ref<Thread> &thread);

	CCNetworkManager();
	~CCNetworkManager();
};

#endif // CROWD_CONTROL_CC_NETWORK_MANAGER_HPP
