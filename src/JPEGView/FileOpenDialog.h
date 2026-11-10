#pragma once

#include <vector>

// Open/save file dialog without hook procedure and without dialog template.
// GetOpenFileName()/GetSaveFileName() show the modern (Vista style) file dialog only in this case, the old
// Explorer style dialog used before (WTL CFileDialog, always installs a hook) does not support dark mode.
// The interface (m_ofn, m_szFileName, DoModal) is the same as the one of WTL CFileDialog.
class CSystemFileDialog
{
public:
	// The filter is separated by '\0' and terminated by a double '\0' (use Helpers::CReplacePipe)
	CSystemFileDialog(bool bOpenFileDialog, LPCTSTR sDefaultExtension, LPCTSTR sFileName, DWORD nFlags,
		LPCTSTR sFilter, HWND hWndParent);

	// Returns IDOK or IDCANCEL
	INT_PTR DoModal(HWND hWndParent);

	OPENFILENAME m_ofn;
	TCHAR m_szFileName[MAX_PATH];
	TCHAR m_szFileTitle[MAX_PATH];

private:
	bool m_bOpenFileDialog;
	CString m_sDefaultExtension;
	std::vector<TCHAR> m_filter;
};

// The JPEGView 'Open image' dialog
class CFileOpenDialog : public CSystemFileDialog
{
public:
	// The file extensions must be separated by semicolon
	CFileOpenDialog(HWND parentWindow, LPCTSTR sInitialFileName, LPCTSTR sFileEndings);
};
