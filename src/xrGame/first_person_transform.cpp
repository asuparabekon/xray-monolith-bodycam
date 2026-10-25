#include "pch_script.h"
#include "first_person_transform.h"

#include "Actor.h"
#include "Inventory.h"
#include "Level.h"
#include "Weapon.h"
#include "firedeps.h"
#include "player_hud.h"
#include "../xrEngine/CameraBase.h"
#include "../xrEngine/CameraManager.h"
#include "../xrEngine/device.h"

using namespace luabind;

namespace FirstPerson
{
namespace
{
enum class Source : u8
{
	Invalid,
	AimCamera,
	VisualCamera,
	BaseCamera,
	CameraDelta,
	ViewmodelDelta,
	HudRoot,
	HudItem,
	HudBone,
	HudFirePoint,
	HudFirePoint2,
	HudShellPoint,
	WeaponFirePoint,
	WeaponFirePoint2,
	WeaponShellPoint,
	Sight,
};

struct Pose
{
	Fmatrix transform = Fidentity;
	u32 frame = 0;
	u32 generation = 0;
	bool valid = false;
};

struct ScriptPose
{
	Fvector position = { 0.f, 0.f, 0.f };
	Fvector right = { 1.f, 0.f, 0.f };
	Fvector up = { 0.f, 1.f, 0.f };
	Fvector forward = { 0.f, 0.f, 1.f };
	u32 frame = 0;
	u32 generation = 0;
};

struct SourceRef
{
	Source source = Source::Invalid;
	s16 slot = -1;
	shared_str bone;
};

struct HudPoseSet
{
	Pose root;
	Pose item;
	Pose fire_point;
	Pose fire_point2;
	Pose shell_point;
};

// The HUD holds a primary item, an offhand item and a sight; only the first
// two have a root transform of their own.
constexpr s16 kHudSlots = 3;
constexpr s16 kHudRootSlots = 2;

struct ProviderState
{
	Pose visual_camera;
	Pose base_camera;
	Pose aim_camera;
	Pose camera_delta;
	Pose viewmodel_delta;
	HudPoseSet hud[kHudSlots];
	u32 generation = 1;
};

ProviderState g_providers;

bool IsRecent(const Pose& pose)
{
	return pose.valid && Device.dwFrame - pose.frame <= 1;
}

void SetPose(Pose& pose, const Fmatrix& transform, u32 generation)
{
	pose.transform = transform;
	pose.frame = Device.dwFrame;
	pose.generation = generation;
	pose.valid = _valid(transform);
}

void InvalidatePose(Pose& pose, u32 generation)
{
	pose = Pose();
	pose.frame = Device.dwFrame;
	pose.generation = generation;
}

Fmatrix MakeRayPose(const Fvector& origin, const Fvector& direction)
{
	Fmatrix pose;
	pose.identity();
	pose.c = origin;
	pose.k = direction;
	pose.k.normalize_safe();
	Fvector::generate_orthonormal_basis_normalized(pose.k, pose.j, pose.i);
	return pose;
}

struct SourceInfo
{
	LPCSTR name;
	Source source;
	s16 slots; // HUD slots the source exists in; 0 for a source that takes no slot.
};

const SourceInfo kSources[] = {
	{ "camera_aim", Source::AimCamera, 0 },
	{ "camera_visual", Source::VisualCamera, 0 },
	{ "camera_base", Source::BaseCamera, 0 },
	{ "camera_delta", Source::CameraDelta, 0 },
	{ "viewmodel_delta", Source::ViewmodelDelta, 0 },
	{ "hud_root", Source::HudRoot, kHudRootSlots },
	{ "hud_item", Source::HudItem, kHudSlots },
	{ "hud_bone", Source::HudBone, kHudSlots },
	{ "hud_firepoint", Source::HudFirePoint, kHudSlots },
	{ "hud_firepoint2", Source::HudFirePoint2, kHudSlots },
	{ "hud_shellpoint", Source::HudShellPoint, kHudSlots },
	{ "weapon_firepoint", Source::WeaponFirePoint, 0 },
	{ "weapon_firepoint2", Source::WeaponFirePoint2, 0 },
	{ "weapon_shellpoint", Source::WeaponShellPoint, 0 },
	{ "sight", Source::Sight, 0 },
};

Source ParseSource(LPCSTR name)
{
	if (name)
		for (const SourceInfo& info : kSources)
			if (!xr_strcmp(name, info.name))
				return info.source;
	return Source::Invalid;
}

bool ValidSourceRef(const SourceRef& ref)
{
	for (const SourceInfo& info : kSources)
		if (info.source == ref.source)
			return (!info.slots || (ref.slot >= 0 && ref.slot < info.slots)) && (ref.source != Source::HudBone || ref.bone.size());
	return false;
}

CWeapon* ActiveWeapon()
{
	CActor* actor = Bodycam::CurrentActor();
	return actor && actor->inventory().ActiveItem() ? actor->inventory().ActiveItem()->cast_weapon() : nullptr;
}

bool ResolveHudBone(const SourceRef& ref, Pose& pose)
{
	if (!g_player_hud || !IsRecent(g_providers.hud[ref.slot].item))
		return false;

	attachable_hud_item* item = g_player_hud->attached_item(ref.slot);
	if (!item || !item->m_model)
		return false;

	const u16 bone_id = item->m_model->LL_BoneID(ref.bone);
	if (bone_id == BI_NONE)
		return false;

	Fmatrix transform;
	transform.mul_43(item->m_item_transform, item->m_model->LL_GetTransform(bone_id));
	SetPose(pose, transform, g_providers.hud[ref.slot].item.generation);
	return pose.valid;
}

bool ResolveHudPoint(Source source, s16 slot, Pose& pose)
{
	if (!g_player_hud || !IsRecent(g_providers.hud[slot].item))
		return false;

	HudPoseSet& set = g_providers.hud[slot];
	Pose* requested = nullptr;
	switch (source)
	{
	case Source::HudFirePoint: requested = &set.fire_point; break;
	case Source::HudFirePoint2: requested = &set.fire_point2; break;
	case Source::HudShellPoint: requested = &set.shell_point; break;
	default: return false;
	}
	if (requested->frame == Device.dwFrame)
	{
		pose = *requested;
		return pose.valid;
	}

	attachable_hud_item* item = g_player_hud->attached_item(slot);
	if (!item || !item->m_model)
		return false;

	firedeps deps;
	item->setup_firedeps(deps);
	if (item->m_measures.m_prop_flags.test(hud_item_measures::e_fire_point))
		SetPose(set.fire_point, MakeRayPose(deps.vLastFP, deps.vLastFD), g_providers.generation);
	else
		InvalidatePose(set.fire_point, g_providers.generation);
	if (item->m_measures.m_prop_flags.test(hud_item_measures::e_fire_point2))
		SetPose(set.fire_point2, MakeRayPose(deps.vLastFP2, deps.vLastFD), g_providers.generation);
	else
		InvalidatePose(set.fire_point2, g_providers.generation);
	if (item->m_measures.m_prop_flags.test(hud_item_measures::e_shell_point))
		SetPose(set.shell_point, MakeRayPose(deps.vLastSP, deps.vLastFD), g_providers.generation);
	else
		InvalidatePose(set.shell_point, g_providers.generation);

	pose = *requested;
	return pose.valid;
}

bool ResolveWeaponPoint(Source source, Pose& pose)
{
	CWeapon* weapon = ActiveWeapon();
	if (!weapon)
		return false;

	Fvector origin;
	switch (source)
	{
	case Source::WeaponFirePoint: origin = weapon->get_CurrentFirePoint(); break;
	case Source::WeaponFirePoint2: origin = weapon->get_CurrentFirePoint2(); break;
	case Source::WeaponShellPoint: origin = weapon->get_LastSP(); break;
	default: return false;
	}

	SetPose(pose, MakeRayPose(origin, weapon->get_LastFD()), g_providers.generation);
	return pose.valid;
}

bool Resolve(const SourceRef& ref, Pose& pose)
{
	pose = Pose();
	if (!ValidSourceRef(ref))
		return false;

	switch (ref.source)
	{
	case Source::AimCamera: pose = g_providers.aim_camera; break;
	case Source::VisualCamera: pose = g_providers.visual_camera; break;
	case Source::BaseCamera: pose = g_providers.base_camera; break;
	case Source::CameraDelta: pose = g_providers.camera_delta; break;
	case Source::ViewmodelDelta: pose = g_providers.viewmodel_delta; break;
	case Source::HudRoot: pose = g_providers.hud[ref.slot].root; break;
	case Source::HudItem: pose = g_providers.hud[ref.slot].item; break;
	case Source::HudFirePoint:
	case Source::HudFirePoint2:
	case Source::HudShellPoint: return ResolveHudPoint(ref.source, ref.slot, pose);
	case Source::HudBone: return ResolveHudBone(ref, pose);
	case Source::WeaponFirePoint:
	case Source::WeaponFirePoint2:
	case Source::WeaponShellPoint: return ResolveWeaponPoint(ref.source, pose);
	case Source::Sight:
		pose = IsRecent(g_providers.hud[SCOPE_ATTACH_IDX].item) ? g_providers.hud[SCOPE_ATTACH_IDX].item : g_providers.hud[0].item;
		break;
	default: return false;
	}
	return IsRecent(pose);
}

bool ReadPose(LPCSTR source, int slot, LPCSTR bone, ScriptPose& output)
{
	SourceRef ref;
	ref.source = ParseSource(source);
	ref.slot = static_cast<s16>(slot);
	ref.bone = bone ? bone : "";
	Pose pose;
	if (!Resolve(ref, pose))
		return false;
	output.position = pose.transform.c;
	output.right = pose.transform.i;
	output.up = pose.transform.j;
	output.forward = pose.transform.k;
	output.frame = pose.frame;
	output.generation = pose.generation;
	return true;
}
} // namespace

void PublishCamera()
{
	CActor* actor = Bodycam::CurrentActor();
	if (!actor || !actor->HUDview())
	{
		InvalidatePose(g_providers.aim_camera, g_providers.generation);
		InvalidatePose(g_providers.visual_camera, g_providers.generation);
		InvalidatePose(g_providers.base_camera, g_providers.generation);
		InvalidatePose(g_providers.camera_delta, g_providers.generation);
		return;
	}

	Fmatrix visual;
	visual.set(Device.vCameraRight, Device.vCameraTop, Device.vCameraDirection, Device.vCameraPosition);
	SetPose(g_providers.visual_camera, visual, g_providers.generation);

	if (CCameraBase* camera = actor->cam_FirstEye())
	{
		Fmatrix aim;
		aim.set(camera->Right(), camera->Up(), camera->Direction(), camera->vPosition);
		SetPose(g_providers.aim_camera, aim, g_providers.generation);
	}
	else
	{
		g_providers.aim_camera = Pose();
	}

	Fmatrix base;
	base.set(
		Level().Cameras().BaseRight(), Level().Cameras().BaseUp(), Level().Cameras().BaseDirection(), Level().Cameras().BasePosition());
	SetPose(g_providers.base_camera, base, g_providers.generation);

	Fmatrix base_inverse;
	base_inverse.invert(base);
	Fmatrix delta;
	delta.mul_43(base_inverse, visual);
	SetPose(g_providers.camera_delta, delta, g_providers.generation);
}

void PublishHud(player_hud& hud)
{
	CActor* actor = Bodycam::CurrentActor();
	if (!actor || !actor->HUDview())
	{
		for (HudPoseSet& set : g_providers.hud)
			set = HudPoseSet();
		g_providers.viewmodel_delta = Pose();
		return;
	}

	Fvector viewmodel_pos, viewmodel_rot;
	if (actor->cam_BodycamGetHudOffset(viewmodel_pos, viewmodel_rot, hud.BodycamLoweringInfluence()))
	{
		Fmatrix delta;
		delta.setHPB(deg2rad(viewmodel_rot.x), deg2rad(viewmodel_rot.y), deg2rad(viewmodel_rot.z));
		delta.c = viewmodel_pos;
		SetPose(g_providers.viewmodel_delta, delta, g_providers.generation);
	}
	else
	{
		Fmatrix identity;
		identity.identity();
		SetPose(g_providers.viewmodel_delta, identity, g_providers.generation);
	}

	for (s16 slot = 0; slot < kHudSlots; ++slot)
	{
		HudPoseSet& set = g_providers.hud[slot];
		if (slot < kHudRootSlots)
			SetPose(set.root, slot == 0 ? hud.m_transform : hud.m_transform_2, g_providers.generation);

		attachable_hud_item* item = hud.attached_item(slot);
		if (!item || !item->m_model)
		{
			set.item = set.fire_point = set.fire_point2 = set.shell_point = Pose();
			continue;
		}

		SetPose(set.item, item->m_item_transform, g_providers.generation);
		// Point poses are computed lazily after this HUD transform is published.
		set.fire_point = set.fire_point2 = set.shell_point = Pose();
	}
}

void Reset()
{
	const u32 generation = g_providers.generation + 1;
	g_providers = ProviderState();
	g_providers.generation = generation;
}

#pragma optimize("s", on)
void script_register(lua_State* state)
{
	module(state, "first_person")[class_<ScriptPose>("pose")
									  .def(constructor<>())
									  .def_readonly("position", &ScriptPose::position)
									  .def_readonly("right", &ScriptPose::right)
									  .def_readonly("up", &ScriptPose::up)
									  .def_readonly("forward", &ScriptPose::forward)
									  .def_readonly("frame", &ScriptPose::frame)
									  .def_readonly("generation", &ScriptPose::generation),
		def("get_pose", &ReadPose)];
}
} // namespace FirstPerson
