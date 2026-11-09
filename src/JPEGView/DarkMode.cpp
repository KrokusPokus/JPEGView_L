// DarkMode.cpp : Optional dark mode support for JPEGView

// All dark mode code is kept in this single file.
// and (dark mode is the default). in INI file
//
// the dark mode only work on title bar and JPEGView's own dialogs,
// using the Windows dark theme on Windows 10 1903+.

#include "StdAfx.h"
#include "SettingsProvider.h"

#include <commctrl.h>
#include <dlgs.h>

#ifndef _RICHEDIT_VER
#define _RICHEDIT_VER 0x0210
#endif
#include <richedit.h>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#endif

namespace DarkMode {

namespace {

	const COLORREF DARK_BACKGROUND = RGB(32, 32, 32);
	const COLORREF DARK_EDIT_BACKGROUND = RGB(45, 45, 48);
	const COLORREF DARK_TEXT = RGB(230, 230, 230);

	const TCHAR* const DARK_MODE_APPLIED_PROPERTY = _T("JPEGViewDarkModeApplied");
	const UINT_PTR DARK_MODE_REFRESH_TIMER_ID = 0xD4A7;

	typedef HRESULT (WINAPI *DwmSetWindowAttributeFunc)(HWND, DWORD, LPCVOID, DWORD);
	typedef HRESULT (WINAPI *SetWindowThemeFunc)(HWND, LPCWSTR, LPCWSTR);

	// Undocumented uxtheme.dll functions used for the Windows dark mode
	typedef int (WINAPI *SetPreferredAppModeFunc)(int);
	typedef BOOL (WINAPI *AllowDarkModeForWindowFunc)(HWND, BOOL);
	typedef void (WINAPI *FlushMenuThemesFunc)();
	typedef void (WINAPI *RefreshImmersiveColorPolicyStateFunc)();

	bool s_bInitialized = false;
	bool s_bEnabled = true;
	HHOOK s_hCbtHook = NULL;
	HWND s_hMainWindow = NULL;
	HBRUSH s_hBackgroundBrush = NULL;
	HBRUSH s_hEditBackgroundBrush = NULL;

	DwmSetWindowAttributeFunc s_pDwmSetWindowAttribute = NULL;
	SetWindowThemeFunc s_pSetWindowTheme = NULL;
	SetPreferredAppModeFunc s_pSetPreferredAppMode = NULL;
	AllowDarkModeForWindowFunc s_pAllowDarkModeForWindow = NULL;
	FlushMenuThemesFunc s_pFlushMenuThemes = NULL;

	bool ParseBool(LPCTSTR sValue) {
		return _tcsicmp(sValue, _T("true")) == 0 ||
			_tcsicmp(sValue, _T("yes")) == 0 ||
			_tcsicmp(sValue, _T("on")) == 0 ||
			_ttoi(sValue) != 0;
	}

	// Reads DarkMode from the user INI file first, then from the global INI file.
	// Returns true (dark mode) if the setting is not present anywhere.
	bool ReadDarkModeFromIni() {
		CSettingsProvider& sp = CSettingsProvider::This();
		LPCTSTR sUserIni = sp.GetUserINIFileName();
		LPCTSTR sGlobalIni = sp.GetGlobalINIFileName();
		TCHAR sValue[32];

		if (sUserIni != NULL && sUserIni[0] != 0) {
			sValue[0] = 0;
			if (::GetPrivateProfileString(_T("JPEGView"), _T("DarkMode"), _T(""), sValue, ARRAYSIZE(sValue), sUserIni) > 0) {
				return ParseBool(sValue);
			}
		}

		if (sGlobalIni != NULL && sGlobalIni[0] != 0 &&
			(sUserIni == NULL || _tcsicmp(sUserIni, sGlobalIni) != 0)) {
			sValue[0] = 0;
			if (::GetPrivateProfileString(_T("JPEGView"), _T("DarkMode"), _T(""), sValue, ARRAYSIZE(sValue), sGlobalIni) > 0) {
				return ParseBool(sValue);
			}
		}

		return true;
	}

	bool IsTopLevelWindow(HWND hWnd) {
		return hWnd != NULL && (::GetWindowLongPtr(hWnd, GWL_STYLE) & WS_CHILD) == 0;
	}

	bool IsDialogWindow(HWND hWnd) {
		TCHAR sClassName[64];
		if (::GetClassName(hWnd, sClassName, ARRAYSIZE(sClassName)) == 0) {
			return false;
		}
		return _tcsicmp(sClassName, _T("#32770")) == 0;
	}

	struct FindChildClassData {
		LPCTSTR sClassName;
		bool bFound;
	};

	BOOL CALLBACK FindChildClassProc(HWND hWnd, LPARAM lParam) {
		FindChildClassData* pData = (FindChildClassData*)lParam;
		TCHAR sClassName[64];
		if (::GetClassName(hWnd, sClassName, ARRAYSIZE(sClassName)) > 0 &&
			_tcsicmp(sClassName, pData->sClassName) == 0) {
			pData->bFound = true;
			return FALSE;
		}
		return TRUE;
	}

	bool HasChildClass(HWND hWnd, LPCTSTR sClassName) {
		FindChildClassData data = { sClassName, false };
		::EnumChildWindows(hWnd, FindChildClassProc, (LPARAM)&data);
		return data.bFound;
	}

	// The open/save dialogs are left completely alone. 
	bool IsCommonFileDialog(HWND hWnd) {

		if (::GetDlgItem(hWnd, edt1) != NULL || ::GetDlgItem(hWnd, cmb13) != NULL) {
			return true;
		}

		return HasChildClass(hWnd, _T("SHELLDLL_DefView")) ||
			HasChildClass(hWnd, _T("DirectUIHWND"));
	}

	void AllowDarkModeForWindow(HWND hWnd) {
		if (s_pAllowDarkModeForWindow != NULL) {
			s_pAllowDarkModeForWindow(hWnd, TRUE);
		}
	}

	void ApplyDarkTitleBar(HWND hWnd) {
		if (s_pDwmSetWindowAttribute == NULL || hWnd == NULL) {
			return;
		}
		BOOL bDark = TRUE;
		// DWMWA_USE_IMMERSIVE_DARK_MODE is 20 on current Windows versions and 19 on older ones
		HRESULT hResult = s_pDwmSetWindowAttribute(hWnd, 20, &bDark, sizeof(bDark));
		if (FAILED(hResult)) {
			s_pDwmSetWindowAttribute(hWnd, 19, &bDark, sizeof(bDark));
		}
	}

	// Applies the dark color scheme to a rich edit control, including its existing text
	void ApplyDarkModeToRichEdit(HWND hWnd) {
		::SendMessage(hWnd, EM_SETBKGNDCOLOR, 0, (LPARAM)DARK_EDIT_BACKGROUND);

		CHARFORMAT charFormat;
		memset(&charFormat, 0, sizeof(charFormat));
		charFormat.cbSize = sizeof(charFormat);
		charFormat.dwMask = CFM_COLOR;
		charFormat.crTextColor = DARK_TEXT;

		// Change the existing text, links keep working but use the dark mode text color
		::SendMessage(hWnd, EM_SETSEL, 0, -1);
		::SendMessage(hWnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&charFormat);
		::SendMessage(hWnd, EM_SETSEL, 0, 0);

		// Also use the dark color for text added later
		::SendMessage(hWnd, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&charFormat);
	}

	BOOL CALLBACK ApplyDarkModeToChildProc(HWND hWnd, LPARAM /*lParam*/) {
		AllowDarkModeForWindow(hWnd);
		if (s_pSetWindowTheme != NULL) {
			s_pSetWindowTheme(hWnd, L"DarkMode_Explorer", NULL);
		}

		TCHAR sClassName[64];
		if (::GetClassName(hWnd, sClassName, ARRAYSIZE(sClassName)) > 0) {
			if (_tcsicmp(sClassName, _T("SysListView32")) == 0) {
				::SendMessage(hWnd, LVM_SETBKCOLOR, 0, (LPARAM)DARK_EDIT_BACKGROUND);
				::SendMessage(hWnd, LVM_SETTEXTBKCOLOR, 0, (LPARAM)DARK_EDIT_BACKGROUND);
				::SendMessage(hWnd, LVM_SETTEXTCOLOR, 0, (LPARAM)DARK_TEXT);
			} else if (_tcsicmp(sClassName, _T("SysTreeView32")) == 0) {
				::SendMessage(hWnd, TVM_SETBKCOLOR, 0, (LPARAM)DARK_EDIT_BACKGROUND);
				::SendMessage(hWnd, TVM_SETTEXTCOLOR, 0, (LPARAM)DARK_TEXT);
			} else if (_tcsnicmp(sClassName, _T("RichEdit"), 8) == 0) {
				ApplyDarkModeToRichEdit(hWnd);
			}
		}
		return TRUE;
	}

	LRESULT CALLBACK DarkModeSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/) {
		switch (uMsg) {
		case WM_ERASEBKGND: {
			HDC hDC = (HDC)wParam;
			RECT rect;
			::GetClientRect(hWnd, &rect);
			::FillRect(hDC, &rect, s_hBackgroundBrush);
			return TRUE;
		}
		case WM_CTLCOLORDLG:
			return (LRESULT)s_hBackgroundBrush;
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORBTN: {
			HDC hDC = (HDC)wParam;
			::SetTextColor(hDC, DARK_TEXT);
			::SetBkColor(hDC, DARK_BACKGROUND);
			::SetBkMode(hDC, TRANSPARENT);
			return (LRESULT)s_hBackgroundBrush;
		}
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX: {
			HDC hDC = (HDC)wParam;
			::SetTextColor(hDC, DARK_TEXT);
			::SetBkColor(hDC, DARK_EDIT_BACKGROUND);
			::SetBkMode(hDC, OPAQUE);
			return (LRESULT)s_hEditBackgroundBrush;
		}
		case WM_TIMER:
			if (wParam == DARK_MODE_REFRESH_TIMER_ID) {

				::KillTimer(hWnd, DARK_MODE_REFRESH_TIMER_ID);
				::EnumChildWindows(hWnd, ApplyDarkModeToChildProc, 0);
				::RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
				return 0;
			}
			break;
		default:
			break;
		}
		return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	void ApplyDarkModeToWindow(HWND hWnd, bool bPaintClientArea) {
		if (hWnd == NULL || ::GetProp(hWnd, DARK_MODE_APPLIED_PROPERTY) != NULL) {
			return;
		}
		::SetProp(hWnd, DARK_MODE_APPLIED_PROPERTY, (HANDLE)1);

		AllowDarkModeForWindow(hWnd);
		ApplyDarkTitleBar(hWnd);
		if (s_pSetWindowTheme != NULL) {
			s_pSetWindowTheme(hWnd, L"DarkMode_Explorer", NULL);
		}
		::EnumChildWindows(hWnd, ApplyDarkModeToChildProc, 0);

		if (bPaintClientArea) {
			// Paint the dialog background and the text of its child controls dark
			::SetWindowSubclass(hWnd, DarkModeSubclassProc, 1, 0);
			::RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
			::SetTimer(hWnd, DARK_MODE_REFRESH_TIMER_ID, 150, NULL);
		}
	}

	LRESULT CALLBACK CbtHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
		if (nCode == HCBT_CREATEWND) {
			HWND hWnd = (HWND)wParam;
			// The first top-level dialog created after Initialize() is the JPEGView main window.
			// It paints its client area itself, so only its title bar is themed.
			if (s_hMainWindow == NULL && IsTopLevelWindow(hWnd) && IsDialogWindow(hWnd)) {
				s_hMainWindow = hWnd;
			}
		} else if (nCode == HCBT_ACTIVATE) {
			HWND hWnd = (HWND)wParam;
			if (IsTopLevelWindow(hWnd) && !IsCommonFileDialog(hWnd)) {
				ApplyDarkModeToWindow(hWnd, hWnd != s_hMainWindow);
			}
		}
		return ::CallNextHookEx(s_hCbtHook, nCode, wParam, lParam);
	}

} // anonymous namespace

	void Initialize() {
		if (s_bInitialized) {
			return;
		}
		s_bInitialized = true;

		s_bEnabled = ReadDarkModeFromIni();
		if (!s_bEnabled) {
			return;
		}

		s_hBackgroundBrush = ::CreateSolidBrush(DARK_BACKGROUND);
		s_hEditBackgroundBrush = ::CreateSolidBrush(DARK_EDIT_BACKGROUND);

		// Both modules are loaded dynamically to keep compatibility with older Windows versions
		HMODULE hDwmapi = ::LoadLibrary(_T("dwmapi.dll"));
		if (hDwmapi != NULL) {
			s_pDwmSetWindowAttribute = (DwmSetWindowAttributeFunc)::GetProcAddress(hDwmapi, "DwmSetWindowAttribute");
		}

		HMODULE hUxTheme = ::LoadLibrary(_T("uxtheme.dll"));
		if (hUxTheme != NULL) {
			s_pSetWindowTheme = (SetWindowThemeFunc)::GetProcAddress(hUxTheme, "SetWindowTheme");

			// Ordinals from uxtheme.dll. They are not officially documented, but are
			// widely used to activate the Windows dark mode for standard controls and menus.
			// 135: SetPreferredAppMode (PreferredAppMode 1 == AllowDark)
			// 133: AllowDarkModeForWindow
			// 136: FlushMenuThemes
			// 104: RefreshImmersiveColorPolicyState
			s_pSetPreferredAppMode = (SetPreferredAppModeFunc)::GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135));
			s_pAllowDarkModeForWindow = (AllowDarkModeForWindowFunc)::GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133));
			s_pFlushMenuThemes = (FlushMenuThemesFunc)::GetProcAddress(hUxTheme, MAKEINTRESOURCEA(136));
			RefreshImmersiveColorPolicyStateFunc pRefreshImmersiveColorPolicyState =
				(RefreshImmersiveColorPolicyStateFunc)::GetProcAddress(hUxTheme, MAKEINTRESOURCEA(104));
			if (pRefreshImmersiveColorPolicyState != NULL) {
				pRefreshImmersiveColorPolicyState();
			}
		}

		// This must happen before the first window is created so that the standard
		// Windows controls, use the theme selected in the Windows settings.
		// AllowDark (1) is what Explorer use.
		// ForceDark (2) is not honored by the common file dialog and leaves it light or half themed.
		if (s_pSetPreferredAppMode != NULL) {
			s_pSetPreferredAppMode(1); // AllowDark
		}
		if (s_pFlushMenuThemes != NULL) {
			s_pFlushMenuThemes();
		}

		s_hCbtHook = ::SetWindowsHookEx(WH_CBT, CbtHookProc, NULL, ::GetCurrentThreadId());
	}

} // namespace DarkMode
