#ifndef CROWD_CONTROL_CC_EFFECT_INSTANCE_HPP
#define CROWD_CONTROL_CC_EFFECT_INSTANCE_HPP

#include <godot_cpp/classes/ref_counted.hpp>

#include "json/effect_request.hpp"
#include "util/binding_macros.hpp"

class StreamUser;
class CCEffect;
class CCEffectTimed;

using namespace godot;

class CCEffectInstance : public RefCounted {
	GDCLASS(CCEffectInstance, RefCounted)

protected:
	static void _bind_methods();

private:
	bool test = false;
	bool local = false;
	Ref<CCEffect> effect;
	Ref<StreamUser> user;
	String id;
	int retry_count = 5;
	// Unix timestamps need double precision, a float can only resolve them to ~2 minutes.
	double start_time = -1;
	double queue_time = -1;

public:
	bool is_test() const { return test; }
	void set_test(bool p_test) { test = p_test; }

	/// @brief Whether this instance was created locally (@see CrowdControl::test_effect) instead of by a
	/// request from the Crowd Control service. Local instances are never reported back to the service.
	bool is_local() const { return local; }
	void set_local(bool p_local) { local = p_local; }

	Ref<CCEffect> get_effect() const { return effect; }
	void set_effect(const Ref<CCEffect> &p_effect) { effect = p_effect; }

	Ref<StreamUser> get_user() const { return user; }
	void set_user(const Ref<StreamUser> &p_user) { user = p_user; }

	String get_id() const { return id; }
	void set_id(const String &p_id) { id = p_id; }

	int get_retry_count() const { return retry_count; }
	void set_retry_count(int p_retry_count) { retry_count = p_retry_count; }

	double get_start_time() const { return start_time; }
	void set_start_time(double p_start_time) { start_time = p_start_time; }

	/// @brief Unix time at which the request was received, used to answer requests before they time out.
	double get_queue_time() const { return queue_time; }
	void set_queue_time(double p_queue_time) { queue_time = p_queue_time; }

	StringName get_effect_id() const;
};

class CCEffectInstanceTimed : public CCEffectInstance {
	GDCLASS(CCEffectInstanceTimed, CCEffectInstance)

protected:
	static void _bind_methods();

private:
	bool active = true;
	double end_time = -1;
	float duration = 0;
	float time_left = 0;

public:
	bool is_active() const { return active; }
	void set_active(bool p_active) { active = p_active; }

	Ref<CCEffectTimed> get_effect_timed() const;
	void set_effect_timed(const Ref<CCEffectTimed> &p_effect_timed);
	double get_end_time() const { return end_time; }
	void set_end_time(const double &p_end_time) { end_time = p_end_time; }

	/// @brief The duration in seconds this instance was requested with.
	/// @details
	/// Streamers can change the duration of an effect in their menu, so this may differ
	/// from @see CCEffectTimed::get_duration.
	float get_duration() const;
	void set_duration(const float &p_duration) { duration = p_duration; }

	float get_time_left() const { return time_left; }
	void set_time_left(const float &p_time_left) { time_left = p_time_left; }

	bool is_paused() const;

	void set_paused(bool paused);

	bool should_be_running() const;
};

class CCEffectInstanceBidWar : public CCEffectInstance {
	GDCLASS(CCEffectInstanceBidWar, CCEffectInstance)

protected:
	static void _bind_methods();

private:
	String bid_id;

public:
	void set_bid_id(const String &p_bid_id) { bid_id = p_bid_id; }
	String get_bid_id() const { return bid_id; }

	uint32_t get_bid_amount() const;

	String get_bid_name() const;
};

class CCEffectInstanceParameters : public CCEffectInstance {
	GDCLASS(CCEffectInstanceParameters, CCEffectInstance)

private:
	HashMap<String, ParameterEntry> parameters = HashMap<String, ParameterEntry>();

protected:
	static void _bind_methods();

public:
	Variant get_parameter(const String &p_param) const;
	PackedStringArray get_parameter_names() const;
	String get_option_name(const String &p_param) const;
	String get_parameter_title(const String &p_param) const;
	void assign_parameters(const HashMap<String, ParameterEntry> &p_params);
};

#endif // CROWD_CONTROL_CC_EFFECT_INSTANCE_HPP