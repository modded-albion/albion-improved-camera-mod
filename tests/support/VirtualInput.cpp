/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <algorithm>
#include <utility>

#include <mod_core/Hotkey.h>
#include <support/VirtualInput.h>

namespace moa_mod::autotest
{

namespace
{
constexpr float k_WheelAxisPerNotch = 0.1f;
/// The axes "Mouse X" and "Mouse Y" of the default input manager of Unity give 0.1 for each pixel.
constexpr float k_MouseAxisPerPixel = 0.1f;

constexpr std::array<std::pair<std::string_view, int32_t>, 24> k_NamedKeys {{
    {"Backspace", 8},    {"Tab", 9},         {"Return", 13},        {"Escape", 27},       {"Space", 32},
    {"Delete", 127},     {"UpArrow", 273},   {"DownArrow", 274},    {"RightArrow", 275},  {"LeftArrow", 276},
    {"Insert", 277},     {"Home", 278},      {"End", 279},          {"PageUp", 280},      {"PageDown", 281},
    {"RightShift", 303}, {"LeftShift", 304}, {"RightControl", 305}, {"LeftControl", 306}, {"RightAlt", 307},
    {"LeftAlt", 308},    {"Period", 46},     {"Comma", 44},         {"Minus", 45},
}};
} // namespace

std::optional<int32_t> KeyCodeOf(std::string_view name)
{
	if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z')
	{
		return static_cast<int32_t>(name[0] - 'A' + 'a');
	}
	if (name.size() == 6 && name.starts_with("Alpha") && name[5] >= '0' && name[5] <= '9')
	{
		return static_cast<int32_t>(name[5]);
	}
	if (name.size() == 7 && name.starts_with("Keypad") && name[6] >= '0' && name[6] <= '9')
	{
		return 256 + (name[6] - '0');
	}
	if (name.size() == 6 && name.starts_with("Mouse") && name[5] >= '0' && name[5] <= '6')
	{
		return k_KeyCodeMouse0 + (name[5] - '0');
	}
	if (name.size() >= 2 && name.size() <= 3 && name[0] == 'F')
	{
		int number = 0;
		for (const auto c : name.substr(1))
		{
			if (c < '0' || c > '9')
			{
				return std::nullopt;
			}
			number = number * 10 + (c - '0');
		}
		if (number >= 1 && number <= 15)
		{
			return 281 + number;
		}
		return std::nullopt;
	}
	for (const auto& [key, value] : k_NamedKeys)
	{
		if (key == name)
		{
			return value;
		}
	}
	return std::nullopt;
}

moa::Result<InputTimeline> InputTimeline::Make(std::span<const Step> steps)
{
	InputTimeline timeline;
	double time = 0.0;
	const auto add = [&](InputEvent event) { timeline._events.push_back(event); };
	const auto moveTo = [&](const Step& step) {
		if (step.at)
		{
			add(InputEvent::MoveTo(time, *step.at));
		}
	};
	const auto click = [&](Button button, double start) {
		add(InputEvent::OfButton(start, InputEvent::Type::ButtonDown, button));
		add(InputEvent::OfButton(start + static_cast<double>(k_ClickSeconds), InputEvent::Type::ButtonUp, button));
	};
	const auto keyCodes = [](const std::vector<std::string>& names) -> moa::Result<std::vector<int32_t>> {
		std::vector<int32_t> codes;
		for (const auto& name : names)
		{
			const auto code = KeyCodeOf(name);
			if (!code)
			{
				return moa::MakeError("the key '{}' is not a known name of the Unity KeyCode enum", name);
			}
			codes.push_back(*code);
		}
		return codes;
	};
	for (const auto& step : steps)
	{
		const auto duration = static_cast<double>(step.Duration());
		switch (step.kind)
		{
		case Step::Kind::Press:
			moveTo(step);
			add(InputEvent::OfButton(time, InputEvent::Type::ButtonDown, step.button));
			break;
		case Step::Kind::Release:
			moveTo(step);
			add(InputEvent::OfButton(time, InputEvent::Type::ButtonUp, step.button));
			break;
		case Step::Kind::Move:
			moveTo(step);
			timeline._moves.push_back({.start = time, .end = time + duration, .right = step.right, .up = step.up});
			break;
		case Step::Kind::Drag:
			moveTo(step);
			add(InputEvent::OfButton(time, InputEvent::Type::ButtonDown, step.button));
			timeline._moves.push_back({.start = time, .end = time + duration, .right = step.right, .up = step.up});
			if (!step.keepHeld)
			{
				add(InputEvent::OfButton(time + duration, InputEvent::Type::ButtonUp, step.button));
			}
			break;
		case Step::Kind::Wheel:
		{
			const auto sign = step.notches > 0 ? 1 : -1;
			for (int notch = 0; notch < std::abs(step.notches); ++notch)
			{
				add(InputEvent::WheelNotch(time + notch * static_cast<double>(step.interval), sign));
			}
			break;
		}
		case Step::Kind::Keys:
		{
			auto codes = keyCodes(step.keys);
			if (!codes)
			{
				return codes.GetError();
			}
			for (const auto code : *codes)
			{
				add(InputEvent::OfKey(time, InputEvent::Type::KeyDown, code));
				add(InputEvent::OfKey(time + duration, InputEvent::Type::KeyUp, code));
			}
			break;
		}
		case Step::Kind::Hotkey:
		{
			auto hotkey = ParseHotkey(step.hotkey);
			if (!hotkey || !hotkey->Enabled())
			{
				return moa::MakeError("the hotkey '{}' is not correct", step.hotkey);
			}
			std::vector<std::string> names;
			for (const auto& [held, name] : {std::pair {hotkey->control, "LeftControl"}, std::pair {hotkey->alt, "LeftAlt"},
			                                 std::pair {hotkey->shift, "LeftShift"}})
			{
				if (held)
				{
					names.emplace_back(name);
				}
			}
			names.push_back(hotkey->key);
			auto codes = keyCodes(names);
			if (!codes)
			{
				return codes.GetError();
			}
			for (const auto code : *codes)
			{
				add(InputEvent::OfKey(time, InputEvent::Type::KeyDown, code));
				add(InputEvent::OfKey(time + static_cast<double>(k_HotkeySeconds), InputEvent::Type::KeyUp, code));
			}
			break;
		}
		case Step::Kind::Click:
			moveTo(step);
			click(step.button, time);
			break;
		case Step::Kind::DoubleClick:
			moveTo(step);
			click(step.button, time);
			click(step.button, time + static_cast<double>(k_ClickSeconds + k_ClickGapSeconds));
			break;
		case Step::Kind::Wait:
		case Step::Kind::Blend:
			// No input. The runner asks the host for the blend.
			break;
		}
		time += duration;
	}
	timeline._duration = time;
	// Keep the order of the steps for the events at the same time.
	std::ranges::stable_sort(timeline._events, {}, &InputEvent::time);
	return timeline;
}

InputWindow InputTimeline::Window(double from, double to) const
{
	InputWindow window;
	// The first window starts before the time zero, so that it includes the events at the time zero.
	const auto inside = [&](double time) { return time > from && time <= to; };
	for (const auto& event : _events)
	{
		if (inside(event.time))
		{
			window.events.push_back(event);
		}
	}
	for (const auto& move : _moves)
	{
		const auto length = move.end - move.start;
		if (length <= 0.0)
		{
			// A move with no time is a jump of the mouse at its start.
			if (inside(move.start))
			{
				window.right += move.right;
				window.up += move.up;
			}
			continue;
		}
		const auto start = std::max(from, move.start);
		const auto end = std::min(to, move.end);
		if (end > start)
		{
			const auto part = static_cast<float>((end - start) / length);
			window.right += move.right * part;
			window.up += move.up * part;
		}
	}
	return window;
}

void VirtualInput::Start(Vector2 screen, WorldPoint mouse)
{
	_active = true;
	_frames = 0;
	_buttons = {};
	_keys.clear();
	_mouse = mouse;
	Clamp(screen);
	_lastMouse = _mouse;
	_delta = {};
	_notches = 0;
}

void VirtualInput::Stop()
{
	_active = false;
}

void VirtualInput::Press(Edge& edge)
{
	if (edge.held)
	{
		edge.releaseNext = false;
		return;
	}
	edge.held = true;
	edge.down = true;
}

void VirtualInput::Release(Edge& edge)
{
	if (!edge.held)
	{
		return;
	}
	if (edge.down)
	{
		// A press in this frame: the game sees the release in the next frame.
		edge.releaseNext = true;
		return;
	}
	edge.held = false;
	edge.up = true;
}

void VirtualInput::StartFrame(Edge& edge)
{
	edge.down = false;
	edge.up = false;
	if (std::exchange(edge.releaseNext, false))
	{
		edge.held = false;
		edge.up = true;
	}
}

VirtualInput::Edge& VirtualInput::KeyEdge(int32_t key)
{
	const auto found = std::ranges::find(_keys, key, &KeyState::key);
	if (found != _keys.end())
	{
		return found->edge;
	}
	return _keys.emplace_back(KeyState {.key = key, .edge = {}}).edge;
}

const VirtualInput::Edge* VirtualInput::FindKey(int32_t key) const
{
	if (key >= k_KeyCodeMouse0 && key < k_KeyCodeMouse0 + static_cast<int32_t>(k_ButtonCount))
	{
		return &_buttons[static_cast<size_t>(key - k_KeyCodeMouse0)];
	}
	const auto found = std::ranges::find(_keys, key, &KeyState::key);
	return found != _keys.end() ? &found->edge : nullptr;
}

void VirtualInput::Clamp(Vector2 screen)
{
	_mouse.x = std::clamp(_mouse.x, 0.0f, std::max(screen.x - 1.0f, 0.0f));
	_mouse.y = std::clamp(_mouse.y, 0.0f, std::max(screen.y - 1.0f, 0.0f));
	_mouse.z = 0.0f;
}

void VirtualInput::BeginFrame(const InputWindow& window, Vector2 screen)
{
	++_frames;
	for (auto& edge : _buttons)
	{
		StartFrame(edge);
	}
	for (auto& key : _keys)
	{
		StartFrame(key.edge);
	}
	_notches = 0;
	for (const auto& event : window.events)
	{
		switch (event.type)
		{
		case InputEvent::Type::MoveTo:
			_mouse = {event.at.x * screen.x, (1.0f - event.at.y) * screen.y, 0.0f};
			break;
		case InputEvent::Type::ButtonDown:
			Press(_buttons[static_cast<size_t>(event.button)]);
			break;
		case InputEvent::Type::ButtonUp:
			Release(_buttons[static_cast<size_t>(event.button)]);
			break;
		case InputEvent::Type::KeyDown:
			Press(KeyEdge(event.key));
			break;
		case InputEvent::Type::KeyUp:
			Release(KeyEdge(event.key));
			break;
		case InputEvent::Type::Wheel:
			_notches += event.notches;
			break;
		}
	}
	_mouse.x += window.right;
	_mouse.y += window.up;
	Clamp(screen);
	_delta = _mouse - _lastMouse;
	_lastMouse = _mouse;
}

WorldPoint VirtualInput::Warp(WorldPoint mouse)
{
	_mouse = {mouse.x, mouse.y, 0.0f};
	// The move of the next frame starts from the new place, as for the real cursor.
	_lastMouse = _mouse;
	return _mouse;
}

bool VirtualInput::Button(int32_t button) const
{
	return button >= 0 && button < static_cast<int32_t>(k_ButtonCount) && _buttons[static_cast<size_t>(button)].held;
}

bool VirtualInput::ButtonDown(int32_t button) const
{
	return button >= 0 && button < static_cast<int32_t>(k_ButtonCount) && _buttons[static_cast<size_t>(button)].down;
}

bool VirtualInput::ButtonUp(int32_t button) const
{
	return button >= 0 && button < static_cast<int32_t>(k_ButtonCount) && _buttons[static_cast<size_t>(button)].up;
}

bool VirtualInput::Key(int32_t key) const
{
	const auto* edge = FindKey(key);
	return edge != nullptr && edge->held;
}

bool VirtualInput::KeyDown(int32_t key) const
{
	const auto* edge = FindKey(key);
	return edge != nullptr && edge->down;
}

bool VirtualInput::KeyUp(int32_t key) const
{
	const auto* edge = FindKey(key);
	return edge != nullptr && edge->up;
}

bool VirtualInput::AnyKey() const
{
	return std::ranges::any_of(_buttons, &Edge::held) ||
	       std::ranges::any_of(_keys, [](const KeyState& key) { return key.edge.held; });
}

bool VirtualInput::AnyKeyDown() const
{
	return std::ranges::any_of(_buttons, &Edge::down) ||
	       std::ranges::any_of(_keys, [](const KeyState& key) { return key.edge.down; });
}

std::vector<int32_t> VirtualInput::HeldKeys() const
{
	std::vector<int32_t> held;
	for (const auto& key : _keys)
	{
		if (key.edge.held)
		{
			held.push_back(key.key);
		}
	}
	return held;
}

float VirtualInput::WheelAxis() const
{
	return static_cast<float>(_notches) * k_WheelAxisPerNotch;
}

float VirtualInput::Axis(std::string_view name) const
{
	if (name == "Mouse ScrollWheel")
	{
		return WheelAxis();
	}
	if (name == "Mouse X")
	{
		return _delta.x * k_MouseAxisPerPixel;
	}
	if (name == "Mouse Y")
	{
		return _delta.y * k_MouseAxisPerPixel;
	}
	return 0.0f;
}

} // namespace moa_mod::autotest
