#ifndef CROWD_CONTROL_EFFECT_RESPONSE_HPP
#define CROWD_CONTROL_EFFECT_RESPONSE_HPP

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "classes/cc_network_manager.hpp"

using namespace godot;

/// @brief A single entry of the `args` array of an `effectResponse` RPC.
/// @details
/// `timeRemaining` is in milliseconds and is only sent for the timed statuses that carry it
/// (`timedBegin`, `timedPause`, `timedResume`). It is omitted for everything else.
struct EffectResponseArgs {
	String id;
	String request;
	String status;
	String message = "";
	uint64_t stamp = 0;
	/// @brief Remaining time in milliseconds, negative means "do not send".
	int64_t time_remaining = -1;

	EffectResponseArgs() {}

	EffectResponseArgs(const String &p_request, const String &p_status, int64_t p_time_remaining = -1, const String &p_message = "") {
		id = CCNetworkManager::generate_uuid();
		request = p_request;
		status = p_status;
		message = p_message;
		time_remaining = p_time_remaining;
		stamp = (uint64_t)Time::get_singleton()->get_unix_time_from_system();
	}

	Dictionary serialize() const {
		Dictionary out;
		out["id"] = id;
		out["request"] = request;
		out["status"] = status;
		out["message"] = message;
		out["stamp"] = stamp;
		if (time_remaining >= 0) {
			out["timeRemaining"] = time_remaining;
		}
		return out;
	}
};

#endif // CROWD_CONTROL_EFFECT_RESPONSE_HPP
