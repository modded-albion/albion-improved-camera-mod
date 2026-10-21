/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>
#include <cwctype>

#include <algorithm>
#include <format>
#include <iterator>
#include <utility>

#include <core/CameraTrace.h>

namespace moa_mod::camera
{

namespace
{
[[nodiscard]] std::optional<WorldPoint> Difference(const std::optional<WorldPoint>& a, const std::optional<WorldPoint>& b)
{
	if (!a || !b)
	{
		return std::nullopt;
	}
	return *a - *b;
}

[[nodiscard]] std::optional<WorldPoint> PositionOf(const std::optional<CameraPlacement>& camera)
{
	return camera ? std::optional<WorldPoint>(camera->position) : std::nullopt;
}

/// The angle between two rotations, degrees.
[[nodiscard]] std::optional<float> TurnBetween(const std::optional<CameraPlacement>& a, const std::optional<CameraPlacement>& b)
{
	if (!a || !b)
	{
		return std::nullopt;
	}
	const auto dot = std::abs(a->rotation.x * b->rotation.x + a->rotation.y * b->rotation.y + a->rotation.z * b->rotation.z +
	                          a->rotation.w * b->rotation.w);
	return 2.0f * std::acos(std::min(dot, 1.0f)) / k_DegreesToRadians;
}

/// The roll of a camera, degrees.
[[nodiscard]] std::optional<float> RollOf(const std::optional<CameraPlacement>& camera)
{
	return camera ? std::optional<float>(RollDegrees(camera->rotation)) : std::nullopt;
}

/// Give each column of a row to `visit`, as a name and a value. The names and the values come from the same list,
/// so they always agree.
template <typename Visitor>
void VisitColumns(const CameraTraceRow& row, Visitor&& visit)
{
	visit("frame", row.frame);
	visit("unityFrame", row.unityFrame);
	visit("time", row.time);
	visit("dt", row.deltaTime);
	visit("buttonLeft", row.buttons[0]);
	visit("buttonRight", row.buttons[1]);
	visit("buttonMiddle", row.buttons[2]);
	visit("mouse", row.mouse);
	visit("mouseDelta", row.mouseDelta);
	visit("inputPos", row.inputPosition);
	visit("holdsObject", row.holdsObject);
	visit("wheel", row.wheel);
	visit("cursorLand", row.cursorLand);
	visit("gameLeft", row.gameLeft);
	visit("gameRight", row.gameRight);
	visit("gameMiddle", row.gameMiddle);
	visit("utilityMiddle", row.utilityMiddle);
	visit("turnInput", row.turnInput);
	visit("uiInputActive", row.uiInputActive);
	visit("inputFieldActive", row.inputFieldActive);
	visit("rotateMouse", row.rotateMouse);
	visit("standBack", row.standBack);
	visit("active", row.active);

	visit("cameraAtStart", row.cameraAtStart);
	visit("inhibitAtStart", row.inhibitAtStart);

	visit("focusBefore", row.focusBefore);
	visit("stageBefore", row.stageBefore);
	visit("directionBefore", row.directionBefore);
	visit("tiltBefore", row.tiltBefore);
	visit("heightBefore", row.heightBefore);
	visit("cameraBefore", row.cameraBefore);
	visit("inhibitBefore", row.inhibitBefore);
	visit("ignoreHandBefore", row.ignoreHandBefore);
	visit("noHandFocus", row.noHandFocus);
	visit("lastCursorTerrainBefore", row.lastCursorTerrainBefore);
	visit("dragFadeBefore", row.dragFadeBefore);

	visit("focusAfterGame", row.focusAfterGame);
	visit("stageAfterGame", row.stageAfterGame);
	visit("directionAfterGame", row.directionAfterGame);
	visit("tiltAfterGame", row.tiltAfterGame);
	visit("heightAfterGame", row.heightAfterGame);
	visit("cameraAfterGame", row.cameraAfterGame);
	visit("dragFade", row.dragFade);
	visit("zoomRotateChanging", row.zoomRotateChanging);
	visit("holdingZoomRotate", row.holdingZoomRotate);
	visit("dragValid", row.dragValid);
	visit("inhibitAfterGame", row.inhibitAfterGame);
	visit("dragGrab", row.dragGrab);
	visit("dragStartedTime", row.dragStartedTime);
	visit("lastCursorTerrainAfter", row.lastCursorTerrainAfter);
	visit("holdCursor", row.holdCursor);
	visit("highestGround", row.highestGround);
	visit("minimumZoom", row.minimumZoom);
	visit("grabbingLand", row.grabbingLand);

	const auto& loop = row.loop;
	visit("restored", loop.restored);
	visit("movedByOther", loop.movedByOther);
	visit("holdBlocked", loop.holdBlocked);
	visit("writtenStage", loop.writtenStage);
	visit("zoomTarget", loop.zoomTarget);
	visit("glideFocus", loop.glideFocus);
	visit("applied", loop.applied);
	visit("raise", loop.raise);
	visit("raiseTarget", loop.raiseTarget);
	visit("viewTilt", loop.viewTilt);
	visit("appliedLookUp", loop.appliedLookUp);
	visit("clearance", loop.clearance);
	visit("turned", loop.turned);
	visit("turnLocked", loop.turnLocked);
	visit("pivot", loop.pivot);
	visit("cursorJump", loop.cursorJump);
	visit("holdShift", loop.holdShift);
	visit("dragActive", loop.dragActive);
	visit("anchor", loop.anchor);
	visit("viewFocus", loop.viewFocus);
	visit("writtenFocus", loop.writtenFocus);
	visit("writtenCamera", loop.writtenCamera);
	visit("handRig", loop.handRig);
	visit("pivotKept", loop.pivotKept);
	visit("gameShift", loop.gameShift);
	visit("inPlaceTilt", loop.inPlaceTilt);
	visit("zoomFactor", loop.zoomFactor);
	visit("zoomLimited", loop.zoomLimited);
	visit("pivotLimited", loop.pivotLimited);
	visit("cursorLock", loop.cursorLock);
	visit("warpedMouse", loop.warpedMouse);
	visit("cursorLockOff", loop.cursorLockOff);
	visit("modMiddle", loop.modMiddle);
	visit("gesture", loop.gesture);
	visit("pivotReason", loop.pivotReason);
	visit("desiredBefore", loop.desiredBefore);
	visit("armHitName", loop.armHitName);
	visit("events", loop.events);
	visit("desiredDistance", loop.desiredDistance);
	visit("armDistance", loop.armDistance);
	visit("armHit", loop.armHit);
	visit("gamePushIgnored", loop.gamePushIgnored);
	visit("turnRate", loop.turnRate);
	visit("pitchRate", loop.pitchRate);
	visit("desiredChange", loop.desiredChange);
	visit("glideTurn", loop.glideTurn);
	visit("glidePitch", loop.glidePitch);
	visit("zoomHeight", loop.zoomHeight);
	visit("panScale", loop.panScale);
	visit("edgeScrollRemoved", loop.edgeScrollRemoved);
	visit("edgeSoftened", loop.edgeSoftened);
	visit("flight", loop.flight);
	visit("flightStarted", loop.flightStarted);
	visit("flightStopped", loop.flightStopped);
	visit("armLift", loop.armLift);
	visit("landPushes", loop.landPushes);
	visit("armLandSamples", loop.armLandSamples);
	visit("groundBlend", loop.groundBlend);
	visit("eyeHeight", loop.eyeHeight);
	visit("chaseLag", loop.chaseLag);
	visit("chaseSpeed", loop.chaseSpeed);
	visit("gripActive", loop.gripActive);
	visit("gripMove", loop.gripMove);
	visit("gripLimited", loop.gripLimited);
	visit("focusFinal", row.focusFinal);
	visit("cameraFinal", row.cameraFinal);
	visit("hand", row.hand);
	visit("handOverUi", row.handOverUi);
	visit("pivotScreen", row.pivotScreen);
	visit("landAtFinal", row.landAtFinal);
	visit("fadedObjects", row.fadedObjects);
	visit("hiddenObjects", row.hiddenObjects);
	visit("fadeOpacity", row.fadeOpacity);
	visit("modEvents", row.modEvents);
	visit("invariant", row.invariant);
	visit("mainIsGameCamera", row.mainIsGameCamera);
	visit("mainCameraRoll", row.mainCameraRoll);
	visit("renders", row.renders);
	visit("cameraRender", row.cameraRender);
	visit("renderLocalRoll", row.renderLocalRoll);
	visit("renderParents", row.renderParents);
	visit("renderParentRoll", row.renderParentRoll);
	visit("renderMatrixRoll", row.renderMatrixRoll);
	visit("otherRenders", row.otherRenders);
	visit("otherRenderRoll", row.otherRenderRoll);
	visit("cameraNextStart", row.cameraNextStart);

	// The moves of the camera in each stage of the frame, and the parts of the camera of the game that do not come
	// from its focus and its stage (a push, a collision, a hold or a shake).
	std::optional<WorldPoint> placementError;
	if (row.cameraAfterGame && row.focusAfterGame && row.directionAfterGame && row.heightAfterGame && row.tiltAfterGame)
	{
		placementError =
		    row.cameraAfterGame->position -
		    (*row.focusAfterGame + GameCameraOffset(*row.directionAfterGame, *row.heightAfterGame, *row.tiltAfterGame));
	}
	visit("gamePlacementError", placementError);
	std::optional<float> gameTurn;
	if (row.directionAfterGame && row.directionBefore)
	{
		gameTurn = WrapDegrees(*row.directionAfterGame - *row.directionBefore);
	}
	visit("gameTurn", gameTurn);
	visit("focusGameMove", Difference(row.focusAfterGame, row.focusBefore));
	visit("restoreMove", Difference(PositionOf(row.cameraBefore), PositionOf(row.cameraAtStart)));
	visit("gameMove", Difference(PositionOf(row.cameraAfterGame), PositionOf(row.cameraBefore)));
	visit("modMove", Difference(PositionOf(row.cameraFinal), PositionOf(row.cameraAfterGame)));
	visit("afterUsMove", Difference(PositionOf(row.cameraNextStart), PositionOf(row.cameraFinal)));
	visit("afterUsTurnDegrees", TurnBetween(row.cameraNextStart, row.cameraFinal));
	visit("cameraFinalRoll", RollOf(row.cameraFinal));
	visit("cameraRenderRoll", RollOf(row.cameraRender));
	visit("renderMove", Difference(PositionOf(row.cameraRender), PositionOf(row.cameraFinal)));
	visit("renderTurnDegrees", TurnBetween(row.cameraRender, row.cameraFinal));
}

/// Writes the names of the columns. A point or a rotation has one column for each part.
struct NameWriter
{
	std::string& out;

	void Name(std::string_view name, std::string_view suffix = {})
	{
		if (!out.empty())
		{
			out += ',';
		}
		out += name;
		out += suffix;
	}

	template <typename T>
	void operator()(std::string_view name, const std::optional<T>& /*value*/)
	{
		(*this)(name, T {});
	}
	void operator()(std::string_view name, WorldPoint /*value*/)
	{
		Name(name, "X");
		Name(name, "Y");
		Name(name, "Z");
	}
	void operator()(std::string_view name, const CameraPlacement& /*value*/)
	{
		(*this)(std::string(name) + "Pos", WorldPoint {});
		Name(name, "RotX");
		Name(name, "RotY");
		Name(name, "RotZ");
		Name(name, "RotW");
	}
	template <typename T>
	void operator()(std::string_view name, const T& /*value*/)
	{
		Name(name);
	}
};

/// Writes the values of the columns. An empty value writes empty cells.
struct ValueWriter
{
	std::string& out;
	bool first = true;

	void Separator()
	{
		if (!first)
		{
			out += ',';
		}
		first = false;
	}
	void Empty(int count)
	{
		for (int i = 0; i < count; ++i)
		{
			Separator();
		}
	}

	template <typename T>
	void operator()(std::string_view name, const std::optional<T>& value)
	{
		if (value)
		{
			(*this)(name, *value);
		}
		else if constexpr (std::is_same_v<T, WorldPoint>)
		{
			Empty(3);
		}
		else if constexpr (std::is_same_v<T, CameraPlacement>)
		{
			Empty(7);
		}
		else
		{
			Empty(1);
		}
	}
	void operator()(std::string_view /*name*/, WorldPoint value)
	{
		Separator();
		std::format_to(std::back_inserter(out), "{:.4f},{:.4f},{:.4f}", value.x, value.y, value.z);
		first = false;
	}
	void operator()(std::string_view /*name*/, const CameraPlacement& value)
	{
		Separator();
		std::format_to(std::back_inserter(out), "{:.4f},{:.4f},{:.4f},{:.6f},{:.6f},{:.6f},{:.6f}", value.position.x,
		               value.position.y, value.position.z, value.rotation.x, value.rotation.y, value.rotation.z,
		               value.rotation.w);
	}
	void operator()(std::string_view /*name*/, const EventText& value)
	{
		// A text cell is in quotes. A quote in the text is two quotes.
		Separator();
		if (value.Empty())
		{
			return;
		}
		out += '"';
		for (const auto c : value.View())
		{
			out += c;
			if (c == '"')
			{
				out += '"';
			}
		}
		out += '"';
	}
	void operator()(std::string_view /*name*/, Gesture value)
	{
		Separator();
		out += Name(value);
	}
	void operator()(std::string_view /*name*/, PivotReason value)
	{
		Separator();
		out += Name(value);
	}
	void operator()(std::string_view /*name*/, Invariant value)
	{
		Separator();
		out += Name(value);
	}
	void operator()(std::string_view /*name*/, bool value)
	{
		Separator();
		out += value ? '1' : '0';
	}
	void operator()(std::string_view /*name*/, float value)
	{
		Separator();
		std::format_to(std::back_inserter(out), "{:.5f}", value);
	}
	void operator()(std::string_view /*name*/, double value)
	{
		Separator();
		std::format_to(std::back_inserter(out), "{:.5f}", value);
	}
	void operator()(std::string_view /*name*/, int32_t value)
	{
		Separator();
		std::format_to(std::back_inserter(out), "{}", value);
	}
	void operator()(std::string_view /*name*/, uint64_t value)
	{
		Separator();
		std::format_to(std::back_inserter(out), "{}", value);
	}
};

} // namespace

CameraTraceRecorder::CameraTraceRecorder(Options options, Sink sink)
    : _options(options)
    , _sink(std::move(sink))
{
	_buffer.reserve(_options.bufferBytes + 4096);
	_line.reserve(4096);
}

CameraTraceRecorder::~CameraTraceRecorder()
{
	Close();
}

std::string CameraTraceRecorder::ColumnNames()
{
	std::string names;
	VisitColumns(CameraTraceRow {}, NameWriter {names});
	return names;
}

void CameraTraceRecorder::FormatRow(const CameraTraceRow& row, std::string& line)
{
	line.clear();
	VisitColumns(row, ValueWriter {line});
	line += '\n';
}

void CameraTraceRecorder::WriteHeader(std::string_view comment)
{
	if (_stopped)
	{
		return;
	}
	std::string text;
	while (!comment.empty())
	{
		const auto end = comment.find('\n');
		text += "# ";
		text += comment.substr(0, end);
		text += '\n';
		comment = end == std::string_view::npos ? std::string_view {} : comment.substr(end + 1);
	}
	text += ColumnNames();
	text += '\n';
	_bytes += text.size();
	_buffer += text;
	Flush();
}

void CameraTraceRecorder::OpenFile(Sink sink)
{
	Close();
	_sink = std::move(sink);
	_closed = false;
	_stopped = false;
	_recording = false;
	_lastHeld.reset();
	_rows = 0;
	_bytes = 0;
	_buffer.clear();
}

bool CameraTraceRecorder::StartFrame(double seconds, bool buttonHeld, const std::optional<CameraPlacement>& cameraAtStart)
{
	if (_pending)
	{
		_row.cameraNextStart = cameraAtStart;
		if (_rowDone)
		{
			_rowDone(_row);
		}
		if (_pendingToFile)
		{
			Emit();
		}
		_pending = false;
		_pendingToFile = false;
	}
	if (buttonHeld)
	{
		_lastHeld = seconds;
	}
	const auto wasRecording = _recording;
	_recording = _sink && !_stopped && !_closed && _lastHeld && seconds - *_lastHeld <= _options.tailSeconds;
	if (wasRecording && !_recording)
	{
		// The gesture ended. Write it now, so the file has it also if the game stops soon.
		Flush();
	}
	_filling = _recording || static_cast<bool>(_rowDone);
	if (_filling)
	{
		_row = CameraTraceRow {};
		_row.time = seconds;
		_row.cameraAtStart = cameraAtStart;
	}
	return _filling;
}

void CameraTraceRecorder::EndFrame(double seconds)
{
	_pending = _filling;
	_pendingToFile = _recording;
	if (seconds - _lastFlush >= _options.flushSeconds)
	{
		_lastFlush = seconds;
		Flush();
	}
}

void CameraTraceRecorder::Close()
{
	if (_closed)
	{
		return;
	}
	if (_pendingToFile)
	{
		Emit();
		_pendingToFile = false;
	}
	Flush();
	_closed = true;
	_recording = false;
}

void CameraTraceRecorder::Flush()
{
	if (_buffer.empty() || !_sink)
	{
		_buffer.clear();
		return;
	}
	if (!_sink || !_sink(_buffer))
	{
		_stopped = true;
	}
	_buffer.clear();
}

void CameraTraceRecorder::Emit()
{
	if (_stopped)
	{
		return;
	}
	FormatRow(_row, _line);
	if (_bytes + _line.size() > _options.maxBytes)
	{
		const auto note =
		    std::format("# the trace stopped at the size limit of {} bytes after {} rows\n", _options.maxBytes, _rows);
		_buffer += note;
		_bytes += note.size();
		Flush();
		_stopped = true;
		return;
	}
	_buffer += _line;
	_bytes += _line.size();
	++_rows;
	if (_buffer.size() >= _options.bufferBytes)
	{
		Flush();
	}
}

std::string CameraTraceFileName(std::chrono::system_clock::time_point time, uint32_t process)
{
	return std::format("camera-trace-{:%Y%m%d-%H%M%S}-{}.csv", std::chrono::floor<std::chrono::seconds>(time), process);
}

} // namespace moa_mod::camera
