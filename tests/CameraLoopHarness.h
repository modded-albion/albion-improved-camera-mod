/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cmath>

#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <core/CameraLoop.h>

namespace moa_mod::camera::test
{

/// A model of the town camera of the game, for the tests of the camera loop. It does in each frame what the camera
/// code of the game does, in the same order: the zoom stage and its limits, the smoothed height and tilt, the turn,
/// the key pan, the land drag and the hold during a zoom or a turn, the placement from the focus, the clamp into the
/// map, the push above the land and the collision sweep. It has no camera shake, no pinch and no zoom to the cursor.
///
/// The mouse is the cursor of the system: `WarpCursor` moves it, and a test adds the move of the user to it before a
/// frame. The hand uses the hand override of the frame, or else the mouse.
///
/// A test can replace parts of the model with values of a frame trace: the height and the tilt of the stage, the turn
/// of the game, the hand point and the land test of the cursor.
class FakeCameraGame final: public CameraGame
{
public:
	/// One row of the stage table: the height of the camera above the focus and the tilt.
	struct StageRow
	{
		float height = 0.0f;
		float tilt = 0.0f;
	};
	/// A round obstacle for the collision sweep of the camera.
	struct Obstacle
	{
		WorldPoint centre;
		float radius = 1.0f;
	};
	/// A box obstacle (a building) for the collision sweep of the camera, with sides parallel to the axes.
	struct Box
	{
		WorldPoint minimum;
		WorldPoint maximum;
	};
	/// The radius of the sphere of the collision sweep of the game.
	static constexpr float k_CollisionRadius = 0.6f;

	static constexpr std::array<StageRow, 5> k_StageTable {
	    {{6.0f, 20.0f}, {30.0f, 25.0f}, {90.0f, 45.0f}, {200.0f, 50.0f}, {1200.0f, 50.0f}}};
	static constexpr float k_FieldOfView = 60.0f;

	// The world.
	std::function<float(float, float)> ground = [](float, float) { return 100.0f; };
	float mapMinimum = -3000.0f;
	float mapMaximum = 3000.0f;
	/// The smallest z of the map, if it differs from the smallest x.
	std::optional<float> mapMinimumZ;
	std::vector<Obstacle> obstacles;
	std::vector<Box> boxes;
	/// The game does no push above the land and no collision sweep (the setting of the game).
	bool ignoreCollisions = false;
	/// The game has the sphere test of its camera.
	bool castAvailable = true;
	/// The land is a collider for the sphere test, on this Unity layer (in the game, `MoATerrain` on the land layer).
	/// Nothing: the sphere test finds only the obstacles.
	std::optional<int32_t> landColliderLayer;
	/// The setting of the game that holds the land under the cursor while the user zooms or turns.
	bool zoomRotateAboutHand = true;
	/// The input of the game sees the release of the middle button one frame after Unity does.
	bool lateMiddleRelease = false;
	/// The mouse button of the game gives only the left and the right button, as with Steam Input. It never gives the
	/// middle button, but the game still turns with it.
	bool gameButtonWithoutMiddle = false;
	/// The user holds a key that turns the camera with the mouse (a key that the user gave to the turn, or Steam
	/// Input), with the middle button of the mouse up.
	bool turnKeyHeld = false;
	/// The mod has the turn input of the game.
	bool turnInputAvailable = true;
	/// The mod has the mouse button of the game.
	bool gameButtonAvailable = true;
	float screenWidth = 1920.0f;
	float screenHeight = 1080.0f;
	/// The game eases the height of the focus to the land in each frame. The old tests do not model it.
	bool focusHeightEase = false;
	/// The game turns by the horizontal move of the cursor while the middle button is held, from the cursor of the
	/// last frame, degrees for each pixel. Zero turns only by `turnPerSecond`.
	float turnDegreesPerPixel = 0.0f;
	/// The hand holds an object. The game then keeps the zoom stage at the smallest stage for a held object or more.
	bool holdsObject = false;
	/// The smallest zoom stage of the game while the hand holds an object.
	static constexpr float k_HoldMinimumStage = 0.5f;
	/// The static value that the game reads in each frame for the smallest zoom stage of a held object.
	float heldObjectMinimumStage = k_HoldMinimumStage;
	/// The system does not move the cursor.
	bool warpFails = false;
	/// The move speed setting of the game. The game multiplies the key pan by it.
	float moveSpeed = 1.0f;
	/// The setting of the game that makes a key zoom continuous. Off, a key zoom moves by whole stages.
	bool dollySmooth = false;
	/// The user drags an object, so the game scrolls when the cursor is at the edge of the screen.
	bool edgeScrollActive = false;
	/// The game gives the box that it keeps its focus in.
	bool focusLimitsAvailable = true;
	/// A double click of the left button on these screen points does not count as a double click on the land (for
	/// example an object or the UI): the game does other things there.
	std::function<bool(WorldPoint)> clickOnObject = [](WorldPoint) { return false; };
	/// The zoom stage that the game zooms to by itself after a double click on the land. Less than zero is no zoom.
	float zoomToStage = -1.0f;
	int doubleClicks = 0; ///< The count of the double clicks of the game on the land.

	// The state of the game.
	WorldPoint focus {};
	float stage = 0.0f;
	float direction = 30.0f;
	float height = k_StageTable[0].height;
	float tilt = k_StageTable[0].tilt;
	CameraPlacement transform {};
	DragFields drag {};
	WorldPoint lastDraggedMouse {};
	bool ignoreWheel = false;
	GameEases eases {};
	float time = 0.0f;
	WorldPoint handPoint {};
	/// The cursor is over the UI of the game. The hold of the land does not test this.
	bool handOverUi = false;
	float minimumZoom = 0.0f;               ///< The smallest zoom stage of this frame.
	WorldPoint turnCursor {};               ///< The cursor of the last frame, for the turn of the middle button.
	std::optional<WorldPoint> handOverride; ///< The place of the hand on the screen in this frame.
	int warps = 0;                          ///< The count of the moves of the cursor by the loop.

	// The input of this frame.
	WorldPoint mouse {960.0f, 540.0f, 0.0f};
	std::array<bool, 3> buttons {};
	float wheel = 0.0f;
	WorldPoint pan {};          ///< The key pan, metres for each second.
	float turnPerSecond = 0.0f; ///< The turn of the keys or the horizontal drag, degrees for each second.
	Vector2 panKeys {};         ///< The pan keys of the game: x to the right, y forward, each from -1 to 1.
	float zoomKeys = 0.0f;      ///< The zoom keys: stages for each second when the key zoom is continuous. Out is positive.

	// The pan of the keys and the edge scroll of the game.
	Vector2 keySmooth {};        ///< The smoothed key pan of the game, units of the stick.
	float pushSpeed = 0.0f;      ///< The smoothed speed of the edge scroll.
	EdgeScrollFrame lastEdge {}; ///< The edge scroll of the last frame.
	WorldPoint lastPanMove {};   ///< The move of the focus by the key pan and the edge scroll in the last frame.

	// Values of a trace that replace the model in the next frame.
	std::optional<StageRow> scriptedStage; ///< The smoothed height and tilt of the stage after the frame.
	bool scriptedHand = false;             ///< The test sets the hand point. The game does not find it.
	std::optional<bool> cursorOnLand;      ///< The land test of the cursor.

	std::vector<std::string> errors;

	/// Put the focus on the land and place the camera as the game does at the start.
	void Start(WorldPoint focusXz, float startStage)
	{
		focus = {focusXz.x, GroundAt(focusXz.x, focusXz.z), focusXz.z};
		stage = startStage;
		const auto row = Row(stage);
		height = row.height;
		tilt = row.tilt;
		Place();
		handPoint = LandAt(transform, mouse).value_or(WorldPoint {});
	}

	/// Start from a state of a trace: the focus, the stage, and the smoothed height and tilt of the stage.
	void StartAt(WorldPoint startFocus, float startStage, StageRow view)
	{
		focus = startFocus;
		stage = startStage;
		height = view.height;
		tilt = view.tilt;
		Place();
	}

	/// Run the camera part of one frame of the game.
	void RunFrame(float deltaTime)
	{
		time += deltaTime;
		const auto start = transform.position;
		DoubleClick();
		minimumZoom = holdsObject ? heldObjectMinimumStage : 0.0f;
		if (zoomKeys != 0.0f)
		{
			// The keys zoom continuously with the setting, else by one whole stage for each press.
			if (dollySmooth)
			{
				stage += zoomKeys * deltaTime;
			}
			else if (!_zoomKeysWereHeld)
			{
				stage = std::round(stage + (zoomKeys > 0.0f ? 0.55f : -0.55f));
			}
		}
		_zoomKeysWereHeld = zoomKeys != 0.0f;
		if (wheel != 0.0f && !ignoreWheel)
		{
			stage = std::round(stage - wheel / 0.1f);
		}
		if (zoomToStage >= 0.0f)
		{
			// The zoom of the double click of the game: an ease of the stage to the target, then a snap.
			if (std::abs(stage - zoomToStage) < 1e-3f)
			{
				stage = zoomToStage;
				zoomToStage = -1.0f;
			}
			else
			{
				stage += (zoomToStage - stage) * (1.0f - std::pow(1.0f - 0.1f, 60.0f * deltaTime));
			}
		}
		// The game keeps the stage in its limits in each frame.
		stage = std::clamp(stage, minimumZoom, 4.0f);
		const auto row = Row(stage);
		const auto stageEase = 1.0f - std::pow(1.0f - eases.stage, deltaTime * 60.0f);
		const auto lastHeight = height;
		const auto lastTilt = tilt;
		height += (row.height - height) * stageEase;
		tilt += (row.tilt - tilt) * stageEase;
		if (scriptedStage)
		{
			height = scriptedStage->height;
			tilt = scriptedStage->tilt;
		}
		// The game also holds the land while the offset of the camera changes, for example while the height eases to
		// the stage after a zoom.
		const auto offsetChanging = std::abs(height - lastHeight) > 1e-4f || std::abs(tilt - lastTilt) > 1e-4f;
		direction = WrapDegrees(direction + turnPerSecond * deltaTime);
		if (TurnHeld() && turnDegreesPerPixel != 0.0f)
		{
			// The game turns by the move of the cursor from the cursor of the last frame. The press only takes the
			// cursor.
			if (_turnPressed)
			{
				direction = WrapDegrees(direction + (mouse.x - turnCursor.x) * turnDegreesPerPixel);
			}
			turnCursor = mouse;
		}
		_turnPressed = TurnHeld();
		focus = focus + pan * deltaTime;
		KeyPanAndEdgeScroll(deltaTime);
		if (focusHeightEase)
		{
			focus.y += (GroundAt(focus.x, focus.z) - focus.y) * (1.0f - std::pow(1.0f - eases.focusHeight, deltaTime * 60.0f));
		}
		drag.grab = drag.grab + pan * deltaTime;

		// The land drag of the right mouse button.
		const auto rightHeld = buttons[1];
		if (rightHeld && !_rightWasHeld)
		{
			drag.startedTime = time;
			if (auto land = LandAt(transform, mouse))
			{
				drag.grab = *land;
			}
			lastDraggedMouse = {};
			drag.dragValid = true;
		}
		if (!rightHeld && _rightWasHeld)
		{
			lastDraggedMouse = mouse;
			drag.dragValid = false;
		}
		_rightWasHeld = rightHeld;
		drag.dragValid = drag.dragValid && rightHeld;
		if (drag.dragValid)
		{
			drag.fade = 1.0f;
		}
		else
		{
			drag.fade *= std::pow(0.9f, 60.0f * deltaTime);
		}
		const auto zoomRotateHeld = TurnHeld() || (lateMiddleRelease && _middleWasHeld);
		_middleWasHeld = TurnHeld();
		drag.holdingZoomRotate = zoomRotateHeld;
		drag.zoomRotateChanging = zoomRotateAboutHand && (zoomRotateHeld || turnPerSecond != 0.0f || offsetChanging);
		if (drag.zoomRotateChanging)
		{
			// With the setting on, the game places the camera before the hold.
			Place();
		}
		const auto hold =
		    (drag.fade > 0.01f || drag.zoomRotateChanging) && !drag.noHandFocus && drag.ignoreHand + drag.inhibitHand == 0;
		lastFrameHeld = hold;
		if (hold)
		{
			const auto target = IsZero(lastDraggedMouse) ? mouse : lastDraggedMouse;
			const auto step = drag.fade >= 1.0f ? 1.0f - std::pow(0.9f, 60.0f * deltaTime) : 1.0f - std::pow(drag.fade, 0.2f);
			drag.holdCursor = drag.holdCursor + (target - drag.holdCursor) * step;
			drag.handCursor = drag.holdCursor;
			const auto ray = moa_mod::camera::ScreenRay(ScreenCameraOf(transform), {drag.holdCursor.x, drag.holdCursor.y});
			if (std::abs(ray.direction.y) > 1e-6f)
			{
				const auto t = (drag.grab.y - ray.origin.y) / ray.direction.y;
				const auto point = ray.origin + ray.direction * t;
				const auto shift = drag.grab - point;
				focus = focus + shift;
				transform.position = transform.position + shift;
			}
		}
		else
		{
			drag.holdCursor = mouse;
			drag.handCursor = mouse;
			if (auto land = LandAt(transform, mouse))
			{
				drag.grab = *land;
			}
		}
		if (!drag.zoomRotateChanging)
		{
			Place();
		}
		ClampFocus();
		if (ignoreCollisions)
		{
			if (drag.inhibitHand > 0)
			{
				--drag.inhibitHand;
			}
			return;
		}
		// The push above the land.
		const auto highest = HighestGroundAt(transform.position);
		if (transform.position.y < highest + 1.5f)
		{
			const auto raise = highest + 1.5f - transform.position.y;
			transform.position.y += raise;
			focus.y += raise;
			drag.inhibitHand = 4;
			++pushes;
		}
		Collide(start);
		if (drag.inhibitHand > 0)
		{
			--drag.inhibitHand;
		}
	}

	/// After the frame: the hand finds the land under the cursor in the camera that the user sees.
	void EndFrame()
	{
		const auto cursor = handOverride.value_or(mouse);
		handScreen = cursor;
		handOverride.reset();
		if (scriptedHand)
		{
			return;
		}
		if (auto land = LandAt(transform, cursor))
		{
			handPoint = *land;
		}
	}

	WorldPoint handScreen {}; ///< The place of the hand on the screen in the last frame.

	[[nodiscard]] float GroundAt(float x, float z) const { return std::max(ground(x, z), 90.0f); }

	/// The land under a point of the screen for a camera, or nothing if the ray does not meet the land.
	[[nodiscard]] std::optional<WorldPoint> LandAt(const CameraPlacement& camera, WorldPoint screen) const
	{
		const auto ray = moa_mod::camera::ScreenRay(ScreenCameraOf(camera), {screen.x, screen.y});
		float distance = 0.0f;
		float previous = 0.0f;
		for (int step = 0; step < 4000 && distance < 6000.0f; ++step)
		{
			const auto point = ray.origin + ray.direction * distance;
			const auto above = point.y - GroundAt(point.x, point.z);
			if (above <= 0.0f)
			{
				// Find the crossing between the two last samples.
				auto low = previous;
				auto high = distance;
				for (int refine = 0; refine < 40; ++refine)
				{
					const auto middle = (low + high) * 0.5f;
					const auto test = ray.origin + ray.direction * middle;
					(test.y - GroundAt(test.x, test.z) > 0.0f ? low : high) = middle;
				}
				return ray.origin + ray.direction * high;
			}
			previous = distance;
			distance += std::max(0.25f, above * 0.25f);
		}
		return std::nullopt;
	}

	[[nodiscard]] ScreenCamera ScreenCameraOf(const CameraPlacement& camera) const
	{
		return {.position = camera.position,
		        .rotation = camera.rotation,
		        .verticalFieldOfView = k_FieldOfView,
		        .width = screenWidth,
		        .height = screenHeight};
	}

	/// The land height that the game keeps its camera above, near a position.
	[[nodiscard]] float HighestGroundAtForTest(WorldPoint position) const { return HighestGroundAt(position); }

	bool lastFrameHeld = false; ///< The game held the land under the cursor in the last frame.
	int pushes = 0;             ///< The count of the pushes above the land.
	int collisions = 0;         ///< The count of the collision hits.
	int landQueries = 0;        ///< The count of the exact land queries of the camera loop.

	// CameraGame
	[[nodiscard]] WorldPoint Focus() const override { return focus; }
	void SetFocus(WorldPoint value) override { focus = value; }
	[[nodiscard]] float Direction() const override { return direction; }
	void SetDirection(float value) override { direction = value; }
	[[nodiscard]] float Stage() const override { return stage; }
	void SetStage(float value) override { stage = value; }
	[[nodiscard]] float StageTilt() const override { return tilt; }
	[[nodiscard]] float StageHeight() const override { return height; }
	[[nodiscard]] std::optional<StageLimits> ZoomLimits() override
	{
		return StageLimits {.minimum = minimumZoom, .maximum = 4.0f};
	}
	[[nodiscard]] std::optional<std::vector<StageView>> StageTable() override
	{
		std::vector<StageView> table;
		for (const auto& row : k_StageTable)
		{
			table.push_back({.height = row.height, .tilt = row.tilt});
		}
		return table;
	}
	void SetStageView(StageView view) override
	{
		height = view.height;
		tilt = view.tilt;
	}
	[[nodiscard]] std::optional<GameEases> Eases() override { return eases; }
	[[nodiscard]] bool SetEases(GameEases value) override
	{
		eases = value;
		return true;
	}
	[[nodiscard]] std::optional<float> HeldObjectMinimumStage() override { return heldObjectMinimumStage; }
	[[nodiscard]] bool SetHeldObjectMinimumStage(float value) override
	{
		heldObjectMinimumStage = value;
		return true;
	}
	[[nodiscard]] bool IgnoresCollisions() const override { return ignoreCollisions; }
	[[nodiscard]] bool SetIgnoresCollisions(bool value) override
	{
		ignoreCollisions = value;
		return true;
	}
	[[nodiscard]] std::optional<CameraPlacement> Camera() override { return transform; }
	[[nodiscard]] bool SetCamera(const CameraPlacement& placement) override
	{
		transform = placement;
		return true;
	}
	[[nodiscard]] std::optional<Ray> ScreenRay(WorldPoint screen) override
	{
		return moa_mod::camera::ScreenRay(ScreenCameraOf(transform), {screen.x, screen.y});
	}
	[[nodiscard]] std::optional<WorldPoint> LandUnderScreenPoint(WorldPoint screen) override
	{
		return LandAt(transform, screen);
	}
	[[nodiscard]] std::optional<bool> CursorOnLand() override
	{
		return cursorOnLand ? *cursorOnLand : LandAt(transform, mouse).has_value();
	}
	[[nodiscard]] std::optional<WorldPoint> ScreenPointOf(WorldPoint point) override
	{
		auto screen = ScreenPoint(ScreenCameraOf(transform), point);
		if (!screen)
		{
			return std::nullopt;
		}
		return WorldPoint {screen->x, screen->y, Length(point - transform.position)};
	}
	[[nodiscard]] std::optional<Vector2> ScreenSize() override { return Vector2 {screenWidth, screenHeight}; }
	[[nodiscard]] std::optional<float> FieldOfView() override { return k_FieldOfView; }
	[[nodiscard]] std::optional<float> MoveSpeed() override { return moveSpeed; }
	[[nodiscard]] bool SetMoveSpeed(float value) override
	{
		moveSpeed = value;
		return true;
	}
	[[nodiscard]] std::optional<bool> DollySmooth() override { return dollySmooth; }
	[[nodiscard]] bool SetDollySmooth(bool value) override
	{
		dollySmooth = value;
		return true;
	}
	[[nodiscard]] std::optional<EdgeScrollFrame> EdgeScroll() override { return lastEdge; }
	[[nodiscard]] std::optional<float> ZoomToStage() override { return zoomToStage; }
	void SetZoomToStage(float value) override { zoomToStage = value; }
	[[nodiscard]] std::optional<FocusBox> FocusLimits() override
	{
		if (!focusLimitsAvailable)
		{
			return std::nullopt;
		}
		return FocusBox {.minimumX = mapMinimum, .minimumZ = mapMinimum, .maximumX = mapMaximum, .maximumZ = mapMaximum};
	}
	[[nodiscard]] std::optional<float> HighestGround(WorldPoint position) override { return HighestGroundAt(position); }
	[[nodiscard]] std::optional<float> LandHeight(WorldPoint position) override
	{
		++landQueries;
		return GroundAt(position.x, position.z);
	}
	[[nodiscard]] std::optional<SphereHit> CastSphere(WorldPoint origin, WorldPoint along, float radius, float length,
	                                                  int32_t layers) override
	{
		if (!castAvailable)
		{
			return std::nullopt;
		}
		++casts;
		// The obstacles are on the default layer. As in Unity, a sphere that starts in an object finds it at the
		// distance zero.
		auto hit = (layers & 1) != 0 ? Sweep(origin, along, radius, true) : std::nullopt;
		auto collider = k_ObstacleCollider;
		if (landColliderLayer && (layers & (1 << *landColliderLayer)) != 0)
		{
			if (auto land = SweepLand(origin, along, radius, length); land && (!hit || *land < *hit))
			{
				hit = land;
				collider = k_LandCollider;
			}
		}
		if (!hit || *hit > length)
		{
			return SphereHit {};
		}
		return SphereHit {.hit = true, .distance = *hit, .collider = collider};
	}
	int casts = 0; ///< The count of the sphere tests of the loop.
	/// The identifiers of the colliders that the sphere test gives.
	static constexpr uint64_t k_ObstacleCollider = 1;
	static constexpr uint64_t k_LandCollider = 2;

	/// The distance from a point to the nearest surface of an obstacle. Less than zero is in the obstacle.
	[[nodiscard]] float ObstacleClearance(WorldPoint point) const
	{
		float nearest = 1e9f;
		for (const auto& obstacle : obstacles)
		{
			nearest = std::min(nearest, Length(point - obstacle.centre) - obstacle.radius);
		}
		for (const auto& box : boxes)
		{
			const WorldPoint outside {std::max({box.minimum.x - point.x, 0.0f, point.x - box.maximum.x}),
			                          std::max({box.minimum.y - point.y, 0.0f, point.y - box.maximum.y}),
			                          std::max({box.minimum.z - point.z, 0.0f, point.z - box.maximum.z})};
			const auto inside = std::min({point.x - box.minimum.x, box.maximum.x - point.x, point.y - box.minimum.y,
			                              box.maximum.y - point.y, point.z - box.minimum.z, box.maximum.z - point.z});
			nearest = std::min(nearest, Length(outside) > 0.0f ? Length(outside) : -inside);
		}
		return nearest;
	}
	void ClampFocus() override
	{
		const WorldPoint clamped {std::clamp(focus.x, mapMinimum, mapMaximum), focus.y,
		                          std::clamp(focus.z, mapMinimumZ.value_or(mapMinimum), mapMaximum)};
		transform.position = transform.position + (clamped - focus);
		focus = clamped;
	}
	[[nodiscard]] DragFields Drag() const override { return drag; }
	void SetDragGrab(WorldPoint grab) override { drag.grab = grab; }
	void SetLastCursorTerrain(WorldPoint point) override { drag.lastCursorTerrain = point; }
	void SetDragFade(float fade) override { drag.fade = fade; }
	void SetHoldBlock(int32_t frames) override { drag.inhibitHand = frames; }
	[[nodiscard]] std::optional<HandState> Hand() override
	{
		return HandState {.point = handPoint, .visible = true, .overUi = handOverUi};
	}
	void SetHandOverride(WorldPoint screen) override { handOverride = screen; }
	/// The game turns the camera while the input of its turn with the mouse is held.
	[[nodiscard]] bool TurnHeld() const { return buttons[2] || turnKeyHeld; }
	[[nodiscard]] std::optional<bool> ReadMouseButton(ButtonSource source, int32_t button) override
	{
		if (button < 0 || button >= 3)
		{
			return false;
		}
		const auto unity = buttons[static_cast<size_t>(button)];
		switch (source)
		{
		case ButtonSource::TurnInput:
			if (!turnInputAvailable || button != k_MiddleButton)
			{
				return std::nullopt;
			}
			return TurnHeld();
		case ButtonSource::GameButton:
			if (!gameButtonAvailable)
			{
				return std::nullopt;
			}
			return unity && !(gameButtonWithoutMiddle && button == k_MiddleButton);
		case ButtonSource::Unity:
			return unity;
		}
		return std::nullopt;
	}
	[[nodiscard]] std::optional<WorldPoint> MousePosition() override { return mouse; }
	[[nodiscard]] std::optional<WorldPoint> InputPosition() override { return mouse; }
	[[nodiscard]] std::optional<float> WheelAxis() override { return wheel; }
	[[nodiscard]] std::optional<bool> IgnoreWheel() override { return ignoreWheel; }
	[[nodiscard]] bool SetIgnoreWheel(bool value) override
	{
		ignoreWheel = value;
		return true;
	}
	[[nodiscard]] std::optional<WorldPoint> WarpCursor(WorldPoint screen) override
	{
		if (warpFails)
		{
			return std::nullopt;
		}
		mouse = screen;
		++warps;
		return screen;
	}
	void SetTurnCursor(WorldPoint screen) override { turnCursor = screen; }
	void ReportError(std::string message) override { errors.push_back(std::move(message)); }
	void Debug(std::string message) override { debugLines.push_back(std::move(message)); }
	void Event(CameraEventKind kind, std::string text) override { events.emplace_back(kind, std::move(text)); }
	[[nodiscard]] std::optional<std::string> ColliderName(uint64_t collider) override
	{
		if (collider == k_ObstacleCollider)
		{
			return "obstacle";
		}
		return collider == k_LandCollider ? std::optional<std::string>("MoATerrain") : std::nullopt;
	}
	std::vector<std::pair<CameraEventKind, std::string>> events; ///< The events of the loop.
	std::vector<std::string> debugLines;                         ///< The lines of the debug log of the loop.

	/// A blend of the camera of the game to a new state: the camera goes from the pose at the start to the camera of
	/// the new state, with an ease in and out, as the game does it. The camera mod stands back while it runs.
	struct Blend
	{
		CameraPlacement from;
		float left = 1.0f; ///< The part of the blend that is not done, 1 to 0.
		float seconds = 1.0f;
	};
	std::optional<Blend> blend;

	/// Start a blend to a focus, a heading and a stage.
	void StartBlend(WorldPoint toFocus, float toDirection, float toStage, float seconds)
	{
		blend = Blend {.from = transform, .left = 1.0f, .seconds = seconds};
		focus = {toFocus.x, GroundAt(toFocus.x, toFocus.z), toFocus.z};
		direction = toDirection;
		stage = toStage;
		const auto row = Row(stage);
		height = row.height;
		tilt = row.tilt;
	}

	/// One frame of the blend, in place of the frame of the camera of the game.
	void BlendFrame(float deltaTime)
	{
		time += deltaTime;
		Place();
		auto& state = *blend;
		const auto done = 1.0f - state.left;
		const auto weight = std::clamp((3.0f - 2.0f * done) * done * done, 0.0f, 1.0f);
		transform.position = state.from.position + (transform.position - state.from.position) * weight;
		// A normalised linear blend of the rotations: near enough to a spherical blend for the model.
		auto to = transform.rotation;
		if (Dot(WorldPoint {to.x, to.y, to.z},
		        WorldPoint {state.from.rotation.x, state.from.rotation.y, state.from.rotation.z}) +
		        to.w * state.from.rotation.w <
		    0.0f)
		{
			to = {.x = -to.x, .y = -to.y, .z = -to.z, .w = -to.w};
		}
		const Quaternion mixed {.x = state.from.rotation.x + (to.x - state.from.rotation.x) * weight,
		                        .y = state.from.rotation.y + (to.y - state.from.rotation.y) * weight,
		                        .z = state.from.rotation.z + (to.z - state.from.rotation.z) * weight,
		                        .w = state.from.rotation.w + (to.w - state.from.rotation.w) * weight};
		const auto length = std::sqrt(mixed.x * mixed.x + mixed.y * mixed.y + mixed.z * mixed.z + mixed.w * mixed.w);
		transform.rotation = {.x = mixed.x / length, .y = mixed.y / length, .z = mixed.z / length, .w = mixed.w / length};
		state.left -= deltaTime / state.seconds;
		if (state.left <= 0.0f)
		{
			blend.reset();
		}
	}

private:
	[[nodiscard]] static bool IsZero(WorldPoint point) { return point.x == 0.0f && point.y == 0.0f && point.z == 0.0f; }

	[[nodiscard]] static StageRow Row(float value)
	{
		const auto clamped = std::clamp(value, 0.0f, static_cast<float>(k_StageTable.size() - 1));
		const auto index = std::min(static_cast<size_t>(clamped), k_StageTable.size() - 2);
		const auto part = clamped - static_cast<float>(index);
		const auto& a = k_StageTable[index];
		const auto& b = k_StageTable[index + 1];
		return {a.height + (b.height - a.height) * part, a.tilt + (b.tilt - a.tilt) * part};
	}

	/// The key pan and the edge scroll of the game, with the camera at the frame start.
	void KeyPanAndEdgeScroll(float deltaTime)
	{
		// The keys move the stick to 50 units with the smoothing of the game: faster up than down.
		const auto accelerate = 1.0f - std::pow(1.0f - 0.033f, 60.0f * deltaTime);
		const auto decelerate = 1.0f - std::pow(1.0f - 0.132f, 60.0f * deltaTime);
		const auto smoothAxis = [&](float value, float key) {
			const auto target = key * k_GameKeyPanSpeed;
			value += (target - value) * (std::abs(value) < std::abs(target) ? accelerate : decelerate);
			return value * value < 1e-6f ? 0.0f : value;
		};
		keySmooth = {.x = smoothAxis(keySmooth.x, panKeys.x), .y = smoothAxis(keySmooth.y, panKeys.y)};
		const auto factor = GamePanHeightFactor(transform.position.y) * moveSpeed * deltaTime;
		Vector2 move {.x = keySmooth.x * factor, .y = keySmooth.y * factor};
		lastEdge = {};
		if (edgeScrollActive)
		{
			// The speed of the edge scroll is a part of the distance to the land at the screen centre, smoothed.
			const auto centre = LandAt(transform, {screenWidth * 0.5f, screenHeight * 0.5f, 0.0f});
			const auto distance = centre ? Length(*centre - transform.position) : 50.0f;
			pushSpeed += (distance * 0.4f - pushSpeed) * 0.1f;
			const auto edge = EdgeScrollDirection(mouse, {screenWidth, screenHeight});
			move.x += edge.x * pushSpeed * deltaTime;
			move.y += edge.y * pushSpeed * deltaTime;
			lastEdge = {.active = true, .speed = pushSpeed, .deltaTime = deltaTime, .cursor = mouse};
		}
		else
		{
			pushSpeed *= 0.9f;
		}
		lastPanMove = PanMove(move, transform.rotation);
		focus = focus + lastPanMove;
		drag.grab = drag.grab + lastPanMove;
	}

	void Place()
	{
		const CameraRig rig {.focus = focus, .direction = direction, .tilt = tilt, .distance = StageDistance(height, tilt)};
		transform = {.position = RigPosition(rig), .rotation = RigRotation(rig)};
	}

	[[nodiscard]] float HighestGroundAt(WorldPoint position) const
	{
		float highest = GroundAt(position.x, position.z);
		for (const auto& [x, z] :
		     {std::pair {1.5f, 0.0f}, std::pair {-1.5f, 0.0f}, std::pair {0.0f, 1.5f}, std::pair {0.0f, -1.5f}})
		{
			highest = std::max(highest, GroundAt(position.x + x, position.z + z));
		}
		return highest + 2.0f;
	}

	/// The distance that a sphere moves on a line from `origin` before it touches the land collider, or nothing. The
	/// land collider of the game did not give a sphere that starts in the land at the distance zero: in the game the
	/// arm found it 0.3 to 1.9 metres from a pivot on the land. Thus a start in the land touches it at the first step.
	[[nodiscard]] std::optional<float> SweepLand(WorldPoint origin, WorldPoint along, float radius, float length) const
	{
		constexpr float k_Step = 0.05f;
		for (float distance = k_Step; distance <= length; distance += k_Step)
		{
			const auto centre = origin + along * distance;
			if (centre.y - GroundAt(centre.x, centre.z) < radius)
			{
				return distance;
			}
		}
		return std::nullopt;
	}

	/// The distance that a sphere moves on a line from `origin` before it touches an obstacle, or nothing. A sphere
	/// that starts in an obstacle touches it at zero if `insideHits`, else it does not touch that obstacle. A box
	/// grows by the radius on each side, so a hit near a corner of a box is a little early.
	[[nodiscard]] std::optional<float> Sweep(WorldPoint origin, WorldPoint along, float radius, bool insideHits) const
	{
		std::optional<float> nearest;
		const auto take = [&](float hit) {
			if (!nearest || hit < *nearest)
			{
				nearest = hit;
			}
		};
		for (const auto& obstacle : obstacles)
		{
			const auto toCentre = obstacle.centre - origin;
			const auto reach = (obstacle.radius + radius) * (obstacle.radius + radius);
			if (Dot(toCentre, toCentre) < reach)
			{
				if (insideHits)
				{
					take(0.0f);
				}
				continue;
			}
			const auto ahead = Dot(toCentre, along);
			const auto across = Dot(toCentre, toCentre) - ahead * ahead;
			if (across > reach || ahead < 0.0f)
			{
				continue;
			}
			take(ahead - std::sqrt(reach - across));
		}
		for (const auto& box : boxes)
		{
			const std::array<float, 3> low {box.minimum.x - radius, box.minimum.y - radius, box.minimum.z - radius};
			const std::array<float, 3> high {box.maximum.x + radius, box.maximum.y + radius, box.maximum.z + radius};
			const std::array<float, 3> from {origin.x, origin.y, origin.z};
			const std::array<float, 3> step {along.x, along.y, along.z};
			float enter = -1e30f;
			float leave = 1e30f;
			bool misses = false;
			for (size_t axis = 0; axis < 3; ++axis)
			{
				if (std::abs(step[axis]) < 1e-9f)
				{
					misses = misses || from[axis] < low[axis] || from[axis] > high[axis];
					continue;
				}
				const auto a = (low[axis] - from[axis]) / step[axis];
				const auto b = (high[axis] - from[axis]) / step[axis];
				enter = std::max(enter, std::min(a, b));
				leave = std::min(leave, std::max(a, b));
			}
			if (misses || enter > leave || leave < 0.0f)
			{
				continue;
			}
			if (enter < 0.0f)
			{
				if (insideHits)
				{
					take(0.0f);
				}
				continue;
			}
			take(enter);
		}
		return nearest;
	}

	/// The sweep of a sphere from the camera position at the frame start to the new position, as the game does it.
	/// A hit stops the camera 1 metre before the hit (behind the start for a hit nearer than 1 metre) and moves the
	/// focus by the same change.
	void Collide(WorldPoint start)
	{
		const auto move = transform.position - start;
		const auto length = Length(move);
		if (length < 1e-5f)
		{
			return;
		}
		const auto along = move * (1.0f / length);
		const auto nearest = Sweep(start, along, k_CollisionRadius, false);
		if (!nearest || *nearest - 1.0f >= length)
		{
			return;
		}
		const auto stopped = start + along * (*nearest - 1.0f);
		focus = focus + (stopped - transform.position);
		transform.position = stopped;
		drag.inhibitHand = 4;
		++collisions;
	}

	/// The double click of the game: a second press of the left button in 0.5 seconds, within 2 percent of the height
	/// of the screen. On the land (not on an object), the game zooms to stage 1 from above stage 1.1, else to stage 0.5.
	void DoubleClick()
	{
		const auto pressed = buttons[0] && !_leftWasHeld;
		_leftWasHeld = buttons[0];
		if (!pressed)
		{
			return;
		}
		const auto near = Length(mouse - _lastClickPosition) < screenHeight * 0.02f;
		if (time < _lastClickTime + 0.5f && near && !handOverUi && !clickOnObject(mouse) && LandAt(transform, mouse))
		{
			zoomToStage = stage > 1.1f ? 1.0f : 0.5f;
			++doubleClicks;
		}
		_lastClickTime = time;
		_lastClickPosition = mouse;
	}

	bool _leftWasHeld = false;
	float _lastClickTime = -10.0f;
	WorldPoint _lastClickPosition {};
	bool _rightWasHeld = false;
	bool _middleWasHeld = false;
	bool _zoomKeysWereHeld = false;
	bool _turnPressed = false;
};

/// The game model and the camera loop, run frame by frame in the order of the research mod: `Before`, the frame of
/// the game, `After`, and then the hand.
struct CameraLoopHarness
{
	FakeCameraGame game;
	CameraLoop loop;
	double seconds = 0.0;

	explicit CameraLoopHarness(const moa_mod::camera::CameraSettings& settings)
	    : loop(settings)
	{
	}

	/// The camera at each stage of the last frame.
	CameraPlacement atStart;
	CameraPlacement beforeGame;
	CameraPlacement afterGame;

	void Frame(float deltaTime)
	{
		seconds += deltaTime;
		atStart = game.transform;
		const auto active = loop.Before(game, deltaTime, seconds);
		beforeGame = game.transform;
		game.RunFrame(deltaTime);
		afterGame = game.transform;
		if (loop.EndFrame(game) && active)
		{
			static_cast<void>(loop.After(game));
		}
		game.EndFrame();
	}

	/// The move of the camera in the frame of the game, in the last frame.
	[[nodiscard]] float GameMove() const { return Length(afterGame.position - beforeGame.position); }
	/// The move of the camera by the loop (`Before` and `After`), in the last frame.
	[[nodiscard]] float LoopMove() const
	{
		return Length(beforeGame.position - atStart.position) + Length(game.transform.position - afterGame.position);
	}
	/// The move of the camera that the user saw, in the last frame.
	[[nodiscard]] float ShownMove() const { return Length(game.transform.position - atStart.position); }

	/// The camera position that the user sees.
	[[nodiscard]] WorldPoint Shown() const { return game.transform.position; }
};

/// Turn off the camera feel of batch A (the glide after a turn and a pitch, the continuous zoom and the pan speed) and
/// of batch C (the soft edge of the map and the smooth rise over the land), the ground look of batch D and the short
/// smoothing of the mouse. Then the tests from before batch A see the zoom by stages, no turn after the release, the
/// turn of each move of the mouse in its frame and the hard edge, as they expect. The chase and the land grip stay on.
inline void KeepTheFeelOfTheOldTests(moa_mod::camera::CameraSettings& settings)
{
	settings.mouseSmoothing = 0.0;
	settings.turnInertia = 0.0;
	settings.pitchInertia = 0.0;
	settings.continuousZoom = false;
	settings.panSpeed = 0.0;
	settings.panAvoidance = false;
	settings.edgeMargin = 0.0;
	settings.groundLook = false;
}

/// One frame of a camera trace of an in-game test, as `camera_trace.py --extract` writes it.
struct TraceFrame
{
	int frame = 0;
	float deltaTime = 0.0f;
	bool middle = false;
	bool right = false;
	WorldPoint mouse;
	float wheel = 0.0f;
	bool cursorOnLand = false;
	WorldPoint focus; ///< The focus that the game got at the start of the frame.
	float stage = 0.0f;
	float direction = 0.0f;     ///< The heading at the start of the frame.
	float gameDirection = 0.0f; ///< The heading after the frame of the game.
	float height = 0.0f;        ///< The smoothed height of the stage at the start of the frame.
	float tilt = 0.0f;
	float gameHeight = 0.0f; ///< The smoothed height of the stage after the frame of the game.
	float gameTilt = 0.0f;
	WorldPoint hand;
	WorldPoint oldCamera;       ///< The camera that the user saw with the mod of that test.
	WorldPoint oldPivot;        ///< The pivot of the hand rig in that test. Zero if the trace has none.
	WorldPoint mouseDelta;      ///< The move of the mouse from the place where the mod put the cursor in the last frame.
	float raise = 0.0f;         ///< The raise of the view in that test, degrees.
	float highestGround = 0.0f; ///< The land height that the game keeps its camera above, at the camera of the test.
	WorldPoint cursorLand;      ///< The land under the cursor in the camera of the test. Zero if the cursor was not on it.
};

/// Read the extract of the trace (`camera_trace.py --extract`).
[[nodiscard]] inline std::vector<TraceFrame> ReadTraceFrames(const std::string& path)
{
	std::ifstream file(path);
	std::vector<TraceFrame> frames;
	std::string line;
	if (!std::getline(file, line))
	{
		return frames;
	}
	std::map<std::string, size_t> columns;
	{
		std::stringstream header(line);
		for (std::string name; std::getline(header, name, ',');)
		{
			columns.emplace(name, columns.size());
		}
	}
	while (std::getline(file, line))
	{
		std::vector<float> cells;
		std::stringstream row(line);
		for (std::string cell; std::getline(row, cell, ',');)
		{
			cells.push_back(cell.empty() ? 0.0f : std::stof(cell));
		}
		const auto value = [&](const std::string& name) {
			const auto column = columns.find(name);
			return column != columns.end() && column->second < cells.size() ? cells[column->second] : 0.0f;
		};
		const auto point = [&](const std::string& name) {
			return WorldPoint {value(name + "X"), value(name + "Y"), value(name + "Z")};
		};
		frames.push_back({.frame = static_cast<int>(value("frame")),
		                  .deltaTime = value("dt"),
		                  .middle = value("buttonMiddle") != 0.0f,
		                  .right = value("buttonRight") != 0.0f,
		                  .mouse = {value("mouseX"), value("mouseY"), 0.0f},
		                  .wheel = value("wheel"),
		                  .cursorOnLand = value("cursorOnLand") != 0.0f,
		                  .focus = point("focusBefore"),
		                  .stage = value("stageBefore"),
		                  .direction = value("directionBefore"),
		                  .gameDirection = value("directionAfterGame"),
		                  .height = value("heightBefore"),
		                  .tilt = value("tiltBefore"),
		                  .gameHeight = value("heightAfterGame"),
		                  .gameTilt = value("tiltAfterGame"),
		                  .hand = point("hand"),
		                  .oldCamera = point("cameraFinalPos"),
		                  .oldPivot = point("pivot"),
		                  .mouseDelta = {value("mouseDeltaX"), value("mouseDeltaY"), 0.0f},
		                  .raise = value("raise"),
		                  .highestGround = value("highestGround"),
		                  .cursorLand = point("cursorLand")});
	}
	return frames;
}

} // namespace moa_mod::camera::test
