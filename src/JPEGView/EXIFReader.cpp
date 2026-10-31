#include "StdAfx.h"
#include "EXIFReader.h"
#include "ImageProcessingTypes.h"
#include "Helpers.h"
#include <cmath>

double CEXIFReader::UNKNOWN_DOUBLE_VALUE = 283740261.192864;

static uint32 ReadUInt(void * ptr, bool bLittleEndian) {
	uint32 nValue = *((uint32*)ptr);
	if (!bLittleEndian) {
		return _byteswap_ulong(nValue);
	} else {
		return nValue;
	}
}

static void WriteUInt(void * ptr, uint32 nValue, bool bLittleEndian) {
	uint32 nVal = nValue;
	if (!bLittleEndian) {
		nVal = _byteswap_ulong(nVal);
	}
	*((uint32*)ptr) = nVal;
}

static uint16 ReadUShort(void * ptr, bool bLittleEndian) {
	uint16 nValue = *((uint16*)ptr);
	if (!bLittleEndian) {
		return _byteswap_ushort(nValue);
	} else {
		return nValue;
	}
}

static void WriteUShort(void * ptr, uint16 nValue, bool bLittleEndian) {
	uint16 nVal = nValue;
	if (!bLittleEndian) {
		nVal = _byteswap_ushort(nVal);
	}
	*((uint16*)ptr) = nVal;
}

// The TIFF data of the EXIF block (behind "Exif\0\0"): all offsets in the EXIF data are relative to its start.
// Offsets and lengths come from the file and can be corrupt or manipulated, so every access with them is checked
// with Contains() (in 64 bit, offset + length can not overflow).
struct TiffData {
	uint8* p;		// start of the TIFF header
	uint32 size;	// number of bytes from p to the end of the APP1 block

	bool Contains(uint64_t offset, uint64_t length) const { return offset <= size && length <= size - offset; }
};

// The entries (12 bytes each) between ptr and ptrLast must be inside the data, the caller checks this
static uint8* FindTag(uint8* ptr, uint8* ptrLast, uint16 nTag, bool bLittleEndian) {
	while (ptr < ptrLast) {
		if (ReadUShort(ptr, bLittleEndian) == nTag) {
			return ptr;
		}
		ptr += 12;
	}
	return NULL;
}

static void ReadStringTag(CString & strOut, uint8* ptr, const TiffData& tiff, bool bLittleEndian, bool bTryReadAsUTF8 = false, uint32 maxLength = 65536) {
	try {
		if (ptr != NULL && ReadUShort(ptr + 2, bLittleEndian) == 2) {
			uint32 nSize = ReadUInt(ptr + 4, bLittleEndian);
			if (nSize > maxLength) {
				strOut.Empty();
				return;
			}
			LPCSTR pString;
			if (nSize <= 4) {
				pString = (LPCSTR)(ptr + 8); // stored in the entry itself
			} else {
				const uint32 nOffset = ReadUInt(ptr + 8, bLittleEndian);
				if (!tiff.Contains(nOffset, nSize)) {
					strOut.Empty();
					return;
				}
				pString = (LPCSTR)(tiff.p + nOffset);
			}
			const size_t nLen = strnlen(pString, nSize);
			int nMaxChars = (int)(nLen < nSize ? nLen : nSize);
			if (bTryReadAsUTF8) {
				CString strUTF8 = Helpers::TryConvertFromUTF8((uint8*)pString, nMaxChars);
				if (!strUTF8.IsEmpty()) {
					strOut = strUTF8;
					return;
				}
			}
			strOut = CString(pString, nMaxChars);
		} else {
			strOut.Empty();
		}
	} catch (...) {
		// EXIF corrupt?
		strOut.Empty();
	}
}

static void ReadUserCommentTag(CString & strOut, uint8* ptr, const TiffData& tiff, bool bLittleEndian) {
	try {
		if (ptr != NULL && ReadUShort(ptr + 2, bLittleEndian) == 7) {
			const uint32 nSize32 = ReadUInt(ptr + 4, bLittleEndian);
			const uint32 nOffset = ReadUInt(ptr + 8, bLittleEndian);
			if (nSize32 > 10 && nSize32 <= 4096 && tiff.Contains(nOffset, nSize32)) {
				int nSize = (int)nSize32;
				// 8 bytes character code, then the comment
				LPCSTR sCodeDesc = (LPCSTR)(tiff.p + nOffset);
				if (memcmp(sCodeDesc, "ASCII\0", 6) == 0) {
					strOut = CString((LPCSTR)(sCodeDesc + 8), nSize - 8);
				} else if (memcmp(sCodeDesc, "UNICODE\0", 8) == 0) {
					bool bLE = bLittleEndian;
					nSize -= 8;
					sCodeDesc += 8;
					if (sCodeDesc[0] == 0xFF && sCodeDesc[1] == 0xFE) {
						bLE = true;
						nSize -= 2; sCodeDesc += 2;
					} else if (sCodeDesc[0] == 0xFE && sCodeDesc[1] == 0xFF) {
						bLE = false;
						nSize -= 2; sCodeDesc += 2;
					}
					if (bLE) {
						strOut = CString((LPCWSTR)sCodeDesc, nSize / 2);
					} else {
						// swap 16 bit characters to little endian
						char* pString = new char[nSize];
						char* pSwap = pString;
						memcpy(pString, sCodeDesc, nSize);
						for (int i = 0; i < nSize / 2; i++) {
							char t = *pSwap;
							*pSwap = pSwap[1];
							pSwap[1] = t;
							pSwap += 2;
						}
						strOut = CString((LPCWSTR)pString, nSize / 2);
						delete[] pString;
					}
				}
			} // else comment is corrupt or too big
		}
	} catch (...) {
		// EXIF corrupt?
		strOut.Empty();
	}
}

static uint32 ReadLongTag(uint8* ptr, bool bLittleEndian) {
	if (ptr != NULL && ReadUShort(ptr + 2, bLittleEndian) == 4) {
		return ReadUInt(ptr + 8, bLittleEndian);
	} else {
		return 0;
	}
}

static uint16 ReadShortTag(uint8* ptr, bool bLittleEndian) {
	if (ptr != NULL && ReadUShort(ptr + 2, bLittleEndian) == 3) {
		return ReadUShort(ptr + 8, bLittleEndian);
	} else {
		return 0;
	}
}

static uint32 ReadShortOrLongTag(uint8* ptr, bool bLittleEndian) {
	if (ptr != NULL) {
		uint16 nType = ReadUShort(ptr + 2, bLittleEndian);
		if (nType == 3) {
			return ReadUShort(ptr + 8, bLittleEndian);
		} else if (nType == 4) {
			return ReadUInt(ptr + 8, bLittleEndian);
		}
	}
	return 0;
}

static void WriteShortTag(uint8* ptr, uint16 nValue, bool bLittleEndian) {
	if (ptr != NULL && ReadUShort(ptr + 2, bLittleEndian) == 3) {
		WriteUShort(ptr + 8, nValue, bLittleEndian);
	}
}

static void WriteLongTag(uint8* ptr, uint32 nValue, bool bLittleEndian) {
	if (ptr != NULL && ReadUShort(ptr + 2, bLittleEndian) == 4) {
		WriteUInt(ptr + 8, nValue, bLittleEndian);
	}
}

// Absolute value of a signed 32 bit value as unsigned (also for INT_MIN, where abs() is undefined)
static uint32 AbsSigned(uint32 nValue) {
	const int64_t n = (int32_t)nValue;
	return (uint32)(n < 0 ? -n : n);
}

static int ReadRationalTag(Rational & rational, uint8* ptr, const TiffData& tiff, int index, bool bLittleEndian) {
	rational.Numerator = 0;
	rational.Denominator = 0;
	if (ptr != NULL) {
		uint16 nType = ReadUShort(ptr + 2, bLittleEndian);
		uint16 nCount = ReadUInt(ptr + 4, bLittleEndian);
		if (index < nCount) {
			if (nType == 5 || nType == 10) {
				const uint64_t nOffset = (uint64_t)ReadUInt(ptr + 8, bLittleEndian) + (uint64_t)index * 8;
				if (!tiff.Contains(nOffset, 8)) {
					return nType; // value outside of the data: unknown
				}
				rational.Numerator = ReadUInt(tiff.p + nOffset, bLittleEndian);
				rational.Denominator = ReadUInt(tiff.p + nOffset + 4, bLittleEndian);
				if (rational.Numerator != 0 && rational.Denominator != 0) {
					// Calculate the ggT
					uint32 nModulo;
					uint32 nA = (nType == 10) ? AbsSigned(rational.Numerator) : rational.Numerator;
					uint32 nB = (nType == 10) ? AbsSigned(rational.Denominator) : rational.Denominator;
					do {
						nModulo = nA % nB;
						nA = nB;
						nB = nModulo;
					} while (nB != 0);
					// normalize
					if (nType == 10) {
						// in 64 bit: INT_MIN / -1 would overflow in 32 bit
						rational.Numerator = (uint32)((int64_t)(int32_t)rational.Numerator / (int64_t)nA);
						rational.Denominator = (uint32)((int64_t)(int32_t)rational.Denominator / (int64_t)nA);
					}
					else {
						rational.Numerator /= nA;
						rational.Denominator /= nA;
					}
				}
			}
		}
		return nType;
	}
	return 0;
}

static int ReadRationalTag(Rational & rational, uint8* ptr, const TiffData& tiff, bool bLittleEndian) {
	return ReadRationalTag(rational, ptr, tiff, 0, bLittleEndian);
}

static bool ReadSignedRationalTag(SignedRational & rational, uint8* ptr, const TiffData& tiff, bool bLittleEndian) {
	return ReadRationalTag((Rational &)rational, ptr, tiff, bLittleEndian);
}

static double ReadDoubleTag(uint8* ptr, const TiffData& tiff, int index, bool bLittleEndian) {
	Rational rational(0, 0);
	int nType = ReadRationalTag(rational, ptr, tiff, index, bLittleEndian);
	if (rational.Denominator != 0) {
		if (nType == 5) {
			return (double)rational.Numerator / rational.Denominator;
		} else {
			int nNum = rational.Numerator;
			int nDenum = rational.Denominator;
			return (double)nNum/nDenum;
		}
	}
	return CEXIFReader::UNKNOWN_DOUBLE_VALUE;
}

static double ReadDoubleTag(uint8* ptr, const TiffData& tiff, bool bLittleEndian) {
	return ReadDoubleTag(ptr, tiff, 0, bLittleEndian);
}

bool CEXIFReader::ParseDateString(SYSTEMTIME & date, const CString& str) {
	int nYear, nMonth, nDay, nHour, nMin, nSec;
	if (_stscanf(str, _T("%d:%d:%d %d:%d:%d"), &nYear, &nMonth, &nDay, &nHour, &nMin, &nSec) == 6) {
		date.wYear = nYear;
		date.wMonth = nMonth;
		date.wDay = nDay;
		date.wHour = nHour;
		date.wMinute = nMin;
		date.wSecond = nSec;
		date.wMilliseconds = 0;
		date.wDayOfWeek = 0;
		return true;
	}
	return false;
}

CEXIFReader::CEXIFReader(void* pApp1Block, int nBlockSize, EImageFormat eImageFormat)
	: m_exposureTime{ 0, 0 },
	m_acqDate{ 0 },
	m_dateTime{ 0 }
{
	m_bFlashFired = false;
	m_bFlashFlagPresent = false;
	m_dFocalLength = m_dExposureBias = m_dFNumber = UNKNOWN_DOUBLE_VALUE;
	m_nISOSpeed = 0;
	m_nImageOrientation = 0;
	m_pTagOrientation = NULL;
	m_bLittleEndian = true;
	m_nThumbWidth = -1;
	m_nThumbHeight = -1;
	m_pLastIFD0 = NULL;
	m_pIFD1 = NULL;
	m_pLastIFD1 = NULL;
	m_bHasJPEGCompressedThumbnail = false;
	m_nJPEGThumbStreamLen = 0;
	m_pLatitude = NULL;
	m_pLongitude = NULL;
	m_dAltitude = UNKNOWN_DOUBLE_VALUE;

	m_pApp1 = (uint8*)pApp1Block;
	// APP1 marker (2) + length (2) + "Exif\0\0" (6) + TIFF header (8)
	if (m_pApp1 == NULL || nBlockSize < 18 || m_pApp1[0] != 0xFF || m_pApp1[1] != 0xE1) {
		return;
	}
	// The length stored in the block must not reach beyond the buffer
	const int nApp1Size = min(nBlockSize, m_pApp1[2]*256 + m_pApp1[3] + 2);
	if (nApp1Size < 18) {
		return;
	}
	const TiffData tiff = { m_pApp1 + 10, (uint32)(nApp1Size - 10) };

	// Read TIFF header
	uint8* pTIFFHeader = tiff.p;
	bool bLittleEndian;
	if (*(short*)pTIFFHeader == 0x4949) {
		bLittleEndian = true;
	} else if (*(short*)pTIFFHeader == 0x4D4D) {
		bLittleEndian = false;
	} else {
		return;
	}
	m_bLittleEndian = bLittleEndian;

	// An IFD is: number of entries (2 bytes), the entries (12 bytes each), offset of the next IFD (4 bytes)
	const uint32 nOffsetIFD0 = ReadUInt(pTIFFHeader + 4, bLittleEndian);
	if (!tiff.Contains(nOffsetIFD0, 2)) {
		return;
	}
	uint8* pIFD0 = tiff.p + nOffsetIFD0;
	// Read IFD0
	uint16 nNumTags = ReadUShort(pIFD0, bLittleEndian);
	pIFD0 += 2;
	if (!tiff.Contains((uint64_t)nOffsetIFD0 + 2, (uint64_t)nNumTags * 12 + 4)) {
		return;
	}
	uint8* pLastIFD0 = pIFD0 + nNumTags*12;
	uint32 nOffsetIFD1 = ReadUInt(pLastIFD0, bLittleEndian);
	m_pLastIFD0 = pLastIFD0;

	// image orientation
	uint8* pTagOrientation = NULL;
	// orientation tags must be ignored for JXL and HEIF/AVIF
	if (eImageFormat != IF_JXL && eImageFormat != IF_HEIF && eImageFormat != IF_AVIF) {
		pTagOrientation = FindTag(pIFD0, pLastIFD0, 0x112, bLittleEndian);
	}
	if (pTagOrientation != NULL) {
		m_nImageOrientation = ReadShortTag(pTagOrientation, bLittleEndian);
	}
	m_pTagOrientation = pTagOrientation;

	uint8* pTagModel = FindTag(pIFD0, pLastIFD0, 0x110, bLittleEndian);
	ReadStringTag(m_sModel, pTagModel, tiff, bLittleEndian);

	uint8* pTagImageDesc = FindTag(pIFD0, pLastIFD0, 0x10E, bLittleEndian);
	ReadStringTag(m_sImageDescription, pTagImageDesc, tiff, bLittleEndian, true);

	// Add the manufacturer name if not contained in model name
	if (!m_sModel.IsEmpty()) {
		CString sMake;
		uint8* pTagMake = FindTag(pIFD0, pLastIFD0, 0x10F, bLittleEndian);
		ReadStringTag(sMake, pTagMake, tiff, bLittleEndian);
		if (!sMake.IsEmpty()) {
			int nSpace = sMake.Find(_T(' '));
			CString sMakeL(nSpace > 0 ? sMake.Left(nSpace) : sMake);
			if (m_sModel.Find(sMakeL) == -1) {
				m_sModel = sMakeL + _T(" ") + m_sModel;
			}
		}
	}

	uint8* pTagSoftware = FindTag(pIFD0, pLastIFD0, 0x0131, bLittleEndian);
	ReadStringTag(m_sSoftware, pTagSoftware, tiff, bLittleEndian);

	uint8* pTagModDate = FindTag(pIFD0, pLastIFD0, 0x0132, bLittleEndian);
	CString sModDate;
	ReadStringTag(sModDate, pTagModDate, tiff, bLittleEndian);
	ParseDateString(m_dateTime, sModDate);

	uint8* pTagEXIFIFD = FindTag(pIFD0, pLastIFD0, 0x8769, bLittleEndian);
	if (pTagEXIFIFD == NULL) {
		return;
	}

	uint8* pTagGPSIFD = FindTag(pIFD0, pLastIFD0, 0x8825, bLittleEndian);
	if (pTagGPSIFD != NULL) {
		ReadGPSData(tiff.p, tiff.size, pTagGPSIFD, bLittleEndian);
	}

	// Read EXIF IFD
	uint32 nOffsetEXIF = ReadLongTag(pTagEXIFIFD, bLittleEndian);
	if (nOffsetEXIF == 0) {
		return;
	}
	if (!tiff.Contains(nOffsetEXIF, 2)) {
		return;
	}
	uint8* pEXIFIFD = tiff.p + nOffsetEXIF;
	nNumTags = ReadUShort(pEXIFIFD, bLittleEndian);
	pEXIFIFD += 2;
	if (!tiff.Contains((uint64_t)nOffsetEXIF + 2, (uint64_t)nNumTags * 12)) {
		return;
	}
	uint8* pLastEXIF = pEXIFIFD + nNumTags*12;
	uint8* pTagAcquisitionDate = FindTag(pEXIFIFD, pLastEXIF, 0x9003, bLittleEndian);
	CString sAcqDate;
	ReadStringTag(sAcqDate, pTagAcquisitionDate, tiff, bLittleEndian);
	ParseDateString(m_acqDate, sAcqDate);

	uint8* pTagExposureTime = FindTag(pEXIFIFD, pLastEXIF, 0x829A, bLittleEndian);
	ReadRationalTag(m_exposureTime, pTagExposureTime, tiff, bLittleEndian);

	uint8* pTagExposureBias = FindTag(pEXIFIFD, pLastEXIF, 0x9204, bLittleEndian);
	m_dExposureBias = ReadDoubleTag(pTagExposureBias, tiff, bLittleEndian);

	uint8* pTagFlash = FindTag(pEXIFIFD, pLastEXIF, 0x9209, bLittleEndian);
	uint16 nFlash = ReadShortTag(pTagFlash, bLittleEndian);
	m_bFlashFired = (nFlash & 1) != 0;
	m_bFlashFlagPresent = pTagFlash != NULL;

	uint8* pTagFocalLength = FindTag(pEXIFIFD, pLastEXIF, 0x920A, bLittleEndian);
	m_dFocalLength = ReadDoubleTag(pTagFocalLength, tiff, bLittleEndian);

	uint8* pTagFNumber = FindTag(pEXIFIFD, pLastEXIF, 0x829D, bLittleEndian);
	m_dFNumber = ReadDoubleTag(pTagFNumber, tiff, bLittleEndian);

	uint8* pTagISOSpeed = FindTag(pEXIFIFD, pLastEXIF, 0x8827, bLittleEndian);

	uint8* pTagISOSpeed2 = FindTag(pEXIFIFD, pLastEXIF, 0x8833, bLittleEndian);

	m_nISOSpeed = (pTagISOSpeed != NULL) ? ReadShortTag(pTagISOSpeed, bLittleEndian) :
		ReadLongTag(pTagISOSpeed2, bLittleEndian);

	uint8* pTagUserComment = FindTag(pEXIFIFD, pLastEXIF, 0x9286, bLittleEndian);
	ReadUserCommentTag(m_sUserComment, pTagUserComment, tiff, bLittleEndian);
	// Samsung Galaxy puts this useless comment into each JPEG, just ignore
	if (m_sUserComment == "User comments") {	
		m_sUserComment = "";
	}

	// https://exiv2.org/tags.html
	// uint8* pTagXPComment = FindTag(pIFD0, pLastIFD0, 0x9c9c, bLittleEndian);  // this is the XPComment tag to resolve this issue https://github.com/sylikc/jpegview/issues/72 , but I'm not sure how to decode it


	if (nOffsetIFD1 != 0) {
		if (!tiff.Contains(nOffsetIFD1, 2)) {
			return;
		}
		uint8* pIFD1 = tiff.p + nOffsetIFD1;
		nNumTags = ReadUShort(pIFD1, bLittleEndian);
		pIFD1 += 2;
		if (!tiff.Contains((uint64_t)nOffsetIFD1 + 2, (uint64_t)nNumTags * 12)) {
			return;
		}
		m_pIFD1 = pIFD1;
		uint8* pLastIFD1 = m_pIFD1 + nNumTags*12;
		m_pLastIFD1 = pLastIFD1;
		uint8* pTagCompression = FindTag(m_pIFD1, pLastIFD1, 0x103, bLittleEndian);
		if (pTagCompression == NULL) {
			return;
		}
		if (ReadShortTag(pTagCompression, bLittleEndian) == 6) {
			// compressed
			uint8* pTagOffsetSOI = FindTag(m_pIFD1, pLastIFD1, 0x0201, bLittleEndian);
			uint8* pTagJPEGLen = FindTag(m_pIFD1, pLastIFD1, 0x0202, bLittleEndian);
			if (pTagOffsetSOI != NULL && pTagJPEGLen != NULL) {
				uint32 nOffsetSOI = ReadLongTag(pTagOffsetSOI, bLittleEndian);
				uint32 nJPEGBytes = ReadLongTag(pTagJPEGLen, bLittleEndian);
				if (!tiff.Contains(nOffsetSOI, nJPEGBytes)) {
					return; // the thumbnail is not (completely) inside of the EXIF data
				}
				uint8* pSOI = tiff.p + nOffsetSOI;
				uint8* pSOF = (uint8*) Helpers::FindJPEGMarker(pSOI, (int)nJPEGBytes, 0xC0);
				// the SOF segment is read up to byte 8 (height, width)
				if (pSOF != NULL && pSOF + 9 <= pSOI + nJPEGBytes) {
					m_nThumbWidth = pSOF[7]*256 + pSOF[8];
					m_nThumbHeight = pSOF[5]*256 + pSOF[6];
					m_nJPEGThumbStreamLen = nJPEGBytes;
					m_bHasJPEGCompressedThumbnail = true;
				}
			}
		} else {
			// uncompressed
			uint8* pTagThumbWidth = FindTag(m_pIFD1, pLastIFD1, 0x0001, bLittleEndian);
			uint8* pTagThumbHeight = FindTag(m_pIFD1, pLastIFD1, 0x0101, bLittleEndian);
			if (pTagThumbWidth != NULL && pTagThumbHeight != NULL) {
				m_nThumbWidth = ReadShortOrLongTag(pTagThumbWidth, bLittleEndian);
				m_nThumbHeight = ReadShortOrLongTag(pTagThumbHeight, bLittleEndian);
			}
		}
	}
}

CEXIFReader::~CEXIFReader(void) {
	delete m_pLatitude;
	delete m_pLongitude;
}

void CEXIFReader::WriteImageOrientation(int nOrientation) {
	if (m_pTagOrientation != NULL && ImageOrientationPresent()) {
		WriteShortTag(m_pTagOrientation, nOrientation, m_bLittleEndian);
	}
}

void CEXIFReader::DeleteThumbnail() {
	if (m_pLastIFD0 != NULL) {
		*m_pLastIFD0 = 0;
	}
}

void CEXIFReader::UpdateJPEGThumbnail(unsigned char* pJPEGStream, int nStreamLen, int nEXIFBlockLenCorrection, CSize sizeThumb) {
	if (!m_bHasJPEGCompressedThumbnail) {
		return;
	}
	uint8* pTagOffsetSOI = FindTag(m_pIFD1, m_pLastIFD1, 0x0201, m_bLittleEndian);
	uint32 nOffsetSOI = ReadLongTag(pTagOffsetSOI, m_bLittleEndian);
	uint8* pTIFFHeader = m_pApp1 + 10;
	uint8* pSOI = pTIFFHeader + nOffsetSOI;
	memcpy(pSOI + 2, pJPEGStream, nStreamLen);

	uint8* pTagJPEGBytes = FindTag(m_pIFD1, m_pLastIFD1, 0x0202, m_bLittleEndian);
	WriteLongTag(pTagJPEGBytes, nStreamLen + 2, m_bLittleEndian);
	int nNewApp1Size = m_pApp1[2]*256 + m_pApp1[3] + nEXIFBlockLenCorrection;
	m_pApp1[2] = nNewApp1Size >> 8;
	m_pApp1[3] = nNewApp1Size & 0xFF;
}

void CEXIFReader::ReadGPSData(uint8* pTIFFHeader, uint32 nTIFFSize, uint8* pTagGPSIFD, bool bLittleEndian) {
	const TiffData tiff = { pTIFFHeader, nTIFFSize };
	uint32 nOffsetGPS = ReadLongTag(pTagGPSIFD, bLittleEndian);
	if (nOffsetGPS == 0 || !tiff.Contains(nOffsetGPS, 2)) {
		return;
	}
	uint8* pGPSIFD = tiff.p + nOffsetGPS;
	int nNumTags = ReadUShort(pGPSIFD, bLittleEndian);
	pGPSIFD += 2;
	if (!tiff.Contains((uint64_t)nOffsetGPS + 2, (uint64_t)nNumTags * 12)) {
		return;
	}
	uint8* pLastGPS = pGPSIFD + nNumTags * 12;

	uint8* pTagLatitudeRef = FindTag(pGPSIFD, pLastGPS, 0x1, bLittleEndian);
	if (pTagLatitudeRef == NULL)
		return;
	CString latitudeRef;
	ReadStringTag(latitudeRef, pTagLatitudeRef, tiff, bLittleEndian, false, 2);

	uint8* pTagLatitude = FindTag(pGPSIFD, pLastGPS, 0x2, bLittleEndian);
	m_pLatitude = ReadGPSCoordinate(tiff.p, tiff.size, pTagLatitude, latitudeRef, bLittleEndian);

	uint8* pTagLongitudeRef = FindTag(pGPSIFD, pLastGPS, 0x3, bLittleEndian);
	if (pTagLongitudeRef == NULL)
		return;
	CString longitudeRef;
	ReadStringTag(longitudeRef, pTagLongitudeRef, tiff, bLittleEndian, false, 2);

	uint8* pTagLongitude = FindTag(pGPSIFD, pLastGPS, 0x4, bLittleEndian);
	m_pLongitude = ReadGPSCoordinate(tiff.p, tiff.size, pTagLongitude, longitudeRef, bLittleEndian);

	uint8* pTagAltitude = FindTag(pGPSIFD, pLastGPS, 0x6, bLittleEndian);
	if (pTagAltitude != NULL) {
		m_dAltitude = ReadDoubleTag(pTagAltitude, tiff, bLittleEndian);
		uint8* pTagAltitudeRef = FindTag(pGPSIFD, pLastGPS, 0x5, bLittleEndian);
		if (pTagAltitudeRef != NULL && *(pTagAltitudeRef + 8) == 1) {
			m_dAltitude *= -1;
		}
	}
}

GPSCoordinate* CEXIFReader::ReadGPSCoordinate(uint8* pTIFFHeader, uint32 nTIFFSize, uint8* pTagLatOrLong, LPCTSTR reference, bool bLittleEndian) {
	if (pTagLatOrLong == NULL)
		return NULL;
	const TiffData tiff = { pTIFFHeader, nTIFFSize };
	double dDeg = ReadDoubleTag(pTagLatOrLong, tiff, 0, bLittleEndian);
	double dMin = ReadDoubleTag(pTagLatOrLong, tiff, 1, bLittleEndian);
	double dSec = ReadDoubleTag(pTagLatOrLong, tiff, 2, bLittleEndian);
	if (dDeg == UNKNOWN_DOUBLE_VALUE || dMin == UNKNOWN_DOUBLE_VALUE || dSec == UNKNOWN_DOUBLE_VALUE)
		return NULL;
	// GPSCoordinate converts the values to int: absurd values (corrupt data) would not fit
	const double cdMaxValue = 1e6;
	if (!(fabs(dDeg) <= cdMaxValue && fabs(dMin) <= cdMaxValue && fabs(dSec) <= cdMaxValue))
		return NULL;

	return new GPSCoordinate(reference, dDeg, dMin, dSec);
}
