/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <algorithm>
#include <format>

#include <moa_mod/camera/CameraEvents.h>

namespace moa_mod::camera
{

namespace
{
constexpr std::string_view k_Separator = " | ";
} // namespace

void EventText::Append(std::string_view text)
{
	if (text.empty())
	{
		return;
	}
	auto add = [this](std::string_view part) {
		const auto count = std::min(part.size(), k_Capacity - _size);
		std::copy_n(part.data(), count, _text.data() + _size);
		_size = static_cast<uint8_t>(_size + count);
	};
	if (_size > 0)
	{
		add(k_Separator);
	}
	add(text);
}

std::string_view Name(Gesture gesture)
{
	switch (gesture)
	{
	case Gesture::None:
		return "none";
	case Gesture::Turn:
		return "turn";
	case Gesture::Pitch:
		return "pitch";
	case Gesture::TurnPitch:
		return "turn+pitch";
	case Gesture::Hold:
		return "hold";
	case Gesture::LandDrag:
		return "land drag";
	case Gesture::Glide:
		return "glide";
	case Gesture::Flight:
		return "flight";
	case Gesture::Zoom:
		return "zoom";
	case Gesture::Ground:
		return "ground";
	}
	return "";
}

std::string_view Name(PivotReason reason)
{
	switch (reason)
	{
	case PivotReason::Press:
		return "middle press";
	case PivotReason::Wheel:
		return "wheel";
	case PivotReason::Turn:
		return "key turn";
	case PivotReason::Zoom:
		return "zoom";
	case PivotReason::Pitch:
		return "pitch ease";
	case PivotReason::GroundExit:
		return "ground exit";
	}
	return "";
}

std::string_view Name(CameraEventKind kind)
{
	switch (kind)
	{
	case CameraEventKind::Gesture:
		return "gesture";
	case CameraEventKind::Pivot:
		return "pivot";
	case CameraEventKind::Desired:
		return "desired";
	case CameraEventKind::Arm:
		return "arm";
	case CameraEventKind::CursorLock:
		return "cursor lock";
	case CameraEventKind::StandBack:
		return "stand-back";
	case CameraEventKind::Input:
		return "input";
	case CameraEventKind::Invariant:
		return "invariant";
	case CameraEventKind::BlackBox:
		return "black box";
	}
	return "";
}

std::string_view Name(Invariant invariant)
{
	switch (invariant)
	{
	case Invariant::DesiredChange:
		return "desired change while held";
	case Invariant::CursorLockOff:
		return "cursor lock off while held";
	case Invariant::BelowLand:
		return "camera below the land";
	case Invariant::PivotDrift:
		return "pivot drift";
	case Invariant::CameraJump:
		return "camera jump";
	case Invariant::NotANumber:
		return "not a number";
	}
	return "";
}

CameraEventLog::CameraEventLog()
    : CameraEventLog(Options {})
{
}

CameraEventLog::CameraEventLog(Options options)
    : _options(options)
{
}

bool CameraEventLog::Add(uint64_t frame, double seconds, CameraEventKind kind, std::string text)
{
	auto repeat = std::ranges::find_if(_repeats, [&](const Repeat& r) { return r.kind == kind && r.text == text; });
	int repeats = 0;
	if (repeat != _repeats.end())
	{
		if (seconds - repeat->seconds < _options.repeatSeconds)
		{
			++repeat->count;
			return false;
		}
		repeats = std::exchange(repeat->count, 0);
		repeat->seconds = seconds;
	}
	else
	{
		// Forget the old repeats, so that the list stays short.
		std::erase_if(_repeats, [&](const Repeat& r) { return seconds - r.seconds >= _options.repeatSeconds && r.count == 0; });
		_repeats.push_back({.kind = kind, .text = text, .seconds = seconds, .count = 0});
	}
	CameraEvent event {.frame = frame, .seconds = seconds, .kind = kind, .text = std::move(text), .repeats = repeats};
	if (_sink)
	{
		_sink(event);
	}
	_events.push_back(std::move(event));
	while (_events.size() > _options.capacity)
	{
		_events.pop_front();
	}
	return true;
}

std::vector<CameraEvent> CameraEventLog::Recent(size_t count) const
{
	const auto first = _events.size() > count ? _events.size() - count : 0;
	return {_events.begin() + static_cast<std::ptrdiff_t>(first), _events.end()};
}

nlohmann::json CameraEventLog::RecentJson(size_t count) const
{
	auto result = nlohmann::json::array();
	for (const auto& event : Recent(count))
	{
		result.push_back({{"frame", event.frame},
		                  {"seconds", event.seconds},
		                  {"kind", Name(event.kind)},
		                  {"text", event.text},
		                  {"repeats", event.repeats}});
	}
	return result;
}

std::string FormatEventLine(const CameraEvent& event)
{
	if (event.repeats > 0)
	{
		return std::format("camera: {} ({} more times since the last line)", event.text, event.repeats);
	}
	return std::format("camera: {}", event.text);
}

std::string FormatPoint(WorldPoint point)
{
	return std::format("({:.1f}, {:.1f}, {:.1f})", point.x, point.y, point.z);
}

} // namespace moa_mod::camera
