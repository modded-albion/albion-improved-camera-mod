/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <array>
#include <string>
#include <string_view>

namespace moa_mod::camera
{

constexpr int32_t k_LeftButton = 0;
constexpr int32_t k_RightButton = 1;
constexpr int32_t k_MiddleButton = 2;

/// A reader of the mouse buttons.
enum class ButtonSource
{
	/// The turn input of the game: the keys and the mouse button that the user gave to the turn of the camera with
	/// the mouse. The game turns the camera only while this input is held.
	TurnInput,
	/// The mouse button of the game. It gives the left and the right button also from Steam Input. With Steam Input,
	/// it never gives the middle button.
	GameButton,
	/// The mouse button of Unity.
	Unity,
};

/// The readers that the mod has.
struct ButtonSources
{
	bool turnInput = false;
	bool gameButton = false;
};

/// The readers of a mouse button, in the order that the mod tries them. The mod uses the first reader that gives a
/// value. The middle button is the turn of the game, so the mod reads it as the game reads its turn. The left and the
/// right button come from the mouse button of the game.
[[nodiscard]] constexpr std::array<ButtonSource, 2> ButtonReadOrder(int32_t button)
{
	if (button == k_MiddleButton)
	{
		return {ButtonSource::TurnInput, ButtonSource::Unity};
	}
	return {ButtonSource::GameButton, ButtonSource::Unity};
}

/// The reader that the mod uses for a button when it has the readers in `sources`.
[[nodiscard]] constexpr ButtonSource ButtonSourceFor(int32_t button, ButtonSources sources)
{
	for (const auto source : ButtonReadOrder(button))
	{
		if (source == ButtonSource::Unity || (source == ButtonSource::TurnInput && sources.turnInput) ||
		    (source == ButtonSource::GameButton && sources.gameButton))
		{
			return source;
		}
	}
	return ButtonSource::Unity;
}

[[nodiscard]] constexpr std::string_view Name(ButtonSource source)
{
	switch (source)
	{
	case ButtonSource::TurnInput:
		return "the turn input of the game";
	case ButtonSource::GameButton:
		return "the mouse button of the game";
	case ButtonSource::Unity:
		return "the mouse button of Unity";
	}
	return "";
}

/// One line for the log that tells which reader gives each button.
[[nodiscard]] inline std::string DescribeButtonSources(ButtonSources sources)
{
	return std::string("left and right button from ") + std::string(Name(ButtonSourceFor(k_LeftButton, sources))) +
	       ", middle button from " + std::string(Name(ButtonSourceFor(k_MiddleButton, sources)));
}

} // namespace moa_mod::camera
