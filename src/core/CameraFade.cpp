/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <algorithm>
#include <format>
#include <iterator>

#include <core/CameraFade.h>

namespace moa_mod::camera
{

namespace
{
/// An opacity at or below this value hides the object.
constexpr float k_HiddenOpacity = 0.01f;
/// The fade gives the scene a new opacity only when it changed by more than this value.
constexpr float k_OpacityStep = 0.005f;
/// The fade out of an opacity near 1 takes at least the time for this change, so that it does not happen at once.
constexpr float k_SmallestFadeRange = 0.05f;
} // namespace

void OccluderFade::Mark(FadeScene& scene, const std::optional<std::vector<uint64_t>>& found, bool inside)
{
	if (!found)
	{
		_lookupFailed = true;
		return;
	}
	for (const auto object : *found)
	{
		if (object == 0 || scene.Held(object))
		{
			continue;
		}
		auto entry = _objects.find(object);
		if (entry == _objects.end())
		{
			if (_objects.size() >= k_MostObjects)
			{
				continue;
			}
			entry = _objects.emplace(object, FadeObjectState {}).first;
		}
		(inside ? entry->second.inside : entry->second.nearCamera) = true;
	}
}

void OccluderFade::Step(FadeScene& scene, const FadeSettings& settings, const FadeView& view, float deltaTime)
{
	if (!settings.enabled)
	{
		RestoreAll(scene);
		return;
	}
	_lookupFailed = false;
	scene.BeginFrame();
	for (auto& [object, state] : _objects)
	{
		state.nearCamera = false;
		state.inside = false;
	}
	Mark(scene, scene.ObjectsAt(view.camera, std::max(settings.distance, 0.0f)), false);
	if (settings.hideInside)
	{
		Mark(scene, scene.ObjectsAt(view.camera, k_InsideRadius), true);
	}
	ReleaseHeld(scene);

	const auto low = std::clamp(settings.opacity, 0.0f, 1.0f);
	const auto rate = settings.seconds > 0.0f ? std::max(1.0f - low, k_SmallestFadeRange) / settings.seconds : 0.0f;
	const auto step = std::max(deltaTime, 0.0f) * rate;
	for (auto entry = _objects.begin(); entry != _objects.end();)
	{
		auto& [object, state] = *entry;
		state.clearSeconds = state.nearCamera || state.inside ? 0.0f : state.clearSeconds + std::max(deltaTime, 0.0f);
		const auto faded = state.nearCamera || state.inside || state.clearSeconds < k_ClearDelay;
		const auto goal = faded ? low : 1.0f;
		if (rate <= 0.0f)
		{
			state.opacity = goal;
		}
		else if (state.opacity > goal)
		{
			state.opacity = std::max(state.opacity - step, goal);
		}
		else
		{
			state.opacity = std::min(state.opacity + step, goal);
		}
		state.hidden = state.inside || state.opacity <= k_HiddenOpacity;
		if (!faded && state.opacity >= 1.0f && !state.hidden)
		{
			// The object is solid again: the game has it back.
			if (state.shownOpacity)
			{
				scene.RestoreObject(object);
			}
			entry = _objects.erase(entry);
			continue;
		}
		const auto changed = !state.shownOpacity || std::abs(*state.shownOpacity - state.opacity) > k_OpacityStep ||
		                     state.shownHidden != state.hidden || (state.opacity == goal && *state.shownOpacity != goal);
		if (changed)
		{
			if (!scene.ShowObject(object, state.opacity, state.hidden))
			{
				// The object is gone, or it cannot change: give back what changed, and forget it.
				scene.RestoreObject(object);
				entry = _objects.erase(entry);
				continue;
			}
			state.shownOpacity = state.opacity;
			state.shownHidden = state.hidden;
		}
		++entry;
	}
}

void OccluderFade::ReleaseHeld(FadeScene& scene)
{
	// The fade does not wait for the fade in: an object in the hand is solid in the frame that the hand takes it.
	for (auto entry = _objects.begin(); entry != _objects.end();)
	{
		if (scene.Held(entry->first))
		{
			if (entry->second.shownOpacity)
			{
				scene.RestoreObject(entry->first);
			}
			entry = _objects.erase(entry);
			continue;
		}
		++entry;
	}
}

void OccluderFade::RestoreAll(FadeScene& scene)
{
	for (const auto& [object, state] : _objects)
	{
		if (state.shownOpacity)
		{
			scene.RestoreObject(object);
		}
	}
	_objects.clear();
}

int OccluderFade::Faded() const
{
	return static_cast<int>(
	    std::ranges::count_if(_objects, [](const auto& entry) { return !entry.second.hidden && entry.second.opacity < 1.0f; }));
}

int OccluderFade::Hidden() const
{
	return static_cast<int>(std::ranges::count_if(_objects, [](const auto& entry) { return entry.second.hidden; }));
}

std::optional<float> OccluderFade::LowestOpacity() const
{
	std::optional<float> lowest;
	for (const auto& [object, state] : _objects)
	{
		const auto opacity = state.hidden ? 0.0f : state.opacity;
		lowest = lowest ? std::min(*lowest, opacity) : opacity;
	}
	return lowest;
}

nlohmann::json OccluderFade::State() const
{
	auto objects = nlohmann::json::array();
	for (const auto& [object, state] : _objects)
	{
		objects.push_back({{"object", std::format("{:#x}", object)},
		                   {"opacity", state.opacity},
		                   {"hidden", state.hidden},
		                   {"near", state.nearCamera},
		                   {"inside", state.inside}});
	}
	return {{"faded", Faded()}, {"hidden", Hidden()}, {"lookupFailed", _lookupFailed}, {"objects", std::move(objects)}};
}

} // namespace moa_mod::camera
