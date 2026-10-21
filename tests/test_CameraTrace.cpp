/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <algorithm>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <moa_mod/camera/CameraSettings.h>
#include <moa_mod/camera/CameraTrace.h>

using namespace moa_mod;
using namespace moa_mod::camera;

namespace
{
/// The lines of the text, with no line ends.
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

size_t Cells(const std::string& line)
{
	return static_cast<size_t>(std::ranges::count(line, ',')) + 1;
}

struct Recorded
{
	std::string text;
	int writes = 0;
	CameraTraceRecorder recorder;

	explicit Recorded(CameraTraceRecorder::Options options = {})
	    : recorder(options, [this](std::string_view value) {
		    text += value;
		    ++writes;
		    return true;
	    })
	{
	}
};

CameraPlacement At(float x)
{
	return {.position = {x, 10.0f, 0.0f}, .rotation = {}};
}
} // namespace

TEST(CameraTrace, WritesTheHeaderAndOneCellForEachColumnName)
{
	Recorded out;
	out.recorder.WriteHeader("camera trace {\"modVersion\": \"0.2.0\"}");
	const auto lines = Lines(out.text);
	ASSERT_EQ(lines.size(), 2u);
	EXPECT_TRUE(lines[0].starts_with("# camera trace"));
	EXPECT_TRUE(lines[1].starts_with("frame,unityFrame,time,dt,"));
	EXPECT_NE(lines[1].find("afterUsMoveX"), std::string::npos);
	EXPECT_NE(lines[1].find("cameraNextStartPosX"), std::string::npos);

	// An empty row and a full row have the same count of cells as the names.
	std::string line;
	CameraTraceRecorder::FormatRow(CameraTraceRow {}, line);
	EXPECT_EQ(Cells(line), Cells(lines[1]));
	CameraTraceRow full;
	full.mouse = WorldPoint {1.0f, 2.0f, 0.0f};
	full.cameraAfterGame = At(1.0f);
	full.loop.writtenCamera = At(2.0f);
	full.loop.pivot = WorldPoint {};
	full.cameraFinal = At(2.0f);
	full.cameraNextStart = At(2.5f);
	full.inhibitAtStart = 3;
	CameraTraceRecorder::FormatRow(full, line);
	EXPECT_EQ(Cells(line), Cells(lines[1]));
}

TEST(CameraTrace, RecordsWhileAButtonIsHeldAndOneSecondAfter)
{
	Recorded out;
	out.recorder.WriteHeader("test");
	const auto headerLines = Lines(out.text).size();
	double seconds = 0.0;
	constexpr double k_Frame = 0.1;
	auto frame = [&](bool held, float x) {
		const auto recording = out.recorder.StartFrame(seconds, held, At(x));
		if (recording)
		{
			out.recorder.Row().cameraFinal = At(x);
		}
		out.recorder.EndFrame(seconds);
		seconds += k_Frame;
		return recording;
	};
	EXPECT_FALSE(frame(false, 0.0f));
	EXPECT_TRUE(frame(true, 0.0f));
	EXPECT_TRUE(frame(true, 0.0f));
	// The camera moved after the end of the last frame: the next start shows it.
	EXPECT_TRUE(frame(false, 0.5f));
	int tail = 0;
	while (frame(false, 0.5f))
	{
		++tail;
	}
	EXPECT_GE(tail, 9);
	EXPECT_LE(tail, 11);
	EXPECT_FALSE(frame(false, 0.5f));
	out.recorder.Close();

	const auto lines = Lines(out.text);
	const auto rows = lines.size() - headerLines;
	EXPECT_EQ(rows, out.recorder.Rows());
	EXPECT_EQ(rows, static_cast<size_t>(3 + tail));
	// Find the column of the move after the mod.
	const auto names = Lines(out.text)[headerLines - 1];
	std::vector<std::string> columns;
	for (auto part : names | std::views::split(','))
	{
		columns.emplace_back(part.begin(), part.end());
	}
	const auto column = static_cast<size_t>(std::ranges::find(columns, "afterUsMoveX") - columns.begin());
	ASSERT_LT(column, columns.size());
	auto cell = [&](size_t row) {
		std::vector<std::string> cells;
		for (auto part : lines[headerLines + row] | std::views::split(','))
		{
			cells.emplace_back(part.begin(), part.end());
		}
		return cells.at(column);
	};
	EXPECT_EQ(cell(0), "0.0000");
	EXPECT_EQ(cell(1), "0.5000");
	EXPECT_EQ(cell(2), "0.0000");
	// The last row gets the camera of the first frame that the trace does not record.
	EXPECT_EQ(cell(rows - 1), "0.0000");

	// A row that `Close` writes has no next frame.
	Recorded closed;
	closed.recorder.WriteHeader("test");
	static_cast<void>(closed.recorder.StartFrame(0.0, true, At(0.0f)));
	closed.recorder.Row().cameraFinal = At(0.0f);
	closed.recorder.EndFrame(0.0);
	closed.recorder.Close();
	EXPECT_TRUE(Lines(closed.text).back().ends_with(",,,,"));
}

TEST(CameraTrace, FlushesAtLeastOnceInEachSecond)
{
	Recorded out({.bufferBytes = 1u << 30});
	out.recorder.WriteHeader("test");
	const auto afterHeader = out.writes;
	double seconds = 0.0;
	for (int i = 0; i < 70; ++i)
	{
		static_cast<void>(out.recorder.StartFrame(seconds, true, At(0.0f)));
		out.recorder.EndFrame(seconds);
		seconds += 1.0 / 60.0;
	}
	EXPECT_GE(out.writes, afterHeader + 1);
	EXPECT_GT(Lines(out.text).size(), 2u);
}

TEST(CameraTrace, StopsAtTheSizeLimit)
{
	Recorded out({.maxBytes = 20'000});
	out.recorder.WriteHeader("test");
	double seconds = 0.0;
	for (int i = 0; i < 1000; ++i)
	{
		static_cast<void>(out.recorder.StartFrame(seconds, true, At(0.0f)));
		out.recorder.EndFrame(seconds);
		seconds += 0.01;
	}
	out.recorder.Close();
	EXPECT_TRUE(out.recorder.Stopped());
	EXPECT_LE(out.text.size(), 20'200u);
	EXPECT_NE(out.text.find("# the trace stopped at the size limit"), std::string::npos);
}

TEST(CameraTrace, FindsAPathInAFolder)
{
	EXPECT_TRUE(IsInsideFolder("F:/SteamLibrary/steamapps/common/Masters of Albion/runs",
	                           "F:\\SteamLibrary\\steamapps\\common\\Masters of Albion"));
	EXPECT_TRUE(IsInsideFolder("d:\\MASTERS_OF_ALBION_ORIGINAL", "D:/masters_of_Albion_original/"));
	EXPECT_TRUE(IsInsideFolder("D:/masters_of_Albion_original/a/../b", "D:/masters_of_Albion_original"));
	EXPECT_FALSE(IsInsideFolder("D:/masters_of_albion/runs", "D:/masters_of_Albion_original"));
	EXPECT_FALSE(IsInsideFolder("D:/masters_of_Albion_original2", "D:/masters_of_Albion_original"));
	EXPECT_FALSE(IsInsideFolder("D:/masters_of_Albion_original/../masters_of_albion", "D:/masters_of_Albion_original"));
}

TEST(CameraTrace, NamesTheFileByTimeAndProcess)
{
	using namespace std::chrono;
	const auto time = sys_days {year {2026} / 10 / 19} + hours {14} + minutes {5} + seconds {9};
	EXPECT_EQ(CameraTraceFileName(time, 1234), "camera-trace-20261019-140509-1234.csv");
}

TEST(CameraTrace, ReadsTheSettings)
{
	const auto off = moa_mod::camera::ParseCameraSettings(nlohmann::json::parse(R"({"enabled": true})"));
	ASSERT_TRUE(off);
	EXPECT_FALSE(off->trace);
	EXPECT_TRUE(off->traceFolder.empty());
	const auto on = moa_mod::camera::ParseCameraSettings(
	    nlohmann::json::parse(R"({"enabled": true, "trace": true, "traceFolder": "E:/traces"})"));
	ASSERT_TRUE(on);
	EXPECT_TRUE(on->trace);
	EXPECT_EQ(on->traceFolder, "E:/traces");
	EXPECT_FALSE(moa_mod::camera::ParseCameraSettings(nlohmann::json::parse(R"({"traceFolder": 3})")));
	EXPECT_FALSE(moa_mod::camera::ParseCameraSettings(nlohmann::json::parse(R"({"trace": "yes"})")));
}

TEST(CameraTrace, KeepsTheRenderSampleInTheRowOfItsFrame)
{
	// The cameras render after the end of the frame of the mod. The render sample goes into the row of that frame,
	// and the row is written at the start of the next frame.
	Recorded out;
	out.recorder.WriteHeader("test");
	const auto headerLines = Lines(out.text).size();
	double seconds = 0.0;
	for (int frame = 0; frame < 3; ++frame)
	{
		ASSERT_TRUE(out.recorder.StartFrame(seconds, true, At(0.0f)));
		out.recorder.Row().frame = static_cast<uint64_t>(frame);
		out.recorder.Row().cameraFinal = At(static_cast<float>(frame));
		out.recorder.EndFrame(seconds);
		// The render callback of this frame.
		auto& row = out.recorder.Row();
		row.renders = frame + 1;
		row.cameraRender = At(static_cast<float>(frame));
		seconds += 0.1;
	}
	out.recorder.Close();
	const auto lines = Lines(out.text);
	ASSERT_EQ(lines.size(), headerLines + 3);
	std::vector<std::string> columns;
	for (auto part : lines[headerLines - 1] | std::views::split(','))
	{
		columns.emplace_back(part.begin(), part.end());
	}
	auto cell = [&](size_t row, const char* name) {
		const auto column = static_cast<size_t>(std::ranges::find(columns, name) - columns.begin());
		std::vector<std::string> cells;
		for (auto part : lines[headerLines + row] | std::views::split(','))
		{
			cells.emplace_back(part.begin(), part.end());
		}
		return cells.at(column);
	};
	for (size_t row = 0; row < 3; ++row)
	{
		EXPECT_EQ(cell(row, "frame"), std::to_string(row));
		EXPECT_EQ(cell(row, "renders"), std::to_string(row + 1));
		EXPECT_EQ(cell(row, "renderMoveX"), "0.0000");
	}
}
