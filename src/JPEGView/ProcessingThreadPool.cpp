#include "StdAfx.h"
#include "ProcessingThreadPool.h"
#include "SettingsProvider.h"

CProcessingThreadPool* CProcessingThreadPool::sm_instance;

///////////////////////////////////////////////////////////////////////////////////
// Supporting classes
///////////////////////////////////////////////////////////////////////////////////

// A request wrapping another request and adding information about the image strip to process.
class CWrappedRequest : public CRequestBase {
public:
	// nOffset, nSize defines the strip to process: 'nSize' rows, starting at row 'nOffset'
	CWrappedRequest(CProcessingRequest * pRequest, int nOffset, int nSize, HANDLE hEventFinished) : CRequestBase(hEventFinished) {
		InnerRequest = pRequest;
		Offset = nOffset;
		SizeY = nSize;
	}

	CProcessingRequest* InnerRequest;
	int Offset;
	int SizeY;
};


// Worker thread in thread pool, executing image processing operations on image strips
class CProcessingThread : public CWorkThread {
public:
	CProcessingThread(void) : CWorkThread(false) {}
	~CProcessingThread(void) {}

	// Start a processing request on this thread asynchronously, returns immediately
	void StartProcess(CWrappedRequest* pRequest);

	// Processes a request synchronously on the calling thread
	static void DoProcess(CProcessingRequest* pRequest, int nOffsetY, int nSizeY);
private:

	virtual void ProcessRequest(CRequestBase& request);
};

///////////////////////////////////////////////////////////////////////////////////
// CProcessingThreadPool
///////////////////////////////////////////////////////////////////////////////////

CProcessingThreadPool& CProcessingThreadPool::This() {
	if (sm_instance == NULL) {
		sm_instance = new CProcessingThreadPool();
	}
	return *sm_instance;
}

void CProcessingThreadPool::CreateThreadPoolThreads() {
	m_nNumThreads = CSettingsProvider::This().NumberOfCoresToUse() - 1;
	if (m_nNumThreads > 0) {
		m_threads = new CProcessingThread*[m_nNumThreads];
		for (int i = 0; i < m_nNumThreads; i++) {
			m_threads[i] = new CProcessingThread();
		}
	} else {
		m_threads = NULL;
	}
}

void CProcessingThreadPool::StopAllThreads() {
	for (int i = 0; i < m_nNumThreads; i++) {
		m_threads[i]->Terminate();
		delete m_threads[i];
	}
	delete[] m_threads;
	m_nNumThreads = 0;
	m_threads = NULL;
}

bool CProcessingThreadPool::Process(CProcessingRequest* pRequest) {
	int nTargetCX = pRequest->ClippedTargetSize.cx;
	int nTargetCY = pRequest->ClippedTargetSize.cy;
	if (m_nNumThreads == 0) {
		CProcessingThread::DoProcess(pRequest, 0, nTargetCY);
	} else {
		// Small jobs are processed on the calling thread. The work depends on the number of target pixels and,
		// for downsampling, on the number of source pixels involved (e.g. 24 MP to a 300x200 thumbnail).
		double dSourcePixels = 0;
		if (pRequest->FullTargetSize.cx > 0 && pRequest->FullTargetSize.cy > 0) {
			dSourcePixels = (double)pRequest->SourceSize.cx * nTargetCX / pRequest->FullTargetSize.cx *
							pRequest->SourceSize.cy * nTargetCY / pRequest->FullTargetSize.cy;
		}
		double dWorkPixels = max((double)nTargetCX * nTargetCY, dSourcePixels);
		if (dWorkPixels < 100000 || nTargetCY < 2 * pRequest->StripPadding) { // https://github.com/sylikc/jpegview/pull/290
			CProcessingThread::DoProcess(pRequest, 0, nTargetCY);
		} else {
			// Distribute the lines in blocks of StripPadding lines as evenly as possible: each thread gets
			// nBlocks / nNumThreadsUsed blocks, the first (nBlocks % nNumThreadsUsed) threads one block more.
			// All slices except the last one (processed by the calling thread) are multiples of StripPadding.
			int nBlocks = (nTargetCY + pRequest->StripPadding - 1) / pRequest->StripPadding;
			int nNumThreadsUsed = min(m_nNumThreads + 1, nBlocks); // we also use the calling thread, thus +1
			if (pRequest->MaxThreads > 0) {
				nNumThreadsUsed = min(nNumThreadsUsed, pRequest->MaxThreads);
			}
			if (nNumThreadsUsed <= 1) {
				CProcessingThread::DoProcess(pRequest, 0, nTargetCY);
				return pRequest->Success;
			}
			volatile LONG nRequestThreadCounter = nNumThreadsUsed - 1;
			int nCurrCY = 0;
			HANDLE eventFinished = ::CreateEvent(0, TRUE, FALSE, NULL);
			CWrappedRequest** pAllWrappedRequests = new CWrappedRequest*[nNumThreadsUsed-1];
			for (int i = 0; i < nNumThreadsUsed-1; i++) {
				int nSliceCY = (nBlocks / nNumThreadsUsed + (i < nBlocks % nNumThreadsUsed ? 1 : 0)) * pRequest->StripPadding;
				pAllWrappedRequests[i] = new CWrappedRequest(pRequest, nCurrCY, nSliceCY, eventFinished);
				pAllWrappedRequests[i]->EventFinishedCounter = &nRequestThreadCounter;
				m_threads[i]->StartProcess(pAllWrappedRequests[i]);
				nCurrCY += nSliceCY;
			}
			CProcessingThread::DoProcess(pRequest, nCurrCY, nTargetCY - nCurrCY);
			::WaitForSingleObject(eventFinished, INFINITE);
			::CloseHandle(eventFinished);
			for (int i = 0; i < nNumThreadsUsed-1; i++) {
				pAllWrappedRequests[i]->Deleted = true; // thread pool threads will remove the requests from the queue
			}
			delete [] pAllWrappedRequests;
		}
	}
	return pRequest->Success;
}

CProcessingThreadPool::CProcessingThreadPool(void) {
	m_threads = NULL;
	m_nNumThreads = 0;
}


void CProcessingThread::StartProcess(CWrappedRequest* pRequest) {
	ProcessAsync(pRequest);
}

void CProcessingThread::DoProcess(CProcessingRequest* pRequest, int nOffsetY, int nSizeY) {
	if (!pRequest->ProcessSlice(nOffsetY, nSizeY)) {
		pRequest->Success = false;
	}
}

///////////////////////////////////////////////////////////////////////////////////
// CProcessingRequest
///////////////////////////////////////////////////////////////////////////////////

bool CProcessingRequest::ProcessSlice(int nOffsetY, int nSizeY) {
	// Processing is done in strips to reduce memory consumption and increase cache hit rate.
	// The following constant gives the number of pixels to process per strip.
	const uint32 MAX_SRC_PIXELS_PER_STRIP = 1024 * 100;
	uint32 nNumberOfPixelsInSource = (uint32)((SourceSize.cx * (double)ClippedTargetSize.cx / FullTargetSize.cx) *
		(SourceSize.cy * (double)nSizeY / FullTargetSize.cy));
	uint32 nStrips = 1 + nNumberOfPixelsInSource / MAX_SRC_PIXELS_PER_STRIP;
	uint32 nStripHeight = nSizeY / nStrips;
	uint32 minimalStripHeight = min(16, StripPadding);

	if (nStrips > 1) {
		nStripHeight = nStripHeight & ~(StripPadding - 1); // must be dividable by 'StripPadding', except last strip
		nStripHeight = min(nSizeY, max(nStripHeight, minimalStripHeight));
	}
	int nSizeProcessed = 0;
	int nCurrentSizeY = nStripHeight;
	while (nSizeProcessed < nSizeY) {
		int nCurrentOffsetY = nOffsetY + nSizeProcessed;
		if (!ProcessStrip(nCurrentOffsetY, nCurrentSizeY)) {
			return false;
		}
		nSizeProcessed += nCurrentSizeY;
		nCurrentSizeY = min(nStripHeight, nSizeY - nSizeProcessed);
	}
	return true;
}

void CProcessingThread::ProcessRequest(CRequestBase& request) {
	CWrappedRequest* pWrappedRequest = (CWrappedRequest*)&request;
	DoProcess(pWrappedRequest->InnerRequest, pWrappedRequest->Offset, pWrappedRequest->SizeY);
}

