/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <algorithm>
#include <format>
#include <numbers>
#include <utility>

#include <core/CameraPitch.h>
#include <core/CameraSettings.h>
#include <core/HandRig.h>
#include <support/ScenarioRunner.h>

namespace moa_mod::autotest
{

namespace
{
/// The longest wait for the camera mod in the setup, seconds.
constexpr double k_SetupTimeout = 15.0;
/// The longest extra wait for a camera that does not move at the end of the setup, seconds.
constexpr double k_SettleTimeout = 4.0;
/// A camera that moves slower than this does not move, metres for each second.
constexpr float k_StillSpeed = 0.02f;
/// A camera that turns slower than this does not turn, degrees for each second.
constexpr float k_StillTurn = 0.1f;
/// A pivot that moves more than this in a hold is a new pivot, metres.
constexpr float k_PivotRetake = 0.05f;
/// The smallest move in one frame that is a jump, metres. A jump is also more than the height above the land.
constexpr float k_JumpMetres = 50.0f;
/// An object in the hand with an opacity below this value does not show solid.
constexpr float k_SolidOpacity = 0.99f;
/// The radius of the test for a camera in an object, metres.
constexpr float k_ObstacleProbe = 0.05f;

[[nodiscard]] bool Finite(WorldPoint point)
{
	return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

[[nodiscard]] bool Finite(Quaternion rotation)
{
	return std::isfinite(rotation.x) && std::isfinite(rotation.y) && std::isfinite(rotation.z) && std::isfinite(rotation.w);
}

/// The angle between two rotations, degrees. The formula with the sine stays exact for small angles.
[[nodiscard]] float AngleBetween(Quaternion a, Quaternion b)
{
	const auto turn = Multiply(a, Inverse(b));
	const auto sine = std::sqrt(turn.x * turn.x + turn.y * turn.y + turn.z * turn.z);
	return 2.0f * std::atan2(sine, std::abs(turn.w)) / k_DegreesToRadians;
}

[[nodiscard]] float Distance2(WorldPoint a, WorldPoint b)
{
	return std::hypot(a.x - b.x, a.y - b.y);
}

[[nodiscard]] std::string Format(WorldPoint point)
{
	return std::format("({:.2f}, {:.2f}, {:.2f})", point.x, point.y, point.z);
}

[[nodiscard]] std::optional<camera::CameraEventKind> KindOfName(std::string_view name)
{
	for (const auto kind :
	     {camera::CameraEventKind::Gesture, camera::CameraEventKind::Pivot, camera::CameraEventKind::Desired,
	      camera::CameraEventKind::Arm, camera::CameraEventKind::CursorLock, camera::CameraEventKind::StandBack,
	      camera::CameraEventKind::Input, camera::CameraEventKind::Invariant, camera::CameraEventKind::BlackBox})
	{
		if (camera::Name(kind) == name)
		{
			return kind;
		}
	}
	return std::nullopt;
}

[[nodiscard]] nlohmann::json PoseJson(const camera::CameraPlacement& camera)
{
	return {{"position", {camera.position.x, camera.position.y, camera.position.z}},
	        {"rotation", {camera.rotation.x, camera.rotation.y, camera.rotation.z, camera.rotation.w}}};
}
} // namespace

std::string_view Name(Outcome outcome)
{
	switch (outcome)
	{
	case Outcome::Running:
		return "running";
	case Outcome::Passed:
		return "passed";
	case Outcome::Failed:
		return "failed";
	case Outcome::Inconclusive:
		return "inconclusive";
	}
	return "";
}

ScenarioRunner::ScenarioRunner(Scenario scenario)
    : _scenario(std::move(scenario))
{
	auto timeline = InputTimeline::Make(_scenario.steps);
	if (timeline)
	{
		_timeline = std::move(*timeline);
	}
	else
	{
		_timelineError = timeline.GetError().message;
	}
	double time = 0.0;
	for (size_t index = 0; index < _scenario.steps.size(); ++index)
	{
		const auto& step = _scenario.steps[index];
		const auto duration = static_cast<double>(step.Duration());
		if (step.kind == Step::Kind::Blend)
		{
			_blends.push_back({.start = time, .end = time + duration, .step = index, .started = false});
		}
		time += duration;
	}
}

bool ScenarioRunner::StartBlends(AutotestHost& host, camera::CameraGame& game, double now)
{
	for (auto& blend : _blends)
	{
		if (blend.started || now < blend.start)
		{
			continue;
		}
		blend.started = true;
		const auto& step = _scenario.steps[blend.step];
		// The new view: the focus moves on the heading, the heading turns, and the stage changes if the step has one.
		const auto direction = game.Direction();
		const auto radians = direction * k_DegreesToRadians;
		const auto focus = game.Focus() + WorldPoint {std::sin(radians), 0.0f, std::cos(radians)} * step.forward;
		const auto stage = step.stage.value_or(game.Stage());
		if (auto started = host.BlendCamera(focus, WrapDegrees(direction + step.turn), stage, step.seconds); !started)
		{
			Stop(host, std::format("the game did not blend its camera: {}", started.GetError().message));
			return false;
		}
		_notes.push_back(std::format("the game blends its camera to {} in {:.1f} s", Format(focus), step.seconds));
	}
	return true;
}

bool ScenarioRunner::StandBackPermitted(double seconds) const
{
	return std::ranges::any_of(
	    _blends, [seconds](const BlendStep& blend) { return blend.started && seconds >= blend.start && seconds < blend.end; });
}

std::string_view ScenarioRunner::PhaseName() const
{
	switch (_phase)
	{
	case Phase::Prepare:
		return "prepare";
	case Phase::Settle:
		return "settle";
	case Phase::Steps:
		return "steps";
	case Phase::End:
		return "end";
	case Phase::Done:
		return "done";
	}
	return "";
}

WorldPoint ScenarioRunner::StartCursor(Vector2 screen) const
{
	const auto& cursor = _scenario.setup.cursor;
	return {cursor.x * screen.x, (1.0f - cursor.y) * screen.y, 0.0f};
}

bool ScenarioRunner::Applies(const Check& check) const
{
	return !(check.offlineOnly && !_offline) && !(check.gameOnly && _offline) &&
	       !(check.rule == Check::Rule::NoInvariant && _offline);
}

const Check* ScenarioRunner::Find(Check::Rule rule) const
{
	for (const auto& check : _scenario.checks)
	{
		if (check.rule == rule && Applies(check))
		{
			return &check;
		}
	}
	return nullptr;
}

bool ScenarioRunner::Has(Check::Rule rule) const
{
	return Find(rule) != nullptr;
}

void ScenarioRunner::Stop(AutotestHost& host, std::string reason)
{
	if (_phase == Phase::Done)
	{
		return;
	}
	_notes.push_back(std::move(reason));
	_outcome = Outcome::Inconclusive;
	if (_settingsToRestore)
	{
		static_cast<void>(host.ApplyCameraSettings(*_settingsToRestore));
		_settingsToRestore.reset();
	}
	_phase = Phase::Done;
}

std::optional<WorldPoint> ScenarioRunner::PlaceFocus(AutotestHost& host, camera::CameraGame& game,
                                                     std::optional<float>& heading)
{
	const auto& place = _scenario.setup.place;
	const auto landAt = [&](WorldPoint point) -> std::optional<WorldPoint> {
		auto land = game.LandHeight({point.x, game.Focus().y, point.z});
		if (!land)
		{
			return std::nullopt;
		}
		return WorldPoint {point.x, *land, point.z};
	};
	switch (place.kind)
	{
	case Placement::Kind::Current:
		return std::nullopt;
	case Placement::Kind::Point:
		return landAt(place.point);
	case Placement::Kind::Object:
	case Placement::Kind::ObjectOfType:
	{
		auto object = host.FindObject(place);
		if (!object)
		{
			return std::nullopt;
		}
		_notes.push_back(std::format("the object '{}' is at {}", place.name, Format(*object)));
		return landAt(*object + place.offset);
	}
	case Placement::Kind::Steepest:
	{
		// Find the point near the focus where the land rises most in one step, and look up the slope from its foot.
		const auto centre = game.Focus();
		std::optional<WorldPoint> best;
		float bestRise = 0.0f;
		const auto count = static_cast<int>(place.radius / place.step);
		for (int i = -count; i <= count; ++i)
		{
			for (int j = -count; j <= count; ++j)
			{
				const WorldPoint point {centre.x + static_cast<float>(i) * place.step, centre.y,
				                        centre.z + static_cast<float>(j) * place.step};
				const auto base = game.LandHeight(point);
				if (!base)
				{
					return std::nullopt;
				}
				for (const auto& [dx, dz] :
				     {std::pair {1.0f, 0.0f}, std::pair {-1.0f, 0.0f}, std::pair {0.0f, 1.0f}, std::pair {0.0f, -1.0f}})
				{
					const auto next = game.LandHeight({point.x + dx * place.step, point.y, point.z + dz * place.step});
					if (next && *next - *base > bestRise)
					{
						bestRise = *next - *base;
						best = WorldPoint {point.x, *base, point.z};
						// The heading looks to the slope: forward is (sin d, 0, cos d).
						heading = std::atan2(dx, dz) / k_DegreesToRadians;
					}
				}
			}
		}
		if (best)
		{
			_notes.push_back(std::format("the steepest step of the land is {:.1f} m in {:.0f} m at {}", bestRise, place.step,
			                             Format(*best)));
		}
		return best;
	}
	case Placement::Kind::MapEdge:
	{
		const auto box = game.FocusLimits();
		if (!box)
		{
			return std::nullopt;
		}
		// The focus is near the edge of the smallest z, and the view looks to that edge.
		const auto focus = game.Focus();
		const WorldPoint point {std::clamp(focus.x, box->minimumX + place.inset, box->maximumX - place.inset), focus.y,
		                        box->minimumZ + place.inset};
		heading = 180.0f;
		return landAt(point);
	}
	}
	return std::nullopt;
}

void ScenarioRunner::Prepare(AutotestHost& host, camera::CameraGame& game)
{
	_offline = host.Offline();
	if (!_timelineError.empty())
	{
		Stop(host, _timelineError);
		return;
	}
	// The settings of the scenario: the settings of the user with the values of the scenario.
	_settingsToRestore = host.CameraSettings();
	auto json = camera::CameraSettingsObject(host.CameraSettings());
	json.merge_patch(_scenario.setup.settings);
	json["enabled"] = true;
	auto parsed = camera::ParseCameraSettings(json);
	if (!parsed)
	{
		Stop(host, std::format("the settings of the scenario are not correct: {}", parsed.GetError().message));
		return;
	}
	if (!host.ApplyCameraSettings(*parsed))
	{
		Stop(host, "the camera mod did not take the settings of the scenario");
		return;
	}
	std::optional<float> heading = _scenario.setup.heading;
	std::optional<float> placeHeading;
	auto focus = PlaceFocus(host, game, placeHeading);
	if (_scenario.setup.place.kind != Placement::Kind::Current && !focus)
	{
		Stop(host, "the setup did not find the place of the camera");
		return;
	}
	if (!heading)
	{
		heading = placeHeading;
	}
	if (focus)
	{
		game.SetFocus(*focus);
	}
	if (heading)
	{
		game.SetDirection(WrapDegrees(*heading));
	}
	// The camera of the game at its state: the camera loop starts again from the camera transform, so the setup puts
	// the transform there too. Each scenario then starts with no pitch of the user.
	const auto stage = _scenario.setup.stage ? std::clamp(*_scenario.setup.stage, 0.0f, 4.0f) : game.Stage();
	game.SetStage(stage);
	if (auto table = game.StageTable(); table && !table->empty())
	{
		const auto view = camera::InterpolateStage(*table, stage);
		game.SetStageView(view);
		const camera::CameraRig rig {.focus = game.Focus(),
		                             .direction = game.Direction(),
		                             .tilt = view.tilt,
		                             .distance = camera::StageDistance(view.height, view.tilt)};
		if (!game.SetCamera({.position = camera::RigPosition(rig), .rotation = camera::RigRotation(rig)}))
		{
			Stop(host, "the setup could not place the camera");
			return;
		}
	}
	host.Loop().Resync();
}

void ScenarioRunner::BeforeFrame(AutotestHost& host, camera::CameraGame* game, double seconds)
{
	_frameSeconds = Now(seconds);
	switch (_phase)
	{
	case Phase::Prepare:
		if (!_waitStart)
		{
			_waitStart = seconds;
		}
		if (game == nullptr)
		{
			if (seconds - *_waitStart > k_SetupTimeout)
			{
				Stop(host, "the game has no camera state");
			}
			return;
		}
		_phaseStart = seconds;
		Prepare(host, *game);
		if (_phase == Phase::Prepare)
		{
			_phase = Phase::Settle;
		}
		return;
	case Phase::Settle:
	{
		const auto elapsed = seconds - _phaseStart;
		if (!_scenario.setup.holdObject.empty() && !_objectPut && elapsed >= _scenario.setup.settleSeconds * 0.5)
		{
			_objectPut = true;
			if (auto put = host.PutObjectAtHand(_scenario.setup.holdObject); !put)
			{
				Stop(host, std::format("the setup could not put '{}' at the hand: {}", _scenario.setup.holdObject,
				                       put.GetError().message));
				return;
			}
		}
		if (!_scenario.setup.pickupClass.empty() && !_pickedUp && elapsed >= _scenario.setup.settleSeconds * 0.5)
		{
			_pickedUp = true;
			auto taken = host.PickUp(_scenario.setup.pickupClass, _scenario.setup.pickupName);
			if (!taken)
			{
				Stop(host, std::format("the setup could not put a '{}' named '{}' in the hand: {}", _scenario.setup.pickupClass,
				                       _scenario.setup.pickupName, taken.GetError().message));
				return;
			}
			_notes.push_back(std::format("the setup put '{}' in the hand", *taken));
		}
		const auto settled = elapsed >= _scenario.setup.settleSeconds && _stillSince &&
		                     seconds - *_stillSince >= _scenario.stillSeconds && _last && _last->active;
		if (settled || elapsed >= _scenario.setup.settleSeconds + k_SettleTimeout)
		{
			if (!settled)
			{
				if (!_last || !_last->active)
				{
					if (elapsed < k_SetupTimeout)
					{
						return;
					}
					Stop(host, "the camera mod did not become active after the setup");
					return;
				}
				_notes.push_back("the camera still moved at the end of the setup");
			}
			_phase = Phase::Steps;
			_stepsStart = seconds;
			_frameSeconds = 0.0;
			_lastInput = k_BeforeSteps;
			// The camera before the first input of the steps.
			_start = _last;
			_stillSince.reset();
		}
		return;
	}
	case Phase::Steps:
		if (game != nullptr && !StartBlends(host, *game, Now(seconds)))
		{
			return;
		}
		if (Now(seconds) >= _timeline.Duration())
		{
			_phase = Phase::End;
			_phaseStart = seconds;
			_stillSince.reset();
		}
		return;
	case Phase::End:
		if (_stillSince && seconds - *_stillSince >= _scenario.stillSeconds)
		{
			_stoppedAt = *_stillSince - _phaseStart;
			Finish(host);
		}
		else if (seconds - _phaseStart >= _scenario.endSeconds)
		{
			Finish(host);
		}
		return;
	case Phase::Done:
		return;
	}
}

InputWindow ScenarioRunner::TakeInput(double seconds)
{
	InputWindow window;
	if (!_cursorSent)
	{
		_cursorSent = true;
		window.events.push_back(InputEvent::MoveTo(0.0, _scenario.setup.cursor));
	}
	if (_phase != Phase::Steps && _phase != Phase::End)
	{
		return window;
	}
	const auto now = Now(seconds);
	if (now < _lastInput)
	{
		return window;
	}
	auto steps = _timeline.Window(_lastInput, now);
	window.events.insert(window.events.end(), steps.events.begin(), steps.events.end());
	window.right = steps.right;
	window.up = steps.up;
	_lastInput = now;
	return window;
}

void ScenarioRunner::OnEvent(camera::CameraEventKind kind, std::string_view text)
{
	if (_phase == Phase::Steps || _phase == Phase::End)
	{
		_events.push_back({.kind = kind, .text = std::string(text), .seconds = _frameSeconds});
	}
}

void ScenarioRunner::Fail(const std::string& check, const Sample& sample, std::string text)
{
	const auto found = std::ranges::find(_failures, check, &Failure::check);
	if (found != _failures.end())
	{
		++found->count;
		return;
	}
	_failures.push_back({.check = check, .frame = sample.frame, .seconds = sample.seconds, .text = std::move(text)});
}

void ScenarioRunner::AfterFrame(AutotestHost& host, camera::CameraGame* gameOrNull, const VirtualInput& input,
                                const std::optional<std::string>& standBack, double seconds)
{
	if (_phase == Phase::Done || _phase == Phase::Prepare)
	{
		return;
	}
	if (gameOrNull == nullptr)
	{
		_stillSince.reset();
		if ((_phase == Phase::Steps || _phase == Phase::End) && _last)
		{
			Fail("active", *_last, std::format("the camera mod stood back: {}", standBack.value_or("no camera state")));
		}
		return;
	}
	auto& game = *gameOrNull;
	const auto& loop = host.Loop();
	const auto& trace = loop.Trace();
	Sample sample;
	sample.frame = _frame++;
	sample.seconds = _phase == Phase::Settle ? -(seconds - _phaseStart) : Now(seconds);
	sample.phase = _phase;
	sample.active = !standBack;
	_standBackReason = standBack.value_or("");
	if (auto placement = game.Camera())
	{
		sample.camera = *placement;
	}
	sample.heading = game.Direction();
	sample.turned =
	    _samples.empty() ? sample.heading : _samples.back().turned + WrapDegrees(sample.heading - _samples.back().heading);
	sample.pitch = camera::PitchOf(sample.camera.rotation);
	sample.raise = trace.raise;
	sample.stage = game.Stage();
	sample.desired = trace.desiredDistance;
	sample.desiredChange = trace.desiredChange;
	// The exact land under the camera, the land of the floor of the camera.
	sample.land = game.LandHeight(sample.camera.position);
	sample.pivot = trace.pivot;
	sample.pivotKept = trace.pivotKept;
	if (sample.pivot)
	{
		sample.pivotScreen = game.ScreenPointOf(*sample.pivot);
	}
	sample.cursorLock = trace.cursorLock.has_value();
	sample.cursorLockOff = trace.cursorLockOff;
	sample.middle = input.Button(static_cast<int32_t>(Button::Middle));
	sample.middlePressed = input.ButtonDown(static_cast<int32_t>(Button::Middle));
	sample.mouse = input.Mouse();
	sample.flying = loop.Flying();
	sample.ground = loop.InGround();
	sample.glideTurn = trace.glideTurn;
	sample.glidePitch = trace.glidePitch;
	if (Has(Check::Rule::HoldsObject))
	{
		sample.holdsObject = host.HoldsObject().value_or(false);
	}
	if (Has(Check::Rule::HeldVisible))
	{
		sample.held = host.HeldLook();
	}
	if (auto fade = host.Fade())
	{
		sample.fade = *fade;
	}
	if (Has(Check::Rule::NotInObstacle))
	{
		auto hit =
		    game.CastSphere(sample.camera.position, {0.0f, 1.0f, 0.0f}, k_ObstacleProbe, 0.01f, camera::k_CameraObstacleLayers);
		sample.inObstacle = hit && hit->hit && hit->distance <= 0.0f;
	}

	// The camera does not move: for the end of the setup and of the steps.
	if (!_samples.empty())
	{
		const auto& previous = _samples.back();
		const auto step = static_cast<float>(std::max(std::abs(sample.seconds - previous.seconds), 1e-4));
		if (previous.phase == sample.phase || (previous.phase == Phase::Steps && sample.phase == Phase::End))
		{
			sample.velocity = (sample.camera.position - previous.camera.position) * (1.0f / step);
		}
		const auto speed = Length(sample.camera.position - previous.camera.position) / step;
		const auto turn = AngleBetween(sample.camera.rotation, previous.camera.rotation) / step;
		const auto still = speed < k_StillSpeed && turn < k_StillTurn && sample.glideTurn == 0.0f &&
		                   sample.glidePitch == 0.0f && !sample.flying && sample.active;
		if (!still)
		{
			_stillSince.reset();
		}
		else if (!_stillSince)
		{
			_stillSince = seconds;
		}
	}
	if (_phase == Phase::Steps || _phase == Phase::End)
	{
		if (!_start)
		{
			_start = sample;
		}
		CheckFrame(sample);
	}
	_samples.push_back(sample);
	_last = sample;
}

void ScenarioRunner::CheckFrame(const Sample& sample)
{
	if (!sample.active && !StandBackPermitted(sample.seconds))
	{
		Fail("active", sample, std::format("the camera mod stood back during the steps: {}", _standBackReason));
	}
	if (!Finite(sample.camera.position) || !Finite(sample.camera.rotation))
	{
		Fail("noJump", sample, "a value of the camera is not a number");
		return;
	}
	_sawFlight = _sawFlight || sample.flying;
	_sawGround = _sawGround || sample.ground;
	_sawHold = _sawHold || sample.holdsObject;
	const auto previous = _samples.empty() || _samples.back().phase == Phase::Settle ? std::optional<Sample>()
	                                                                                 : std::optional<Sample>(_samples.back());

	if (const auto* check = Find(Check::Rule::NoJump); check != nullptr && previous)
	{
		const auto move = Length(sample.camera.position - previous->camera.position);
		const auto height = sample.land ? sample.camera.position.y - *sample.land : 0.0f;
		if (!sample.flying && move > std::max(k_JumpMetres, height))
		{
			Fail("noJump", sample,
			     std::format("the camera moved {:.1f} m in one frame, {:.1f} m above the land", move, height));
		}
	}
	if (const auto* check = Find(Check::Rule::MaxSpeed); check != nullptr && sample.velocity && !sample.flying)
	{
		if (const auto speed = Length(*sample.velocity); speed > check->maximum.value_or(0.0f))
		{
			Fail("maxSpeed", sample, std::format("the camera moved at {:.1f} m/s", speed));
		}
	}
	if (const auto* check = Find(Check::Rule::MaxAcceleration);
	    check != nullptr && sample.velocity && previous && previous->velocity && !sample.flying && !previous->flying)
	{
		const auto step = static_cast<float>(std::max(std::abs(sample.seconds - previous->seconds), 1e-4));
		if (const auto change = Length(*sample.velocity - *previous->velocity) / step; change > check->maximum.value_or(0.0f))
		{
			Fail("maxAcceleration", sample, std::format("the speed of the camera changed at {:.1f} m/s2", change));
		}
	}
	_mostFaded = std::max(_mostFaded, sample.fade.faded + sample.fade.hidden);
	if (sample.held && sample.held->held)
	{
		_sawHeldLook = true;
		const auto& held = *sample.held;
		if (held.changedByFade)
		{
			Fail("heldVisible", sample, std::format("the fade changes '{}' in the hand", held.name));
		}
		else if (held.renderers > 0 && held.shadowOnly == held.renderers)
		{
			Fail("heldVisible", sample, std::format("'{}' in the hand draws only its shadow", held.name));
		}
		else if (held.lowestOpacity && *held.lowestOpacity < k_SolidOpacity)
		{
			Fail("heldVisible", sample, std::format("'{}' in the hand has the opacity {:.2f}", held.name, *held.lowestOpacity));
		}
	}
	if (const auto* check = Find(Check::Rule::AboveLand);
	    check != nullptr && sample.land && sample.camera.position.y < *sample.land - check->metres)
	{
		Fail("aboveLand", sample,
		     std::format("the camera is {:.2f} m below the land height {:.2f} at {}", *sample.land - sample.camera.position.y,
		                 *sample.land, Format(sample.camera.position)));
	}
	if (Has(Check::Rule::NotInObstacle) && sample.inObstacle)
	{
		Fail("notInObstacle", sample, std::format("the camera is in an object at {}", Format(sample.camera.position)));
	}

	// The rules of a hold of the middle button. The frame of the press takes the pivot and the desired distance.
	if (!sample.middle)
	{
		_heldFrames = 0;
		_holdPivot.reset();
		_holdScreen.reset();
		_holdDesired.reset();
		return;
	}
	++_heldFrames;
	const auto afterPress = _heldFrames > 1;
	if (const auto* check = Find(Check::Rule::CursorLock); check != nullptr && afterPress && !sample.cursorLock)
	{
		Fail("cursorLock", sample,
		     std::format("the middle button is held and the cursor lock is off (cause {})",
		                 sample.cursorLockOff ? std::to_string(*sample.cursorLockOff) : std::string("none")));
	}
	if (const auto* check = Find(Check::Rule::DesiredConstant); check != nullptr && sample.desired)
	{
		// A zoom (1) and the clamp into the map (6) give a new desired distance.
		if (!_holdDesired || sample.desiredChange == 1 || sample.desiredChange == 6 || !afterPress)
		{
			_holdDesired = sample.desired;
		}
		else if (sample.desiredChange == 2 || sample.desiredChange == 4 || sample.desiredChange == 5)
		{
			Fail("desiredConstant", sample,
			     std::format("the desired distance changed from {:.2f} to {:.2f} m (change {}) while the middle button is held",
			                 *_holdDesired, *sample.desired, sample.desiredChange));
		}
		else if (std::abs(*sample.desired - *_holdDesired) > *_holdDesired * check->percent / 100.0f)
		{
			Fail(
			    "desiredConstant", sample,
			    std::format("the desired distance changed from {:.3f} to {:.3f} m with no zoom while the middle button is held",
			                *_holdDesired, *sample.desired));
		}
	}
	if (const auto* check = Find(Check::Rule::PivotFixed); check != nullptr && sample.pivot && sample.pivotScreen)
	{
		if (!_holdPivot || !afterPress)
		{
			_holdPivot = sample.pivot;
			_holdScreen = sample.pivotScreen;
		}
		else if (Length(*sample.pivot - *_holdPivot) > k_PivotRetake)
		{
			Fail("pivotFixed", sample,
			     std::format("the pivot moved from {} to {} while the middle button is held", Format(*_holdPivot),
			                 Format(*sample.pivot)));
			_holdPivot = sample.pivot;
			_holdScreen = sample.pivotScreen;
		}
		else if (!sample.pivotKept)
		{
			// The land or the edge of the map moved the pivot on the screen in this frame: start again from here.
			_holdScreen = sample.pivotScreen;
		}
		else if (const auto drift = Distance2(*sample.pivotScreen, *_holdScreen); drift > check->pixels)
		{
			Fail("pivotFixed", sample,
			     std::format("the pivot moved {:.1f} px on the screen while the middle button is held", drift));
		}
	}
}

std::optional<float> ScenarioRunner::ChangeOf(const std::string& of) const
{
	if (!_start || !_last)
	{
		return std::nullopt;
	}
	const auto& a = *_start;
	const auto& b = *_last;
	if (of == "heading")
	{
		// The turn added up over the frames, so that a turn of more than half a circle counts in full.
		return std::abs(b.turned - a.turned);
	}
	if (of == "raise")
	{
		return b.raise - a.raise;
	}
	if (of == "pitch")
	{
		return b.pitch - a.pitch;
	}
	if (of == "stage")
	{
		return b.stage - a.stage;
	}
	if (of == "camera")
	{
		return Length(b.camera.position - a.camera.position);
	}
	if (of == "desired")
	{
		if (!a.desired || !b.desired || *a.desired <= 0.0f)
		{
			return std::nullopt;
		}
		return *b.desired / *a.desired;
	}
	if (of == "height")
	{
		if (!a.land || !b.land)
		{
			return std::nullopt;
		}
		return (b.camera.position.y - *b.land) - (a.camera.position.y - *a.land);
	}
	return std::nullopt;
}

void ScenarioRunner::FinalChecks()
{
	if (!_last || !_start)
	{
		_notes.push_back("the steps ran no frame");
		_outcome = Outcome::Inconclusive;
		return;
	}
	const auto& last = *_last;
	for (const auto& check : _scenario.checks)
	{
		if (!Applies(check))
		{
			continue;
		}
		switch (check.rule)
		{
		case Check::Rule::GlideStops:
			if (!_stoppedAt || *_stoppedAt > check.seconds)
			{
				Fail("glideStops", last,
				     _stoppedAt ? std::format("the camera stopped {:.2f} s after the steps", *_stoppedAt)
				                : std::format("the camera still moved {:.1f} s after the steps", _scenario.endSeconds));
			}
			break;
		case Check::Rule::ReturnsToStart:
		{
			const auto move = Length(last.camera.position - _start->camera.position);
			const auto turn = AngleBetween(last.camera.rotation, _start->camera.rotation);
			if (move > check.metres || turn > check.degrees)
			{
				Fail("returnsToStart", last,
				     std::format("the camera is {:.3f} m and {:.3f} deg from its place before the steps", move, turn));
			}
			break;
		}
		case Check::Rule::Event:
		{
			const auto kind = KindOfName(check.kind);
			const auto count = static_cast<float>(std::ranges::count_if(_events, [&](const EventRecord& event) {
				return kind && event.kind == *kind && event.text.find(check.contains) != std::string::npos;
			}));
			if ((check.minimum && count < *check.minimum) || (check.maximum && count > *check.maximum))
			{
				Fail(check.Describe(), last,
				     std::format("{} events of the kind '{}' with '{}' (expected {} to {})", count, check.kind, check.contains,
				                 check.minimum.value_or(0.0f), check.maximum ? std::format("{}", *check.maximum) : "any"));
			}
			break;
		}
		case Check::Rule::Change:
		{
			const auto value = ChangeOf(check.of);
			if (!value)
			{
				Fail(check.Describe(), last, std::format("no value for the change of {}", check.of));
			}
			else if ((check.minimum && *value < *check.minimum) || (check.maximum && *value > *check.maximum))
			{
				Fail(check.Describe(), last,
				     std::format("the change of {} is {:.3f} (expected {} to {})", check.of, *value,
				                 check.minimum ? std::format("{}", *check.minimum) : "any",
				                 check.maximum ? std::format("{}", *check.maximum) : "any"));
			}
			break;
		}
		case Check::Rule::Flew:
			if (!_sawFlight)
			{
				Fail("flew", last, "no flight of the hand rig ran");
			}
			break;
		case Check::Rule::Ground:
			if (!_sawGround)
			{
				Fail("ground", last, "the zoom did not go into the ground range");
			}
			break;
		case Check::Rule::HoldsObject:
			if (!_sawHold)
			{
				_notes.push_back("the hand did not hold an object, so the scenario did not test a pickup");
				_outcome = Outcome::Inconclusive;
			}
			break;
		case Check::Rule::Faded:
		{
			// With only a largest count, the check permits no change.
			const auto least = check.minimum.value_or(check.maximum ? 0.0f : 1.0f);
			if (static_cast<float>(_mostFaded) < least)
			{
				Fail("faded", last,
				     std::format("the fade changed at most {} objects in a frame (expected {})", _mostFaded, least));
			}
			if (check.maximum && static_cast<float>(_mostFaded) > *check.maximum)
			{
				Fail("faded", last,
				     std::format("the fade changed {} objects in a frame (at most {} permitted)", _mostFaded, *check.maximum));
			}
			break;
		}
		case Check::Rule::HeldVisible:
			if (!_sawHeldLook)
			{
				_notes.push_back("the hand did not hold an object, so the scenario did not test the object in the hand");
				_outcome = Outcome::Inconclusive;
			}
			break;
		case Check::Rule::FadeRestored:
			if (last.fade.faded + last.fade.hidden > 0)
			{
				Fail("fadeRestored", last,
				     std::format("at the end the fade still changes {} objects ({} hidden)", last.fade.faded + last.fade.hidden,
				                 last.fade.hidden));
			}
			break;
		case Check::Rule::NoInvariant:
			for (const auto& event : _events)
			{
				if (event.kind == camera::CameraEventKind::Invariant)
				{
					Fail("noInvariant", last, event.text);
				}
			}
			break;
		default:
			break;
		}
	}
}

void ScenarioRunner::Finish(AutotestHost& host)
{
	FinalChecks();
	if (_outcome == Outcome::Running)
	{
		_outcome = _failures.empty() ? Outcome::Passed : Outcome::Failed;
	}
	else if (_outcome == Outcome::Inconclusive && !_failures.empty())
	{
		_outcome = Outcome::Failed;
	}
	if (_settingsToRestore)
	{
		static_cast<void>(host.ApplyCameraSettings(*_settingsToRestore));
		_settingsToRestore.reset();
	}
	_phase = Phase::Done;
}

nlohmann::json ScenarioRunner::ResultJson() const
{
	auto failures = nlohmann::json::array();
	for (const auto& failure : _failures)
	{
		failures.push_back({{"check", failure.check},
		                    {"frame", failure.frame},
		                    {"seconds", failure.seconds},
		                    {"frames", failure.count},
		                    {"text", failure.text}});
	}
	auto checks = nlohmann::json::array();
	for (const auto& check : _scenario.checks)
	{
		const auto name = check.Describe();
		std::string status = "passed";
		if (!Applies(check))
		{
			status = "skipped";
		}
		else if (std::ranges::any_of(_failures, [&](const Failure& f) { return f.check == name; }))
		{
			status = "failed";
		}
		checks.push_back({{"check", name}, {"status", status}});
	}
	nlohmann::json changes = nlohmann::json::object();
	for (const auto* of : {"heading", "raise", "pitch", "stage", "camera", "desired", "height"})
	{
		if (auto value = ChangeOf(of))
		{
			changes[of] = *value;
		}
	}
	nlohmann::json result {
	    {"name", _scenario.name},
	    {"description", _scenario.description},
	    {"outcome", std::string(Name(_outcome))},
	    {"phase", std::string(PhaseName())},
	    {"offline", _offline},
	    {"frames", _samples.size()},
	    {"stepsSeconds", _timeline.Duration()},
	    {"failures", failures},
	    {"checks", checks},
	    {"changes", changes},
	    {"notes", _notes},
	};
	if (_stoppedAt)
	{
		result["stoppedAfterSteps"] = *_stoppedAt;
	}
	if (_start)
	{
		result["start"] = PoseJson(_start->camera);
	}
	if (_last)
	{
		result["end"] = PoseJson(_last->camera);
	}
	auto events = nlohmann::json::array();
	for (const auto& event : _events)
	{
		events.push_back(std::format("{:.3f} {}: {}", event.seconds, camera::Name(event.kind), event.text));
	}
	result["events"] = events;
	return result;
}

std::string ScenarioRunner::FramesCsv() const
{
	std::string text = "frame,seconds,phase,active,camX,camY,camZ,heading,pitch,raise,stage,desired,desiredChange,land,"
	                   "pivotX,pivotY,pivotZ,pivotKept,pivotScreenX,pivotScreenY,cursorLock,middle,mouseX,mouseY,"
	                   "flying,ground,glideTurn,glidePitch,holdsObject,fadedObjects,hiddenObjects\n";
	const auto optional = [](const std::optional<float>& value) {
		return value ? std::format("{:.4f}", *value) : std::string();
	};
	const auto phaseName = [](Phase phase) {
		switch (phase)
		{
		case Phase::Settle:
			return "settle";
		case Phase::Steps:
			return "steps";
		case Phase::End:
			return "end";
		default:
			return "other";
		}
	};
	for (const auto& s : _samples)
	{
		text +=
		    std::format("{},{:.4f},{},{:d},{:.4f},{:.4f},{:.4f},{:.3f},{:.3f},{:.3f},{:.4f},{},{},{},{},{},{},{:d},{},{},{:"
		                "d},{:d},{:.1f},{:.1f},{:d},{:d},"
		                "{:.4f},{:.4f},{:d},{},{}\n",
		                s.frame, s.seconds, phaseName(s.phase), s.active, s.camera.position.x, s.camera.position.y,
		                s.camera.position.z, s.heading, s.pitch, s.raise, s.stage, optional(s.desired), s.desiredChange,
		                optional(s.land), s.pivot ? std::format("{:.4f}", s.pivot->x) : "",
		                s.pivot ? std::format("{:.4f}", s.pivot->y) : "", s.pivot ? std::format("{:.4f}", s.pivot->z) : "",
		                s.pivotKept, s.pivotScreen ? std::format("{:.2f}", s.pivotScreen->x) : "",
		                s.pivotScreen ? std::format("{:.2f}", s.pivotScreen->y) : "", s.cursorLock, s.middle, s.mouse.x,
		                s.mouse.y, s.flying, s.ground, s.glideTurn, s.glidePitch, s.holdsObject, s.fade.faded, s.fade.hidden);
	}
	return text;
}

} // namespace moa_mod::autotest
