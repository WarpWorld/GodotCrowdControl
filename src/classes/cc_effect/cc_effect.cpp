#include "cc_effect.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/zip_packer.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "classes/cc_effect_instance.hpp"
#include "classes/crowd_control.hpp"
#include "json/effect_override.hpp"
#include "json/effect_request.hpp"
#include "json/manifest.hpp"
#include "util/binding_macros.hpp"

using namespace godot;

const char *CCEffect::effect_menu_status_names[] = {
	"menuUnavailable",
	"menuAvailable",
	"menuVisible",
	"menuHidden",
};

void CCEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_pooling_disabled", "disabled"), &CCEffect::set_pooling_disabled);
	ClassDB::bind_method(D_METHOD("is_pooling_disabled"), &CCEffect::is_pooling_disabled);

	ClassDB::bind_method(D_METHOD("set_sellable", "enable"), &CCEffect::set_sellable);
	ClassDB::bind_method(D_METHOD("is_sellable"), &CCEffect::is_sellable);

	ClassDB::bind_method(D_METHOD("set_visible", "enable"), &CCEffect::set_visible);
	ClassDB::bind_method(D_METHOD("is_visible"), &CCEffect::is_visible);

	ClassDB::bind_method(D_METHOD("set_morality", "morality"), &CCEffect::set_morality);
	ClassDB::bind_method(D_METHOD("get_morality"), &CCEffect::get_morality);

	ClassDB::bind_method(D_METHOD("set_icon", "texture"), &CCEffect::set_icon);
	ClassDB::bind_method(D_METHOD("get_icon"), &CCEffect::get_icon);

	ClassDB::bind_method(D_METHOD("set_icon_color", "color"), &CCEffect::set_icon_color);
	ClassDB::bind_method(D_METHOD("get_icon_color"), &CCEffect::get_icon_color);

	ClassDB::bind_method(D_METHOD("set_id", "id"), &CCEffect::set_id);
	ClassDB::bind_method(D_METHOD("get_id"), &CCEffect::get_id);

	ClassDB::bind_method(D_METHOD("set_display_name", "name"), &CCEffect::set_display_name);
	ClassDB::bind_method(D_METHOD("get_display_name"), &CCEffect::get_display_name);

	ClassDB::bind_method(D_METHOD("set_description", "text"), &CCEffect::set_description);
	ClassDB::bind_method(D_METHOD("get_description"), &CCEffect::get_description);

	ClassDB::bind_method(D_METHOD("set_price", "value"), &CCEffect::set_price);
	ClassDB::bind_method(D_METHOD("get_price"), &CCEffect::get_price);

	ClassDB::bind_method(D_METHOD("set_max_retries", "value"), &CCEffect::set_max_retries);
	ClassDB::bind_method(D_METHOD("get_max_retries"), &CCEffect::get_max_retries);

	ClassDB::bind_method(D_METHOD("set_retry_delay", "delay"), &CCEffect::set_retry_delay);
	ClassDB::bind_method(D_METHOD("get_retry_delay"), &CCEffect::get_retry_delay);

	ClassDB::bind_method(D_METHOD("set_pending_delay", "delay"), &CCEffect::set_pending_delay);
	ClassDB::bind_method(D_METHOD("get_pending_delay"), &CCEffect::get_pending_delay);

	ClassDB::bind_method(D_METHOD("set_session_max", "max"), &CCEffect::set_session_max);
	ClassDB::bind_method(D_METHOD("get_session_max"), &CCEffect::get_session_max);

	ClassDB::bind_method(D_METHOD("set_categories", "categories"), &CCEffect::set_categories);
	ClassDB::bind_method(D_METHOD("get_categories"), &CCEffect::get_categories);

	ClassDB::bind_method(D_METHOD("set_groups", "groups"), &CCEffect::set_groups);
	ClassDB::bind_method(D_METHOD("get_groups"), &CCEffect::get_groups);

	ClassDB::bind_method(D_METHOD("set_conflicts", "effect_ids"), &CCEffect::set_conflicts);
	ClassDB::bind_method(D_METHOD("get_conflicts"), &CCEffect::get_conflicts);

	ClassDB::bind_method(D_METHOD("set_custom", "enable"), &CCEffect::set_custom);
	ClassDB::bind_method(D_METHOD("is_custom"), &CCEffect::is_custom);

	ClassDB::bind_method(D_METHOD("set_orderliness", "orderliness"), &CCEffect::set_orderliness);
	ClassDB::bind_method(D_METHOD("get_orderliness"), &CCEffect::get_orderliness);

	ClassDB::bind_method(D_METHOD("set_note", "text"), &CCEffect::set_note);
	ClassDB::bind_method(D_METHOD("get_note"), &CCEffect::get_note);

	ClassDB::bind_method(D_METHOD("get_definition"), &CCEffect::get_definition);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "pooling_disabled"), "set_pooling_disabled", "is_pooling_disabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sellable"), "set_sellable", "is_sellable");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "visible"), "set_visible", "is_visible");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "custom"), "set_custom", "is_custom");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "morality", PROPERTY_HINT_ENUM, "Neutral,Good,Evil"), "set_morality", "get_morality");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "orderliness", PROPERTY_HINT_ENUM, "Neutral,Orderly,Chaotic"), "set_orderliness", "get_orderliness");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "icon", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_icon", "get_icon");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "icon_color"), "set_icon_color", "get_icon_color");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "id"), "set_id", "get_id");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name"), "set_display_name", "get_display_name");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "description", PROPERTY_HINT_MULTILINE_TEXT), "set_description", "get_description");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "note"), "set_note", "get_note");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "price", PROPERTY_HINT_RANGE, "1,100000,1"), "set_price", "get_price");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_retries", PROPERTY_HINT_RANGE, "0,60,1"), "set_max_retries", "get_max_retries");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "retry_delay", PROPERTY_HINT_RANGE, "0,10,0.001f"), "set_retry_delay", "get_retry_delay");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pending_delay", PROPERTY_HINT_RANGE, "0,10,0.001f"), "set_pending_delay", "get_pending_delay");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "session_max"), "set_session_max", "get_session_max");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "categories"), "set_categories", "get_categories");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "groups"), "set_groups", "get_groups");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "conflicts"), "set_conflicts", "get_conflicts");

	GDVIRTUAL_BIND(_can_run)
	GDVIRTUAL_BIND(_trigger, "effect")

	BIND_ENUM_CONSTANT(SUCCESS);
	BIND_ENUM_CONSTANT(FAILURE);
	BIND_ENUM_CONSTANT(UNAVAILABLE);
	BIND_ENUM_CONSTANT(RETRY);
	BIND_ENUM_CONSTANT(QUEUE);
	BIND_ENUM_CONSTANT(RUNNING);

	BIND_ENUM_CONSTANT(NEUTRAL);
	BIND_ENUM_CONSTANT(GOOD);
	BIND_ENUM_CONSTANT(EVIL);

	BIND_ENUM_CONSTANT(ORDER_NEUTRAL);
	BIND_ENUM_CONSTANT(ORDERLY);
	BIND_ENUM_CONSTANT(CHAOTIC);
}

Dictionary CCEffect::get_definition() const {
	return Manifest::effect_definition(Ref<CCEffect>(const_cast<CCEffect *>(this)));
}

bool CCEffect::can_run() {
	bool res;
	if (GDVIRTUAL_CALL(_can_run, res)) {
		return res;
	}
	return true;
}

CCEffect::EffectResult CCEffect::trigger(Ref<CCEffectInstance> effect) {
	int res = UNAVAILABLE;
	if (!GDVIRTUAL_CALL(_trigger, effect, res)) {
		CrowdControl::get_singleton()->log_error(vformat("Effect '%s' does not implement _trigger()!", get_id()));
		return UNAVAILABLE;
	}
	return (EffectResult)res;
};

void CCEffect::set_pooling_disabled(bool p_pooling_disabled) {
	pooling_disabled = p_pooling_disabled;
	CrowdControl::get_singleton()->post_effect_update(JSONSerializer::serialize_string<EffectChangeNonPoolable>(EffectChangeNonPoolable(id, pooling_disabled)));
}

void CCEffect::set_sellable(bool p_sellable) {
	sellable = p_sellable;
	if (sellable) {
		CrowdControl::get_singleton()->update_effect_status(this, effect_menu_status_names[MENU_AVAILABLE]);
	} else {
		CrowdControl::get_singleton()->update_effect_status(this, effect_menu_status_names[MENU_UNAVAILABLE]);
	}
}

void CCEffect::set_visible(bool p_visible) {
	visible = p_visible;
	if (visible) {
		CrowdControl::get_singleton()->update_effect_status(this, effect_menu_status_names[MENU_VISIBLE]);
	} else {
		CrowdControl::get_singleton()->update_effect_status(this, effect_menu_status_names[MENU_HIDDEN]);
	}
}

void CCEffect::set_price(int p_price) {
	if (p_price < 1) {
		CrowdControl::get_singleton()->log_error(vformat("Price for effect '%s' must be greater or equal 1!", get_id()));
		return;
	}
	price = p_price;
	CrowdControl::get_singleton()->post_effect_update(JSONSerializer::serialize_string<EffectChangePrice>(EffectChangePrice(id, price)));
}

void CCEffect::set_session_max(int p_session_max) {
	session_max = p_session_max;
	CrowdControl::get_singleton()->post_effect_update(JSONSerializer::serialize_string<EffectChangeSessionMax>(EffectChangeSessionMax(id, p_session_max)));
}

CCEffect::CCEffect() {
	//CrowdControl::get_singleton()->register_effect(Ref<CCEffect>(this);
}

void CCEffectEntries::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_effects", "effects"), &CCEffectEntries::set_effects);
	ClassDB::bind_method(D_METHOD("get_effects"), &CCEffectEntries::get_effects);
	ClassDB::bind_method(D_METHOD("get_manifest"), &CCEffectEntries::get_manifest);
	ClassDB::bind_method(D_METHOD("get_manifest_data"), &CCEffectEntries::get_manifest_data);
	ClassDB::bind_method(D_METHOD("save_pack_zip", "path"), &CCEffectEntries::save_pack_zip);

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "effects", PROPERTY_HINT_TYPE_STRING, vformat("%d/%d:CCEffect", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE)), "set_effects", "get_effects");
}

String CCEffectEntries::get_manifest() const {
	return JSON::stringify(get_manifest_data(), "\t", false);
}

Dictionary CCEffectEntries::get_manifest_data() const {
	String game_id = GLOBAL_GET("crowd_control/common/game_id");
	// Game pack IDs double as identifiers on the service
	bool valid_id = !game_id.is_empty() && !(game_id[0] >= '0' && game_id[0] <= '9');
	for (int i = 0; i < game_id.length() && valid_id; i++) {
		const char32_t c = game_id[i];
		valid_id = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
	}
	if (!valid_id) {
		UtilityFunctions::push_warning(vformat("[CC] The game ID '%s' is not a valid game pack ID, it may only contain letters, digits and underscores and must not start with a digit.", game_id));
	}
	return Manifest::build(GLOBAL_GET("crowd_control/common/game_name"), game_id, get_pack_effects());
}

Array CCEffectEntries::get_pack_effects() const {
	Array valid_effects;
	Dictionary seen;
	for (int i = 0; i < effects.size(); i++) {
		Ref<CCEffect> effect = Object::cast_to<CCEffect>(effects[i]);
		ERR_CONTINUE_EDMSG(effect == nullptr, vformat("Invalid value '%s' in effect entries!", UtilityFunctions::str(effects[i])));
		const String effect_id = effect->get_id();
		ERR_CONTINUE_EDMSG(effect_id.is_empty(), vformat("Effect '%s' has no ID and was left out of the manifest!", effect->get_name()));
		// The same rule the developer portal applies to uploads
		bool valid_id = effect_id.length() <= 100;
		for (int j = 0; j < effect_id.length() && valid_id; j++) {
			const char32_t c = effect_id[j];
			valid_id = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
		}
		ERR_CONTINUE_EDMSG(!valid_id, vformat("Effect ID '%s' is not valid and was left out of the manifest! IDs can only contain letters, numbers and underscores (100 at most).", effect_id));
		ERR_CONTINUE_EDMSG(seen.has(effect_id), vformat("Duplicate effect ID '%s', only the first one is part of the manifest!", effect_id));
		seen[effect_id] = true;
		if (effect->is_custom()) {
			// Custom effects are uploaded by the game, they are not part of the submitted pack
			continue;
		}
		if (effect->get_description().length() > 512) {
			UtilityFunctions::push_warning(vformat("[CC] The description of effect '%s' is longer than 512 characters, the developer portal will reject it.", effect_id));
		}
		valid_effects.append(effect);
	}
	return valid_effects;
}

Error CCEffectEntries::save_pack_zip(const String &p_path) const {
	// Limits of the developer portal for effect icons
	const int max_icon_size = 512;
	const int max_icon_bytes = 256 * 1024;

	Array pack_effects = get_pack_effects();
	ERR_FAIL_COND_V_EDMSG(pack_effects.is_empty(), ERR_INVALID_DATA, "There are no effects to export!");

	Ref<ZIPPacker> zip;
	zip.instantiate();
	Error err = zip->open(p_path);
	ERR_FAIL_COND_V_EDMSG(err != OK, err, vformat("Unable to write the effect pack to '%s'!", p_path));

	zip->start_file("menu.json");
	zip->write_file(get_manifest().to_utf8_buffer());
	zip->close_file();

	for (int i = 0; i < pack_effects.size(); i++) {
		Ref<CCEffect> effect = pack_effects[i];
		Ref<Texture2D> icon = effect->get_icon();
		if (icon.is_null()) {
			continue;
		}
		Ref<Image> image = icon->get_image();
		if (image.is_null() || image->is_empty()) {
			continue;
		}
		// Work on a copy, the image may be shared with the texture
		image = image->duplicate();
		if (image->is_compressed() && image->decompress() != OK) {
			UtilityFunctions::push_warning(vformat("[CC] The icon of effect '%s' could not be read and was left out.", effect->get_id()));
			continue;
		}
		if (image->get_width() > max_icon_size || image->get_height() > max_icon_size) {
			const float scale = (float)max_icon_size / MAX(image->get_width(), image->get_height());
			image->resize(MAX(1, (int)(image->get_width() * scale)), MAX(1, (int)(image->get_height() * scale)), Image::INTERPOLATE_LANCZOS);
		}
		PackedByteArray png = image->save_png_to_buffer();
		if (png.is_empty() || png.size() > max_icon_bytes) {
			UtilityFunctions::push_warning(vformat("[CC] The icon of effect '%s' is larger than 256 KB as a PNG and was left out.", effect->get_id()));
			continue;
		}
		// An icon named after its effect is picked up without being referenced in menu.json
		zip->start_file(vformat("assets/icons/%s.png", effect->get_id()));
		zip->write_file(png);
		zip->close_file();
	}
	return zip->close();
}
