/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <moa_mod/UnityMath.h>
#include <moa_mod/autotest/Scenario.h>

namespace moa_mod::autotest
{

/// The value of a name of the Unity `KeyCode` enum, for the keys that the scenarios use: letters, digits (`Alpha0`),
/// `F1` to `F15`, the arrows, the modifiers, `Space`, `Escape`, `Return`, `Tab` and `Mouse0` to `Mouse6`. Nothing for
/// another name. The values of this enum do not change between versions of Unity.
[[nodiscard]] std::optional<int32_t> KeyCodeOf(std::string_view name);

/// The first value of the mouse buttons in the Unity `KeyCode` enum (`Mouse0`).
constexpr int32_t k_KeyCodeMouse0 = 323;

/// One change of the input at a time of the steps.
struct InputEvent
{
	enum class Type : uint8_t
	{
		MoveTo,     ///< The mouse goes to a point of the screen.
		ButtonDown, ///< A mouse button goes down.
		ButtonUp,   ///< A mouse button goes up.
		KeyDown,    ///< A key goes down.
		KeyUp,      ///< A key goes up.
		Wheel,      ///< The wheel turns by one notch (`notches` is 1 or -1).
	};
	double time = 0.0;
	Type type = Type::MoveTo;
	Button button = Button::Left;
	ScreenFraction at;
	int32_t key = 0;
	int notches = 0;

	[[nodiscard]] static InputEvent MoveTo(double time, ScreenFraction at)
	{
		return {.time = time, .type = Type::MoveTo, .button = Button::Left, .at = at, .key = 0, .notches = 0};
	}
	[[nodiscard]] static InputEvent OfButton(double time, Type type, Button button)
	{
		return {.time = time, .type = type, .button = button, .at = {}, .key = 0, .notches = 0};
	}
	[[nodiscard]] static InputEvent OfKey(double time, Type type, int32_t key)
	{
		return {.time = time, .type = type, .button = Button::Left, .at = {}, .key = key, .notches = 0};
	}
	[[nodiscard]] static InputEvent WheelNotch(double time, int notches)
	{
		return {.time = time, .type = Type::Wheel, .button = Button::Left, .at = {}, .key = 0, .notches = notches};
	}
};

/// The input of the steps in a part of the time: the changes, and the move of the mouse.
struct InputWindow
{
	std::vector<InputEvent> events; ///< In the order of their times.
	float right = 0.0f;             ///< The move of the mouse to the right, pixels.
	float up = 0.0f;                ///< The move of the mouse up, pixels.
};

/// The start of the first window of the steps: a time just before the time zero.
constexpr double k_BeforeSteps = -1e-6;

/// The input of the steps of a scenario as a function of the time from the start of the steps.
class InputTimeline
{
public:
	InputTimeline() = default;
	/// Make the timeline. A key or a hotkey that is not known gives an error.
	[[nodiscard]] static moa::Result<InputTimeline> Make(std::span<const Step> steps);

	/// The input after the time `from` and up to the time `to`, seconds from the start of the steps. The first window
	/// starts at `k_BeforeSteps`, so that it includes the events at the time zero.
	[[nodiscard]] InputWindow Window(double from, double to) const;
	/// The time of the end of the last step, seconds.
	[[nodiscard]] double Duration() const { return _duration; }

private:
	struct Segment
	{
		double start = 0.0;
		double end = 0.0;
		float right = 0.0f;
		float up = 0.0f;
	};

	std::vector<InputEvent> _events;
	std::vector<Segment> _moves;
	double _duration = 0.0;
};

/// The mouse and the keys that the game reads while a scenario runs, in place of the real mouse and keyboard. The
/// state changes one time in each frame of the game (`BeginFrame`), as the input of Unity does. A press and a release
/// in the same frame give the press in that frame and the release in the next frame, so that the game sees both.
class VirtualInput
{
public:
	/// Start the virtual input with no button and no key held, and the mouse at `mouse` (pixels, from the bottom left).
	void Start(Vector2 screen, WorldPoint mouse);
	/// Stop the virtual input. The game reads the real input again.
	void Stop();
	[[nodiscard]] bool Active() const { return _active; }

	/// Start a new frame with the input of a window of the steps.
	void BeginFrame(const InputWindow& window, Vector2 screen);
	/// Start a new frame with no new input.
	void BeginFrame(Vector2 screen) { BeginFrame(InputWindow {}, screen); }
	/// The count of the frames since the start.
	[[nodiscard]] uint64_t Frames() const { return _frames; }

	[[nodiscard]] bool Button(int32_t button) const;
	[[nodiscard]] bool ButtonDown(int32_t button) const;
	[[nodiscard]] bool ButtonUp(int32_t button) const;
	/// A key of the Unity `KeyCode` enum. `Mouse0` to `Mouse2` are the mouse buttons.
	[[nodiscard]] bool Key(int32_t key) const;
	[[nodiscard]] bool KeyDown(int32_t key) const;
	[[nodiscard]] bool KeyUp(int32_t key) const;
	[[nodiscard]] bool AnyKey() const;
	[[nodiscard]] bool AnyKeyDown() const;
	/// The keys that are held.
	[[nodiscard]] std::vector<int32_t> HeldKeys() const;

	/// The mouse position, pixels from the bottom left, as `Input.mousePosition`.
	[[nodiscard]] WorldPoint Mouse() const { return _mouse; }
	/// The move of the mouse since the last frame, pixels.
	[[nodiscard]] WorldPoint MouseDelta() const { return _delta; }
	/// The wheel of this frame, as `Input.GetAxis("Mouse ScrollWheel")`: 0.1 for each notch, more than zero is up.
	[[nodiscard]] float WheelAxis() const;
	/// The notches of the wheel in this frame.
	[[nodiscard]] int WheelNotches() const { return _notches; }
	/// The value of an axis of the input manager of Unity in this frame. Only the axes of the mouse have a value.
	[[nodiscard]] float Axis(std::string_view name) const;

	/// Move the mouse to a point of the screen now, as the cursor lock of the camera mod does with the real cursor.
	/// Return the new mouse position.
	WorldPoint Warp(WorldPoint mouse);

private:
	struct Edge
	{
		bool held = false;
		bool down = false;
		bool up = false;
		bool releaseNext = false;
	};
	struct KeyState
	{
		int32_t key = 0;
		Edge edge;
	};

	static void Press(Edge& edge);
	static void Release(Edge& edge);
	static void StartFrame(Edge& edge);
	[[nodiscard]] Edge& KeyEdge(int32_t key);
	[[nodiscard]] const Edge* FindKey(int32_t key) const;
	void Clamp(Vector2 screen);

	bool _active = false;
	uint64_t _frames = 0;
	std::array<Edge, k_ButtonCount> _buttons {};
	std::vector<KeyState> _keys;
	WorldPoint _mouse;
	WorldPoint _lastMouse;
	WorldPoint _delta;
	int _notches = 0;
};

} // namespace moa_mod::autotest
