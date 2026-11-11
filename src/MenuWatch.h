#pragma once

// Which of the game's menus are open, kept from the menu open/close events.
//
// Asking the UI with GetMenuOpen() copies a reference to the menu itself. Done every frame, sooner or later that
// copy is the last one when the game closes the menu, and the menu is then torn down on our thread instead of the
// UI's: the workshop menu's Scaleform pieces get freed under the UI, which crashes on the way out of build mode or
// leaves its icons on screen. These flags never touch the menus.
namespace fp::menus
{
	enum Id : std::size_t
	{
		kPause,
		kPipboy,
		kLoading,
		kMain,
		kConsole,
		kWorkshop,
		kDialogue,
		kLooks,  // LooksMenu (the face and body editor, and its tattoos): Playable Pool stands completely still while it's up
		kCount
	};

	// starts listening (once; safe to call again) and reads where things stand right now
	void Watch();

	[[nodiscard]] bool Open(Id a_id) noexcept;

	// the pause, Pip-Boy, loading, main menu, console, workshop or LooksMenu
	[[nodiscard]] bool AnyBlocking() noexcept;
}
