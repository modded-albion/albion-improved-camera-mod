/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <common/Result.h>
#include <core/CameraPitch.h>
#include <mod_core/SettingsFields.h>
#include <nlohmann/json.hpp>

namespace moa_mod::camera
{

/// The settings of the camera mod: the object `camera` of mods.json. The camera turns about the hand, and the zoom and
/// the turn are smooth. A missing field keeps its default.
struct CameraSettings
{
	bool enabled = false;
	bool pivotOnHand = true;               ///< Turn the camera about the hand point, not about the focus.
	bool smoothZoom = true;                ///< The mouse wheel moves a target stage; the stage follows it smoothly.
	double zoomSmoothing = 0.3;            ///< Seconds for the zoom to get most of the way to its target.
	double rotateSmoothing = 0.0;          ///< Seconds for the turn to get most of the way to its target. 0 is off.
	double wheelZoomStep = 0.5;            ///< Zoom stages for each notch of the mouse wheel.
	bool invertRotate = false;             ///< Turn about the hand in the opposite direction (the sign of the turn).
	bool lookUp = false;                   ///< A vertical drag with the middle mouse button turns the view up near the ground.
	double maxLookUpDegrees = 15.0;        ///< The highest angle of the view centre above the horizon.
	double pitchSensitivity = 10.0;        ///< Degrees of look up for each 100 pixels of vertical drag.
	double lookUpFullStage = 0.5;          ///< At this zoom stage and below, the full look up is permitted.
	double lookUpZeroStage = 1.5;          ///< At this zoom stage and above, no look up is permitted.
	bool invertPitch = false;              ///< A drag down turns the view up.
	bool pitch = false;                    ///< A vertical drag with the middle mouse button changes the tilt at each zoom.
	double minTiltDegrees = 5.0;           ///< The flattest tilt of the pitch, degrees below the horizon.
	double maxTiltDegrees = 85.0;          ///< The steepest tilt of the pitch, degrees below the horizon.
	double pitchSmoothing = 0.1;           ///< Seconds for the pitch to get most of the way to its target. 0 is off.
	bool pitchResetOnZoom = false;         ///< A zoom puts the tilt of the game back.
	bool resetPitchOnDoubleClick = true;   ///< A double click of the middle mouse button puts the tilt of the game back.
	double groundClearance = 1.5;          ///< Metres above the land height of the game that a pitched camera keeps.
	PitchMode pitchMode = PitchMode::Hand; ///< How the pitch moves the camera: "hand", "inPlace" or "orbit".
	bool pitchTurnLock = true;             ///< A middle button drag that is mostly vertical does not turn the camera.
	bool lockCursor = true; ///< While the middle button is held, the hand rig keeps the cursor and the hand on the pivot.
	/// The largest distance from the camera to the pivot of a turn or a pitch, as a multiple of the distance from
	/// the camera to the focus of the game at the zoom stage. A hand that is farther gives a pivot at this
	/// distance on the line to the hand. 0 is no limit.
	double maxPivotDistance = 3.0;
	/// The hand rig keeps the camera in front of the land (and of the objects, with `objectCollision`) on the line from the
	/// pivot to the camera. It moves the camera nearer to the pivot on that line, and back out when the line is clear.
	bool springArm = true;
	double collisionInSmoothing = 0.05; ///< Seconds for the camera to move in front of an obstacle. 0 is at once.
	double collisionOutSmoothing = 0.3; ///< Seconds for the camera to move back out when the line is clear.
	double collisionMargin = 0.3;       ///< Metres between the sphere of the camera and the first obstacle.
	/// The spring arm and the walk on the land also stop in front of the objects (buildings, trees, rocks). Off, only
	/// the land stops the camera, and the mod does no sphere test of the objects.
	bool objectCollision = false;
	/// The largest count of the points where the spring arm examines the land between the pivot and the camera.
	double armLandSamples = 256.0;
	/// The radius of the ball of points around the camera that keeps the camera out of the land, metres. 0 is off.
	double cameraClearanceRadius = 2.0;
	double terrainPushSmoothing = 0.1;   ///< Seconds for the camera to move out of the land. 0 is at once.
	double terrainReturnSmoothing = 0.4; ///< Seconds for the camera to move back when the land permits. 0 is at once.
	/// After the game moved the camera while the mod stood back (a blend, a cutscene), the camera flies back to the
	/// view that the user had before. Off, the camera stays at the view of the game.
	bool flyBackAfterStandBack = false;
	double dragInertia = 0.35;         ///< Seconds. The time constant of the glide after the land drag. 0 is off.
	double dragInertiaMaxSpeed = 15.0; ///< The highest speed of the glide, in camera heights for each second.
	double turnInertia = 0.25;         ///< Seconds. The time constant of the glide after a turn of the hand rig. 0 is off.
	double pitchInertia = 0.25;        ///< Seconds. The time constant of the glide after a pitch of the hand rig. 0 is off.
	bool continuousZoom = true;        ///< Each notch of the wheel changes the height by `zoomStepRatio`, with no stage steps.
	double zoomStepRatio = 1.25;       ///< The change of the height of the camera for each notch of the wheel.
	/// The key pan and the edge scroll, in view widths for each second at the move speed 1 of the game. 0 keeps the
	/// pan speed of the game, which changes with the height.
	double panSpeed = 0.45;
	bool edgeScroll = true; ///< The camera scrolls when the cursor is at the edge of the screen during a drag.
	/// The width of the soft edge of the map, in view widths. In it, the key pan, the edge scroll, the glide and a
	/// flight slow down and stop at the limit of the game. 0 is the hard stop of the game. Only the hand rig.
	double edgeMargin = 0.25;
	/// With the spring arm, the camera rises over the land smoothly (fast up, slowly down) and never goes into the
	/// land, for example in a key pan or an edge scroll onto a hill. Off, it rises at once. Only the hand rig.
	bool panAvoidance = true;
	bool flyTo = true;         ///< A double click on the land flies the camera to that point. Only the hand rig.
	double flyToSeconds = 0.6; ///< The duration of a flight: a double click, a camera spot or the reset of the view.
	double flyToStage = -1.0;  ///< The zoom stage at the end of a double click flight. Less than 0 keeps the zoom.
	/// Below the closest zoom stage, the zoom of the hand rig continues down to the eye height above the land, and the
	/// view becomes level. There, the camera turns about itself and the key pan walks on the land. Only the hand rig.
	bool groundLook = true;
	double groundEyeHeight = 1.8;         ///< Metres above the land at the bottom of the ground range.
	double groundPitchDownDegrees = 60.0; ///< On the land, the largest angle of the view centre below the horizon.
	double groundPitchUpDegrees = 60.0;   ///< On the land, the largest angle of the view centre above the horizon.
	double groundWalkSpeed = 4.0;         ///< The key pan on the land, metres for each second at the move speed 1.
	/// The hand rig moves the camera that the user sees after the camera of the land drag, the key pan, the edge
	/// scroll, the glide and the clamp into the map, with a spring that keeps the speed. This is the time in which
	/// the camera covers 95 percent of a sudden move, seconds. 0 is off. Only the hand rig.
	double chaseSeconds = 0.3;
	/// The highest speed of the camera in that chase: the larger of this value in distances to the focus for each
	/// second and `chaseMinSpeed` in metres for each second. It stops a leap; a normal move is slower.
	double chaseMaxSpeed = 6.0;
	double chaseMinSpeed = 150.0;
	/// The hand rig drags the land with the right button itself: the land under the cursor at the press stays under
	/// the cursor, on a plane through that point, from the camera at the press. Off, the land drag of the game.
	bool landGrip = true;
	/// The land drag moves the land at most this part of the distance from the camera to the focus for each pixel of
	/// the cursor, but always `gripMinimumStep` metres. So a cursor near the horizon does not move the land far.
	double gripStepPerPixel = 0.011;
	double gripMinimumStep = 1.0;
	/// Seconds. A short smoothing of the turn and the pitch input of the hand rig, before the glides. 0 is off.
	double mouseSmoothing = 0.03;
	/// The objects (buildings, trees, rocks, cliffs) very near the camera become see-through. An object in the hand
	/// never fades.
	bool fadeOccluders = true;
	double fadeAlpha = 0.3;       ///< The opacity of an object near the camera, 0 to 1.
	double fadeSeconds = 0.25;    ///< The time of the fade out, and of the fade in when the object is no longer near.
	double fadeDistance = 4.0;    ///< Metres. An object nearer to the camera than this distance fades.
	bool hideCameraInside = true; ///< An object that the camera is in is hidden while the camera is in it.
	bool trace = false;           ///< Write a CSV trace of each frame while the user holds a mouse button.
	std::string traceFolder;      ///< The folder of the trace files. Empty is the folder of the logs of the mod.
	/// The heading of the north in the world, clockwise from +z, degrees. The reset of the view puts it at the top.
	double northDegrees = 0.0;

	friend bool operator==(const CameraSettings&, const CameraSettings&) = default;
};

/// Read the object `camera` of mods.json. Return an error if a field has a wrong type or a value is out of range.
[[nodiscard]] moa::Result<CameraSettings> ParseCameraSettings(const nlohmann::json& object);

/// All the settings as the object `camera` of mods.json, in the form that `ParseCameraSettings` reads.
[[nodiscard]] nlohmann::json CameraSettingsObject(const CameraSettings& settings);

/// What new settings change in the running camera mod.
struct CameraSettingsChange
{
	bool enabled = false; ///< `enabled` changed.
	bool camera = false;  ///< A value other than `enabled`, `trace` and `traceFolder` changed.
	bool trace = false;   ///< `trace` or `traceFolder` changed.

	[[nodiscard]] bool Any() const { return enabled || camera || trace; }
};

[[nodiscard]] CameraSettingsChange CompareCameraSettings(const CameraSettings& current, const CameraSettings& next);

/// A short text with the main values, one value on each line, for a message on the screen.
[[nodiscard]] std::string CameraSummary(const CameraSettings& settings);

/// What a hotkey or a button of the camera mod does.
enum class CameraAction
{
	ToggleCamera,
	CyclePitchMode,
	ToggleCursorLock,
	ToggleTrace,
	SaveSpot1,
	SaveSpot2,
	SaveSpot3,
	SaveSpot4,
	GoToSpot1,
	GoToSpot2,
	GoToSpot3,
	GoToSpot4,
	ResetView,
};

constexpr std::array k_CameraActions {
    CameraAction::ToggleCamera, CameraAction::CyclePitchMode, CameraAction::ToggleCursorLock, CameraAction::ToggleTrace,
    CameraAction::SaveSpot1,    CameraAction::SaveSpot2,      CameraAction::SaveSpot3,        CameraAction::SaveSpot4,
    CameraAction::GoToSpot1,    CameraAction::GoToSpot2,      CameraAction::GoToSpot3,        CameraAction::GoToSpot4,
    CameraAction::ResetView,
};

/// The name of the action in the object `hotkeys` of mods.json, for example "toggleCamera".
[[nodiscard]] std::string_view CameraActionName(CameraAction action);

/// The hotkey of an action when mods.json does not give one: Ctrl+Shift with F5, F7, F8 and F9 for the settings, Ctrl
/// with F1 to F4 to save a camera spot, F1 to F4 to fly to it, and F5 to reset the view.
[[nodiscard]] std::string_view CameraActionDefaultHotkey(CameraAction action);

/// The camera spot of an action that saves a spot, from 0. Nothing for another action.
[[nodiscard]] std::optional<size_t> SaveSpotIndex(CameraAction action);

/// The camera spot of an action that flies to a spot, from 0. Nothing for another action.
[[nodiscard]] std::optional<size_t> GoToSpotIndex(CameraAction action);

/// Change the settings for an action. Return nothing for the actions that do not change the settings (the camera
/// spots and the reset of the view).
[[nodiscard]] std::optional<settings::SettingsEdit> EditForAction(CameraSettings& settings, CameraAction action);

/// The fields of the camera menu, in the order of the menu.
[[nodiscard]] std::span<const settings::Field<CameraSettings>> CameraFields();

/// The range of a number field for the current settings. Some limits depend on another field: the full look up
/// stage stays below the zero stage, and the lowest tilt stays below the highest tilt.
[[nodiscard]] settings::FieldRange CameraRangeOf(const settings::Field<CameraSettings>& field, const CameraSettings& settings);

/// Change one field. Return the change of mods.json and a message, or an error and keep `settings`.
[[nodiscard]] moa::Result<settings::SettingsEdit>
EditCameraField(CameraSettings& settings, const settings::Field<CameraSettings>& field, const settings::FieldValue& value);

} // namespace moa_mod::camera
