#include "StdAfx.h"
#include "FileOpenDialog.h"
#include "NLS.h"
#include "Helpers.h"

CSystemFileDialog::CSystemFileDialog(bool bOpenFileDialog, LPCTSTR sDefaultExtension, LPCTSTR sFileName, DWORD nFlags,
	LPCTSTR sFilter, HWND hWndParent) {
	m_bOpenFileDialog = bOpenFileDialog;

	// The filter and the default extension are copied, the callers pass temporary strings
	if (sFilter != NULL) {
		LPCTSTR pEnd = sFilter;
		while (*pEnd != 0 || *(pEnd + 1) != 0) {
			pEnd++;
		}
		m_filter.assign(sFilter, pEnd + 2);
	}
	// The default extension must not contain the period
	if (sDefaultExtension != NULL) {
		m_sDefaultExtension = (sDefaultExtension[0] == _T('.')) ? sDefaultExtension + 1 : sDefaultExtension;
	}

	m_szFileName[0] = 0;
	m_szFileTitle[0] = 0;
	if (sFileName != NULL) {
		_tcsncpy_s(m_szFileName, MAX_PATH, sFileName, _TRUNCATE);
	}

	memset(&m_ofn, 0, sizeof(m_ofn));
	m_ofn.lStructSize = sizeof(m_ofn);
	m_ofn.hwndOwner = hWndParent;
	m_ofn.lpstrFile = m_szFileName;
	m_ofn.nMaxFile = MAX_PATH;
	m_ofn.lpstrFileTitle = m_szFileTitle;
	m_ofn.nMaxFileTitle = MAX_PATH;
	m_ofn.lpstrDefExt = m_sDefaultExtension.IsEmpty() ? NULL : (LPCTSTR)m_sDefaultExtension;
	m_ofn.lpstrFilter = m_filter.empty() ? NULL : &m_filter[0];
	m_ofn.nFilterIndex = 1;
	// No OFN_ENABLEHOOK and no OFN_ENABLETEMPLATE, else Windows falls back to the old dialog
	m_ofn.Flags = (nFlags | OFN_EXPLORER) & ~(OFN_ENABLEHOOK | OFN_ENABLETEMPLATE | OFN_ENABLETEMPLATEHANDLE);
}

INT_PTR CSystemFileDialog::DoModal(HWND hWndParent) {
	if (hWndParent != NULL) {
		m_ofn.hwndOwner = hWndParent;
	}
	BOOL bResult = m_bOpenFileDialog ? ::GetOpenFileName(&m_ofn) : ::GetSaveFileName(&m_ofn);
	return bResult ? IDOK : IDCANCEL;
}

CFileOpenDialog::CFileOpenDialog(HWND parentWindow, LPCTSTR sInitialFileName, LPCTSTR sFileEndings) :
CSystemFileDialog(true, _T("jpg"), sInitialFileName,
			OFN_ENABLESIZING | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST,
			Helpers::CReplacePipe(CString(CNLS::GetString(_T("Images"))) + _T(" (") + sFileEndings + _T(")|") + sFileEndings + _T("|") +
			CNLS::GetString(_T("All Files")) + _T("|*.*|")), parentWindow) {
}
