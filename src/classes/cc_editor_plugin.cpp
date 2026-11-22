#include "cc_editor_plugin.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/variant/callable.hpp>

#include "classes/cc_effect/cc_effect.hpp"

void CCEditorInspectorPlugin::_create_manifest(Ref<CCEffectEntries> p_entries) {
	ERR_FAIL_NULL_EDMSG(p_entries, "No effect entries loaded!");
	ERR_FAIL_NULL_EDMSG(manifest_info, "Unable to show manifest info!");
	String text = p_entries->get_manifest();
	manifest_text->set_text(text);
	DisplayServer::get_singleton()->clipboard_set(text);
	manifest_info->popup_centered();
}

void CCEditorInspectorPlugin::_save_manifest(Ref<CCEffectEntries> p_entries) {
	ERR_FAIL_NULL_EDMSG(p_entries, "No effect entries loaded!");
	ERR_FAIL_NULL_EDMSG(manifest_save_dialog, "Unable to show the save dialog!");
	pending_entries = p_entries;
	manifest_save_dialog->set_current_file("effect_pack.zip");
	manifest_save_dialog->popup_file_dialog();
}

void CCEditorInspectorPlugin::_write_manifest(const String &p_path) {
	Ref<CCEffectEntries> entries = pending_entries;
	pending_entries.unref();
	ERR_FAIL_NULL_EDMSG(entries, "No effect entries loaded!");
	if (p_path.get_extension().to_lower() == "json") {
		// Just the menu, e.g. to look at it or to zip it up by hand
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
		ERR_FAIL_COND_EDMSG(file.is_null(), vformat("Unable to write the menu JSON to '%s'!", p_path));
		file->store_string(entries->get_manifest());
		file->close();
		return;
	}
	entries->save_pack_zip(p_path);
}

bool CCEditorInspectorPlugin::_can_handle(Object *p_object) const {
	return Object::cast_to<CCEffectEntries>(p_object) != nullptr;
}

void CCEditorInspectorPlugin::_parse_end(Object *p_object) {
	Ref<CCEffectEntries> entries = Object::cast_to<CCEffectEntries>(p_object);
	if (entries != nullptr) {
		Label *label = memnew(Label);
		label->set_text("Actions");
		label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		add_custom_control(label);

		Button *button = memnew(Button);
		button->set_text("JSON Manifest");
		// Callable.bind is broken in 4.1 so we use bindv to have the array handle the Variants
		button->connect("pressed", Callable(this, "__on_manifest").bindv(Array::make(entries)));
		add_custom_control(button);

		// The zip to upload in the developer portal (or only its menu.json)
		Button *save_button = memnew(Button);
		save_button->set_text("Export Effect Pack...");
		save_button->connect("pressed", Callable(this, "__on_manifest_save").bindv(Array::make(entries)));
		add_custom_control(save_button);
		if (manifest_save_dialog == nullptr) {
			manifest_save_dialog = memnew(EditorFileDialog);
			manifest_save_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
			manifest_save_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
			manifest_save_dialog->add_filter("*.zip", "Effect pack for the developer portal");
			manifest_save_dialog->add_filter("*.json", "Menu JSON only");
			manifest_save_dialog->connect("file_selected", Callable(this, "__on_manifest_path_selected"));
			plugin->get_editor_interface()->get_base_control()->add_child(manifest_save_dialog);
		}
		if (manifest_info == nullptr) {
			manifest_info = memnew(AcceptDialog);
			manifest_info->set_min_size(Vector2i(500, 300));
			// add it to the editor root so we inherit the theme
			plugin->get_editor_interface()->get_base_control()->add_child(manifest_info);

			manifest_text = memnew(RichTextLabel);
			manifest_text->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
			manifest_text->set_selection_enabled(true);
			manifest_text->set_scroll_active(true);
			manifest_info->add_child(manifest_text, false, Node::INTERNAL_MODE_FRONT);
			manifest_info->move_child(manifest_text, 1);
		}
	}
}

void CCEditorInspectorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("__on_manifest", "entries"), &CCEditorInspectorPlugin::_create_manifest);
	ClassDB::bind_method(D_METHOD("__on_manifest_save", "entries"), &CCEditorInspectorPlugin::_save_manifest);
	ClassDB::bind_method(D_METHOD("__on_manifest_path_selected", "path"), &CCEditorInspectorPlugin::_write_manifest);
}

CCEditorInspectorPlugin::CCEditorInspectorPlugin(CCEditorPlugin *p_plugin) {
	plugin = p_plugin;
}

void CCEditorPlugin::_edit(Object *p_object) {
	if (!p_object) {
		// cleanup
		return;
	}
}

void CCEditorPlugin::_make_visible(bool p_visible) {
	if (p_visible) {
		// make visible
	}
}

bool CCEditorPlugin::_handles(Object *p_object) const {
	return p_object->is_class("CCEffectEntries");
}

CCEditorPlugin::CCEditorPlugin() {
	inspector_plugin = memnew(CCEditorInspectorPlugin(this));
	call_deferred("add_inspector_plugin", inspector_plugin);
}

CCEditorPlugin::~CCEditorPlugin() {
	call_deferred("remove_inspector_plugin", inspector_plugin);
}
