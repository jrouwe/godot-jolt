#include "jolt_shaped_object_impl_3d.hpp"

#include "shapes/jolt_custom_double_sided_shape.hpp"
#include "shapes/jolt_shape_impl_3d.hpp"
#include "spaces/jolt_space_3d.hpp"

JoltShapedObjectImpl3D::JoltShapedObjectImpl3D(ObjectType p_object_type)
	: JoltObjectImpl3D(p_object_type) {
	jolt_settings->mAllowSleeping = true;
	jolt_settings->mFriction = 1.0f;
	jolt_settings->mRestitution = 0.0f;
	jolt_settings->mLinearDamping = 0.0f;
	jolt_settings->mAngularDamping = 0.0f;
	jolt_settings->mGravityFactor = 0.0f;
}

JoltShapedObjectImpl3D::~JoltShapedObjectImpl3D() {
	delete_safely(jolt_settings);
}

Transform3D JoltShapedObjectImpl3D::get_transform_unscaled() const {
	if (!in_space()) {
		return {to_godot(jolt_settings->mRotation), to_godot(jolt_settings->mPosition)};
	}

	const JoltReadableBody3D body = space->read_body(jolt_id);
	ERR_FAIL_COND_D(body.is_invalid());

	return {to_godot(body->GetRotation()), to_godot(body->GetPosition())};
}

Transform3D JoltShapedObjectImpl3D::get_transform_scaled() const {
	return get_transform_unscaled().scaled_local(scale);
}

Basis JoltShapedObjectImpl3D::get_basis() const {
	if (!in_space()) {
		return to_godot(jolt_settings->mRotation);
	}

	const JoltReadableBody3D body = space->read_body(jolt_id);
	ERR_FAIL_COND_D(body.is_invalid());

	return to_godot(body->GetRotation());
}

Vector3 JoltShapedObjectImpl3D::get_position() const {
	if (!in_space()) {
		return to_godot(jolt_settings->mPosition);
	}

	const JoltReadableBody3D body = space->read_body(jolt_id);
	ERR_FAIL_COND_D(body.is_invalid());

	return to_godot(body->GetPosition());
}

Vector3 JoltShapedObjectImpl3D::get_center_of_mass() const {
	ERR_FAIL_NULL_D_MSG(
		space,
		vformat(
			"Failed to retrieve center-of-mass of '%s'. "
			"Doing so without a physics space is not supported by Godot Jolt. "
			"If this relates to a node, try adding the node to a scene tree first.",
			to_string()
		)
	);

	const JoltReadableBody3D body = space->read_body(jolt_id);
	ERR_FAIL_COND_D(body.is_invalid());

	return to_godot(body->GetCenterOfMassPosition());
}

Vector3 JoltShapedObjectImpl3D::get_center_of_mass_relative() const {
	return get_center_of_mass() - get_position();
}

Vector3 JoltShapedObjectImpl3D::get_center_of_mass_local() const {
	ERR_FAIL_NULL_D_MSG(
		space,
		vformat(
			"Failed to retrieve local center-of-mass of '%s'. "
			"Doing so without a physics space is not supported by Godot Jolt. "
			"If this relates to a node, try adding the node to a scene tree first.",
			to_string()
		)
	);

	return get_transform_scaled().xform_inv(get_center_of_mass());
}

Vector3 JoltShapedObjectImpl3D::get_linear_velocity() const {
	if (!in_space()) {
		return to_godot(jolt_settings->mLinearVelocity);
	}

	const JoltReadableBody3D body = space->read_body(jolt_id);
	ERR_FAIL_COND_D(body.is_invalid());

	return to_godot(body->GetLinearVelocity());
}

Vector3 JoltShapedObjectImpl3D::get_angular_velocity() const {
	if (!in_space()) {
		return to_godot(jolt_settings->mAngularVelocity);
	}

	const JoltReadableBody3D body = space->read_body(jolt_id);
	ERR_FAIL_COND_D(body.is_invalid());

	return to_godot(body->GetAngularVelocity());
}

AABB JoltShapedObjectImpl3D::get_aabb() const {
	AABB result;

	for (const JoltShapeInstance3D& shape : shapes) {
		if (shape.is_disabled()) {
			continue;
		}

		if (result == AABB()) {
			result = shape.get_aabb();
		} else {
			result.merge_with(shape.get_aabb());
		}
	}

	return get_transform_scaled().xform(result);
}

JPH::ShapeRefC JoltShapedObjectImpl3D::try_build_shape() {
	int32_t built_shapes = 0;

	for (JoltShapeInstance3D& shape : shapes) {
		if (shape.is_enabled() && shape.try_build()) {
			built_shapes += 1;
		}
	}

	QUIET_FAIL_COND_D(built_shapes == 0);

	JPH::ShapeRefC result = built_shapes == 1
		? _try_build_single_shape()
		: _try_build_compound_shape();

	QUIET_FAIL_NULL_D(result);

	if (has_custom_center_of_mass()) {
		result = JoltShapeImpl3D::with_center_of_mass(result, get_center_of_mass_custom());
	}

	if (scale != Vector3(1, 1, 1)) {
		Vector3 actual_scale = scale;

		ENSURE_SCALE_VALID(
			result,
			actual_scale,
			vformat("Failed to correctly scale body '%s'.", to_string())
		);

		result = JoltShapeImpl3D::with_scale(result, actual_scale);
	}

	if (is_area()) {
		result = JoltShapeImpl3D::with_double_sided(result, true);
	}

	return result;
}

JPH::ShapeRefC JoltShapedObjectImpl3D::build_shape() {
	JPH::ShapeRefC new_shape = try_build_shape();

	if (new_shape == nullptr) {
		if (has_custom_center_of_mass()) {
			new_shape = new JPH::EmptyShape(to_jolt(get_center_of_mass_custom()));
		} else {
			new_shape = new JPH::EmptyShape();
		}
	}

	return new_shape;
}

void JoltShapedObjectImpl3D::update_shape() {
	if (!in_space()) {
		_shapes_built();
		return;
	}

	const JoltWritableBody3D body = space->write_body(jolt_id);
	ERR_FAIL_COND(body.is_invalid());

	previous_jolt_shape = jolt_shape;
	jolt_shape = build_shape();

	if (jolt_shape == previous_jolt_shape) {
		return;
	}

	space->get_body_iface().SetShape(jolt_id, jolt_shape, false, JPH::EActivation::DontActivate);

	_shapes_built();
}

void JoltShapedObjectImpl3D::add_shape(
	JoltShapeImpl3D* p_shape,
	Transform3D p_transform,
	bool p_disabled
) {
	ENSURE_SCALE_NOT_ZERO(
		p_transform,
		vformat(
			"An invalid transform was passed when adding shape at index %d to physics body '%s'.",
			shapes.size(),
			to_string()
		)
	);

	Vector3 shape_scale;
	Math::decompose(p_transform, shape_scale);

	shapes.emplace_back(this, p_shape, p_transform, shape_scale, p_disabled);

	_shapes_changed();
}

void JoltShapedObjectImpl3D::remove_shape(const JoltShapeImpl3D* p_shape) {
	shapes.erase_if([&](const JoltShapeInstance3D& p_instance) {
		return p_instance.get_shape() == p_shape;
	});

	_shapes_changed();
}

void JoltShapedObjectImpl3D::remove_shape(int32_t p_index) {
	ERR_FAIL_INDEX(p_index, shapes.size());

	shapes.remove_at(p_index);

	_shapes_changed();
}

JoltShapeImpl3D* JoltShapedObjectImpl3D::get_shape(int32_t p_index) const {
	ERR_FAIL_INDEX_D(p_index, shapes.size());

	return shapes[p_index].get_shape();
}

void JoltShapedObjectImpl3D::set_shape(int32_t p_index, JoltShapeImpl3D* p_shape) {
	ERR_FAIL_INDEX(p_index, shapes.size());

	shapes[p_index] = JoltShapeInstance3D(this, p_shape);

	_shapes_changed();
}

void JoltShapedObjectImpl3D::clear_shapes() {
	shapes.clear();

	_shapes_changed();
}

int32_t JoltShapedObjectImpl3D::find_shape_index(uint32_t p_shape_instance_id) const {
	return shapes.find_if([&](const JoltShapeInstance3D& p_shape) {
		return p_shape.get_id() == p_shape_instance_id;
	});
}

int32_t JoltShapedObjectImpl3D::find_shape_index(const JPH::SubShapeID& p_sub_shape_id) const {
	ERR_FAIL_NULL_V(jolt_shape, -1);

	return find_shape_index((uint32_t)jolt_shape->GetSubShapeUserData(p_sub_shape_id));
}

JoltShapeImpl3D* JoltShapedObjectImpl3D::find_shape(uint32_t p_shape_instance_id) const {
	const int32_t shape_index = find_shape_index(p_shape_instance_id);
	return shape_index != -1 ? shapes[shape_index].get_shape() : nullptr;
}

JoltShapeImpl3D* JoltShapedObjectImpl3D::find_shape(const JPH::SubShapeID& p_sub_shape_id) const {
	const int32_t shape_index = find_shape_index(p_sub_shape_id);
	return shape_index != -1 ? shapes[shape_index].get_shape() : nullptr;
}

Transform3D JoltShapedObjectImpl3D::get_shape_transform_unscaled(int32_t p_index) const {
	ERR_FAIL_INDEX_D(p_index, shapes.size());

	return shapes[p_index].get_transform_unscaled();
}

Transform3D JoltShapedObjectImpl3D::get_shape_transform_scaled(int32_t p_index) const {
	ERR_FAIL_INDEX_D(p_index, shapes.size());

	return shapes[p_index].get_transform_scaled();
}

Vector3 JoltShapedObjectImpl3D::get_shape_scale(int32_t p_index) const {
	ERR_FAIL_INDEX_D(p_index, shapes.size());

	return shapes[p_index].get_scale();
}

void JoltShapedObjectImpl3D::set_shape_transform(int32_t p_index, Transform3D p_transform) {
	ERR_FAIL_INDEX(p_index, shapes.size());

	ENSURE_SCALE_NOT_ZERO(
		p_transform,
		vformat(
			"Failed to correctly set transform for shape at index %d in body '%s'.",
			p_index,
			to_string()
		)
	);

	Vector3 new_scale;
	Math::decompose(p_transform, new_scale);

	JoltShapeInstance3D& shape = shapes[p_index];

	if (shape.get_transform_unscaled() == p_transform && shape.get_scale() == new_scale) {
		return;
	}

	shape.set_transform(p_transform);
	shape.set_scale(new_scale);

	_shapes_changed();
}

bool JoltShapedObjectImpl3D::is_shape_disabled(int32_t p_index) const {
	ERR_FAIL_INDEX_D(p_index, shapes.size());

	return shapes[p_index].is_disabled();
}

void JoltShapedObjectImpl3D::set_shape_disabled(int32_t p_index, bool p_disabled) {
	ERR_FAIL_INDEX(p_index, shapes.size());

	JoltShapeInstance3D& shape = shapes[p_index];

	if (shape.is_disabled() == p_disabled) {
		return;
	}

	if (p_disabled) {
		shape.disable();
	} else {
		shape.enable();
	}

	_shapes_changed();
}

void JoltShapedObjectImpl3D::post_step(float p_step, JPH::Body& p_jolt_body) {
	JoltObjectImpl3D::post_step(p_step, p_jolt_body);

	previous_jolt_shape = nullptr;
}

bool JoltShapedObjectImpl3D::_is_big() const {
	// HACK(mihe): This number is completely arbitrary, and mostly just needs to capture any
	// `WorldBoundaryShape3D`. There could be a better sweet spot to be found here.
	return get_aabb().get_longest_axis_size() >= 1000.0f;
}

JPH::ShapeRefC JoltShapedObjectImpl3D::_try_build_single_shape() {
	// NOLINTNEXTLINE(modernize-loop-convert)
	for (int32_t shape_index = 0; shape_index < shapes.size(); ++shape_index) {
		const JoltShapeInstance3D& sub_shape = shapes[shape_index];

		if (!sub_shape.is_enabled() || !sub_shape.is_built()) {
			continue;
		}

		JPH::ShapeRefC jolt_sub_shape = sub_shape.get_jolt_ref();

		Vector3 sub_shape_scale = sub_shape.get_scale();
		const Transform3D sub_shape_transform = sub_shape.get_transform_unscaled();

		if (sub_shape_scale != Vector3(1, 1, 1)) {
			ENSURE_SCALE_VALID(
				jolt_sub_shape,
				sub_shape_scale,
				vformat(
					"Failed to correctly scale shape at index %d in body '%s'.",
					shape_index,
					to_string()
				)
			);

			jolt_sub_shape = JoltShapeImpl3D::with_scale(jolt_sub_shape, sub_shape_scale);
		}

		if (sub_shape_transform != Transform3D()) {
			jolt_sub_shape = JoltShapeImpl3D::with_basis_origin(
				jolt_sub_shape,
				sub_shape_transform.basis,
				sub_shape_transform.origin
			);
		}

		return jolt_sub_shape;
	}

	return {};
}

JPH::ShapeRefC JoltShapedObjectImpl3D::_try_build_compound_shape() {
	JPH::StaticCompoundShapeSettings compound_shape_settings;

	// NOLINTNEXTLINE(modernize-loop-convert)
	for (int32_t shape_index = 0; shape_index < shapes.size(); ++shape_index) {
		const JoltShapeInstance3D& sub_shape = shapes[shape_index];

		if (!sub_shape.is_enabled() || !sub_shape.is_built()) {
			continue;
		}

		JPH::ShapeRefC jolt_sub_shape = sub_shape.get_jolt_ref();

		Vector3 sub_shape_scale = sub_shape.get_scale();
		const Transform3D sub_shape_transform = sub_shape.get_transform_unscaled();

		if (sub_shape_scale != Vector3(1, 1, 1)) {
			ENSURE_SCALE_VALID(
				jolt_sub_shape,
				sub_shape_scale,
				vformat(
					"Failed to correctly scale shape at index %d in body '%s'.",
					shape_index,
					to_string()
				)
			);

			jolt_sub_shape = JoltShapeImpl3D::with_scale(jolt_sub_shape, sub_shape_scale);
		}

		compound_shape_settings.AddShape(
			to_jolt(sub_shape_transform.origin),
			to_jolt(sub_shape_transform.basis),
			jolt_sub_shape
		);
	}

	const JPH::ShapeSettings::ShapeResult shape_result = compound_shape_settings.Create();

	ERR_FAIL_COND_D_MSG(
		shape_result.HasError(),
		vformat(
			"Failed to create compound shape with sub-shape count '%d'. "
			"It returned the following error: '%s'.",
			(int32_t)compound_shape_settings.mSubShapes.size(),
			to_godot(shape_result.GetError())
		)
	);

	return shape_result.Get();
}

void JoltShapedObjectImpl3D::_shapes_changed() {
	update_shape();
	_update_object_layer();
}

void JoltShapedObjectImpl3D::_space_changing() {
	JoltObjectImpl3D::_space_changing();

	if (space != nullptr) {
		const JoltWritableBody3D body = space->write_body(jolt_id);
		ERR_FAIL_COND(body.is_invalid());

		jolt_settings = new JPH::BodyCreationSettings(body->GetBodyCreationSettings());
	}
}
