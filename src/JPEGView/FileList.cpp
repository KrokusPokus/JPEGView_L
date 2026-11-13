#include "StdAfx.h"
#include "FileList.h"
#include "SettingsProvider.h"
#include "Helpers.h"
#include "DirectoryWatcher.h"
#include "Shlwapi.h"
#include <set>
#include <vector>

///////////////////////////////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////////////////////////////

// static initializers
Helpers::ESorting CFileDesc::sm_eSorting = Helpers::FS_LastModTime;
bool CFileDesc::sm_bSortAscending = true;
Helpers::ENavigationMode CFileList::sm_eMode = Helpers::NM_LoopDirectory;

// Helper to add the current file of filefind object to the list
static void AddToFileList(std::list<CFileDesc> & fileList, CFindFile & fileFind, LPCTSTR expectedExtension) {
	if (!fileFind.IsDirectory()) {
		if (expectedExtension != NULL) {
			// compare if the extension is the expected extension
			// this is required because a strange behavior of CFindFile: If searching for "*.tif", .tiff files are also found
			CString fileName = fileFind.GetFileName();
			int lastDot = fileName.ReverseFind(_T('.'));
			if (_tcsicmp(((LPCTSTR)fileName) + lastDot + 1, expectedExtension) != 0) {
				return;
			}
		}
		FILETIME lastWriteTime, creationTime;
		fileFind.GetLastWriteTime(&lastWriteTime);
		fileFind.GetCreationTime(&creationTime);
		CFileDesc thisFile(fileFind.GetFilePath(), &lastWriteTime, &creationTime, fileFind.GetFileSize());
		fileList.push_back(thisFile);
	}
}

static bool s_bUseLogicalStringCompare = true;
static bool s_bUseLogicalStringCompareValid = false;

// Used by method below
static bool IsNoLogicalStrCmpSetInRegistryHive(HKEY hKeyRoot) {
	HKEY key;
	if (RegOpenKeyEx(hKeyRoot, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer"), 0, KEY_READ, &key) == ERROR_SUCCESS) {
		DWORD value;
		DWORD size = sizeof(DWORD);
		DWORD type;
		if (RegQueryValueEx(key, _T("NoStrCmpLogical"), NULL, &type, (LPBYTE)&value, &size) == ERROR_SUCCESS) {
			if (type == REG_DWORD) {
				RegCloseKey(key);
				return value == 1;
			}
		}
		RegCloseKey(key);
	}
	return false;
}

// Queries the registry if logical string comparing (file9.txt before file10.txt) shall be used.
// This is default in Windows XP and later.
static bool UseLogicalStringCompare() {
	if (!s_bUseLogicalStringCompareValid) {
		s_bUseLogicalStringCompareValid = true;
		s_bUseLogicalStringCompare = !(IsNoLogicalStrCmpSetInRegistryHive(HKEY_LOCAL_MACHINE) || 
			IsNoLogicalStrCmpSetInRegistryHive(HKEY_CURRENT_USER));
	}
	return s_bUseLogicalStringCompare;
}

// Order of folder names for the folder navigation modes: the same name order as the files
// (natural, case-insensitive, full name as tie-breaker), reversed when sorting descending.
// Folders are always ordered by name, also when the files are sorted by date, size or randomly.
static bool FolderNameAscending(const CString& sName, const CString& sOther) {
	int nByName = UseLogicalStringCompare() ? StrCmpLogicalW(sName, sOther) : _tcsicoll(sName, sOther);
	if (nByName != 0) {
		return nByName < 0;
	}
	return sName.Compare(sOther) < 0;
}

static bool FolderNameLess(const CString& sName, const CString& sOther) {
	// Descending: "other before this", see CFileDesc::operator < for why not "not this before other"
	return CFileDesc::IsSortedAscending() ? FolderNameAscending(sName, sOther) : FolderNameAscending(sOther, sName);
}

///////////////////////////////////////////////////////////////////////////////////
// CFileDesc
///////////////////////////////////////////////////////////////////////////////////

CFileDesc::CFileDesc(const CString & sName, const FILETIME* lastModTime, const FILETIME* creationTime, __int64 fileSize) {
	m_sName = sName;
	m_sTitle = (LPCTSTR)m_sName + sName.ReverseFind(_T('\\')) + 1;
	memcpy(&m_lastModTime, lastModTime, sizeof(FILETIME));
	memcpy(&m_creationTime, creationTime, sizeof(FILETIME));
	m_nRandomOrderNumber = rand();
	m_fileSize = fileSize;
}

bool CFileDesc::SortAscending(const CFileDesc& other) const {
	// The sort key; if it is equal (same date or size is common, e.g. the pages of an extracted archive), the name
	// decides, and if that is equal too (natural, case-insensitive order), the full path: the order is always unique.
	if (sm_eSorting == Helpers::FS_CreationTime || sm_eSorting == Helpers::FS_LastModTime) {
		const FILETIME* pTime = (sm_eSorting == Helpers::FS_LastModTime) ? &m_lastModTime : &m_creationTime;
		const FILETIME* pTimeOther = (sm_eSorting == Helpers::FS_LastModTime) ? &(other.m_lastModTime) : &(other.m_creationTime);
		if (pTime->dwHighDateTime != pTimeOther->dwHighDateTime) {
			return pTime->dwHighDateTime < pTimeOther->dwHighDateTime;
		} else if (pTime->dwLowDateTime != pTimeOther->dwLowDateTime) {
			return pTime->dwLowDateTime < pTimeOther->dwLowDateTime;
		}
	} else if (sm_eSorting == Helpers::FS_Random) {
		if (m_nRandomOrderNumber != other.m_nRandomOrderNumber) {
			return m_nRandomOrderNumber < other.m_nRandomOrderNumber;
		}
	} else if (sm_eSorting == Helpers::FS_FileSize) {
		if (m_fileSize != other.m_fileSize) {
			return m_fileSize < other.m_fileSize;
		}
	}
	// File name sorting, and the tie-breaker of the other sortings
	int nByName;
	if (UseLogicalStringCompare()) {
		// If the filename contains numbers, we want to sort the files
		// according to the numbers to place 'File9' before 'File10'
		nByName = StrCmpLogicalW(m_sTitle, other.m_sTitle);
	} else {
		nByName = _tcsicoll(m_sTitle, other.m_sTitle);
	}
	if (nByName != 0) {
		return nByName < 0;
	}
	return m_sName.Compare(other.m_sName) < 0;
}

bool CFileDesc::operator < (const CFileDesc& other) const {
	// Descending: "other before this". Not "not this before other", that is true in both directions for equal
	// entries and breaks the rules std::list::sort() relies on.
	return sm_bSortAscending ? SortAscending(other) : other.SortAscending(*this);
}


void CFileDesc::SetName(LPCTSTR sNewName) {
	m_sName = sNewName;
	m_sTitle = (LPCTSTR)m_sName + m_sName.ReverseFind(_T('\\')) + 1;
}

void CFileDesc::SetModificationDate(const FILETIME& lastModDate) {
	memcpy(&m_lastModTime, &lastModDate, sizeof(FILETIME));
}

///////////////////////////////////////////////////////////////////////////////////
// Public interface
///////////////////////////////////////////////////////////////////////////////////

// image file types supported internally (there are additional endings for RAW and WIC - these come from INI file)
// NOTE: when adding more supported filetypes, update installer to add another extension for "SupportedTypes"
static const int cnNumEndingsInternal = 20;
static const TCHAR* csFileEndingsInternal[cnNumEndingsInternal] = {_T("jpg"), _T("jpeg"), _T("jfif"), _T("bmp"), _T("png"),
	_T("tif"), _T("tiff"), _T("gif"), _T("webp"), _T("jxl"), _T("avif"), _T("heif"), _T("heic"), _T("tga"), _T("qoi"), _T("psd"), _T("psb"), _T("ico"), _T("cbz"), _T("cb7") };
// supported camera RAW formats
static const TCHAR* csFileEndingsRAW = _T("*.pef;*.dng;*.crw;*.nef;*.cr2;*.mrw;*.rw2;*.orf;*.x3f;*.arw;*.kdc;*.nrw;*.dcr;*.sr2;*.raf");


static const int MAX_ENDINGS = 48;
static int nNumEndings;
static LPCTSTR* sFileEndings;

__declspec(dllimport) bool __stdcall WICPresent(void);

// Check if Windows Image Codecs library is present
static bool WICPresentGuarded(void) {
	try {
		return WICPresent();
	} catch (...) {
		return false;
	}
}

// Parses a semicolon separated list of file endings of the form "*.nef;*.cr2;*.dng"
static void ParseAndAddFileEndings(LPCTSTR sEndings) {
	if (_tcslen(sEndings) > 2) {
		LPTSTR buffer = new TCHAR[_tcslen(sEndings) + 1]; // this buffer will not be freed deliberately!
		_tcscpy(buffer, sEndings);
		LPTSTR sStart = buffer, sCurrent = buffer;
		while (*sCurrent != 0 && nNumEndings < MAX_ENDINGS) {
			while (*sCurrent != 0 && *sCurrent != _T(';')) {
				sCurrent++;
			}
			if (*sCurrent == _T(';')) {
				*sCurrent = 0;
				sCurrent++;
			}
			if (_tcslen(sStart) > 2) {
				sFileEndings[nNumEndings++] = sStart + 2;
			}
			sStart = sCurrent;
		}
	}
}

// Gets all supported file endings, including the ones from WIC.
// The length of the returned list is nNumEndings
static LPCTSTR* GetSupportedFileEndingList() {
	if (sFileEndings == NULL) {
		sFileEndings = new LPCTSTR[MAX_ENDINGS];
		for (nNumEndings = 0; nNumEndings < cnNumEndingsInternal; nNumEndings++) {
			sFileEndings[nNumEndings] = csFileEndingsInternal[nNumEndings];
		}

		LPCTSTR sFileEndingsWIC = CSettingsProvider::This().FilesProcessedByWIC();
		if (_tcslen(sFileEndingsWIC) > 2 && WICPresentGuarded()) {
			ParseAndAddFileEndings(sFileEndingsWIC);
		}
		ParseAndAddFileEndings(CSettingsProvider::This().FileEndingsRAW());
	}
	return sFileEndings;
}

CFileList::CFileList(const CString & sInitialFile,
	Helpers::ESorting eInitialSorting, bool isSortedAscending, bool bWrapAroundFolder, int nLevel, bool forceSorting) {

	CFileDesc::SetSorting(eInitialSorting, isSortedAscending);
	m_bDeleteHistory = true;
	m_bWrapAroundFolder = bWrapAroundFolder;
	m_sInitialFile = sInitialFile;
	m_nLevel = nLevel;
	m_next = m_prev = NULL;
	m_bNextIsAdjacent = m_bPrevIsAdjacent = false;
	int nPos = sInitialFile.ReverseFind(_T('\\'));
	m_sDirectory = (nPos > 0) ? sInitialFile.Left(nPos) : _T(""); // the backslash is stripped away!
	nPos = sInitialFile.ReverseFind(_T('.'));
	bool bIsDirectory = (::GetFileAttributes(sInitialFile) & FILE_ATTRIBUTE_DIRECTORY) != 0;
	CString sExtensionInitialFile = (nPos > 0) ? sInitialFile.Right(sInitialFile.GetLength()-nPos-1) : _T("");
	sExtensionInitialFile.MakeLower();
	bool bImageFile = !bIsDirectory && IsImageFile(sExtensionInitialFile);
	if (!bIsDirectory && !bImageFile && !sExtensionInitialFile.IsEmpty() && _tcsstr(csFileEndingsRAW, _T("*.") + sExtensionInitialFile) != NULL) {
		// initial file is a supported camera raw file but was excluded in INI file - add temporarily to file ending list
		ParseAndAddFileEndings(_T("*.") + sExtensionInitialFile);
		CSettingsProvider::This().AddTemporaryRAWFileEnding(sExtensionInitialFile);
		bImageFile = true;
	}
	m_bIsSlideShowList = !bIsDirectory && !bImageFile && TryReadingSlideShowList(sInitialFile);
	m_nMarkedIndexShow = -1;

	if (!m_bIsSlideShowList) {
		if (bImageFile || bIsDirectory) {
			FindFiles();
			m_iter = FindFile(sInitialFile);
			m_iterStart = UsesStartFileAsLoopStart() ? m_iter : m_fileList.begin();
		} else {
			// neither image file nor directory nor list of file names - try to read anyway but normally will fail
			CFindFile fileFind;
			if (fileFind.FindFile(sInitialFile)) {
				AddToFileList(m_fileList, fileFind, NULL);
			}
			m_iter = m_iterStart = m_fileList.begin();
		}
	} else {
		sm_eMode = Helpers::NM_LoopDirectory;
		if (forceSorting) m_fileList.sort();
		m_iter = m_iterStart = m_fileList.begin();
	}
}

CFileList::~CFileList() {
	if (m_bDeleteHistory) {
		DeleteHistory();
	}
	m_fileList.clear();
}

CString CFileList::GetSupportedFileEndings() {
	LPCTSTR* allFileEndings = GetSupportedFileEndingList();
	CString sList;
	for (int i = 0; i < nNumEndings; i++) {
		sList += _T("*.");
		sList += allFileEndings[i];
		if (i+1 < nNumEndings) sList += _T(";");
	}
	return sList;
}

void CFileList::Reload(LPCTSTR sFileName, bool clearForwardHistory) {
	LPCTSTR sCurrent = sFileName;
	if (sCurrent == NULL) {
		sCurrent = Current();
		if (sCurrent == NULL) {
			m_fileList.clear();
			return;
		}
	}

	CString sCurrentFile = sCurrent;

	if (clearForwardHistory) {
		// delete the chain of CFileLists forward
		CFileList* pFileList = this->m_next;
		while (pFileList != NULL && pFileList->m_prev != NULL) {
			CFileList* thisList = pFileList;
			pFileList = pFileList->m_next;
			thisList->m_bDeleteHistory = false;
			delete thisList;
		}
		m_next = NULL;
		m_bNextIsAdjacent = false;
	}

	if (!m_bIsSlideShowList) {
		FindFiles();
		m_iterStart = UsesStartFileAsLoopStart() ? FindFile(m_sInitialFile) : m_fileList.begin();
	} else {
		VerifyFiles(); // maybe some of the files got deleted or moved
		m_iterStart = m_fileList.begin();
	}
	m_iter = FindFile(sCurrentFile); // go again to current file
}

bool CFileList::CurrentFileExists() const {
	if (Current() != NULL) {
		return ::GetFileAttributes(Current()) != INVALID_FILE_ATTRIBUTES;
	}
	return false;
}

bool CFileList::DriveHasRecycleBin(LPCTSTR filePath) {
	TCHAR driveLetter[3];
	if (_tcslen(filePath) < 3) return false;
	driveLetter[0] = filePath[0];
	driveLetter[1] = filePath[1];
	driveLetter[2] = 0;

	if (driveLetter[0] == _T('\\') && driveLetter[1] == _T('\\')) return false; // remote drive, no recycle bin

	CString recycleBin = CString((LPCTSTR(driveLetter))) + _T("\\$Recycle.Bin\\");

	DWORD attributes = ::GetFileAttributes(recycleBin);
	if (attributes == INVALID_FILE_ATTRIBUTES) return false;
	if (attributes & FILE_ATTRIBUTE_DIRECTORY) return true; // this is a directory!

	return false; // this is not a directory!
}

bool CFileList::DeleteFile(LPCTSTR fileNameWithPath) const {
	if (fileNameWithPath != NULL) {
		// append a double null character
		// (heap buffer: paths can be longer than MAX_PATH)
		size_t nLen = _tcslen(fileNameWithPath);
		std::vector<TCHAR> fileName(nLen + 2, 0);
		_tcscpy_s(fileName.data(), nLen + 2, fileNameWithPath);

		SHFILEOPSTRUCT fileOp{ 0 };
		fileOp.hwnd = NULL;
		fileOp.wFunc = FO_DELETE;
		fileOp.pFrom = fileName.data();
		fileOp.pTo = NULL;
		fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION;
		fileOp.hNameMappings = NULL;
		if (::SHFileOperation(&fileOp) == 0 && !fileOp.fAnyOperationsAborted) {
			return true;
		}
	}
	return false;
}

void CFileList::FileHasRenamed(LPCTSTR sOldFileName, LPCTSTR sNewFileName) {
	if (_tcsicmp(sOldFileName, m_sInitialFile) == 0) {
		m_sInitialFile = sNewFileName;
	}
	std::list<CFileDesc>::iterator iter;
	for (iter = m_fileList.begin( ); iter != m_fileList.end( ); iter++ ) {
		if (_tcsicmp(sOldFileName, iter->GetName()) == 0) {
			iter->SetName(sNewFileName);
		}
	}
}

void CFileList::ModificationTimeChanged() {
	if (m_iter != m_fileList.end()) {
		LPCTSTR sName = m_iter->GetName();
		HANDLE hFile = ::CreateFile(sName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile != INVALID_HANDLE_VALUE) {
			FILETIME lastModTime;
			if (::GetFileTime(hFile, NULL, NULL, &lastModTime)) {
				m_iter->SetModificationDate(lastModTime);
			}
			::CloseHandle(hFile);
		}
	}
}

CFileList* CFileList::Next() {
	m_nMarkedIndexShow = -1;
	if (sm_eMode != Helpers::NM_LoopDirectory) {
		return NextInFolderList();
	}
	if (m_fileList.size() > 0) {
		std::list<CFileDesc>::iterator iterTemp = m_iter;
		if (iterTemp == m_fileList.end())
			return this;
		iterTemp++;
		if (iterTemp == m_fileList.end()) {
			iterTemp = m_fileList.begin();
		}
		if (iterTemp == m_iterStart && !WrapsAround()) {
			return this; // we are finished with this folder
		}
		NextInFolder();
	}
	return this;
}

CFileList* CFileList::Prev() {
	m_nMarkedIndexShow = -1;
	if (sm_eMode != Helpers::NM_LoopDirectory) {
		return PrevInFolderList();
	}
	if (m_iter == m_iterStart) {
		if (!WrapsAround()) {
			return this;
		}
		if (m_iter == m_fileList.begin()) {
			MoveIterToLast();
		} else {
			m_iter--;
		}
		return this;
	}
	if (m_fileList.size() > 0) {
		if (m_iter == m_fileList.begin()) {
			m_iter = m_fileList.end();
			m_iter--;
		} else {
			m_iter--;
		}
	}
	return this;
}

CFileList* CFileList::First() {
	m_nMarkedIndexShow = -1;
	CFileList* pFirst = this;
	if (sm_eMode != Helpers::NM_LoopDirectory) {
		pFirst = OuterFolder(true);
		if (pFirst == NULL) {
			pFirst = this; // no images at all
		}
		pFirst->m_nMarkedIndexShow = -1;
	}
	pFirst->m_iter = pFirst->m_iterStart = pFirst->m_fileList.begin();
	return pFirst;
}

CFileList* CFileList::Last() {
	m_nMarkedIndexShow = -1;
	CFileList* pLast = this;
	if (sm_eMode != Helpers::NM_LoopDirectory) {
		pLast = OuterFolder(false);
		if (pLast == NULL) {
			pLast = this; // no images at all
		}
		pLast->m_nMarkedIndexShow = -1;
	}
	pLast->MoveIterToLast();
	pLast->m_iterStart = pLast->m_fileList.begin();
	return pLast;
}

bool CFileList::WrapsAround() const {
	return m_bWrapAroundFolder;
}

CFileList* CFileList::AwayFromCurrent() {
	LPCTSTR sCurrentFile = Current();
	LPCTSTR sNextFile = PeekNextPrev(1, true, false);
	if (sCurrentFile == NULL || sNextFile == NULL || _tcscmp(sCurrentFile, sNextFile) == 0) {
		CFileList* pFileList = Prev();
		if (Current() != NULL && sCurrentFile != NULL && _tcscmp(sCurrentFile, Current()) == 0) {
			// not moved away, only one image
			m_iter = m_fileList.end();
		}
		return pFileList;
	} else {
		return Next();
	}
}

LPCTSTR CFileList::Current() const {
	if (m_iter != m_fileList.end()) {
		return m_iter->GetName();
	} else {
		return NULL;
	}
}

LPCTSTR CFileList::CurrentFileTitle() const {
	LPCTSTR sCurrent = Current();
	if (sCurrent != NULL) {
		LPCTSTR sTitle = _tcsrchr(sCurrent, _T('\\'));
		if (sTitle == NULL) sTitle = sCurrent; else sTitle++;
		return sTitle;
	} else {
		return NULL;
	}
}

LPCTSTR CFileList::CurrentDirectory() const {
	if (m_sDirectory.GetLength() > 0) {
		return m_sDirectory;
	} else {
		return NULL;
	}
}

int CFileList::CurrentIndex() const {
	int i = 0;
	std::list<CFileDesc>::const_iterator iter;
	for (iter = m_fileList.begin( ); iter != m_fileList.end( ); iter++ ) {
		if (iter == m_iter) {
			return i;
		}
		i++;
	}
	return -1;
}

const FILETIME* CFileList::CurrentModificationTime() const {
	if (m_iter != m_fileList.end()) {
		return &(m_iter->GetLastModTime());
	} else {
		return NULL;
	}
}

LPCTSTR CFileList::PeekNextPrev(int nIndex, bool bForward, bool bToggle) {
	if (bToggle) {
		return (m_nMarkedIndexShow == 0) ? m_sMarkedFile : m_sMarkedFileCurrent;
	} else {
		std::list<CFileDesc>::iterator thisIter = m_iter;
		LPCTSTR sFileName;
		if (nIndex != 0) {
			CFileList* pFL = bForward ? Next() : Prev();
			sFileName = pFL->PeekNextPrev(nIndex-1, bForward, bToggle);
		} else {
			sFileName = Current();
		}
		m_iter = thisIter;
		return sFileName;
	}
}

void CFileList::SetSorting(Helpers::ESorting eSorting, bool sortAscending) {
	if (eSorting != CFileDesc::GetSorting() || sortAscending != CFileDesc::IsSortedAscending()) {
		CString sThisFile = (m_iter != m_fileList.end()) ? m_iter->GetName() : "";
		CFileDesc::SetSorting(eSorting, sortAscending);
		m_fileList.sort();
		m_iter = FindFile(sThisFile);
		m_iterStart = UsesStartFileAsLoopStart() ? m_iter : m_fileList.begin();
		if (sm_eMode != Helpers::NM_LoopDirectory) {
			// the other loaded folders are neither resorted nor in the new folder order, reload them when needed
			DeleteHistory();
		}
	}
}

Helpers::ESorting CFileList::GetSorting() const {
	return CFileDesc::GetSorting();
}

bool CFileList::IsSortedAscending() const {
	return CFileDesc::IsSortedAscending();
}

void CFileList::SetNavigationMode(Helpers::ENavigationMode eMode) {
	if (m_bIsSlideShowList) {
		return;
	}

	sm_eMode = eMode;
	DeleteHistory();
	m_nLevel = 0;
	m_iterStart = UsesStartFileAsLoopStart() ? m_iter : m_fileList.begin();
}

void CFileList::MarkCurrentFile() {
	LPCTSTR sCurrent = Current();
	if (::GetFileAttributes(sCurrent) != INVALID_FILE_ATTRIBUTES) {
		m_sMarkedFile = CString(sCurrent);
		m_sMarkedFileCurrent = _T("");
		m_nMarkedIndexShow = -1;
	}
}

bool CFileList::FileMarkedForToggle() {
	return !m_sMarkedFile.IsEmpty();
}

void CFileList::ToggleBetweenMarkedAndCurrentFile() {
	if (m_nMarkedIndexShow == -1) {
		m_nMarkedIndexShow = 0;
	}
	LPCTSTR sNewFile = (m_nMarkedIndexShow == 0) ? m_sMarkedFile : m_sMarkedFileCurrent;
	if (sNewFile[0] == 0) {
		return;
	}
	if (m_nMarkedIndexShow == 0) {
		m_sMarkedFileCurrent = Current();
	}

	CString sRootDirectory = RootDirectory();
	m_sInitialFile = sNewFile;
	int nPos = m_sInitialFile.ReverseFind(_T('\\'));
	m_sDirectory = (nPos > 0) ? m_sInitialFile.Left(nPos) : _T(""); // the backslash is stripped away!
	// LoopSubFolders: keep the loop below the same root folder if the file is in it, else start a new loop
	m_nLevel = 0;
	if (m_sDirectory.GetLength() > sRootDirectory.GetLength() && sRootDirectory.GetLength() > 0 &&
		m_sDirectory[sRootDirectory.GetLength()] == _T('\\') &&
		m_sDirectory.Left(sRootDirectory.GetLength()).CompareNoCase(sRootDirectory) == 0) {
		CString sBelowRoot = m_sDirectory.Mid(sRootDirectory.GetLength());
		for (int i = 0; i < sBelowRoot.GetLength(); i++) {
			if (sBelowRoot[i] == _T('\\')) m_nLevel++;
		}
	}

	DeleteHistory();
	Reload(m_sInitialFile);

	m_nMarkedIndexShow = (m_nMarkedIndexShow + 1) & 1;
}

bool CFileList::CanOpenCurrentFileForReading() const
{
	LPCTSTR sCurrentFile = Current();
	if (sCurrentFile != NULL && sCurrentFile[0] != 0) {
		HANDLE hFile = ::CreateFile(Current(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile != INVALID_HANDLE_VALUE) {
			::CloseHandle(hFile);
			return true;
		}
	}
	return false;
}

void CFileList::DeleteHistory(bool onlyForward) {
	// delete the chain of CFileLists forward and backward and only leave the current node alive
	CFileList* pFileList = this->m_next;
	while (pFileList != NULL && pFileList->m_prev != NULL) {
		CFileList* thisList = pFileList;
		pFileList = pFileList->m_next;
		thisList->m_bDeleteHistory = false; // prevent multiple delete
		delete thisList;
	}
	if (!onlyForward) {
		pFileList = this->m_prev;
		while (pFileList != NULL) {
			CFileList* thisList = pFileList;
			pFileList = pFileList->m_prev;
			thisList->m_bDeleteHistory = false;  // prevent multiple delete
			delete thisList;
		}
		m_prev = NULL;
		m_bPrevIsAdjacent = false;
	}
	m_next = NULL;
	m_bNextIsAdjacent = false;
}

///////////////////////////////////////////////////////////////////////////////////
// Private
///////////////////////////////////////////////////////////////////////////////////

void CFileList::MoveIterToLast() {
	// also correct when m_iter is end(), e.g. after AwayFromCurrent() found no other image
	m_iter = m_fileList.end();
	if (m_fileList.size() > 0) {
		m_iter--;
	}
}

std::list<CFileDesc>::iterator CFileList::FindFile(const CString& sName) {
	int nStart = sName.ReverseFind(_T('\\')) + 1;
	if (nStart == sName.GetLength()) {
		return m_fileList.begin();
	}
	std::list<CFileDesc>::iterator iter;
	for (iter = m_fileList.begin( ); iter != m_fileList.end( ); iter++ ) {
		if (_tcsicmp((LPCTSTR)sName + nStart, iter->GetTitle()) == 0) {
			return iter;
		}
	}
	return m_fileList.begin(); // in case the file was not found
}

bool CFileList::UsesStartFileAsLoopStart() const {
	// Only LoopFolder with wrap around starts the loop at the start file. In the folder navigation
	// modes all folders of the loop form one file list and the start file is just a position in it.
	return m_bWrapAroundFolder && sm_eMode == Helpers::NM_LoopDirectory;
}

// LoopSameFolderLevel and LoopSubFolders: the files of all folders of the loop form one list, in folder
// order and within each folder in file order. The folders of the loop are
// - LoopSameFolderLevel: the sibling folders of the start folder, the start folder included
// - LoopSubFolders: the start folder and all folders below it, depth-first, the files of a folder before
//   its subfolders (m_nLevel is the depth of a folder below the start folder)
// With wrap around, the last file of the last folder is followed by the first file of the first folder;
// without, navigation stops there.
// The chain m_prev/m_next is a cache of the folders loaded so far, kept in folder order.
CFileList* CFileList::NextInFolderList() {
	if (m_fileList.size() > 0) {
		if (m_iter == m_fileList.end()) {
			return this;
		}
		std::list<CFileDesc>::iterator iterNext = m_iter;
		iterNext++;
		if (iterNext != m_fileList.end()) {
			m_iter = iterNext;
			return this;
		}
	}
	// last file of this folder (or no files): continue with the first file of the next folder
	CFileList* pNext = AdjacentFolder(true);
	if (pNext == NULL) {
		if (!WrapsAround()) {
			return this;
		}
		pNext = OuterFolder(true);
		if (pNext == NULL) {
			return this;
		}
	}
	pNext->m_nMarkedIndexShow = -1;
	pNext->m_iter = pNext->m_fileList.begin();
	return pNext;
}

CFileList* CFileList::PrevInFolderList() {
	if (m_fileList.size() > 0 && m_iter != m_fileList.begin()) {
		m_iter--;
		return this;
	}
	// first file of this folder (or no files): continue with the last file of the previous folder
	CFileList* pPrev = AdjacentFolder(false);
	if (pPrev == NULL) {
		if (!WrapsAround()) {
			return this;
		}
		pPrev = OuterFolder(false);
		if (pPrev == NULL) {
			return this;
		}
	}
	pPrev->m_nMarkedIndexShow = -1;
	pPrev->MoveIterToLast();
	return pPrev;
}

// Returns the nearest folder of the loop with images after (before) this folder, without wrap around.
// NULL if there is none.
CFileList* CFileList::AdjacentFolder(bool bForward) {
	if (bForward && m_next != NULL && m_bNextIsAdjacent) {
		return m_next;
	}
	if (!bForward && m_prev != NULL && m_bPrevIsAdjacent) {
		return m_prev;
	}
	if (m_sDirectory.IsEmpty()) {
		return NULL;
	}
	CFileList* pList = (sm_eMode == Helpers::NM_LoopSubDirectories) ? AdjacentSubFolder(bForward) : AdjacentSiblingFolder(bForward);
	if (pList == NULL || pList == this) {
		return NULL;
	}
	if (bForward) {
		InsertAfter(pList, true);
	} else {
		InsertBefore(pList, true);
	}
	return pList;
}

// Returns the first (last) folder of the loop with images, this folder included. NULL if there is none.
CFileList* CFileList::OuterFolder(bool bFirst) {
	if (m_sDirectory.IsEmpty()) {
		return (m_fileList.size() > 0) ? this : NULL;
	}
	CFileList* pList = (sm_eMode == Helpers::NM_LoopSubDirectories) ? OuterSubFolder(bFirst) : OuterSiblingFolder(bFirst);
	if (pList != NULL && pList != this) {
		// keep the chain in folder order: the first (last) folder belongs to its start (end)
		CFileList* pOuter = this;
		if (bFirst) {
			while (pOuter->m_prev != NULL) pOuter = pOuter->m_prev;
			if (pOuter != pList) pOuter->InsertBefore(pList, false);
		} else {
			while (pOuter->m_next != NULL) pOuter = pOuter->m_next;
			if (pOuter != pList) pOuter->InsertAfter(pList, false);
		}
	}
	return pList;
}

// Gets the names of the subfolders of the given folder (without backslash at the end), in folder order
static void GetSubFolders(const CString& sDirectory, std::list<CString>& dirList) {
	CFindFile fileFind;
	if (fileFind.FindFile(sDirectory + _T("\\*"))) {
		do {
			if (fileFind.IsDirectory() && !fileFind.IsDots()) {
				dirList.push_back(fileFind.GetFileName());
			}
		} while (fileFind.FindNextFile());
	}
	dirList.sort(FolderNameLess);
}

// LoopSameFolderLevel: gets the parent folder and the names of all its subfolders (this folder included),
// in folder order. Returns false if this folder has no parent folder.
bool CFileList::GetSiblingFolders(CString& sParentDir, std::list<CString>& dirList) const {
	int nPos = m_sDirectory.ReverseFind(_T('\\'));
	if (nPos <= 0) {
		return false; // root dir - no siblings
	}
	sParentDir = m_sDirectory.Left(nPos);
	GetSubFolders(sParentDir, dirList);
	return true;
}

// LoopSameFolderLevel: see AdjacentFolder(), the result is not yet linked into the chain
CFileList* CFileList::AdjacentSiblingFolder(bool bForward) {
	CString sParentDir;
	std::list<CString> dirList;
	if (!GetSiblingFolders(sParentDir, dirList)) {
		return NULL;
	}
	// compared by name, so this also works if this folder has been deleted or renamed meanwhile
	CString sThisTitle = m_sDirectory.Mid(sParentDir.GetLength() + 1);
	if (!bForward) {
		dirList.reverse();
	}
	for (std::list<CString>::iterator iter = dirList.begin(); iter != dirList.end(); iter++) {
		if (iter->CompareNoCase(sThisTitle) == 0) {
			continue;
		}
		bool bBehindThis = bForward ? FolderNameLess(sThisTitle, *iter) : FolderNameLess(*iter, sThisTitle);
		if (bBehindThis) {
			CFileList* pList = GetFolderList(sParentDir + _T('\\') + *iter, 0);
			if (pList != NULL) {
				return pList;
			}
		}
	}
	return NULL;
}

// LoopSameFolderLevel: see OuterFolder(), the result is not yet linked into the chain
CFileList* CFileList::OuterSiblingFolder(bool bFirst) {
	CString sParentDir;
	std::list<CString> dirList;
	if (GetSiblingFolders(sParentDir, dirList)) {
		if (!bFirst) {
			dirList.reverse();
		}
		for (std::list<CString>::iterator iter = dirList.begin(); iter != dirList.end(); iter++) {
			CFileList* pList = GetFolderList(sParentDir + _T('\\') + *iter, 0);
			if (pList != NULL) {
				return pList;
			}
		}
	}
	return (m_fileList.size() > 0) ? this : NULL;
}

// LoopSubFolders: moves sDirectory (nLevel below the start folder) to the next folder in depth-first
// order, without wrap around. Returns false if there is none.
static bool NextFolderInTree(CString& sDirectory, int& nLevel) {
	std::list<CString> subFolders;
	GetSubFolders(sDirectory, subFolders);
	if (!subFolders.empty()) {
		sDirectory += _T("\\");
		sDirectory += subFolders.front();
		nLevel++;
		return true;
	}
	// no subfolders: the next sibling of this folder or of the nearest parent folder below the start folder
	CString sDir = sDirectory;
	for (int nLvl = nLevel; nLvl > 0; nLvl--) {
		int nPos = sDir.ReverseFind(_T('\\'));
		if (nPos <= 0) {
			break;
		}
		CString sParentDir = sDir.Left(nPos);
		CString sTitle = sDir.Mid(nPos + 1);
		std::list<CString> siblings;
		GetSubFolders(sParentDir, siblings);
		for (std::list<CString>::iterator iter = siblings.begin(); iter != siblings.end(); iter++) {
			if (iter->CompareNoCase(sTitle) != 0 && FolderNameLess(sTitle, *iter)) {
				sDirectory = sParentDir + _T('\\') + *iter;
				nLevel = nLvl;
				return true;
			}
		}
		sDir = sParentDir;
	}
	return false;
}

// LoopSubFolders: moves sDirectory to the last folder of its subtree in depth-first order
static void DescendToLastFolder(CString& sDirectory, int& nLevel) {
	for (;;) {
		std::list<CString> subFolders;
		GetSubFolders(sDirectory, subFolders);
		if (subFolders.empty()) {
			return;
		}
		sDirectory += _T("\\");
		sDirectory += subFolders.back();
		nLevel++;
	}
}

// LoopSubFolders: moves sDirectory (nLevel below the start folder) to the previous folder in depth-first
// order, without wrap around. Returns false if there is none (sDirectory is the start folder).
static bool PrevFolderInTree(CString& sDirectory, int& nLevel) {
	if (nLevel <= 0) {
		return false;
	}
	int nPos = sDirectory.ReverseFind(_T('\\'));
	if (nPos <= 0) {
		return false;
	}
	CString sParentDir = sDirectory.Left(nPos);
	CString sTitle = sDirectory.Mid(nPos + 1);
	std::list<CString> siblings;
	GetSubFolders(sParentDir, siblings);
	for (std::list<CString>::reverse_iterator iter = siblings.rbegin(); iter != siblings.rend(); iter++) {
		if (iter->CompareNoCase(sTitle) != 0 && FolderNameLess(*iter, sTitle)) {
			// the previous sibling's subtree precedes this folder, its last folder comes directly before
			sDirectory = sParentDir + _T('\\') + *iter;
			DescendToLastFolder(sDirectory, nLevel);
			return true;
		}
	}
	// no previous sibling: the parent folder comes directly before (its files precede its subfolders)
	sDirectory = sParentDir;
	nLevel--;
	return true;
}

// LoopSubFolders: see AdjacentFolder(), the result is not yet linked into the chain
CFileList* CFileList::AdjacentSubFolder(bool bForward) {
	CString sDirectory = m_sDirectory;
	int nLevel = m_nLevel;
	while (bForward ? NextFolderInTree(sDirectory, nLevel) : PrevFolderInTree(sDirectory, nLevel)) {
		CFileList* pList = GetFolderList(sDirectory, nLevel);
		if (pList != NULL) {
			return pList;
		}
	}
	return NULL;
}

// LoopSubFolders: see OuterFolder(), the result is not yet linked into the chain
CFileList* CFileList::OuterSubFolder(bool bFirst) {
	CString sDirectory = RootDirectory();
	int nLevel = 0;
	if (!bFirst) {
		DescendToLastFolder(sDirectory, nLevel);
	}
	do {
		CFileList* pList = GetFolderList(sDirectory, nLevel);
		if (pList != NULL) {
			return pList;
		}
	} while (bFirst ? NextFolderInTree(sDirectory, nLevel) : PrevFolderInTree(sDirectory, nLevel));
	return NULL;
}

// The start folder of the loop: LoopSubFolders loops through it and the folders below it
CString CFileList::RootDirectory() const {
	CString sDirectory = m_sDirectory;
	for (int i = 0; i < m_nLevel; i++) {
		int nPos = sDirectory.ReverseFind(_T('\\'));
		if (nPos <= 0) {
			break;
		}
		sDirectory = sDirectory.Left(nPos);
	}
	return sDirectory;
}

// Returns the file list of the given folder (without backslash at the end), from the chain if already
// loaded, else newly created and not yet linked into the chain. NULL if the folder has no images.
CFileList* CFileList::GetFolderList(const CString& sDirectory, int nLevel) {
	CFileList* pList = this;
	while (pList->m_prev != NULL) pList = pList->m_prev;
	for (; pList != NULL; pList = pList->m_next) {
		if (pList->m_sDirectory.CompareNoCase(sDirectory) == 0) {
			return (pList->m_fileList.size() > 0) ? pList : NULL;
		}
	}
	CFileList* pNewList = new CFileList(sDirectory + _T('\\'), CFileDesc::GetSorting(), CFileDesc::IsSortedAscending(), m_bWrapAroundFolder, nLevel);
	if (pNewList->m_fileList.size() == 0) {
		delete pNewList;
		return NULL;
	}
	return pNewList;
}

// Inserts pList into the chain directly behind this list, taking it out of its old place first
void CFileList::InsertAfter(CFileList* pList, bool bAdjacent) {
	if (pList != m_next) {
		pList->Unlink();
		pList->m_next = m_next;
		pList->m_bNextIsAdjacent = false;
		if (m_next != NULL) {
			m_next->m_prev = pList;
			m_next->m_bPrevIsAdjacent = false;
		}
		m_next = pList;
		pList->m_prev = this;
		m_bNextIsAdjacent = pList->m_bPrevIsAdjacent = bAdjacent;
	} else if (bAdjacent) {
		m_bNextIsAdjacent = pList->m_bPrevIsAdjacent = true; // already linked, now known to be adjacent
	}
}

// Inserts pList into the chain directly in front of this list, taking it out of its old place first
void CFileList::InsertBefore(CFileList* pList, bool bAdjacent) {
	if (pList != m_prev) {
		pList->Unlink();
		pList->m_prev = m_prev;
		pList->m_bPrevIsAdjacent = false;
		if (m_prev != NULL) {
			m_prev->m_next = pList;
			m_prev->m_bNextIsAdjacent = false;
		}
		m_prev = pList;
		pList->m_next = this;
		m_bPrevIsAdjacent = pList->m_bNextIsAdjacent = bAdjacent;
	} else if (bAdjacent) {
		m_bPrevIsAdjacent = pList->m_bNextIsAdjacent = true; // already linked, now known to be adjacent
	}
}

// Takes this list out of the chain, its neighbors are linked to each other
void CFileList::Unlink() {
	if (m_prev != NULL) {
		m_prev->m_next = m_next;
		m_prev->m_bNextIsAdjacent = false;
	}
	if (m_next != NULL) {
		m_next->m_prev = m_prev;
		m_next->m_bPrevIsAdjacent = false;
	}
	m_prev = m_next = NULL;
	m_bPrevIsAdjacent = m_bNextIsAdjacent = false;
}

void CFileList::NextInFolder() {
	if (m_fileList.size() > 0) {
		m_iter++;
		if (m_iter == m_fileList.end()) {
			m_iter = m_fileList.begin();
		}
	}
}

void CFileList::FindFiles() {
	m_fileList.clear();
	
	if (m_sDirectory.IsEmpty()) return;

    LPCTSTR* allFileEndings = GetSupportedFileEndingList();
	std::set<CString> validExtensions;
    for (int i = 0; i < nNumEndings; ++i) {
        CString ext = allFileEndings[i];
        ext.MakeLower();
        validExtensions.insert(ext);
    }

	CFindFile fileFind;
	if (fileFind.FindFile(m_sDirectory + _T("\\*"))) {
		// WTL CFindFile: the first entry is already valid after FindFile(),
		// FindNextFile() fetches the following one (unlike MFC).
		// Drive roots (RAM disk, mounted ISO) have no "." / ".." entries.
		do {
			if (fileFind.IsDots() || fileFind.IsDirectory()) continue;

			CString fileName = fileFind.GetFileName();
			int nDotPos = fileName.ReverseFind(_T('.'));
			if (nDotPos != -1) {
				CString ext = fileName.Mid(nDotPos + 1);
				ext.MakeLower();

				if (validExtensions.find(ext) != validExtensions.end()) {
					AddToFileList(m_fileList, fileFind, ext);
				}
			}
		} while (fileFind.FindNextFile());
		fileFind.Close();
	}

	m_fileList.sort();
}

void CFileList::VerifyFiles() {
	std::list<CFileDesc>::iterator iter = m_fileList.begin();
	while (iter != m_fileList.end()) {
		if (::GetFileAttributes(iter->GetName()) == INVALID_FILE_ATTRIBUTES) {
			iter = m_fileList.erase(iter);
		}
		else {
			++iter;
		}
	}
}

bool CFileList::IsImageFile(const CString & sEnding) {
	CString sEndingLC = sEnding;
	sEndingLC.MakeLower();
	LPCTSTR* allFileEndings = GetSupportedFileEndingList();
	for (int i = 0; i < nNumEndings; i++) {
		if (allFileEndings[i] == sEndingLC) {
			return true;
		}
	}
	return false;
}

bool CFileList::TryReadingSlideShowList(const CString & sSlideShowFile) {
	FILE *fptr;
	if ((fptr = _tfopen(sSlideShowFile,_T("rb"))) == NULL) {
		return false;
	}
	// Try to detect if this is a UTF16 Unicode text file or a ANSI or UTF-8 coded file
	const int NUMPROBE = 64;
	uint8 buff[NUMPROBE];
	bool bUnicode;
	bool bUTF8Marker = false;
	int nSeekPos = 0;
	int nRealProbe = (int)fread(buff, 1, NUMPROBE, fptr);
	if (nRealProbe < 16) {
		// file is too short for a good guess
		bUnicode = false;
	} else {
		if (buff[0] == 0xFF && buff[1] == 0xFE) {
			// Unicode byte order marker, only supporting little endian unicode files
			bUnicode = true;
			nSeekPos = 2;
		} else if (buff[0] == 0xEF && buff[1] == 0xBB && buff[2] == 0xBF) {
			// UTF-8 encoded text file
			bUTF8Marker = true;
		} else {
			// Use a heuristic in case the BOM is missing
			int nGood = 0;
			for (int i = 0; i < nRealProbe; i++) {
				if (i & 1) {
					if (buff[i] == 0) nGood++;
				} else {
					if (buff[i] != 0) nGood++;
				}
			}
			bUnicode = nGood > nRealProbe*2/3;
		}
	}

	bool bIsUTF8EncodedXML = strstr((char*)buff, "encoding=\"utf-8\"") != NULL;
	if (bUTF8Marker || bIsUTF8EncodedXML) {
		// UTF-8 encoded text file, open the file in UTF-8 mode, it will be read as Unicode
		fclose(fptr);
		if ((fptr = _tfopen(sSlideShowFile,_T("r, ccs=UTF-8"))) == NULL) {
			return false;
		}
		nSeekPos = bIsUTF8EncodedXML ? max(0, (int)(strchr((char*)buff, '<') - (char*)buff)) : 3;
		bUnicode = true;
	}

	int FILE_BUFFER_SIZE = CSettingsProvider::This().MaxSlideShowFileListSize() * 1024;
	char* fileBuff = new char[FILE_BUFFER_SIZE];
	char* fileBuffOrig = fileBuff;
	wchar_t* wideFileBuff = (wchar_t*)fileBuff; // same buffer interpreted as Unicode
	fseek(fptr, nSeekPos, SEEK_SET);
	int nRealFileSizeChars = (int)fread(fileBuff, 1, FILE_BUFFER_SIZE, fptr);
	if (bUnicode) nRealFileSizeChars = nRealFileSizeChars/2;

	const int LINE_BUFF_SIZE = 512;
	TCHAR lineBuff[LINE_BUFF_SIZE + 1];
	bool bValid = true;
	int nTotalChars = 0;

	do {
		int nNumChars = 0;
		if (bUnicode) {
			wchar_t* pRunning = wideFileBuff;
			bool bIsNull = false;
			while (*pRunning != L'\n' && nNumChars < LINE_BUFF_SIZE && nTotalChars < nRealFileSizeChars) {
				if (*pRunning == 0) {
					bIsNull = true;
				}
				pRunning++; nNumChars++; nTotalChars++;
			}
			bValid = nNumChars < 512 && !bIsNull;
			if (bValid) {
				memcpy(lineBuff, wideFileBuff, (pRunning - wideFileBuff)*sizeof(TCHAR));
			}
			wideFileBuff = pRunning+1;
			nTotalChars++;
		} else {
			char* pRunning = fileBuff;
			bool bIsNull = false;
			while (*pRunning != '\n' && nNumChars < LINE_BUFF_SIZE && nTotalChars < nRealFileSizeChars) {
				if (*pRunning == 0) {
					bIsNull = true;
				}
				pRunning++; nNumChars++; nTotalChars++;
			}
			bValid = nNumChars < LINE_BUFF_SIZE && !bIsNull;
			if (bValid) {
				size_t nDummy;
				mbstowcs_s(&nDummy, lineBuff, LINE_BUFF_SIZE, fileBuff, pRunning - fileBuff);
			}
			fileBuff = pRunning+1;
			nTotalChars++;
		}
		if (!bValid) {
			// the file contains very long lines or null characters, it is most likely not a text file
			fclose(fptr);
			delete[] fileBuffOrig;
			return false;
		}
		// one line in lineBuff, check if there is a valid file name
		bool bRelativePath = false;
		lineBuff[nNumChars] = _T('\n'); // force end
		lineBuff[nNumChars + 1] = 0; // terminate line string
		TCHAR* pStart = _tcsstr(lineBuff, _T("\\\\"));
		if (pStart == NULL) {
			pStart = _tcsstr(lineBuff+1, _T(":\\"));
			if (pStart != NULL) {
				pStart--;
			} else {
				// try to find file name with relative path
				LPCTSTR* allFileEndings = GetSupportedFileEndingList();
				for (int i = 0; i < nNumEndings; i++) {
					pStart = Helpers::stristr(lineBuff, (LPCTSTR)(CString(_T(".")) + allFileEndings[i]));
					if (pStart != NULL) {
						while (pStart >= lineBuff && *pStart != _T('"') && *pStart != _T('>')) pStart--;
						pStart++;
						bRelativePath = true;
						break;
					}
				}
			}
			
		}
		if (pStart != NULL) {
			// extract file name and add to list of files to show
			TCHAR* pEnd = pStart;
			while (*pEnd != _T('\r') && *pEnd != _T('\n') && *pEnd != _T('"') && *pEnd != _T('<')) {
				pEnd++;
			}
			*pEnd = 0;
			CFindFile fileFind;
			CString sPath = bRelativePath ? (m_sDirectory + _T('\\') + pStart) : pStart;
			if (fileFind.FindFile(sPath)) {
				AddToFileList(m_fileList, fileFind, NULL);
			}
		}
	} while (nTotalChars < nRealFileSizeChars);

	fclose(fptr);
	delete[] fileBuffOrig;
	return true;
}

//#####################################################
//# GF: Custom public functions of linear scaling mod #
//#####################################################

// Used by custom SaveBookmark()
bool CFileList::IsEndpoint() {
	m_nMarkedIndexShow = -1;

	if (m_iter == m_fileList.begin())
		return true;

	if (m_fileList.size() > 0)
		{
		std::list<CFileDesc>::iterator iterTemp = m_iter;
		iterTemp++;
		if (iterTemp == m_fileList.end()) {
			return true;
		}
	}

	return false;
}

// Used by JPEGProvider to keep buffer for marked toggle file alive
LPCTSTR CFileList::GetFileMarkedForToggle() {
	if (!m_sMarkedFile.IsEmpty())
		return  (LPCTSTR)m_sMarkedFile;

	return NULL;
}
