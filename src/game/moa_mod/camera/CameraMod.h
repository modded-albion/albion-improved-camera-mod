/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <moa/common/Result.h>
#include <moa_mod/camera/CameraBlackBox.h>
#include <moa_mod/camera/CameraEvents.h>
#include <moa_mod/camera/CameraFade.h>
#include <moa_mod/camera/CameraLoop.h>
#include <moa_mod/camera/CameraSettings.h>
#include <moa_mod/camera/CameraTrace.h>
#include <nlohmann/json.hpp>

#include "moa_mod/Managed.h"
#include "moa_mod/ModStatus.h"

namespace moa_mod::camera
{

class SystemCursor;
class GameFadeScene;

/// A test of the camera controls that runs around the frames of the camera mod (`autotest::AutotestMod`).
class CameraFrameObserver
{
public:
	virtual ~CameraFrameObserver() = default;
	/// Before the camera mod starts its frame. `game` is null when the game has no camera state.
	virtual void BeforeFrame(CameraGame* game) = 0;
	/// After the camera mod made the camera of the frame. `standBack` is the reason when the mod did not change the
	/// frame. `game` is null when the game has no camera state.
	virtual void AfterFrame(CameraGame* game, const std::optional<std::string>& standBack) = 0;
	/// An event of the camera mod.
	virtual void OnEvent(CameraEventKind kind, const std::string& text) = 0;
	/// True while the observer gives the mouse to the game. Then the cursor lock moves the mouse of the observer, not the
	/// cursor of the system.
	[[nodiscard]] virtual bool OwnsCursor() const = 0;
	/// Move the mouse of the observer. Return the new mouse position.
	[[nodiscard]] virtual WorldPoint Warp(WorldPoint mouse) = 0;
};

/// The names of the game and of Unity that the camera mod and the query `camera.state` use. `Bind` finds all of
/// them, or returns an error that names each missing one.
struct CameraBindings
{
	// GameManager
	size_t state = 0;
	size_t dataReady = 0;
	size_t camera = 0;
	size_t minimumZoom = 0;
	size_t clampMaxZoom = 0;
	size_t forceZoomOut = 0;
	size_t freeCamera = 0;
	size_t panSequence = 0;
	size_t trackingFocus = 0;
	size_t overrideTransform = 0;
	size_t transition = 0;
	size_t inputLocked = 0;
	size_t blendFromT = 0;
	size_t clampForwardSize = 0;
	size_t lastTilt = 0;
	size_t lastHeight = 0;
	size_t ignoreCollisions = 0;
	size_t dragFade = 0;
	size_t zoomRotateChanging = 0;
	size_t holdingZoomRotate = 0;
	size_t dragValid = 0;
	size_t dragStartedTime = 0;
	size_t noHandFocus = 0;
	size_t stageIgnoreHand = 0;
	size_t inhibitHandFocus = 0;
	size_t dragGrab = 0;
	size_t smoothMouseRaycast = 0;
	size_t lastCursorTerrain = 0;
	size_t subSceneFloor = 0;
	size_t smoothMouseVisual = 0;
	size_t turnCursor = 0;
	size_t moveSpeed = 0;
	size_t dollySmooth = 0;
	size_t inputWasConsumed = 0;
	size_t inputWasDragging = 0;
	size_t lightsAndCameras = 0;
	size_t edgeScrollSpeed = 0;
	/// The zoom stage of the double click of the game on the land. Zero if the game does not have it: then the double
	/// click zooms as in the game.
	size_t zoomToStage = 0;
	/// The save slot of the game, for the key of the camera spots. Zero if the game does not have it.
	size_t saveSlot = 0;
	/// The box that the game keeps its focus in. Null if the game does not have it: then the map edge is hard.
	const MethodInfo* terrainMinimum = nullptr;
	const MethodInfo* terrainMaximum = nullptr;
	/// The mouse button of the game (the mouse, Steam Input and the touch screen). Null if the game does not have it:
	/// then the mod reads the mouse of Unity.
	const MethodInfo* gameMouseButton = nullptr;
	/// The test of the game for a held input of a key function. The mod reads the turn of the camera with the mouse
	/// with it: the game turns the camera while that input is held. Null if the game does not have it: then the mod
	/// reads the middle button of Unity.
	const MethodInfo* inputHeld = nullptr;
	/// Readers only for the trace and the events. Null if the game does not have them.
	const MethodInfo* utilityMouseButton = nullptr; ///< The mouse button of the base input of the UI of the game.
	const MethodInfo* uiInputActive = nullptr;      ///< The UI input of the game is active.
	const MethodInfo* findObject = nullptr;         ///< The object of an identifier, for the name of a collider.
	const MethodInfo* objectName = nullptr;         ///< The name of an object.
	/// The static smallest zoom stage while the hand holds an object. Null if the game does not have it.
	FieldInfo* heldObjectMinimumStage = nullptr;
	const MethodInfo* raycastTerrain = nullptr;
	const MethodInfo* raycastSubScene = nullptr;
	const MethodInfo* clampFocus = nullptr;
	const MethodInfo* highestGround = nullptr;
	const MethodInfo* cameraGroundHeight = nullptr; ///< Optional: the exact land of the camera at one point.
	const MethodInfo* blendCameraTo = nullptr;      ///< Optional: the blend of the camera to a view, for the tests.
	/// The sphere test of the collision test of the camera of the game. Null if the game does not have it: then the
	/// spring arm of the hand rig uses only the land.
	const MethodInfo* filteredSphereCast = nullptr;
	const MethodInfo* isOverridden = nullptr;
	const MethodInfo* isClamped = nullptr;
	const MethodInfo* isDesignTable = nullptr;
	const MethodInfo* isInterior = nullptr;
	const MethodInfo* isDesignTransition = nullptr;
	const MethodInfo* isTitleScreen = nullptr;
	const MethodInfo* isPossessing = nullptr;
	const MethodInfo* isCavelike = nullptr;
	// GameState
	size_t focus = 0;
	size_t stage = 0;
	size_t direction = 0;
	// PlayerHandManager
	size_t handPosition = 0;
	size_t handOverUi = 0;
	size_t handVisible = 0;
	const MethodInfo* setMouseOverride = nullptr;
	// Other classes of the game
	const MethodInfo* isPaused = nullptr;
	const MethodInfo* inputFieldActive = nullptr;
	const MethodInfo* getIgnoreWheel = nullptr;
	const MethodInfo* setIgnoreWheel = nullptr;
	const MethodInfo* stageCount = nullptr;
	const MethodInfo* stageRow = nullptr;
	size_t rowHeight = 0;
	size_t rowTilt = 0;
	// NGManager
	size_t stageTightness = 0;
	size_t focusHeightTightness = 0;
	const MethodInfo* inputPosition = nullptr;
	// Unity
	const MethodInfo* componentTransform = nullptr;
	const MethodInfo* getPosition = nullptr;
	const MethodInfo* setPosition = nullptr;
	const MethodInfo* getRotation = nullptr;
	const MethodInfo* setRotation = nullptr;
	const MethodInfo* getLocalRotation = nullptr;
	const MethodInfo* getParent = nullptr;
	const MethodInfo* worldToCameraMatrix = nullptr;
	const MethodInfo* mainCamera = nullptr;
	const MethodInfo* getAxis = nullptr;
	const MethodInfo* getMouseButton = nullptr;
	const MethodInfo* mousePosition = nullptr;
	const MethodInfo* fieldOfView = nullptr;
	const MethodInfo* screenPointToRay = nullptr;
	const MethodInfo* worldToScreenPoint = nullptr;
	const MethodInfo* screenWidth = nullptr;
	const MethodInfo* screenHeight = nullptr;
	const MethodInfo* activeSelf = nullptr;
	const MethodInfo* deltaTime = nullptr;

	[[nodiscard]] static moa::Result<CameraBindings> Bind(const Managed& managed);
};

/// The camera and hand rework, on the running game. The per-frame logic is `CameraLoop` (tested with a model of the
/// game); this class gives it the game through IL2CPP. It works around `GameManager.Update`: `Before` runs before
/// the game moves the camera in the frame, and `After` runs after it.
///
/// The mod does nothing in a frame where the game controls the camera in a different way (for example a free
/// camera, a camera sequence, possession, a blend or a lock), and starts again from the state of the game after it.
class CameraMod
{
public:
	CameraMod(const Managed& managed, CameraBindings bindings, CameraSettings settings, ModStatus& status);
	CameraMod(const CameraMod&) = delete;
	CameraMod& operator=(const CameraMod&) = delete;
	~CameraMod();

	void Before(void* gameManager);
	void After(void* gameManager);

	/// Use new settings from the next frame. The loop starts again from the camera of the game. Call it on the main
	/// thread, between two frames.
	void ApplySettings(const CameraSettings& settings);
	/// Stop between two frames: the game controls its camera again, and the cursor is free.
	void StandBack(const std::string& reason);
	/// Give each object that the fade changed back to the game. Call it on the main thread, for example before the
	/// removal of the hooks and at the quit.
	void RestoreFade();
	/// The objects that the fade changes now: see-through and hidden.
	[[nodiscard]] std::pair<int, int> FadeCounts() const { return {_fade.Faded(), _fade.Hidden()}; }

	/// Record a trace of the frames in a file: give the rows to `sink`, after the header `comment`.
	void StartTrace(CameraTraceRecorder::Sink sink, std::string_view comment);
	/// Write the last row of the trace and close the file. The black box continues.
	void CloseTrace();
	/// Close the file of the trace. `StartTrace` can start a new trace after that.
	void StopTrace();
	/// The folder of the files of the black box, and the first line of each file.
	void SetBlackBox(std::filesystem::path folder, std::string header);
	/// The last `count` events of the camera, for the query `camera.events`.
	[[nodiscard]] nlohmann::json Events(size_t count) const;
	/// Record the pose of a camera just before it renders. The render hook calls it on the main thread.
	void TraceRender(void* camera);
	/// Make the render pipeline call the render hook before each camera renders. `hooked` is the method of the hook.
	/// The mod adds its own callback in the next frame that the trace records, because the callback of the game does
	/// not go through the hook.
	void EnableRenderSample(const MethodInfo* hooked);
	/// Remove the callbacks of the render pipeline that call the render hook. Call it on the main thread.
	void RemoveRenderSample();

	/// Give each frame to a test of the camera controls, or to nobody (null). The observer must stay until the next call.
	void SetObserver(CameraFrameObserver* observer) { _observer = observer; }
	/// The camera loop, for a test of the camera controls.
	[[nodiscard]] CameraLoop& Loop() { return _loop; }
	/// The settings that the camera mod uses now.
	[[nodiscard]] const CameraSettings& Settings() const { return _settings; }
	/// The rows that the black box keeps now, as a trace file.
	[[nodiscard]] std::string BlackBoxText(std::string_view cause) const;
	/// True while the hand holds an object. Nothing if the game does not tell it.
	[[nodiscard]] std::optional<bool> HoldsObject() const;
	/// How the object in the hand shows now, for a test. Nothing if the fade cannot tell it.
	[[nodiscard]] std::optional<HeldObjectLook> HeldLook();
	/// The game blends its camera to a view (focus, heading in degrees, zoom stage) in `seconds`, for a test of the camera
	/// controls. Call it in a frame of the camera mod. Return an error if it is not possible.
	[[nodiscard]] moa::Result<void> BlendCamera(WorldPoint focus, float direction, float stage, float seconds);

	/// Describe the camera of the game and the state of the mod, for the query `camera.state`.
	[[nodiscard]] nlohmann::json State(void* gameManager) const;

	/// Save the current view to a camera spot of the current save, and write the file of the spots. Return a message.
	[[nodiscard]] std::string SaveSpot(size_t index);
	/// Fly to a camera spot of the current save. Return a message.
	[[nodiscard]] std::string GoToSpot(size_t index);
	/// Fly to the view of the game at the current zoom with the north at the top. Return a message.
	[[nodiscard]] std::string ResetView(double northDegrees);
	/// For each camera spot of the current save: true if it is saved.
	[[nodiscard]] std::array<bool, k_CameraSpotCount> SavedSpots() const;

private:
	[[nodiscard]] std::optional<std::string> StandBackReason(void* gameManager) const;
	/// Return the reason when the mod did not change the frame.
	[[nodiscard]] std::optional<std::string> AfterLoop(void* gameManager);
	/// Fade the objects between the camera and the hand, after the camera loop made the camera of the frame.
	void StepFade();
	/// Give the frame to the observer, with a game when the game has a camera state.
	template <typename Function>
	void Observe(void* gameManager, Function&& function);

	// The trace. Each function does nothing when the trace is off, and stops the trace on an error.
	[[nodiscard]] std::optional<CameraPlacement> ReadCamera(void* gameManager) const;
	[[nodiscard]] std::optional<CameraPlacement> ReadCameraObject(Il2CppObject* camera) const;
	void TraceStart(void* gameManager, double seconds);
	void TraceBefore(void* gameManager, float deltaTime, bool standBack);
	void TraceAfterGame(void* gameManager);
	void TraceEnd(void* gameManager, double seconds);
	void RegisterRenderSample();
	/// Log one error if the render callback gives no sample for some frames.
	void CheckRenderSamples();
	[[nodiscard]] std::optional<std::string> AddRenderCallback();
	void RemoveCameraShake();
	/// Bind the names that only the trace and the black box use.
	void BindTraceNames();
	/// Add an event of the mod, and put it in the row of this frame.
	void Tell(CameraEventKind kind, std::string text);
	/// Write a file of the black box. Return false on an error.
	bool WriteBlackBox(std::string_view name, std::string_view text);
	template <typename Function>
	void Guard(Function&& function);

	CameraFrameObserver* _observer = nullptr;
	// The fade of the objects that hide the hand.
	OccluderFade _fade;
	std::unique_ptr<GameFadeScene> _fadeScene;
	bool _fadeBindTried = false; ///< The mod tried to bind the names of the fade. On an error the fade stays off.
	float _frameDeltaTime = 0.0f;
	void* _frameGameManager = nullptr; ///< The game manager of the frame that runs now.
	const Managed& _managed;
	CameraBindings _bindings;
	CameraSettings _settings;
	ModStatus& _status;
	CameraLoop _loop;
	/// The mouse cursor of the system. The hand rig keeps it on the pivot while the middle button is held.
	std::unique_ptr<SystemCursor> _cursor;
	std::chrono::steady_clock::time_point _start = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point _lastFrame {};
	std::unique_ptr<CameraTraceRecorder> _trace;
	bool _traceRow = false; ///< The trace records the current frame.
	CameraEventLog _events;
	std::unique_ptr<CameraBlackBox> _blackBox;
	std::filesystem::path _blackBoxFolder;
	/// Gives the events of the camera loop to the event log. One function for the session, so that each frame does
	/// not make a new one.
	std::function<void(CameraEventKind, std::string)> _loopEvents;
	std::optional<std::string> _standBack; ///< The reason of the last stand-back, for its event.
	std::optional<bool> _unityMiddle;      ///< The middle button of Unity in the last frame.
	bool _gameMiddleTold = false;          ///< The event about the middle button of the game was told.
	uint64_t _frame = 0;
	std::optional<WorldPoint> _lastMouse;
	const MethodInfo* _unityFrameCount = nullptr;
	/// The object that the hand holds, for the trace. Empty if the game does not have the method.
	const MethodInfo* _currentDragObject = nullptr;
	/// The field of the hand that shows the land drag, for the trace. Zero if the game does not have it.
	size_t _grabbingLand = 0;
	/// The camera of the town camera code in the last frame. The trace compares it with the camera that renders.
	Il2CppObject* _gameCamera = nullptr;
	// The callback of the render pipeline that runs the render hook.
	const MethodInfo* _renderMethod = nullptr;
	bool _renderTried = false;
	Il2CppObject* _renderDelegate = nullptr;
	uint64_t _renderSamples = 0;               ///< The count of render samples of the camera of the game.
	std::optional<uint64_t> _renderAddedFrame; ///< The frame in which the mod added its render callback.
	bool _renderSilenceLogged = false;
	uintptr_t _renderHandle = 0;
	const MethodInfo* _removeBeginCamera = nullptr;

	// The camera spots.
	/// Read the file of the camera spots one time. Return false if the file is not correct.
	bool LoadSpots();
	[[nodiscard]] std::string SpotKey() const;
	CameraSpotBook _spots;
	bool _spotsLoaded = false;
	bool _spotsFileBad = false; ///< The file of the spots is not correct: the mod does not write over it.
	int32_t _saveSlot = -1;     ///< The save slot of the game in the last frame.
};

/// The file of the camera spots: `MastersOfAlbionReforged\camera-spots.json` in the local application data of the user.
[[nodiscard]] std::filesystem::path CameraSpotsPath();

/// The settings of the camera mod as JSON.
[[nodiscard]] nlohmann::json CameraSettingsJson(const CameraSettings& settings);

/// Describe the camera of the game, with no mod: for the query `camera.state` when the camera mod is off.
[[nodiscard]] nlohmann::json DescribeCamera(const Managed& managed, const CameraBindings& bindings, void* gameManager);

} // namespace moa_mod::camera
