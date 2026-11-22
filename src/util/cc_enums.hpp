#ifndef CROWD_CONTROL_CC_ENUMS_HPP
#define CROWD_CONTROL_CC_ENUMS_HPP

#include <godot_cpp/core/class_db.hpp>

enum BroadcasterType {
	COMMUNITY,
	AFFILIATE,
	PARTNER,
};

enum TargetServer {
	PRODUCTION,
	STAGING,
	DEV,
};

/// @brief How uploaded custom effects are combined with the ones already stored for the game pack.
enum CustomEffectsMode {
	/// Add or update the uploaded effects, keep everything else.
	CUSTOM_EFFECTS_MERGE,
	/// Replace the whole list with the uploaded effects.
	CUSTOM_EFFECTS_REPLACE_ALL,
	/// Replace only the uploaded effects.
	CUSTOM_EFFECTS_REPLACE_PARTIAL,
};

enum EffectInstanceStatus {
	SUCCESS,
	FAIL_TEMPORARILY,
	FAIL_PERMANENTLY,
	TIMED_BEGIN,
	TIMED_PAUSE,
	TIMED_RESUME,
	TIMED_END,
};

//VARIANT_ENUM_CAST(BroadcasterType)
VARIANT_ENUM_CAST(TargetServer)
VARIANT_ENUM_CAST(CustomEffectsMode)

#endif // CROWD_CONTROL_CC_ENUMS_HPP