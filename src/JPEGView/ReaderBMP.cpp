#include "StdAfx.h"
#include "ReaderBMP.h"
#include "JPEGImage.h"
#include "Helpers.h"
#include "BasicProcessing.h"
#include "MaxImageDef.h"

//////////////////////////////////////////////////////////////////////////////////
// BITMAP reading

struct BMHEADER {
	unsigned short int type;                 /* Magic identifier            */
	unsigned int size;                       /* File size in bytes          */
	unsigned short int reserved1, reserved2;
	unsigned int offset;                     /* Offset to image data, bytes */
};

struct BMINFOHEADER {
	unsigned int size;               /* Header size in bytes      */
	int width,height;                /* Width and height of image */
	unsigned short int planes;       /* Number of color planes    */
	unsigned short int bits;         /* Bits per pixel            */
	unsigned int compression;        /* Compression type          */
	unsigned int imagesize;          /* Image size in bytes       */
	int xresolution,yresolution;     /* Pixels per meter          */
	unsigned int ncolors;            /* Number of colors          */
	unsigned int importantcolors;    /* Important colors          */
};

static void ReadUShort(FILE* file, uint16* pUShort) {
	fread(pUShort, sizeof(uint16), 1, file);
}

static void ReadUInt(FILE* file, uint32* pUInt) {
	fread(pUInt, sizeof(uint32), 1, file);
}

CJPEGImage* CReaderBMP::ReadBmpImage(void* pBuffer, int bufsize, bool& bOutOfMemory, EImageFormat eContainerFormat, bool bIsAnimation, int nFrameIndex, int nNumberOfFrames, int nFrameTimeMs)
{
	BMHEADER header;
	BMINFOHEADER infoheader;

	bOutOfMemory = false;

	if (pBuffer == NULL || bufsize <= 0) return NULL;
	const std::size_t nBufSize = (std::size_t)bufsize;

	// size_t: offsets and sizes come from the file and must never become negative
	std::size_t off = 0, amtRead = 0;
	char* pBuf = (char*)pBuffer;
	/* Read the header */
	amtRead += 14;
	if (nBufSize < amtRead) return NULL;
	header.type = *((uint16*)pBuf); pBuf = pBuf + sizeof(uint16);
	header.size = *((uint32*)pBuf); pBuf += sizeof(uint32);
	header.reserved1 = *((uint16*)pBuf); pBuf += sizeof(uint16);
	header.reserved1 = *((uint16*)pBuf); pBuf += sizeof(uint16);
	header.offset = *((uint32*)pBuf); pBuf += sizeof(uint32);

	/* Read and check the information header */
	std::size_t size = sizeof(BMINFOHEADER);
	amtRead += size;
	if (nBufSize < amtRead) return NULL;
	memcpy(&infoheader, pBuf, size);
	//pBuf += size;

	/* Only 24 and 32 bpp */
	if (infoheader.bits != 24 && infoheader.bits != 32 && infoheader.bits != 8) {
		return NULL;
	}
	/* Not too big files (height is negative for top-down DIBs) */
	if (infoheader.width <= 0 || infoheader.width > (int)MAX_IMAGE_DIMENSION ||
		infoheader.height == 0 || infoheader.height < -(int)MAX_IMAGE_DIMENSION || infoheader.height > (int)MAX_IMAGE_DIMENSION) {
		return NULL;
	}
	if ((double)infoheader.width * abs(infoheader.height) > MAX_IMAGE_PIXELS) {
		bOutOfMemory = true;
		return NULL;
	}

	// read palette for 8 bpp DIBs
	uint8 palette[4 * 256];
	if (infoheader.bits == 8) {
		off = (std::size_t)infoheader.size + 14;
		if (nBufSize < off) return NULL;
		pBuf = ((char*)pBuffer) + off;
		size = 256 * 4;
		amtRead = off + size;
		if (nBufSize < amtRead) return NULL;
		memcpy(palette, pBuf, size);
		//pBuf += size;
	}

	/* Seek to the start of the image data */
	amtRead = off = header.offset;
	if (nBufSize < off) return NULL;
	pBuf = ((char*)pBuffer) + off;

	// DIBs are normally stored flipped vertically (meaning they are stored bottom-up)
	bool bFlipped;
	if (infoheader.height < 0) {
		infoheader.height = -infoheader.height;
		bFlipped = false;
	}
	else {
		bFlipped = true;
	}

	int bytesPerPixel = infoheader.bits / 8;
	int paddedWidth = Helpers::DoPadding(infoheader.width * bytesPerPixel, 4);
	// 64 bit: width and height can be up to 65535 each, so the product overflows an int (the buffer would be
	// allocated far too small and the copy below would write beyond it)
	int64_t nImageBytes = (int64_t)infoheader.height * paddedWidth;
	if (nImageBytes > MAX_BMP_FILE_SIZE) {
		bOutOfMemory = true;
		return NULL; // corrupt or manipulated header
	}
	int fileSizeBytes = (int)nImageBytes; // <= MAX_BMP_FILE_SIZE, fits into an int
	if (nBufSize - off < (std::size_t)fileSizeBytes) {
		return NULL; // the file can not contain the pixel data: do not allocate memory for it
	}

	uint8* pDest = new(std::nothrow) uint8[fileSizeBytes];
	if (pDest == NULL) {
		bOutOfMemory = true;
		return NULL;
	}
	uint8* pStart = bFlipped ? pDest + (std::size_t)paddedWidth * (infoheader.height - 1) : pDest;
	for (int nLine = 0; nLine < infoheader.height; nLine++) {
		amtRead += paddedWidth;
		if (nBufSize < amtRead)
		{
			delete[] pDest;
			return NULL;
		}
		memcpy(pStart, pBuf, paddedWidth);
		pBuf += paddedWidth;
		pStart = pStart + (bFlipped ? -paddedWidth : paddedWidth);
	}

	// Convert 8 bpp DIBs
	if (infoheader.bits == 8) {
		uint8* pTemp = (uint8*)CBasicProcessing::Convert8bppTo32bppDIB(infoheader.width, infoheader.height, pDest, palette);
		delete[] pDest;
		if (pTemp == NULL) {
			bOutOfMemory = true;
			return NULL;
		}
		pDest = pTemp;
		infoheader.bits = 32;
	}

	// The CJPEGImage object gets ownership of the memory in pDest
	CJPEGImage* pImage = (pDest == NULL) ? NULL : new CJPEGImage(infoheader.width, infoheader.height, pDest, NULL, infoheader.bits / 8,
		0, IF_WindowsBMP, eContainerFormat, bIsAnimation, nFrameIndex, nNumberOfFrames, nFrameTimeMs);

	bOutOfMemory = pImage == NULL;

	return pImage;
}

CJPEGImage* CReaderBMP::ReadBmpImage(LPCTSTR strFileName, bool& bOutOfMemory) {
	BMHEADER header;
	BMINFOHEADER infoheader;
	FILE *fptr;

	bOutOfMemory = false;

	/* Open file */
	if ((fptr = _tfopen(strFileName,_T("rb"))) == NULL) {
		return NULL;
	}

	/* Read the header */
	ReadUShort(fptr,&header.type);
	ReadUInt(fptr,&header.size);
	ReadUShort(fptr,&header.reserved1);
	ReadUShort(fptr,&header.reserved2);
	ReadUInt(fptr,&header.offset);

	/* Read and check the information header */
	if (fread(&infoheader,sizeof(BMINFOHEADER),1,fptr) != 1) {
		fclose(fptr);
		return NULL;
	}
	/* Only 24 and 32 bpp */
	if (infoheader.bits != 24 && infoheader.bits != 32 && infoheader.bits != 8) {
		fclose(fptr);
		return NULL;
	}
	/* Not too big files (height is negative for top-down DIBs) */
	if (infoheader.width <= 0 || infoheader.width > (int)MAX_IMAGE_DIMENSION ||
		infoheader.height == 0 || infoheader.height < -(int)MAX_IMAGE_DIMENSION || infoheader.height > (int)MAX_IMAGE_DIMENSION) {
		fclose(fptr);
		return NULL;
	}
	if ((double)infoheader.width * abs(infoheader.height) > MAX_IMAGE_PIXELS) {
		fclose(fptr);
		bOutOfMemory = true;
		return NULL;
	}

	// read palette for 8 bpp DIBs
	uint8 palette[4*256];
	if (infoheader.bits == 8) {
		if (_fseeki64(fptr, (int64_t)infoheader.size + 14, SEEK_SET) != 0 || fread(palette, 256*4, 1, fptr) != 1) {
			fclose(fptr);
			return NULL;
		}
	}

	/* Seek to the start of the image data */
	if (_fseeki64(fptr, (int64_t)header.offset, SEEK_SET) != 0) {
		fclose(fptr);
		return NULL;
	}

	// DIBs are normally stored flipped vertically (meaning they are stored bottom-up)
	bool bFlipped;
	if (infoheader.height < 0) {
		infoheader.height = -infoheader.height;
		bFlipped = false;
	} else {
		bFlipped = true;
	}

	int bytesPerPixel = infoheader.bits/8;
	int paddedWidth = Helpers::DoPadding(infoheader.width*bytesPerPixel, 4);
	// 64 bit: width and height can be up to 65535 each, so the product overflows an int (the buffer would be
	// allocated far too small and the reading below would write beyond it)
	int64_t nImageBytes = (int64_t)infoheader.height * paddedWidth;
	if (nImageBytes > MAX_BMP_FILE_SIZE) {
		fclose(fptr);
		bOutOfMemory = true;
		return NULL; // corrupt or manipulated header
	}
	int fileSizeBytes = (int)nImageBytes; // <= MAX_BMP_FILE_SIZE, fits into an int

	// The file must be able to contain the pixel data: do not allocate memory for it otherwise
	int64_t nDataStart = _ftelli64(fptr);
	if (nDataStart < 0 || _fseeki64(fptr, 0, SEEK_END) != 0) {
		fclose(fptr);
		return NULL;
	}
	int64_t nFileSize = _ftelli64(fptr);
	if (nFileSize < nDataStart || nFileSize - nDataStart < nImageBytes || _fseeki64(fptr, nDataStart, SEEK_SET) != 0) {
		fclose(fptr);
		return NULL;
	}

	uint8* pDest = new(std::nothrow) uint8[fileSizeBytes];
	if (pDest == NULL) {
		fclose(fptr);
		bOutOfMemory = true;
		return NULL;
	}
	uint8* pStart = bFlipped ? pDest + (std::size_t)paddedWidth*(infoheader.height-1) : pDest;
	for (int nLine = 0; nLine < infoheader.height; nLine++) {
		if ((std::size_t)paddedWidth != fread(pStart, 1, paddedWidth, fptr)) {
			delete[] pDest;
			fclose(fptr);
			return NULL;
		}
		pStart = pStart + (bFlipped ? -paddedWidth : paddedWidth);
	}

	// Convert 8 bpp DIBs
	if (infoheader.bits == 8) {
		uint8* pTemp = (uint8*)CBasicProcessing::Convert8bppTo32bppDIB(infoheader.width, infoheader.height, pDest, palette);
		delete[] pDest;
		if (pTemp == NULL) {
			bOutOfMemory = true;
			return NULL;
		}
		pDest = pTemp;
		infoheader.bits = 32;
	}

	// The CJPEGImage object gets ownership of the memory in pDest
	CJPEGImage* pImage = (pDest == NULL) ? NULL : new CJPEGImage(infoheader.width, infoheader.height, pDest, NULL, infoheader.bits/8, 
		0, IF_WindowsBMP, false, 0, 1, 0);

	fclose(fptr);

	bOutOfMemory = pImage == NULL;

	return pImage;
}