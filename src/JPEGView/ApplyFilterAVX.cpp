#include "StdAfx.h"
#include "XMMImage.h"
#include "ResizeFilter.h"
#include "ApplyFilterAVX.h"
#include <immintrin.h>

#ifdef _WIN64

CXMMImage* ApplyFilter_AVX(int nSourceHeight, int nTargetHeight, int nWidth,
    int nStartY_FP, int nStartX, int nIncrementY_FP,
    const AVXFilterKernelBlock& filter,
    int nFilterOffset, const CXMMImage* pSourceImg, bool bRoundResult) {

    int nStartXAligned = nStartX & ~7;
    int nEndXAligned = (nStartX + nWidth + 7) & ~7;
    CXMMImage* tempImage = new CXMMImage(nEndXAligned - nStartXAligned, nTargetHeight, 8);
    if (tempImage->AlignedPtr() == NULL) {
        delete tempImage;
        return NULL;
    }

    int nCurY = nStartY_FP;
    int nChannelLenBytes = pSourceImg->GetPaddedWidth() * sizeof(float);
    int nRowLenBytes = nChannelLenBytes * 3;
    int nNumberOfBlocksX = (nEndXAligned - nStartXAligned) >> 3;

    const uint8* pSourceStart = (const uint8*)pSourceImg->AlignedPtr() + nStartXAligned * sizeof(float);
    AVXFilterKernel** pKernelIndexStart = filter.Indices;

    const __m256 ymmZero = _mm256_setzero_ps();
    const __m256 ymmMax = _mm256_set1_ps(4095.0f); // (Wert entsprechend YMM255)

    __m256* pDestination = (__m256*)tempImage->AlignedPtr();

    for (int y = 0; y < nTargetHeight; y++) {
        uint32 nCurYInt = (uint32)nCurY >> 16;
        int filterIndex = y + nFilterOffset;
        AVXFilterKernel* pKernel = pKernelIndexStart[filterIndex];
        int filterLen = pKernel->FilterLen;
        int filterOffset = pKernel->FilterOffset;
        const __m256* pFilterStart = (__m256*) & (pKernel->Kernel);
        const __m256* pSourceRow = (const __m256*)(pSourceStart + ((int)nCurYInt - filterOffset) * nRowLenBytes);

        for (int x = 0; x < nNumberOfBlocksX; x++) {
            const __m256* pFilter = pFilterStart;

            const uint8* pR = (const uint8*)pSourceRow;
            const uint8* pG = pR + nChannelLenBytes;
            const uint8* pB = pG + nChannelLenBytes;

            __m256 ymmR = _mm256_setzero_ps();
            __m256 ymmG = _mm256_setzero_ps();
            __m256 ymmB = _mm256_setzero_ps();

            for (int i = 0; i < filterLen; i++) {
                __m256 ymmKernel = *pFilter++;

                // Fused Multiply-Add: ymmR += (*pR) * ymmKernel
                ymmR = _mm256_fmadd_ps(*((const __m256*)pR), ymmKernel, ymmR);
                ymmG = _mm256_fmadd_ps(*((const __m256*)pG), ymmKernel, ymmG);
                ymmB = _mm256_fmadd_ps(*((const __m256*)pB), ymmKernel, ymmB);

                pR += nRowLenBytes;
                pG += nRowLenBytes;
                pB += nRowLenBytes;
            }

            if (bRoundResult) {
                // Clamping [0, 4095]
                ymmR = _mm256_min_ps(_mm256_max_ps(ymmR, ymmZero), ymmMax);
                ymmG = _mm256_min_ps(_mm256_max_ps(ymmG, ymmZero), ymmMax);
                ymmB = _mm256_min_ps(_mm256_max_ps(ymmB, ymmZero), ymmMax);

                ymmR = _mm256_round_ps(ymmR, _MM_FROUND_TO_NEAREST_INT);
                ymmG = _mm256_round_ps(ymmG, _MM_FROUND_TO_NEAREST_INT);
                ymmB = _mm256_round_ps(ymmB, _MM_FROUND_TO_NEAREST_INT);
            }

            *pDestination++ = ymmR;
            *pDestination++ = ymmG;
            *pDestination++ = ymmB;

            pSourceRow++;
        }

        nCurY += nIncrementY_FP;
    }

    return tempImage;
}

#endif