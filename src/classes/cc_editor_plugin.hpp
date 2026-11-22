#ifndef CROWD_CONTROL_CC_EDITOR_PLUGIN
#define CROWD_CONTROL_CC_EDITOR_PLUGIN

#include <godot_cpp/classes/accept_dialog.hpp>
#include <godot_cpp/classes/editor_file_dialog.hpp>
#include <godot_cpp/classes/editor_inspector_plugin.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>

#include "classes/cc_effect/cc_effect.hpp"

class CCEditorPlugin;

using namespace godot;

class CCEditorInspectorPlugin : public EditorInspectorPlugin {
	GDCLASS(CCEditorInspectorPlugin, EditorInspectorPlugin)

private:
	CCEditorPlugin *plugin;

	AcceptDialog *manifest_info = nullptr;
	RichTextLabel *manifest_text = nullptr;
	EditorFileDialog *manifest_save_dialog = nullptr;
	// The effects waiting to be written by the save dialog
	Ref<CCEffectEntries> pending_entries;

	void _create_manifest(Ref<CCEffectEntries> p_entries);
	void _save_manifest(Ref<CCEffectEntries> p_entries);
	void _write_manifest(const String &p_path);
	// Default constructor needed for GDCLASS
	CCEditorInspectorPlugin(){};

protected:
	static void _bind_methods();

public:
	bool _can_handle(Object *p_object) const override;
	void _parse_end(Object *p_object) override;

	CCEditorInspectorPlugin(CCEditorPlugin *p_plugin);
};

class CCEditorPlugin : public EditorPlugin {
	GDCLASS(CCEditorPlugin, EditorPlugin)

private:
	CCEditorInspectorPlugin *inspector_plugin;

protected:
	static void _bind_methods(){};

public:
	void _edit(Object *p_object) override;
	void _make_visible(bool p_visible) override;
	bool _handles(Object *p_object) const override;
	String _get_plugin_name() const override { return "Crowd Control"; }

	CCEditorPlugin();
	~CCEditorPlugin();
};

#endif // CROWD_CONTROL_CC_EDITOR_PLUGIN