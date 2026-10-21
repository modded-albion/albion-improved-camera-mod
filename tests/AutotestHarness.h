/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cmath>

#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <moa_mod/autotest/ScenarioRunner.h>
#include <moa_mod/autotest/VirtualInput.h>
#include <moa_mod/camera/CameraSettings.h>

#include "CameraLoopHarness.h"
#include "FadeHarness.h"

namespace moa_mod::autotest::test
{

/// The folder of the scenarios and of the settings of the camera tests.
[[nodiscard]] inline std::filesystem::path AutotestFolder()
{
	return std::filesystem::path(MOA_AUTOTEST_DIR);
}

/// The camera settings of the camera tests (`autotest/settings.json`), the same in the game and in the model.
[[nodiscard]] inline moa_mod::camera::CameraSettings AutotestSettings()
{
	std::ifstream file(AutotestFolder() / "settings.json");
	std::stringstream text;
	text << file.rdbuf();
	const auto json = nlohmann::json::parse(text.str(), nullptr, false);
	auto parsed = moa_mod::camera::ParseCameraSettings(json.is_object() ? json.value("camera", nlohmann::json::object())
	                                                                    : nlohmann::json::object());
	if (parsed)
	{
		return *parsed;
	}
	moa_mod::camera::CameraSettings camera;
	camera.enabled = true;
	return camera;
}

/// The model of the game for a world of a scenario: the land, the objects and the edge of the map.
inline void MakeWorld(camera::test::FakeCameraGame& game, const std::string& world)
{
	game.focusHeightEase = true;
	game.turnDegreesPerPixel = 0.11f;
	if (world == "hills")
	{
		game.ground = [](float x, float z) { return 100.0f + 12.0f * std::sin(x / 37.0f) * std::cos(z / 53.0f); };
	}
	else if (world == "cliff")
	{
		// A step of 25 metres over 6 metres, at z = 30.
		game.ground = [](float, float z) {
			const auto t = std::clamp((z - 30.0f) / 6.0f, 0.0f, 1.0f);
			return 100.0f + 25.0f * t * t * (3.0f - 2.0f * t);
		};
	}
	else if (world == "buildings")
	{
		game.boxes.push_back({.minimum = {-4.0f, 99.0f, -4.0f}, .maximum = {4.0f, 112.0f, 4.0f}});
		game.boxes.push_back({.minimum = {-4.0f, 99.0f, 12.0f}, .maximum = {3.0f, 108.0f, 19.0f}});
		game.obstacles.push_back({.centre = {14.0f, 104.0f, -12.0f}, .radius = 2.0f});
	}
	else if (world == "ridge")
	{
		// A ring of hills 400 metres from the focus, with sides that rise 2 metres for each metre. At Map, with the
		// focus as the pivot, its top is 15 metres over the line from the focus to the camera at each heading.
		game.ground = [](float x, float z) {
			const auto top = 400.0f * std::tan(50.0f * k_DegreesToRadians) + 15.0f;
			return 100.0f + std::max(0.0f, top - 2.0f * std::abs(std::hypot(x, z) - 400.0f));
		};
	}
	else if (world == "mapEdge")
	{
		game.mapMinimum = -250.0f;
		game.mapMaximum = 250.0f;
	}
}

/// The host of the runner in the model of the game.
class OfflineHost final: public AutotestHost
{
public:
	OfflineHost(camera::test::CameraLoopHarness& harness, moa_mod::camera::CameraSettings settings)
	    : _harness(harness)
	    , _settings(settings)
	{
	}

	[[nodiscard]] camera::CameraLoop& Loop() override { return _harness.loop; }
	[[nodiscard]] const moa_mod::camera::CameraSettings& CameraSettings() const override { return _settings; }
	[[nodiscard]] bool ApplyCameraSettings(const moa_mod::camera::CameraSettings& settings) override
	{
		_settings = settings;
		_harness.loop = camera::CameraLoop(settings);
		return true;
	}
	[[nodiscard]] std::optional<WorldPoint> FindObject(const Placement& place) override
	{
		if (place.name == "MABuilding" && !_harness.game.boxes.empty())
		{
			const auto& box = _harness.game.boxes.front();
			return (box.minimum + box.maximum) * 0.5f;
		}
		return std::nullopt;
	}
	[[nodiscard]] moa::Result<void> PutObjectAtHand(const std::string& /*name*/) override
	{
		_objectAtHand = true;
		return {};
	}
	[[nodiscard]] std::optional<bool> HoldsObject() override { return _harness.game.holdsObject; }
	[[nodiscard]] moa::Result<void> BlendCamera(WorldPoint focus, float direction, float stage, float seconds) override
	{
		_harness.game.StartBlend(focus, direction, stage, seconds);
		return {};
	}
	[[nodiscard]] bool Offline() const override { return true; }
	[[nodiscard]] std::optional<FadeCounts> Fade() const override
	{
		return FadeCounts {.faded = _fade.Faded(), .hidden = _fade.Hidden()};
	}
	/// The model has no objects to take: the hand takes a box of 1 m that stays at the camera, the worst place for the
	/// fade.
	[[nodiscard]] moa::Result<std::string> PickUp(const std::string& /*className*/, const std::string& name) override
	{
		_pickedUp = true;
		_harness.game.holdsObject = true;
		return name;
	}
	[[nodiscard]] std::optional<camera::HeldObjectLook> HeldLook() override
	{
		if (!_pickedUp || !_scene || _heldBox == 0)
		{
			return camera::HeldObjectLook {};
		}
		const auto& box = _scene->Get(_heldBox);
		return camera::HeldObjectLook {.held = true,
		                               .name = "box",
		                               .renderers = 1,
		                               .shadowOnly = box.hidden ? 1 : 0,
		                               .lowestOpacity = box.opacity,
		                               .changedByFade = box.hidden || box.opacity < 1.0f};
	}

	/// The fade of the objects of the model after the frame, as the research mod runs it after the camera loop.
	void StepFade(float deltaTime)
	{
		if (!_scene)
		{
			_scene = camera::test::FakeFadeScene::Of(_harness.game);
		}
		const auto& game = _harness.game;
		if (_pickedUp)
		{
			const auto at = game.transform.position;
			const WorldPoint half {0.5f, 0.5f, 0.5f};
			if (_heldBox == 0)
			{
				_heldBox = _scene->Add(at - half, at + half);
				_scene->Get(_heldBox).held = true;
			}
			_scene->Get(_heldBox).minimum = at - half;
			_scene->Get(_heldBox).maximum = at + half;
		}
		_fade.Step(*_scene, camera::FadeSettingsOf(_settings), {.camera = game.transform.position}, deltaTime);
	}
	/// The camera mod stood back: the objects go back to the game.
	void RestoreFade()
	{
		if (_scene)
		{
			_fade.RestoreAll(*_scene);
		}
	}

	/// The model has no object: a press of the left button on the object at the hand takes it.
	void TakeObject(const VirtualInput& input)
	{
		if (_objectAtHand && input.ButtonDown(0))
		{
			_harness.game.holdsObject = true;
		}
		if (input.ButtonUp(0))
		{
			_harness.game.holdsObject = false;
		}
	}

private:
	camera::test::CameraLoopHarness& _harness;
	moa_mod::camera::CameraSettings _settings;
	bool _objectAtHand = false;
	bool _pickedUp = false;
	uint64_t _heldBox = 0; ///< The box in the hand in the scene of the fade, or 0.
	std::optional<camera::test::FakeFadeScene> _scene;
	camera::OccluderFade _fade;
};

/// The hotkeys of the camera spots and of the reset of the view, as the research mod gives them to the camera loop.
class OfflineHotkeys
{
public:
	void Poll(const VirtualInput& input, camera::CameraLoop& loop)
	{
		constexpr int32_t k_F1 = 282;
		constexpr int32_t k_F5 = 286;
		const auto control = input.Key(305) || input.Key(306);
		for (int32_t index = 0; index < 4; ++index)
		{
			if (!input.KeyDown(k_F1 + index))
			{
				continue;
			}
			if (control)
			{
				_spots[static_cast<size_t>(index)] = loop.CurrentSpot();
			}
			else if (const auto& spot = _spots[static_cast<size_t>(index)])
			{
				static_cast<void>(loop.FlyToSpot(*spot));
			}
		}
		if (input.KeyDown(k_F5) && !control)
		{
			static_cast<void>(loop.ResetView(0.0f));
		}
	}

private:
	std::array<std::optional<camera::CameraSpot>, 4> _spots;
};

/// Run a scenario in the model of the game at a frame rate. Return the runner, with the result.
[[nodiscard]] inline ScenarioRunner RunOffline(const Scenario& scenario, float framesPerSecond)
{
	const auto settings = AutotestSettings();
	camera::test::CameraLoopHarness harness(settings);
	auto& game = harness.game;
	MakeWorld(game, scenario.offline.name);
	game.Start({0.0f, 0.0f, 0.0f}, 1.0f);
	OfflineHost host(harness, settings);
	OfflineHotkeys hotkeys;
	ScenarioRunner runner(scenario);
	VirtualInput input;
	const Vector2 screen {game.screenWidth, game.screenHeight};
	input.Start(screen, runner.StartCursor(screen));
	game.mouse = input.Mouse();
	const auto deltaTime = 1.0f / framesPerSecond;
	const auto limit = static_cast<int>(120.0f * framesPerSecond);
	double seconds = 0.0;
	size_t eventsSeen = 0;
	for (int frame = 0; frame < limit && !runner.Done(); ++frame)
	{
		seconds += static_cast<double>(deltaTime);
		runner.BeforeFrame(host, &game, seconds);
		if (runner.Done())
		{
			break;
		}
		// The cursor lock of the camera loop moves the cursor of the model; the virtual mouse continues from there.
		if (game.mouse.x != input.Mouse().x || game.mouse.y != input.Mouse().y)
		{
			static_cast<void>(input.Warp(game.mouse));
		}
		input.BeginFrame(runner.TakeInput(seconds), screen);
		hotkeys.Poll(input, harness.loop);
		host.TakeObject(input);
		game.mouse = input.Mouse();
		for (size_t button = 0; button < k_ButtonCount; ++button)
		{
			game.buttons[button] = input.Button(static_cast<int32_t>(button));
		}
		game.wheel = input.WheelAxis();
		// The move keys of the game: W, S, A, D and the arrows.
		const auto held = [&](int32_t a, int32_t b) { return input.Key(a) || input.Key(b); };
		game.panKeys = {.x = (held('d', 275) ? 1.0f : 0.0f) - (held('a', 276) ? 1.0f : 0.0f),
		                .y = (held('w', 273) ? 1.0f : 0.0f) - (held('s', 274) ? 1.0f : 0.0f)};
		std::optional<std::string> standBack;
		if (game.blend)
		{
			// The game blends its camera: the camera mod stands back, as in the game.
			harness.seconds += static_cast<double>(deltaTime);
			harness.loop.StandBack();
			game.BlendFrame(deltaTime);
			game.EndFrame();
			host.RestoreFade();
			standBack = "a blend of the camera of the game";
		}
		else
		{
			harness.Frame(deltaTime);
			host.StepFade(deltaTime);
		}
		for (; eventsSeen < game.events.size(); ++eventsSeen)
		{
			runner.OnEvent(game.events[eventsSeen].first, game.events[eventsSeen].second);
		}
		runner.AfterFrame(host, &game, input, standBack, seconds);
	}
	if (!runner.Done())
	{
		runner.Stop(host, "the scenario did not end in 120 seconds of the model");
	}
	return runner;
}

/// The scenario files of the camera tests.
[[nodiscard]] inline std::vector<std::filesystem::path> ScenarioFiles()
{
	std::vector<std::filesystem::path> files;
	std::error_code error;
	for (const auto& entry : std::filesystem::directory_iterator(AutotestFolder() / "scenarios", error))
	{
		if (entry.path().extension() == ".json")
		{
			files.push_back(entry.path());
		}
	}
	std::ranges::sort(files);
	return files;
}

} // namespace moa_mod::autotest::test
