/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <core/CameraNav.h>

namespace moa_mod::camera
{

namespace
{
constexpr int k_SpotsVersion = 1;

[[nodiscard]] nlohmann::json PointJson(WorldPoint point)
{
	return nlohmann::json::array({point.x, point.y, point.z});
}

/// Read a point of three finite numbers.
[[nodiscard]] std::optional<WorldPoint> ReadPoint(const nlohmann::json& value)
{
	if (!value.is_array() || value.size() != 3)
	{
		return std::nullopt;
	}
	std::array<float, 3> parts {};
	for (size_t index = 0; index < parts.size(); ++index)
	{
		if (!value[index].is_number())
		{
			return std::nullopt;
		}
		parts[index] = value[index].get<float>();
		if (!std::isfinite(parts[index]))
		{
			return std::nullopt;
		}
	}
	return WorldPoint {parts[0], parts[1], parts[2]};
}

/// Read a finite number field of an object.
[[nodiscard]] std::optional<float> ReadNumber(const nlohmann::json& object, const char* name)
{
	const auto field = object.find(name);
	if (field == object.end() || !field->is_number())
	{
		return std::nullopt;
	}
	const auto value = field->get<float>();
	return std::isfinite(value) ? std::optional<float>(value) : std::nullopt;
}
} // namespace

nlohmann::json CameraSpotsJson(const CameraSpotBook& book)
{
	nlohmann::json saves = nlohmann::json::object();
	for (const auto& [key, spots] : book)
	{
		auto list = nlohmann::json::array();
		for (const auto& spot : spots)
		{
			if (!spot)
			{
				list.push_back(nullptr);
				continue;
			}
			list.push_back({{"position", PointJson(spot->position)},
			                {"yaw", spot->yaw},
			                {"pitch", spot->pitch},
			                {"raise", spot->raise},
			                {"stage", spot->stage},
			                {"centre", PointJson(spot->centre)}});
		}
		saves[key] = std::move(list);
	}
	return {{"version", k_SpotsVersion}, {"saves", std::move(saves)}};
}

moa::Result<CameraSpotBook> ParseCameraSpots(std::string_view text)
{
	CameraSpotBook book;
	if (text.find_first_not_of(" \t\r\n") == std::string_view::npos)
	{
		return book;
	}
	const auto root = nlohmann::json::parse(text, nullptr, false);
	if (root.is_discarded() || !root.is_object())
	{
		return moa::MakeError("the camera spots are not a JSON object");
	}
	const auto version = root.find("version");
	if (version == root.end() || !version->is_number_integer() || version->get<int>() != k_SpotsVersion)
	{
		return moa::MakeError("the camera spots have no version {}", k_SpotsVersion);
	}
	const auto saves = root.find("saves");
	if (saves == root.end() || !saves->is_object())
	{
		return moa::MakeError("the camera spots have no object 'saves'");
	}
	for (const auto& [key, list] : saves->items())
	{
		if (!list.is_array() || list.size() > k_CameraSpotCount)
		{
			return moa::MakeError("the camera spots of '{}' are not a list of {} or fewer spots", key, k_CameraSpotCount);
		}
		auto& spots = book[key];
		for (size_t index = 0; index < list.size(); ++index)
		{
			const auto& value = list[index];
			if (value.is_null())
			{
				continue;
			}
			if (!value.is_object())
			{
				return moa::MakeError("the camera spot {} of '{}' is not an object", index + 1, key);
			}
			const auto position = ReadPoint(value.value("position", nlohmann::json()));
			const auto centre = ReadPoint(value.value("centre", nlohmann::json()));
			const auto yaw = ReadNumber(value, "yaw");
			const auto pitch = ReadNumber(value, "pitch");
			const auto raise = ReadNumber(value, "raise");
			const auto stage = ReadNumber(value, "stage");
			if (!position || !centre || !yaw || !pitch || !raise || !stage)
			{
				return moa::MakeError("the camera spot {} of '{}' has a missing or wrong field", index + 1, key);
			}
			spots[index] = CameraSpot {
			    .position = *position, .yaw = *yaw, .pitch = *pitch, .raise = *raise, .stage = *stage, .centre = *centre};
		}
	}
	return book;
}

} // namespace moa_mod::camera
