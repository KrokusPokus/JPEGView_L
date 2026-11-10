#pragma once

// Optional dark mode support for JPEGView, implemented in DarkMode.cpp
namespace DarkMode {

	// Value of the 'DarkMode' INI setting
	enum EMode {
		DM_Auto,	// follow the Windows color setting (default)
		DM_On,		// always dark
		DM_Off		// never dark, standard Windows colors
	};

	// Reads the settings and installs the hooks needed for dark mode. Must be called before the first window is created.
	// Does nothing if dark mode is turned off or if Windows is too old (Windows 10 version 1903 is required).
	void Initialize();

	// Tells dark mode which window is the main window of the application. Must be called from the dialog initialization
	// of the main window. The main window paints its client area itself, so only its title bar and menus are themed.
	void RegisterMainWindow(HWND hWnd);

}
