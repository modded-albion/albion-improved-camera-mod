/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <moa_mod/UnityMath.h>
#include <nlohmann/json.hpp>

namespace moa_mod::camera
{

/// A short text with a fixed size, for a cell of the trace. It never allocates memory, so a trace row can be copied
/// in each frame. A text that is too long is cut.
class EventText
{
public:
	static constexpr size_t k_Capacity = 239;

	/// Add a text. If the text is not empty, put " | " between the old and the new text.
	void Append(std::string_view text);
	void Assign(std::string_view text)
	{
		_size = 0;
		Append(text);
	}
	[[nodiscard]] std::string_view View() const { return {_text.data(), _size}; }
	[[nodiscard]] bool Empty() const { return _size == 0; }

private:
	std::array<char, k_Capacity> _text {};
	uint8_t _size = 0;
};

/// What the user does with the camera in a frame.
enum class Gesture : uint8_t
{
	None,
	Turn,      ///< The middle button turns the camera, or the keys turn it.
	Pitch,     ///< The middle button changes the pitch.
	TurnPitch, ///< The middle button turns the camera and changes the pitch.
	Hold,      ///< The middle button is held with no move.
	LandDrag,  ///< The right button drags the land.
	Glide,     ///< A glide after a turn, a pitch or a land drag.
	Flight,    ///< The camera flies to a point.
	Zoom,      ///< The zoom moves the camera.
	Ground,    ///< The camera is in the ground range.
};

[[nodiscard]] std::string_view Name(Gesture gesture);

/// Why the hand rig took a pivot.
enum class PivotReason : uint8_t
{
	Press,      ///< A press of the middle button.
	Wheel,      ///< A notch of the wheel.
	Turn,       ///< A turn with no press (the keys of the game).
	Zoom,       ///< A zoom with no press (the keys or the smoothing).
	Pitch,      ///< A pitch with no press (the glide or the ease).
	GroundExit, ///< The camera left the ground range.
};

[[nodiscard]] std::string_view Name(PivotReason reason);

/// The kinds of the events of the camera.
enum class CameraEventKind : uint8_t
{
	Gesture,    ///< A gesture starts or ends.
	Pivot,      ///< The hand rig takes a pivot.
	Desired,    ///< The desired distance of the spring arm changes.
	Arm,        ///< The spring arm touches an object or the land, or is clear again.
	CursorLock, ///< The cursor lock starts or stops.
	StandBack,  ///< The mod stands back, or is active again.
	Input,      ///< The readers of the buttons do not agree.
	Invariant,  ///< A rule of the camera broke.
	BlackBox,   ///< The black box wrote a file.
};

[[nodiscard]] std::string_view Name(CameraEventKind kind);

/// The rules of the camera that the black box checks in each frame.
enum class Invariant : uint8_t
{
	DesiredChange = 1, ///< The desired distance changed while the middle button stays held, with no zoom.
	CursorLockOff,     ///< The middle button is held, but the hand rig does not keep the cursor.
	BelowLand,         ///< The camera is below the land height of the game.
	PivotDrift,        ///< The pivot moved on the screen in a frame that kept it.
	CameraJump,        ///< The camera moved too far in one frame.
	NotANumber,        ///< A value of the camera is not a number.
};

[[nodiscard]] std::string_view Name(Invariant invariant);

/// One event of the camera.
struct CameraEvent
{
	uint64_t frame = 0;
	double seconds = 0.0;
	CameraEventKind kind = CameraEventKind::Gesture;
	std::string text;
	int repeats = 0; ///< The count of the same events that the log did not tell before this one.
};

/// The last events of the camera, with a limit for repeats. An event with the same kind and text as an event in the
/// last `repeatSeconds` is only counted. The next event of that kind and text after that time tells the count.
class CameraEventLog
{
public:
	/// Receives each event that the log tells, for example to write a line to the log file.
	using Sink = std::function<void(const CameraEvent&)>;

	struct Options
	{
		size_t capacity = 256;      ///< The count of events that the log keeps.
		double repeatSeconds = 2.0; ///< The time in which the same event is only counted.
	};

	CameraEventLog();
	explicit CameraEventLog(Options options);

	void SetSink(Sink sink) { _sink = std::move(sink); }
	/// Add an event. Return true if the log told it, false if it only counted it.
	bool Add(uint64_t frame, double seconds, CameraEventKind kind, std::string text);
	/// The last `count` events, the oldest first.
	[[nodiscard]] std::vector<CameraEvent> Recent(size_t count) const;
	[[nodiscard]] nlohmann::json RecentJson(size_t count) const;
	[[nodiscard]] size_t Size() const { return _events.size(); }

private:
	struct Repeat
	{
		CameraEventKind kind;
		std::string text;
		double seconds = 0.0; ///< The time when the log told the event last.
		int count = 0;        ///< The events since then that the log only counted.
	};

	Options _options;
	Sink _sink;
	std::deque<CameraEvent> _events;
	std::vector<Repeat> _repeats;
};

/// The line of an event for the log file: `camera: <text>`, with the count of repeats.
[[nodiscard]] std::string FormatEventLine(const CameraEvent& event);
/// A point as `(x, y, z)` with one decimal.
[[nodiscard]] std::string FormatPoint(WorldPoint point);

} // namespace moa_mod::camera
