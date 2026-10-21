/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <format>

#include <moa_mod/camera/CameraSettings.h>

namespace moa_mod::camera
{

using settings::Field;
using settings::FieldKind;
using settings::ReadField;

namespace
{
/// The space between the two values of a pair whose order the settings file checks.
constexpr double k_StageGap = 0.05;
constexpr double k_TiltGap = 1.0;

/// The names of the pitch modes in mods.json, in the order of the enum.
constexpr std::array<std::string_view, 3> k_PitchModeNames {"hand", "inPlace", "orbit"};

[[nodiscard]] const char* OnOff(bool value)
{
	return value ? "on" : "off";
}

[[nodiscard]] PitchMode NextPitchMode(PitchMode mode)
{
	switch (mode)
	{
	case PitchMode::Hand:
		return PitchMode::InPlace;
	case PitchMode::InPlace:
		return PitchMode::Orbit;
	case PitchMode::Orbit:
		return PitchMode::Hand;
	}
	return PitchMode::Hand;
}

// The accessors of each field. A lambda with no capture becomes a plain function pointer.
#define MOA_TOGGLE(path) \
	.getToggle = [](const CameraSettings& s) { return s.path; }, .setToggle = [](CameraSettings& s, bool v) { s.path = v; }
#define MOA_NUMBER(path) \
	.getNumber = [](const CameraSettings& s) { return s.path; }, .setNumber = [](CameraSettings& s, double v) { s.path = v; }

const std::array k_Fields {
    Field<CameraSettings> {.group = "Camera",
                           .section = "camera",
                           .key = "enabled",
                           .label = "Camera mod on",
                           .help = "The camera turns about the hand, and the zoom and the turn are smooth.",
                           MOA_TOGGLE(enabled)},
    Field<CameraSettings> {.group = "Camera",
                           .section = "camera",
                           .key = "pitchMode",
                           .label = "Pitch mode",
                           .help = "hand: turn about the hand point. inPlace: the camera stays. orbit: turn about the focus.",
                           .kind = FieldKind::Choice,
                           .choices = k_PitchModeNames,
                           .getChoice = [](const CameraSettings& s) { return static_cast<size_t>(s.pitchMode); },
                           .setChoice = [](CameraSettings& s, size_t v) { s.pitchMode = static_cast<PitchMode>(v); }},
    Field<CameraSettings> {.group = "Camera",
                           .section = "camera",
                           .key = "pivotOnHand",
                           .label = "Turn about the hand",
                           .help = "Turn the camera about the hand point, not about the focus.",
                           MOA_TOGGLE(pivotOnHand)},
    Field<CameraSettings> {.group = "Camera",
                           .section = "camera",
                           .key = "lockCursor",
                           .label = "Lock the cursor",
                           .help = "While the middle button is held, the cursor stays on the pivot.",
                           MOA_TOGGLE(lockCursor)},
    Field<CameraSettings> {.group = "Camera",
                           .section = "camera",
                           .key = "maxPivotDistance",
                           .label = "Max pivot distance",
                           .help = "A multiple of the distance to the focus. 0 is no limit.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 100.0,
                           .step = 0.1,
                           .format = "%.1f",
                           MOA_NUMBER(maxPivotDistance)},
    Field<CameraSettings> {.group = "Zoom",
                           .section = "camera",
                           .key = "smoothZoom",
                           .label = "Smooth zoom",
                           .help = "The mouse wheel moves a target; the zoom follows it smoothly.",
                           MOA_TOGGLE(smoothZoom)},
    Field<CameraSettings> {.group = "Zoom",
                           .section = "camera",
                           .key = "zoomSmoothing",
                           .label = "Zoom smoothing",
                           .help = "Seconds for the zoom to get most of the way to its target.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 5.0,
                           .step = 0.01,
                           .format = "%.2f s",
                           MOA_NUMBER(zoomSmoothing)},
    Field<CameraSettings> {.group = "Zoom",
                           .section = "camera",
                           .key = "wheelZoomStep",
                           .label = "Wheel zoom step",
                           .help = "Zoom stages for each notch of the mouse wheel.",
                           .kind = FieldKind::Number,
                           .min = 0.01,
                           .max = 4.0,
                           .step = 0.01,
                           .format = "%.2f",
                           MOA_NUMBER(wheelZoomStep)},
    Field<CameraSettings> {.group = "Zoom",
                           .section = "camera",
                           .key = "continuousZoom",
                           .label = "Continuous zoom",
                           .help = "Each notch of the wheel changes the height by the zoom ratio, with no stage steps.",
                           MOA_TOGGLE(continuousZoom)},
    Field<CameraSettings> {.group = "Zoom",
                           .section = "camera",
                           .key = "zoomStepRatio",
                           .label = "Zoom ratio",
                           .help = "The change of the height of the camera for each notch of the wheel.",
                           .kind = FieldKind::Number,
                           .min = 1.01,
                           .max = 4.0,
                           .step = 0.01,
                           .format = "x%.2f",
                           MOA_NUMBER(zoomStepRatio)},
    Field<CameraSettings> {.group = "Turn",
                           .section = "camera",
                           .key = "rotateSmoothing",
                           .label = "Turn smoothing",
                           .help = "Seconds for the turn to get most of the way to its target. 0 is off.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 5.0,
                           .step = 0.01,
                           .format = "%.2f s",
                           MOA_NUMBER(rotateSmoothing)},
    Field<CameraSettings> {.group = "Turn",
                           .section = "camera",
                           .key = "invertRotate",
                           .label = "Invert the turn",
                           .help = "Turn about the hand in the opposite direction.",
                           MOA_TOGGLE(invertRotate)},
    Field<CameraSettings> {.group = "Pitch",
                           .section = "camera",
                           .key = "pitch",
                           .label = "Pitch on",
                           .help = "A vertical drag with the middle button changes the tilt at each zoom.",
                           MOA_TOGGLE(pitch)},
    Field<CameraSettings> {.group = "Pitch",
                           .section = "camera",
                           .key = "minTiltDegrees",
                           .label = "Lowest tilt",
                           .help = "The flattest tilt, in degrees below the horizon.",
                           .kind = FieldKind::Number,
                           .min = 1.0,
                           .max = 89.0,
                           .step = 1.0,
                           .format = "%.0f deg",
                           MOA_NUMBER(minTiltDegrees)},
    Field<CameraSettings> {.group = "Pitch",
                           .section = "camera",
                           .key = "maxTiltDegrees",
                           .label = "Highest tilt",
                           .help = "The steepest tilt, in degrees below the horizon.",
                           .kind = FieldKind::Number,
                           .min = 1.0,
                           .max = 89.0,
                           .step = 1.0,
                           .format = "%.0f deg",
                           MOA_NUMBER(maxTiltDegrees)},
    Field<CameraSettings> {.group = "Pitch",
                           .section = "camera",
                           .key = "pitchSensitivity",
                           .label = "Pitch sensitivity",
                           .help = "Degrees for each 100 pixels of vertical drag.",
                           .kind = FieldKind::Number,
                           .min = 0.1,
                           .max = 100.0,
                           .step = 0.1,
                           .format = "%.1f",
                           .logarithmic = true,
                           MOA_NUMBER(pitchSensitivity)},
    Field<CameraSettings> {.group = "Pitch",
                           .section = "camera",
                           .key = "invertPitch",
                           .label = "Invert the pitch",
                           .help = "A drag down turns the view up.",
                           MOA_TOGGLE(invertPitch)},
    Field<CameraSettings> {.group = "Pitch",
                           .section = "camera",
                           .key = "pitchSmoothing",
                           .label = "Pitch smoothing",
                           .help = "Seconds for the pitch to get most of the way to its target. 0 is off.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 5.0,
                           .step = 0.01,
                           .format = "%.2f s",
                           MOA_NUMBER(pitchSmoothing)},
    Field<CameraSettings> {.group = "Pitch",
                           .section = "camera",
                           .key = "pitchTurnLock",
                           .label = "Pitch turn lock",
                           .help = "A drag that is mostly vertical does not turn the camera.",
                           MOA_TOGGLE(pitchTurnLock)},
    Field<CameraSettings> {.group = "Pitch",
                           .section = "camera",
                           .key = "pitchResetOnZoom",
                           .label = "Reset the pitch on zoom",
                           .help = "A zoom puts the tilt of the game back.",
                           MOA_TOGGLE(pitchResetOnZoom)},
    Field<CameraSettings> {.group = "Pitch",
                           .section = "camera",
                           .key = "resetPitchOnDoubleClick",
                           .label = "Reset the pitch on double click",
                           .help = "A double click of the middle button puts the tilt of the game back.",
                           MOA_TOGGLE(resetPitchOnDoubleClick)},
    Field<CameraSettings> {.group = "Pitch",
                           .section = "camera",
                           .key = "groundClearance",
                           .label = "Ground clearance",
                           .help = "Metres above the land that a pitched camera keeps.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 20.0,
                           .step = 0.1,
                           .format = "%.1f m",
                           MOA_NUMBER(groundClearance)},
    Field<CameraSettings> {.group = "Look up",
                           .section = "camera",
                           .key = "lookUp",
                           .label = "Look up on",
                           .help = "A vertical drag turns the view up near the ground.",
                           MOA_TOGGLE(lookUp)},
    Field<CameraSettings> {.group = "Look up",
                           .section = "camera",
                           .key = "maxLookUpDegrees",
                           .label = "Max look up",
                           .help = "The highest angle of the view centre above the horizon.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 60.0,
                           .step = 1.0,
                           .format = "%.0f deg",
                           MOA_NUMBER(maxLookUpDegrees)},
    Field<CameraSettings> {.group = "Look up",
                           .section = "camera",
                           .key = "lookUpFullStage",
                           .label = "Full look up stage",
                           .help = "At this zoom stage and below, the full look up is permitted.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 5.0,
                           .step = 0.05,
                           .format = "%.2f",
                           MOA_NUMBER(lookUpFullStage)},
    Field<CameraSettings> {.group = "Look up",
                           .section = "camera",
                           .key = "lookUpZeroStage",
                           .label = "No look up stage",
                           .help = "At this zoom stage and above, no look up is permitted.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 5.0,
                           .step = 0.05,
                           .format = "%.2f",
                           MOA_NUMBER(lookUpZeroStage)},
    Field<CameraSettings> {.group = "Glide",
                           .section = "camera",
                           .key = "dragInertia",
                           .label = "Glide time",
                           .help = "Seconds. The time constant of the glide after a land drag. 0 is off.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 5.0,
                           .step = 0.01,
                           .format = "%.2f s",
                           MOA_NUMBER(dragInertia)},
    Field<CameraSettings> {.group = "Glide",
                           .section = "camera",
                           .key = "dragInertiaMaxSpeed",
                           .label = "Glide max speed",
                           .help = "Camera heights for each second.",
                           .kind = FieldKind::Number,
                           .min = 0.1,
                           .max = 1000.0,
                           .step = 0.1,
                           .format = "%.1f",
                           .logarithmic = true,
                           MOA_NUMBER(dragInertiaMaxSpeed)},
    Field<CameraSettings> {.group = "Glide",
                           .section = "camera",
                           .key = "turnInertia",
                           .label = "Turn glide time",
                           .help = "Seconds. The time constant of the glide after a turn. 0 is off.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 5.0,
                           .step = 0.01,
                           .format = "%.2f s",
                           MOA_NUMBER(turnInertia)},
    Field<CameraSettings> {.group = "Glide",
                           .section = "camera",
                           .key = "pitchInertia",
                           .label = "Pitch glide time",
                           .help = "Seconds. The time constant of the glide after a pitch. 0 is off.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 5.0,
                           .step = 0.01,
                           .format = "%.2f s",
                           MOA_NUMBER(pitchInertia)},
    Field<CameraSettings> {.group = "Pan",
                           .section = "camera",
                           .key = "panSpeed",
                           .label = "Pan speed",
                           .help = "View widths for each second of the key pan. 0 keeps the speed of the game.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 5.0,
                           .step = 0.01,
                           .format = "%.2f",
                           MOA_NUMBER(panSpeed)},
    Field<CameraSettings> {.group = "Pan",
                           .section = "camera",
                           .key = "edgeScroll",
                           .label = "Edge scroll",
                           .help = "The camera scrolls when the cursor is at the edge of the screen during a drag.",
                           MOA_TOGGLE(edgeScroll)},
    Field<CameraSettings> {.group = "Spring arm",
                           .section = "camera",
                           .key = "springArm",
                           .label = "Spring arm on",
                           .help = "The camera stays in front of the objects and the land between the pivot and the camera.",
                           MOA_TOGGLE(springArm)},
    Field<CameraSettings> {.group = "Spring arm",
                           .section = "camera",
                           .key = "collisionInSmoothing",
                           .label = "Move in time",
                           .help = "Seconds for the camera to move in front of an obstacle. 0 is at once.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 5.0,
                           .step = 0.01,
                           .format = "%.2f s",
                           MOA_NUMBER(collisionInSmoothing)},
    Field<CameraSettings> {.group = "Spring arm",
                           .section = "camera",
                           .key = "collisionOutSmoothing",
                           .label = "Move out time",
                           .help = "Seconds for the camera to move back out when the line is clear.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 5.0,
                           .step = 0.01,
                           .format = "%.2f s",
                           MOA_NUMBER(collisionOutSmoothing)},
    Field<CameraSettings> {.group = "Spring arm",
                           .section = "camera",
                           .key = "collisionMargin",
                           .label = "Obstacle margin",
                           .help = "Metres between the camera and the first obstacle.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 10.0,
                           .step = 0.05,
                           .format = "%.2f m",
                           MOA_NUMBER(collisionMargin)},
    Field<CameraSettings> {.group = "Spring arm",
                           .section = "camera",
                           .key = "objectCollision",
                           .label = "Stop at objects",
                           .help =
                               "The camera stops in front of buildings, trees and rocks. Off, only the land stops the camera.",
                           MOA_TOGGLE(objectCollision)},
    Field<CameraSettings> {.group = "Spring arm",
                           .section = "camera",
                           .key = "armLandSamples",
                           .label = "Land samples",
                           .help =
                               "The largest count of points where the spring arm examines the land between the hand and the "
                               "camera.",
                           .kind = FieldKind::Number,
                           .min = 16.0,
                           .max = 1024.0,
                           .step = 16.0,
                           .format = "%.0f",
                           MOA_NUMBER(armLandSamples)},
    Field<CameraSettings> {.group = "Spring arm",
                           .section = "camera",
                           .key = "cameraClearanceRadius",
                           .label = "Land ball radius",
                           .help = "The radius of the ball around the camera that the land pushes. 0 is off.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 10.0,
                           .step = 0.1,
                           .format = "%.1f m",
                           MOA_NUMBER(cameraClearanceRadius)},
    Field<CameraSettings> {.group = "Spring arm",
                           .section = "camera",
                           .key = "terrainPushSmoothing",
                           .label = "Land push time",
                           .help = "Seconds for the camera to move out of the land. 0 is at once.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 5.0,
                           .step = 0.01,
                           .format = "%.2f s",
                           MOA_NUMBER(terrainPushSmoothing)},
    Field<CameraSettings> {.group = "Spring arm",
                           .section = "camera",
                           .key = "terrainReturnSmoothing",
                           .label = "Land return time",
                           .help = "Seconds for the camera to move back when the land permits. 0 is at once.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 5.0,
                           .step = 0.01,
                           .format = "%.2f s",
                           MOA_NUMBER(terrainReturnSmoothing)},
    Field<CameraSettings> {.group = "Spring arm",
                           .section = "camera",
                           .key = "flyBackAfterStandBack",
                           .label = "Fly back after a game camera move",
                           .help = "After the game moved the camera (a blend or a cutscene), the camera flies back to the view "
                                   "that you had before.",
                           MOA_TOGGLE(flyBackAfterStandBack)},
    Field<CameraSettings> {.group = "Spring arm",
                           .section = "camera",
                           .key = "panAvoidance",
                           .label = "Smooth rise over the land",
                           .help =
                               "The camera rises over the land smoothly in a pan, and never goes into the land. Off, it rises "
                               "at once.",
                           MOA_TOGGLE(panAvoidance)},
    Field<CameraSettings> {
        .group = "Chase",
        .section = "camera",
        .key = "chaseSeconds",
        .label = "Chase time",
        .help = "Seconds for the camera to cover most of a move of the land drag, the key pan and the glide. 0 is off.",
        .kind = FieldKind::Number,
        .min = 0.0,
        .max = 5.0,
        .step = 0.01,
        .format = "%.2f s",
        MOA_NUMBER(chaseSeconds)},
    Field<CameraSettings> {.group = "Chase",
                           .section = "camera",
                           .key = "chaseMaxSpeed",
                           .label = "Chase max speed",
                           .help = "Distances to the focus for each second. A safeguard against a leap.",
                           .kind = FieldKind::Number,
                           .min = 0.1,
                           .max = 1000.0,
                           .step = 0.1,
                           .format = "%.1f",
                           .logarithmic = true,
                           MOA_NUMBER(chaseMaxSpeed)},
    Field<CameraSettings> {.group = "Chase",
                           .section = "camera",
                           .key = "chaseMinSpeed",
                           .label = "Chase min speed",
                           .help = "The chase can always move at this speed, metres for each second.",
                           .kind = FieldKind::Number,
                           .min = 1.0,
                           .max = 100000.0,
                           .step = 1.0,
                           .format = "%.0f m/s",
                           .logarithmic = true,
                           MOA_NUMBER(chaseMinSpeed)},
    Field<CameraSettings> {
        .group = "Chase",
        .section = "camera",
        .key = "landGrip",
        .label = "Grip the land",
        .help =
            "The right button drag keeps the land under the cursor, with no leap near the horizon. Off, the drag of the game.",
        MOA_TOGGLE(landGrip)},
    Field<CameraSettings> {.group = "Chase",
                           .section = "camera",
                           .key = "gripStepPerPixel",
                           .label = "Grip step per pixel",
                           .help = "The largest move of the land for each pixel, as a part of the distance to the focus.",
                           .kind = FieldKind::Number,
                           .min = 0.001,
                           .max = 1.0,
                           .step = 0.001,
                           .format = "%.3f",
                           .logarithmic = true,
                           MOA_NUMBER(gripStepPerPixel)},
    Field<CameraSettings> {.group = "Chase",
                           .section = "camera",
                           .key = "gripMinimumStep",
                           .label = "Grip minimum step",
                           .help = "The land drag can always move the land this far, metres.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 1000.0,
                           .step = 0.1,
                           .format = "%.1f m",
                           MOA_NUMBER(gripMinimumStep)},
    Field<CameraSettings> {.group = "Chase",
                           .section = "camera",
                           .key = "mouseSmoothing",
                           .label = "Mouse smoothing",
                           .help = "Seconds. A short smoothing of the turn and the pitch with the mouse. 0 is off.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 1.0,
                           .step = 0.005,
                           .format = "%.3f s",
                           MOA_NUMBER(mouseSmoothing)},
    Field<CameraSettings> {.group = "Fade",
                           .section = "camera",
                           .key = "fadeOccluders",
                           .label = "Fade objects near the camera",
                           .help =
                               "Buildings, trees, rocks and cliffs very near the camera become see-through. An object in the "
                               "hand never fades.",
                           MOA_TOGGLE(fadeOccluders)},
    Field<CameraSettings> {.group = "Fade",
                           .section = "camera",
                           .key = "fadeAlpha",
                           .label = "Fade opacity",
                           .help = "The opacity of an object near the camera. 0 is invisible.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 1.0,
                           .step = 0.01,
                           .format = "%.2f",
                           MOA_NUMBER(fadeAlpha)},
    Field<CameraSettings> {.group = "Fade",
                           .section = "camera",
                           .key = "fadeSeconds",
                           .label = "Fade time",
                           .help = "Seconds for an object to fade out, and to fade back in when it is no longer near.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 5.0,
                           .step = 0.01,
                           .format = "%.2f s",
                           MOA_NUMBER(fadeSeconds)},
    Field<CameraSettings> {.group = "Fade",
                           .section = "camera",
                           .key = "fadeDistance",
                           .label = "Fade distance",
                           .help = "Metres. An object nearer to the camera than this distance fades.",
                           .kind = FieldKind::Number,
                           .min = 0.5,
                           .max = 30.0,
                           .step = 0.1,
                           .format = "%.1f m",
                           MOA_NUMBER(fadeDistance)},
    Field<CameraSettings> {.group = "Fade",
                           .section = "camera",
                           .key = "hideCameraInside",
                           .label = "Hide an object at the camera",
                           .help = "An object that the camera is in is hidden while the camera is in it.",
                           MOA_TOGGLE(hideCameraInside)},
    Field<CameraSettings> {.group = "Trace",
                           .section = "camera",
                           .key = "trace",
                           .label = "Trace",
                           .help = "Write a CSV of each frame of a mouse drag to the trace folder.",
                           MOA_TOGGLE(trace)},
    Field<CameraSettings> {.group = "Navigation",
                           .section = "camera",
                           .key = "flyTo",
                           .label = "Double click flies to the land",
                           .help = "A double click on the land flies the camera to that point. Off, the game zooms as before.",
                           MOA_TOGGLE(flyTo)},
    Field<CameraSettings> {.group = "Navigation",
                           .section = "camera",
                           .key = "flyToSeconds",
                           .label = "Flight time",
                           .help = "Seconds of a flight to a point, to a camera spot or to the north view.",
                           .kind = FieldKind::Number,
                           .min = 0.05,
                           .max = 5.0,
                           .step = 0.05,
                           .format = "%.2f s",
                           MOA_NUMBER(flyToSeconds)},
    Field<CameraSettings> {.group = "Navigation",
                           .section = "camera",
                           .key = "flyToStage",
                           .label = "Flight zoom stage",
                           .help = "The zoom stage at the end of a double click flight. Less than 0 keeps the zoom.",
                           .kind = FieldKind::Number,
                           .min = -1.0,
                           .max = 4.0,
                           .step = 0.1,
                           .format = "%.1f",
                           MOA_NUMBER(flyToStage)},
    Field<CameraSettings> {.group = "Navigation",
                           .section = "camera",
                           .key = "edgeMargin",
                           .label = "Soft map edge",
                           .help =
                               "View widths in which the pan slows down before the edge of the map. 0 is the hard stop of the "
                               "game.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 2.0,
                           .step = 0.05,
                           .format = "%.2f views",
                           MOA_NUMBER(edgeMargin)},
    Field<CameraSettings> {.group = "Ground",
                           .section = "camera",
                           .key = "groundLook",
                           .label = "Ground look",
                           .help = "Below the closest zoom, the camera goes down to the land and the view becomes level.",
                           MOA_TOGGLE(groundLook)},
    Field<CameraSettings> {.group = "Ground",
                           .section = "camera",
                           .key = "groundEyeHeight",
                           .label = "Eye height",
                           .help = "Metres above the land at the closest zoom of the ground look.",
                           .kind = FieldKind::Number,
                           .min = 0.5,
                           .max = 5.0,
                           .step = 0.1,
                           .format = "%.1f m",
                           MOA_NUMBER(groundEyeHeight)},
    Field<CameraSettings> {.group = "Ground",
                           .section = "camera",
                           .key = "groundPitchDownDegrees",
                           .label = "Look down limit",
                           .help = "On the land, the largest angle of the view below the horizon.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 89.0,
                           .step = 1.0,
                           .format = "%.0f deg",
                           MOA_NUMBER(groundPitchDownDegrees)},
    Field<CameraSettings> {.group = "Ground",
                           .section = "camera",
                           .key = "groundPitchUpDegrees",
                           .label = "Look up limit",
                           .help = "On the land, the largest angle of the view above the horizon.",
                           .kind = FieldKind::Number,
                           .min = 0.0,
                           .max = 89.0,
                           .step = 1.0,
                           .format = "%.0f deg",
                           MOA_NUMBER(groundPitchUpDegrees)},
    Field<CameraSettings> {.group = "Ground",
                           .section = "camera",
                           .key = "groundWalkSpeed",
                           .label = "Walk speed",
                           .help = "The key pan on the land, metres for each second.",
                           .kind = FieldKind::Number,
                           .min = 0.1,
                           .max = 50.0,
                           .step = 0.1,
                           .format = "%.1f m/s",
                           MOA_NUMBER(groundWalkSpeed)},
    // The moon menu.
    Field<CameraSettings> {
        .group = "Navigation",
        .section = "camera",
        .key = "northDegrees",
        .label = "North heading",
        .help = "The heading of the north in the world, clockwise, degrees. The reset of the view puts it at the top.",
        .kind = FieldKind::Number,
        .min = -360.0,
        .max = 360.0,
        .step = 1.0,
        .format = "%.0f deg",
        MOA_NUMBER(northDegrees)},
};

#undef MOA_TOGGLE
#undef MOA_NUMBER
} // namespace

moa::Result<CameraSettings> ParseCameraSettings(const nlohmann::json& object)
{
	if (!object.is_object())
	{
		return moa::MakeError("the object 'camera' must be a JSON object");
	}
	CameraSettings value;
	if (!ReadField(object, "enabled", value.enabled) || !ReadField(object, "pivotOnHand", value.pivotOnHand) ||
	    !ReadField(object, "smoothZoom", value.smoothZoom) || !ReadField(object, "zoomSmoothing", value.zoomSmoothing) ||
	    !ReadField(object, "rotateSmoothing", value.rotateSmoothing) ||
	    !ReadField(object, "wheelZoomStep", value.wheelZoomStep) || !ReadField(object, "invertRotate", value.invertRotate) ||
	    !ReadField(object, "lookUp", value.lookUp) || !ReadField(object, "maxLookUpDegrees", value.maxLookUpDegrees) ||
	    !ReadField(object, "pitchSensitivity", value.pitchSensitivity) ||
	    !ReadField(object, "lookUpFullStage", value.lookUpFullStage) ||
	    !ReadField(object, "lookUpZeroStage", value.lookUpZeroStage) || !ReadField(object, "invertPitch", value.invertPitch) ||
	    !ReadField(object, "pitch", value.pitch) || !ReadField(object, "minTiltDegrees", value.minTiltDegrees) ||
	    !ReadField(object, "maxTiltDegrees", value.maxTiltDegrees) ||
	    !ReadField(object, "pitchSmoothing", value.pitchSmoothing) ||
	    !ReadField(object, "pitchResetOnZoom", value.pitchResetOnZoom) ||
	    !ReadField(object, "resetPitchOnDoubleClick", value.resetPitchOnDoubleClick) ||
	    !ReadField(object, "groundClearance", value.groundClearance) ||
	    !ReadField(object, "pitchTurnLock", value.pitchTurnLock) || !ReadField(object, "lockCursor", value.lockCursor) ||
	    !ReadField(object, "maxPivotDistance", value.maxPivotDistance) || !ReadField(object, "springArm", value.springArm) ||
	    !ReadField(object, "collisionInSmoothing", value.collisionInSmoothing) ||
	    !ReadField(object, "collisionOutSmoothing", value.collisionOutSmoothing) ||
	    !ReadField(object, "collisionMargin", value.collisionMargin) ||
	    !ReadField(object, "objectCollision", value.objectCollision) ||
	    !ReadField(object, "armLandSamples", value.armLandSamples) ||
	    !ReadField(object, "cameraClearanceRadius", value.cameraClearanceRadius) ||
	    !ReadField(object, "terrainPushSmoothing", value.terrainPushSmoothing) ||
	    !ReadField(object, "terrainReturnSmoothing", value.terrainReturnSmoothing) ||
	    !ReadField(object, "flyBackAfterStandBack", value.flyBackAfterStandBack) ||
	    !ReadField(object, "dragInertia", value.dragInertia) ||
	    !ReadField(object, "dragInertiaMaxSpeed", value.dragInertiaMaxSpeed) ||
	    !ReadField(object, "turnInertia", value.turnInertia) || !ReadField(object, "pitchInertia", value.pitchInertia) ||
	    !ReadField(object, "continuousZoom", value.continuousZoom) ||
	    !ReadField(object, "zoomStepRatio", value.zoomStepRatio) || !ReadField(object, "panSpeed", value.panSpeed) ||
	    !ReadField(object, "edgeScroll", value.edgeScroll) || !ReadField(object, "edgeMargin", value.edgeMargin) ||
	    !ReadField(object, "panAvoidance", value.panAvoidance) || !ReadField(object, "flyTo", value.flyTo) ||
	    !ReadField(object, "flyToSeconds", value.flyToSeconds) || !ReadField(object, "flyToStage", value.flyToStage) ||
	    !ReadField(object, "groundLook", value.groundLook) || !ReadField(object, "groundEyeHeight", value.groundEyeHeight) ||
	    !ReadField(object, "groundPitchDownDegrees", value.groundPitchDownDegrees) ||
	    !ReadField(object, "groundPitchUpDegrees", value.groundPitchUpDegrees) ||
	    !ReadField(object, "groundWalkSpeed", value.groundWalkSpeed) ||
	    !ReadField(object, "chaseSeconds", value.chaseSeconds) || !ReadField(object, "chaseMaxSpeed", value.chaseMaxSpeed) ||
	    !ReadField(object, "chaseMinSpeed", value.chaseMinSpeed) || !ReadField(object, "landGrip", value.landGrip) ||
	    !ReadField(object, "gripStepPerPixel", value.gripStepPerPixel) ||
	    !ReadField(object, "gripMinimumStep", value.gripMinimumStep) ||
	    !ReadField(object, "mouseSmoothing", value.mouseSmoothing) ||
	    !ReadField(object, "fadeOccluders", value.fadeOccluders) || !ReadField(object, "fadeAlpha", value.fadeAlpha) ||
	    !ReadField(object, "fadeSeconds", value.fadeSeconds) || !ReadField(object, "fadeDistance", value.fadeDistance) ||
	    !ReadField(object, "hideCameraInside", value.hideCameraInside) || !ReadField(object, "trace", value.trace) ||
	    !ReadField(object, "traceFolder", value.traceFolder))
	{
		return moa::MakeError("the object 'camera' has a field with a wrong type");
	}
	if (value.zoomSmoothing < 0.0 || value.zoomSmoothing > 5.0 || value.rotateSmoothing < 0.0 || value.rotateSmoothing > 5.0)
	{
		return moa::MakeError("the zoom and turn smoothing must be 0 to 5 seconds");
	}
	if (value.wheelZoomStep < 0.01 || value.wheelZoomStep > 4.0)
	{
		return moa::MakeError("'wheelZoomStep' must be 0.01 to 4 stages");
	}
	if (value.maxLookUpDegrees < 0.0 || value.maxLookUpDegrees > 60.0 || value.pitchSensitivity < 0.1 ||
	    value.pitchSensitivity > 100.0)
	{
		return moa::MakeError("'maxLookUpDegrees' must be 0 to 60 and 'pitchSensitivity' 0.1 to 100");
	}
	if (value.lookUpFullStage < 0.0 || value.lookUpZeroStage > 5.0 || value.lookUpZeroStage <= value.lookUpFullStage)
	{
		return moa::MakeError("'lookUpFullStage' must be 0 or more, and less than 'lookUpZeroStage' (5 or less)");
	}
	if (value.minTiltDegrees < 1.0 || value.maxTiltDegrees > 89.0 || value.minTiltDegrees >= value.maxTiltDegrees)
	{
		return moa::MakeError("'minTiltDegrees' must be 1 or more, and less than 'maxTiltDegrees' (89 or less)");
	}
	if (value.pitchSmoothing < 0.0 || value.pitchSmoothing > 5.0 || value.groundClearance < 0.0 || value.groundClearance > 20.0)
	{
		return moa::MakeError("'pitchSmoothing' must be 0 to 5 seconds and 'groundClearance' 0 to 20 metres");
	}
	if (value.maxPivotDistance < 0.0 || value.maxPivotDistance > 100.0)
	{
		return moa::MakeError("'maxPivotDistance' must be 0 to 100");
	}
	if (value.collisionInSmoothing < 0.0 || value.collisionInSmoothing > 5.0 || value.collisionOutSmoothing < 0.0 ||
	    value.collisionOutSmoothing > 5.0 || value.collisionMargin < 0.0 || value.collisionMargin > 10.0)
	{
		return moa::MakeError("the collision smoothing must be 0 to 5 seconds and 'collisionMargin' 0 to 10 metres");
	}
	if (value.armLandSamples < 16.0 || value.armLandSamples > 1024.0 || value.cameraClearanceRadius < 0.0 ||
	    value.cameraClearanceRadius > 10.0)
	{
		return moa::MakeError("'armLandSamples' must be 16 to 1024 and 'cameraClearanceRadius' 0 to 10 metres");
	}
	if (value.terrainPushSmoothing < 0.0 || value.terrainPushSmoothing > 5.0 || value.terrainReturnSmoothing < 0.0 ||
	    value.terrainReturnSmoothing > 5.0)
	{
		return moa::MakeError("'terrainPushSmoothing' and 'terrainReturnSmoothing' must be 0 to 5 seconds");
	}
	if (value.dragInertia < 0.0 || value.dragInertia > 5.0 || value.dragInertiaMaxSpeed <= 0.0 ||
	    value.dragInertiaMaxSpeed > 1000.0)
	{
		return moa::MakeError("'dragInertia' must be 0 to 5 seconds and 'dragInertiaMaxSpeed' more than 0, to 1000");
	}
	if (value.turnInertia < 0.0 || value.turnInertia > 5.0 || value.pitchInertia < 0.0 || value.pitchInertia > 5.0)
	{
		return moa::MakeError("'turnInertia' and 'pitchInertia' must be 0 to 5 seconds");
	}
	if (value.zoomStepRatio < 1.01 || value.zoomStepRatio > 4.0)
	{
		return moa::MakeError("'zoomStepRatio' must be 1.01 to 4");
	}
	if (value.panSpeed < 0.0 || value.panSpeed > 5.0)
	{
		return moa::MakeError("'panSpeed' must be 0 to 5 view widths for each second");
	}
	if (value.edgeMargin < 0.0 || value.edgeMargin > 2.0)
	{
		return moa::MakeError("'edgeMargin' must be 0 to 2 view widths");
	}
	if (value.flyToSeconds < 0.05 || value.flyToSeconds > 5.0 || value.flyToStage < -1.0 || value.flyToStage > 4.0)
	{
		return moa::MakeError("'flyToSeconds' must be 0.05 to 5 seconds and 'flyToStage' -1 to 4");
	}
	if (value.groundEyeHeight < 0.5 || value.groundEyeHeight > 5.0 || value.groundWalkSpeed < 0.1 ||
	    value.groundWalkSpeed > 50.0)
	{
		return moa::MakeError("'groundEyeHeight' must be 0.5 to 5 metres and 'groundWalkSpeed' 0.1 to 50 metres for "
		                      "each second");
	}
	if (value.groundPitchDownDegrees < 0.0 || value.groundPitchDownDegrees > 89.0 || value.groundPitchUpDegrees < 0.0 ||
	    value.groundPitchUpDegrees > 89.0)
	{
		return moa::MakeError("'groundPitchDownDegrees' and 'groundPitchUpDegrees' must be 0 to 89");
	}
	if (value.chaseSeconds < 0.0 || value.chaseSeconds > 5.0 || value.chaseMaxSpeed <= 0.0 || value.chaseMaxSpeed > 1000.0 ||
	    value.chaseMinSpeed <= 0.0 || value.chaseMinSpeed > 100000.0)
	{
		return moa::MakeError("'chaseSeconds' must be 0 to 5 seconds, 'chaseMaxSpeed' more than 0, to 1000, and "
		                      "'chaseMinSpeed' more than 0, to 100000 metres for each second");
	}
	if (value.gripStepPerPixel <= 0.0 || value.gripStepPerPixel > 1.0 || value.gripMinimumStep < 0.0 ||
	    value.gripMinimumStep > 1000.0)
	{
		return moa::MakeError("'gripStepPerPixel' must be more than 0, to 1, and 'gripMinimumStep' 0 to 1000 metres");
	}
	if (value.mouseSmoothing < 0.0 || value.mouseSmoothing > 1.0)
	{
		return moa::MakeError("'mouseSmoothing' must be 0 to 1 seconds");
	}
	if (value.fadeAlpha < 0.0 || value.fadeAlpha > 1.0 || value.fadeSeconds < 0.0 || value.fadeSeconds > 5.0)
	{
		return moa::MakeError("'fadeAlpha' must be 0 to 1 and 'fadeSeconds' 0 to 5 seconds");
	}
	if (value.fadeDistance < 0.5 || value.fadeDistance > 30.0)
	{
		return moa::MakeError("'fadeDistance' must be 0.5 to 30 metres");
	}
	if (const auto mode = object.find("pitchMode"); mode != object.end())
	{
		if (*mode == "hand")
		{
			value.pitchMode = PitchMode::Hand;
		}
		else if (*mode == "inPlace")
		{
			value.pitchMode = PitchMode::InPlace;
		}
		else if (*mode == "orbit")
		{
			value.pitchMode = PitchMode::Orbit;
		}
		else
		{
			return moa::MakeError("'pitchMode' must be \"hand\", \"inPlace\" or \"orbit\"");
		}
	}
	if (!ReadField(object, "northDegrees", value.northDegrees) || value.northDegrees < -360.0 || value.northDegrees > 360.0)
	{
		return moa::MakeError("'northDegrees' must be -360 to 360");
	}
	return value;
}

nlohmann::json CameraSettingsObject(const CameraSettings& settings)
{
	return {
	    {"enabled", settings.enabled},
	    {"pivotOnHand", settings.pivotOnHand},
	    {"smoothZoom", settings.smoothZoom},
	    {"zoomSmoothing", settings.zoomSmoothing},
	    {"rotateSmoothing", settings.rotateSmoothing},
	    {"wheelZoomStep", settings.wheelZoomStep},
	    {"invertRotate", settings.invertRotate},
	    {"lookUp", settings.lookUp},
	    {"maxLookUpDegrees", settings.maxLookUpDegrees},
	    {"pitchSensitivity", settings.pitchSensitivity},
	    {"lookUpFullStage", settings.lookUpFullStage},
	    {"lookUpZeroStage", settings.lookUpZeroStage},
	    {"invertPitch", settings.invertPitch},
	    {"pitch", settings.pitch},
	    {"minTiltDegrees", settings.minTiltDegrees},
	    {"maxTiltDegrees", settings.maxTiltDegrees},
	    {"pitchSmoothing", settings.pitchSmoothing},
	    {"pitchResetOnZoom", settings.pitchResetOnZoom},
	    {"resetPitchOnDoubleClick", settings.resetPitchOnDoubleClick},
	    {"groundClearance", settings.groundClearance},
	    {"pitchMode", PitchModeName(settings.pitchMode)},
	    {"pitchTurnLock", settings.pitchTurnLock},
	    {"lockCursor", settings.lockCursor},
	    {"maxPivotDistance", settings.maxPivotDistance},
	    {"springArm", settings.springArm},
	    {"collisionInSmoothing", settings.collisionInSmoothing},
	    {"collisionOutSmoothing", settings.collisionOutSmoothing},
	    {"collisionMargin", settings.collisionMargin},
	    {"objectCollision", settings.objectCollision},
	    {"armLandSamples", settings.armLandSamples},
	    {"cameraClearanceRadius", settings.cameraClearanceRadius},
	    {"terrainPushSmoothing", settings.terrainPushSmoothing},
	    {"terrainReturnSmoothing", settings.terrainReturnSmoothing},
	    {"flyBackAfterStandBack", settings.flyBackAfterStandBack},
	    {"dragInertia", settings.dragInertia},
	    {"dragInertiaMaxSpeed", settings.dragInertiaMaxSpeed},
	    {"turnInertia", settings.turnInertia},
	    {"pitchInertia", settings.pitchInertia},
	    {"continuousZoom", settings.continuousZoom},
	    {"zoomStepRatio", settings.zoomStepRatio},
	    {"panSpeed", settings.panSpeed},
	    {"edgeScroll", settings.edgeScroll},
	    {"edgeMargin", settings.edgeMargin},
	    {"panAvoidance", settings.panAvoidance},
	    {"flyTo", settings.flyTo},
	    {"flyToSeconds", settings.flyToSeconds},
	    {"flyToStage", settings.flyToStage},
	    {"groundLook", settings.groundLook},
	    {"groundEyeHeight", settings.groundEyeHeight},
	    {"groundPitchDownDegrees", settings.groundPitchDownDegrees},
	    {"groundPitchUpDegrees", settings.groundPitchUpDegrees},
	    {"groundWalkSpeed", settings.groundWalkSpeed},
	    {"chaseSeconds", settings.chaseSeconds},
	    {"chaseMaxSpeed", settings.chaseMaxSpeed},
	    {"chaseMinSpeed", settings.chaseMinSpeed},
	    {"landGrip", settings.landGrip},
	    {"gripStepPerPixel", settings.gripStepPerPixel},
	    {"gripMinimumStep", settings.gripMinimumStep},
	    {"mouseSmoothing", settings.mouseSmoothing},
	    {"fadeOccluders", settings.fadeOccluders},
	    {"fadeAlpha", settings.fadeAlpha},
	    {"fadeSeconds", settings.fadeSeconds},
	    {"fadeDistance", settings.fadeDistance},
	    {"hideCameraInside", settings.hideCameraInside},
	    {"trace", settings.trace},
	    {"traceFolder", settings.traceFolder},
	    {"northDegrees", settings.northDegrees},
	};
}

CameraSettingsChange CompareCameraSettings(const CameraSettings& current, const CameraSettings& next)
{
	CameraSettingsChange change;
	change.enabled = current.enabled != next.enabled;
	change.trace = current.trace != next.trace || current.traceFolder != next.traceFolder;
	// Compare the other values: make the fields that have their own flag the same.
	auto other = next;
	other.enabled = current.enabled;
	other.trace = current.trace;
	other.traceFolder = current.traceFolder;
	change.camera = other != current;
	return change;
}

std::string CameraSummary(const CameraSettings& settings)
{
	const auto& c = settings;
	std::string text = std::format("Camera mod: {}\n", OnOff(c.enabled));
	text += std::format("Pitch: {}, mode {}, tilt {:.0f} to {:.0f} deg\n", OnOff(c.pitch), PitchModeName(c.pitchMode),
	                    c.minTiltDegrees, c.maxTiltDegrees);
	text += std::format("Look up: {}, max {:.0f} deg, sensitivity {:.1f}\n", OnOff(c.lookUp), c.maxLookUpDegrees,
	                    c.pitchSensitivity);
	text += std::format("Smoothing: zoom {:.2f} s, turn {:.2f} s, pitch {:.2f} s\n", c.zoomSmoothing, c.rotateSmoothing,
	                    c.pitchSmoothing);
	text += std::format("Glide: {:.2f} s, cursor lock {}, trace {}\n", c.dragInertia, OnOff(c.lockCursor), OnOff(c.trace));
	text += std::format("Spring arm: {}, in {:.2f} s, out {:.2f} s, margin {:.2f} m\n", OnOff(c.springArm),
	                    c.collisionInSmoothing, c.collisionOutSmoothing, c.collisionMargin);
	text += std::format("Land: objects {}, {:.0f} samples, ball {:.2f} m, push {:.2f} s, return {:.2f} s, fly back {}\n",
	                    OnOff(c.objectCollision), c.armLandSamples, c.cameraClearanceRadius, c.terrainPushSmoothing,
	                    c.terrainReturnSmoothing, OnOff(c.flyBackAfterStandBack));
	text += std::format("Turn glide {:.2f} s, pitch glide {:.2f} s, zoom {} x{:.2f}\n", c.turnInertia, c.pitchInertia,
	                    c.continuousZoom ? "continuous" : "stages", c.zoomStepRatio);
	text += std::format("Pan: {:.2f} views/s, edge scroll {}, soft edge {:.2f} views, avoidance {}\n", c.panSpeed,
	                    OnOff(c.edgeScroll), c.edgeMargin, OnOff(c.panAvoidance));
	text += std::format("Fly to: {}, {:.2f} s, {}\n", OnOff(c.flyTo), c.flyToSeconds,
	                    c.flyToStage < 0.0 ? std::string("keep the zoom") : std::format("stage {:.1f}", c.flyToStage));
	text += std::format("Ground look: {}, eye {:.1f} m, view {:.0f} deg down to {:.0f} deg up, walk {:.1f} m/s\n",
	                    OnOff(c.groundLook), c.groundEyeHeight, c.groundPitchDownDegrees, c.groundPitchUpDegrees,
	                    c.groundWalkSpeed);
	text += std::format("Chase: {:.2f} s, land grip {}, mouse smoothing {:.3f} s\n", c.chaseSeconds, OnOff(c.landGrip),
	                    c.mouseSmoothing);
	text += std::format("Fade: {}, opacity {:.2f}, {:.2f} s, distance {:.1f} m, hide inside {}", OnOff(c.fadeOccluders),
	                    c.fadeAlpha, c.fadeSeconds, c.fadeDistance, OnOff(c.hideCameraInside));
	return text;
}

std::string_view CameraActionName(CameraAction action)
{
	switch (action)
	{
	case CameraAction::ToggleCamera:
		return "toggleCamera";
	case CameraAction::CyclePitchMode:
		return "cyclePitchMode";
	case CameraAction::ToggleCursorLock:
		return "toggleCursorLock";
	case CameraAction::ToggleTrace:
		return "toggleTrace";
	case CameraAction::SaveSpot1:
		return "saveSpot1";
	case CameraAction::SaveSpot2:
		return "saveSpot2";
	case CameraAction::SaveSpot3:
		return "saveSpot3";
	case CameraAction::SaveSpot4:
		return "saveSpot4";
	case CameraAction::GoToSpot1:
		return "goToSpot1";
	case CameraAction::GoToSpot2:
		return "goToSpot2";
	case CameraAction::GoToSpot3:
		return "goToSpot3";
	case CameraAction::GoToSpot4:
		return "goToSpot4";
	case CameraAction::ResetView:
		return "resetView";
	}
	return "unknown";
}

std::string_view CameraActionDefaultHotkey(CameraAction action)
{
	switch (action)
	{
	case CameraAction::ToggleCamera:
		return "Ctrl+Shift+F5";
	case CameraAction::CyclePitchMode:
		return "Ctrl+Shift+F7";
	case CameraAction::ToggleCursorLock:
		return "Ctrl+Shift+F8";
	case CameraAction::ToggleTrace:
		return "Ctrl+Shift+F9";
	case CameraAction::SaveSpot1:
		return "Ctrl+F1";
	case CameraAction::SaveSpot2:
		return "Ctrl+F2";
	case CameraAction::SaveSpot3:
		return "Ctrl+F3";
	case CameraAction::SaveSpot4:
		return "Ctrl+F4";
	case CameraAction::GoToSpot1:
		return "F1";
	case CameraAction::GoToSpot2:
		return "F2";
	case CameraAction::GoToSpot3:
		return "F3";
	case CameraAction::GoToSpot4:
		return "F4";
	case CameraAction::ResetView:
		return "F5";
	}
	return "";
}

std::optional<size_t> SaveSpotIndex(CameraAction action)
{
	const auto index = static_cast<int>(action) - static_cast<int>(CameraAction::SaveSpot1);
	return index >= 0 && index < 4 ? std::optional<size_t>(static_cast<size_t>(index)) : std::nullopt;
}

std::optional<size_t> GoToSpotIndex(CameraAction action)
{
	const auto index = static_cast<int>(action) - static_cast<int>(CameraAction::GoToSpot1);
	return index >= 0 && index < 4 ? std::optional<size_t>(static_cast<size_t>(index)) : std::nullopt;
}

std::optional<settings::SettingsEdit> EditForAction(CameraSettings& settings, CameraAction action)
{
	auto& camera = settings;
	switch (action)
	{
	case CameraAction::ToggleCamera:
		camera.enabled = !camera.enabled;
		return settings::SettingsEdit {std::format("Camera mod: {}", OnOff(camera.enabled)),
		                               {{"camera", {{"enabled", camera.enabled}}}}};
	case CameraAction::CyclePitchMode:
		camera.pitchMode = NextPitchMode(camera.pitchMode);
		return settings::SettingsEdit {
		    std::format("Camera pitch mode: {}{}", PitchModeName(camera.pitchMode),
		                camera.pitch || camera.pitchMode == PitchMode::Hand ? "" : " (the pitch is off)"),
		    {{"camera", {{"pitchMode", PitchModeName(camera.pitchMode)}}}}};
	case CameraAction::ToggleCursorLock:
		camera.lockCursor = !camera.lockCursor;
		return settings::SettingsEdit {std::format("Camera cursor lock: {}", OnOff(camera.lockCursor)),
		                               {{"camera", {{"lockCursor", camera.lockCursor}}}}};
	case CameraAction::ToggleTrace:
		camera.trace = !camera.trace;
		return settings::SettingsEdit {std::format("Camera trace: {}", OnOff(camera.trace)),
		                               {{"camera", {{"trace", camera.trace}}}}};
	default:
		return std::nullopt;
	}
}

std::span<const settings::Field<CameraSettings>> CameraFields()
{
	return k_Fields;
}

settings::FieldRange CameraRangeOf(const settings::Field<CameraSettings>& field, const CameraSettings& settings)
{
	settings::FieldRange range {.min = field.min, .max = field.max};
	const auto& c = settings;
	if (field.section == "camera")
	{
		if (field.key == "lookUpFullStage")
		{
			range.max = std::max(range.min, c.lookUpZeroStage - k_StageGap);
		}
		else if (field.key == "lookUpZeroStage")
		{
			range.min = std::min(range.max, c.lookUpFullStage + k_StageGap);
		}
		else if (field.key == "minTiltDegrees")
		{
			range.max = std::max(range.min, c.maxTiltDegrees - k_TiltGap);
		}
		else if (field.key == "maxTiltDegrees")
		{
			range.min = std::min(range.max, c.minTiltDegrees + k_TiltGap);
		}
	}
	return range;
}

moa::Result<settings::SettingsEdit> EditCameraField(CameraSettings& current, const settings::Field<CameraSettings>& field,
                                                    const settings::FieldValue& value)
{
	return settings::EditField(current, field, value, CameraRangeOf(field, current),
	                           [](const CameraSettings& next) -> moa::Result<void> {
		                           if (auto checked = ParseCameraSettings(CameraSettingsObject(next)); !checked)
		                           {
			                           return checked.GetError();
		                           }
		                           return {};
	                           });
}

} // namespace moa_mod::camera
