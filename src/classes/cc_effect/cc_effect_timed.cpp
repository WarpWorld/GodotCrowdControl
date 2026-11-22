#include "cc_effect_timed.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "classes/cc_effect/cc_effect.hpp"
#include "classes/crowd_control.hpp"

void CCEffectTimed::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_duration", "secs"), &CCEffectTimed::set_duration);
	ClassDB::bind_method(D_METHOD("get_duration"), &CCEffectTimed::get_duration);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "duration"), "set_duration", "get_duration");

	GDVIRTUAL_BIND(_start, "effect")
	GDVIRTUAL_BIND(_stop, "effect", "force")
	GDVIRTUAL_BIND(_pause)
	GDVIRTUAL_BIND(_resume)
	GDVIRTUAL_BIND(_reset)
	GDVIRTUAL_BIND(_should_be_running)
}

CCEffect::EffectResult CCEffectTimed::trigger(Ref<CCEffectInstance> effect) {
	paused = false;
	Ref<CCEffectInstanceTimed> timed = Object::cast_to<CCEffectInstanceTimed>(effect.ptr());
	// The request decides how long the effect runs, the resource only provides the default.
	const float requested_duration = timed->get_duration();
	timed->set_end_time(effect->get_start_time() + requested_duration);
	timed->set_time_left(requested_duration);
	Variant res = start(timed);
	if ((int)res != 0) {
		return (EffectResult)(int)res;
	}
	return RUNNING;
}

CCEffect::EffectResult CCEffectTimed::start(Ref<CCEffectInstanceTimed> effect) {
	EffectResult res = SUCCESS;
	if (!GDVIRTUAL_CALL(_start, effect, res)) {
		CrowdControl::get_singleton()->log_error(vformat("Timed effect '%s' does not implement _start()!", get_id()));
		return UNAVAILABLE;
	}
	return res;
}

bool CCEffectTimed::stop(Ref<CCEffectInstanceTimed> effect, bool force) {
	bool res = false;
	GDVIRTUAL_CALL(_stop, effect, force, res);
	if (!res && force) {
		CrowdControl::get_singleton()->log_error(vformat("Effect '%s' wasn't stopped, but 'force==true'!", get_id()));
	}
	return res;
}

void CCEffectTimed::pause(Ref<CCEffectInstanceTimed> effect) {
	paused = true;
	GDVIRTUAL_CALL(_pause);
}

void CCEffectTimed::resume(Ref<CCEffectInstanceTimed> effect) {
	paused = false;
	effect->set_time_left(effect->get_time_left() + (1.0 / Engine::get_singleton()->get_frames_per_second()));
	GDVIRTUAL_CALL(_resume);
}

void CCEffectTimed::reset(Ref<CCEffectInstanceTimed> effect) {
	paused = false;
	effect->set_time_left(effect->get_duration());
	GDVIRTUAL_CALL(_reset);
}

bool CCEffectTimed::should_be_running() const {
	if (paused) {
		return false;
	}
	bool res;
	if (GDVIRTUAL_CALL(_should_be_running, res)) {
		return res;
	}
	return true;
}