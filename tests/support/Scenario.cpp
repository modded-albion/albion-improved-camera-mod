/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <algorithm>
#include <array>
#include <format>
#include <numeric>
#include <utility>

#include <support/Scenario.h>

namespace moa_mod::autotest
{

namespace
{
constexpr std::array<std::pair<std::string_view, float>, 5> k_StageNames {{
    {"Detail", 0.0f},
    {"Production", 1.0f},
    {"Combat", 2.0f},
    {"Planning", 3.0f},
    {"Map", 4.0f},
}};

constexpr std::array<std::pair<std::string_view, Check::Rule>, 19> k_RuleNames {{
    {"pivotFixed", Check::Rule::PivotFixed},
    {"desiredConstant", Check::Rule::DesiredConstant},
    {"cursorLock", Check::Rule::CursorLock},
    {"aboveLand", Check::Rule::AboveLand},
    {"notInObstacle", Check::Rule::NotInObstacle},
    {"noJump", Check::Rule::NoJump},
    {"glideStops", Check::Rule::GlideStops},
    {"returnsToStart", Check::Rule::ReturnsToStart},
    {"event", Check::Rule::Event},
    {"change", Check::Rule::Change},
    {"flew", Check::Rule::Flew},
    {"ground", Check::Rule::Ground},
    {"holdsObject", Check::Rule::HoldsObject},
    {"noInvariant", Check::Rule::NoInvariant},
    {"faded", Check::Rule::Faded},
    {"fadeRestored", Check::Rule::FadeRestored},
    {"maxSpeed", Check::Rule::MaxSpeed},
    {"maxAcceleration", Check::Rule::MaxAcceleration},
    {"heldVisible", Check::Rule::HeldVisible},
}};

/// The value of a field, or an error that names the field.
template <typename T>
[[nodiscard]] moa::Result<T> Read(const nlohmann::json& json, const char* key, T fallback)
{
	const auto found = json.find(key);
	if (found == json.end())
	{
		return fallback;
	}
	try
	{
		return found->get<T>();
	}
	catch (const nlohmann::json::exception& error)
	{
		return moa::MakeError("the field '{}' is not correct: {}", key, error.what());
	}
}

[[nodiscard]] moa::Result<ScreenFraction> ReadFraction(const nlohmann::json& value)
{
	if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number())
	{
		return moa::MakeError("a point of the screen is [x, y], as parts of the width and the height from the top left");
	}
	const ScreenFraction point {value[0].get<float>(), value[1].get<float>()};
	if (point.x < 0.0f || point.x > 1.0f || point.y < 0.0f || point.y > 1.0f)
	{
		return moa::MakeError("the point of the screen [{}, {}] is not in the screen", point.x, point.y);
	}
	return point;
}

[[nodiscard]] moa::Result<Button> ReadButton(const nlohmann::json& value)
{
	if (value.is_string())
	{
		const auto text = value.get<std::string>();
		for (const auto button : {Button::Left, Button::Right, Button::Middle})
		{
			if (text == Name(button))
			{
				return button;
			}
		}
	}
	return moa::MakeError("a button is \"left\", \"right\" or \"middle\", not {}", value.dump());
}

[[nodiscard]] moa::Result<WorldPoint> ReadXz(const nlohmann::json& value, const char* what)
{
	if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number())
	{
		return moa::MakeError("'{}' is [x, z] in metres", what);
	}
	return WorldPoint {value[0].get<float>(), 0.0f, value[1].get<float>()};
}

[[nodiscard]] moa::Result<Placement> ReadPlacement(const nlohmann::json& json)
{
	Placement place;
	if (!json.is_object())
	{
		return moa::MakeError("'place' is an object");
	}
	if (json.contains("heading"))
	{
		if (!json["heading"].is_number())
		{
			return moa::MakeError("'place.heading' is a number of degrees");
		}
		place.heading = json["heading"].get<float>();
	}
	if (json.contains("offset"))
	{
		auto offset = ReadXz(json["offset"], "place.offset");
		if (!offset)
		{
			return offset.GetError();
		}
		place.offset = *offset;
	}
	if (json.contains("point"))
	{
		auto point = ReadXz(json["point"], "place.point");
		if (!point)
		{
			return point.GetError();
		}
		place.kind = Placement::Kind::Point;
		place.point = *point;
	}
	else if (json.contains("object") && json["object"].is_string())
	{
		place.kind = Placement::Kind::Object;
		place.name = json["object"].get<std::string>();
	}
	else if (json.contains("objectOfType") && json["objectOfType"].is_string())
	{
		place.kind = Placement::Kind::ObjectOfType;
		place.name = json["objectOfType"].get<std::string>();
	}
	else if (json.contains("steepest"))
	{
		place.kind = Placement::Kind::Steepest;
		const auto& steepest = json["steepest"];
		auto radius = Read(steepest, "radius", place.radius);
		auto step = Read(steepest, "step", place.step);
		if (!radius || !step)
		{
			return !radius ? radius.GetError() : step.GetError();
		}
		place.radius = *radius;
		place.step = std::max(*step, 1.0f);
	}
	else if (json.contains("mapEdge"))
	{
		place.kind = Placement::Kind::MapEdge;
		auto inset = Read(json["mapEdge"], "inset", place.inset);
		if (!inset)
		{
			return inset.GetError();
		}
		place.inset = *inset;
	}
	else if (!json.empty() && !json.contains("heading") && !json.contains("offset"))
	{
		return moa::MakeError("'place' has point, object, objectOfType, steepest or mapEdge");
	}
	return place;
}

[[nodiscard]] moa::Result<Setup> ReadSetup(const nlohmann::json& json)
{
	Setup setup;
	if (json.is_null())
	{
		return setup;
	}
	if (!json.is_object())
	{
		return moa::MakeError("'setup' is an object");
	}
	if (json.contains("stage"))
	{
		setup.stage = StageOfName(json["stage"]);
		if (!setup.stage)
		{
			return moa::MakeError("'setup.stage' is a number from 0 to 4 or a stage name, not {}", json["stage"].dump());
		}
	}
	if (json.contains("heading"))
	{
		if (!json["heading"].is_number())
		{
			return moa::MakeError("'setup.heading' is a number of degrees");
		}
		setup.heading = json["heading"].get<float>();
	}
	if (json.contains("place"))
	{
		auto place = ReadPlacement(json["place"]);
		if (!place)
		{
			return place.GetError();
		}
		setup.place = *place;
		if (setup.place.heading && !setup.heading)
		{
			setup.heading = setup.place.heading;
		}
	}
	if (json.contains("cursor"))
	{
		auto cursor = ReadFraction(json["cursor"]);
		if (!cursor)
		{
			return cursor.GetError();
		}
		setup.cursor = *cursor;
	}
	if (json.contains("settings"))
	{
		if (!json["settings"].is_object())
		{
			return moa::MakeError("'setup.settings' is an object of camera settings");
		}
		setup.settings = json["settings"];
	}
	auto hold = Read(json, "holdObject", std::string());
	auto settle = Read(json, "settleSeconds", setup.settleSeconds);
	if (!hold || !settle)
	{
		return !hold ? hold.GetError() : settle.GetError();
	}
	setup.holdObject = *hold;
	setup.settleSeconds = std::max(*settle, 0.0f);
	if (const auto pickup = json.find("pickup"); pickup != json.end())
	{
		if (!pickup->is_object())
		{
			return moa::MakeError("'setup.pickup' is an object with a 'class' and a 'name'");
		}
		auto className = Read(*pickup, "class", std::string("NGDecoration"));
		auto name = Read(*pickup, "name", std::string());
		if (!className || !name || className->empty())
		{
			return moa::MakeError("'setup.pickup' needs a text 'class' and a text 'name'");
		}
		setup.pickupClass = *className;
		setup.pickupName = *name;
	}
	return setup;
}

[[nodiscard]] moa::Result<Step> ReadStep(const nlohmann::json& json)
{
	if (!json.is_object())
	{
		return moa::MakeError("a step is an object");
	}
	Step step;
	auto seconds = Read(json, "seconds", 0.0f);
	auto right = Read(json, "right", 0.0f);
	auto up = Read(json, "up", 0.0f);
	auto interval = Read(json, "interval", step.interval);
	auto keepHeld = Read(json, "keepHeld", false);
	if (!seconds || !right || !up || !interval || !keepHeld)
	{
		for (const auto* error :
		     {seconds ? nullptr : &seconds.GetError(), right ? nullptr : &right.GetError(), up ? nullptr : &up.GetError(),
		      interval ? nullptr : &interval.GetError(), keepHeld ? nullptr : &keepHeld.GetError()})
		{
			if (error != nullptr)
			{
				return *error;
			}
		}
	}
	step.seconds = std::max(*seconds, 0.0f);
	step.right = *right;
	step.up = *up;
	step.interval = std::max(*interval, 0.0f);
	step.keepHeld = *keepHeld;
	if (json.contains("at"))
	{
		auto at = ReadFraction(json["at"]);
		if (!at)
		{
			return at.GetError();
		}
		step.at = *at;
	}
	const auto button = [&](const char* key) -> moa::Result<void> {
		auto read = ReadButton(json[key]);
		if (!read)
		{
			return read.GetError();
		}
		step.button = *read;
		return {};
	};
	moa::Result<void> done;
	if (json.contains("press"))
	{
		step.kind = Step::Kind::Press;
		done = button("press");
	}
	else if (json.contains("release"))
	{
		step.kind = Step::Kind::Release;
		done = button("release");
	}
	else if (json.contains("drag"))
	{
		step.kind = Step::Kind::Drag;
		done = button("drag");
	}
	else if (json.contains("click"))
	{
		step.kind = Step::Kind::Click;
		done = button("click");
	}
	else if (json.contains("doubleClick"))
	{
		step.kind = Step::Kind::DoubleClick;
		done = button("doubleClick");
	}
	else if (json.contains("move"))
	{
		step.kind = Step::Kind::Move;
		const auto& move = json["move"];
		auto moveRight = Read(move, "right", 0.0f);
		auto moveUp = Read(move, "up", 0.0f);
		if (!moveRight || !moveUp)
		{
			return !moveRight ? moveRight.GetError() : moveUp.GetError();
		}
		step.right = *moveRight;
		step.up = *moveUp;
	}
	else if (json.contains("wheel"))
	{
		step.kind = Step::Kind::Wheel;
		if (!json["wheel"].is_number_integer() || json["wheel"].get<int>() == 0)
		{
			return moa::MakeError("'wheel' is the count of notches, not zero; more than zero zooms in");
		}
		step.notches = json["wheel"].get<int>();
	}
	else if (json.contains("keys"))
	{
		step.kind = Step::Kind::Keys;
		auto keys = Read(json, "keys", std::vector<std::string>());
		if (!keys || keys->empty())
		{
			return moa::MakeError("'keys' is a list of names of the Unity KeyCode enum");
		}
		step.keys = *keys;
	}
	else if (json.contains("hotkey"))
	{
		step.kind = Step::Kind::Hotkey;
		auto hotkey = Read(json, "hotkey", std::string());
		if (!hotkey || hotkey->empty())
		{
			return moa::MakeError("'hotkey' is the text of a hotkey, for example \"Ctrl+F1\"");
		}
		step.hotkey = *hotkey;
	}
	else if (json.contains("blend"))
	{
		step.kind = Step::Kind::Blend;
		const auto& blend = json["blend"];
		if (!blend.is_object())
		{
			return moa::MakeError("'blend' is an object with 'seconds', 'forward', 'turn' and 'stage'");
		}
		auto blendSeconds = Read(blend, "seconds", 1.0f);
		auto forward = Read(blend, "forward", 0.0f);
		auto turn = Read(blend, "turn", 0.0f);
		if (!blendSeconds || !forward || !turn || *blendSeconds <= 0.0f)
		{
			return moa::MakeError("'blend' has 'seconds' (more than 0), 'forward' and 'turn' as numbers");
		}
		step.seconds = *blendSeconds;
		step.forward = *forward;
		step.turn = *turn;
		if (const auto stage = blend.find("stage"); stage != blend.end())
		{
			step.stage = StageOfName(*stage);
			if (!step.stage)
			{
				return moa::MakeError("the 'stage' of 'blend' is a name of a stage or a number");
			}
		}
	}
	else if (json.contains("wait"))
	{
		step.kind = Step::Kind::Wait;
		if (!json["wait"].is_number())
		{
			return moa::MakeError("'wait' is a number of seconds");
		}
		step.seconds = std::max(json["wait"].get<float>(), 0.0f);
	}
	else
	{
		return moa::MakeError(
		    "a step has press, release, move, drag, click, doubleClick, wheel, keys, hotkey, blend or wait: {}", json.dump());
	}
	if (!done)
	{
		return done.GetError();
	}
	return step;
}

[[nodiscard]] moa::Result<Check> ReadCheck(const nlohmann::json& json)
{
	if (!json.is_object() || !json.contains("rule") || !json["rule"].is_string())
	{
		return moa::MakeError("a check is an object with a 'rule'");
	}
	Check check;
	const auto rule = json["rule"].get<std::string>();
	const auto found = std::ranges::find(k_RuleNames, rule, &std::pair<std::string_view, Check::Rule>::first);
	if (found == k_RuleNames.end())
	{
		return moa::MakeError("the check rule '{}' is not known", rule);
	}
	check.rule = found->second;
	auto pixels = Read(json, "pixels", check.pixels);
	auto percent = Read(json, "percent", check.percent);
	auto metres = Read(json, "metres", check.metres);
	auto degrees = Read(json, "degrees", check.degrees);
	auto seconds = Read(json, "seconds", check.seconds);
	auto kind = Read(json, "kind", std::string());
	auto contains = Read(json, "contains", std::string());
	auto of = Read(json, "of", std::string());
	auto offlineOnly = Read(json, "offlineOnly", false);
	auto gameOnly = Read(json, "gameOnly", false);
	if (!pixels || !percent || !metres || !degrees || !seconds || !kind || !contains || !of || !offlineOnly || !gameOnly)
	{
		return moa::MakeError("the check '{}' has a field that is not correct: {}", rule, json.dump());
	}
	check.pixels = *pixels;
	check.percent = *percent;
	check.metres = *metres;
	check.degrees = *degrees;
	check.seconds = *seconds;
	check.kind = *kind;
	check.contains = *contains;
	check.of = *of;
	check.offlineOnly = *offlineOnly;
	check.gameOnly = *gameOnly;
	for (const auto& [key, target] : {std::pair {"min", &check.minimum}, std::pair {"max", &check.maximum}})
	{
		if (json.contains(key))
		{
			if (!json[key].is_number())
			{
				return moa::MakeError("'{}' of the check '{}' is a number", key, rule);
			}
			*target = json[key].get<float>();
		}
	}
	if (check.rule == Check::Rule::Change)
	{
		constexpr std::array<std::string_view, 7> k_Values {"heading", "raise",   "pitch", "stage",
		                                                    "camera",  "desired", "height"};
		if (std::ranges::find(k_Values, check.of) == k_Values.end())
		{
			return moa::MakeError("'of' of a change check is heading, raise, pitch, stage, camera, desired or height");
		}
	}
	if (check.rule == Check::Rule::Event && check.kind.empty())
	{
		return moa::MakeError("an event check needs 'kind'");
	}
	if ((check.rule == Check::Rule::MaxSpeed || check.rule == Check::Rule::MaxAcceleration) && !check.maximum)
	{
		return moa::MakeError("the check '{}' needs 'max'", rule);
	}
	return check;
}

} // namespace

std::string_view Name(Button button)
{
	switch (button)
	{
	case Button::Left:
		return "left";
	case Button::Right:
		return "right";
	case Button::Middle:
		return "middle";
	}
	return "";
}

std::string_view Name(Check::Rule rule)
{
	for (const auto& [name, value] : k_RuleNames)
	{
		if (value == rule)
		{
			return name;
		}
	}
	return "";
}

std::string Check::Describe() const
{
	switch (rule)
	{
	case Rule::Event:
		return std::format("event {} '{}'", kind, contains);
	case Rule::Change:
		return std::format("change of {}", of);
	default:
		return std::string(Name(rule));
	}
}

float Step::Duration() const
{
	switch (kind)
	{
	case Kind::Press:
	case Kind::Release:
		return seconds;
	case Kind::Move:
	case Kind::Drag:
	case Kind::Keys:
	case Kind::Wait:
		return seconds;
	case Kind::Blend:
		return seconds + k_AfterBlendSeconds;
	case Kind::Wheel:
		return static_cast<float>(std::abs(notches)) * interval;
	case Kind::Hotkey:
		return k_HotkeySeconds + k_AfterClickSeconds;
	case Kind::Click:
		return k_ClickSeconds + k_AfterClickSeconds;
	case Kind::DoubleClick:
		return 2.0f * k_ClickSeconds + k_ClickGapSeconds + k_AfterClickSeconds;
	}
	return 0.0f;
}

float Scenario::StepsDuration() const
{
	return std::accumulate(steps.begin(), steps.end(), 0.0f, [](float sum, const Step& step) { return sum + step.Duration(); });
}

std::optional<float> StageOfName(const nlohmann::json& value)
{
	if (value.is_number())
	{
		const auto stage = value.get<float>();
		if (stage < -1.0f || stage > 4.0f)
		{
			return std::nullopt;
		}
		return stage;
	}
	if (value.is_string())
	{
		const auto text = value.get<std::string>();
		const auto found = std::ranges::find(k_StageNames, text, &std::pair<std::string_view, float>::first);
		if (found != k_StageNames.end())
		{
			return found->second;
		}
	}
	return std::nullopt;
}

moa::Result<Scenario> ParseScenario(const nlohmann::json& json)
{
	if (!json.is_object())
	{
		return moa::MakeError("a scenario is a JSON object");
	}
	Scenario scenario;
	auto name = Read(json, "name", std::string());
	auto description = Read(json, "description", std::string());
	auto endSeconds = Read(json, "endSeconds", scenario.endSeconds);
	auto stillSeconds = Read(json, "stillSeconds", scenario.stillSeconds);
	if (!name || name->empty())
	{
		return moa::MakeError("a scenario needs a 'name'");
	}
	if (!description || !endSeconds || !stillSeconds)
	{
		return moa::MakeError("the scenario '{}' has a field that is not correct", *name);
	}
	scenario.name = *name;
	scenario.description = *description;
	scenario.endSeconds = std::max(*endSeconds, 0.0f);
	scenario.stillSeconds = std::max(*stillSeconds, 0.0f);
	const auto prefix = [&](const moa::Error& error) { return moa::MakeError("{}: {}", scenario.name, error.message); };
	auto setup = ReadSetup(json.value("setup", nlohmann::json::object()));
	if (!setup)
	{
		return prefix(setup.GetError());
	}
	scenario.setup = *setup;
	if (!json.contains("steps") || !json["steps"].is_array() || json["steps"].empty())
	{
		return moa::MakeError("{}: 'steps' is a list with one step or more", scenario.name);
	}
	for (const auto& item : json["steps"])
	{
		auto step = ReadStep(item);
		if (!step)
		{
			return prefix(step.GetError());
		}
		scenario.steps.push_back(*step);
	}
	for (const auto& item : json.value("checks", nlohmann::json::array()))
	{
		auto check = ReadCheck(item);
		if (!check)
		{
			return prefix(check.GetError());
		}
		scenario.checks.push_back(*check);
	}
	if (json.contains("offline"))
	{
		const auto& offline = json["offline"];
		auto world = Read(offline, "world", scenario.offline.name);
		auto rates = Read(offline, "fps", scenario.offline.framesPerSecond);
		auto skip = Read(offline, "skip", std::string());
		if (!world || !rates || !skip || rates->empty())
		{
			return moa::MakeError("{}: 'offline' has world, fps (a list) and skip", scenario.name);
		}
		scenario.offline = {.name = *world, .framesPerSecond = *rates, .skip = *skip};
	}
	if (json.contains("game"))
	{
		auto skip = Read(json["game"], "skip", std::string());
		if (!skip)
		{
			return prefix(skip.GetError());
		}
		scenario.gameSkip = *skip;
	}
	return scenario;
}

moa::Result<Scenario> ParseScenarioText(std::string_view text)
{
	auto json = nlohmann::json::parse(text, nullptr, false);
	if (json.is_discarded())
	{
		return moa::MakeError("the scenario is not JSON");
	}
	return ParseScenario(json);
}

} // namespace moa_mod::autotest
