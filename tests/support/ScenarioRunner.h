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
#include <core/CameraEvents.h>
#include <core/CameraFade.h>
#include <core/CameraLoop.h>
#include <core/CameraSettings.h>
#include <nlohmann/json.hpp>
#include <support/Scenario.h>
#include <support/VirtualInput.h>

namespace moa_mod::autotest
{

/// The objects that the fade of the camera mod changes in a frame.
struct FadeCounts
{
	int faded = 0;  ///< The objects that are see-through.
	int hidden = 0; ///< The objects that are hidden.
};

/// What the runner of a scenario needs from the place where it runs: the game with the camera mod, or the model of
/// the game in the tests.
class AutotestHost
{
public:
	virtual ~AutotestHost() = default;

	/// The camera loop of the camera mod.
	[[nodiscard]] virtual camera::CameraLoop& Loop() = 0;
	/// The camera settings that the camera mod uses now.
	[[nodiscard]] virtual const moa_mod::camera::CameraSettings& CameraSettings() const = 0;
	/// Use other camera settings. The camera mod starts again from the camera of the game. Return false on an error.
	[[nodiscard]] virtual bool ApplyCameraSettings(const moa_mod::camera::CameraSettings& settings) = 0;
	/// The position of a game object of a placement (`Object` or `ObjectOfType`). Nothing if there is no such object.
	[[nodiscard]] virtual std::optional<WorldPoint> FindObject(const Placement& place) = 0;
	/// Put a carried resource at the hand. Return an error if it is not possible.
	[[nodiscard]] virtual moa::Result<void> PutObjectAtHand(const std::string& name) = 0;
	/// True while the hand holds an object. Nothing if the host cannot tell.
	[[nodiscard]] virtual std::optional<bool> HoldsObject() = 0;
	/// The game blends its camera to a view (focus, heading in degrees, zoom stage) in `seconds`. The camera mod stands
	/// back while the blend runs. Return an error if it is not possible.
	[[nodiscard]] virtual moa::Result<void> BlendCamera(WorldPoint focus, float direction, float stage, float seconds) = 0;
	/// True for the model of the game in the tests, false in the game.
	[[nodiscard]] virtual bool Offline() const = 0;
	/// The objects that the fade of the camera mod changes now. Nothing if the host has no fade.
	[[nodiscard]] virtual std::optional<FadeCounts> Fade() const { return std::nullopt; }
	/// Put the object of the world of a class whose name contains `name`, the nearest to the camera, in the hand.
	/// Return the name of the object, or an error if it is not possible.
	[[nodiscard]] virtual moa::Result<std::string> PickUp(const std::string& className, const std::string& name) = 0;
	/// How the object in the hand shows now. Nothing if the host cannot tell.
	[[nodiscard]] virtual std::optional<camera::HeldObjectLook> HeldLook() { return std::nullopt; }
};

/// The result of a scenario.
enum class Outcome : uint8_t
{
	Running,
	Passed,
	Failed,
	Inconclusive, ///< The scenario could not test the camera, for example the setup failed.
};

[[nodiscard]] std::string_view Name(Outcome outcome);

/// One broken rule, with the first frame where it broke.
struct Failure
{
	std::string check;
	int frame = 0;        ///< The frame from the start of the scenario.
	double seconds = 0.0; ///< The time from the start of the steps; less than zero before the steps.
	std::string text;
	int count = 1; ///< The count of the frames where the rule broke.
};

/// Runs one scenario frame by frame, around the frames of the camera mod: the setup, the input steps, the wait at the
/// end, and the checks of each frame and of the end.
///
/// For each frame:
/// 1. `BeforeFrame`, before the camera mod starts its frame.
/// 2. `TakeInput` gives the input of the frame to the virtual input, before the game reads the input.
/// 3. The camera mod and the game run the frame.
/// 4. `AfterFrame`, after the camera mod, with the camera that the user sees.
class ScenarioRunner
{
public:
	explicit ScenarioRunner(Scenario scenario);

	[[nodiscard]] const Scenario& Get() const { return _scenario; }

	/// Before the frame of the camera mod. `seconds` is a clock that only goes forward. `game` is null when the game has
	/// no camera state; then the setup waits.
	void BeforeFrame(AutotestHost& host, camera::CameraGame* game, double seconds);
	/// The input of the frame: the input of the steps since the last call.
	[[nodiscard]] InputWindow TakeInput(double seconds);
	/// After the frame of the camera mod. `standBack` is the reason when the camera mod did not change the frame.
	/// `game` is null when the game has no camera state.
	void AfterFrame(AutotestHost& host, camera::CameraGame* game, const VirtualInput& input,
	                const std::optional<std::string>& standBack, double seconds);
	/// An event of the camera mod.
	void OnEvent(camera::CameraEventKind kind, std::string_view text);
	/// Stop the scenario now: the result is inconclusive, for this reason.
	void Stop(AutotestHost& host, std::string reason);

	[[nodiscard]] bool Done() const { return _phase == Phase::Done; }
	[[nodiscard]] Outcome Result() const { return _outcome; }
	[[nodiscard]] const std::vector<Failure>& Failures() const { return _failures; }
	/// The result as JSON: the outcome, each check, the failures, the start and the end of the camera.
	[[nodiscard]] nlohmann::json ResultJson() const;
	/// One row for each frame, as CSV with a header line.
	[[nodiscard]] std::string FramesCsv() const;
	/// The name of the phase now.
	[[nodiscard]] std::string_view PhaseName() const;
	/// The point of the screen of the cursor at the start, in pixels from the bottom left.
	[[nodiscard]] WorldPoint StartCursor(Vector2 screen) const;

private:
	enum class Phase : uint8_t
	{
		Prepare,
		Settle,
		Steps,
		End,
		Done,
	};

	/// The values of one frame that the checks use.
	struct Sample
	{
		int frame = 0;
		double seconds = 0.0; ///< From the start of the steps.
		Phase phase = Phase::Prepare;
		bool active = false;
		camera::CameraPlacement camera;
		float heading = 0.0f;
		/// The heading with each turn added up from the first frame, degrees: a turn of more than half a circle does
		/// not wrap.
		float turned = 0.0f;
		float pitch = 0.0f; ///< The angle of the view below the horizon, degrees.
		float raise = 0.0f;
		float stage = 0.0f;
		std::optional<float> desired;
		int desiredChange = 0;
		std::optional<float> land;
		std::optional<WorldPoint> pivot;
		bool pivotKept = true;
		std::optional<WorldPoint> pivotScreen;
		bool cursorLock = false;
		std::optional<int> cursorLockOff;
		bool middle = false;
		bool middlePressed = false;
		WorldPoint mouse;
		bool flying = false;
		bool ground = false;
		float glideTurn = 0.0f;
		float glidePitch = 0.0f;
		bool holdsObject = false;
		std::optional<camera::HeldObjectLook> held; ///< How the object in the hand shows, for `HeldVisible`.
		bool inObstacle = false;
		FadeCounts fade;                    ///< The objects that the fade changes.
		std::optional<WorldPoint> velocity; ///< The velocity of the camera from the last frame, metres for each second.
	};

	struct EventRecord
	{
		camera::CameraEventKind kind;
		std::string text;
		double seconds = 0.0;
	};

	[[nodiscard]] bool Has(Check::Rule rule) const;
	[[nodiscard]] const Check* Find(Check::Rule rule) const;
	[[nodiscard]] bool Applies(const Check& check) const;
	void Prepare(AutotestHost& host, camera::CameraGame& game);
	[[nodiscard]] std::optional<WorldPoint> PlaceFocus(AutotestHost& host, camera::CameraGame& game,
	                                                   std::optional<float>& heading);
	void Fail(const std::string& check, const Sample& sample, std::string text);
	void CheckFrame(const Sample& sample);
	void Finish(AutotestHost& host);
	void FinalChecks();
	[[nodiscard]] std::optional<float> ChangeOf(const std::string& of) const;
	[[nodiscard]] double Now(double seconds) const { return seconds - _stepsStart; }
	/// Start each blend step whose time came. Return false if the scenario stopped.
	[[nodiscard]] bool StartBlends(AutotestHost& host, camera::CameraGame& game, double now);
	/// True if the camera mod can stand back at this time of the steps: in a blend step.
	[[nodiscard]] bool StandBackPermitted(double seconds) const;

	Scenario _scenario;
	InputTimeline _timeline;
	std::string _timelineError;
	Phase _phase = Phase::Prepare;
	Outcome _outcome = Outcome::Running;
	std::vector<Failure> _failures;
	std::vector<std::string> _notes;
	std::vector<Sample> _samples;
	std::vector<EventRecord> _events;
	std::optional<moa_mod::camera::CameraSettings> _settingsToRestore;
	bool _offline = false;
	int _frame = 0;
	double _phaseStart = 0.0;
	double _stepsStart = 0.0;
	double _lastInput = 0.0;
	double _frameSeconds = 0.0;       ///< The time of the current frame from the start of the steps.
	std::optional<double> _waitStart; ///< The first frame of the setup.
	std::string _standBackReason;     ///< Why the camera mod stood back in the current frame.
	bool _cursorSent = false;
	bool _objectPut = false;
	std::optional<double> _stillSince;
	std::optional<double> _stoppedAt; ///< The time from the end of the steps when the camera stopped.
	std::optional<Sample> _start;     ///< The first frame of the steps.
	std::optional<Sample> _last;
	// The state of the checks while the middle button is held.
	std::optional<WorldPoint> _holdPivot;
	std::optional<WorldPoint> _holdScreen;
	std::optional<float> _holdDesired;
	int _heldFrames = 0;
	bool _sawFlight = false;
	bool _sawGround = false;
	bool _sawHold = false;
	bool _pickedUp = false;
	bool _sawHeldLook = false; ///< `HeldVisible` saw a frame with an object in the hand.
	int _mostFaded = 0;        ///< The largest count of the objects that the fade changed in a frame of the steps.
	// The blend steps: the time from the start of the steps, the step, and if it started.
	struct BlendStep
	{
		double start = 0.0;
		double end = 0.0; ///< The end of the step: the camera mod is active again before it.
		size_t step = 0;  ///< The index of the step in the scenario.
		bool started = false;
	};
	std::vector<BlendStep> _blends;
};

} // namespace moa_mod::autotest
