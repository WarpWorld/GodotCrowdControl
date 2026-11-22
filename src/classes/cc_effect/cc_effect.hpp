#ifndef CROWD_CONTROL_CC_EFFECT_HPP
#define CROWD_CONTROL_CC_EFFECT_HPP

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>

#include "classes/cc_effect_instance.hpp"
#include "classes/stream_user.hpp"
#include "util/binding_macros.hpp"

using namespace godot;

class CrowdControl;
class CCEffectInstance;

class CCEffect : public Resource {
	GDCLASS(CCEffect, Resource)

public:
	enum EffectType {
		NORMAL,
		TIMED,
		BID_WAR,
		PARAMETERS,
	};

	enum Morality {
		NEUTRAL,
		GOOD,
		EVIL,
	};

	enum Orderliness {
		ORDER_NEUTRAL,
		ORDERLY,
		CHAOTIC,
	};

	enum EffectResult {
		SUCCESS,
		FAILURE,
		UNAVAILABLE,
		RETRY,
		QUEUE,
		RUNNING,
	};

	enum EffectMenuStatus {
		MENU_UNAVAILABLE,
		MENU_AVAILABLE,
		MENU_VISIBLE,
		MENU_HIDDEN,
	};
	static const char *effect_menu_status_names[];

protected:
	static void _bind_methods();

private:
	bool pooling_disabled = false;
	bool sellable = true;
	bool visible = true;
	bool custom = false;
	Morality morality = NEUTRAL;
	Orderliness orderliness = ORDER_NEUTRAL;
	Ref<Texture2D> icon;
	Color icon_color = Color(1, 1, 1);
	StringName id;
	String display_name;
	String description;
	String note;
	int price = 10;
	int max_retries = 3;
	float retry_delay = 5.0f;
	float pending_delay = 0.5f;
	int session_max = 0;
	double delay_until = 0;
	PackedStringArray categories = PackedStringArray();
	PackedStringArray groups = PackedStringArray();
	PackedStringArray conflicts = PackedStringArray();

public:
	virtual EffectType get_effect_type() const { return NORMAL; }

	/* VIRTUAL CALLERS */
	/*
	 * Use these functions
	 */

	virtual bool can_run();
	virtual EffectResult trigger(Ref<CCEffectInstance> effect);

	GDVIRTUAL0RC(bool, _can_run)
	GDVIRTUAL1R(int, _trigger, Ref<CCEffectInstance>)

	bool is_pooling_disabled() const { return pooling_disabled; }
	void set_pooling_disabled(bool p_pooling_disabled);

	bool is_sellable() const { return sellable; }
	void set_sellable(bool p_sellable);

	bool is_visible() const { return visible; }
	void set_visible(bool p_visible);

	/// @brief Updates the menu state without reporting it, for when the report is sent in bulk.
	void set_menu_state_silent(bool p_visible, bool p_sellable) {
		visible = p_visible;
		sellable = p_sellable;
	}

	/// @brief Custom effects are not part of the submitted game pack, they are uploaded by the game
	/// through @see CrowdControl::upload_custom_effects (a development tool).
	bool is_custom() const { return custom; }
	void set_custom(bool p_custom) { custom = p_custom; }

	Morality get_morality() const { return morality; }
	void set_morality(Morality p_morality) { morality = p_morality; }
	/// @brief Morality as used by the game pack format: -1 (evil) to 1 (good).
	int get_morality_value() const { return morality == GOOD ? 1 : (morality == EVIL ? -1 : 0); }

	Orderliness get_orderliness() const { return orderliness; }
	void set_orderliness(Orderliness p_orderliness) { orderliness = p_orderliness; }
	/// @brief Orderliness as used by the game pack format: -1 (chaotic) to 1 (orderly).
	int get_orderliness_value() const { return orderliness == ORDERLY ? 1 : (orderliness == CHAOTIC ? -1 : 0); }

	Ref<Texture2D> get_icon() const { return icon; }
	void set_icon(const Ref<Texture2D> &p_icon) { icon = p_icon; }

	Color get_icon_color() const { return icon_color; }
	void set_icon_color(Color p_icon_color) { icon_color = p_icon_color; }

	StringName get_id() const { return id; }
	void set_id(const StringName &p_id) { id = p_id; }

	String get_display_name() const { return display_name; }
	void set_display_name(const String &p_display_name) { display_name = p_display_name; }

	String get_description() const { return description; }
	void set_description(const String &p_description) { description = p_description; }

	/// @brief Short text shown next to the name in the menu.
	String get_note() const { return note; }
	void set_note(const String &p_note) { note = p_note; }

	int get_price() const { return price; }
	void set_price(int p_price);

	int get_max_retries() const { return max_retries; }
	void set_max_retries(int p_max_retries) { max_retries = p_max_retries; }

	float get_retry_delay() const { return retry_delay; }
	void set_retry_delay(float p_retry_delay) { retry_delay = p_retry_delay; }

	float get_pending_delay() const { return pending_delay; }
	void set_pending_delay(float p_pending_delay) { pending_delay = p_pending_delay; }

	int get_session_max() const { return session_max; }
	void set_session_max(int p_session_max);

	double get_delay_until() const { return delay_until; }
	void set_delay_until(double p_delay_until) { delay_until = p_delay_until; }

	PackedStringArray get_categories() const { return categories; }
	void set_categories(const PackedStringArray &p_categories) { categories = p_categories; }

	/// @brief Internal collections of effects, not shown to viewers.
	PackedStringArray get_groups() const { return groups; }
	void set_groups(const PackedStringArray &p_groups) { groups = p_groups; }

	/// @brief IDs of effects this one can't run alongside. Requests are rejected while any of them is running.
	PackedStringArray get_conflicts() const { return conflicts; }
	void set_conflicts(const PackedStringArray &p_conflicts) { conflicts = p_conflicts; }

	virtual String get_name() const { return display_name; }

	/// @brief The definition of this effect in the game pack format, as used by the manifest and custom effects.
	Dictionary get_definition() const;

	CCEffect();
};

VARIANT_ENUM_CAST(CCEffect::EffectResult);
VARIANT_ENUM_CAST(CCEffect::Morality);
VARIANT_ENUM_CAST(CCEffect::Orderliness);

class CCEffectEntries : public Resource {
	GDCLASS(CCEffectEntries, Resource)

protected:
	static void _bind_methods();

private:
	Array effects = Array();

public:
	Array get_effects() const { return effects; }
	void set_effects(const Array &p_effects) { effects = p_effects; };

	/// @brief The game pack ("menu") JSON to submit to Crowd Control.
	String get_manifest() const;
	Dictionary get_manifest_data() const;
	/// @brief Writes the zip to upload in the developer portal: `menu.json` plus `assets/icons/<effectID>.png`
	/// for every effect that has an icon.
	Error save_pack_zip(const String &p_path) const;

private:
	/// @brief The effects that are part of the submitted pack: valid, unique IDs and not custom.
	Array get_pack_effects() const;
};

#endif // CROWD_CONTROL_CC_EFFECT_HPP