// DarkMode.cpp : Optional dark mode support for JPEGView
//
// All dark mode code is kept in this single file. The INI setting 'DarkMode' selects the behavior:
// 'auto' (default) follows the Windows color setting, 'true' is always dark, 'false' turns dark mode off.
//
// Dark mode is applied to the title bar and menus of the main window and to the title bar and the controls of JPEGView's own
// dialogs. The main window paints its client area itself and is not touched. The common file dialogs (open/save) are drawn by
// Windows and are left alone. Windows 10 version 1903 or later is required, on older Windows versions nothing happens.
//
// Microsoft only documents the dark title bar (DWMWA_USE_IMMERSIVE_DARK_MODE) and the use of the standard color messages
// (WM_CTLCOLOR*, LVM_SETBKCOLOR, ...). There is no documented way to make the standard controls (buttons, combo boxes, scroll bars)
// and menus dark. For these the same undocumented uxtheme.dll functions and the 'DarkMode_Explorer' theme are used as by
// Windows Explorer and by many other applications. All of these are looked up dynamically and are optional: if one of them is
// missing, dark mode is simply not activated.

#include "StdAfx.h"
#include "DarkMode.h"
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

	// Windows 10 version 1903. The uxtheme.dll ordinals used below are only valid starting with this build.
	const DWORD MIN_WINDOWS_BUILD = 18362;

	const COLORREF DARK_BACKGROUND = RGB(32, 32, 32);
	const COLORREF DARK_EDIT_BACKGROUND = RGB(45, 45, 48);
	const COLORREF DARK_TEXT = RGB(230, 230, 230);

	const TCHAR* const DARK_MODE_APPLIED_PROPERTY = _T("JPEGViewDarkModeApplied");
	const TCHAR* const DARK_MODE_REFRESH_PENDING_PROPERTY = _T("JPEGViewDarkModeRefreshPending");
	const UINT_PTR DIALOG_SUBCLASS_ID = 1;
	const UINT_PTR MAIN_WINDOW_SUBCLASS_ID = 2;

	// Values of the PreferredAppMode parameter of SetPreferredAppMode
	const int PREFERRED_APP_MODE_ALLOW_DARK = 1;	// the standard controls and menus follow the Windows color setting
	const int PREFERRED_APP_MODE_FORCE_DARK = 2;	// always dark

	typedef HRESULT (WINAPI *DwmSetWindowAttributeFunc)(HWND, DWORD, LPCVOID, DWORD);
	typedef HRESULT (WINAPI *SetWindowThemeFunc)(HWND, LPCWSTR, LPCWSTR);
	typedef LONG (WINAPI *RtlGetVersionFunc)(OSVERSIONINFOW*);

	// Undocumented uxtheme.dll functions, only available by ordinal
	typedef int (WINAPI *SetPreferredAppModeFunc)(int);
	typedef BOOL (WINAPI *AllowDarkModeForWindowFunc)(HWND, BOOL);
	typedef void (WINAPI *FlushMenuThemesFunc)();
	typedef void (WINAPI *RefreshImmersiveColorPolicyStateFunc)();

	bool s_bInitialized = false;
	bool s_bEnabled = false;
	EMode s_eMode = DM_Auto;
	HHOOK s_hCbtHook = NULL;
	HWND s_hMainWindow = NULL;
	UINT s_nRefreshMessage = 0;
	HBRUSH s_hBackgroundBrush = NULL;
	HBRUSH s_hEditBackgroundBrush = NULL;

	DwmSetWindowAttributeFunc s_pDwmSetWindowAttribute = NULL;
	SetWindowThemeFunc s_pSetWindowTheme = NULL;
	SetPreferredAppModeFunc s_pSetPreferredAppMode = NULL;
	AllowDarkModeForWindowFunc s_pAllowDarkModeForWindow = NULL;
	FlushMenuThemesFunc s_pFlushMenuThemes = NULL;
	RefreshImmersiveColorPolicyStateFunc s_pRefreshImmersiveColorPolicyState = NULL;

	// Returns the build number of Windows 10/11 or 0 for older Windows versions.
	// GetVersionEx() is not used because it reports wrong values for applications without a compatibility manifest.
	DWORD GetWindowsBuildNumber() {
		HMODULE hNtDll = ::GetModuleHandle(_T("ntdll.dll"));
		RtlGetVersionFunc pRtlGetVersion = (hNtDll != NULL) ? (RtlGetVersionFunc)::GetProcAddress(hNtDll, "RtlGetVersion") : NULL;
		if (pRtlGetVersion == NULL) {
			return 0;
		}
		OSVERSIONINFOW versionInfo;
		memset(&versionInfo, 0, sizeof(versionInfo));
		versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);
		if (pRtlGetVersion(&versionInfo) != 0) {
			return 0;
		}
		return (versionInfo.dwMajorVersion >= 10) ? versionInfo.dwBuildNumber : 0;
	}

	// Returns true if the Windows setting 'Choose your default app mode' is set to dark
	bool SystemUsesDarkMode() {
		HKEY hKey = NULL;
		if (::RegOpenKeyEx(HKEY_CURRENT_USER, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
			0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
			return false;
		}
		DWORD nAppsUseLightTheme = 1;
		DWORD nSize = sizeof(nAppsUseLightTheme);
		DWORD nType = 0;
		LONG nResult = ::RegQueryValueEx(hKey, _T("AppsUseLightTheme"), NULL, &nType, (LPBYTE)&nAppsUseLightTheme, &nSize);
		::RegCloseKey(hKey);
		return nResult == ERROR_SUCCESS && nType == REG_DWORD && nAppsUseLightTheme == 0;
	}

	// Returns true if the UI shall be dark right now. Evaluated each time a window is shown, so changes
	// of the Windows color setting are picked up by dialogs opened afterwards.
	bool IsDarkModeActive() {
		switch (s_eMode) {
		case DM_On:
			return true;
		case DM_Auto:
			return SystemUsesDarkMode();
		default:
			return false;
		}
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

	// The open/save dialogs are drawn by the Windows shell and can not be themed with the means used here
	// (they stay light or end up half themed), so they are left completely alone.
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

	void ApplyTitleBar(HWND hWnd, bool bDark) {
		if (s_pDwmSetWindowAttribute == NULL || hWnd == NULL) {
			return;
		}
		BOOL bValue = bDark ? TRUE : FALSE;
		// DWMWA_USE_IMMERSIVE_DARK_MODE is 20 on current Windows versions and 19 on the first builds of Windows 10 1903/1909
		HRESULT hResult = s_pDwmSetWindowAttribute(hWnd, 20, &bValue, sizeof(bValue));
		if (FAILED(hResult)) {
			s_pDwmSetWindowAttribute(hWnd, 19, &bValue, sizeof(bValue));
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

	void ApplyDarkModeToChildren(HWND hWnd) {
		::EnumChildWindows(hWnd, ApplyDarkModeToChildProc, 0);
		::RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
	}

	// Controls are not always completely set up at the time a dialog is activated and some are created later.
	// Therefore the theme is applied once more after the message queue has been processed, when the dialog is
	// shown or a child window is created. Many requests are merged into one.
	void RequestRefresh(HWND hWnd) {
		if (::GetProp(hWnd, DARK_MODE_REFRESH_PENDING_PROPERTY) == NULL) {
			::SetProp(hWnd, DARK_MODE_REFRESH_PENDING_PROPERTY, (HANDLE)1);
			::PostMessage(hWnd, s_nRefreshMessage, 0, 0);
		}
	}

	// Subclass of the JPEGView dialogs (and message boxes): paints the dialog dark
	LRESULT CALLBACK DialogSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/) {
		if (uMsg == s_nRefreshMessage) {
			::RemoveProp(hWnd, DARK_MODE_REFRESH_PENDING_PROPERTY);
			ApplyDarkModeToChildren(hWnd);
			return 0;
		}

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
		case WM_SHOWWINDOW:
			if (wParam != FALSE) {
				RequestRefresh(hWnd);
			}
			break;
		case WM_PARENTNOTIFY:
			if (LOWORD(wParam) == WM_CREATE) {
				RequestRefresh(hWnd);
			}
			break;
		case WM_NCDESTROY:
			::RemoveProp(hWnd, DARK_MODE_APPLIED_PROPERTY);
			::RemoveProp(hWnd, DARK_MODE_REFRESH_PENDING_PROPERTY);
			::RemoveWindowSubclass(hWnd, DialogSubclassProc, DIALOG_SUBCLASS_ID);
			break;
		default:
			break;
		}
		return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	// Subclass of the main window: follows changes of the Windows color setting while JPEGView is running
	LRESULT CALLBACK MainWindowSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/) {
		switch (uMsg) {
		case WM_SETTINGCHANGE:
			if (lParam != 0 && _wcsicmp((LPCWSTR)lParam, L"ImmersiveColorSet") == 0) {
				if (s_pRefreshImmersiveColorPolicyState != NULL) {
					s_pRefreshImmersiveColorPolicyState();
				}
				if (s_pFlushMenuThemes != NULL) {
					s_pFlushMenuThemes();
				}
				ApplyTitleBar(hWnd, IsDarkModeActive());
			}
			break;
		case WM_NCDESTROY:
			::RemoveWindowSubclass(hWnd, MainWindowSubclassProc, MAIN_WINDOW_SUBCLASS_ID);
			break;
		default:
			break;
		}
		return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	void ApplyDarkModeToWindow(HWND hWnd) {
		if (hWnd == NULL || ::GetProp(hWnd, DARK_MODE_APPLIED_PROPERTY) != NULL) {
			return;
		}
		::SetProp(hWnd, DARK_MODE_APPLIED_PROPERTY, (HANDLE)1);

		AllowDarkModeForWindow(hWnd);
		ApplyTitleBar(hWnd, true);

		// Only dialogs (including message boxes) are themed completely, other windows only get the dark title bar
		if (IsDialogWindow(hWnd)) {
			if (s_pSetWindowTheme != NULL) {
				s_pSetWindowTheme(hWnd, L"DarkMode_Explorer", NULL);
			}
			::SetWindowSubclass(hWnd, DialogSubclassProc, DIALOG_SUBCLASS_ID, 0);
			ApplyDarkModeToChildren(hWnd);
			RequestRefresh(hWnd);
		}
	}

	LRESULT CALLBACK CbtHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
		if (nCode == HCBT_ACTIVATE) {
			HWND hWnd = (HWND)wParam;
			if (hWnd != s_hMainWindow && IsTopLevelWindow(hWnd) && IsDarkModeActive() && !IsCommonFileDialog(hWnd)) {
				ApplyDarkModeToWindow(hWnd);
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

		s_eMode = CSettingsProvider::This().DarkModeSetting();
		if (s_eMode == DM_Off || GetWindowsBuildNumber() < MIN_WINDOWS_BUILD) {
			return;
		}

		// Both modules are loaded dynamically to keep compatibility with older Windows versions
		HMODULE hDwmapi = ::LoadLibrary(_T("dwmapi.dll"));
		if (hDwmapi != NULL) {
			s_pDwmSetWindowAttribute = (DwmSetWindowAttributeFunc)::GetProcAddress(hDwmapi, "DwmSetWindowAttribute");
		}

		HMODULE hUxTheme = ::LoadLibrary(_T("uxtheme.dll"));
		if (hUxTheme == NULL) {
			return;
		}
		s_pSetWindowTheme = (SetWindowThemeFunc)::GetProcAddress(hUxTheme, "SetWindowTheme");

		// Ordinals of uxtheme.dll. They are not documented, but are valid on Windows 10 1903 and later and are
		// used by Windows Explorer and many other applications to activate the dark mode of standard controls and menus.
		// 135: SetPreferredAppMode, 133: AllowDarkModeForWindow, 136: FlushMenuThemes, 104: RefreshImmersiveColorPolicyState
		s_pSetPreferredAppMode = (SetPreferredAppModeFunc)::GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135));
		s_pAllowDarkModeForWindow = (AllowDarkModeForWindowFunc)::GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133));
		s_pFlushMenuThemes = (FlushMenuThemesFunc)::GetProcAddress(hUxTheme, MAKEINTRESOURCEA(136));
		s_pRefreshImmersiveColorPolicyState = (RefreshImmersiveColorPolicyStateFunc)::GetProcAddress(hUxTheme, MAKEINTRESOURCEA(104));

		if (s_pSetWindowTheme == NULL || s_pSetPreferredAppMode == NULL || s_pAllowDarkModeForWindow == NULL ||
			s_pFlushMenuThemes == NULL || s_pRefreshImmersiveColorPolicyState == NULL) {
			return;
		}

		s_hBackgroundBrush = ::CreateSolidBrush(DARK_BACKGROUND);
		s_hEditBackgroundBrush = ::CreateSolidBrush(DARK_EDIT_BACKGROUND);
		s_nRefreshMessage = ::RegisterWindowMessage(_T("JPEGViewDarkModeRefresh"));

		// This must happen before the first window is created. AllowDark (like Windows Explorer) lets the standard
		// controls and menus follow the Windows color setting. ForceDark is only used if dark is forced in the INI file.
		s_pSetPreferredAppMode(s_eMode == DM_On ? PREFERRED_APP_MODE_FORCE_DARK : PREFERRED_APP_MODE_ALLOW_DARK);
		s_pRefreshImmersiveColorPolicyState();
		s_pFlushMenuThemes();

		s_hCbtHook = ::SetWindowsHookEx(WH_CBT, CbtHookProc, NULL, ::GetCurrentThreadId());
		s_bEnabled = (s_hCbtHook != NULL);
	}

	void RegisterMainWindow(HWND hWnd) {
		if (!s_bEnabled || hWnd == NULL) {
			return;
		}
		s_hMainWindow = hWnd;
		AllowDarkModeForWindow(hWnd);
		ApplyTitleBar(hWnd, IsDarkModeActive());
		::SetWindowSubclass(hWnd, MainWindowSubclassProc, MAIN_WINDOW_SUBCLASS_ID, 0);
	}

} // namespace DarkMode
