/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

// The entry points of the improved camera mod DLL. The mod loader (version.dll) loads the DLL from the folder `mods`
// of the game and calls these functions. The camera mod reads the object `camera` of mods.json, adds its fields, its
// camera spots and its hotkeys to the menu bar, and runs around `GameManager.Update`.

#include <cstdio>

#include <array>
#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <core/CameraSettings.h>
#include <core/CameraTrace.h>
#include <mod_api/ModApi.h>
#include <mod_core/Folders.h>
#include <mod_sdk/HostLogSink.h>
#include <mod_sdk/ModHost.h>
#include <nlohmann/json.hpp>
#include <share.h>
#include <spdlog/spdlog.h>
#include <windows.h>

#include "game/CameraMod.h"
#include "mod_game/Il2Cpp.h"
#include "mod_game/Managed.h"
#include "mod_game/ModStatus.h"

namespace moa_mod::camera
{

namespace
{
constexpr const char* k_ModVersion = "0.1.0";
/// The two copies of the game on the computer of the developer. The mod writes nothing into them.
constexpr const wchar_t* k_OriginalGameFolder = L"D:/masters_of_Albion_original";
constexpr const wchar_t* k_SteamGameFolder = L"F:/SteamLibrary/steamapps/common/Masters of Albion";

const sdk::MethodRef k_Update {.space = "", .className = "GameManager", .method = "Update", .parameterTypes = {}};
const sdk::MethodRef k_Render {.space = "",
                               .className = "EffectCameraShake",
                               .method = "OnBeginCameraRendering",
                               .parameterTypes = {"UnityEngine.Rendering.ScriptableRenderContext", "UnityEngine.Camera"}};

[[nodiscard]] std::filesystem::path PathOf(const std::string& utf8)
{
	return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

/// The state of the camera mod. It lives from `MoaModInit` to `MoaModShutdown`.
struct CameraModState
{
	std::unique_ptr<sdk::ModHost> host;
	std::shared_ptr<sdk::HostLogSink> sink;
	Il2CppApi api;
	std::optional<Managed> managed;
	std::optional<CameraBindings> bindings;
	ModStatus status {"camera"};
	/// The settings that the user asked for, and the settings that the mod uses now. All camera values work at once,
	/// so the two are the same after each change.
	CameraSettings requested;
	CameraSettings live;
	/// The camera mod. The first settings make it.
	std::unique_ptr<CameraMod> camera;
	/// The camera mod runs in the current frame of the game.
	bool inFrame = false;
	/// The trace records the render samples.
	bool renderSample = false;
};

std::unique_ptr<CameraModState> s_state;

/// The folders that the trace must not write to: the game folders.
[[nodiscard]] std::vector<std::filesystem::path> GameFolders(const CameraModState& state)
{
	return {PathOf(state.host->GameFolder()), std::filesystem::path(k_OriginalGameFolder),
	        std::filesystem::path(k_SteamGameFolder)};
}

/// The folder of the trace files and of the black box. Nothing if the folder is in a game folder.
[[nodiscard]] std::optional<std::filesystem::path> TraceFolder(const CameraModState& state)
{
	const auto& settings = state.live;
	const auto folder = settings.traceFolder.empty() ? PathOf(state.host->LogFolder()) : PathOf(settings.traceFolder);
	std::error_code error;
	const auto absolute = std::filesystem::absolute(folder, error);
	for (const auto& game : GameFolders(state))
	{
		if (error || IsInsideFolder(absolute, game))
		{
			spdlog::error("camera trace: the folder {} is in the game folder {} or not correct; the trace is off",
			              folder.string(), game.string());
			return std::nullopt;
		}
	}
	return absolute;
}

/// The first line of a trace file and of a file of the black box.
[[nodiscard]] std::string TraceHeader(const CameraModState& state)
{
	const nlohmann::json header {{"modVersion", k_ModVersion},
	                             {"buildGuid", state.host->BuildGuid()},
	                             {"process", GetCurrentProcessId()},
	                             {"maxBytes", CameraTraceRecorder::Options {}.maxBytes},
	                             {"camera", CameraSettingsJson(state.live)}};
	return std::format("camera trace {}", header.dump());
}

/// Open the trace file and give it to the camera mod.
void StartTrace(CameraModState& state)
{
	const auto folder = TraceFolder(state);
	if (!folder)
	{
		return;
	}
	std::error_code error;
	std::filesystem::create_directories(*folder, error);
	const auto path = *folder / CameraTraceFileName(std::chrono::system_clock::now(), GetCurrentProcessId());
	// Other programs can read the file while the game runs.
	std::FILE* raw = _wfsopen(path.c_str(), L"wb", _SH_DENYWR);
	if (raw == nullptr)
	{
		spdlog::error("camera trace: the mod cannot open {}; the trace is off", path.string());
		return;
	}
	std::shared_ptr<std::FILE> file(raw, &std::fclose);
	auto sink = [file](std::string_view text) {
		return std::fwrite(text.data(), 1, text.size(), file.get()) == text.size() && std::fflush(file.get()) == 0;
	};
	state.camera->StartTrace(std::move(sink), TraceHeader(state));
	spdlog::info("camera trace: writes {}", path.string());
	// The callback of the game copied the code pointer before the hook, so it does not call the hook. The camera mod
	// adds its own callback through the hook.
	if (auto shake = state.managed->Class("", "EffectCameraShake"); shake)
	{
		auto method = state.managed->Method(*shake, "OnBeginCameraRendering",
		                                    {"UnityEngine.Rendering.ScriptableRenderContext", "UnityEngine.Camera"},
		                                    "System.Void", false);
		if (method)
		{
			state.camera->EnableRenderSample(*method);
			state.renderSample = true;
		}
		else
		{
			spdlog::error("camera trace: no render sample: {}", method.GetError().message);
		}
	}
	else
	{
		spdlog::error("camera trace: no render sample: {}", shake.GetError().message);
	}
}

void StopTrace(CameraModState& state)
{
	state.camera->RemoveRenderSample();
	state.renderSample = false;
	state.camera->StopTrace();
	spdlog::info("camera trace: off");
}

/// Use new settings. The first call makes the camera mod; the next calls run on the main thread, between two frames.
void Apply(CameraModState& state, const CameraSettings& next)
{
	const auto previous = state.live;
	const auto change = CompareCameraSettings(previous, next);
	state.requested = next;
	state.live = next;
	state.status.SetEnabled(next.enabled);
	if (!state.camera)
	{
		state.camera = std::make_unique<CameraMod>(*state.managed, *state.bindings, next, state.status);
		if (auto folder = TraceFolder(state))
		{
			state.camera->SetBlackBox(*folder, TraceHeader(state));
		}
		state.status.SetReady(true);
		spdlog::info("camera mod: {}", next.enabled ? "on" : "ready, off");
		if (next.enabled && next.trace)
		{
			StartTrace(state);
		}
		return;
	}
	if (change.enabled || change.camera)
	{
		if (previous.enabled && !next.enabled)
		{
			state.camera->StandBack("the settings turned the camera mod off");
		}
		state.camera->ApplySettings(next);
	}
	// The trace runs while the camera mod and the trace are both on. A new trace folder starts a new trace file.
	const bool traced = previous.enabled && previous.trace;
	const bool trace = next.enabled && next.trace;
	if (traced != trace || (trace && change.trace))
	{
		if (traced)
		{
			StopTrace(state);
		}
		if (trace)
		{
			StartTrace(state);
		}
	}
}

/// Do an action of a hotkey or of a button. Main thread.
void RunAction(CameraModState& state, CameraAction action)
{
	const auto saveSpot = SaveSpotIndex(action);
	const auto goToSpot = GoToSpotIndex(action);
	if (saveSpot || goToSpot || action == CameraAction::ResetView)
	{
		if (!state.camera || !state.live.enabled)
		{
			state.host->ShowMessage(state.camera ? "Camera mod: off" : "Camera mod: not ready (see the log)", 2.0);
			return;
		}
		const auto message = saveSpot   ? state.camera->SaveSpot(*saveSpot)
		                     : goToSpot ? state.camera->GoToSpot(*goToSpot)
		                                : state.camera->ResetView(state.live.northDegrees);
		state.host->ShowMessage(message, 2.0);
		return;
	}
	if (!state.camera)
	{
		state.host->ShowMessage("Camera mod: not ready (see the log)", 2.0);
		return;
	}
	auto next = state.requested;
	const auto edit = EditForAction(next, action);
	if (!edit)
	{
		return;
	}
	Apply(state, next);
	state.host->ShowMessage(edit->message, 2.0);
	(void)state.host->SaveSettings(edit->patch);
}

/// The label of the button of an action, or null for an action that only has a hotkey.
[[nodiscard]] std::optional<std::string> ButtonLabel(CameraAction action)
{
	if (const auto index = SaveSpotIndex(action))
	{
		return std::format("Save spot {}", *index + 1);
	}
	if (const auto index = GoToSpotIndex(action))
	{
		return std::format("Go to spot {}", *index + 1);
	}
	if (action == CameraAction::ResetView)
	{
		return std::string("Reset the view (north at the top)");
	}
	return std::nullopt;
}

[[nodiscard]] const char* ButtonHelp(CameraAction action)
{
	if (SaveSpotIndex(action))
	{
		return "Save the view of the camera for this save.";
	}
	if (GoToSpotIndex(action))
	{
		return "Fly to the view. The flight starts when the game continues.";
	}
	return "Fly to the view of the game at this zoom, with the north at the top.";
}

/// Register the queries of the inspector. The loader calls them on the main thread. A refused query does not stop the
/// mod: the log tells it.
void RegisterQueries(CameraModState& state)
{
	auto& host = *state.host;
	if (!host.HasQueries())
	{
		spdlog::info("camera mod: the loader has no inspector queries");
		return;
	}
	bool ok = host.AddQuery("camera.state", "The camera of the game and the state of the camera mod.",
	                        [&state](const nlohmann::json& /*params*/) -> nlohmann::json {
		                        auto gameManager = state.managed->SingletonInstance("GameManager");
		                        if (!gameManager)
		                        {
			                        return {{"error", gameManager.GetError().message}};
		                        }
		                        return state.camera ? state.camera->State(*gameManager)
		                                            : DescribeCamera(*state.managed, *state.bindings, *gameManager);
	                        });
	ok = host.AddQuery("camera.events", "The last events of the camera mod. Parameter: count (50 if not given).",
	                   [&state](const nlohmann::json& params) -> nlohmann::json {
		                   if (!state.camera)
		                   {
			                   return {{"error", "the camera mod is not installed"}};
		                   }
		                   return {{"events", state.camera->Events(params.value("count", size_t {50}))}};
	                   }) &&
	     ok;
	if (!ok)
	{
		spdlog::warn("camera mod: the loader refused a query; see the log of the loader");
	}
}

/// Register the settings, the fields, the actions, the readout, the status and the method callbacks.
bool Register(CameraModState& state)
{
	auto& host = *state.host;
	bool ok = host.AddSettings("camera", [&state](const nlohmann::json& object) -> moa::Result<void> {
		auto next = ParseCameraSettings(object);
		if (!next)
		{
			return next.GetError();
		}
		Apply(state, *next);
		return {};
	});
	const auto refused = sdk::BindFields<CameraSettings>(
	    host, CameraFields(), nullptr,
	    {.requested = [&state]() -> const CameraSettings& { return state.requested; },
	     .live = [&state]() -> const CameraSettings& { return state.live; },
	     .range = [](const settings::Field<CameraSettings>& field,
	                 const CameraSettings& settings) { return CameraRangeOf(field, settings); },
	     .edit = [&state](const settings::Field<CameraSettings>& field, const settings::FieldValue& value,
	                      bool /*save*/) -> moa::Result<std::string> {
		     auto next = state.requested;
		     auto edit = EditCameraField(next, field, value);
		     if (!edit)
		     {
			     return edit.GetError();
		     }
		     Apply(state, next);
		     return edit->message;
	     }});
	ok = ok && refused == 0;
	for (const auto action : k_CameraActions)
	{
		const auto name = std::string(CameraActionName(action));
		const auto hotkey = std::string(CameraActionDefaultHotkey(action));
		const auto label = ButtonLabel(action);
		const MoaActionDesc desc {.menu = nullptr,
		                          .group = "Camera spots",
		                          .label = label ? label->c_str() : nullptr,
		                          .help = label ? ButtonHelp(action) : nullptr,
		                          .hotkey = name.c_str(),
		                          .defaultHotkey = hotkey.c_str()};
		std::function<bool()> enabled;
		if (const auto index = GoToSpotIndex(action))
		{
			enabled = [&state, index = *index] { return state.camera && state.camera->SavedSpots()[index]; };
		}
		ok = host.AddAction(desc, [&state, action] { RunAction(state, action); }, std::move(enabled)) && ok;
	}
	ok = host.AddReadout(nullptr, "Camera spots", [&state] {
		const auto& folder = state.live.traceFolder;
		return std::format("Trace folder: {}", folder.empty() ? "the folder of the logs" : folder.c_str());
	}) && ok;
	ok = host.SetStatusReader([&state] {
		auto status = state.status.Describe();
		if (state.camera)
		{
			const auto [faded, hidden] = state.camera->FadeCounts();
			status["faded"] = faded;
			status["hidden"] = hidden;
		}
		return status;
	}) && ok;
	// The settings change only between two frames, so `After` runs in each frame where `Before` ran.
	ok = host.AddMethodCallback(k_Update, MOA_HOOK_BEFORE, "camera", [&state](const MoaCall& call) {
		state.inFrame = state.camera && state.live.enabled;
		if (state.inFrame)
		{
			state.camera->Before(call.instance);
		}
	}) && ok;
	ok = host.AddMethodCallback(k_Update, MOA_HOOK_AFTER, "camera", [&state](const MoaCall& call) {
		if (std::exchange(state.inFrame, false))
		{
			state.camera->After(call.instance);
		}
	}) && ok;
	// The trace records the camera of the game just before it renders. The camera shake of the game is the method
	// that the render pipeline calls before each camera renders.
	ok = host.AddMethodCallback(k_Render, MOA_HOOK_BEFORE, "trace", [&state](const MoaCall& call) {
		if (state.renderSample && state.camera)
		{
			state.camera->TraceRender(call.arguments[1]);
		}
	}) && ok;
	return ok;
}

uint32_t Init(const MoaHostApi* api, uint32_t apiVersion)
{
	if (auto checked = sdk::ModHost::Check(api, apiVersion); !checked)
	{
		if (api != nullptr && api->log != nullptr)
		{
			api->log(api->context, MOA_LOG_ERROR, checked.GetError().message.c_str());
		}
		return 0;
	}
	auto state = std::make_unique<CameraModState>();
	state->sink = sdk::UseHostLog(*api, "improved_camera");
	state->host = std::make_unique<sdk::ModHost>(*api, "improved_camera");
	auto il2cpp = Il2CppApi::Load();
	if (!il2cpp)
	{
		spdlog::error("camera mod: {}", il2cpp.GetError().message);
		return 0;
	}
	state->api = std::move(*il2cpp);
	auto managed = Managed::Load(state->api);
	if (!managed)
	{
		spdlog::error("camera mod: {}", managed.GetError().message);
		return 0;
	}
	state->managed = *managed;
	auto bindings = CameraBindings::Bind(*state->managed);
	if (!bindings)
	{
		spdlog::error("camera mod: {}; the camera mod does not start", bindings.GetError().message);
		return 0;
	}
	state->bindings = *bindings;
	if (!Register(*state))
	{
		spdlog::error("camera mod: the loader refused a registration; the camera mod does not start");
		return 0;
	}
	RegisterQueries(*state);
	s_state = std::move(state);
	return 1;
}

void Shutdown()
{
	if (!s_state)
	{
		return;
	}
	auto& state = *s_state;
	if (state.camera)
	{
		// The game gets its objects back, and the trace writes its last row.
		state.camera->RemoveRenderSample();
		state.camera->RestoreFade();
		state.camera->CloseTrace();
	}
	spdlog::info("camera mod: stopped");
	state.sink->Detach();
}
} // namespace

} // namespace moa_mod::camera

extern "C" {
__declspec(dllexport) uint32_t MoaModApiVersion()
{
	return MOA_MOD_API_VERSION;
}

__declspec(dllexport) const MoaModInfo* MoaModGetInfo()
{
	static const MoaModInfo info {.id = "improved_camera", .name = "Camera", .version = moa_mod::camera::k_ModVersion};
	return &info;
}

__declspec(dllexport) uint32_t MoaModInit(const MoaHostApi* host, uint32_t apiVersion)
{
	// No C++ exception may leave the mod into the loader.
	try
	{
		return moa_mod::camera::Init(host, apiVersion);
	}
	catch (const std::exception& error)
	{
		spdlog::error("camera mod: the init stopped: {}", error.what());
	}
	catch (...)
	{
		spdlog::error("camera mod: the init stopped with an unknown exception");
	}
	return 0;
}

__declspec(dllexport) void MoaModShutdown()
{
	try
	{
		moa_mod::camera::Shutdown();
	}
	catch (...)
	{
		// The game quits. Nothing more can go to the log.
	}
}
}
