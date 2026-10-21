/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include <cmath>

#include <set>
#include <string>

#include <gtest/gtest.h>
#include <moa_mod/Hotkey.h>
#include <moa_mod/SettingsFile.h>
#include <moa_mod/camera/CameraSettings.h>
#include <nlohmann/json.hpp>

using namespace moa_mod;
using namespace moa_mod::camera;
using settings::FieldKind;
using settings::FieldRange;
using settings::FieldValue;

namespace
{

[[nodiscard]] moa::Result<CameraSettings> Parse(const char* json)
{
	return ParseCameraSettings(nlohmann::json::parse(json));
}

[[nodiscard]] const settings::Field<CameraSettings>& FieldOf(std::string_view key)
{
	const auto* field = settings::FindField<CameraSettings>(CameraFields(), "camera", key);
	EXPECT_NE(field, nullptr) << key;
	return *field;
}

/// Write one value into the JSON of the settings at the key of the field.
nlohmann::json WithValue(const CameraSettings& settings, const settings::Field<CameraSettings>& field,
                         const nlohmann::json& value)
{
	auto json = CameraSettingsObject(settings);
	json[std::string(field.key)] = value;
	return json;
}

} // namespace

TEST(CameraSettings, IsOffByDefaultWithTheDefaultsOfItsParts)
{
	const auto empty = Parse("{}");
	ASSERT_TRUE(empty);
	EXPECT_FALSE(empty->enabled);
	const auto camera = Parse(R"({"enabled": true})");
	ASSERT_TRUE(camera);
	EXPECT_TRUE(camera->enabled);
	EXPECT_TRUE(camera->pivotOnHand);
	EXPECT_TRUE(camera->smoothZoom);
	EXPECT_FALSE(camera->invertRotate);
	EXPECT_FALSE(camera->pitch);
	EXPECT_FALSE(camera->pitchResetOnZoom);
	EXPECT_TRUE(camera->resetPitchOnDoubleClick);
	EXPECT_EQ(camera->pitchMode, PitchMode::Hand);
	EXPECT_DOUBLE_EQ(camera->dragInertia, 0.35);
	EXPECT_DOUBLE_EQ(camera->dragInertiaMaxSpeed, 15.0);
	EXPECT_DOUBLE_EQ(camera->northDegrees, 0.0);
}

TEST(CameraSettings, ReadsTheFields)
{
	const auto settings = Parse(R"({"enabled": true, "pivotOnHand": false, "smoothZoom": false, "zoomSmoothing": 0.25,
		"rotateSmoothing": 0.1, "wheelZoomStep": 0.75, "invertRotate": true, "lookUp": true, "maxLookUpDegrees": 20,
		"pitchSensitivity": 5, "lookUpFullStage": 0.25, "lookUpZeroStage": 2, "invertPitch": true, "pitch": true,
		"minTiltDegrees": 8, "maxTiltDegrees": 80, "pitchSmoothing": 0.2, "pitchResetOnZoom": true,
		"resetPitchOnDoubleClick": false, "groundClearance": 3, "pitchMode": "orbit", "dragInertia": 0.5,
		"dragInertiaMaxSpeed": 20, "northDegrees": 30})");
	ASSERT_TRUE(settings) << settings.GetError().message;
	EXPECT_TRUE(settings->enabled);
	EXPECT_FALSE(settings->pivotOnHand);
	EXPECT_FALSE(settings->smoothZoom);
	EXPECT_DOUBLE_EQ(settings->zoomSmoothing, 0.25);
	EXPECT_DOUBLE_EQ(settings->rotateSmoothing, 0.1);
	EXPECT_DOUBLE_EQ(settings->wheelZoomStep, 0.75);
	EXPECT_TRUE(settings->invertRotate);
	EXPECT_TRUE(settings->lookUp);
	EXPECT_DOUBLE_EQ(settings->maxLookUpDegrees, 20.0);
	EXPECT_DOUBLE_EQ(settings->pitchSensitivity, 5.0);
	EXPECT_DOUBLE_EQ(settings->lookUpFullStage, 0.25);
	EXPECT_DOUBLE_EQ(settings->lookUpZeroStage, 2.0);
	EXPECT_TRUE(settings->invertPitch);
	EXPECT_TRUE(settings->pitch);
	EXPECT_DOUBLE_EQ(settings->minTiltDegrees, 8.0);
	EXPECT_DOUBLE_EQ(settings->maxTiltDegrees, 80.0);
	EXPECT_DOUBLE_EQ(settings->pitchSmoothing, 0.2);
	EXPECT_TRUE(settings->pitchResetOnZoom);
	EXPECT_FALSE(settings->resetPitchOnDoubleClick);
	EXPECT_DOUBLE_EQ(settings->groundClearance, 3.0);
	EXPECT_EQ(settings->pitchMode, PitchMode::Orbit);
	EXPECT_EQ(Parse(R"({"pitchMode": "hand"})")->pitchMode, PitchMode::Hand);
	EXPECT_EQ(Parse(R"({"pitchMode": "inPlace"})")->pitchMode, PitchMode::InPlace);
	EXPECT_DOUBLE_EQ(settings->dragInertia, 0.5);
	EXPECT_DOUBLE_EQ(settings->dragInertiaMaxSpeed, 20.0);
	EXPECT_DOUBLE_EQ(settings->northDegrees, 30.0);
}

TEST(CameraSettings, RefusesBadValues)
{
	EXPECT_FALSE(ParseCameraSettings(nlohmann::json(3)));
	EXPECT_FALSE(ParseCameraSettings(nlohmann::json::array()));
	for (const auto* text : {R"({"zoomSmoothing": -1})",
	                         R"({"rotateSmoothing": 6})",
	                         R"({"wheelZoomStep": 0})",
	                         R"({"enabled": "yes"})",
	                         R"({"lookUpFullStage": 2, "lookUpZeroStage": 1})",
	                         R"({"maxLookUpDegrees": 90})",
	                         R"({"minTiltDegrees": 50, "maxTiltDegrees": 40})",
	                         R"({"maxTiltDegrees": 90})",
	                         R"({"minTiltDegrees": 0})",
	                         R"({"groundClearance": -1})",
	                         R"({"pitch": 1})",
	                         R"({"pitchMode": "spin"})",
	                         R"({"pitchMode": 1})",
	                         R"({"dragInertia": -1})",
	                         R"({"dragInertiaMaxSpeed": 0})",
	                         R"({"northDegrees": 400})",
	                         R"({"zoomStepRatio": 1.0})",
	                         R"({"panSpeed": -0.1})",
	                         R"({"turnInertia": 6})",
	                         R"({"edgeScroll": 1})",
	                         R"({"edgeMargin": -0.1})",
	                         R"({"flyToSeconds": 0})",
	                         R"({"flyToStage": 4.5})",
	                         R"({"flyTo": 1})",
	                         R"({"groundEyeHeight": 0.2})",
	                         R"({"groundEyeHeight": 6})",
	                         R"({"groundPitchUpDegrees": 90})",
	                         R"({"groundWalkSpeed": 0})",
	                         R"({"groundLook": 1})"})
	{
		EXPECT_FALSE(Parse(text)) << text;
	}
}

TEST(CameraSettings, TheObjectReadsBackTheSame)
{
	CameraSettings settings;
	settings.enabled = true;
	settings.pitchMode = PitchMode::Orbit;
	settings.zoomSmoothing = 0.75;
	settings.dragInertia = 0.5;
	settings.maxPivotDistance = 7.5;
	settings.springArm = false;
	settings.collisionInSmoothing = 0.1;
	settings.collisionOutSmoothing = 0.6;
	settings.collisionMargin = 0.5;
	settings.traceFolder = "D:/traces";
	settings.turnInertia = 0.5;
	settings.pitchInertia = 0.0;
	settings.continuousZoom = false;
	settings.zoomStepRatio = 1.5;
	settings.panSpeed = 0.8;
	settings.edgeScroll = false;
	settings.edgeMargin = 0.5;
	settings.panAvoidance = false;
	settings.flyTo = false;
	settings.flyToSeconds = 1.5;
	settings.flyToStage = 2.0;
	settings.groundLook = false;
	settings.groundEyeHeight = 2.5;
	settings.groundPitchDownDegrees = 45.0;
	settings.groundPitchUpDegrees = 70.0;
	settings.groundWalkSpeed = 8.0;
	settings.northDegrees = -45.0;
	const auto read = ParseCameraSettings(CameraSettingsObject(settings));
	ASSERT_TRUE(read) << read.GetError().message;
	EXPECT_EQ(*read, settings);
	EXPECT_EQ(*ParseCameraSettings(CameraSettingsObject(CameraSettings {})), CameraSettings {});
}

TEST(CameraSettings, FindsTheChangedParts)
{
	const CameraSettings current;
	EXPECT_FALSE(CompareCameraSettings(current, current).Any());
	auto next = current;
	next.enabled = true;
	auto change = CompareCameraSettings(current, next);
	EXPECT_TRUE(change.enabled);
	EXPECT_FALSE(change.camera);
	EXPECT_FALSE(change.trace);

	next = current;
	next.zoomSmoothing = 1.0;
	next.maxPivotDistance = 5.0;
	change = CompareCameraSettings(current, next);
	EXPECT_FALSE(change.enabled);
	EXPECT_TRUE(change.camera);

	next = current;
	next.traceFolder = "D:/traces";
	change = CompareCameraSettings(current, next);
	EXPECT_TRUE(change.trace);
	EXPECT_FALSE(change.camera);
}

TEST(CameraSettings, ActionsChangeTheSettingsAndGiveAPatch)
{
	CameraSettings settings;
	auto edit = EditForAction(settings, CameraAction::ToggleCamera);
	ASSERT_TRUE(edit);
	EXPECT_TRUE(settings.enabled);
	EXPECT_EQ(edit->message, "Camera mod: on");
	EXPECT_EQ(edit->patch, (nlohmann::json {{"camera", {{"enabled", true}}}}));

	EXPECT_EQ(settings.pitchMode, PitchMode::Hand);
	edit = EditForAction(settings, CameraAction::CyclePitchMode);
	EXPECT_EQ(settings.pitchMode, PitchMode::InPlace);
	EXPECT_EQ(edit->patch, (nlohmann::json {{"camera", {{"pitchMode", "inPlace"}}}}));
	(void)EditForAction(settings, CameraAction::CyclePitchMode);
	EXPECT_EQ(settings.pitchMode, PitchMode::Orbit);
	(void)EditForAction(settings, CameraAction::CyclePitchMode);
	EXPECT_EQ(settings.pitchMode, PitchMode::Hand);

	edit = EditForAction(settings, CameraAction::ToggleCursorLock);
	EXPECT_FALSE(settings.lockCursor);
	EXPECT_EQ(edit->message, "Camera cursor lock: off");

	edit = EditForAction(settings, CameraAction::ToggleTrace);
	EXPECT_TRUE(settings.trace);

	// The navigation actions change no setting.
	const auto before = settings;
	EXPECT_FALSE(EditForAction(settings, CameraAction::SaveSpot1));
	EXPECT_FALSE(EditForAction(settings, CameraAction::GoToSpot4));
	EXPECT_FALSE(EditForAction(settings, CameraAction::ResetView));
	EXPECT_EQ(settings, before);
}

TEST(CameraSettings, TheActionsHaveNamesAndDefaultHotkeysThatDiffer)
{
	std::set<std::string> names;
	std::set<std::string> keys;
	for (const auto action : k_CameraActions)
	{
		const auto hotkey = ParseHotkey(CameraActionDefaultHotkey(action));
		ASSERT_TRUE(hotkey) << CameraActionName(action);
		EXPECT_TRUE(hotkey->Enabled());
		EXPECT_TRUE(names.insert(std::string(CameraActionName(action))).second);
		EXPECT_TRUE(keys.insert(FormatHotkey(*hotkey)).second);
		const bool navigation = SaveSpotIndex(action) || GoToSpotIndex(action) || action == CameraAction::ResetView;
		EXPECT_EQ(hotkey->shift, !navigation) << CameraActionName(action);
	}
	EXPECT_EQ(CameraActionName(CameraAction::GoToSpot2), "goToSpot2");
	EXPECT_EQ(SaveSpotIndex(CameraAction::SaveSpot3), 2U);
	EXPECT_FALSE(SaveSpotIndex(CameraAction::GoToSpot1));
	EXPECT_EQ(GoToSpotIndex(CameraAction::GoToSpot1), 0U);
	EXPECT_FALSE(GoToSpotIndex(CameraAction::ResetView));
	// The loader keeps Ctrl+Shift+F10, F11 and F4 for its own hotkeys, and the sky mod uses Ctrl+Shift+F6.
	for (const auto* other : {"Ctrl+Shift+F10", "Ctrl+Shift+F11", "Ctrl+Shift+F4", "Ctrl+Shift+F6"})
	{
		EXPECT_FALSE(keys.contains(other)) << other;
	}
}

TEST(CameraSettings, TheSummaryNamesTheMainValues)
{
	CameraSettings settings;
	settings.enabled = true;
	const auto text = CameraSummary(settings);
	EXPECT_NE(text.find("Camera mod: on"), std::string::npos);
	EXPECT_NE(text.find("mode hand"), std::string::npos);
}

TEST(CameraFields, EachFieldIsInTheObjectOneTime)
{
	const auto json = CameraSettingsObject(CameraSettings {});
	std::set<std::string> keys;
	for (const auto& field : CameraFields())
	{
		const auto key = std::string(field.key);
		EXPECT_EQ(field.section, "camera") << key;
		EXPECT_TRUE(keys.insert(key).second) << key;
		EXPECT_TRUE(json.contains(key)) << key;
		EXPECT_FALSE(field.label.empty()) << key;
		EXPECT_FALSE(field.help.empty()) << key;
		EXPECT_FALSE(field.restartOnly) << key;
		switch (field.kind)
		{
		case FieldKind::Toggle:
			EXPECT_NE(field.getToggle, nullptr) << key;
			EXPECT_NE(field.setToggle, nullptr) << key;
			break;
		case FieldKind::Number:
			EXPECT_NE(field.getNumber, nullptr) << key;
			EXPECT_NE(field.setNumber, nullptr) << key;
			EXPECT_LT(field.min, field.max) << key;
			EXPECT_GT(field.step, 0.0) << key;
			EXPECT_FALSE(field.format.empty()) << key;
			break;
		case FieldKind::Choice:
			EXPECT_NE(field.getChoice, nullptr) << key;
			EXPECT_FALSE(field.choices.empty()) << key;
			break;
		}
	}
	for (const auto* key : {"enabled",
	                        "pitchMode",
	                        "pivotOnHand",
	                        "lockCursor",
	                        "smoothZoom",
	                        "zoomSmoothing",
	                        "wheelZoomStep",
	                        "pitch",
	                        "minTiltDegrees",
	                        "maxTiltDegrees",
	                        "pitchSensitivity",
	                        "invertPitch",
	                        "pitchSmoothing",
	                        "lookUp",
	                        "maxLookUpDegrees",
	                        "lookUpFullStage",
	                        "lookUpZeroStage",
	                        "dragInertia",
	                        "dragInertiaMaxSpeed",
	                        "maxPivotDistance",
	                        "pitchTurnLock",
	                        "groundClearance",
	                        "trace",
	                        "springArm",
	                        "collisionInSmoothing",
	                        "collisionOutSmoothing",
	                        "collisionMargin",
	                        "turnInertia",
	                        "pitchInertia",
	                        "continuousZoom",
	                        "zoomStepRatio",
	                        "panSpeed",
	                        "edgeScroll",
	                        "northDegrees"})
	{
		EXPECT_TRUE(keys.contains(key)) << key;
	}
}

TEST(CameraFields, TheRangesAgreeWithTheChecksOfTheFile)
{
	const CameraSettings defaults;
	for (const auto& field : CameraFields())
	{
		if (field.kind != FieldKind::Number)
		{
			continue;
		}
		const auto key = std::string(field.key);
		const auto range = CameraRangeOf(field, defaults);
		// The ends of the range that the menu bar permits are correct values for the file.
		EXPECT_TRUE(ParseCameraSettings(WithValue(defaults, field, range.min))) << key << " min";
		EXPECT_TRUE(ParseCameraSettings(WithValue(defaults, field, range.max))) << key << " max";
		// The file refuses a value a little out of the static range (or the menu bar keeps a tighter range).
		if (field.min == range.min)
		{
			EXPECT_FALSE(ParseCameraSettings(WithValue(defaults, field, field.min - field.step))) << key << " below";
		}
		if (field.max == range.max)
		{
			EXPECT_FALSE(ParseCameraSettings(WithValue(defaults, field, field.max + field.step))) << key << " above";
		}
	}
}

TEST(CameraFields, CrossLimitsKeepTheOrderOfThePairs)
{
	CameraSettings settings;
	settings.lookUpFullStage = 0.5;
	settings.lookUpZeroStage = 1.5;
	settings.minTiltDegrees = 20.0;
	settings.maxTiltDegrees = 60.0;
	EXPECT_DOUBLE_EQ(CameraRangeOf(FieldOf("lookUpFullStage"), settings).max, 1.45);
	EXPECT_DOUBLE_EQ(CameraRangeOf(FieldOf("lookUpZeroStage"), settings).min, 0.55);
	EXPECT_EQ(CameraRangeOf(FieldOf("minTiltDegrees"), settings), (FieldRange {.min = 1.0, .max = 59.0}));
	EXPECT_EQ(CameraRangeOf(FieldOf("maxTiltDegrees"), settings), (FieldRange {.min = 21.0, .max = 89.0}));

	// A value past the other field of the pair stops at the limit.
	auto edit = EditCameraField(settings, FieldOf("minTiltDegrees"), FieldValue {80.0});
	ASSERT_TRUE(edit) << edit.GetError().message;
	EXPECT_DOUBLE_EQ(settings.minTiltDegrees, 59.0);
	EXPECT_EQ(edit->patch, (nlohmann::json {{"camera", {{"minTiltDegrees", 59.0}}}}));
	edit = EditCameraField(settings, FieldOf("lookUpZeroStage"), FieldValue {0.0});
	ASSERT_TRUE(edit) << edit.GetError().message;
	EXPECT_DOUBLE_EQ(settings.lookUpZeroStage, 0.55);
}

TEST(CameraFields, EditMakesAPatchOfOneField)
{
	CameraSettings settings;
	auto edit = EditCameraField(settings, FieldOf("enabled"), FieldValue {true});
	ASSERT_TRUE(edit) << edit.GetError().message;
	EXPECT_TRUE(settings.enabled);
	EXPECT_EQ(edit->patch, (nlohmann::json {{"camera", {{"enabled", true}}}}));
	EXPECT_EQ(edit->message, "Camera mod on: on");

	edit = EditCameraField(settings, FieldOf("pitchMode"), FieldValue {static_cast<size_t>(PitchMode::Orbit)});
	ASSERT_TRUE(edit) << edit.GetError().message;
	EXPECT_EQ(settings.pitchMode, PitchMode::Orbit);
	EXPECT_EQ(edit->patch, (nlohmann::json {{"camera", {{"pitchMode", "orbit"}}}}));

	// The patch on a settings file gives the same settings.
	auto text = PatchSettingsText(R"({"unknown": 1})", edit->patch);
	ASSERT_TRUE(text) << text.GetError().message;
	EXPECT_EQ(ParseCameraSettings(nlohmann::json::parse(*text).at("camera"))->pitchMode, PitchMode::Orbit);
	EXPECT_TRUE(nlohmann::json::parse(*text).contains("unknown"));
}

TEST(CameraFields, EditRefusesAValueOfTheWrongType)
{
	CameraSettings settings;
	const auto before = settings;
	EXPECT_FALSE(EditCameraField(settings, FieldOf("enabled"), FieldValue {1.0}));
	EXPECT_FALSE(EditCameraField(settings, FieldOf("zoomSmoothing"), FieldValue {true}));
	EXPECT_FALSE(EditCameraField(settings, FieldOf("zoomSmoothing"), FieldValue {std::nan("")}));
	EXPECT_FALSE(EditCameraField(settings, FieldOf("pitchMode"), FieldValue {false}));
	EXPECT_FALSE(EditCameraField(settings, FieldOf("pitchMode"), FieldValue {size_t {3}}));
	EXPECT_EQ(settings, before);
}

TEST(CameraFields, TheGroupsHaveTheFieldsOfTheFeelTheNavigationAndTheGround)
{
	const CameraSettings defaults;
	for (const auto* key : {"flyTo", "flyToSeconds", "flyToStage", "edgeMargin"})
	{
		EXPECT_EQ(FieldOf(key).group, "Navigation") << key;
	}
	EXPECT_EQ(FieldOf("panAvoidance").group, "Spring arm");
	for (const auto* key :
	     {"groundLook", "groundEyeHeight", "groundPitchDownDegrees", "groundPitchUpDegrees", "groundWalkSpeed"})
	{
		EXPECT_EQ(FieldOf(key).group, "Ground") << key;
	}
	EXPECT_EQ(CameraRangeOf(FieldOf("zoomStepRatio"), defaults), (FieldRange {.min = 1.01, .max = 4.0}));
	EXPECT_EQ(std::get<double>(settings::ValueOf(FieldOf("zoomStepRatio"), defaults)), 1.25);
	EXPECT_EQ(std::get<double>(settings::ValueOf(FieldOf("panSpeed"), defaults)), 0.45);
	EXPECT_TRUE(std::get<bool>(settings::ValueOf(FieldOf("edgeScroll"), defaults)));
	EXPECT_EQ(std::get<double>(settings::ValueOf(FieldOf("flyToSeconds"), defaults)), 0.6);
	EXPECT_EQ(std::get<double>(settings::ValueOf(FieldOf("groundEyeHeight"), defaults)), 1.8);
	EXPECT_EQ(std::get<size_t>(settings::ValueOf(FieldOf("pitchMode"), defaults)), static_cast<size_t>(PitchMode::Hand));
}
