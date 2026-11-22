#ifndef CROWD_CONTROL_EFFECT_REPORT_HPP
#define CROWD_CONTROL_EFFECT_REPORT_HPP

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "classes/cc_network_manager.hpp"

using namespace godot;

/// @brief A single entry of the `args` array of an `effectReport` RPC.
/// @details
/// `status` is one of `menuAvailable`, `menuUnavailable`, `menuVisible` or `menuHidden`.
struct EffectReportArgs {
	String id;
	String status;
	uint64_t stamp = 0;
	PackedStringArray ids;
	String effect_type = "game";
	String identifier_type = "effect";
	/// @brief Optional human readable reason, e.g. why an effect is unavailable.
	String message;

	EffectReportArgs() {}

	EffectReportArgs(const String &p_status, const PackedStringArray &p_ids) {
		id = CCNetworkManager::generate_uuid();
		status = p_status;
		ids = p_ids;
		stamp = (uint64_t)Time::get_singleton()->get_unix_time_from_system();
	}

	Dictionary serialize() const {
		Dictionary out;
		out["id"] = id;
		out["status"] = status;
		out["stamp"] = stamp;
		Array out_ids;
		for (int i = 0; i < ids.size(); i++) {
			out_ids.append(ids[i]);
		}
		out["ids"] = out_ids;
		out["effectType"] = effect_type;
		out["identifierType"] = identifier_type;
		if (!message.is_empty()) {
			out["message"] = message;
		}
		return out;
	}
};

#endif // CROWD_CONTROL_EFFECT_REPORT_HPP
