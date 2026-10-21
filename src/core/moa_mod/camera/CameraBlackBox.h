/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <moa_mod/camera/CameraTrace.h>

namespace moa_mod::camera
{

/// A broken rule in a frame.
struct InvariantBreak
{
	Invariant invariant = Invariant::DesiredChange;
	std::string text; ///< What broke, with the values.
};

/// The limits of the rules.
struct InvariantLimits
{
	float belowLandMetres = 0.5f; ///< The camera can be this far below the land height of the game.
	float driftPixels = 8.0f;     ///< The largest move of a kept pivot on the screen in one frame.
	float jumpMetres = 50.0f;     ///< The smallest move in one frame that is a jump.
	float jumpHeights = 1.0f;     ///< A jump is also more than this many heights of the camera above the land.
};

/// Checks the rules of the camera on the rows of the trace, one row after the other.
class InvariantChecker
{
public:
	explicit InvariantChecker(InvariantLimits limits = {})
	    : _limits(limits)
	{
	}

	/// Check a row. Return the first broken rule.
	[[nodiscard]] std::optional<InvariantBreak> Check(const CameraTraceRow& row);

private:
	InvariantLimits _limits;
	bool _middleHeld = false;
	std::optional<WorldPoint> _pivot;
	std::optional<WorldPoint> _pivotScreen;
	std::optional<WorldPoint> _camera;
};

/// The last seconds of the rows of the trace, also when the trace file is off. When a rule of the camera breaks, the
/// black box writes these rows as a trace file, a few times in a session.
class CameraBlackBox
{
public:
	/// Write a file. `name` is a short name of the cause, `text` the trace. Return false on an error.
	using Dump = std::function<bool(std::string_view name, std::string_view text)>;

	struct Options
	{
		double seconds = 10.0;         ///< The time that the black box keeps.
		size_t capacity = 2048;        ///< The largest count of rows.
		int maxDumps = 3;              ///< The largest count of files in a session.
		double cooldownSeconds = 30.0; ///< The shortest time between two files.
		InvariantLimits limits;
	};

	CameraBlackBox(Options options, Dump dump);

	/// Write this text at the start of each file, after "# ".
	void SetHeader(std::string header) { _header = std::move(header); }
	/// Check a finished row and keep it. The row gets the broken rule. If a rule broke, write the file if the limits
	/// permit it. Return the broken rule.
	std::optional<InvariantBreak> Add(CameraTraceRow& row);
	/// The rows, the oldest first.
	[[nodiscard]] size_t Size() const { return _size; }
	[[nodiscard]] const CameraTraceRow& At(size_t index) const;
	[[nodiscard]] int Dumps() const { return _dumps; }
	/// The text of a file with the rows that the black box keeps now.
	[[nodiscard]] std::string Text(std::string_view cause) const;

private:
	Options _options;
	Dump _dump;
	InvariantChecker _checker;
	std::vector<CameraTraceRow> _rows;
	size_t _first = 0;
	size_t _size = 0;
	int _dumps = 0;
	std::optional<double> _lastDump;
	std::string _header;
};

} // namespace moa_mod::camera
