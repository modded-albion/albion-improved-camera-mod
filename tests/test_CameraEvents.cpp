/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <algorithm>
#include <format>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <core/CameraBlackBox.h>
#include <core/CameraEvents.h>
#include <gtest/gtest.h>

#include "CameraLoopHarness.h"

using namespace moa_mod;
using namespace moa_mod::camera;
using moa_mod::camera::test::CameraLoopHarness;

namespace
{

/// Split a line of CSV into its cells, with the quotes of a text cell.
std::vector<std::string> SplitCsv(const std::string& line)
{
	std::vector<std::string> cells(1);
	bool quoted = false;
	for (size_t index = 0; index < line.size(); ++index)
	{
		const auto c = line[index];
		if (quoted)
		{
			if (c == '"' && index + 1 < line.size() && line[index + 1] == '"')
			{
				cells.back() += '"';
				++index;
			}
			else if (c == '"')
			{
				quoted = false;
			}
			else
			{
				cells.back() += c;
			}
		}
		else if (c == '"')
		{
			quoted = true;
		}
		else if (c == ',')
		{
			cells.emplace_back();
		}
		else if (c != '\n')
		{
			cells.back() += c;
		}
	}
	return cells;
}

/// The index of a column of the trace.
size_t Column(const std::string& name)
{
	const auto names = SplitCsv(CameraTraceRecorder::ColumnNames());
	return static_cast<size_t>(std::ranges::find(names, name) - names.begin());
}

/// A row of a frame where the mod changed the camera.
CameraTraceRow ActiveRow(double time, float x)
{
	CameraTraceRow row;
	row.time = time;
	row.active = true;
	row.cameraFinal = CameraPlacement {.position = {x, 120.0f, 0.0f}, .rotation = {0.0f, 0.0f, 0.0f, 1.0f}};
	row.landAtFinal = 100.0f;
	return row;
}

std::vector<std::string> Lines(const std::string& text)
{
	std::vector<std::string> lines;
	std::istringstream stream(text);
	for (std::string line; std::getline(stream, line);)
	{
		lines.push_back(line);
	}
	return lines;
}

} // namespace

TEST(CameraEvents, TheTextOfACellHasAFixedSize)
{
	EventText text;
	EXPECT_TRUE(text.Empty());
	text.Append("gesture turn starts");
	text.Append("pivot taken");
	EXPECT_EQ(text.View(), "gesture turn starts | pivot taken");
	text.Assign(std::string(1000, 'x'));
	EXPECT_EQ(text.View().size(), EventText::k_Capacity);
	text.Append("more");
	EXPECT_EQ(text.View().size(), EventText::k_Capacity);
}

TEST(CameraEvents, TheLogCountsRepeatsAndTellsTheCountLater)
{
	CameraEventLog log({.capacity = 4, .repeatSeconds = 2.0});
	std::vector<std::string> lines;
	log.SetSink([&](const CameraEvent& event) { lines.push_back(FormatEventLine(event)); });
	EXPECT_TRUE(log.Add(1, 0.0, CameraEventKind::CursorLock, "cursor lock off (release)"));
	EXPECT_FALSE(log.Add(2, 0.5, CameraEventKind::CursorLock, "cursor lock off (release)"));
	EXPECT_FALSE(log.Add(3, 1.0, CameraEventKind::CursorLock, "cursor lock off (release)"));
	// Another text is not a repeat.
	EXPECT_TRUE(log.Add(4, 1.0, CameraEventKind::Pivot, "pivot taken"));
	EXPECT_TRUE(log.Add(5, 2.5, CameraEventKind::CursorLock, "cursor lock off (release)"));
	ASSERT_EQ(lines.size(), 3u);
	EXPECT_EQ(lines[0], "camera: cursor lock off (release)");
	EXPECT_EQ(lines[2], "camera: cursor lock off (release) (2 more times since the last line)");
	// The log keeps the last events only.
	for (int index = 0; index < 10; ++index)
	{
		static_cast<void>(log.Add(10 + index, 10.0 + index * 3.0, CameraEventKind::Gesture, std::format("event {}", index)));
	}
	EXPECT_EQ(log.Size(), 4u);
	const auto recent = log.RecentJson(2);
	ASSERT_EQ(recent.size(), 2u);
	EXPECT_EQ(recent[0]["text"], "event 8");
	EXPECT_EQ(recent[1]["text"], "event 9");
	EXPECT_EQ(recent[1]["kind"], "gesture");
	EXPECT_EQ(FormatPoint({1.25f, -2.0f, 300.04f}), "(1.2, -2.0, 300.0)");
}

TEST(CameraEvents, ATextCellIsInQuotes)
{
	CameraTraceRow row;
	row.loop.events.Append("pivot taken (middle press) at (1.0, 2.0, 3.0)");
	row.loop.events.Append("arm hit 'House \"A\"' at 4.0 m");
	row.loop.gesture = Gesture::TurnPitch;
	row.loop.pivotReason = PivotReason::Press;
	row.invariant = Invariant::CameraJump;
	std::string line;
	CameraTraceRecorder::FormatRow(row, line);
	const auto cells = SplitCsv(line);
	ASSERT_EQ(cells.size(), SplitCsv(CameraTraceRecorder::ColumnNames()).size());
	EXPECT_EQ(cells[Column("events")], "pivot taken (middle press) at (1.0, 2.0, 3.0) | arm hit 'House \"A\"' at 4.0 m");
	EXPECT_EQ(cells[Column("gesture")], "turn+pitch");
	EXPECT_EQ(cells[Column("pivotReason")], "middle press");
	EXPECT_EQ(cells[Column("invariant")], "camera jump");
	EXPECT_EQ(cells[Column("modEvents")], "");
}

TEST(CameraBlackBox, TheRingKeepsTheLastSecondsAndTheLastRows)
{
	CameraBlackBox box({.seconds = 1.0, .capacity = 50, .maxDumps = 0}, {});
	for (int frame = 0; frame < 200; ++frame)
	{
		auto row = ActiveRow(frame * 0.01, 0.0f);
		row.frame = static_cast<uint64_t>(frame);
		static_cast<void>(box.Add(row));
	}
	// 50 rows fit, and they are in the last second.
	ASSERT_EQ(box.Size(), 50u);
	EXPECT_EQ(box.At(0).frame, 150u);
	EXPECT_EQ(box.At(49).frame, 199u);
	// With room for more rows, the time limits the rows: the rows of the last second (frames of 1/64 s).
	CameraBlackBox timed({.seconds = 1.0, .capacity = 500, .maxDumps = 0}, {});
	for (int frame = 0; frame < 300; ++frame)
	{
		auto row = ActiveRow(frame / 64.0, 0.0f);
		row.frame = static_cast<uint64_t>(frame);
		static_cast<void>(timed.Add(row));
	}
	EXPECT_EQ(timed.Size(), 65u);
	EXPECT_EQ(timed.At(0).frame, 235u);
}

TEST(CameraBlackBox, EachRuleBreaksOnlyForItsCase)
{
	auto check = [](std::vector<CameraTraceRow> rows) {
		InvariantChecker checker;
		std::optional<InvariantBreak> last;
		for (const auto& row : rows)
		{
			last = checker.Check(row);
		}
		return last ? std::optional<Invariant>(last->invariant) : std::nullopt;
	};
	auto held = [](double time) {
		auto row = ActiveRow(time, 0.0f);
		row.loop.modMiddle = true;
		row.loop.desiredDistance = 20.0f;
		return row;
	};
	// A good hold.
	EXPECT_FALSE(check({held(0.0), held(0.1), held(0.2)}));
	// A new pivot at the press is good; a new pivot later in the hold is not.
	auto press = held(0.0);
	press.loop.desiredChange = 2;
	EXPECT_FALSE(check({ActiveRow(0.0, 0.0f), press}));
	auto later = held(0.1);
	later.loop.desiredChange = 2;
	EXPECT_EQ(check({held(0.0), later}), Invariant::DesiredChange);
	// A zoom may change the desired distance.
	auto zoom = held(0.1);
	zoom.loop.desiredChange = 1;
	EXPECT_FALSE(check({held(0.0), zoom}));
	// The cursor lock.
	auto free = held(0.1);
	free.loop.cursorLockOff = static_cast<int>(CursorLockOff::WarpFailed);
	EXPECT_EQ(check({held(0.0), free}), Invariant::CursorLockOff);
	// Below the land.
	auto below = ActiveRow(0.1, 0.0f);
	below.landAtFinal = 121.0f;
	EXPECT_EQ(check({below}), Invariant::BelowLand);
	// A jump: 60 m when the camera is 20 m above the land.
	EXPECT_EQ(check({ActiveRow(0.0, 0.0f), ActiveRow(0.1, 60.0f)}), Invariant::CameraJump);
	EXPECT_FALSE(check({ActiveRow(0.0, 0.0f), ActiveRow(0.1, 40.0f)}));
	// A flight may move far.
	auto flight = ActiveRow(0.1, 60.0f);
	flight.loop.flight = 0.5f;
	EXPECT_FALSE(check({ActiveRow(0.0, 0.0f), flight}));
	// A kept pivot that moves on the screen.
	auto pivotAt = [](double time, float screenX) {
		auto row = ActiveRow(time, 0.0f);
		row.loop.pivot = WorldPoint {5.0f, 100.0f, 5.0f};
		row.pivotScreen = WorldPoint {screenX, 500.0f, 20.0f};
		return row;
	};
	EXPECT_FALSE(check({pivotAt(0.0, 900.0f), pivotAt(0.1, 905.0f)}));
	EXPECT_EQ(check({pivotAt(0.0, 900.0f), pivotAt(0.1, 920.0f)}), Invariant::PivotDrift);
	// Not a number.
	auto nan = ActiveRow(0.1, std::numeric_limits<float>::quiet_NaN());
	EXPECT_EQ(check({nan}), Invariant::NotANumber);
	// A frame where the mod stands back is not checked.
	auto standBack = below;
	standBack.standBack = true;
	standBack.active = false;
	EXPECT_FALSE(check({standBack}));
}

TEST(CameraBlackBox, ABreakWritesTheRowsAFewTimesInASession)
{
	std::vector<std::pair<std::string, std::string>> files;
	CameraBlackBox box({.seconds = 10.0, .capacity = 100, .maxDumps = 2, .cooldownSeconds = 5.0},
	                   [&](std::string_view name, std::string_view text) {
		                   files.emplace_back(std::string(name), std::string(text));
		                   return true;
	                   });
	box.SetHeader("camera trace {\"test\": true}");
	double time = 0.0;
	float x = 0.0f;
	auto frame = [&](bool jump) {
		x += jump ? 100.0f : 0.1f;
		auto row = ActiveRow(time, x);
		time += 0.1;
		const auto broken = box.Add(row);
		EXPECT_EQ(row.invariant.has_value(), broken.has_value());
		return broken;
	};
	for (int index = 0; index < 20; ++index)
	{
		EXPECT_FALSE(frame(false));
	}
	ASSERT_TRUE(frame(true));
	ASSERT_EQ(files.size(), 1u);
	EXPECT_EQ(files[0].first, "camera jump");
	const auto lines = Lines(files[0].second);
	ASSERT_EQ(lines.size(), 3u + 21u);
	EXPECT_EQ(lines[0], "# camera trace {\"test\": true}");
	EXPECT_TRUE(lines[1].starts_with("# camera black box: camera jump: the camera moved 100.0 m"));
	EXPECT_EQ(lines[2], CameraTraceRecorder::ColumnNames());
	EXPECT_EQ(SplitCsv(lines.back())[Column("invariant")], "camera jump");
	// The next break in the cool down time writes no file.
	ASSERT_TRUE(frame(true));
	EXPECT_EQ(files.size(), 1u);
	for (int index = 0; index < 60; ++index)
	{
		static_cast<void>(frame(false));
	}
	ASSERT_TRUE(frame(true));
	EXPECT_EQ(files.size(), 2u);
	// The limit of files for the session.
	for (int index = 0; index < 60; ++index)
	{
		static_cast<void>(frame(false));
	}
	ASSERT_TRUE(frame(true));
	EXPECT_EQ(files.size(), 2u);
	EXPECT_EQ(box.Dumps(), 2);
}

TEST(CameraBlackBox, TheRecorderGivesEachRowAlsoWithNoFile)
{
	std::vector<CameraTraceRow> rows;
	CameraTraceRecorder recorder({}, {});
	recorder.SetRowDone([&](CameraTraceRow& row) { rows.push_back(row); });
	for (int frame = 0; frame < 5; ++frame)
	{
		const CameraPlacement camera {.position = {static_cast<float>(frame), 0.0f, 0.0f}, .rotation = {}};
		ASSERT_TRUE(recorder.StartFrame(frame * 0.1, false, camera));
		recorder.Row().frame = static_cast<uint64_t>(frame);
		recorder.EndFrame(frame * 0.1);
	}
	EXPECT_FALSE(recorder.FileOpen());
	EXPECT_EQ(recorder.Rows(), 0u);
	ASSERT_EQ(rows.size(), 4u);
	// The row of a frame has the camera at the start of the next frame.
	EXPECT_EQ(rows[1].frame, 1u);
	ASSERT_TRUE(rows[1].cameraNextStart);
	EXPECT_EQ(rows[1].cameraNextStart->position.x, 2.0f);

	// A file opens later, and gets only the frames with a button.
	std::string text;
	recorder.OpenFile([&](std::string_view value) {
		text += value;
		return true;
	});
	recorder.WriteHeader("test");
	for (int frame = 5; frame < 8; ++frame)
	{
		ASSERT_TRUE(recorder.StartFrame(frame * 0.1, frame == 6, std::nullopt));
		recorder.EndFrame(frame * 0.1);
	}
	recorder.Close();
	EXPECT_TRUE(recorder.FileOpen() == false);
	EXPECT_EQ(Lines(text).size(), 2u + 2u);
	EXPECT_EQ(rows.size(), 7u);
}

TEST(CameraLoopEvents, AMiddleDragTellsTheGestureThePivotAndTheCursorLock)
{
	constexpr float k_DeltaTime = 1.0f / 60.0f;
	moa_mod::camera::CameraSettings settings;
	settings.enabled = true;
	settings.pitch = true;
	settings.lookUp = true;
	settings.pitchMode = PitchMode::Hand;
	CameraLoopHarness harness(settings);
	auto& game = harness.game;
	game.turnDegreesPerPixel = 0.1f;
	game.gameButtonWithoutMiddle = true;
	game.mouse = {1100.0f, 300.0f, 0.0f};
	game.Start({}, 1.0f);
	for (float time = 0.0f; time < 1.0f; time += k_DeltaTime)
	{
		harness.Frame(k_DeltaTime);
	}
	game.events.clear();
	game.buttons[2] = true;
	harness.Frame(k_DeltaTime);
	EXPECT_EQ(harness.loop.Trace().gesture, Gesture::Hold);
	EXPECT_EQ(harness.loop.Trace().pivotReason, PivotReason::Press);
	for (int frame = 0; frame < 20; ++frame)
	{
		game.mouse.x += 4.0f;
		harness.Frame(k_DeltaTime);
	}
	EXPECT_EQ(harness.loop.Trace().gesture, Gesture::Turn);
	game.buttons[2] = false;
	harness.Frame(k_DeltaTime);
	std::vector<std::string> texts;
	for (const auto& [kind, text] : game.events)
	{
		texts.push_back(std::format("{}: {}", Name(kind), text));
	}
	auto has = [&](std::string_view part) {
		return std::ranges::any_of(texts, [&](const std::string& text) { return text.find(part) != std::string::npos; });
	};
	for (const auto& text : texts)
	{
		std::cout << text << "\n";
	}
	EXPECT_TRUE(has("gesture: gesture hold starts (middle press, source the turn input of the game, pivot ("));
	EXPECT_TRUE(has("pivot: pivot taken (middle press) at ("));
	EXPECT_TRUE(has("cursor lock: cursor lock on at ("));
	EXPECT_TRUE(has("gesture: gesture hold ends; gesture turn starts"));
	EXPECT_TRUE(has("cursor lock: cursor lock off (release)"));
	EXPECT_TRUE(has("gesture: gesture turn ends; gesture glide starts"));
	EXPECT_FALSE(has("re-taken"));
	EXPECT_FALSE(has("cursor lock off (the"));
}
