/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <algorithm>
#include <format>

#include <moa_mod/camera/CameraBlackBox.h>

namespace moa_mod::camera
{

namespace
{

[[nodiscard]] bool IsNumber(WorldPoint point)
{
	return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

[[nodiscard]] bool IsNumber(const CameraPlacement& placement)
{
	return IsNumber(placement.position) && std::isfinite(placement.rotation.x) && std::isfinite(placement.rotation.y) &&
	       std::isfinite(placement.rotation.z) && std::isfinite(placement.rotation.w);
}

[[nodiscard]] float ScreenDistance(WorldPoint a, WorldPoint b)
{
	return std::hypot(a.x - b.x, a.y - b.y);
}

} // namespace

std::optional<InvariantBreak> InvariantChecker::Check(const CameraTraceRow& row)
{
	const auto& loop = row.loop;
	const auto middle = loop.modMiddle.value_or(false);
	const auto wasHeld = std::exchange(_middleHeld, middle);
	const auto lastPivot = std::exchange(_pivot, loop.pivot);
	const auto lastScreen = std::exchange(_pivotScreen, row.pivotScreen);
	const auto lastCamera = row.cameraFinal ? std::exchange(_camera, row.cameraFinal->position) : _camera;
	if (row.standBack || !row.active)
	{
		_camera.reset();
		return std::nullopt;
	}

	std::optional<InvariantBreak> result;
	auto broke = [&result](Invariant invariant, std::string text) {
		if (!result)
		{
			result = InvariantBreak {.invariant = invariant, .text = std::move(text)};
		}
	};

	if (row.cameraFinal && !IsNumber(*row.cameraFinal))
	{
		broke(Invariant::NotANumber, "the camera at the end of the frame is not a number");
	}
	for (const auto value : {loop.desiredDistance, loop.armDistance, std::optional<float>(loop.raise)})
	{
		if (value && !std::isfinite(*value))
		{
			broke(Invariant::NotANumber, "a distance or the raise of the hand rig is not a number");
		}
	}
	// A press takes a new pivot. Later in the hold, only a zoom may change the desired distance.
	if (middle && wasHeld && (loop.desiredChange == 2 || loop.desiredChange == 4 || loop.desiredChange == 5))
	{
		broke(Invariant::DesiredChange,
		      std::format("the desired distance changed from {:.2f} to {:.2f} m (cause {}) while the middle button is held",
		                  loop.desiredBefore.value_or(0.0f), loop.desiredDistance.value_or(0.0f), loop.desiredChange));
	}
	if (middle && loop.cursorLockOff)
	{
		broke(Invariant::CursorLockOff, std::format("the middle button is held, but the cursor is free: {}",
		                                            Describe(static_cast<CursorLockOff>(*loop.cursorLockOff))));
	}
	if (row.cameraFinal && row.landAtFinal && row.cameraFinal->position.y < *row.landAtFinal - _limits.belowLandMetres)
	{
		broke(Invariant::BelowLand, std::format("the camera is {:.2f} m below the land height of the game",
		                                        *row.landAtFinal - row.cameraFinal->position.y));
	}
	if (loop.pivotKept && loop.pivot && lastPivot && row.pivotScreen && lastScreen && Length(loop.gameShift) < 1e-6f &&
	    Length(*loop.pivot - *lastPivot) < 1e-4f)
	{
		const auto drift = ScreenDistance(*row.pivotScreen, *lastScreen);
		if (drift > _limits.driftPixels)
		{
			broke(Invariant::PivotDrift, std::format("the kept pivot moved {:.1f} px on the screen", drift));
		}
	}
	if (row.cameraFinal && lastCamera && !loop.flight)
	{
		const auto move = Length(row.cameraFinal->position - *lastCamera);
		const auto height = row.landAtFinal ? std::max(row.cameraFinal->position.y - *row.landAtFinal, 0.0f) : 0.0f;
		const auto limit = std::max(_limits.jumpMetres, _limits.jumpHeights * height);
		if (move > limit)
		{
			broke(Invariant::CameraJump, std::format("the camera moved {:.1f} m in one frame (limit {:.1f} m)", move, limit));
		}
	}
	return result;
}

CameraBlackBox::CameraBlackBox(Options options, Dump dump)
    : _options(options)
    , _dump(std::move(dump))
    , _checker(options.limits)
    , _rows(std::max<size_t>(options.capacity, 1))
{
}

const CameraTraceRow& CameraBlackBox::At(size_t index) const
{
	return _rows[(_first + index) % _rows.size()];
}

std::optional<InvariantBreak> CameraBlackBox::Add(CameraTraceRow& row)
{
	auto broken = _checker.Check(row);
	if (broken)
	{
		row.invariant = broken->invariant;
	}
	// Keep the row. Forget the rows that are older than the time of the black box, or that do not fit.
	if (_size == _rows.size())
	{
		_first = (_first + 1) % _rows.size();
		--_size;
	}
	_rows[(_first + _size) % _rows.size()] = row;
	++_size;
	while (_size > 1 && row.time - At(0).time > _options.seconds)
	{
		_first = (_first + 1) % _rows.size();
		--_size;
	}
	if (broken && _dump && _dumps < _options.maxDumps && (!_lastDump || row.time - *_lastDump >= _options.cooldownSeconds))
	{
		_lastDump = row.time;
		++_dumps;
		const auto cause = std::format("{}: {}", Name(broken->invariant), broken->text);
		static_cast<void>(_dump(Name(broken->invariant), Text(cause)));
	}
	return broken;
}

std::string CameraBlackBox::Text(std::string_view cause) const
{
	std::string text;
	text.reserve(_size * 2048 + 4096);
	if (!_header.empty())
	{
		text += "# ";
		text += _header;
		text += '\n';
	}
	text += std::format("# camera black box: {}\n", cause);
	text += CameraTraceRecorder::ColumnNames();
	text += '\n';
	std::string line;
	for (size_t index = 0; index < _size; ++index)
	{
		CameraTraceRecorder::FormatRow(At(index), line);
		text += line;
	}
	return text;
}

} // namespace moa_mod::camera
