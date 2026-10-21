/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <core/CameraSettings.h>
#include <mod_core/UnityMath.h>
#include <nlohmann/json.hpp>

namespace moa_mod::camera
{

/// The settings of the fade of the objects near the camera.
struct FadeSettings
{
	bool enabled = true;
	float opacity = 0.3f;   ///< The opacity of an object near the camera, 0 to 1.
	float seconds = 0.25f;  ///< The time of the fade out from 1 to `opacity`, and of the fade in.
	float distance = 4.0f;  ///< An object nearer to the camera than this distance fades, metres.
	bool hideInside = true; ///< An object that the camera is in is hidden.
};

/// The fade settings of the camera settings.
[[nodiscard]] inline FadeSettings FadeSettingsOf(const CameraSettings& settings)
{
	return {.enabled = settings.enabled && settings.fadeOccluders,
	        .opacity = static_cast<float>(settings.fadeAlpha),
	        .seconds = static_cast<float>(settings.fadeSeconds),
	        .distance = static_cast<float>(settings.fadeDistance),
	        .hideInside = settings.hideCameraInside};
}

/// How the object in the hand shows now, for the tests.
struct HeldObjectLook
{
	bool held = false;                  ///< The hand holds an object.
	std::string name;                   ///< The name of the game object in the hand.
	int renderers = 0;                  ///< The enabled mesh renderers of the object.
	int shadowOnly = 0;                 ///< The renderers that draw only their shadow.
	std::optional<float> lowestOpacity; ///< The smallest value of `_AlphaMultiply` of the materials. Nothing: none.
	bool changedByFade = false;         ///< The fade changes the object now.
};

/// What the fade examines in a frame.
struct FadeView
{
	WorldPoint camera; ///< The camera that the user sees.
};

/// The objects of the game for the fade. The research mod implements it on the running game; the tests implement it
/// with a model. An object is the root of a building, a tree, a rock or a cliff, with all its renderers.
class FadeScene
{
public:
	virtual ~FadeScene() = default;
	/// The fade starts a frame. The scene can forget what it found in the last frame and did not change.
	virtual void BeginFrame() {}
	/// The objects that a sphere of `radius` at `point` is in or touches, each one time. An identifier is never zero.
	/// Nothing if the game cannot do the test.
	[[nodiscard]] virtual std::optional<std::vector<uint64_t>> ObjectsAt(WorldPoint point, float radius) = 0;
	/// Show an object with an opacity (1 is solid), or hide it. A hidden object keeps its shadow. Return false if the
	/// object is gone or cannot change.
	[[nodiscard]] virtual bool ShowObject(uint64_t object, float opacity, bool hidden) = 0;
	/// Put the object back as the game had it, and forget it.
	virtual void RestoreObject(uint64_t object) = 0;
	/// True if the hand holds the object, or if the object is a part of the hand. The fade never changes such an
	/// object. The scene can read the hand at `BeginFrame`.
	[[nodiscard]] virtual bool Held(uint64_t /*object*/) { return false; }
};

/// The fade of one object.
struct FadeObjectState
{
	float opacity = 1.0f;
	bool hidden = false;
	bool nearCamera = false;           ///< The object is nearer to the camera than the fade distance in this frame.
	bool inside = false;               ///< The camera is in the object in this frame.
	float clearSeconds = 0.0f;         ///< The time since the object was last near the camera.
	std::optional<float> shownOpacity; ///< The opacity that the scene shows now. Nothing before the first change.
	bool shownHidden = false;          ///< The scene hides the object now.
};

/// The fade of the objects near the camera. In each frame it finds the objects that are nearer to the camera than the
/// fade distance, and the objects that the camera is in. An object that is only between the camera and the hand does
/// not fade. Each object fades out to the opacity of the settings, and fades in when it is no longer near. An object that the
/// camera is in is hidden at once. The change of the opacity is the same for each second, so the fade is the same at each frame
/// rate. When an object is solid again, the fade gives it back to the game. The fade never changes an object that the hand
/// holds: when the hand takes a changed object, the object goes back to the game at once.
class OccluderFade
{
public:
	/// The radius of the sphere at the camera that finds the objects that the camera is in, metres.
	static constexpr float k_InsideRadius = 0.3f;
	/// An object fades in only after it is clear for this time, so that it does not flicker, seconds.
	static constexpr float k_ClearDelay = 0.15f;
	/// The fade changes at most this count of objects.
	static constexpr size_t k_MostObjects = 24;

	/// Run the fade for one frame of `deltaTime` seconds.
	void Step(FadeScene& scene, const FadeSettings& settings, const FadeView& view, float deltaTime);
	/// Give each object back to the game. Call it at a stand-back, at a change of the settings, before the removal of
	/// the hooks and at the quit.
	void RestoreAll(FadeScene& scene);

	/// The count of the objects that are see-through and not hidden.
	[[nodiscard]] int Faded() const;
	/// The count of the hidden objects.
	[[nodiscard]] int Hidden() const;
	/// The smallest opacity of the objects that the fade changed. Nothing if there are none.
	[[nodiscard]] std::optional<float> LowestOpacity() const;
	[[nodiscard]] bool Empty() const { return _objects.empty(); }
	[[nodiscard]] const std::map<uint64_t, FadeObjectState>& Objects() const { return _objects; }
	/// True if a test of the scene failed in the last frame.
	[[nodiscard]] bool LookupFailed() const { return _lookupFailed; }
	/// Describe the fade, for the query `camera.state`.
	[[nodiscard]] nlohmann::json State() const;

private:
	/// Add each object of a test to the fade, and mark it. An object that the hand holds is not added.
	void Mark(FadeScene& scene, const std::optional<std::vector<uint64_t>>& found, bool inside);
	/// Give back at once each changed object that the hand holds now.
	void ReleaseHeld(FadeScene& scene);

	std::map<uint64_t, FadeObjectState> _objects;
	bool _lookupFailed = false;
};

} // namespace moa_mod::camera
