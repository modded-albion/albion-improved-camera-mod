/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "game/CameraMod.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <algorithm>
#include <array>
#include <format>
#include <string_view>
#include <utility>

#include <mod_core/SettingsFile.h>
#include <spdlog/spdlog.h>
#include <windows.h>

#include "game/GameFade.h"

namespace moa_mod::camera
{

namespace
{
constexpr const char* k_WheelAxis = "Mouse ScrollWheel";
/// The floor height of a sub scene is at this value or above when no sub scene is open.
constexpr float k_NoSubSceneFloor = 1e22f;
/// `QueryTriggerInteraction.Ignore`: the collision test of the camera does not find triggers.
constexpr int32_t k_IgnoreTriggers = 1;
/// The land height of the game for the camera is the highest land near a point plus this margin, metres.
constexpr float k_HighestGroundMargin = 2.0f;
/// The key function of the game "turn the camera with the mouse" (`EKeyboardFunction.RotateCameraByMouse`). By default
/// it is the middle mouse button.
constexpr int32_t k_TurnCameraByMouse = 4;

/// The memory of `UnityEngine.RaycastHit`: the point, the normal, the face, the distance, the texture point and the
/// collider.
struct RaycastHitValue
{
	WorldPoint point;
	WorldPoint normal;
	uint32_t face = 0;
	float distance = 0.0f;
	std::array<float, 2> uv {};
	std::array<uint32_t, 4> collider {};
};
static_assert(sizeof(WorldPoint) == 12);

[[nodiscard]] nlohmann::json ToJson(WorldPoint point)
{
	return nlohmann::json::array({point.x, point.y, point.z});
}

[[nodiscard]] nlohmann::json ToJson(Quaternion rotation)
{
	return nlohmann::json::array({rotation.x, rotation.y, rotation.z, rotation.w});
}

/// Return a value of a result as JSON, or its error text.
template <typename T>
[[nodiscard]] nlohmann::json ToJson(const moa::Result<T>& result)
{
	if (!result)
	{
		return {{"error", result.GetError().message}};
	}
	if constexpr (std::is_same_v<T, WorldPoint> || std::is_same_v<T, Quaternion>)
	{
		return ToJson(*result);
	}
	else
	{
		return *result;
	}
}

} // namespace

moa::Result<CameraBindings> CameraBindings::Bind(const Managed& managed)
{
	Binder binder(managed);
	CameraBindings b;
	auto* gameManager = binder.Class("", "GameManager");
	auto* gameState = binder.Class("", "GameState");
	auto* hand = binder.Class("", "PlayerHandManager");
	auto* pause = binder.Class("", "PauseManager");
	auto* utility = binder.Class("", "Utility");
	auto* cameraControl = binder.Class("", "MACameraControl");
	auto* ngManager = binder.Class("", "NGManager");
	auto* component = binder.Class("UnityEngine", "Component");
	auto* transform = binder.Class("UnityEngine", "Transform");
	auto* input = binder.Class("UnityEngine", "Input");
	auto* unityCamera = binder.Class("UnityEngine", "Camera");
	auto* screen = binder.Class("UnityEngine", "Screen");
	auto* gameObject = binder.Class("UnityEngine", "GameObject");
	auto* time = binder.Class("UnityEngine", "Time");

	b.state = binder.Field(gameManager, "<m_state>k__BackingField", "GameState");
	b.dataReady = binder.Field(gameManager, "DataReady", "System.Boolean");
	b.camera = binder.Field(gameManager, "m_camera", "UnityEngine.Camera");
	b.minimumZoom = binder.Field(gameManager, "m_cameraControlMinimumZoomLevel", "System.Single");
	b.clampMaxZoom = binder.Field(gameManager, "m_focusClampMaxZoom", "System.Single");
	b.forceZoomOut = binder.Field(gameManager, "m_isInForceZoomOut", "System.Boolean");
	b.freeCamera = binder.Field(gameManager, "m_freeCameraMode", "System.Boolean");
	b.panSequence = binder.Field(gameManager, "m_cameraPanSequenceInProgress", "System.Int32");
	b.trackingFocus = binder.Field(gameManager, "m_cameraTrackingFocus", "UnityEngine.GameObject");
	b.overrideTransform = binder.Field(gameManager, "m_cameraOverrideTransform", "UnityEngine.Transform");
	b.transition = binder.Field(gameManager, "m_cameraTransition", "UnityEngine.Coroutine");
	b.inputLocked = binder.Field(gameManager, "m_cameraUserInputLocked", "System.Boolean");
	b.blendFromT = binder.Field(gameManager, "m_cameraBlendFromT", "System.Single");
	b.clampForwardSize = binder.Field(gameManager, "m_focusClampForwardSize", "System.Single");
	b.lastTilt = binder.Field(gameManager, "m_cameraControlLastTilt", "System.Single");
	b.lastHeight = binder.Field(gameManager, "m_cameraControlLastDistance", "System.Single");
	b.ignoreCollisions = binder.Field(gameManager, "m_cameraIgnoreCollisions", "System.Boolean");
	b.dragFade = binder.Field(gameManager, "m_dragFade", "System.Single");
	b.zoomRotateChanging = binder.Field(gameManager, "m_wasZoomRotateChanging", "System.Boolean");
	b.holdingZoomRotate = binder.Field(gameManager, "m_wasHoldingZoomRotate", "System.Boolean");
	b.dragValid = binder.Field(gameManager, "m_isDragValid", "System.Boolean");
	b.dragStartedTime = binder.Field(gameManager, "m_dragStartedTime", "System.Single");
	b.noHandFocus = binder.Field(gameManager, "m_noHandFocus", "System.Boolean");
	b.stageIgnoreHand = binder.Field(gameManager, "m_cameraControlStageIgnoreHand", "System.Int32");
	b.inhibitHandFocus = binder.Field(gameManager, "m_cameraControlInhibitHandFocus", "System.Int32");
	b.dragGrab = binder.Field(gameManager, "m_lastDragZoomRotateFocus", "UnityEngine.Vector3");
	b.smoothMouseRaycast = binder.Field(gameManager, "m_smoothMouseRaycast", "UnityEngine.Vector3");
	b.lastCursorTerrain = binder.Field(gameManager, "m_lastCursorTerrainPos", "UnityEngine.Vector3");
	b.subSceneFloor = binder.Field(gameManager, "m_subSceneFloorY", "System.Single");
	b.smoothMouseVisual = binder.Field(gameManager, "m_smoothMouseVisual", "UnityEngine.Vector3");
	b.turnCursor = binder.Field(gameManager, "m_rotateMouse", "UnityEngine.Vector3");
	b.moveSpeed = binder.Field(gameManager, "m_cameraControlMoveSpeed", "System.Single");
	b.dollySmooth = binder.Field(gameManager, "m_cameraDollySmoothByDefault", "System.Boolean");
	b.inputWasConsumed = binder.Field(gameManager, "m_inputWasConsumed", "System.Boolean");
	b.inputWasDragging = binder.Field(gameManager, "m_inputWasDragging", "System.Boolean");
	b.lightsAndCameras = binder.Field(gameManager, "m_lightsAndCameras", "UnityEngine.GameObject");
	b.edgeScrollSpeed = binder.Field(gameManager, "m_pushSpeed", "System.Single");
	b.highestGround = binder.Method(gameManager, "HighestGroundHeightNearCamera", {"UnityEngine.Vector3"}, "System.Single");
	b.raycastTerrain = binder.Method(gameManager, "RaycastTerrain", {"UnityEngine.Vector3", "System.Boolean"},
	                                 "System.Nullable<UnityEngine.Vector3>");
	b.raycastSubScene =
	    binder.Method(gameManager, "RaycastSubSceneOrTerrain", {"UnityEngine.Vector3"}, "System.Nullable<UnityEngine.Vector3>");
	b.clampFocus = binder.Method(gameManager, "ClampFocus", {"UnityEngine.Transform"}, "System.Void");
	b.isOverridden = binder.Method(gameManager, "get_IsCameraBeingOverridden", {}, "System.Boolean");
	b.isClamped = binder.Method(gameManager, "get_IsCameraClamped", {}, "System.Boolean");
	b.isDesignTable = binder.Method(gameManager, "get_IsDesignTable", {}, "System.Boolean");
	b.isInterior = binder.Method(gameManager, "get_IsInterior", {}, "System.Boolean");
	b.isDesignTransition = binder.Method(gameManager, "get_IsAnyTransitionInProgress", {}, "System.Boolean");
	b.isTitleScreen = binder.Method(gameManager, "IsInTitleScreen", {}, "System.Boolean");
	b.isPossessing = binder.Method(gameManager, "get_IsPossessing", {}, "System.Boolean");
	b.isCavelike = binder.Method(gameManager, "get_IsInCavelike", {}, "System.Boolean", true);

	b.focus = binder.Field(gameState, "m_cameraControlFocus", "UnityEngine.Vector3");
	b.stage = binder.Field(gameState, "m_cameraControlStage", "System.Single");
	b.direction = binder.Field(gameState, "m_cameraControlDirection", "System.Single");

	b.handPosition = binder.Field(hand, "m_smoothPos", "UnityEngine.Vector3");
	b.handOverUi = binder.Field(hand, "m_isOverUI", "System.Boolean");
	b.handVisible = binder.Field(hand, "m_handVisible", "System.Boolean");
	b.setMouseOverride = binder.Method(hand, "SetMouseOverride", {"UnityEngine.Vector3"}, "System.Void");

	b.isPaused = binder.Method(pause, "get_IsPaused", {}, "System.Boolean", true);
	b.inputFieldActive = binder.Method(utility, "IsInputFieldActive", {}, "System.Boolean", true);
	b.getIgnoreWheel = binder.Method(utility, "get_IgnoreMouseWheel", {}, "System.Boolean", true);
	b.setIgnoreWheel = binder.Method(utility, "set_IgnoreMouseWheel", {"System.Boolean"}, "System.Void", true);
	b.stageCount = binder.Method(cameraControl, "get_NumStages", {}, "System.Int32", true);
	b.stageRow = binder.Method(cameraControl, "Get", {"System.Int32"}, "MACameraControl", true);
	b.rowHeight = binder.Field(cameraControl, "m_height", "System.Single");
	b.rowTilt = binder.Field(cameraControl, "m_tilt", "System.Single");
	b.stageTightness = binder.Field(ngManager, "m_cameraControlStageAutoSnapTightness", "System.Single");
	b.focusHeightTightness = binder.Field(ngManager, "m_cameraControlFocusYTightness", "System.Single");
	b.inputPosition = binder.Method(utility, "get_InputPos", {}, "UnityEngine.Vector3", true);

	b.componentTransform = binder.Method(component, "get_transform", {}, "UnityEngine.Transform");
	b.getPosition = binder.Method(transform, "get_position", {}, "UnityEngine.Vector3");
	b.setPosition = binder.Method(transform, "set_position", {"UnityEngine.Vector3"}, "System.Void");
	b.getRotation = binder.Method(transform, "get_rotation", {}, "UnityEngine.Quaternion");
	b.setRotation = binder.Method(transform, "set_rotation", {"UnityEngine.Quaternion"}, "System.Void");
	b.getLocalRotation = binder.Method(transform, "get_localRotation", {}, "UnityEngine.Quaternion");
	b.getParent = binder.Method(transform, "get_parent", {}, "UnityEngine.Transform");
	b.worldToCameraMatrix = binder.Method(unityCamera, "get_worldToCameraMatrix", {}, "UnityEngine.Matrix4x4");
	b.mainCamera = binder.Method(unityCamera, "get_main", {}, "UnityEngine.Camera", true);
	b.getAxis = binder.Method(input, "GetAxis", {"System.String"}, "System.Single", true);
	b.getMouseButton = binder.Method(input, "GetMouseButton", {"System.Int32"}, "System.Boolean", true);
	b.mousePosition = binder.Method(input, "get_mousePosition", {}, "UnityEngine.Vector3", true);
	b.fieldOfView = binder.Method(unityCamera, "get_fieldOfView", {}, "System.Single");
	b.screenPointToRay = binder.Method(unityCamera, "ScreenPointToRay", {"UnityEngine.Vector3"}, "UnityEngine.Ray");
	b.worldToScreenPoint = binder.Method(unityCamera, "WorldToScreenPoint", {"UnityEngine.Vector3"}, "UnityEngine.Vector3");
	b.screenWidth = binder.Method(screen, "get_width", {}, "System.Int32", true);
	b.screenHeight = binder.Method(screen, "get_height", {}, "System.Int32", true);
	b.activeSelf = binder.Method(gameObject, "get_activeSelf", {}, "System.Boolean");
	b.deltaTime = binder.Method(time, "get_deltaTime", {}, "System.Single", true);

	if (!binder.Errors().empty())
	{
		return moa::MakeError("{}", binder.Errors());
	}
	// The sphere test of the camera of the game is optional: without it, the spring arm uses only the land.
	if (auto cast = managed.Method(gameManager, "FilteredSphereCast",
	                               {"UnityEngine.Vector3", "System.Single", "UnityEngine.Vector3", "UnityEngine.RaycastHit&",
	                                "System.Single", "System.Int32", "UnityEngine.QueryTriggerInteraction"},
	                               "System.Boolean", false);
	    !cast)
	{
		spdlog::error("camera mod: the spring arm does not find objects, only the land: {}", cast.GetError().message);
	}
	else
	{
		b.filteredSphereCast = *cast;
	}
	// The exact land is optional: without it, the land of the camera is the land height of the game less its margin.
	if (auto ground = managed.Method(gameManager, "CameraGroundHeight", {"UnityEngine.Vector3"}, "UnityEngine.Vector3", false);
	    ground)
	{
		b.cameraGroundHeight = *ground;
	}
	else
	{
		spdlog::error("camera mod: the camera uses the land height with a margin, not the exact land: {}",
		              ground.GetError().message);
	}
	// The blend of the camera is optional: only the tests of the camera controls use it.
	if (auto blend = managed.Method(gameManager, "BlendCameraTo",
	                                {"UnityEngine.Vector3", "System.Single", "System.Single", "System.Single", "System.Action"},
	                                "System.Void", false);
	    blend)
	{
		b.blendCameraTo = *blend;
	}
	// The navigation is optional: without these names, the double click zooms as in the game, the map edge is hard and
	// the camera spots are the same for all saves.
	if (auto field = managed.Field(gameManager, "m_zoomToStage", "System.Single"); field)
	{
		b.zoomToStage = *field;
	}
	else
	{
		spdlog::error("camera mod: a double click zooms as in the game: {}", field.GetError().message);
	}
	if (auto field = managed.Field(gameManager, "m_selectedSaveSlot", "System.Int32"); field)
	{
		b.saveSlot = *field;
	}
	else
	{
		spdlog::error("camera mod: the camera spots are the same for each save: {}", field.GetError().message);
	}
	// The buttons that the game reads come from its own input (Steam Input can give the left and the right button).
	// The mod reads the same buttons, so that the mod and the game agree about a press. The mouse button of the game
	// never gives the middle button with Steam Input. The game turns the camera while the input of its key function
	// "turn the camera with the mouse" is held, so the mod reads the middle button from that input.
	if (auto method = managed.Method(gameManager, "GetMouseButton", {"System.Int32"}, "System.Boolean", true); method)
	{
		b.gameMouseButton = *method;
	}
	else
	{
		spdlog::error("camera mod: the mod reads the left and the right button of Unity: {}", method.GetError().message);
	}
	if (auto extensions = managed.Class("", "Extensions"); !extensions)
	{
		spdlog::error("camera mod: the mod reads the middle button of Unity: {}", extensions.GetError().message);
	}
	else if (auto method = managed.Method(*extensions, "AnyHeld", {"EKeyboardFunction"}, "System.Boolean", true); !method)
	{
		spdlog::error("camera mod: the mod reads the middle button of Unity: {}", method.GetError().message);
	}
	else
	{
		b.inputHeld = *method;
	}
	// The trace and the events read more of the input of the game, and the names of the objects that the spring arm
	// touches. Without these names, the columns stay empty and the events name no object.
	if (auto method = managed.Method(utility, "GetMouseButton", {"System.Int32"}, "System.Boolean", true); method)
	{
		b.utilityMouseButton = *method;
	}
	if (auto uiInput = managed.Class("", "UIInput"); uiInput)
	{
		if (auto method = managed.Method(*uiInput, "get_IsActive", {}, "System.Boolean", true); method)
		{
			b.uiInputActive = *method;
		}
	}
	if (auto object = managed.Class("UnityEngine", "Object"); object)
	{
		if (auto method =
		        managed.Method(*object, "FindObjectFromInstanceID", {"UnityEngine.EntityId"}, "UnityEngine.Object", true);
		    method)
		{
			b.findObject = *method;
		}
		if (auto method = managed.Method(*object, "get_name", {}, "System.String", false); method)
		{
			b.objectName = *method;
		}
	}
	spdlog::info("camera mod: {}",
	             DescribeButtonSources({.turnInput = b.inputHeld != nullptr, .gameButton = b.gameMouseButton != nullptr}));
	if (auto globalData = managed.Class("", "GlobalData"); globalData)
	{
		auto minimum = managed.Method(*globalData, "get_c_terrainMin", {}, "UnityEngine.Vector3", true);
		auto maximum = managed.Method(*globalData, "get_c_terrainMax", {}, "UnityEngine.Vector3", true);
		if (minimum && maximum)
		{
			b.terrainMinimum = *minimum;
			b.terrainMaximum = *maximum;
		}
		else
		{
			spdlog::error("camera mod: the edge of the map stays hard: {}",
			              !minimum ? minimum.GetError().message : maximum.GetError().message);
		}
	}
	// The zoom limit of a held object is optional: without it, a pickup near the ground zooms out as in the game.
	if (auto field = managed.StaticField(gameManager, "s_closeHoldCamera", "System.Single"); !field)
	{
		spdlog::error("camera mod: a pickup zooms out as in the game: {}", field.GetError().message);
	}
	else if (managed.il2cpp_field_static_set_value == nullptr)
	{
		spdlog::error("camera mod: a pickup zooms out as in the game: GameAssembly.dll does not export "
		              "il2cpp_field_static_set_value");
	}
	else
	{
		b.heldObjectMinimumStage = *field;
	}
	return b;
}

/// The mouse cursor of Windows in the window of the game. The hand rig moves it back to the pivot in each frame while
/// the middle button is held, so that the cursor does not move on the screen. Unity has no function that moves the
/// cursor with the input manager of the game.
///
/// The mouse position of Unity has its origin at the bottom left of the window. The class learns the exact relation
/// between that position and the pixel of the window: in a frame where neither the cursor nor the mouse position of
/// Unity moved since the last frame, both show the same place. Until then, it does not move the cursor.
class SystemCursor
{
public:
	/// Compare the mouse position of Unity with the cursor of Windows. Call it once at the start of each frame.
	void Observe(WorldPoint mouse, int32_t width, int32_t height)
	{
		auto client = ClientCursor(width, height);
		if (!client)
		{
			_lastClient.reset();
			_lastMouse.reset();
			return;
		}
		if (_lastClient && _lastMouse && _lastClient->x == client->x && _lastClient->y == client->y &&
		    _lastMouse->x == mouse.x && _lastMouse->y == mouse.y)
		{
			_bias =
			    WorldPoint {mouse.x - static_cast<float>(client->x), mouse.y - static_cast<float>(height - client->y), 0.0f};
		}
		_lastClient = client;
		_lastMouse = mouse;
	}

	/// Move the cursor to a mouse position of Unity. Return the mouse position that Unity gives for the new place
	/// of the cursor, or the reason why the cursor did not move.
	[[nodiscard]] moa::Result<WorldPoint> Warp(WorldPoint mouse, int32_t width, int32_t height)
	{
		if (!_bias)
		{
			return moa::MakeError("the mod has not yet measured the mouse position of Unity against the cursor");
		}
		auto* window = Window();
		if (window == nullptr || GetForegroundWindow() != window)
		{
			return moa::MakeError("the window of the game does not have the focus");
		}
		if (!ClientCursor(width, height))
		{
			return moa::MakeError("the window of the game does not have the size of the screen of Unity");
		}
		POINT point {std::lround(mouse.x - _bias->x), std::lround(static_cast<float>(height) - (mouse.y - _bias->y))};
		point.x = std::clamp<LONG>(point.x, 0, width - 1);
		point.y = std::clamp<LONG>(point.y, 0, height - 1);
		const auto client = point;
		if (!ClientToScreen(window, &point) || !SetCursorPos(point.x, point.y))
		{
			return moa::MakeError("Windows did not move the cursor (error {})", GetLastError());
		}
		_lastClient = client;
		_lastMouse.reset();
		return WorldPoint {static_cast<float>(client.x) + _bias->x, static_cast<float>(height - client.y) + _bias->y, 0.0f};
	}

private:
	/// The cursor in the pixels of the window, or nothing if the window is not found or its size is not the size
	/// of the screen of Unity.
	[[nodiscard]] std::optional<POINT> ClientCursor(int32_t width, int32_t height)
	{
		auto* window = Window();
		RECT rect {};
		POINT point {};
		if (window == nullptr || !GetClientRect(window, &rect) || rect.right != width || rect.bottom != height ||
		    !GetCursorPos(&point) || !ScreenToClient(window, &point))
		{
			_bias.reset();
			return std::nullopt;
		}
		return point;
	}

	/// The window of the Unity player of this process.
	[[nodiscard]] HWND Window()
	{
		if (_window != nullptr && IsWindow(_window))
		{
			return _window;
		}
		struct Search
		{
			DWORD process = GetCurrentProcessId();
			HWND found = nullptr;
		} search;
		EnumWindows(
		    [](HWND window, LPARAM data) -> BOOL {
			    auto& state = *reinterpret_cast<Search*>(data);
			    DWORD process = 0;
			    GetWindowThreadProcessId(window, &process);
			    std::array<wchar_t, 64> name {};
			    if (process == state.process && IsWindowVisible(window) &&
			        GetClassNameW(window, name.data(), static_cast<int>(name.size())) > 0 &&
			        std::wstring_view(name.data()) == L"UnityWndClass")
			    {
				    state.found = window;
				    return FALSE;
			    }
			    return TRUE;
		    },
		    reinterpret_cast<LPARAM>(&search));
		_window = search.found;
		return _window;
	}

	HWND _window = nullptr;
	std::optional<POINT> _lastClient;
	std::optional<WorldPoint> _lastMouse;
	std::optional<WorldPoint> _bias; ///< The mouse position of Unity less the pixel of the window, with y up.
};

CameraMod::CameraMod(const Managed& managed, CameraBindings bindings, CameraSettings settings, ModStatus& status)
    : _managed(managed)
    , _bindings(std::move(bindings))
    , _settings(settings)
    , _status(status)
    , _loop(settings)
    , _cursor(std::make_unique<SystemCursor>())
    , _trace(std::make_unique<CameraTraceRecorder>(CameraTraceRecorder::Options {}, CameraTraceRecorder::Sink {}))
{
	// The menu bar shows the saved spots from the start.
	static_cast<void>(LoadSpots());
	BindTraceNames();
	// Each event of the camera is a line in the log, with a limit for repeats. The log file has it at once, also if
	// the game stops soon after.
	_events.SetSink([](const CameraEvent& event) {
		spdlog::info("{}", FormatEventLine(event));
		spdlog::default_logger()->flush();
	});
	_loopEvents = [this](CameraEventKind kind, std::string text) {
		if (_observer != nullptr)
		{
			_observer->OnEvent(kind, text);
		}
		_events.Add(_frame, std::chrono::duration<double>(std::chrono::steady_clock::now() - _start).count(), kind,
		            std::move(text));
	};
	// The black box keeps the last seconds of the rows, also when the trace file is off. When a rule of the camera
	// breaks, it writes them to a file.
	_blackBox = std::make_unique<CameraBlackBox>(
	    CameraBlackBox::Options {}, [this](std::string_view name, std::string_view text) { return WriteBlackBox(name, text); });
	_trace->SetRowDone([this](CameraTraceRow& row) {
		if (auto broken = _blackBox->Add(row))
		{
			Tell(CameraEventKind::Invariant,
			     std::format("frame {}: the rule '{}' broke: {}", row.frame, Name(broken->invariant), broken->text));
		}
	});
}

CameraMod::~CameraMod() = default;

std::optional<std::string> CameraMod::StandBackReason(void* gameManager) const
{
	const auto& b = _bindings;
	if (gameManager == nullptr)
	{
		return "there is no GameManager";
	}
	if (!ReadField<bool>(gameManager, b.dataReady))
	{
		return "the game data is not ready";
	}
	if (ReadField<bool>(gameManager, b.forceZoomOut))
	{
		return "the game forces a zoom out";
	}
	struct Check
	{
		const MethodInfo* method;
		bool isStatic;
		const char* reason;
	};
	// Each check is a condition that makes the game control the camera with other code than the town camera.
	const std::array<Check, 9> checks {{
	    {b.isPaused, true, "the game is paused"},
	    {b.inputFieldActive, true, "a text field has the keyboard"},
	    {b.isOverridden, false,
	     "the camera is overridden (free camera, sequence, tracking, possession, transition, blend, lock)"},
	    {b.isClamped, false, "the camera is clamped to a building"},
	    {b.isDesignTable, false, "the design table is open"},
	    {b.isInterior, false, "the camera is in an interior"},
	    {b.isDesignTransition, false, "a design table transition is in progress"},
	    {b.isTitleScreen, false, "the title screen is open"},
	    {b.isCavelike, true, "the camera is in a cave"},
	}};
	for (const auto& check : checks)
	{
		auto value = _managed.Call<bool>(check.method, check.isStatic ? nullptr : gameManager);
		if (!value)
		{
			return value.GetError().message;
		}
		if (*value)
		{
			return check.reason;
		}
	}
	if (ReadField<Il2CppObject*>(gameManager, b.state) == nullptr)
	{
		return "there is no game state";
	}
	return std::nullopt;
}

namespace
{
/// The game, for the camera loop, through IL2CPP. Each failed call reports its error to the status of the mod.
class GameAdapter final: public CameraGame
{
public:
	GameAdapter(const Managed& managed, const CameraBindings& bindings, ModStatus& status, SystemCursor& cursor,
	            void* gameManager, const std::function<void(CameraEventKind, std::string)>& events,
	            CameraFrameObserver* observer)
	    : _managed(managed)
	    , _b(bindings)
	    , _status(status)
	    , _cursor(cursor)
	    , _events(events)
	    , _observer(observer)
	    , _gameManager(gameManager)
	    , _state(gameManager != nullptr ? ReadField<Il2CppObject*>(gameManager, bindings.state) : nullptr)
	{
	}

	[[nodiscard]] WorldPoint Focus() const override { return ReadField<WorldPoint>(_state, _b.focus); }
	void SetFocus(WorldPoint focus) override { WriteField(_state, _b.focus, focus); }
	[[nodiscard]] float Direction() const override { return ReadField<float>(_state, _b.direction); }
	void SetDirection(float direction) override { WriteField(_state, _b.direction, direction); }
	[[nodiscard]] float Stage() const override { return ReadField<float>(_state, _b.stage); }
	void SetStage(float stage) override { WriteField(_state, _b.stage, stage); }
	[[nodiscard]] float StageTilt() const override { return ReadField<float>(_gameManager, _b.lastTilt); }
	[[nodiscard]] float StageHeight() const override { return ReadField<float>(_gameManager, _b.lastHeight); }

	[[nodiscard]] std::optional<StageLimits> ZoomLimits() override
	{
		auto count = _managed.Call<int32_t>(_b.stageCount, nullptr);
		if (!count)
		{
			return Fail(count.GetError().message);
		}
		return StageLimitsFor(*count, ReadField<float>(_gameManager, _b.minimumZoom),
		                      ReadField<float>(_gameManager, _b.clampMaxZoom));
	}

	[[nodiscard]] std::optional<std::vector<StageView>> StageTable() override
	{
		auto count = _managed.Call<int32_t>(_b.stageCount, nullptr);
		if (!count)
		{
			return Fail(count.GetError().message);
		}
		std::vector<StageView> table;
		for (int32_t index = 0; index < *count; ++index)
		{
			auto row = _managed.Invoke(_b.stageRow, nullptr, {&index});
			if (!row || *row == nullptr)
			{
				return Fail(row ? std::format("the zoom stage {} has no row", index) : row.GetError().message);
			}
			table.push_back({.height = ReadField<float>(*row, _b.rowHeight), .tilt = ReadField<float>(*row, _b.rowTilt)});
		}
		return table;
	}

	void SetStageView(StageView view) override
	{
		WriteField(_gameManager, _b.lastHeight, view.height);
		WriteField(_gameManager, _b.lastTilt, view.tilt);
	}

	[[nodiscard]] std::optional<GameEases> Eases() override
	{
		auto manager = _managed.SingletonInstance("NGManager");
		if (!manager)
		{
			return Fail(manager.GetError().message);
		}
		return GameEases {.stage = ReadField<float>(*manager, _b.stageTightness),
		                  .focusHeight = ReadField<float>(*manager, _b.focusHeightTightness)};
	}

	[[nodiscard]] bool SetEases(GameEases eases) override
	{
		auto manager = _managed.SingletonInstance("NGManager");
		if (!manager)
		{
			static_cast<void>(Fail(manager.GetError().message));
			return false;
		}
		WriteField(*manager, _b.stageTightness, eases.stage);
		WriteField(*manager, _b.focusHeightTightness, eases.focusHeight);
		return true;
	}

	[[nodiscard]] std::optional<float> HeldObjectMinimumStage() override
	{
		if (_b.heldObjectMinimumStage == nullptr)
		{
			return std::nullopt;
		}
		return _managed.ReadStatic<float>(_b.heldObjectMinimumStage);
	}

	[[nodiscard]] bool SetHeldObjectMinimumStage(float stage) override
	{
		return _b.heldObjectMinimumStage != nullptr && _managed.WriteStatic(_b.heldObjectMinimumStage, stage);
	}

	[[nodiscard]] bool IgnoresCollisions() const override { return ReadField<bool>(_gameManager, _b.ignoreCollisions); }
	[[nodiscard]] bool SetIgnoresCollisions(bool ignore) override
	{
		WriteField(_gameManager, _b.ignoreCollisions, ignore);
		return true;
	}

	[[nodiscard]] std::optional<float> MoveSpeed() override { return ReadField<float>(_gameManager, _b.moveSpeed); }
	[[nodiscard]] bool SetMoveSpeed(float speed) override
	{
		WriteField(_gameManager, _b.moveSpeed, speed);
		return true;
	}
	[[nodiscard]] std::optional<bool> DollySmooth() override { return ReadField<bool>(_gameManager, _b.dollySmooth); }
	[[nodiscard]] bool SetDollySmooth(bool smooth) override
	{
		WriteField(_gameManager, _b.dollySmooth, smooth);
		return true;
	}

	[[nodiscard]] std::optional<EdgeScrollFrame> EdgeScroll() override
	{
		// The game adds its edge scroll to the pan only while a drag consumes the input, and while its object of
		// lights and cameras is active.
		EdgeScrollFrame frame;
		auto* lights = ReadField<Il2CppObject*>(_gameManager, _b.lightsAndCameras);
		if (!ReadField<bool>(_gameManager, _b.inputWasConsumed) || !ReadField<bool>(_gameManager, _b.inputWasDragging) ||
		    lights == nullptr)
		{
			return frame;
		}
		auto active = _managed.Call<bool>(_b.activeSelf, lights);
		auto deltaTime = _managed.Call<float>(_b.deltaTime, nullptr);
		auto cursor = InputPosition();
		if (!active || !deltaTime || !cursor)
		{
			return Fail(!active ? active.GetError().message
			                    : (!deltaTime ? deltaTime.GetError().message : "no input position"));
		}
		frame.active = *active;
		frame.speed = ReadField<float>(_gameManager, _b.edgeScrollSpeed);
		frame.deltaTime = *deltaTime;
		frame.cursor = *cursor;
		return frame;
	}

	[[nodiscard]] std::optional<float> ZoomToStage() override
	{
		if (_b.zoomToStage == 0)
		{
			return std::nullopt;
		}
		return ReadField<float>(_gameManager, _b.zoomToStage);
	}

	void SetZoomToStage(float stage) override
	{
		if (_b.zoomToStage != 0)
		{
			WriteField(_gameManager, _b.zoomToStage, stage);
		}
	}

	[[nodiscard]] std::optional<FocusBox> FocusLimits() override
	{
		if (_b.terrainMinimum == nullptr || _b.terrainMaximum == nullptr)
		{
			return std::nullopt;
		}
		auto minimum = _managed.Call<WorldPoint>(_b.terrainMinimum, nullptr);
		auto maximum = _managed.Call<WorldPoint>(_b.terrainMaximum, nullptr);
		if (!minimum || !maximum)
		{
			return Fail(!minimum ? minimum.GetError().message : maximum.GetError().message);
		}
		if (minimum->x >= maximum->x || minimum->z >= maximum->z)
		{
			// The terrain is not ready.
			return std::nullopt;
		}
		return FocusBox {.minimumX = minimum->x, .minimumZ = minimum->z, .maximumX = maximum->x, .maximumZ = maximum->z};
	}

	[[nodiscard]] std::optional<CameraPlacement> Camera() override
	{
		auto* transform = Transform();
		if (transform == nullptr)
		{
			return std::nullopt;
		}
		auto position = _managed.Call<WorldPoint>(_b.getPosition, transform);
		auto rotation = _managed.Call<Quaternion>(_b.getRotation, transform);
		if (!position || !rotation)
		{
			return Fail(!position ? position.GetError().message : rotation.GetError().message);
		}
		return CameraPlacement {.position = *position, .rotation = *rotation};
	}

	[[nodiscard]] bool SetCamera(const CameraPlacement& placement) override
	{
		auto* transform = Transform();
		auto position = placement.position;
		auto rotation = placement.rotation;
		return transform != nullptr && _managed.Invoke(_b.setPosition, transform, {&position}) &&
		       _managed.Invoke(_b.setRotation, transform, {&rotation});
	}

	[[nodiscard]] std::optional<Ray> ScreenRay(WorldPoint screen) override
	{
		return Value(_managed.Call<Ray>(_b.screenPointToRay, ReadField<Il2CppObject*>(_gameManager, _b.camera), {&screen}));
	}

	[[nodiscard]] std::optional<WorldPoint> LandUnderScreenPoint(WorldPoint screen) override
	{
		auto hit = _managed.Invoke(_b.raycastSubScene, _gameManager, {&screen});
		if (!hit)
		{
			return Fail(hit.GetError().message);
		}
		return Unbox(*hit);
	}

	[[nodiscard]] std::optional<bool> CursorOnLand() override
	{
		auto mouse = MousePosition();
		if (!mouse)
		{
			return std::nullopt;
		}
		bool clampToSeaLevel = false;
		auto hit = _managed.Invoke(_b.raycastTerrain, _gameManager, {&*mouse, &clampToSeaLevel});
		if (!hit)
		{
			return Fail(hit.GetError().message);
		}
		return *hit != nullptr;
	}

	[[nodiscard]] std::optional<WorldPoint> ScreenPointOf(WorldPoint point) override
	{
		return Value(
		    _managed.Call<WorldPoint>(_b.worldToScreenPoint, ReadField<Il2CppObject*>(_gameManager, _b.camera), {&point}));
	}

	[[nodiscard]] std::optional<Vector2> ScreenSize() override
	{
		auto width = _managed.Call<int32_t>(_b.screenWidth, nullptr);
		auto height = _managed.Call<int32_t>(_b.screenHeight, nullptr);
		if (!width || !height)
		{
			return Fail(!width ? width.GetError().message : height.GetError().message);
		}
		return Vector2 {static_cast<float>(*width), static_cast<float>(*height)};
	}

	[[nodiscard]] std::optional<float> FieldOfView() override
	{
		return Value(_managed.Call<float>(_b.fieldOfView, ReadField<Il2CppObject*>(_gameManager, _b.camera)));
	}

	[[nodiscard]] std::optional<float> HighestGround(WorldPoint position) override
	{
		return Value(_managed.Call<float>(_b.highestGround, _gameManager, {&position}));
	}

	[[nodiscard]] std::optional<float> LandHeight(WorldPoint position) override
	{
		if (_b.cameraGroundHeight == nullptr)
		{
			auto highest = HighestGround(position);
			return highest ? std::optional<float>(*highest - k_HighestGroundMargin) : std::nullopt;
		}
		auto ground = Value(_managed.Call<WorldPoint>(_b.cameraGroundHeight, _gameManager, {&position}));
		return ground ? std::optional<float>(ground->y) : std::nullopt;
	}

	[[nodiscard]] std::optional<SphereHit> CastSphere(WorldPoint origin, WorldPoint direction, float radius, float length,
	                                                  int32_t layers) override
	{
		if (_b.filteredSphereCast == nullptr)
		{
			return std::nullopt;
		}
		// The test of the game ignores the objects of the camera itself. It uses the trigger rule of the collision test
		// of the camera of the game.
		RaycastHitValue hit {};
		auto triggers = k_IgnoreTriggers;
		auto found = _managed.Call<bool>(_b.filteredSphereCast, _gameManager,
		                                 {&origin, &radius, &direction, &hit, &length, &layers, &triggers});
		if (!found)
		{
			return Fail(found.GetError().message);
		}
		uint64_t collider = 0;
		std::memcpy(&collider, hit.collider.data(), sizeof(collider));
		return SphereHit {.hit = *found, .distance = *found ? hit.distance : 0.0f, .collider = *found ? collider : 0};
	}

	void Event(CameraEventKind kind, std::string text) override
	{
		if (_events)
		{
			_events(kind, std::move(text));
		}
	}

	[[nodiscard]] std::optional<std::string> ColliderName(uint64_t collider) override
	{
		if (_b.findObject == nullptr || _b.objectName == nullptr)
		{
			return std::nullopt;
		}
		auto object = _managed.Invoke(_b.findObject, nullptr, {&collider});
		if (!object || *object == nullptr)
		{
			return std::nullopt;
		}
		// The name of a collider is the name of its game object.
		auto name = _managed.Invoke(_b.objectName, *object);
		if (!name || *name == nullptr)
		{
			return std::nullopt;
		}
		return _managed.Text(*name);
	}

	void ClampFocus() override
	{
		auto* transform = Transform();
		if (transform == nullptr)
		{
			return;
		}
		if (auto clamped = _managed.Invoke(_b.clampFocus, _gameManager, {transform}); !clamped)
		{
			_status.SetError(clamped.GetError().message);
		}
	}

	[[nodiscard]] DragFields Drag() const override
	{
		return {.startedTime = ReadField<float>(_gameManager, _b.dragStartedTime),
		        .holdingZoomRotate = ReadField<bool>(_gameManager, _b.holdingZoomRotate),
		        .ignoreHand = ReadField<int32_t>(_gameManager, _b.stageIgnoreHand),
		        .inhibitHand = ReadField<int32_t>(_gameManager, _b.inhibitHandFocus),
		        .lastCursorTerrain = ReadField<WorldPoint>(_gameManager, _b.lastCursorTerrain),
		        .noHandFocus = ReadField<bool>(_gameManager, _b.noHandFocus),
		        .fade = ReadField<float>(_gameManager, _b.dragFade),
		        .zoomRotateChanging = ReadField<bool>(_gameManager, _b.zoomRotateChanging),
		        .dragValid = ReadField<bool>(_gameManager, _b.dragValid),
		        .grab = ReadField<WorldPoint>(_gameManager, _b.dragGrab),
		        .holdCursor = ReadField<WorldPoint>(_gameManager, _b.smoothMouseRaycast),
		        .handCursor = ReadField<WorldPoint>(_gameManager, _b.smoothMouseVisual),
		        .inSubScene = ReadField<float>(_gameManager, _b.subSceneFloor) < k_NoSubSceneFloor};
	}

	void SetDragGrab(WorldPoint grab) override { WriteField(_gameManager, _b.dragGrab, grab); }
	void SetLastCursorTerrain(WorldPoint point) override { WriteField(_gameManager, _b.lastCursorTerrain, point); }
	void SetDragFade(float fade) override { WriteField(_gameManager, _b.dragFade, fade); }
	void SetHoldBlock(int32_t frames) override { WriteField(_gameManager, _b.inhibitHandFocus, frames); }

	[[nodiscard]] std::optional<HandState> Hand() override
	{
		auto hand = _managed.SingletonInstance("PlayerHandManager");
		if (!hand)
		{
			return std::nullopt;
		}
		return HandState {.point = ReadField<WorldPoint>(*hand, _b.handPosition),
		                  .visible = ReadField<bool>(*hand, _b.handVisible),
		                  .overUi = ReadField<bool>(*hand, _b.handOverUi)};
	}

	void SetHandOverride(WorldPoint screen) override
	{
		auto hand = _managed.SingletonInstance("PlayerHandManager");
		if (!hand)
		{
			return;
		}
		if (auto result = _managed.Invoke(_b.setMouseOverride, *hand, {&screen}); !result)
		{
			_status.SetError(result.GetError().message);
		}
	}

	[[nodiscard]] std::optional<bool> ReadMouseButton(ButtonSource source, int32_t button) override
	{
		switch (source)
		{
		case ButtonSource::TurnInput:
		{
			if (_b.inputHeld == nullptr || button != k_MiddleButton)
			{
				return std::nullopt;
			}
			auto function = k_TurnCameraByMouse;
			return Value(_managed.Call<bool>(_b.inputHeld, nullptr, {&function}));
		}
		case ButtonSource::GameButton:
			if (_b.gameMouseButton == nullptr)
			{
				return std::nullopt;
			}
			return Value(_managed.Call<bool>(_b.gameMouseButton, nullptr, {&button}));
		case ButtonSource::Unity:
			return Value(_managed.Call<bool>(_b.getMouseButton, nullptr, {&button}));
		}
		return std::nullopt;
	}
	[[nodiscard]] std::optional<WorldPoint> MousePosition() override
	{
		return Value(_managed.Call<WorldPoint>(_b.mousePosition, nullptr));
	}
	[[nodiscard]] std::optional<WorldPoint> InputPosition() override
	{
		return Value(_managed.Call<WorldPoint>(_b.inputPosition, nullptr));
	}
	[[nodiscard]] std::optional<float> WheelAxis() override
	{
		return Value(_managed.Call<float>(_b.getAxis, nullptr, {_managed.String(k_WheelAxis)}));
	}
	[[nodiscard]] std::optional<bool> IgnoreWheel() override { return Value(_managed.Call<bool>(_b.getIgnoreWheel, nullptr)); }
	[[nodiscard]] bool SetIgnoreWheel(bool ignore) override
	{
		if (auto result = _managed.Invoke(_b.setIgnoreWheel, nullptr, {&ignore}); !result)
		{
			_status.SetError(result.GetError().message);
			return false;
		}
		return true;
	}

	[[nodiscard]] std::optional<WorldPoint> WarpCursor(WorldPoint screen) override
	{
		// During a test of the camera controls, the game reads the mouse of the test: the lock moves that mouse.
		if (_observer != nullptr && _observer->OwnsCursor())
		{
			return _observer->Warp(screen);
		}
		auto size = ScreenSize();
		if (!size)
		{
			return std::nullopt;
		}
		auto warped = _cursor.Warp(screen, static_cast<int32_t>(size->x), static_cast<int32_t>(size->y));
		if (!warped)
		{
			spdlog::debug("camera mod: the cursor stays free: {}", warped.GetError().message);
			return std::nullopt;
		}
		return *warped;
	}

	void SetTurnCursor(WorldPoint screen) override { WriteField(_gameManager, _b.turnCursor, screen); }

	void ReportError(std::string message) override { _status.SetError(std::move(message)); }
	void Debug(std::string message) override { spdlog::debug("{}", message); }

private:
	[[nodiscard]] std::nullopt_t Fail(const std::string& message)
	{
		_status.SetError(message);
		return std::nullopt;
	}

	template <typename T>
	[[nodiscard]] std::optional<T> Value(const moa::Result<T>& result)
	{
		if (!result)
		{
			return Fail(result.GetError().message);
		}
		return *result;
	}

	[[nodiscard]] Il2CppObject* Transform()
	{
		auto transform = _managed.Invoke(_b.componentTransform, ReadField<Il2CppObject*>(_gameManager, _b.camera));
		if (!transform || *transform == nullptr)
		{
			static_cast<void>(Fail(transform ? "the camera has no transform" : transform.GetError().message));
			return nullptr;
		}
		return *transform;
	}

	[[nodiscard]] std::optional<WorldPoint> Unbox(Il2CppObject* hit) const
	{
		if (hit == nullptr)
		{
			return std::nullopt;
		}
		// A nullable value with a value comes back as the boxed value.
		WorldPoint point;
		std::memcpy(&point, _managed.il2cpp_object_unbox(hit), sizeof(point));
		return point;
	}

	const Managed& _managed;
	const CameraBindings& _b;
	ModStatus& _status;
	SystemCursor& _cursor;
	const std::function<void(CameraEventKind, std::string)>& _events;
	CameraFrameObserver* _observer;
	void* _gameManager;
	Il2CppObject* _state;
};
} // namespace

void CameraMod::Before(void* gameManager)
{
	_frameGameManager = gameManager;
	const auto now = std::chrono::steady_clock::now();
	const auto deltaTime = std::chrono::duration<float>(now - _lastFrame).count();
	const auto seconds = std::chrono::duration<double>(now - _start).count();
	_lastFrame = now;
	_frameDeltaTime = std::clamp(deltaTime, 0.0f, 0.1f);
	++_frame;
	TraceStart(gameManager, seconds);
	if (auto mouse = _managed.Call<WorldPoint>(_bindings.mousePosition, nullptr); mouse)
	{
		auto width = _managed.Call<int32_t>(_bindings.screenWidth, nullptr);
		auto height = _managed.Call<int32_t>(_bindings.screenHeight, nullptr);
		if (width && height)
		{
			_cursor->Observe(*mouse, *width, *height);
		}
	}
	auto reason = StandBackReason(gameManager);
	if (reason != _standBack)
	{
		Tell(CameraEventKind::StandBack, reason ? std::format("stand-back ({})", *reason) : std::string("active again"));
		_standBack = reason;
	}
	Observe(gameManager, [&](CameraGame* game) { _observer->BeforeFrame(game); });
	if (reason)
	{
		_loop.StandBack();
		RestoreFade();
		_status.SetActive(false, *reason);
		TraceBefore(gameManager, deltaTime, true);
		return;
	}
	if (_bindings.saveSlot != 0)
	{
		_saveSlot = ReadField<int32_t>(gameManager, _bindings.saveSlot);
	}
	GameAdapter game(_managed, _bindings, _status, *_cursor, gameManager, _loopEvents, _observer);
	static_cast<void>(_loop.Before(game, deltaTime, seconds));
	TraceBefore(gameManager, deltaTime, false);
}

void CameraMod::ApplySettings(const CameraSettings& settings)
{
	RestoreFade();
	_settings = settings;
	// The loop has no state of the old settings, and starts again from the camera of the game.
	_loop.ApplySettings(settings);
}

void CameraMod::StandBack(const std::string& reason)
{
	_loop.StandBack();
	RestoreFade();
	_status.SetActive(false, reason);
}

void CameraMod::RestoreFade()
{
	if (_fadeScene)
	{
		_fade.RestoreAll(*_fadeScene);
	}
}

void CameraMod::StepFade()
{
	const auto settings = FadeSettingsOf(_settings);
	if (!settings.enabled && _fade.Empty())
	{
		return;
	}
	if (!_fadeScene)
	{
		if (std::exchange(_fadeBindTried, true))
		{
			return;
		}
		auto bindings = FadeBindings::Bind(_managed);
		if (!bindings)
		{
			// Fail closed: with a missing name the objects do not fade, and the camera works as before.
			spdlog::error("camera mod: the objects that hide the hand do not fade: {}", bindings.GetError().message);
			return;
		}
		_fadeScene = std::make_unique<GameFadeScene>(_managed, *bindings);
	}
	const auto camera = _loop.ShownPosition();
	if (!camera)
	{
		RestoreFade();
		return;
	}
	_fade.Step(*_fadeScene, settings, {.camera = *camera}, _frameDeltaTime);
	if (_traceRow)
	{
		auto& row = _trace->Row();
		row.fadedObjects = _fade.Faded();
		row.hiddenObjects = _fade.Hidden();
		row.fadeOpacity = _fade.LowestOpacity();
	}
}

void CameraMod::After(void* gameManager)
{
	_frameGameManager = gameManager;
	TraceAfterGame(gameManager);
	const auto standBack = AfterLoop(gameManager);
	Observe(gameManager, [&](CameraGame* game) { _observer->AfterFrame(game, standBack); });
	TraceEnd(gameManager, std::chrono::duration<double>(std::chrono::steady_clock::now() - _start).count());
}

template <typename Function>
void CameraMod::Observe(void* gameManager, Function&& function)
{
	if (_observer == nullptr)
	{
		return;
	}
	if (gameManager != nullptr && ReadField<Il2CppObject*>(gameManager, _bindings.state) != nullptr)
	{
		GameAdapter game(_managed, _bindings, _status, *_cursor, gameManager, _loopEvents, _observer);
		function(&game);
	}
	else
	{
		function(nullptr);
	}
}

std::optional<std::string> CameraMod::AfterLoop(void* gameManager)
{
	GameAdapter game(_managed, _bindings, _status, *_cursor, gameManager, _loopEvents, _observer);
	if (!_loop.EndFrame(game))
	{
		return _standBack.value_or("the camera loop did not start this frame");
	}
	if (auto reason = StandBackReason(gameManager))
	{
		_loop.Resync();
		_status.SetActive(false, *reason);
		return reason;
	}
	std::optional<std::string> result;
	if (_loop.After(game))
	{
		StepFade();
		_status.SetActive(true);
		if (_traceRow)
		{
			_trace->Row().active = true;
		}
	}
	else
	{
		result = "the camera loop did not change the frame";
	}
	if (const auto& warped = _loop.Trace().warpedMouse)
	{
		// The move of the mouse in the next row of the trace starts from the place where the rig put the cursor.
		_lastMouse = *warped;
	}
	return result;
}

void CameraMod::StartTrace(CameraTraceRecorder::Sink sink, std::string_view comment)
{
	if (!_trace)
	{
		return;
	}
	_trace->OpenFile(std::move(sink));
	_trace->WriteHeader(comment);
}

void CameraMod::SetBlackBox(std::filesystem::path folder, std::string header)
{
	_blackBoxFolder = std::move(folder);
	if (_blackBox)
	{
		_blackBox->SetHeader(std::move(header));
	}
}

nlohmann::json CameraMod::Events(size_t count) const
{
	return _events.RecentJson(count);
}

void CameraMod::Tell(CameraEventKind kind, std::string text)
{
	if (_traceRow && _trace)
	{
		_trace->Row().modEvents.Append(text);
	}
	_loopEvents(kind, std::move(text));
}

bool CameraMod::WriteBlackBox(std::string_view name, std::string_view text)
{
	if (_blackBoxFolder.empty())
	{
		return false;
	}
	std::string slug(name);
	std::ranges::replace_if(slug, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) == 0; }, '-');
	std::error_code error;
	std::filesystem::create_directories(_blackBoxFolder, error);
	const auto path =
	    _blackBoxFolder / std::format("camera-blackbox-{:%Y%m%d-%H%M%S}-{}.csv",
	                                  std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()), slug);
	std::FILE* file = nullptr;
	if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr)
	{
		spdlog::error("camera black box: the mod cannot open {}", path.string());
		return false;
	}
	const auto written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
	std::fclose(file);
	Tell(CameraEventKind::BlackBox, std::format("black box: wrote the last {} rows to {}", _blackBox->Size(), path.string()));
	return written;
}

std::string CameraMod::BlackBoxText(std::string_view cause) const
{
	return _blackBox ? _blackBox->Text(cause) : std::string();
}

moa::Result<void> CameraMod::BlendCamera(WorldPoint focus, float direction, float stage, float seconds)
{
	if (_bindings.blendCameraTo == nullptr)
	{
		return moa::MakeError("the game has no 'GameManager.BlendCameraTo'");
	}
	if (_frameGameManager == nullptr)
	{
		return moa::MakeError("no frame of the camera mod ran");
	}
	// The game keeps the new focus, heading and stage, and blends its camera from the camera of now. No action at the end.
	auto blended = _managed.Invoke(_bindings.blendCameraTo, _frameGameManager, {&focus, &direction, &stage, &seconds, nullptr});
	if (!blended)
	{
		return blended.GetError();
	}
	return {};
}

std::optional<HeldObjectLook> CameraMod::HeldLook()
{
	if (!_fadeScene)
	{
		return std::nullopt;
	}
	return _fadeScene->LookOfHeldObject();
}

std::optional<bool> CameraMod::HoldsObject() const
{
	if (_currentDragObject == nullptr)
	{
		return std::nullopt;
	}
	auto object = _managed.Invoke(_currentDragObject, nullptr);
	if (!object)
	{
		return std::nullopt;
	}
	return *object != nullptr;
}

void CameraMod::BindTraceNames()
{
	// The frame count of Unity is only for the trace. If the game does not have it, the column stays empty.
	if (auto time = _managed.Class("UnityEngine", "Time"); time)
	{
		if (auto method = _managed.Method(*time, "get_frameCount", {}, "System.Int32", true); method)
		{
			_unityFrameCount = *method;
		}
	}
	// The object in the hand and the land drag of the hand are only for the trace. If the game does not have them, the
	// columns stay empty.
	if (auto utilities = _managed.Class("", "InputUtilities"); utilities)
	{
		if (auto method = _managed.Method(*utilities, "GetCurrentDragObject", {}, "UnityEngine.GameObject", true); method)
		{
			_currentDragObject = *method;
		}
	}
	if (auto hand = _managed.Class("", "PlayerHandManager"); hand)
	{
		if (auto field = _managed.Field(*hand, "grabbingLand", "System.Boolean"); field)
		{
			_grabbingLand = *field;
		}
	}
}

void CameraMod::CloseTrace()
{
	if (_trace)
	{
		_trace->Close();
	}
}

void CameraMod::StopTrace()
{
	CloseTrace();
}

void CameraMod::TraceRender(void* camera)
{
	if (!_traceRow || camera == nullptr)
	{
		return;
	}
	Guard([&] {
		const auto& b = _bindings;
		auto& row = _trace->Row();
		auto* object = static_cast<Il2CppObject*>(camera);
		const auto pose = ReadCameraObject(object);
		if (object != _gameCamera)
		{
			++row.otherRenders;
			if (pose)
			{
				row.otherRenderRoll = std::max(row.otherRenderRoll.value_or(0.0f), std::abs(RollDegrees(pose->rotation)));
			}
			return;
		}
		if (row.renders++ > 0)
		{
			return;
		}
		if (_renderSamples++ == 0)
		{
			spdlog::info("camera trace: the first render sample, in frame {}", row.frame);
		}
		row.cameraRender = pose;
		auto transform = _managed.Invoke(b.componentTransform, object);
		if (!transform || *transform == nullptr)
		{
			return;
		}
		if (auto local = _managed.Call<Quaternion>(b.getLocalRotation, *transform); local)
		{
			row.renderLocalRoll = RollDegrees(*local);
		}
		// Count the parents, and take the roll of the world rotation of the first parent.
		int32_t parents = 0;
		auto parent = _managed.Invoke(b.getParent, *transform);
		while (parent && *parent != nullptr && parents < 64)
		{
			if (parents == 0)
			{
				if (auto rotation = _managed.Call<Quaternion>(b.getRotation, *parent); rotation)
				{
					row.renderParentRoll = RollDegrees(*rotation);
				}
			}
			++parents;
			parent = _managed.Invoke(b.getParent, *parent);
		}
		row.renderParents = parents;
		// The first row of the view matrix is the right side of the camera in the world. Its height gives the roll.
		// Unity keeps the matrix by columns, so the second value of the first row is at index 4.
		if (auto matrix = _managed.Call<std::array<float, 16>>(b.worldToCameraMatrix, object); matrix)
		{
			row.renderMatrixRoll = std::asin(std::clamp((*matrix)[4], -1.0f, 1.0f)) / k_DegreesToRadians;
		}
	});
}

void CameraMod::EnableRenderSample(const MethodInfo* hooked)
{
	_renderMethod = hooked;
	_renderTried = false;
}

void CameraMod::RegisterRenderSample()
{
	if (_renderMethod == nullptr || _renderTried || _gameCamera == nullptr)
	{
		return;
	}
	_renderTried = true;
	if (auto error = AddRenderCallback())
	{
		spdlog::error("camera trace: no render sample: {}", *error);
		return;
	}
	_renderAddedFrame = _frame;
	spdlog::info("camera trace: the mod gave the render pipeline a callback that calls the render hook");
}

void CameraMod::CheckRenderSamples()
{
	constexpr uint64_t k_SilentFrames = 120;
	if (!_renderAddedFrame || _renderSamples > 0 || _renderSilenceLogged || _frame < *_renderAddedFrame + k_SilentFrames)
	{
		return;
	}
	_renderSilenceLogged = true;
	spdlog::error("camera trace: no render sample in {} frames after the mod added the render callback", k_SilentFrames);
}

std::optional<std::string> CameraMod::AddRenderCallback()
{
	const auto& m = _managed;
	if (m.il2cpp_method_get_object == nullptr || m.il2cpp_gchandle_new == nullptr || m.il2cpp_gchandle_free == nullptr)
	{
		return "GameAssembly.dll does not export il2cpp_method_get_object and the GC handle functions";
	}
	constexpr const char* k_Action = "System.Action<UnityEngine.Rendering.ScriptableRenderContext,UnityEngine.Camera>";
	auto pipeline = m.Class("UnityEngine.Rendering", "RenderPipelineManager");
	auto delegateClass = m.Class("System", "Delegate");
	auto component = m.Class("UnityEngine", "Component");
	auto shake = m.Class("", "EffectCameraShake");
	if (!pipeline || !delegateClass || !component || !shake)
	{
		return "a class of the render callback is missing";
	}
	auto add = m.Method(*pipeline, "add_beginCameraRendering", {k_Action}, "System.Void", true);
	auto remove = m.Method(*pipeline, "remove_beginCameraRendering", {k_Action}, "System.Void", true);
	auto create = m.Method(*delegateClass, "CreateDelegate", {"System.Type", "System.Object", "System.Reflection.MethodInfo"},
	                       "System.Delegate", true);
	auto getComponent = m.Method(*component, "GetComponent", {"System.Type"}, "UnityEngine.Component", false);
	if (!add || !remove || !create || !getComponent)
	{
		return "a method of the render callback is missing";
	}
	// The target is the camera shake component of the camera of the game. A second call of its method before a
	// render changes nothing, because the method applies the shake only one time.
	auto target = m.Invoke(*getComponent, _gameCamera, {m.TypeObject(*shake)});
	if (!target)
	{
		return target.GetError().message;
	}
	auto* shakeComponent = *target;
	if (shakeComponent == nullptr)
	{
		// The game adds the camera shake to its camera at the first shake, and keeps it after that. The mod adds it
		// in the same way now. The callback that the component gives to the render pipeline does not call the hook
		// (no render sample in run 7), so the mod also adds its own callback below.
		auto gameObjectClass = m.Class("UnityEngine", "GameObject");
		if (!gameObjectClass)
		{
			return gameObjectClass.GetError().message;
		}
		auto getGameObject = m.Method(*component, "get_gameObject", {}, "UnityEngine.GameObject", false);
		auto addComponent = m.Method(*gameObjectClass, "AddComponent", {"System.Type"}, "UnityEngine.Component", false);
		if (!getGameObject || !addComponent)
		{
			return "a method to add the camera shake is missing";
		}
		auto owner = m.Invoke(*getGameObject, _gameCamera);
		if (!owner || *owner == nullptr)
		{
			return owner ? std::string("the camera of the game has no game object") : owner.GetError().message;
		}
		auto added = m.Invoke(*addComponent, *owner, {m.TypeObject(*shake)});
		if (!added || *added == nullptr)
		{
			return added ? std::string("Unity did not add the camera shake") : added.GetError().message;
		}
		spdlog::info("camera trace: the mod added the camera shake of the game to its camera, as the game does at "
		             "the first shake");
		shakeComponent = *added;
	}
	// The runtime makes the delegate. It copies the code pointer of the method now, and the hook already replaced that
	// pointer, so the delegate calls the hook.
	auto* actionType = m.il2cpp_type_get_object(m.Api().il2cpp_method_get_param(*add, 0));
	auto* method = m.il2cpp_method_get_object(_renderMethod, nullptr);
	if (actionType == nullptr || method == nullptr)
	{
		return "the runtime gave no type object or no method object";
	}
	auto made = m.Invoke(*create, nullptr, {actionType, shakeComponent, method});
	if (!made || *made == nullptr)
	{
		return made ? std::string("the runtime made no delegate") : made.GetError().message;
	}
	// Do not add a delegate that does not call the hook.
	const auto* code = ReadField<const void*>(_renderMethod, 0);
	const auto size = static_cast<size_t>(m.Api().il2cpp_class_instance_size(m.il2cpp_object_get_class(*made)));
	bool callsHook = false;
	for (size_t offset = sizeof(void*); offset + sizeof(void*) <= size; offset += sizeof(void*))
	{
		callsHook = callsHook || ReadField<const void*>(*made, offset) == code;
	}
	if (!callsHook)
	{
		return "the delegate does not call the render hook";
	}
	_renderHandle = m.il2cpp_gchandle_new(*made, false);
	if (auto added = m.Invoke(*add, nullptr, {*made}); !added)
	{
		m.il2cpp_gchandle_free(_renderHandle);
		_renderHandle = 0;
		return added.GetError().message;
	}
	_renderDelegate = *made;
	_removeBeginCamera = *remove;
	return std::nullopt;
}

void CameraMod::RemoveRenderSample()
{
	if (_renderDelegate != nullptr)
	{
		if (auto removed = _managed.Invoke(_removeBeginCamera, nullptr, {_renderDelegate}); !removed)
		{
			spdlog::error("camera trace: the mod could not remove the render callback: {}", removed.GetError().message);
		}
		_managed.il2cpp_gchandle_free(_renderHandle);
		_renderHandle = 0;
		_renderDelegate = nullptr;
	}
	if (_renderTried)
	{
		RemoveCameraShake();
	}
}

void CameraMod::RemoveCameraShake()
{
	// A camera shake that started after the install of the hook gave the render pipeline callbacks with the code of
	// the hook. The mod removes the component now, and its callbacks go with it. The game adds the component again at
	// its next shake, with the code of the game.
	const auto& m = _managed;
	auto object = m.Class("UnityEngine", "Object");
	auto component = m.Class("UnityEngine", "Component");
	auto shake = m.Class("", "EffectCameraShake");
	if (!object || !component || !shake)
	{
		spdlog::error("camera trace: a class to remove the camera shake is missing");
		return;
	}
	auto destroy = m.Method(*object, "DestroyImmediate", {"UnityEngine.Object"}, "System.Void", true);
	auto getComponent = m.Method(*component, "GetComponent", {"System.Type"}, "UnityEngine.Component", false);
	auto main = m.Invoke(_bindings.mainCamera, nullptr);
	if (!destroy || !getComponent || !main || *main == nullptr)
	{
		spdlog::error("camera trace: the mod could not find the camera shake to remove");
		return;
	}
	auto target = m.Invoke(*getComponent, *main, {m.TypeObject(*shake)});
	if (!target || *target == nullptr)
	{
		return;
	}
	if (auto removed = m.Invoke(*destroy, nullptr, {*target}); !removed)
	{
		spdlog::error("camera trace: the mod could not remove the camera shake: {}", removed.GetError().message);
		return;
	}
	spdlog::info("camera trace: the mod removed the camera shake of the game and its render callbacks");
}

template <typename Function>
void CameraMod::Guard(Function&& function)
{
	if (!_trace)
	{
		_traceRow = false;
		return;
	}
	// An error in the trace must not stop the camera mod or the game. The trace stops, and the mod continues.
	try
	{
		function();
	}
	catch (const std::exception& error)
	{
		spdlog::error("camera trace: {}; the trace stops", error.what());
		_trace.reset();
		_traceRow = false;
	}
	catch (...)
	{
		spdlog::error("camera trace: an unknown error; the trace stops");
		_trace.reset();
		_traceRow = false;
	}
}

std::optional<CameraPlacement> CameraMod::ReadCamera(void* gameManager) const
{
	if (gameManager == nullptr)
	{
		return std::nullopt;
	}
	return ReadCameraObject(ReadField<Il2CppObject*>(gameManager, _bindings.camera));
}

std::optional<CameraPlacement> CameraMod::ReadCameraObject(Il2CppObject* camera) const
{
	if (camera == nullptr)
	{
		return std::nullopt;
	}
	auto transform = _managed.Invoke(_bindings.componentTransform, camera);
	if (!transform || *transform == nullptr)
	{
		return std::nullopt;
	}
	auto position = _managed.Call<WorldPoint>(_bindings.getPosition, *transform);
	auto rotation = _managed.Call<Quaternion>(_bindings.getRotation, *transform);
	if (!position || !rotation)
	{
		return std::nullopt;
	}
	return CameraPlacement {.position = *position, .rotation = *rotation};
}

void CameraMod::TraceStart(void* gameManager, double seconds)
{
	Guard([&] {
		const auto& b = _bindings;
		std::array<bool, 3> buttons {};
		for (int32_t button = 0; button < 3; ++button)
		{
			auto held = _managed.Call<bool>(b.getMouseButton, nullptr, {&button});
			buttons[static_cast<size_t>(button)] = held && *held;
		}
		_traceRow = _trace->StartFrame(seconds, buttons[0] || buttons[1] || buttons[2], ReadCamera(gameManager));
		auto mouse = _managed.Call<WorldPoint>(b.mousePosition, nullptr);
		const auto lastMouse = std::exchange(_lastMouse, mouse ? std::optional<WorldPoint>(*mouse) : std::nullopt);
		if (!_traceRow)
		{
			return;
		}
		auto& row = _trace->Row();
		row.frame = _frame;
		row.buttons = buttons;
		if (mouse)
		{
			row.mouse = *mouse;
			if (lastMouse)
			{
				row.mouseDelta = *mouse - *lastMouse;
			}
		}
		if (_unityFrameCount != nullptr)
		{
			if (auto count = _managed.Call<int32_t>(_unityFrameCount, nullptr); count)
			{
				row.unityFrame = *count;
			}
		}
		if (auto wheel = _managed.Call<float>(b.getAxis, nullptr, {_managed.String(k_WheelAxis)}); wheel)
		{
			row.wheel = *wheel;
		}
		if (auto input = _managed.Call<WorldPoint>(b.inputPosition, nullptr); input)
		{
			row.inputPosition = *input;
		}
		if (_currentDragObject != nullptr)
		{
			if (auto held = _managed.Invoke(_currentDragObject, nullptr); held)
			{
				row.holdsObject = *held != nullptr;
			}
		}
		if (gameManager != nullptr)
		{
			row.inhibitAtStart = ReadField<int32_t>(gameManager, b.inhibitHandFocus);
			row.rotateMouse = ReadField<WorldPoint>(gameManager, b.turnCursor);
		}
		// The input from each reader of the game, so that a row shows when they do not agree.
		auto read = [this](const MethodInfo* method, int32_t button) -> std::optional<bool> {
			if (method == nullptr)
			{
				return std::nullopt;
			}
			auto held = _managed.Call<bool>(method, nullptr, {&button});
			return held ? std::optional<bool>(*held) : std::nullopt;
		};
		row.gameLeft = read(b.gameMouseButton, k_LeftButton);
		row.gameRight = read(b.gameMouseButton, k_RightButton);
		row.gameMiddle = read(b.gameMouseButton, k_MiddleButton);
		row.utilityMiddle = read(b.utilityMouseButton, k_MiddleButton);
		row.turnInput = read(b.inputHeld, k_TurnCameraByMouse);
		if (b.uiInputActive != nullptr)
		{
			if (auto active = _managed.Call<bool>(b.uiInputActive, nullptr); active)
			{
				row.uiInputActive = *active;
			}
		}
		if (auto field = _managed.Call<bool>(b.inputFieldActive, nullptr); field)
		{
			row.inputFieldActive = *field;
		}
		// Tell when the middle button of Unity and the turn input of the game do not agree at a change of the button.
		const auto unityMiddle = buttons[k_MiddleButton];
		if (std::exchange(_unityMiddle, unityMiddle) != unityMiddle)
		{
			if (row.turnInput && *row.turnInput != unityMiddle)
			{
				Tell(CameraEventKind::Input,
				     std::format("input: the middle button of Unity is {}, but the turn input of the game is {}",
				                 unityMiddle ? "down" : "up", *row.turnInput ? "down" : "up"));
			}
			if (unityMiddle && row.gameMiddle && !*row.gameMiddle && !std::exchange(_gameMiddleTold, true))
			{
				Tell(CameraEventKind::Input, "input: the mouse button of the game does not give the middle button (Steam "
				                             "Input); the mod reads the turn input of the game");
			}
		}
	});
}

void CameraMod::TraceBefore(void* gameManager, float deltaTime, bool standBack)
{
	if (!_traceRow)
	{
		return;
	}
	Guard([&] {
		const auto& b = _bindings;
		auto& row = _trace->Row();
		row.deltaTime = deltaTime;
		row.standBack = standBack;
		if (!standBack)
		{
			row.loop = _loop.Trace();
		}
		if (gameManager == nullptr)
		{
			return;
		}
		if (auto* state = ReadField<Il2CppObject*>(gameManager, b.state); state != nullptr)
		{
			row.focusBefore = ReadField<WorldPoint>(state, b.focus);
			row.stageBefore = ReadField<float>(state, b.stage);
			row.directionBefore = ReadField<float>(state, b.direction);
		}
		row.tiltBefore = ReadField<float>(gameManager, b.lastTilt);
		row.heightBefore = ReadField<float>(gameManager, b.lastHeight);
		row.cameraBefore = ReadCamera(gameManager);
		row.inhibitBefore = ReadField<int32_t>(gameManager, b.inhibitHandFocus);
		row.ignoreHandBefore = ReadField<int32_t>(gameManager, b.stageIgnoreHand);
		row.noHandFocus = ReadField<bool>(gameManager, b.noHandFocus);
		row.lastCursorTerrainBefore = ReadField<WorldPoint>(gameManager, b.lastCursorTerrain);
		row.dragFadeBefore = ReadField<float>(gameManager, b.dragFade);
	});
}

void CameraMod::TraceAfterGame(void* gameManager)
{
	if (!_traceRow || gameManager == nullptr)
	{
		return;
	}
	Guard([&] {
		const auto& b = _bindings;
		auto& row = _trace->Row();
		if (auto* state = ReadField<Il2CppObject*>(gameManager, b.state); state != nullptr)
		{
			row.focusAfterGame = ReadField<WorldPoint>(state, b.focus);
			row.stageAfterGame = ReadField<float>(state, b.stage);
			row.directionAfterGame = ReadField<float>(state, b.direction);
		}
		row.tiltAfterGame = ReadField<float>(gameManager, b.lastTilt);
		row.heightAfterGame = ReadField<float>(gameManager, b.lastHeight);
		row.cameraAfterGame = ReadCamera(gameManager);
		row.dragFade = ReadField<float>(gameManager, b.dragFade);
		row.zoomRotateChanging = ReadField<bool>(gameManager, b.zoomRotateChanging);
		row.holdingZoomRotate = ReadField<bool>(gameManager, b.holdingZoomRotate);
		row.dragValid = ReadField<bool>(gameManager, b.dragValid);
		row.inhibitAfterGame = ReadField<int32_t>(gameManager, b.inhibitHandFocus);
		row.dragGrab = ReadField<WorldPoint>(gameManager, b.dragGrab);
		row.dragStartedTime = ReadField<float>(gameManager, b.dragStartedTime);
		row.lastCursorTerrainAfter = ReadField<WorldPoint>(gameManager, b.lastCursorTerrain);
		row.holdCursor = ReadField<WorldPoint>(gameManager, b.smoothMouseRaycast);
		row.minimumZoom = ReadField<float>(gameManager, b.minimumZoom);
		if (_grabbingLand != 0)
		{
			if (auto hand = _managed.SingletonInstance("PlayerHandManager"); hand && *hand != nullptr)
			{
				row.grabbingLand = ReadField<bool>(*hand, _grabbingLand);
			}
		}
		if (row.cameraAfterGame)
		{
			auto position = row.cameraAfterGame->position;
			if (auto ground = _managed.Call<float>(b.highestGround, gameManager, {&position}); ground)
			{
				row.highestGround = *ground;
			}
		}
	});
}

void CameraMod::TraceEnd(void* gameManager, double seconds)
{
	Guard([&] {
		const auto& b = _bindings;
		if (_traceRow && gameManager != nullptr)
		{
			auto& row = _trace->Row();
			if (row.active)
			{
				row.loop = _loop.Trace();
			}
			if (auto* state = ReadField<Il2CppObject*>(gameManager, b.state); state != nullptr)
			{
				row.focusFinal = ReadField<WorldPoint>(state, b.focus);
			}
			_gameCamera = ReadField<Il2CppObject*>(gameManager, b.camera);
			row.cameraFinal = ReadCameraObject(_gameCamera);
			if (_trace->FileOpen())
			{
				// The render sample is only for the trace file.
				RegisterRenderSample();
				CheckRenderSamples();
			}
			if (row.cameraFinal)
			{
				// The exact land under the camera, the land of the floor of the spring arm.
				auto position = row.cameraFinal->position;
				if (b.cameraGroundHeight != nullptr)
				{
					if (auto ground = _managed.Call<WorldPoint>(b.cameraGroundHeight, gameManager, {&position}); ground)
					{
						row.landAtFinal = ground->y;
					}
				}
				else if (auto ground = _managed.Call<float>(b.highestGround, gameManager, {&position}); ground)
				{
					row.landAtFinal = *ground - k_HighestGroundMargin;
				}
			}
			if (row.loop.pivot && _gameCamera != nullptr)
			{
				// The place of the pivot of the hand rig on the screen, for the drift of the pivot in each gesture.
				auto pivot = *row.loop.pivot;
				if (auto screen = _managed.Call<WorldPoint>(b.worldToScreenPoint, _gameCamera, {&pivot}); screen)
				{
					row.pivotScreen = *screen;
				}
			}
			if (auto main = _managed.Invoke(b.mainCamera, nullptr); main && *main != nullptr)
			{
				row.mainIsGameCamera = *main == _gameCamera;
				if (auto pose = ReadCameraObject(*main))
				{
					row.mainCameraRoll = RollDegrees(pose->rotation);
				}
			}
			if (row.mouse)
			{
				auto mouse = *row.mouse;
				bool clampToSeaLevel = false;
				if (auto hit = _managed.Invoke(b.raycastTerrain, gameManager, {&mouse, &clampToSeaLevel});
				    hit && *hit != nullptr)
				{
					// A nullable value with a value comes back as the boxed value.
					WorldPoint point;
					std::memcpy(&point, _managed.il2cpp_object_unbox(*hit), sizeof(point));
					row.cursorLand = point;
				}
			}
			if (auto hand = _managed.SingletonInstance("PlayerHandManager"); hand && *hand != nullptr)
			{
				row.hand = ReadField<WorldPoint>(*hand, b.handPosition);
				row.handOverUi = ReadField<bool>(*hand, b.handOverUi);
			}
		}
		_trace->EndFrame(seconds);
	});
}

nlohmann::json CameraMod::State(void* gameManager) const
{
	auto result = DescribeCamera(_managed, _bindings, gameManager);
	const auto reason = StandBackReason(gameManager);
	auto mod = _loop.State();
	mod["standBackReason"] = reason ? *reason : "";
	mod["gameGrabPoint"] =
	    gameManager != nullptr ? ToJson(ReadField<WorldPoint>(gameManager, _bindings.dragGrab)) : nlohmann::json();
	mod["settings"] = CameraSettingsJson(_settings);
	mod["fade"] = _fade.State();
	mod["fade"]["materialCopies"] = _fadeScene ? _fadeScene->MaterialCopies() : 0U;
	mod["fade"]["error"] = _fadeScene ? _fadeScene->LastError() : std::string();
	mod["fade"]["names"] = _fadeScene ? _fadeScene->Describe() : nlohmann::json::array();
	mod["flying"] = _loop.Flying();
	mod["spotKey"] = SpotKey();
	mod["spotsSaved"] = SavedSpots();
	mod["spotsPath"] = CameraSpotsPath().string();
	if (_trace)
	{
		mod["trace"] = {
		    {"file", _trace->FileOpen()}, {"rows", _trace->Rows()}, {"bytes", _trace->Bytes()}, {"stopped", _trace->Stopped()}};
	}
	if (_blackBox)
	{
		mod["blackBox"] = {{"rows", _blackBox->Size()}, {"dumps", _blackBox->Dumps()}, {"folder", _blackBoxFolder.string()}};
	}
	mod["events"] = _events.Size();
	result["mod"] = std::move(mod);
	return result;
}

std::filesystem::path CameraSpotsPath()
{
	std::array<wchar_t, MAX_PATH> buffer {};
	const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
	const std::filesystem::path root =
	    length > 0 && length < buffer.size() ? std::filesystem::path(buffer.data()) : std::filesystem::path(".");
	return root / "MastersOfAlbionReforged" / "camera-spots.json";
}

std::string CameraMod::SpotKey() const
{
	return _saveSlot >= 0 ? std::format("slot {}", _saveSlot) : std::string("default");
}

bool CameraMod::LoadSpots()
{
	if (_spotsLoaded)
	{
		return !_spotsFileBad;
	}
	_spotsLoaded = true;
	const auto path = CameraSpotsPath();
	const auto text = ReadTextFile(path);
	if (!text)
	{
		return true;
	}
	auto book = ParseCameraSpots(*text);
	if (!book)
	{
		spdlog::error("camera spots: {}: {}; the mod does not change the file", path.string(), book.GetError().message);
		_spotsFileBad = true;
		return false;
	}
	_spots = std::move(*book);
	return true;
}

std::string CameraMod::SaveSpot(size_t index)
{
	if (index >= k_CameraSpotCount)
	{
		return "Camera spot: no such spot";
	}
	const auto number = index + 1;
	auto spot = _loop.CurrentSpot();
	if (!spot)
	{
		return std::format("Camera spot {}: not saved (the hand rig has no view)", number);
	}
	if (!LoadSpots())
	{
		return std::format("Camera spot {}: not saved (camera-spots.json is not correct, see the log)", number);
	}
	_spots[SpotKey()][index] = *spot;
	const auto path = CameraSpotsPath();
	std::error_code error;
	std::filesystem::create_directories(path.parent_path(), error);
	if (auto written = WriteTextFileAtomically(path, CameraSpotsJson(_spots).dump(2) + "\n"); !written)
	{
		spdlog::error("camera spots: {}: {}", path.string(), written.GetError().message);
		return std::format("Camera spot {}: saved for this session only (see the log)", number);
	}
	spdlog::info("camera spots: spot {} of {} at ({:.1f}, {:.1f}, {:.1f})", number, SpotKey(), spot->position.x,
	             spot->position.y, spot->position.z);
	return std::format("Camera spot {}: saved", number);
}

std::string CameraMod::GoToSpot(size_t index)
{
	if (index >= k_CameraSpotCount)
	{
		return "Camera spot: no such spot";
	}
	const auto number = index + 1;
	static_cast<void>(LoadSpots());
	const auto found = _spots.find(SpotKey());
	if (found == _spots.end() || !found->second[index])
	{
		return std::format("Camera spot {}: empty (Ctrl and the key save it)", number);
	}
	if (!_loop.FlyToSpot(*found->second[index]))
	{
		return std::format("Camera spot {}: only with the pitch mode hand", number);
	}
	return std::format("Camera spot {}", number);
}

std::string CameraMod::ResetView(double northDegrees)
{
	if (!_loop.ResetView(static_cast<float>(northDegrees)))
	{
		return "Camera view: only with the pitch mode hand";
	}
	return "Camera view: north at the top";
}

std::array<bool, k_CameraSpotCount> CameraMod::SavedSpots() const
{
	std::array<bool, k_CameraSpotCount> saved {};
	if (const auto found = _spots.find(SpotKey()); found != _spots.end())
	{
		for (size_t index = 0; index < saved.size(); ++index)
		{
			saved[index] = found->second[index].has_value();
		}
	}
	return saved;
}

nlohmann::json CameraSettingsJson(const CameraSettings& settings)
{
	return {
	    {"pivotOnHand", settings.pivotOnHand},
	    {"smoothZoom", settings.smoothZoom},
	    {"zoomSmoothing", settings.zoomSmoothing},
	    {"rotateSmoothing", settings.rotateSmoothing},
	    {"wheelZoomStep", settings.wheelZoomStep},
	    {"invertRotate", settings.invertRotate},
	    {"lookUp", settings.lookUp},
	    {"maxLookUpDegrees", settings.maxLookUpDegrees},
	    {"pitchSensitivity", settings.pitchSensitivity},
	    {"lookUpFullStage", settings.lookUpFullStage},
	    {"lookUpZeroStage", settings.lookUpZeroStage},
	    {"invertPitch", settings.invertPitch},
	    {"pitch", settings.pitch},
	    {"minTiltDegrees", settings.minTiltDegrees},
	    {"maxTiltDegrees", settings.maxTiltDegrees},
	    {"pitchSmoothing", settings.pitchSmoothing},
	    {"pitchResetOnZoom", settings.pitchResetOnZoom},
	    {"resetPitchOnDoubleClick", settings.resetPitchOnDoubleClick},
	    {"groundClearance", settings.groundClearance},
	    {"pitchMode", PitchModeName(settings.pitchMode)},
	    {"pitchTurnLock", settings.pitchTurnLock},
	    {"lockCursor", settings.lockCursor},
	    {"maxPivotDistance", settings.maxPivotDistance},
	    {"springArm", settings.springArm},
	    {"collisionInSmoothing", settings.collisionInSmoothing},
	    {"collisionOutSmoothing", settings.collisionOutSmoothing},
	    {"collisionMargin", settings.collisionMargin},
	    {"objectCollision", settings.objectCollision},
	    {"armLandSamples", settings.armLandSamples},
	    {"cameraClearanceRadius", settings.cameraClearanceRadius},
	    {"terrainPushSmoothing", settings.terrainPushSmoothing},
	    {"terrainReturnSmoothing", settings.terrainReturnSmoothing},
	    {"flyBackAfterStandBack", settings.flyBackAfterStandBack},
	    {"dragInertia", settings.dragInertia},
	    {"dragInertiaMaxSpeed", settings.dragInertiaMaxSpeed},
	    {"turnInertia", settings.turnInertia},
	    {"pitchInertia", settings.pitchInertia},
	    {"continuousZoom", settings.continuousZoom},
	    {"zoomStepRatio", settings.zoomStepRatio},
	    {"panSpeed", settings.panSpeed},
	    {"edgeScroll", settings.edgeScroll},
	    {"edgeMargin", settings.edgeMargin},
	    {"panAvoidance", settings.panAvoidance},
	    {"flyTo", settings.flyTo},
	    {"flyToSeconds", settings.flyToSeconds},
	    {"flyToStage", settings.flyToStage},
	    {"groundLook", settings.groundLook},
	    {"groundEyeHeight", settings.groundEyeHeight},
	    {"groundPitchDownDegrees", settings.groundPitchDownDegrees},
	    {"groundPitchUpDegrees", settings.groundPitchUpDegrees},
	    {"groundWalkSpeed", settings.groundWalkSpeed},
	    {"chaseSeconds", settings.chaseSeconds},
	    {"chaseMaxSpeed", settings.chaseMaxSpeed},
	    {"chaseMinSpeed", settings.chaseMinSpeed},
	    {"landGrip", settings.landGrip},
	    {"gripStepPerPixel", settings.gripStepPerPixel},
	    {"gripMinimumStep", settings.gripMinimumStep},
	    {"mouseSmoothing", settings.mouseSmoothing},
	    {"fadeOccluders", settings.fadeOccluders},
	    {"fadeAlpha", settings.fadeAlpha},
	    {"fadeSeconds", settings.fadeSeconds},
	    {"fadeDistance", settings.fadeDistance},
	    {"hideCameraInside", settings.hideCameraInside},
	    {"trace", settings.trace},
	    {"traceFolder", settings.traceFolder},
	};
}

nlohmann::json DescribeCamera(const Managed& managed, const CameraBindings& b, void* gameManager)
{
	if (gameManager == nullptr)
	{
		return {{"error", "there is no GameManager"}};
	}
	auto flag = [&](const MethodInfo* method, bool isStatic) {
		return ToJson(managed.Call<bool>(method, isStatic ? nullptr : gameManager));
	};
	nlohmann::json result {
	    {"dataReady", ReadField<bool>(gameManager, b.dataReady)},
	    {"overrides",
	     {
	         {"freeCameraMode", ReadField<bool>(gameManager, b.freeCamera)},
	         {"cameraPanSequenceInProgress", ReadField<int32_t>(gameManager, b.panSequence)},
	         {"cameraTrackingFocus", ReadField<Il2CppObject*>(gameManager, b.trackingFocus) != nullptr},
	         {"cameraOverrideTransform", ReadField<Il2CppObject*>(gameManager, b.overrideTransform) != nullptr},
	         {"cameraTransition", ReadField<Il2CppObject*>(gameManager, b.transition) != nullptr},
	         {"cameraUserInputLocked", ReadField<bool>(gameManager, b.inputLocked)},
	         {"cameraBlendFromT", ReadField<float>(gameManager, b.blendFromT)},
	         {"focusClampForwardSize", ReadField<float>(gameManager, b.clampForwardSize)},
	         {"isInForceZoomOut", ReadField<bool>(gameManager, b.forceZoomOut)},
	         {"IsCameraBeingOverridden", flag(b.isOverridden, false)},
	         {"IsCameraClamped", flag(b.isClamped, false)},
	         {"IsPossessing", flag(b.isPossessing, false)},
	         {"IsDesignTable", flag(b.isDesignTable, false)},
	         {"IsInterior", flag(b.isInterior, false)},
	         {"IsAnyTransitionInProgress", flag(b.isDesignTransition, false)},
	         {"IsInTitleScreen", flag(b.isTitleScreen, false)},
	         {"IsInCavelike", flag(b.isCavelike, true)},
	         {"IsPaused", flag(b.isPaused, true)},
	         {"IsInputFieldActive", flag(b.inputFieldActive, true)},
	     }},
	    {"minimumZoomLevel", ReadField<float>(gameManager, b.minimumZoom)},
	    {"heldObjectMinimumStage", b.heldObjectMinimumStage != nullptr
	                                   ? nlohmann::json(managed.ReadStatic<float>(b.heldObjectMinimumStage))
	                                   : nlohmann::json()},
	    {"lastTilt", ReadField<float>(gameManager, b.lastTilt)},
	    {"focusClampMaxZoom", ReadField<float>(gameManager, b.clampMaxZoom)},
	    {"stageCount", ToJson(managed.Call<int32_t>(b.stageCount, nullptr))},
	    {"ignoreMouseWheel", ToJson(managed.Call<bool>(b.getIgnoreWheel, nullptr))},
	};
	if (auto* state = ReadField<Il2CppObject*>(gameManager, b.state); state != nullptr)
	{
		result["focus"] = ToJson(ReadField<WorldPoint>(state, b.focus));
		result["stage"] = ReadField<float>(state, b.stage);
		result["direction"] = ReadField<float>(state, b.direction);
	}
	if (auto* camera = ReadField<Il2CppObject*>(gameManager, b.camera); camera != nullptr)
	{
		if (auto transform = managed.Invoke(b.componentTransform, camera); transform && *transform != nullptr)
		{
			result["cameraPosition"] = ToJson(managed.Call<WorldPoint>(b.getPosition, *transform));
			result["cameraRotation"] = ToJson(managed.Call<Quaternion>(b.getRotation, *transform));
			if (auto rotation = managed.Call<Quaternion>(b.getRotation, *transform); rotation)
			{
				// The angle of the view centre above the horizon: negative when the camera looks down.
				const auto forward = Rotate(*rotation, WorldPoint {0.0f, 0.0f, 1.0f});
				result["viewElevationDegrees"] = std::asin(std::clamp(forward.y, -1.0f, 1.0f)) / k_DegreesToRadians;
			}
		}
		result["fieldOfView"] = ToJson(managed.Call<float>(b.fieldOfView, camera));
	}
	if (auto mouse = managed.Call<WorldPoint>(b.mousePosition, nullptr); mouse)
	{
		bool clampToSeaLevel = false;
		auto hit = managed.Invoke(b.raycastTerrain, gameManager, {&*mouse, &clampToSeaLevel});
		result["cursorHitsLand"] = hit ? nlohmann::json(*hit != nullptr) : nlohmann::json(hit.GetError().message);
		if (hit && *hit != nullptr)
		{
			// A nullable value with a value comes back as the boxed value.
			WorldPoint point;
			std::memcpy(&point, managed.il2cpp_object_unbox(*hit), sizeof(point));
			result["cursorLandPoint"] = ToJson(point);
		}
	}
	if (auto hand = managed.SingletonInstance("PlayerHandManager"); hand)
	{
		result["hand"] = {
		    {"smoothPos", ToJson(ReadField<WorldPoint>(*hand, b.handPosition))},
		    {"isOverUI", ReadField<bool>(*hand, b.handOverUi)},
		    {"handVisible", ReadField<bool>(*hand, b.handVisible)},
		};
	}
	else
	{
		result["hand"] = {{"error", hand.GetError().message}};
	}
	return result;
}

} // namespace moa_mod::camera
