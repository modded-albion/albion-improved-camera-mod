/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <optional>
#include <string>
#include <vector>

#include <core/CameraChase.h>
#include <core/CameraDrag.h>
#include <core/CameraEvents.h>
#include <core/CameraFeel.h>
#include <core/CameraGround.h>
#include <core/CameraInertia.h>
#include <core/CameraLand.h>
#include <core/CameraNav.h>
#include <core/CameraPitch.h>
#include <core/CameraPivot.h>
#include <core/CameraSettings.h>
#include <core/HandRig.h>
#include <core/MouseButtons.h>
#include <nlohmann/json.hpp>

namespace moa_mod::camera
{

/// A camera position and rotation.
struct CameraPlacement
{
	WorldPoint position;
	Quaternion rotation;
};

/// The values of the land drag of the game that the camera loop reads.
struct DragFields
{
	float startedTime = 0.0f;        ///< The time of the last press of the land drag.
	bool holdingZoomRotate = false;  ///< The user holds a button that zooms or turns.
	int32_t ignoreHand = 0;          ///< A count that stops the hold of the hand point.
	int32_t inhibitHand = 0;         ///< A count that stops the hold after a push of the camera.
	WorldPoint lastCursorTerrain;    ///< The land point of the last zoom to the cursor.
	bool noHandFocus = false;        ///< The hold of the hand point is off.
	float fade = 0.0f;               ///< 1 while the user holds the land, then less.
	bool zoomRotateChanging = false; ///< The game holds the land during a zoom or a turn.
	bool dragValid = false;          ///< The press of the land drag is valid.
	WorldPoint grab;                 ///< The grab point of the game.
	WorldPoint holdCursor;           ///< The smoothed cursor of the hold, screen pixels.
	WorldPoint handCursor;           ///< The smoothed cursor of the hand, screen pixels.
	bool inSubScene = false;         ///< A sub scene floor is set.
};

/// The tightness of the two eases of the town camera of the game. Zero stops an ease.
struct GameEases
{
	float stage = 0.2f;       ///< The ease of the smoothed height and tilt to the row of the stage.
	float focusHeight = 0.2f; ///< The ease of the height of the focus to the land.
};

/// The Unity layer of the land collider of the game (the layer "Terrain"). The land is the only collider on it.
constexpr int32_t k_LandLayer = 8;
/// The layers of the objects that the camera must not go into. These are the layers of the collision test of the
/// camera of the game (all layers but 2 and 3) without the land layer. The camera loop keeps the camera above the land
/// with the land height of the game, so a hit on the land collider is not an obstacle.
constexpr int32_t k_CameraObstacleLayers = ~((1 << 2) | (1 << 3) | (1 << k_LandLayer));

/// The first hit of a sphere that moves on a line.
struct SphereHit
{
	bool hit = false;
	float distance = 0.0f; ///< The distance that the centre of the sphere moved before it touched an object, metres.
	uint64_t collider = 0; ///< An identifier of the object that the sphere touched. Zero if not known.
};

/// The edge scroll of the game in its last frame.
struct EdgeScrollFrame
{
	bool active = false;    ///< The game added its edge scroll to the pan in the frame.
	float speed = 0.0f;     ///< The smoothed speed of the edge scroll, metres for each second.
	float deltaTime = 0.0f; ///< The frame time of the game, seconds.
	WorldPoint cursor;      ///< The input position of the game, screen pixels.
};

/// The hand of the player.
struct HandState
{
	WorldPoint point; ///< The smoothed hand point.
	bool visible = false;
	bool overUi = false;
};

/// Why the hand rig does not keep the cursor on the pivot while the user holds the middle button.
enum class CursorLockOff
{
	Unknown = 1,
	StandBack,       ///< The mod stood back during the hold.
	NoInputPosition, ///< The loop could not read the input position or the mouse position.
	NoPress,         ///< The loop did not see the press of the button.
	PressOnUi,       ///< The press was on the UI of the game.
	WarpFailed,      ///< The system did not move the cursor.
};

[[nodiscard]] constexpr const char* Describe(CursorLockOff cause)
{
	switch (cause)
	{
	case CursorLockOff::Unknown:
		return "the cause is not known";
	case CursorLockOff::StandBack:
		return "the mod stood back during the hold";
	case CursorLockOff::NoInputPosition:
		return "the mod could not read the input position or the mouse position";
	case CursorLockOff::NoPress:
		return "the mod did not see the press of the button";
	case CursorLockOff::PressOnUi:
		return "the press was on the UI of the game";
	case CursorLockOff::WarpFailed:
		return "the system did not move the cursor";
	}
	return "";
}

/// What the camera loop did in the last frame, for the frame trace. A value that the frame did not set stays empty.
struct CameraLoopTrace
{
	bool restored = false;                ///< `Before` put the camera of the game back into the transform.
	bool movedByOther = false;            ///< At `Before`, the transform was not the camera that the loop wrote.
	bool holdBlocked = false;             ///< `Before` stopped the hold of the game for the middle button.
	float writtenStage = 0.0f;            ///< The stage that `Before` gave to the game.
	float zoomTarget = 0.0f;              ///< The target stage of the smooth zoom.
	std::optional<WorldPoint> glideFocus; ///< The focus that the glide gave to the game.
	bool applied = false;                 ///< `After` changed the camera of the game.
	float raise = 0.0f;                   ///< The smoothed raise of the pitch, degrees.
	float raiseTarget = 0.0f;             ///< The raise that the user asked for, degrees.
	float viewTilt = 0.0f;                ///< The tilt of the view of the user, degrees below the horizon.
	float appliedLookUp = 0.0f;           ///< The turn up of the camera from the camera of the game, degrees.
	float clearance = 0.0f;               ///< The raise of the camera above the land, metres.
	bool turned = false;                  ///< The heading changed in the frame.
	bool turnLocked = false;              ///< The turn lock cancelled the turn of the game in the frame.
	std::optional<WorldPoint> pivot;      ///< The point that the turn went about.
	WorldPoint cursorJump;                ///< The move of the focus for a zoom to the cursor.
	WorldPoint holdShift;                 ///< The move of the land drag in the view of the user.
	bool dragActive = false;              ///< The loop held a grab point under the cursor.
	std::optional<WorldPoint> anchor;
	std::optional<WorldPoint> viewFocus;
	std::optional<WorldPoint> writtenFocus;       ///< The focus that `After` wrote to the game.
	std::optional<CameraPlacement> writtenCamera; ///< The camera that `After` wrote to the transform.
	bool handRig = false;                         ///< The hand rig made the camera in this frame.
	bool pivotKept = true;                        ///< The hand rig kept the pivot at its place on the screen in this frame.
	WorldPoint gameShift;                 ///< The move of the game (pan, drag, clamp, push, collision) that the rig took.
	float inPlaceTilt = 0.0f;             ///< The part of the pitch that turned about the camera, not about the pivot, degrees.
	float zoomFactor = 1.0f;              ///< The change of the distance to the pivot in this frame.
	bool zoomLimited = false;             ///< The zoom limits of the game moved the zoom target in this frame.
	bool pivotLimited = false;            ///< The pivot is nearer than the land under the hand, on the same line.
	std::optional<WorldPoint> cursorLock; ///< The point of the screen where the hand rig keeps the cursor and the hand.
	std::optional<WorldPoint> warpedMouse; ///< The mouse position of Unity after the hand rig moved the cursor.
	/// The cause (`CursorLockOff`) when the middle button is held and the hand rig does not keep the cursor.
	std::optional<int> cursorLockOff;
	std::optional<bool> modMiddle;          ///< The middle button as the loop read it.
	Gesture gesture = Gesture::None;        ///< What the user does with the camera in this frame.
	std::optional<PivotReason> pivotReason; ///< Why the hand rig took a pivot in this frame.
	std::optional<float> desiredBefore;     ///< The desired distance before a change in this frame.
	EventText armHitName;                   ///< The name of the object that the spring arm touches.
	EventText events;                       ///< The events of the loop in this frame.
	std::optional<float> desiredDistance;   ///< The distance from the pivot to the camera that the spring arm keeps.
	std::optional<float> armDistance;       ///< The distance from the pivot to the shown camera on the spring arm.
	std::optional<float> armHit;            ///< The distance from the pivot to the first object or land on the spring arm.
	bool gamePushIgnored = false;           ///< The game did not push the camera or test it for collisions in this frame.
	float turnRate = 0.0f;                  ///< The measured rate of the turn of the user, degrees for each second.
	float pitchRate = 0.0f;                 ///< The measured rate of the pitch of the user, degrees for each second.
	/// What changed the desired distance of the spring arm in this frame: 0 nothing, 1 a zoom, 2 a new pivot, 3 a new
	/// arm, 4 the correction of a change that no zoom and no new pivot caused, 5 the return to the distance of the stage,
	/// 6 a new arm after the clamp into the map moved the camera (the pivot of the gesture did not change).
	int desiredChange = 0;
	float glideTurn = 0.0f;                      ///< The turn of the glide after a turn in this frame, degrees.
	float glidePitch = 0.0f;                     ///< The raise of the glide after a pitch in this frame, degrees.
	std::optional<float> zoomHeight;             ///< The height of the zoom stage of the loop, metres.
	std::optional<float> panScale;               ///< The factor that the loop gave to the move speed of the game.
	std::optional<WorldPoint> edgeScrollRemoved; ///< The move of the edge scroll of the game that the loop removed.
	std::optional<WorldPoint> edgeSoftened;      ///< The part of the move of the game that the soft map edge removed.
	std::optional<float> flight;                 ///< The part of the flight of the hand rig that is done, 0 to 1.
	bool flightStarted = false;                  ///< A flight started in this frame.
	bool flightStopped = false;                  ///< An input of the user stopped the flight in this frame.
	std::optional<float> armLift;                ///< The move of the shown camera from the spring arm for the land.
	int landPushes = 0;                          ///< The count of the points of the camera ball that pushed the camera.
	int armLandSamples = 0;                      ///< The count of the land samples of the spring arm in this frame.
	std::optional<float> groundBlend;   ///< In the ground range: the blend from the hand rig (0) to the camera on the land (1).
	std::optional<float> eyeHeight;     ///< In the ground range: the height of the camera above the smoothed land, metres.
	std::optional<WorldPoint> chaseLag; ///< The camera that the user sees less the target camera, when it is not zero.
	float chaseSpeed = 0.0f;            ///< The speed of the camera that the user sees in the chase, metres for each second.
	bool gripActive = false;            ///< The land drag of the hand rig holds the gripped land in this frame.
	std::optional<WorldPoint> gripMove; ///< The move of the target camera by the land drag in this frame.
	bool gripLimited = false;           ///< The limit for each pixel or the land stopped the land drag in this frame.
};

/// Everything that the camera loop reads from the game and writes to the game. The research mod implements it on
/// the running game; the tests implement it with a model of the game. A function that returns nothing failed, and
/// the implementation reports the error itself.
class CameraGame
{
public:
	virtual ~CameraGame() = default;

	// The town camera state of the game.
	[[nodiscard]] virtual WorldPoint Focus() const = 0;
	virtual void SetFocus(WorldPoint focus) = 0;
	[[nodiscard]] virtual float Direction() const = 0;
	virtual void SetDirection(float direction) = 0;
	[[nodiscard]] virtual float Stage() const = 0;
	virtual void SetStage(float stage) = 0;
	/// The smoothed tilt of the zoom stage, degrees.
	[[nodiscard]] virtual float StageTilt() const = 0;
	/// The smoothed height of the zoom stage: the height of the camera above the focus.
	[[nodiscard]] virtual float StageHeight() const = 0;
	[[nodiscard]] virtual std::optional<StageLimits> ZoomLimits() = 0;
	/// The zoom stage table of the game: one row for each whole stage.
	[[nodiscard]] virtual std::optional<std::vector<StageView>> StageTable() = 0;
	/// Set the smoothed height and tilt of the stage. The game places its camera with these values.
	virtual void SetStageView(StageView view) = 0;
	/// The tightness of the eases of the town camera.
	[[nodiscard]] virtual std::optional<GameEases> Eases() = 0;
	[[nodiscard]] virtual bool SetEases(GameEases eases) = 0;
	/// The smallest zoom stage that the game keeps while the hand holds an object. The game reads it in each frame,
	/// before it keeps the stage in its limits. Nothing if the game does not have it.
	[[nodiscard]] virtual std::optional<float> HeldObjectMinimumStage() = 0;
	[[nodiscard]] virtual bool SetHeldObjectMinimumStage(float stage) = 0;
	[[nodiscard]] virtual bool IgnoresCollisions() const = 0;
	/// Stop or start the push of the camera above the land and the collision test of the camera of the game.
	[[nodiscard]] virtual bool SetIgnoresCollisions(bool ignore) = 0;
	/// The move speed setting of the game. The game multiplies the key pan by it.
	[[nodiscard]] virtual std::optional<float> MoveSpeed() = 0;
	[[nodiscard]] virtual bool SetMoveSpeed(float speed) = 0;
	/// True if a key zoom of the game is continuous. False makes a key zoom move by whole stages.
	[[nodiscard]] virtual std::optional<bool> DollySmooth() = 0;
	[[nodiscard]] virtual bool SetDollySmooth(bool smooth) = 0;
	/// The edge scroll of the game in its last frame. Nothing if the game does not give it.
	[[nodiscard]] virtual std::optional<EdgeScrollFrame> EdgeScroll() = 0;
	/// The zoom stage that the game zooms to by itself, for example after a double click on the land. Less than zero
	/// when the game does not zoom to a stage. Nothing if the game does not give it.
	[[nodiscard]] virtual std::optional<float> ZoomToStage() = 0;
	/// Set the zoom stage that the game zooms to by itself. Less than zero stops that zoom.
	virtual void SetZoomToStage(float stage) = 0;
	/// The box that the game keeps its focus in. Nothing if the game does not give it.
	[[nodiscard]] virtual std::optional<FocusBox> FocusLimits() = 0;

	// The camera transform.
	[[nodiscard]] virtual std::optional<CameraPlacement> Camera() = 0;
	[[nodiscard]] virtual bool SetCamera(const CameraPlacement& placement) = 0;
	/// The ray of the camera transform as it is now, through a point of the screen.
	[[nodiscard]] virtual std::optional<Ray> ScreenRay(WorldPoint screen) = 0;
	/// The land under a point of the screen for the camera transform as it is now, with the rule of the land drag.
	[[nodiscard]] virtual std::optional<WorldPoint> LandUnderScreenPoint(WorldPoint screen) = 0;
	/// True if the mouse cursor is on the land, for the camera transform as it is now.
	[[nodiscard]] virtual std::optional<bool> CursorOnLand() = 0;
	/// The place of a world point on the screen for the camera transform as it is now: x and y in pixels, z the
	/// distance in front of the camera.
	[[nodiscard]] virtual std::optional<WorldPoint> ScreenPointOf(WorldPoint point) = 0;
	/// The width and the height of the screen, pixels.
	[[nodiscard]] virtual std::optional<Vector2> ScreenSize() = 0;
	/// The vertical field of view of the camera, degrees.
	[[nodiscard]] virtual std::optional<float> FieldOfView() = 0;
	/// The land height that the camera must stay above, near a position.
	[[nodiscard]] virtual std::optional<float> HighestGround(WorldPoint position) = 0;
	/// The exact land height under a position, with no margin: the land of the camera of the game at that one point
	/// (with the floor of a sub scene and the lowest height of the camera land of the game).
	[[nodiscard]] virtual std::optional<float> LandHeight(WorldPoint position) = 0;
	/// The game moves the focus and the camera transform back into the map.
	virtual void ClampFocus() = 0;
	/// Move a sphere from `origin` on the unit vector `direction` for `length` metres. Find the first object on the
	/// Unity layers of the mask `layers`, with the filter and the trigger rule of the collision test of the camera of
	/// the game. Nothing if the game cannot do the test.
	[[nodiscard]] virtual std::optional<SphereHit> CastSphere(WorldPoint origin, WorldPoint direction, float radius,
	                                                          float length, int32_t layers) = 0;

	// The land drag.
	[[nodiscard]] virtual DragFields Drag() const = 0;
	virtual void SetDragGrab(WorldPoint grab) = 0;
	virtual void SetLastCursorTerrain(WorldPoint point) = 0;
	virtual void SetDragFade(float fade) = 0;
	/// Set the count of frames in which the game does not hold the land under the cursor.
	virtual void SetHoldBlock(int32_t frames) = 0;

	// The hand.
	[[nodiscard]] virtual std::optional<HandState> Hand() = 0;
	virtual void SetHandOverride(WorldPoint screen) = 0;

	// The input.
	/// A mouse button from one reader. Nothing if the mod does not have that reader or the read fails.
	[[nodiscard]] virtual std::optional<bool> ReadMouseButton(ButtonSource source, int32_t button) = 0;
	/// A mouse button as the mod reads it: from the reader that the game uses for that button, else from Unity.
	[[nodiscard]] std::optional<bool> MouseButton(int32_t button)
	{
		for (const auto source : ButtonReadOrder(button))
		{
			if (auto held = ReadMouseButton(source, button))
			{
				if (button >= 0 && button < static_cast<int32_t>(_usedSources.size()))
				{
					_usedSources[static_cast<size_t>(button)] = source;
				}
				return held;
			}
		}
		return std::nullopt;
	}
	/// The reader that gave the last value of a button to `MouseButton`.
	[[nodiscard]] std::optional<ButtonSource> UsedSource(int32_t button) const
	{
		if (button < 0 || button >= static_cast<int32_t>(_usedSources.size()))
		{
			return std::nullopt;
		}
		return _usedSources[static_cast<size_t>(button)];
	}
	/// The mouse position of Unity, screen pixels.
	[[nodiscard]] virtual std::optional<WorldPoint> MousePosition() = 0;
	/// The input position of the game, screen pixels.
	[[nodiscard]] virtual std::optional<WorldPoint> InputPosition() = 0;
	[[nodiscard]] virtual std::optional<float> WheelAxis() = 0;
	[[nodiscard]] virtual std::optional<bool> IgnoreWheel() = 0;
	/// Move the mouse cursor of the system to a point of the screen, in the pixels of the mouse position of Unity.
	/// Return the mouse position that Unity gives for that place, or nothing if the cursor did not move.
	[[nodiscard]] virtual std::optional<WorldPoint> WarpCursor(WorldPoint screen) = 0;
	/// Set the place of the cursor from which the game measures the turn of a middle button drag in the next frame,
	/// in the pixels of the input position of the game.
	virtual void SetTurnCursor(WorldPoint screen) = 0;
	[[nodiscard]] virtual bool SetIgnoreWheel(bool ignore) = 0;

	/// Report an error of the camera loop.
	virtual void ReportError(std::string message) = 0;
	/// Write a line to the debug log.
	virtual void Debug(std::string message) = 0;
	/// An event of the camera loop: a gesture starts, a pivot, a change of the desired distance and the like.
	virtual void Event(CameraEventKind /*kind*/, std::string /*text*/) {}
	/// The name of the object that a sphere test touched. Nothing if the game cannot give it.
	[[nodiscard]] virtual std::optional<std::string> ColliderName(uint64_t /*collider*/) { return std::nullopt; }

private:
	std::array<std::optional<ButtonSource>, 3> _usedSources {};
};

/// The per-frame logic of the camera mod, with no game: the turn about the hand, the smooth zoom, the pitch, the
/// look up, the land clearance, the land drag in the view of the user and the glide after the drag.
///
/// The loop works around the frame of the game. `Before` puts the camera of the game back into the transform and
/// gives the game the stage and the glide of this frame. The game then runs its frame with its own camera. `After`
/// changes the camera of the game to the camera of the user.
///
/// One owner for each value in a frame: the game owns the focus, the heading and the stage during its frame. After
/// the frame, the loop finds the camera of the user only from the state of the game and its own state. It never
/// reads back a camera that it gave to the game, so there is no loop between frames.
class CameraLoop
{
public:
	explicit CameraLoop(CameraSettings settings);

	/// Use new settings. The loop starts again from the camera of the game, but it keeps the return from a stand-back:
	/// a change of the settings in the pause menu does not move the view of the user.
	void ApplySettings(CameraSettings settings);
	/// The mod stands back in this frame. The next active frame starts again from the camera of the game.
	void StandBack();
	/// Start again from the camera of the game in the next frame.
	void Resync() { _resync = true; }
	/// Before the frame of the game. Return true if the loop changes this frame.
	bool Before(CameraGame& game, float deltaTime, double seconds);
	/// After the frame of the game: put back the wheel setting of the game. Return true if `Before` started this
	/// frame. Call it once after each frame of the game, also when `Before` returned false.
	bool EndFrame(CameraGame& game);
	/// After the frame of the game: make the camera of the user. Return false on an error.
	bool After(CameraGame& game);

	/// Describe the state of the loop, for the query `camera.state`.
	[[nodiscard]] nlohmann::json State() const;

	/// The camera position that the user saw at the end of the last frame.
	[[nodiscard]] std::optional<WorldPoint> ShownPosition() const { return _shown; }
	/// The camera position that a view that turns in place holds, with no clearance raise. Nothing if the view is
	/// not turned in place.
	[[nodiscard]] std::optional<WorldPoint> PitchAnchor() const { return _anchor; }
	/// The land point at the view centre of a view that turns in place.
	[[nodiscard]] std::optional<WorldPoint> ViewFocus() const { return _viewFocus; }
	/// What the loop did in the last frame.
	[[nodiscard]] const CameraLoopTrace& Trace() const { return _trace; }

	/// Fly the hand rig to a saved view in the next frame where the hand rig is active. Return false if the pitch mode
	/// is not the hand rig.
	[[nodiscard]] bool FlyToSpot(const CameraSpot& spot);
	/// Fly the hand rig to the view of the game at the current zoom, with the heading `northDegrees`, about the point at
	/// the view centre. The flight starts in the next frame where the hand rig is active. Return false if the pitch
	/// mode is not the hand rig.
	[[nodiscard]] bool ResetView(float northDegrees);
	/// The current view of the hand rig, or nothing if the hand rig has no view.
	[[nodiscard]] std::optional<CameraSpot> CurrentSpot() const;
	/// True while the hand rig flies to a view.
	[[nodiscard]] bool Flying() const { return _flight.has_value(); }
	/// True while the zoom of the hand rig is in the ground range, below the closest zoom stage of the game.
	[[nodiscard]] bool InGround() const { return _ground.has_value(); }

private:
	/// The vertical drag of the middle mouse button in this frame.
	struct PitchInput
	{
		float raise = 0.0f;      ///< Degrees, up is positive.
		float vertical = 0.0f;   ///< The vertical move of the mouse in the drag, pixels.
		float horizontal = 0.0f; ///< The horizontal move of the mouse in the drag, pixels.
		bool reset = false;      ///< A double click asks for the tilt of the game.
	};
	/// The state of the land drag of the game before its frame.
	struct DragBefore
	{
		float startedTime = 0.0f;
		bool holdingZoomRotate = false;
		int32_t ignoreHand = 0;
		int32_t inhibitHand = 0;
		WorldPoint lastCursorTerrain;
		std::optional<WorldPoint> pressPoint;
	};

	[[nodiscard]] PitchInput ReadPitchInput(CameraGame& game);
	[[nodiscard]] PitchLimits PitchLimitsFromSettings() const;
	void ResetPitch();
	[[nodiscard]] std::optional<WorldPoint> HandPivot(CameraGame& game);
	[[nodiscard]] DragFrame ReadDragFrame(CameraGame& game) const;
	void RestoreGameCamera(CameraGame& game);
	void BlockPitchDragHold(CameraGame& game);
	void RestoreWheel(CameraGame& game);
	[[nodiscard]] bool ApplyView(CameraGame& game);
	void StopGlide();
	void StepGlide(CameraGame& game);
	void CheckGlide(CameraGame& game);
	void EaseHand(CameraGame& game);
	/// Measure the speed of a position while the user holds the land, for the glide after the release.
	void TrackPosition(CameraGame& game, WorldPoint position);
	/// True if the hand rig drags the land itself in this frame, not the game.
	[[nodiscard]] bool LandGripOn() const;
	/// The land drag of the hand rig. Find the move of the target camera in this frame (`move`). Return false on an
	/// error.
	[[nodiscard]] bool StepGrip(CameraGame& game, bool rightHeld, bool rightPressed, WorldPoint& move);
	/// Move the camera that the user sees after the target camera in this frame. `targetMove` is the part of the move of
	/// the target camera in this frame that the chase smooths.
	void StepChase(CameraGame& game, WorldPoint targetMove);
	/// True if the chase is on in this frame: the hand rig, the spring arm, and not the ground range.
	[[nodiscard]] bool ChaseOn() const;
	/// Stop the chase, the land drag and the smoothing of the mouse.
	void ResetMotion();
	/// A pan or a land drag moves the pivot of the spring arm with the camera, at the same height. Put the pivot back on
	/// the land if the move put it under the land. Return false on an error.
	[[nodiscard]] bool KeepArmPivotOnLand(CameraGame& game);

	// The pan, the edge scroll and the zoom of all modes.
	void ScalePan(CameraGame& game);
	void RestoreMoveSpeed(CameraGame& game);
	void SmoothKeyZoom(CameraGame& game);
	void RestoreDollySmooth(CameraGame& game);
	void RemoveEdgeScroll(CameraGame& game);
	[[nodiscard]] bool LoadStageTable(CameraGame& game);
	/// Add the wheel of this frame to the zoom target.
	void AddWheelToTarget(float wheel, StageLimits limits);
	/// Move the zoom value to the target, and return it.
	[[nodiscard]] float StepZoom(StageLimits limits, bool intoLimits);
	/// Stop the glides after a turn and a pitch, and forget the measured rates.
	void StopTurnGlides();
	/// Measure the turn of the user, or continue the turn after it. Return the turn of the glide in this frame, degrees.
	[[nodiscard]] float StepTurnGlide(float userTurn, bool middleHeld);
	/// Measure the pitch of the user, or continue the pitch after it. Return the raise of the glide in this frame.
	[[nodiscard]] float StepPitchGlide(float userRaise, bool middleHeld);

	/// `Before` of the modes that are not the hand rig.
	[[nodiscard]] bool BeforeOrbit(CameraGame& game);

	// The hand rig (`PitchMode::Hand`).
	[[nodiscard]] bool BeforeHand(CameraGame& game);
	[[nodiscard]] bool AfterHand(CameraGame& game);
	[[nodiscard]] bool ResyncHand(CameraGame& game);
	void BlockGameHold(CameraGame& game);
	void RestoreEases(CameraGame& game);
	void StopHeldObjectZoom(CameraGame& game);
	void RestoreHeldObjectZoom(CameraGame& game);
	[[nodiscard]] std::optional<StageLimits> ZoomLimits(CameraGame& game) const;
	[[nodiscard]] WorldPoint LimitPivot(WorldPoint camera, WorldPoint land);
	[[nodiscard]] float ZoomFactor(WorldPoint camera, WorldPoint pivot, float zoomFrom, float zoomTo) const;
	[[nodiscard]] std::optional<WorldPoint> LandUnderCursor(CameraGame& game, std::optional<WorldPoint> screen = {});
	[[nodiscard]] std::optional<WorldPoint> PressPoint(CameraGame& game);
	[[nodiscard]] float StageHeightOf(float stage) const;
	[[nodiscard]] float StageTiltOf(float stage) const;
	[[nodiscard]] bool WriteHandState(CameraGame& game, WorldPoint shake, Quaternion shakeTurn);
	void LockCursor(CameraGame& game, bool middleHeld, bool pressed);
	/// Keep the cursor on the pivot while the middle button is held. `LockCursor` calls it and then makes sure that
	/// the rig keeps the cursor while the button is held.
	void KeepCursor(CameraGame& game, bool middleHeld, bool pressed);

	// The spring arm of the hand rig.
	/// The distances on the spring arm that the obstacles permit, from the pivot.
	struct ArmLimit
	{
		float target = 0.0f;      ///< The distance that the arm moves to.
		float hard = 0.0f;        ///< The distance that the arm never goes past.
		std::optional<float> hit; ///< The distance of the first obstacle, if the arm is shorter than the rig.
		uint64_t collider = 0;    ///< The object that the arm touches. Zero for the land or if not known.
	};
	/// Add an event of this frame. The game gets the events at the end of `Before` and of `After`.
	void Tell(CameraEventKind kind, std::string text);
	/// Give the events of this frame to the game.
	void FlushEvents(CameraGame& game);
	/// Find what the user does with the camera in this frame, and tell a change.
	void UpdateGesture(CameraGame& game);
	/// Tell when the spring arm touches an object or the land, and when it is clear again.
	void TellArmHit(CameraGame& game, const ArmLimit& limit);
	[[nodiscard]] WorldPoint ArmCamera() const;
	/// The camera that the chase follows: the camera on the spring arm, with the push of the land.
	[[nodiscard]] WorldPoint TargetCamera() const;
	/// The camera that the user sees: the target camera with the lag of the chase.
	[[nodiscard]] WorldPoint ShownCamera() const;
	/// Make a new arm from `pivot` through the shown camera. `clamp`: the clamp into the map moved the camera, and the
	/// pivot of the gesture is the same.
	void MoveArmPivot(WorldPoint pivot, bool clamp = false);
	void ResetArm();
	[[nodiscard]] std::optional<ArmLimit> FindArmLimit(CameraGame& game, WorldPoint pivot, WorldPoint direction, float length,
	                                                   bool keepAboveLand);
	/// Move the spring arm in this frame. `lineTest`: the land on the line from the pivot to the camera stops the arm.
	/// Only a gesture about the pivot needs it; else the camera ball and the floor keep the camera above the land.
	[[nodiscard]] bool StepArm(CameraGame& game, bool keepAboveLand, bool lineTest);
	/// The largest move of the camera in a frame of the chase, metres.
	[[nodiscard]] float LargestStep(float focusDistance) const;
	/// The move of a camera on the arm that keeps it out of the land: the camera ball and the floor above the land.
	/// Nothing on an error.
	[[nodiscard]] std::optional<WorldPoint> LandPushAt(CameraGame& game, WorldPoint camera, int passes);
	/// The camera above the floor: the exact land plus the ground clearance. Nothing on an error.
	[[nodiscard]] std::optional<WorldPoint> AboveFloor(CameraGame& game, WorldPoint camera);
	/// Start the hand rig from the state of the game (focus, heading, stage and the tilt of the stage), not from the
	/// camera transform.
	void ResyncFromGameState(CameraGame& game);
	/// True if the game still has the town camera state that the rig wrote last.
	[[nodiscard]] bool GameStateKept(const CameraGame& game) const;
	/// The state of the game that places the camera of the game at `camera`. The focus is at most `reachLimit` away.
	[[nodiscard]] GameCameraState GameStateFor(WorldPoint camera, std::optional<float> reachLimit = {}) const;
	void StopGameCollisions(CameraGame& game);
	void RestoreGameCollisions(CameraGame& game);

	// The ground look of the hand rig: the zoom below the closest zoom stage of the game.
	/// The camera in the ground range.
	struct GroundState
	{
		float land = 0.0f;      ///< The smoothed height of the land under the camera, metres.
		float topHeight = 0.0f; ///< The height of the camera above the land at the top of the ground range, metres.
		float lift = 0.0f;      ///< The raise of the camera above its height, for an obstacle under it, metres.
	};
	/// The moves of the camera in one frame of the ground range.
	struct GroundStep
	{
		WorldPoint start;                ///< The camera that the user saw at the start of the frame.
		float from = 0.0f;               ///< The part of the ground range at the start of the frame.
		float to = 0.0f;                 ///< The part of the ground range at the end of the frame.
		float yaw = 0.0f;                ///< The heading at the end of the frame, degrees.
		float yawStep = 0.0f;            ///< The turn of the frame, degrees.
		float userPitch = 0.0f;          ///< The pitch of the frame that the user made, degrees.
		std::optional<WorldPoint> pivot; ///< The pivot of the gesture.
	};
	/// The state of the ground range that a stand-back keeps, while nothing moves the camera.
	struct GroundResume
	{
		CameraPlacement camera;
		GroundState ground;
		HandRigPose rig;
		float zoomValue = 0.0f;
		float zoomTarget = 0.0f;
		float pitchTarget = 0.0f;
		float pitchValue = 0.0f;
	};
	/// True if the zoom of the hand rig can go into the ground range.
	[[nodiscard]] bool GroundLookOn() const;
	/// The height of the bottom of the ground range for the zoom steps, metres. Zero if there is no ground range.
	[[nodiscard]] float ZoomGroundHeight() const;
	/// The zoom limits of the hand rig: the limits of the game, with the ground range below stage 0.
	[[nodiscard]] StageLimits RigZoomLimits(StageLimits limits) const;
	/// The camera goes from the hand rig into the ground range. Return false on an error.
	[[nodiscard]] bool EnterGround(CameraGame& game);
	/// Move the camera in the ground range in this frame. Return false on an error.
	[[nodiscard]] bool StepGround(CameraGame& game, const GroundStep& step);
	/// Move a camera from one point to another, and stop it in front of the objects on the way.
	[[nodiscard]] WorldPoint SweepGround(CameraGame& game, WorldPoint from, WorldPoint to);
	/// Remove a move of the land drag of the game that a camera near the land cannot use.
	[[nodiscard]] WorldPoint GuardGroundDrag(CameraGame& game, WorldPoint shift);
	/// Start the hand rig again in the ground range after a stand-back. Return true if it did.
	[[nodiscard]] bool ResumeGround(const CameraPlacement& camera);

	// The navigation of the hand rig: the flight, the soft map edge.
	/// A flight that a hotkey or the menu bar asked for.
	struct FlightRequest
	{
		enum class Kind
		{
			Spot,
			Reset,
		};
		Kind kind = Kind::Spot;
		CameraSpot spot;
		float northDegrees = 0.0f;
	};
	/// The view tilt that the pitch control gives for a raise at a zoom stage, degrees.
	[[nodiscard]] float PitchFor(float raise, float stage) const;
	/// The view that the user sees now, with the point at the view centre. Return nothing on an error.
	[[nodiscard]] std::optional<FlightView> ShownView(CameraGame& game);
	/// Start a flight from a view to a view. The rig starts from the camera that the user sees.
	void StartFlight(const FlightView& from, FlightView to);
	/// Start the flight of a double click of the game on the land, or of a request. Return false on an error.
	[[nodiscard]] bool StartFlights(CameraGame& game, bool leftPressed, StageLimits limits);
	/// Move the rig on the flight in this frame. Return false on an error.
	[[nodiscard]] bool StepFlight(CameraGame& game, bool keepAboveLand);
	/// Remove the part of a move of the game that goes into the soft edge of the map.
	[[nodiscard]] WorldPoint SoftenGameShift(CameraGame& game, WorldPoint shift);

	CameraSettings _settings;

	HeadingSmoother _heading;
	ZoomSmoother _zoom;
	bool _resync = true;
	bool _frameActive = false;
	double _seconds = 0.0;
	float _startDirection = 0.0f;
	float _writtenStage = 0.0f;
	float _deltaTime = 0.0f;
	float _lastWheel = 0.0f;
	float _pitchTarget = 0.0f;   ///< The raise that the user asked for, degrees.
	float _pitchValue = 0.0f;    ///< The smoothed raise, degrees.
	float _distanceScale = 1.0f; ///< The distance to the focus, divided by the distance of the game.
	float _clearance = 0.0f;     ///< The raise of the camera above the land, metres.
	bool _pitchFromInput = false;
	bool _middleHeld = false; ///< The middle button was held in the last frame.
	bool _pitchReset = false;
	float _lastStage = 0.0f;
	PitchView _view;
	PitchRange _range;
	std::optional<float> _lastMouseY;
	std::optional<float> _lastMouseX;
	std::optional<WorldPoint> _lastPressMouse;
	double _lastPress = -1e9;
	std::optional<WorldPoint> _lastLandPoint;
	std::optional<bool> _wheelToRestore;
	std::optional<CameraPlacement> _written;    ///< The camera that the loop gave to the transform.
	std::optional<CameraPlacement> _gameCamera; ///< The camera of the game for the same focus and heading.
	DragBefore _dragBefore;
	DragGrab _drag;
	bool _dragActive = false;
	WorldPoint _holdShift;
	float _appliedLookUp = 0.0f;           ///< The turn up of the camera from the camera of the game, degrees.
	VelocityFilter _dragVelocity;          ///< The speed of the focus while the user holds the land.
	bool _tracking = false;                ///< The user held the land in the last frame.
	Glide _glide;                          ///< The motion of the focus after the release.
	std::optional<WorldPoint> _glideFocus; ///< The focus that the glide gave to the game in this frame.
	std::optional<WorldPoint> _handCursor; ///< The point of the screen of the hand while it moves to the cursor.
	std::optional<WorldPoint> _shown;      ///< The camera position that the user saw at the end of the last frame.
	std::optional<WorldPoint> _anchor;     ///< The camera position that a view that turns in place holds.
	std::optional<WorldPoint> _viewFocus;  ///< The land at the view centre of a view that turns in place.
	CameraLoopTrace _trace;

	// The hand rig.
	std::optional<HandRigPose> _rig;            ///< The camera of the user, with no camera shake.
	std::vector<StageView> _stageTable;         ///< The zoom stage table of the game.
	std::optional<WorldPoint> _pivot;           ///< The land point that the gesture keeps at its place on the screen.
	std::optional<GameEases> _easesToRestore;   ///< The eases of the game that `EndFrame` puts back.
	std::optional<GameEases> _gameEases;        ///< The eases of the game, as the loop first read them.
	std::optional<float> _heldMinimumToRestore; ///< The smallest stage of a held object that `EndFrame` puts back.
	std::optional<float> _gameHeldMinimum;      ///< The smallest stage of a held object, as the loop first read it.
	bool _heldMinimumStopped = false;           ///< The game does not keep a smallest stage for a held object.
	WorldPoint _focusWritten;                   ///< The focus that the rig gave to the game.
	float _stageWritten = 0.0f;                 ///< The stage that the rig gave to the game.
	float _level = 0.0f;                        ///< The height of the focus that the rig gives to the game.
	WorldPoint _lastCursorTerrainBefore;        ///< The land point of the last zoom to the cursor, before the frame.
	bool _rigMiddleHeld = false;                ///< The middle button was held in the last frame of the rig.
	bool _rigRightHeld = false;                 ///< The right button was held in the last frame of the rig.
	bool _pivotOnCursor = false;                ///< The pivot is the land that was under the cursor.
	float _flattestStageTilt = 0.0f;            ///< The smallest tilt of the zoom stage table, degrees.
	std::optional<WorldPoint> _cursorLock;      ///< The input position where the rig keeps the cursor and the hand.
	std::optional<WorldPoint> _pressPoint;      ///< The point of the screen that the press of the middle button took.
	/// The rig does not keep the cursor until the release of the button, for this cause.
	std::optional<CursorLockOff> _cursorLockOff;
	unsigned _cursorLockOffTold = 0U; ///< One bit for each cause of a free cursor that the debug log told.
	/// The input of the last frame of the hand rig, for the gesture.
	struct FrameInput
	{
		bool middle = false;
		bool right = false;
		bool turning = false;
		bool pitching = false;
		bool zooming = false;
		bool pressed = false;
	};
	FrameInput _frameInput;
	Gesture _gesture = Gesture::None;                             ///< The gesture of the last frame.
	std::vector<std::pair<CameraEventKind, std::string>> _events; ///< The events of this frame for the game.
	std::optional<uint64_t> _armTouch;                            ///< The object that the arm touches (zero for the land).
	bool _groundExit = false;                                     ///< The camera left the ground range in this frame.
	bool _cursorLockTold = false;                                 ///< The event of the cursor lock in this hold was told.
	std::string _armTouchName;                                    ///< The name of the object that the arm touches.
	bool _wheelNotch = false;                                     ///< The wheel moved in this frame.
	bool _pivotLimited = false; ///< The pivot of the gesture is nearer than the land under the hand.

	// The spring arm. The rig is the camera with no obstacles. The shown camera is on the line from the arm pivot to
	// the rig, at the arm length from the pivot.
	std::optional<WorldPoint> _armPivot; ///< The point that the spring arm keeps the camera on a line to.
	bool _armClamped = false;            ///< The clamp into the map moved the camera and the arm pivot in the last frame.
	float _armLength = 0.0f;             ///< The distance from the arm pivot to the shown camera, metres.
	/// The move of the shown camera from the arm that keeps it out of the land, metres. It eases, and the next frame
	/// starts from it, so the camera does not jump back.
	WorldPoint _armPush;
	/// The distance from the arm pivot to the rig. Only a zoom and a new pivot change it; a turn and a pitch do not.
	std::optional<float> _desired;
	std::optional<bool> _collisionsToRestore;   ///< The collision setting of the game that `EndFrame` puts back.
	std::optional<bool> _gameIgnoresCollisions; ///< The collision setting of the game, as the loop first read it.

	// The pan, the edge scroll and the zoom of all modes.
	std::optional<float> _moveSpeedToRestore; ///< The move speed of the game that `EndFrame` puts back.
	std::optional<float> _gameMoveSpeed;      ///< The move speed of the game, as the loop last read it.
	float _writtenMoveSpeed = 0.0f;           ///< The move speed that the loop gave to the game.
	std::optional<bool> _dollyToRestore;      ///< The key zoom setting of the game that `EndFrame` puts back.
	std::optional<bool> _gameDollySmooth;     ///< The key zoom setting of the game, as the loop first read it.
	std::optional<Quaternion> _startRotation; ///< The rotation of the camera at the start of the frame of the game.

	// The glides after a turn and a pitch of the hand rig.
	RateFilter _turnRate;
	RateFilter _pitchRate;
	AngleGlide _turnGlide;
	AngleGlide _pitchGlide;
	bool _turnTracking = false;  ///< The user turned in the last frame.
	bool _pitchTracking = false; ///< The user pitched in the last frame.

	// The navigation of the hand rig.
	std::optional<FlightRequest> _flightRequest; ///< A flight that starts in the next frame of the hand rig.
	std::optional<CameraFlight> _flight;         ///< The flight of the hand rig, while it flies.
	std::optional<float> _zoomToStageBefore;     ///< The zoom stage that the game zoomed to before its frame.
	bool _rigLeftHeld = false;                   ///< The left button was held in the last frame of the rig.
	std::optional<FocusBox> _focusBox;           ///< The box that the game keeps its focus in.

	// The chase, the land drag and the smoothing of the mouse of the hand rig.
	/// The land drag of the hand rig, from the press of the right button.
	struct LandGrip
	{
		WorldPoint point;    ///< The gripped land.
		WorldPoint origin;   ///< The target camera at the grip.
		Quaternion rotation; ///< The rotation of the camera at the grip.
		WorldPoint cursor;   ///< The cursor at the grip, pixels.
		float fieldOfView = 60.0f;
		Vector2 screen; ///< The size of the screen at the grip, pixels.
		GripPlane plane;
		float distance = 0.0f; ///< The distance from the camera to the focus at the grip, metres.
		WorldPoint applied;    ///< The move of the target camera that the land drag made since the grip.
	};
	ChaseSpring _chase;
	float _chaseSlowLeft = 0.0f; ///< While this time is left, the chase is two times slower: a limit moved the target.
	bool _chaseMoving = false;   ///< The chase moved the camera in this frame.
	bool _resyncing = false;     ///< The loop starts again from the camera of the game in this call.
	std::optional<LandGrip> _grip;
	bool _gripStarted = false; ///< The land drag took a new grip in this frame.
	bool _gripRetake = false;  ///< The land drag takes a new grip in the next frame: the view turned or zoomed.
	bool _gripRefused = false; ///< The press of the right button was on the UI: no land drag until the release.
	InputSmoother _turnInput;
	InputSmoother _pitchInput;

	// The ground look of the hand rig.
	std::optional<GroundState> _ground;        ///< The camera in the ground range, while the zoom is in it.
	std::optional<GroundResume> _groundResume; ///< The ground range that a stand-back keeps.

	// The return from a stand-back.
	bool _handBack = false; ///< The mod stood back, and the next resync is the return.
	/// The camera that the mod wrote before the stand-back. If the camera is still there at the return, nothing moved it.
	std::optional<CameraPlacement> _standBackCamera;
	std::optional<CameraSpot> _standBackSpot; ///< The view of the user before the stand-back.
	/// The town camera state of the game, as the game had it after the last write of the rig.
	struct WrittenGameState
	{
		WorldPoint focus;
		float direction = 0.0f; ///< Degrees.
		float stage = 0.0f;
		StageView view;
	};
	/// The state that the rig wrote last. If the game still has it at the return, the game did not move the view of the
	/// user while the mod stood back.
	std::optional<WrittenGameState> _gameStateWritten;
	bool _groundCollisionsFailed = false; ///< The loop cannot stop the push of the game, so there is no ground range.
};

} // namespace moa_mod::camera
