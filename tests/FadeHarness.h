/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cmath>
#include <cstdint>

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#include <core/CameraFade.h>

#include "CameraLoopHarness.h"

namespace moa_mod::camera::test
{

/// A model of the objects of the game for the fade: boxes with sides parallel to the axes, each with its renderers. The
/// identifier of an object is its index plus 1.
class FakeFadeScene final: public FadeScene
{
public:
	struct Object
	{
		WorldPoint minimum;
		WorldPoint maximum;
		float opacity = 1.0f; ///< The opacity that the renderers show.
		bool hidden = false;  ///< The renderers show only the shadow.
		int changes = 0;      ///< The count of the changes of the fade.
		int restores = 0;     ///< The count of the times that the fade gave the object back.
		bool gone = false;    ///< The game removed the object.
		bool held = false;    ///< The hand holds the object.
	};
	std::vector<Object> objects;
	bool lookupFails = false; ///< The tests of the scene fail.

	/// Add a box. Return its identifier.
	uint64_t Add(WorldPoint minimum, WorldPoint maximum)
	{
		objects.push_back({.minimum = minimum,
		                   .maximum = maximum,
		                   .opacity = 1.0f,
		                   .hidden = false,
		                   .changes = 0,
		                   .restores = 0,
		                   .gone = false,
		                   .held = false});
		return objects.size();
	}

	/// Make the scene of the boxes and the round obstacles of the model of the game.
	static FakeFadeScene Of(const FakeCameraGame& game)
	{
		FakeFadeScene scene;
		for (const auto& box : game.boxes)
		{
			scene.Add(box.minimum, box.maximum);
		}
		for (const auto& obstacle : game.obstacles)
		{
			const WorldPoint size {obstacle.radius, obstacle.radius, obstacle.radius};
			scene.Add(obstacle.centre - size, obstacle.centre + size);
		}
		return scene;
	}

	[[nodiscard]] Object& Get(uint64_t object) { return objects[static_cast<size_t>(object - 1)]; }

	/// The count of the objects that show a change of the fade.
	[[nodiscard]] int Changed() const
	{
		return static_cast<int>(
		    std::ranges::count_if(objects, [](const Object& object) { return object.hidden || object.opacity < 1.0f; }));
	}

	[[nodiscard]] std::optional<std::vector<uint64_t>> ObjectsAt(WorldPoint point, float radius) override
	{
		if (lookupFails)
		{
			return std::nullopt;
		}
		std::vector<uint64_t> found;
		for (size_t index = 0; index < objects.size(); ++index)
		{
			if (!objects[index].gone && SegmentMeetsBox(point, point, objects[index], radius))
			{
				found.push_back(index + 1);
			}
		}
		return found;
	}

	[[nodiscard]] bool ShowObject(uint64_t object, float opacity, bool hidden) override
	{
		if (object == 0 || object > objects.size() || Get(object).gone)
		{
			return false;
		}
		auto& entry = Get(object);
		entry.opacity = opacity;
		entry.hidden = hidden;
		++entry.changes;
		return true;
	}

	[[nodiscard]] bool Held(uint64_t object) override { return object != 0 && object <= objects.size() && Get(object).held; }

	void RestoreObject(uint64_t object) override
	{
		if (object == 0 || object > objects.size())
		{
			return;
		}
		auto& entry = Get(object);
		entry.opacity = 1.0f;
		entry.hidden = false;
		++entry.restores;
	}

private:
	/// True if the segment comes within `radius` of the box (the box grows by the radius on each side).
	[[nodiscard]] static bool SegmentMeetsBox(WorldPoint from, WorldPoint to, const Object& box, float radius)
	{
		const std::array<float, 3> low {box.minimum.x - radius, box.minimum.y - radius, box.minimum.z - radius};
		const std::array<float, 3> high {box.maximum.x + radius, box.maximum.y + radius, box.maximum.z + radius};
		const std::array<float, 3> start {from.x, from.y, from.z};
		const std::array<float, 3> step {to.x - from.x, to.y - from.y, to.z - from.z};
		float enter = 0.0f;
		float leave = 1.0f;
		for (size_t axis = 0; axis < 3; ++axis)
		{
			if (std::abs(step[axis]) < 1e-9f)
			{
				if (start[axis] < low[axis] || start[axis] > high[axis])
				{
					return false;
				}
				continue;
			}
			const auto a = (low[axis] - start[axis]) / step[axis];
			const auto b = (high[axis] - start[axis]) / step[axis];
			enter = std::max(enter, std::min(a, b));
			leave = std::min(leave, std::max(a, b));
		}
		return enter <= leave;
	}
};

} // namespace moa_mod::camera::test
