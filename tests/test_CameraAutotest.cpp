/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <format>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "AutotestHarness.h"

namespace
{

using moa_mod::autotest::Button;
using moa_mod::autotest::Check;
using moa_mod::autotest::InputEvent;
using moa_mod::autotest::InputTimeline;
using moa_mod::autotest::KeyCodeOf;
using moa_mod::autotest::Outcome;
using moa_mod::autotest::ParseScenarioText;
using moa_mod::autotest::Scenario;
using moa_mod::autotest::ScenarioRunner;
using moa_mod::autotest::Step;
using moa_mod::autotest::VirtualInput;
using moa_mod::autotest::test::RunOffline;
using moa_mod::autotest::test::ScenarioFiles;

constexpr moa_mod::Vector2 k_Screen {1920.0f, 1080.0f};

[[nodiscard]] Scenario Parse(const std::string& text)
{
	auto scenario = ParseScenarioText(text);
	EXPECT_TRUE(scenario) << (scenario ? "" : scenario.GetError().message);
	return scenario ? *scenario : Scenario {};
}

TEST(AutotestScenario, ReadsTheStepsTheSetupAndTheChecks)
{
	const auto scenario = Parse(R"({
		"name": "turn",
		"setup": {"stage": "Production", "heading": 30, "place": {"point": [10, 20]}, "cursor": [0.25, 0.75],
		          "settings": {"turnInertia": 0}},
		"steps": [{"drag": "middle", "at": [0.5, 0.6], "right": 400, "seconds": 1.5},
		          {"wheel": -3, "interval": 0.1}, {"hotkey": "Ctrl+F4"}, {"wait": 0.5}],
		"checks": [{"rule": "pivotFixed", "pixels": 3}, {"rule": "change", "of": "heading", "min": 10}],
		"offline": {"world": "hills", "fps": [30, 144]}
	})");
	EXPECT_EQ(scenario.name, "turn");
	ASSERT_TRUE(scenario.setup.stage);
	EXPECT_FLOAT_EQ(*scenario.setup.stage, 1.0f);
	EXPECT_EQ(scenario.setup.place.kind, moa_mod::autotest::Placement::Kind::Point);
	EXPECT_FLOAT_EQ(scenario.setup.place.point.z, 20.0f);
	ASSERT_EQ(scenario.steps.size(), 4U);
	EXPECT_EQ(scenario.steps[0].kind, Step::Kind::Drag);
	EXPECT_EQ(scenario.steps[0].button, Button::Middle);
	EXPECT_FLOAT_EQ(scenario.steps[1].Duration(), 0.3f);
	ASSERT_EQ(scenario.checks.size(), 2U);
	EXPECT_EQ(scenario.checks[0].rule, Check::Rule::PivotFixed);
	EXPECT_FLOAT_EQ(scenario.checks[0].pixels, 3.0f);
	EXPECT_EQ(scenario.offline.framesPerSecond.size(), 2U);
}

TEST(AutotestScenario, RefusesAWrongStepOrCheck)
{
	EXPECT_FALSE(ParseScenarioText(R"({"name": "a", "steps": [{"jump": 1}]})"));
	EXPECT_FALSE(ParseScenarioText(R"({"name": "a", "steps": [{"press": "fourth"}]})"));
	EXPECT_FALSE(ParseScenarioText(R"({"name": "a", "steps": [{"wait": 1}], "checks": [{"rule": "x"}]})"));
	EXPECT_FALSE(ParseScenarioText(R"({"name": "a", "steps": [{"wait": 1}], "setup": {"cursor": [2, 0]}})"));
	EXPECT_FALSE(ParseScenarioText(R"({"steps": [{"wait": 1}]})"));
}

TEST(AutotestTimeline, ADragMovesTheMouseAtAConstantSpeedAndReleasesAtTheEnd)
{
	const auto scenario = Parse(R"({"name": "d", "steps": [{"drag": "middle", "at": [0.5, 0.5], "right": 300, "up": -60,
	                                                         "seconds": 1.0}]})");
	auto timeline = InputTimeline::Make(scenario.steps);
	ASSERT_TRUE(timeline);
	VirtualInput input;
	input.Start(k_Screen, {0.0f, 0.0f, 0.0f});
	double last = moa_mod::autotest::k_BeforeSteps;
	int presses = 0;
	int releases = 0;
	for (int frame = 1; frame <= 70; ++frame)
	{
		const auto now = frame / 60.0;
		input.BeginFrame(timeline->Window(last, now), k_Screen);
		last = now;
		presses += input.ButtonDown(2) ? 1 : 0;
		releases += input.ButtonUp(2) ? 1 : 0;
		if (frame == 30)
		{
			EXPECT_TRUE(input.Button(2));
			EXPECT_NEAR(input.Mouse().x, 960.0f + 150.0f, 0.01f);
			EXPECT_NEAR(input.Mouse().y, 540.0f - 30.0f, 0.01f);
			EXPECT_NEAR(input.MouseDelta().x, 5.0f, 0.01f);
		}
	}
	EXPECT_EQ(presses, 1);
	EXPECT_EQ(releases, 1);
	EXPECT_FALSE(input.Button(2));
	EXPECT_NEAR(input.Mouse().x, 1260.0f, 0.01f);
}

TEST(AutotestTimeline, APressAndAReleaseInOneFrameGiveBothToTheGame)
{
	const auto scenario = Parse(R"({"name": "c", "steps": [{"doubleClick": "left", "at": [0.25, 0.5]}]})");
	auto timeline = InputTimeline::Make(scenario.steps);
	ASSERT_TRUE(timeline);
	VirtualInput input;
	input.Start(k_Screen, {});
	// One frame of 0.2 seconds has both clicks: the game sees a press, then a release in the next frame.
	input.BeginFrame(timeline->Window(moa_mod::autotest::k_BeforeSteps, 0.2), k_Screen);
	EXPECT_TRUE(input.ButtonDown(0));
	EXPECT_TRUE(input.Button(0));
	EXPECT_NEAR(input.Mouse().x, 480.0f, 0.01f);
	input.BeginFrame(timeline->Window(0.2, 0.3), k_Screen);
	EXPECT_TRUE(input.ButtonUp(0));
	EXPECT_FALSE(input.Button(0));
}

TEST(AutotestTimeline, EachEventIsInOneWindowOnly)
{
	const auto scenario = Parse(R"({"name": "w", "steps": [{"wheel": 6, "interval": 0.1}]})");
	auto timeline = InputTimeline::Make(scenario.steps);
	ASSERT_TRUE(timeline);
	for (const auto step : {0.1, 1.0 / 60.0, 1.0 / 144.0})
	{
		int notches = 0;
		double last = moa_mod::autotest::k_BeforeSteps;
		for (double now = 0.0; now < 1.0; now += step)
		{
			for (const auto& event : timeline->Window(last, now).events)
			{
				notches += event.notches;
			}
			last = now;
		}
		EXPECT_EQ(notches, 6) << step;
	}
}

TEST(AutotestTimeline, AHotkeyHoldsItsModifierAndItsKey)
{
	const auto scenario = Parse(R"({"name": "h", "steps": [{"hotkey": "Ctrl+F4"}, {"wheel": 2, "interval": 0.1}]})");
	auto timeline = InputTimeline::Make(scenario.steps);
	ASSERT_TRUE(timeline);
	VirtualInput input;
	input.Start(k_Screen, {});
	input.BeginFrame(timeline->Window(moa_mod::autotest::k_BeforeSteps, 0.05), k_Screen);
	EXPECT_TRUE(input.KeyDown(*KeyCodeOf("F4")));
	EXPECT_TRUE(input.Key(*KeyCodeOf("LeftControl")));
	EXPECT_TRUE(input.AnyKeyDown());
	input.BeginFrame(timeline->Window(0.05, 0.25), k_Screen);
	EXPECT_TRUE(input.KeyUp(*KeyCodeOf("F4")));
	EXPECT_NEAR(input.WheelAxis(), 0.1f, 1e-6f);
	EXPECT_NEAR(input.Axis("Mouse ScrollWheel"), 0.1f, 1e-6f);
	input.BeginFrame(timeline->Window(0.25, 0.35), k_Screen);
	EXPECT_NEAR(input.WheelAxis(), 0.1f, 1e-6f);
	EXPECT_FALSE(input.AnyKey());
	EXPECT_EQ(KeyCodeOf("W"), 119);
	EXPECT_EQ(KeyCodeOf("Mouse2"), 325);
	EXPECT_EQ(KeyCodeOf("F15"), 296);
	EXPECT_FALSE(KeyCodeOf("Nothing"));
}

TEST(AutotestTimeline, TheMiddleButtonIsAlsoTheKeyMouse2AndAWarpMovesTheMouse)
{
	const auto scenario = Parse(R"({"name": "m", "steps": [{"press": "middle"}, {"move": {"right": 100}, "seconds": 1}]})");
	auto timeline = InputTimeline::Make(scenario.steps);
	ASSERT_TRUE(timeline);
	VirtualInput input;
	input.Start(k_Screen, {960.0f, 540.0f, 0.0f});
	input.BeginFrame(timeline->Window(moa_mod::autotest::k_BeforeSteps, 0.5), k_Screen);
	EXPECT_TRUE(input.Key(*KeyCodeOf("Mouse2")));
	EXPECT_TRUE(input.KeyDown(*KeyCodeOf("Mouse2")));
	static_cast<void>(input.Warp({100.0f, 100.0f, 0.0f}));
	input.BeginFrame(timeline->Window(0.5, 1.0), k_Screen);
	EXPECT_NEAR(input.Mouse().x, 150.0f, 0.01f);
	EXPECT_NEAR(input.MouseDelta().x, 50.0f, 0.01f);
}

/// Each scenario file of the camera tests passes in the model of the game at each of its frame rates.
class AutotestScenarioFile: public testing::TestWithParam<std::filesystem::path>
{
};

TEST_P(AutotestScenarioFile, PassesInTheModelOfTheGame)
{
	std::ifstream file(GetParam());
	std::stringstream text;
	text << file.rdbuf();
	auto scenario = ParseScenarioText(text.str());
	ASSERT_TRUE(scenario) << scenario.GetError().message;
	if (!scenario->offline.skip.empty())
	{
		GTEST_SKIP() << scenario->offline.skip;
	}
	for (const auto rate : scenario->offline.framesPerSecond)
	{
		SCOPED_TRACE(testing::Message() << rate << " frames per second");
		const auto runner = RunOffline(*scenario, rate);
		if (runner.Result() != Outcome::Passed)
		{
			// Keep the frames of a failed run for the analysis.
			const auto path = std::filesystem::path(testing::TempDir()) /
			                  std::format("autotest-{}-{}fps.csv", scenario->name, static_cast<int>(rate));
			std::ofstream(path) << runner.FramesCsv();
			ADD_FAILURE() << "frames: " << path.string();
		}
		EXPECT_EQ(runner.Result(), Outcome::Passed) << runner.ResultJson().dump(2);
	}
}

INSTANTIATE_TEST_SUITE_P(Scenarios, AutotestScenarioFile, testing::ValuesIn(ScenarioFiles()),
                         [](const testing::TestParamInfo<std::filesystem::path>& info) {
	                         auto name = info.param.stem().string();
	                         std::ranges::replace(name, '-', '_');
	                         return name;
                         });

TEST(AutotestSettings, TheSettingsFileOfTheTestsIsCorrect)
{
	std::ifstream file(moa_mod::autotest::test::AutotestFolder() / "settings.json");
	std::stringstream text;
	text << file.rdbuf();
	const auto json = nlohmann::json::parse(text.str());
	const auto parsed = moa_mod::camera::ParseCameraSettings(json.at("camera"));
	ASSERT_TRUE(parsed) << parsed.GetError().message;
	EXPECT_TRUE(parsed->enabled);
	EXPECT_TRUE(parsed->pitch);
	EXPECT_TRUE(parsed->lockCursor);
	EXPECT_EQ(parsed->pitchMode, moa_mod::camera::PitchMode::Hand);
}

/// A runner that sees a broken rule fails, with the frame of the break.
TEST(AutotestRunner, ABrokenRuleFailsTheScenario)
{
	// The cursor lock is off in the settings, so the check of the cursor lock must fail.
	const auto scenario = Parse(R"({"name": "lock", "setup": {"stage": "Production", "settings": {"lockCursor": false}},
		"steps": [{"drag": "middle", "at": [0.5, 0.6], "right": 200, "seconds": 0.5}],
		"checks": [{"rule": "cursorLock"}]})");
	const auto runner = RunOffline(scenario, 60.0f);
	EXPECT_EQ(runner.Result(), Outcome::Failed);
	ASSERT_FALSE(runner.Failures().empty());
	EXPECT_EQ(runner.Failures().front().check, "cursorLock");
	EXPECT_GE(runner.Failures().front().seconds, 0.0);
}

TEST(AutotestRunner, TheChangeOfTheHeadingCountsATurnOfMoreThanHalfACircle)
{
	// A middle drag of 2500 pixels turns about 275 degrees in the model (0.11 degrees for each pixel). The wrapped
	// difference of the start and the end is about 85 degrees; the change of the heading is the whole turn.
	const auto scenario = Parse(R"({"name": "long-turn", "setup": {"stage": "Production", "settings": {"turnInertia": 0}},
		"steps": [{"drag": "middle", "at": [0.5, 0.6], "right": 2500, "up": 0, "seconds": 2.5}],
		"checks": [{"rule": "change", "of": "heading", "min": 200, "max": 330}]})");
	for (const auto rate : {30.0f, 60.0f, 144.0f})
	{
		const auto runner = RunOffline(scenario, rate);
		EXPECT_EQ(runner.Result(), Outcome::Passed) << rate << " fps: " << runner.ResultJson().dump();
	}
}

TEST(AutotestRunner, AChangeOutOfItsRangeFails)
{
	const auto scenario = Parse(R"({"name": "still", "setup": {"stage": "Production"},
		"steps": [{"wait": 0.5}],
		"checks": [{"rule": "change", "of": "heading", "min": 10}]})");
	const auto runner = RunOffline(scenario, 60.0f);
	EXPECT_EQ(runner.Result(), Outcome::Failed);
	EXPECT_NE(runner.FramesCsv().find("frame,seconds,phase"), std::string::npos);
}

} // namespace
