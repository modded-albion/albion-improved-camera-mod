/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <common/Result.h>
#include <mod_core/UnityMath.h>
#include <nlohmann/json.hpp>

namespace moa_mod::autotest
{

constexpr float k_ClickSeconds = 0.05f;     ///< The time that a click holds the button.
constexpr float k_ClickGapSeconds = 0.1f;   ///< The time between the two clicks of a double click.
constexpr float k_HotkeySeconds = 0.1f;     ///< The time that a hotkey holds its keys.
constexpr float k_AfterClickSeconds = 0.1f; ///< The time after a click or a hotkey before the next step.
/// The time after the end of a blend of the camera of the game before the next step. The camera mod is active again in
/// it.
constexpr float k_AfterBlendSeconds = 0.5f;

/// A mouse button of a scenario.
enum class Button : uint8_t
{
	Left,
	Right,
	Middle,
};

constexpr size_t k_ButtonCount = 3;

[[nodiscard]] std::string_view Name(Button button);

/// A point of the screen as parts of the width and the height, from the top left corner of the screen.
struct ScreenFraction
{
	float x = 0.5f;
	float y = 0.5f;
};

/// Where the setup puts the camera.
struct Placement
{
	enum class Kind : uint8_t
	{
		Current,      ///< Do not move the camera.
		Point,        ///< The focus at a point of the world (x and z).
		Object,       ///< The focus at a game object, found by its name.
		ObjectOfType, ///< The focus at the first object that has a component of a class.
		Steepest,     ///< The focus at the foot of the steepest land near the camera, with the view to the slope.
		MapEdge,      ///< The focus near the edge of the map, with the view to the edge.
	};
	Kind kind = Kind::Current;
	WorldPoint point;             ///< `Point`: x and z of the focus.
	std::string name;             ///< `Object`: the name of the game object. `ObjectOfType`: the name of the class.
	WorldPoint offset;            ///< `Object` and `ObjectOfType`: the move of the focus from the object, x and z.
	float radius = 120.0f;        ///< `Steepest`: the distance from the camera that the search covers, metres.
	float step = 8.0f;            ///< `Steepest`: the distance between two samples of the land, metres.
	float inset = 60.0f;          ///< `MapEdge`: the distance from the focus to the edge of the map, metres.
	std::optional<float> heading; ///< The heading of the camera, degrees. Nothing keeps the heading of the place.
};

/// What the scenario does before its steps.
struct Setup
{
	std::optional<float> stage;   ///< The zoom stage. Nothing keeps the zoom stage of the game.
	std::optional<float> heading; ///< The heading, degrees. Nothing keeps the heading of the game.
	Placement place;
	ScreenFraction cursor {0.5f, 0.6f};
	nlohmann::json settings = nlohmann::json::object(); ///< Values of the camera settings for this scenario.
	std::string holdObject;                             ///< A carried resource that the setup puts at the hand. Empty for none.
	/// An object of the world that the setup puts in the hand: the class of its component of the game, and a text of
	/// its name. The nearest such object to the camera goes into the hand. An empty class is none.
	std::string pickupClass;
	std::string pickupName;
	float settleSeconds = 1.0f;
};

/// One input step. The steps run one after the other; each step starts when the step before it ends.
struct Step
{
	enum class Kind : uint8_t
	{
		Press,       ///< Press a button, at a point if `at` has a value.
		Release,     ///< Release a button.
		Move,        ///< Move the mouse by `right` and `up` pixels in `seconds`, at a constant speed.
		Drag,        ///< Press a button, move the mouse, then release the button (unless `keepHeld`).
		Wheel,       ///< Turn the wheel by `notches` notches (more than zero zooms in), one notch each `interval`.
		Keys,        ///< Hold keys for `seconds`.
		Hotkey,      ///< Hold the keys of a hotkey (for example Ctrl+F1) for a short time.
		Click,       ///< Press and release a button.
		DoubleClick, ///< Two clicks of a button.
		Wait,        ///< No input for `seconds`.
		Blend,       ///< The game blends its camera to a new view in `seconds`, and the camera mod stands back.
	};
	Kind kind = Kind::Wait;
	Button button = Button::Left;
	std::optional<ScreenFraction> at;
	float right = 0.0f; ///< Pixels to the right.
	float up = 0.0f;    ///< Pixels up.
	float seconds = 0.0f;
	int notches = 0;
	float interval = 0.08f;
	bool keepHeld = false;
	std::vector<std::string> keys; ///< Names of the Unity `KeyCode` enum, for example "W" or "LeftControl".
	std::string hotkey;            ///< The text of the hotkey, as the settings write it.
	float forward = 0.0f;          ///< `Blend`: the move of the focus on the heading, metres.
	float turn = 0.0f;             ///< `Blend`: the change of the heading, degrees.
	std::optional<float> stage;    ///< `Blend`: the zoom stage at the end. Nothing keeps the stage.

	/// The time that the step takes, seconds.
	[[nodiscard]] float Duration() const;
};

/// A rule that the run of a scenario must obey.
struct Check
{
	enum class Rule : uint8_t
	{
		PivotFixed,      ///< While the middle button is held, the pivot stays at its place on the screen.
		DesiredConstant, ///< While the middle button is held, only a zoom changes the desired distance.
		CursorLock,      ///< While the middle button is held, the hand rig keeps the cursor on the pivot.
		AboveLand,       ///< The camera stays above the land height of the game.
		NotInObstacle,   ///< The camera is never in an object.
		NoJump,          ///< The camera never jumps, and no value is not a number.
		GlideStops,      ///< After the steps, the camera stops in `seconds`.
		ReturnsToStart,  ///< At the end, the camera is at its place before the steps.
		Event,           ///< The count of the events of a kind that contain a text.
		Change,          ///< The change of a value from the start of the steps to the end.
		Flew,            ///< A flight of the hand rig ran.
		Ground,          ///< The zoom went into the ground range.
		HoldsObject,     ///< The hand held an object during the steps.
		NoInvariant,     ///< No rule of the black box of the camera broke (only in the game).
		Faded,           ///< In a frame of the steps, the fade changed at least `min` objects (default 1), and never
		                 ///< more than `max` objects.
		HeldVisible,     ///< While the hand holds an object, the fade does not change it and it shows solid.
		FadeRestored,    ///< At the end, the fade changes no object.
		MaxSpeed,        ///< The camera never moves faster than `max` metres for each second (not in a flight).
		MaxAcceleration, ///< The speed of the camera never changes faster than `max` metres for each second squared.
	};
	Rule rule = Rule::NoJump;
	float pixels = 2.0f;          ///< `PivotFixed`.
	float percent = 0.1f;         ///< `DesiredConstant`.
	float metres = 0.5f;          ///< `AboveLand`, `ReturnsToStart`.
	float degrees = 0.5f;         ///< `ReturnsToStart`.
	float seconds = 3.0f;         ///< `GlideStops`.
	std::string kind;             ///< `Event`: the name of the kind of event.
	std::string contains;         ///< `Event`: a text that the event contains.
	std::string of;               ///< `Change`: heading, raise, pitch, stage, camera, desired, height.
	std::optional<float> minimum; ///< `Event`, `Change`: the smallest permitted value.
	std::optional<float> maximum; ///< `Event`, `Change`: the largest permitted value.
	bool offlineOnly = false;     ///< Check only in the model of the game.
	bool gameOnly = false;        ///< Check only in the game.

	[[nodiscard]] std::string Describe() const;
};

[[nodiscard]] std::string_view Name(Check::Rule rule);

/// The world of the model of the game, for the offline run.
struct OfflineWorld
{
	std::string name = "flat"; ///< flat, hills, buildings, cliff, ridge, mapEdge.
	std::vector<float> framesPerSecond {60.0f};
	std::string skip; ///< Not empty: the offline run skips the scenario, for this reason.
};

/// A test of the camera controls: the setup, the input steps and the checks.
struct Scenario
{
	std::string name;
	std::string description;
	Setup setup;
	std::vector<Step> steps;
	std::vector<Check> checks;
	float endSeconds = 3.0f;   ///< After the steps, the longest wait for a camera that does not move.
	float stillSeconds = 0.3f; ///< The time that the camera must not move for the end.
	OfflineWorld offline;
	std::string gameSkip; ///< Not empty: the run in the game skips the scenario, for this reason.

	/// The time from the start of the steps to the end of the last step, seconds.
	[[nodiscard]] float StepsDuration() const;
};

/// Read a scenario from its JSON. Return an error that names the wrong field.
[[nodiscard]] moa::Result<Scenario> ParseScenario(const nlohmann::json& json);
[[nodiscard]] moa::Result<Scenario> ParseScenarioText(std::string_view text);

/// The zoom stage of a name of the game (Detail, Production, Combat, Planning, Map), or of a number.
[[nodiscard]] std::optional<float> StageOfName(const nlohmann::json& value);

} // namespace moa_mod::autotest
