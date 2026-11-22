#ifndef CROWD_CONTROL_MANIFEST_HPP
#define CROWD_CONTROL_MANIFEST_HPP

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "classes/cc_effect/cc_effect.hpp"
#include "classes/cc_effect/cc_effect_bid_war.hpp"
#include "classes/cc_effect/cc_effect_parameters.hpp"
#include "classes/cc_effect/cc_effect_timed.hpp"

using namespace godot;

/// @brief Builds the Game Pack ("menu") JSON for a set of effects.
/// @details
/// This is the `menu.json` the developer portal (https://dev.crowdcontrol.live/) expects inside an uploaded pack:
/// @code
/// {
///   "format": 1,
///   "kind": "built-in",
///   "meta": { "name": ..., "platform": "PC", "connector": [...], "releaseDate": ..., "guide": ... },
///   "effects": { "game": { "<effectID>": { "name": ..., "price": ..., ... } } }
/// }
/// @endcode
/// `format` and `kind` are required by the portal. `meta` and the `game` group follow the Game Pack format
/// documented at https://developer.crowdcontrol.live/sockets/pack.html , the portal ignores / unwraps them.
/// The per effect definitions are also what the custom effects API expects.
/// Dictionaries keep their insertion order, so the output lists effects in the order they were added.
struct Manifest {
	/// @brief Builds the definition of a single effect (the value stored under `effects.game.<effectID>`).
	static Dictionary effect_definition(const Ref<CCEffect> &effect) {
		Dictionary out;
		out["name"] = effect->get_name();
		if (!effect->get_note().is_empty()) {
			out["note"] = effect->get_note();
		}
		out["description"] = effect->get_description();
		out["price"] = effect->get_price();

		PackedStringArray categories = effect->get_categories();
		if (!categories.is_empty()) {
			out["category"] = to_array(categories);
		}
		PackedStringArray groups = effect->get_groups();
		if (!groups.is_empty()) {
			out["group"] = to_array(groups);
		}

		// -1 (evil / chaotic) to 1 (good / orderly)
		Dictionary alignment;
		alignment["orderliness"] = effect->get_orderliness_value();
		alignment["morality"] = effect->get_morality_value();
		out["alignment"] = alignment;

		// `visible` and `sellable` are runtime state, reported when a session starts. They are not part of the
		// pack: `inactive` there means "off unless the streamer turns it on", which is something else.
		// Flags are only written when set, absent means false
		if (effect->is_pooling_disabled()) {
			out["unpoolable"] = true;
		}
		if (effect->get_session_max() > 0) {
			out["sessionMax"] = effect->get_session_max();
		}
		PackedStringArray conflicts = effect->get_conflicts();
		if (!conflicts.is_empty()) {
			out["conflicts"] = to_array(conflicts);
		}

		if (effect->get_effect_type() == CCEffect::EffectType::TIMED) {
			Ref<CCEffectTimed> timed = Object::cast_to<CCEffectTimed>(effect.ptr());
			// Whole seconds, the service accepts 1 to 600
			Dictionary duration;
			duration["value"] = CLAMP((int64_t)Math::round(timed->get_duration()), (int64_t)1, (int64_t)600);
			out["duration"] = duration;
		} else if (effect->get_effect_type() == CCEffect::EffectType::PARAMETERS) {
			Ref<CCEffectParameters> params = Object::cast_to<CCEffectParameters>(effect.ptr());
			Dictionary parameters;
			// By ID lookups are unordered, go through the entries in the order they were declared
			for (int i = 0; i < params->get_parameter_count(); i++) {
				const String param_id = params->get_parameter_id(i);
				if (params->get_parameter_kind(i) == CCEffectParameters::ParameterEntryKind::RANGE) {
					// An effect has a single quantity, it is not part of the parameter list
					if (out.has("quantity")) {
						continue;
					}
					Dictionary quantity;
					quantity["min"] = MAX((int64_t)params->get_range_parameter_min(i), (int64_t)1);
					quantity["max"] = MAX((int64_t)params->get_range_parameter_max(i), (int64_t)2);
					out["quantity"] = quantity;
					continue;
				}
				Dictionary param;
				param["type"] = "options";
				param["name"] = params->get_parameter_name(i);
				Dictionary options;
				PackedStringArray option_ids = params->get_item_parameter_option_ids(i);
				PackedStringArray option_names = params->get_item_parameter_options(i);
				for (int j = 0; j < option_ids.size() && j < option_names.size(); j++) {
					Dictionary option;
					option["name"] = option_names[j];
					options[option_ids[j]] = option;
				}
				param["options"] = options;
				parameters[param_id] = param;
			}
			if (!parameters.is_empty()) {
				out["parameters"] = parameters;
			}
		} else if (effect->get_effect_type() == CCEffect::EffectType::BID_WAR) {
			Ref<CCEffectBidWar> bid_war = Object::cast_to<CCEffectBidWar>(effect.ptr());
			Dictionary param;
			param["type"] = "options";
			param["name"] = vformat("%s Options", bid_war->get_name());
			Dictionary options;
			for (int i = 0; i < bid_war->get_entry_count(); i++) {
				Dictionary option;
				option["name"] = bid_war->get_entry_name(i);
				options[bid_war->get_entry_id(i)] = option;
			}
			param["options"] = options;
			Dictionary parameters;
			parameters[vformat("%s_options", bid_war->get_id())] = param;
			out["parameters"] = parameters;
		}
		return out;
	}

	/// @brief Builds the map of effect ID to definition for a list of effects.
	/// @param custom_only only include effects flagged as custom effects
	static Dictionary effect_definitions(const Array &p_effects, bool custom_only = false) {
		Dictionary out;
		for (int i = 0; i < p_effects.size(); i++) {
			Ref<CCEffect> effect = Object::cast_to<CCEffect>(p_effects[i]);
			if (effect.is_null() || String(effect->get_id()).is_empty()) {
				continue;
			}
			if (custom_only && !effect->is_custom()) {
				continue;
			}
			out[String(effect->get_id())] = effect_definition(effect);
		}
		return out;
	}

	/// @brief Builds the complete Game Pack document.
	static Dictionary build(const String &p_game_name, const String &p_game_id, const Array &p_effects) {
		Dictionary meta;
		meta["name"] = p_game_name;
		meta["platform"] = "PC";
		// The connector for games talking to the service themselves, the name is a leftover of its first user
		meta["connector"] = Array::make("ExternalUnity");
		// Date of the last major change of the pack
		meta["releaseDate"] = Time::get_singleton()->get_date_string_from_system(true);
		meta["guide"] = vformat("https://crowdcontrol.live/guides/%s", p_game_id);

		Dictionary effects;
		effects["game"] = effect_definitions(p_effects);

		Dictionary out;
		out["format"] = 1;
		out["kind"] = "built-in";
		out["meta"] = meta;
		out["effects"] = effects;
		return out;
	}

private:
	static Array to_array(const PackedStringArray &p_strings) {
		Array out;
		for (int i = 0; i < p_strings.size(); i++) {
			out.append(p_strings[i]);
		}
		return out;
	}
};

#endif // CROWD_CONTROL_MANIFEST_HPP
