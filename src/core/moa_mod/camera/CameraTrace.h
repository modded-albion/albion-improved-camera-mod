/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <moa_mod/Folders.h>
#include <moa_mod/camera/CameraLoop.h>

namespace moa_mod::camera
{

/// One frame of the camera trace. A value that the mod did not read in the frame stays empty, and the file has an
/// empty cell for it. The stages of the frame are, in this order: the start of `Before`, the frame of the game, and
/// the end of `After`. The camera at the start of the next `Before` shows what other code did after the mod.
struct CameraTraceRow
{
	uint64_t frame = 0;     ///< The count of frames of the hook.
	double time = 0.0;      ///< Seconds since the mod started.
	float deltaTime = 0.0f; ///< The frame time that the mod used, seconds.
	std::optional<int32_t> unityFrame;

	// The input.
	std::array<bool, 3> buttons {}; ///< Left, right, middle.
	std::optional<WorldPoint> mouse;
	std::optional<WorldPoint> mouseDelta;
	std::optional<WorldPoint> inputPosition; ///< The input position of the game, at the start of the frame.
	std::optional<bool> holdsObject;         ///< The hand holds an object, at the start of the frame.
	std::optional<float> wheel;
	std::optional<WorldPoint> cursorLand; ///< The land under the cursor, for the camera after the frame of the game.

	// The input from each reader, at the start of the frame. `buttons` is the mouse of Unity.
	std::optional<bool> gameLeft;          ///< The left button from the mouse button of the game.
	std::optional<bool> gameRight;         ///< The right button from the mouse button of the game.
	std::optional<bool> gameMiddle;        ///< The middle button from the mouse button of the game.
	std::optional<bool> utilityMiddle;     ///< The middle button from the base input of the UI of the game.
	std::optional<bool> turnInput;         ///< The turn input of the game (the key function of the turn with the mouse).
	std::optional<bool> uiInputActive;     ///< The UI input of the game is active (it stops the land grab).
	std::optional<bool> inputFieldActive;  ///< A text field of the UI has the keyboard.
	std::optional<WorldPoint> rotateMouse; ///< The cursor from which the game measures the turn of the next frame.

	bool standBack = false; ///< The mod did nothing in this frame (an override, a pause, a menu).
	bool active = false;    ///< `After` ran the camera loop in this frame.

	// At the start of `Before`, before the mod changes anything.
	std::optional<CameraPlacement> cameraAtStart;
	std::optional<int32_t> inhibitAtStart;

	// The state that the game gets: after `Before`, before the frame of the game.
	std::optional<WorldPoint> focusBefore;
	std::optional<float> stageBefore;
	std::optional<float> directionBefore;
	std::optional<float> tiltBefore;   ///< The smoothed tilt of the stage.
	std::optional<float> heightBefore; ///< The smoothed height of the stage.
	std::optional<CameraPlacement> cameraBefore;
	std::optional<int32_t> inhibitBefore;
	std::optional<int32_t> ignoreHandBefore;
	std::optional<bool> noHandFocus;
	std::optional<WorldPoint> lastCursorTerrainBefore;
	std::optional<float> dragFadeBefore;

	// After the frame of the game, before the camera loop.
	std::optional<WorldPoint> focusAfterGame;
	std::optional<float> stageAfterGame;
	std::optional<float> directionAfterGame;
	std::optional<float> tiltAfterGame;
	std::optional<float> heightAfterGame;
	std::optional<CameraPlacement> cameraAfterGame;
	std::optional<float> dragFade;
	std::optional<bool> zoomRotateChanging;
	std::optional<bool> holdingZoomRotate;
	std::optional<bool> dragValid;
	std::optional<int32_t> inhibitAfterGame;
	std::optional<WorldPoint> dragGrab;
	std::optional<float> dragStartedTime;
	std::optional<WorldPoint> lastCursorTerrainAfter;
	std::optional<WorldPoint> holdCursor;
	std::optional<float> highestGround; ///< The land height that the game keeps its camera above, at that camera.
	std::optional<float> minimumZoom;   ///< The smallest zoom stage of the game in this frame.
	std::optional<bool> grabbingLand;   ///< The hand of the game shows the land drag.

	// The camera loop and the end of `After`.
	CameraLoopTrace loop;
	std::optional<WorldPoint> focusFinal;
	std::optional<CameraPlacement> cameraFinal; ///< The transform at the end of `After`.
	std::optional<WorldPoint> hand;             ///< The smoothed hand point.
	std::optional<bool> handOverUi;             ///< The cursor is over the UI of the game.
	std::optional<WorldPoint> pivotScreen;      ///< The pivot of the hand rig on the screen, pixels (x, y, depth).
	std::optional<float> landAtFinal;           ///< The exact land under the camera at the end of `After`.
	std::optional<int32_t> fadedObjects;        ///< The count of the objects that the fade shows see-through.
	std::optional<int32_t> hiddenObjects;       ///< The count of the objects that the fade hides.
	std::optional<float> fadeOpacity;           ///< The smallest opacity of the faded objects.
	EventText modEvents;                        ///< The events of the mod in this frame (stand-back, input).
	std::optional<Invariant> invariant;         ///< The first rule of the camera that broke in this frame.
	std::optional<bool> mainIsGameCamera;       ///< `Camera.main` is the camera of the town camera code.
	std::optional<float> mainCameraRoll;        ///< The roll of `Camera.main` at the end of `After`, degrees.

	// Before the cameras render, after the frame and the late update of all scripts.
	int32_t renders = 0;                         ///< The count of renders of the camera of the game.
	std::optional<CameraPlacement> cameraRender; ///< The world pose of the camera of the game at its first render.
	std::optional<float> renderLocalRoll;        ///< The roll of the local rotation of that camera, degrees.
	std::optional<int32_t> renderParents;        ///< The count of the parents of that camera.
	std::optional<float> renderParentRoll;       ///< The roll of the world rotation of the parent, degrees.
	std::optional<float> renderMatrixRoll;       ///< The roll of the view matrix of that camera, degrees.
	int32_t otherRenders = 0;                    ///< The count of renders of other cameras.
	std::optional<float> otherRenderRoll;        ///< The largest roll of the other cameras, degrees.

	// At the start of the next `Before`.
	std::optional<CameraPlacement> cameraNextStart;
};

/// Writes the camera trace as CSV text. It records each frame while the user holds a mouse button, and for a time
/// after the release. It keeps the text in a buffer, and gives the buffer to the sink at
/// least one time in each flush interval, at the end of each recorded gesture, and at `Close`.
///
/// The recorder keeps the row of a frame until the start of the next frame, because the last columns are the camera
/// at that time. It stops at the size limit and writes a line that tells this.
class CameraTraceRecorder
{
public:
	/// Write text to the file. Return false on an error: the recorder then stops.
	using Sink = std::function<bool(std::string_view)>;
	/// Receives each finished row, in each frame, also when the file is off (for the black box).
	using RowDone = std::function<void(CameraTraceRow&)>;

	struct Options
	{
		double tailSeconds = 1.0;     ///< Seconds to record after the release of the buttons.
		double flushSeconds = 1.0;    ///< The longest time that text stays in the buffer.
		size_t maxBytes = 50'000'000; ///< The largest size of the file.
		size_t bufferBytes = 65'536;  ///< The buffer goes to the sink when it has this size.
	};

	/// A recorder with no sink writes no file. It fills the rows only for `RowDone`.
	CameraTraceRecorder(Options options, Sink sink);
	CameraTraceRecorder(const CameraTraceRecorder&) = delete;
	CameraTraceRecorder& operator=(const CameraTraceRecorder&) = delete;
	~CameraTraceRecorder();

	/// Give each finished row to `rowDone` before the file gets it. Then the recorder fills a row in each frame.
	void SetRowDone(RowDone rowDone) { _rowDone = std::move(rowDone); }
	/// Close the file, and write the next rows to `sink`.
	void OpenFile(Sink sink);
	/// Write the header: each line of `comment` with "# " before it, then the names of the columns.
	void WriteHeader(std::string_view comment);

	/// Start a frame. Finish the row of the last recorded frame with the camera at this time. Return true if the mod
	/// must fill the row of this frame.
	bool StartFrame(double seconds, bool buttonHeld, const std::optional<CameraPlacement>& cameraAtStart);
	/// The row of the frame that `StartFrame` started.
	[[nodiscard]] CameraTraceRow& Row() { return _row; }
	/// End a frame. The row stays until the next `StartFrame`.
	void EndFrame(double seconds);
	/// Write the last row (with no next camera) and give all text to the sink. The rows continue to go to
	/// `RowDone`.
	void Close();
	/// Give the text in the buffer to the sink.
	void Flush();

	[[nodiscard]] bool Recording() const { return _recording; }
	/// True while the recorder writes a file.
	[[nodiscard]] bool FileOpen() const { return static_cast<bool>(_sink) && !_closed && !_stopped; }
	[[nodiscard]] bool Stopped() const { return _stopped; }
	[[nodiscard]] uint64_t Rows() const { return _rows; }
	[[nodiscard]] size_t Bytes() const { return _bytes; }

	/// The names of the columns, separated by commas, with no line end.
	[[nodiscard]] static std::string ColumnNames();
	/// The values of a row, separated by commas, with a line end.
	static void FormatRow(const CameraTraceRow& row, std::string& line);

private:
	void Emit();

	Options _options;
	Sink _sink;
	RowDone _rowDone;
	CameraTraceRow _row;
	std::string _line;
	std::string _buffer;
	bool _recording = false;
	bool _pending = false;       ///< The row waits for the camera at the start of the next frame.
	bool _pendingToFile = false; ///< The waiting row goes to the file.
	bool _filling = false;       ///< The mod fills the row of this frame.
	bool _stopped = false;
	bool _closed = false;
	std::optional<double> _lastHeld;
	double _lastFlush = 0.0;
	uint64_t _rows = 0;
	size_t _bytes = 0;
};

/// The name of a trace file: `camera-trace-<date>-<time>-<process>.csv`.
[[nodiscard]] std::string CameraTraceFileName(std::chrono::system_clock::time_point time, uint32_t process);

} // namespace moa_mod::camera
