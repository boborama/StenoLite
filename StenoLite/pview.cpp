#include "stdafx.h"

#include "pview.h"
#include "globals.h"
#include "texthelpers.h"
#include "resource.h"
#include <Richedit.h>
#include <list>
#include <db.h>
#include <Windowsx.h>
#include <Commdlg.h>
#include "fileops.h"
#include <vector>
#include "pstroke.h"
#include <DShow.h>
#include "VorbisDecodeFilter.h"
#include <InitGuid.h>
#include "VorbisTypes.h"
#include "OggTypes.h"
#include <tuple>
#include "SpeexTypes.h"

pdata projectdata;
#define STROKES  "#####STROKES#####"
#define FILES  "#####FILES#####"
HINSTANCE instance;

void saveProject(const tstring &file) {
	static tstring sbuffer;
	HANDLE hfile = CreateFile(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hfile != INVALID_HANDLE_VALUE) {
		writeBOM(hfile);

		DB_TXN* trans;
		projectdata.d->env->txn_begin(projectdata.d->env, NULL, &trans, DB_READ_UNCOMMITTED);

		DBC* startcursor;

		DBT keyin;
		keyin.data = new unsigned __int8[projectdata.d->longest * 3];
		keyin.size = 0;
		keyin.ulen = projectdata.d->longest * 3;
		keyin.dlen = 0;
		keyin.doff = 0;
		keyin.flags = DB_DBT_USERMEM;

		DBT strin;
		strin.data = new unsigned __int8[projectdata.d->lchars + 1];
		strin.size = 0;
		strin.ulen = projectdata.d->lchars + 1;
		strin.dlen = 0;
		strin.doff = 0;
		strin.flags = DB_DBT_USERMEM;

		projectdata.d->contents->cursor(projectdata.d->contents, trans, &startcursor, 0);
		int result = startcursor->get(startcursor, &keyin, &strin, DB_FIRST);

		while (result == 0) {
			tstring acc;
			stroketocsteno((unsigned __int8*)(keyin.data), acc, sharedData.currentd->format);
			for (unsigned int i = 1; i * 3 < keyin.size; i++) {
				acc += TEXT("/");
				tstring tmp;
				stroketocsteno(&(((unsigned __int8*)(keyin.data))[i * 3]), tmp, sharedData.currentd->format);
				acc += tmp;
			}
			writestr(hfile, ttostr(acc));
			writestr(hfile, (char*)(strin.data));
			writestr(hfile, "\r\n");

			result = startcursor->get(startcursor, &keyin, &strin, DB_NEXT);
		}

		writestr(hfile, FILES);
		writestr(hfile, "\r\n");
		for (auto i = projectdata.filetimes.cbegin(); i != projectdata.filetimes.cend(); i++) {
			writestr(hfile, std::to_string(*i));
			writestr(hfile, "\r\n");
		}

		writestr(hfile, STROKES);
		writestr(hfile, "\r\n");

		startcursor->close(startcursor);
		trans->commit(trans, 0);


		for (auto it = projectdata.strokes.cbegin(); it != projectdata.strokes.cend(); it++) {
			if ((*it)->textout->first == (*it)) {
				sbuffer.clear();
				stroketocsteno((*it)->value.ival, sbuffer, sharedData.currentd->format);
				writestr(hfile, ttostr(sbuffer));
				writestr(hfile, " ");
				writestr(hfile, std::to_string((*it)->textout->flags));
				writestr(hfile, " ");
				writestr(hfile, std::to_string((*it)->timestamp));
				writestr(hfile, " ");
				writestr(hfile, ttostr(escapestr((*it)->textout->text)));
				writestr(hfile, "\r\n");
			}
			else {
				sbuffer.clear();
				stroketocsteno((*it)->value.ival, sbuffer, sharedData.currentd->format);
				writestr(hfile, ttostr(sbuffer));
				writestr(hfile, " ");
				writestr(hfile, std::to_string((*it)->timestamp));
				writestr(hfile, "\r\n");
			}
		}

		CloseHandle(hfile);
	}
}


DWORD CALLBACK StreamOutCallback(_In_  DWORD_PTR dwCookie, _In_  LPBYTE pbBuff, _In_  LONG cb, _In_  LONG *pcb)
{
	HANDLE* pFile = (HANDLE*)dwCookie;
	DWORD dwW;
	WriteFile(*pFile, pbBuff, cb, &dwW, NULL);
	*pcb = cb;
	return 0;
}

void SaveText(HWND hWnd)
{

	OPENFILENAME file;
	TCHAR buffer[MAX_PATH] = TEXT("\0");
	memset(&file, 0, sizeof(OPENFILENAME));
	file.lStructSize = sizeof(OPENFILENAME);
	file.hwndOwner = NULL;
	file.lpstrFilter = TEXT("Text Files\0*.txt\0\0");
	file.nFilterIndex = 1;
	file.lpstrFile = buffer;
	file.nMaxFile = MAX_PATH;
	file.Flags = OFN_DONTADDTORECENT | OFN_NOCHANGEDIR;
	if (GetSaveFileName(&file)) {
		tstring filename = buffer;
		if (filename.find(TEXT(".txt")) == std::string::npos) {
			filename += TEXT(".txt");
		}

		HANDLE hfile = CreateFile(filename.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hfile != INVALID_HANDLE_VALUE) {
			EDITSTREAM es;
			es.dwError = 0;
			es.dwCookie = (DWORD)&hfile;
			es.pfnCallback = StreamOutCallback;

			SendMessage(hWnd, EM_STREAMOUT, (WPARAM)SF_TEXT, (LPARAM)&es);

			CloseHandle(hfile);
		}
	}
	

}

template <class T> void SafeRelease(T **ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}

HRESULT IsPinConnected(IPin *pPin, BOOL *pResult)
{
	IPin *pTmp = NULL;
	HRESULT hr = pPin->ConnectedTo(&pTmp);
	if (SUCCEEDED(hr))
	{
		*pResult = TRUE;
	}
	else if (hr == VFW_E_NOT_CONNECTED)
	{
		// The pin is not connected. This is not an error for our purposes.
		*pResult = FALSE;
		hr = S_OK;
	}

	SafeRelease(&pTmp);
	return hr;
}

HRESULT IsPinDirection(IPin *pPin, PIN_DIRECTION dir, BOOL *pResult)
{
	PIN_DIRECTION pinDir;
	HRESULT hr = pPin->QueryDirection(&pinDir);
	if (SUCCEEDED(hr))
	{
		*pResult = (pinDir == dir);
	}
	return hr;
}

// DeleteMediaType calls FreeMediaType, although FreeMediaType can
// be used to only delete the pbFormat structure for an AM_MEDIA_TYPE
void WINAPI FreeMediaType(AM_MEDIA_TYPE& mt)
{
	if (mt.cbFormat != 0)
	{
		CoTaskMemFree((PVOID)mt.pbFormat);

		// Strictly unnecessary but tidier
		mt.cbFormat = 0;
		mt.pbFormat = NULL;
	}

	if (mt.pUnk != NULL)
	{
		mt.pUnk->Release();
		mt.pUnk = NULL;
	}
}


void WINAPI DeleteMediaType(AM_MEDIA_TYPE *pmt)
{
	// allow NULL pointers for coding simplicity

	if (pmt == NULL)
	{
		return;
	}

	FreeMediaType(*pmt);
	CoTaskMemFree((PVOID)pmt);
}


HRESULT suitablePin(IPin *pPin, PIN_DIRECTION PinDir, BOOL *pResult){
	BOOL bresult = FALSE;
	HRESULT hr = IsPinConnected(pPin, &bresult);
	if (SUCCEEDED(hr))
	{
		if (bresult == FALSE)
		{
			hr = IsPinDirection(pPin, PinDir, &bresult);
			if (SUCCEEDED(hr)) {
				if (bresult == TRUE) {
					*pResult = TRUE;
					return hr;
				}
			}
		}
	}
	*pResult = FALSE;
	return hr;
}


HRESULT EnumPins(IBaseFilter *inputfilter)
{
	IEnumPins *pEnum = NULL;
	IPin *pPin = NULL;
	//PINDIR_INPUT

	HRESULT hr = inputfilter->EnumPins(&pEnum);
	if (FAILED(hr))
	{
		MessageBox(NULL, TEXT("Enum Pins failed"), TEXT("FAIL"), MB_OK);
	}
	else {
		while (S_OK == pEnum->Next(1, &pPin, NULL))
		{
			LPTSTR str;
			if (SUCCEEDED(pPin->QueryId(&str))) {
				MessageBox(NULL, str, TEXT("PINID"), MB_OK);
				CoTaskMemFree(str);
			}
			SafeRelease(&pPin);
		}
	}

	SafeRelease(&pEnum);
	SafeRelease(&pPin);

	return hr;
}

HRESULT FindCompatablePinsOut(IBaseFilter *inputfilter, IBaseFilter *outputfilter, IPin **ppPin, IPin **ppPout)
{
	IEnumPins *pEnum = NULL;
	IEnumPins *pEnumIn = NULL;
	IEnumMediaTypes* pEnum2 = NULL;
	IPin *pPin = NULL;
	IPin *pPout = NULL;
	BOOL bFound = FALSE;
	AM_MEDIA_TYPE* media;
	//PINDIR_INPUT

	HRESULT hr = outputfilter->EnumPins(&pEnum);
	if (FAILED(hr))
	{
		MessageBox(NULL, TEXT("Enum Pins failed"), TEXT("FAIL"), MB_OK);
		goto done;
	}

	while (S_OK == pEnum->Next(1, &pPin, NULL))
	{

		HRESULT hr = suitablePin(pPin, PINDIR_INPUT, &bFound);
		if (FAILED(hr))
		{
			MessageBox(NULL, TEXT("Suitable pin failed"), TEXT("FAIL"), MB_OK);
			goto done;
		}
		if (bFound)
		{
			HRESULT hr = inputfilter->EnumPins(&pEnumIn);
			if (FAILED(hr))
			{
				MessageBox(NULL, TEXT("Enum Pins failed (input)"), TEXT("FAIL"), MB_OK);
				goto done;
			}

			while (S_OK == pEnumIn->Next(1, &pPout, NULL))
			{
				bFound = FALSE;
				HRESULT hr = suitablePin(pPout, PINDIR_OUTPUT, &bFound);
				if (FAILED(hr))
				{
					MessageBox(NULL, TEXT("Suitable pin (input) failed"), TEXT("FAIL"), MB_OK);
					goto done;
				}
				if (bFound) {
					//pin and pout are both free pins in the correct direction

					hr = pPin->EnumMediaTypes(&pEnum2);
					if (SUCCEEDED(hr))
					{
						while (S_OK == pEnum2->Next(1, &media, NULL))
						{
							if (pPout->QueryAccept(media) == S_OK) {
								bFound = TRUE;
								hr = S_OK;
								DeleteMediaType(media);

								*ppPin = pPout;
								*ppPout = pPin;
								(*ppPin)->AddRef();
								(*ppPout)->AddRef();
								goto done;
							}
							DeleteMediaType(media);
						}
						SafeRelease(&pEnum2);
					}
					else {
						MessageBox(NULL, TEXT("EnumMediaTypes failed"), TEXT("FAIL"), MB_OK);
					}


					hr = pPout->EnumMediaTypes(&pEnum2);
					if (SUCCEEDED(hr))
					{
						while (S_OK == pEnum2->Next(1, &media, NULL))
						{
							if (pPin->QueryAccept(media) == S_OK) {
								bFound = TRUE;
								hr = S_OK;
								DeleteMediaType(media);

								*ppPin = pPout;
								*ppPout = pPin;
								(*ppPin)->AddRef();
								(*ppPout)->AddRef();
								goto done;
							}
							DeleteMediaType(media);
						}
						SafeRelease(&pEnum2);
					}
					else {
						MessageBox(NULL, TEXT("EnumMediaTypes failed"), TEXT("FAIL"), MB_OK);
					}
				}

				SafeRelease(&pPout);
			}
			SafeRelease(&pEnumIn);

		}
		SafeRelease(&pPin);
	}


	hr = VFW_E_NOT_FOUND;


done:
	SafeRelease(&pPin);
	SafeRelease(&pPout);
	SafeRelease(&pEnum);
	SafeRelease(&pEnumIn);
	SafeRelease(&pEnum2);
	return hr;
}


HRESULT ConnectFilters(
	IGraphBuilder *pGraph, // Filter Graph Manager.
	IBaseFilter *pSource,            // Output pin on the upstream filter.
	IBaseFilter *pDest  // Downstream filter.
	)   
{
	IPin *pIn = NULL;
	IPin *pOut = NULL;

	// Find an input pin on the downstream filter.
	HRESULT hr = FindCompatablePinsOut(pSource, pDest, &pIn, &pOut);
	//HRESULT hr = FindUnconnectedPin(pDest, PINDIR_INPUT, &pIn, type);
	if (SUCCEEDED(hr))
	{
		//HRESULT hr = FindUnconnectedPin(pSource, PINDIR_OUTPUT, &pOut, type);
		//if (SUCCEEDED(hr))
		//{
			// Try to connect them.
			hr = pGraph->ConnectDirect(pIn, pOut, NULL);
			//hr = pGraph->Connect(pIn, pOut);
			if (FAILED(hr)) {
				if (VFW_E_NOT_IN_GRAPH == hr)
					MessageBox(NULL, TEXT("Connect FAILED VFW_E_NOT_IN_GRAPH"), TEXT("FAIL"), MB_OK);
				else if (VFW_E_CIRCULAR_GRAPH == hr)
					MessageBox(NULL, TEXT("Connect FAILED VFW_E_CIRCULAR_GRAPH"), TEXT("FAIL"), MB_OK);
				else if (E_POINTER == hr)
					MessageBox(NULL, TEXT("Connect FAILED E_POINTER"), TEXT("FAIL"), MB_OK);
				else {
					TCHAR buff[100];
					swprintf_s(buff, 100, TEXT("Connect FAILED OTHER : %X"), hr);
					MessageBox(NULL, buff, TEXT("FAIL"), MB_OK);
				}
			}
			
		//}
		//else {
		//	MessageBox(NULL, TEXT("FindUnconnectedPin -- output"), TEXT("FAIL"), MB_OK);
		//}
		SafeRelease(&pOut);
		SafeRelease(&pIn);
	}
	else {
		MessageBox(NULL, TEXT("FindCompatablePinsOut -- no matches"), TEXT("FAIL"), MB_OK);
	}
	return hr;
}



HRESULT AddFilterByCLSID(
	IGraphBuilder *pGraph,      // Pointer to the Filter Graph Manager.
	REFGUID clsid,              // CLSID of the filter to create.
	IBaseFilter **ppF,          // Receives a pointer to the filter.
	LPCWSTR wszName             // A name for the filter (can be NULL).
	)
{
	*ppF = 0;

	IBaseFilter *pFilter = NULL;

	HRESULT hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFilter));

	if (FAILED(hr))
	{
		if (hr == REGDB_E_CLASSNOTREG)
			MessageBox(NULL, TEXT("FAILED CoCreateInstance -- REGDB_E_CLASSNOTREG"), TEXT("FAIL"), MB_OK);
		else
			MessageBox(NULL, TEXT("FAILED CoCreateInstance -- other"), TEXT("FAIL"), MB_OK);
		goto done;
	}

	hr = pGraph->AddFilter(pFilter, wszName);
	if (FAILED(hr))
	{
		MessageBox(NULL, TEXT("FAILED Add FIlter"), TEXT("FAIL"), MB_OK);
		goto done;
	}

	*ppF = pFilter;
	(*ppF)->AddRef();

done:
	SafeRelease(&pFilter);
	return hr;
}


IMediaControl *recControl = NULL;
IMediaSeeking *recSeeking = NULL;
IMediaEvent   *recEvent = NULL;
IGraphBuilder *recgraph = NULL;

void InitPlayback()
{

	OPENFILENAME file;
	TCHAR buffer[MAX_PATH] = TEXT("\0");
	memset(&file, 0, sizeof(OPENFILENAME));
	file.lStructSize = sizeof(OPENFILENAME);
	file.hwndOwner = NULL;
	file.lpstrFilter = TEXT("OGG files\0*.ogg\0\0");
	file.nFilterIndex = 1;
	file.lpstrFile = buffer;
	file.nMaxFile = MAX_PATH;
	file.Flags = OFN_DONTADDTORECENT | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	if (GetOpenFileName(&file)) {
		tstring filename = buffer;
		if (filename.find(TEXT(".ogg")) == std::string::npos) {
			filename += TEXT(".ogg");
		}


		IBaseFilter *file = NULL, *demux = NULL, *decoder = NULL, *dsound = NULL;
			IFileSourceFilter *filesource = NULL;
			IGraphBuilder *pGraph;
			HRESULT hr;
			// Create the Filter Graph Manager.
			hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
				IID_IGraphBuilder, (void**)&pGraph);
			if (FAILED(hr)) {
				MessageBox(NULL, TEXT("FAILED creating filer graph"), TEXT("FAIL"), MB_OK); return;
			}


			hr = AddFilterByCLSID(pGraph, CLSID_AsyncReader, &file, L"FileIn");
			if (FAILED(hr))
				MessageBox(NULL, TEXT("FAILED adding FileIn"), TEXT("FAIL"), MB_OK);

			hr = AddFilterByCLSID(pGraph, CLSID_OggDemuxFilter, &demux, L"DecodeOGG");
			if (FAILED(hr)) {
				MessageBox(NULL, TEXT("FAILED adding OggDemux"), TEXT("FAIL"), MB_OK); return;
			}

			hr = AddFilterByCLSID(pGraph, CLSID_VorbisDecodeFilter, &decoder, L"DecodeVORB");
			if (FAILED(hr)) {
				MessageBox(NULL, TEXT("FAILED adding DecodeVorbis"), TEXT("FAIL"), MB_OK); return;
			}
			
			hr = AddFilterByCLSID(pGraph, CLSID_DSoundRender, &dsound, L"DSound");
			if (FAILED(hr))
				MessageBox(NULL, TEXT("FAILED adding DSound"), TEXT("FAIL"), MB_OK);

			// Set the file name.
			hr = file->QueryInterface(IID_IFileSourceFilter, (void**)&filesource);
			if (FAILED(hr))
				MessageBox(NULL, TEXT("FAILED IID_IFileSourceFilter"), TEXT("FAIL"), MB_OK);
			hr = filesource->Load(filename.c_str(), NULL);
			if (FAILED(hr))
				MessageBox(NULL, TEXT("FAILED loading file"), filename.c_str(), MB_OK);
			

			hr = ConnectFilters(pGraph, file, demux);
			if (FAILED(hr))
				MessageBox(NULL, TEXT("FAILED Connect filters1"), TEXT("FAIL"), MB_OK);


			hr = ConnectFilters(pGraph, demux, decoder);
			if (FAILED(hr))
				MessageBox(NULL, TEXT("FAILED Connect filters2"), TEXT("FAIL"), MB_OK);

			hr = ConnectFilters(pGraph, decoder, dsound);
			//hr = DumbConnect(pGraph, decoder, dsound);
			if (FAILED(hr))
				MessageBox(NULL, TEXT("FAILED Connect filters3"), TEXT("FAIL"), MB_OK);
			

			

			IMediaControl *pControl = NULL;
			IMediaEvent   *pEvent = NULL;
			hr = pGraph->QueryInterface(IID_IMediaControl, (void **)&pControl);
			if (FAILED(hr))
				MessageBox(NULL, TEXT("FAILED IID_IMediaControl"), TEXT("FAIL"), MB_OK);
			hr = pGraph->QueryInterface(IID_IMediaEvent, (void **)&pEvent);
			if (FAILED(hr))
				MessageBox(NULL, TEXT("FAILED IID_IMediaEvent"), TEXT("FAIL"), MB_OK);
			

			//hr = pGraph->RenderFile(filename.c_str(), NULL);
			//if (FAILED(hr))
			//	MessageBox(NULL, TEXT("FAILED RenderFile"), TEXT("FAIL"), MB_OK);

			hr = pControl->Run();
			if (SUCCEEDED(hr))
			{
				// Wait for completion.
				long evCode;
				pEvent->WaitForCompletion(INFINITE, &evCode);

				// Note: Do not use INFINITE in a real application, because it
				// can block indefinitely.
			}
			//MessageBox(NULL, TEXT("Audio complete"), TEXT("Done"), MB_OK);

			//delete vb;

			SafeRelease(&filesource);

			SafeRelease(&pControl);
			SafeRelease(&pEvent);

			SafeRelease(&file);
			SafeRelease(&demux);
			SafeRelease(&decoder);
			SafeRelease(&dsound);

			SafeRelease(&pGraph);
		
	}


}

#pragma comment(lib, "strmiids")

HRESULT EnumerateDevices(REFGUID category, IEnumMoniker **ppEnum)
{
	// Create the System Device Enumerator.
	ICreateDevEnum *pDevEnum;
	HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL,
		CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDevEnum));

	if (SUCCEEDED(hr))
	{
		// Create an enumerator for the category.
		hr = pDevEnum->CreateClassEnumerator(category, ppEnum, 0);
		if (hr == S_FALSE)
		{
			hr = VFW_E_NOT_FOUND;  // The category is empty. Treat as an error.
		}
		pDevEnum->Release();
	}
	return hr;
}

std::vector<std::pair<tstring, LONG>> devices;



HRESULT BindDevice(IEnumMoniker *pEnum, IBaseFilter* &pCap, ULONG index)
{
	pCap = NULL;
	IMoniker *pMoniker = NULL;

	if (devices.size() < index + 1)
		return S_OK;

	while (pEnum->Next(1, &pMoniker, NULL) == S_OK)
	{
		IPropertyBag *pPropBag;
		HRESULT hr = pMoniker->BindToStorage(0, 0, IID_PPV_ARGS(&pPropBag));
		if (FAILED(hr))
		{
			pMoniker->Release();
			continue;
		}

		VARIANT var;
		VariantInit(&var);

		hr = pPropBag->Read(L"WaveInID", &var, 0);
		if (SUCCEEDED(hr))
		{
			if (devices[index].second == var.lVal) {
				//bind
				return pMoniker->BindToObject(0, 0, IID_IBaseFilter, (void**)&pCap);
			}
			VariantClear(&var);
		}

		pPropBag->Release();
		pMoniker->Release();
	}

	return E_FAIL;
}

void BuildDeviceList(IEnumMoniker *pEnum)
{
	devices.clear();
	IMoniker *pMoniker = NULL;

	while (pEnum->Next(1, &pMoniker, NULL) == S_OK)
	{
		IPropertyBag *pPropBag;
		HRESULT hr = pMoniker->BindToStorage(0, 0, IID_PPV_ARGS(&pPropBag));
		if (FAILED(hr))
		{
			pMoniker->Release();
			continue;
		}

		VARIANT var;
		VariantInit(&var);

		tstring str;
		LONG id;

		// Get description or friendly name.
		hr = pPropBag->Read(L"Description", &var, 0);
		if (FAILED(hr))
		{
			hr = pPropBag->Read(L"FriendlyName", &var, 0);
		}
		if (SUCCEEDED(hr))
		{
			
			str = var.bstrVal;
			VariantClear(&var);
		}


		// WaveInID applies only to audio capture devices.
		hr = pPropBag->Read(L"WaveInID", &var, 0);
		if (SUCCEEDED(hr))
		{
			id = var.lVal;
			VariantClear(&var);
		}

		devices.push_back(make_pair(str, id));
		pPropBag->Release();
		pMoniker->Release();
	}
}

LONG chosenindex;

INT_PTR CALLBACK ChooseAudio(_In_  HWND hwndDlg, _In_  UINT uMsg, _In_  WPARAM wParam, _In_  LPARAM lParam) {
	switch (uMsg)
	{
	case WM_INITDIALOG:
	{
		for (auto i = devices.cbegin(); i != devices.cend(); i++) {
			SendMessage(GetDlgItem(hwndDlg, IDC_AUDIOCOMBO), (UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)((*i).first.c_str()));
		}

		ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_AUDIOCOMBO), 0);
		
		return TRUE;
	}
	case WM_CLOSE:
		EndDialog(hwndDlg, IDCANCEL);

		return TRUE;
	case WM_COMMAND:
		if (HIWORD(wParam) == CBN_SELCHANGE) {
			chosenindex = SendMessage(GetDlgItem(hwndDlg, IDC_AUDIOCOMBO), (UINT)CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
			break;
		}
		else if (HIWORD(wParam) == BN_CLICKED) {
			switch (LOWORD(wParam))
			{
			case IDOK:
			{
				
				EndDialog(hwndDlg, IDOK);
				return TRUE;
			}
			case IDCANCEL:


				EndDialog(hwndDlg, IDCANCEL);
				return TRUE;
			}
		}
	}
	return FALSE;
}

void InitRecording(int filenumber)
{
	if (filenumber < 1)
		return;

	IEnumMoniker *pEnum;
	HRESULT hr = EnumerateDevices(CLSID_AudioInputDeviceCategory, &pEnum);
	if (SUCCEEDED(hr))
	{
		BuildDeviceList(pEnum);
		if (SUCCEEDED(pEnum->Reset())) {
			//pop up choice
			if (DialogBox(instance, MAKEINTRESOURCE(IDD_CHOOSEAUDIO), projectdata.dlg, ChooseAudio) == IDOK) {

				IBaseFilter* pCap = NULL;
				if (SUCCEEDED(BindDevice(pEnum, pCap, chosenindex))) {
						
					IBaseFilter *mux = NULL, *encode = NULL;
					//IFileSourceFilter *filesource = NULL;

					bool success = true;

					// Create the Filter Graph Manager.
					if (FAILED(CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void**)&recgraph))) {
						MessageBox(NULL, TEXT("Unexpected failure -- Direct Show may not be installed or configured correctly"), TEXT("FAIL"), MB_OK);
						success = false;
					}

					if (success) {
						if (FAILED(recgraph->AddFilter(pCap, TEXT("Audio Capture")))) {
							MessageBox(NULL, TEXT("FAILED adding capture device"), TEXT("FAIL"), MB_OK);
							success = false;
						}
					}

					if (success) {
						if (FAILED(AddFilterByCLSID(recgraph, CLSID_SpeexEncodeFilter, &encode, L"Vorbis Encoder"))) {
							MessageBox(NULL, TEXT("FAILED adding vorbis encoder"), TEXT("FAIL"), MB_OK);
							success = false;
						}
					}

					if (success) {
						if (FAILED(AddFilterByCLSID(recgraph, CLSID_OggMuxFilter, &mux, L"Ogg Mux"))) {
							MessageBox(NULL, TEXT("FAILED adding ogg file mux"), TEXT("FAIL"), MB_OK);
							success = false;
						}
						else {
							IFileSinkFilter* sink = NULL;
							if (FAILED(mux->QueryInterface(IID_IFileSinkFilter, (void **)&sink))) {
								MessageBox(NULL, TEXT("FAILED setting output file"), TEXT("FAIL"), MB_OK);
								success = false;
							}
							else {
								tstring ogg = projectdata.file;
								ogg.pop_back();
								ogg.pop_back();
								ogg.pop_back();
								ogg.pop_back();
								ogg += TEXT("_pt");
								ogg += std::to_wstring(filenumber);
								ogg += TEXT(".ogg");
								sink->SetFileName(ogg.c_str(), NULL);
							}
							SafeRelease(&sink);
						}
					}

					if (success) {
						if (FAILED(ConnectFilters(recgraph, pCap, encode))) {
							MessageBox(NULL, TEXT("FAILED to connect to encoder"), TEXT("FAIL"), MB_OK);
							success = false;
						}
					}

					if (success) {
						if (FAILED(ConnectFilters(recgraph, encode, mux))) {
							MessageBox(NULL, TEXT("FAILED to connect to encoder"), TEXT("FAIL"), MB_OK);
							success = false;
						}
					}

					if (success) {
						recgraph->QueryInterface(IID_IMediaControl, (void **)&recControl);
						recgraph->QueryInterface(IID_IMediaEvent, (void **)&recEvent);
						recgraph->QueryInterface(IID_IMediaSeeking, (void **)&recSeeking);
					}


					SafeRelease(&pCap);
					SafeRelease(&encode);
					SafeRelease(&mux);

					if (!success) {
						SafeRelease(&recControl);
						SafeRelease(&recEvent);
						SafeRelease(&recSeeking);
						SafeRelease(&recgraph);
					}
					else {
						if (FAILED(recControl->Run())) {
							MessageBox(NULL, TEXT("Error starting audio capture"), TEXT("FAIL"), MB_OK);
						}
						else {
							//projectdata.starttick = GetTickCount64() - projectdata.filetimes[filenumber - 1];
							projectdata.paused = false;

							//projectdata.filetimes.push_back(projectdata.starttick);

							HANDLE ico = LoadImage(instance, MAKEINTRESOURCE(IDI_RUN), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
							SendMessage(projectdata.dlg, WM_SETICON, FALSE, (LPARAM)ico);
							SendMessage(projectdata.dlg, WM_SETICON, TRUE, (LPARAM)ico);
						}
					}
				}
				else {
					MessageBox(NULL, TEXT("Could not bind audio recording device"), TEXT("Error"), MB_OK);
				}
			}


			
			return;
		}
		pEnum->Release();
	}

	MessageBox(NULL, TEXT("Could not find audio recording devices"), TEXT("Error"), MB_OK);
}


void PauseRecording() {
	if (!projectdata.paused) {
		if (recControl != NULL) {
			if (recControl != NULL) {
				if (FAILED(recControl->Pause())) {
					MessageBox(NULL, TEXT("Error pausing audio capture"), TEXT("FAIL"), MB_OK);
				}
				else {
					//projectdata.pausetick = GetTickCount64();
					projectdata.paused = true;

					MENUITEMINFO menuinfo;
					memset(&menuinfo, 0, sizeof(MENUITEMINFO));
					menuinfo.cbSize = sizeof(MENUITEMINFO);
					menuinfo.fMask = MIIM_STRING;
					menuinfo.dwTypeData = TEXT("Resume Audio Recording");
					SetMenuItemInfo(GetMenu(projectdata.dlg), IDM_PREC, FALSE, &menuinfo);

					HANDLE ico = LoadImage(instance, MAKEINTRESOURCE(IDI_PAUSE), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
					SendMessage(projectdata.dlg, WM_SETICON, FALSE, (LPARAM)ico);
					SendMessage(projectdata.dlg, WM_SETICON, TRUE, (LPARAM)ico);
				}
			}
		}
	}
	else {
		if (recControl != NULL) {
			if (FAILED(recControl->Run())) {
				MessageBox(NULL, TEXT("Error starting audio capture"), TEXT("FAIL"), MB_OK);
			}
			else {
				//projectdata.starttick += GetTickCount64() - projectdata.pausetick;
				projectdata.paused = false;

				MENUITEMINFO menuinfo;
				memset(&menuinfo, 0, sizeof(MENUITEMINFO));
				menuinfo.cbSize = sizeof(MENUITEMINFO);
				menuinfo.fMask = MIIM_STRING;
				menuinfo.dwTypeData = TEXT("Pause Audio Recording");
				SetMenuItemInfo(GetMenu(projectdata.dlg), IDM_PREC, FALSE, &menuinfo);

				HANDLE ico = LoadImage(instance, MAKEINTRESOURCE(IDI_RUN), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
				SendMessage(projectdata.dlg, WM_SETICON, FALSE, (LPARAM)ico);
				SendMessage(projectdata.dlg, WM_SETICON, TRUE, (LPARAM)ico);
			}
		}
	}
}

IMediaControl *playControl = NULL;
IMediaEvent   *playEvent = NULL;
IMediaSeeking *playSeeking = NULL;
IGraphBuilder *playgraph = NULL;

unsigned int getfilenumber(const ULONGLONG &time) {
	for (unsigned int i = 1; i < projectdata.filetimes.size(); i++) {
		if (time < projectdata.filetimes[i]) {
			return i;
		}
	}
 
	return 0;
}

void LoadPlayback(const tstring& prjfile, int filenumber) {
	
	if (filenumber < 1)
		return;

	tstring ogg = prjfile;
	ogg.pop_back();
	ogg.pop_back();
	ogg.pop_back();
	ogg.pop_back();
	ogg += TEXT("_pt");
	ogg += std::to_wstring(filenumber);
	ogg += TEXT(".ogg");

	if (GetFileAttributes(ogg.c_str()) != INVALID_FILE_ATTRIBUTES) {
		IBaseFilter *file = NULL, *demux = NULL, *decoder = NULL, *dsound = NULL;
		IFileSourceFilter *filesource = NULL;

		
		HRESULT hr;

		bool success = true;

		hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void**)&playgraph);
		if (FAILED(hr)) {
			MessageBox(NULL, TEXT("Unexpected failure -- Direct Show may not be installed or configured correctly"), TEXT("FAIL"), MB_OK);
			success = false;
		}

		if (success) {
			hr = AddFilterByCLSID(playgraph, CLSID_AsyncReader, &file, L"FileIn");
			if (FAILED(hr)) {
				MessageBox(NULL, TEXT("FAILED adding FileIn"), TEXT("FAIL"), MB_OK);
				success = false;
			}
		}

		if (success) {
			hr = AddFilterByCLSID(playgraph, CLSID_OggDemuxFilter, &demux, L"DecodeOGG");
			if (FAILED(hr)) {
				MessageBox(NULL, TEXT("FAILED adding OggDemux"), TEXT("FAIL"), MB_OK);
				success = false;
			}
		}

		if (success) {
			hr = AddFilterByCLSID(playgraph, CLSID_SpeexDecodeFilter, &decoder, L"DecodeVORB");
			if (FAILED(hr)) {
				MessageBox(NULL, TEXT("FAILED adding DecodeVorbis"), TEXT("FAIL"), MB_OK);
				success = false;
			}
		}

		if (success) {
			hr = AddFilterByCLSID(playgraph, CLSID_DSoundRender, &dsound, L"DSound");
			if (FAILED(hr)) {
				MessageBox(NULL, TEXT("FAILED adding DSound"), TEXT("FAIL"), MB_OK);
				success = false;
			}
		}

		if (success) {
			hr = file->QueryInterface(IID_IFileSourceFilter, (void**)&filesource);
			if (FAILED(hr)) {
				MessageBox(NULL, TEXT("FAILED IID_IFileSourceFilter"), TEXT("FAIL"), MB_OK);
				success = false;
			}
		}

		if (success) {
			hr = filesource->Load(ogg.c_str(), NULL);
			if (FAILED(hr)) {
				MessageBox(NULL, TEXT("FAILED loading file"), ogg.c_str(), MB_OK);
				success = false;
			}
		}

		if (success) {
			hr = ConnectFilters(playgraph, file, demux);
			if (FAILED(hr)) {
				MessageBox(NULL, TEXT("FAILED Connect filters1"), TEXT("FAIL"), MB_OK);
				success = false;
			}
		}

		if (success) {
			hr = ConnectFilters(playgraph, demux, decoder);
			if (FAILED(hr)) {
				MessageBox(NULL, TEXT("FAILED Connect filters2"), TEXT("FAIL"), MB_OK);
				success = false;
			}
		}

		if (success) {
			hr = ConnectFilters(playgraph, decoder, dsound);
			if (FAILED(hr)) {
				MessageBox(NULL, TEXT("FAILED Connect filters3"), TEXT("FAIL"), MB_OK);
				success = false;
			}
		}

		if (success) {
			playgraph->QueryInterface(IID_IMediaControl, (void **)&playControl);
			playgraph->QueryInterface(IID_IMediaEvent, (void **)&playEvent);
			playgraph->QueryInterface(IID_IMediaSeeking, (void **)&playSeeking);
		}

		SafeRelease(&file);
		SafeRelease(&demux);
		SafeRelease(&decoder);
		SafeRelease(&dsound);
		SafeRelease(&filesource);

		if (!success) {
			SafeRelease(&playgraph);
			SafeRelease(&playControl);
			SafeRelease(&playEvent);
			SafeRelease(&playSeeking);
		}
		else {
			//anything to do on loading?
		}
	}
}

void RegisterDelete(int n, const ULONGLONG &thetime) {
	if (n < 0)
		return;

	if (projectdata.realtime != NULL) {
		writestr(projectdata.realtime, "\r\nD ");
		writestr(projectdata.realtime, std::to_string(thetime));
		writestr(projectdata.realtime, " ");
		writestr(projectdata.realtime, std::to_string(n));
	}
}

void RegisterDef(tstring stroke, tstring val) {
	if (projectdata.realtime != NULL) {
		writestr(projectdata.realtime, "\r\nX ");
		writestr(projectdata.realtime, ttostr(stroke));
		writestr(projectdata.realtime, " ");
		writestr(projectdata.realtime, ttostr(val));
	}
}

void RegisterFile(const ULONGLONG &time) {
	if (projectdata.realtime != NULL) {
		writestr(projectdata.realtime, "\r\nF ");
		writestr(projectdata.realtime, std::to_string(time));
		writestr(projectdata.realtime, " F");
	}
}

void RegisterStroke(unsigned _int8* stroke, int n, const ULONGLONG &thetime) {
	if (n < 0)
		return;

	if (projectdata.realtime != NULL) {
		tstring buffer;
		stroketocsteno(stroke, buffer, sharedData.currentd->format);
		writestr(projectdata.realtime, "\r\n");
		writestr(projectdata.realtime, std::to_string(n));
		writestr(projectdata.realtime, " ");
		writestr(projectdata.realtime, std::to_string(thetime));
		writestr(projectdata.realtime, " ");
		writestr(projectdata.realtime, ttostr(buffer));
	}
}

struct stroke {
	unsigned __int8 sval[4];
	ULONGLONG timestamp;
};

void delat(std::vector<stroke> &slist, int n) {
	if (n < 0 || n > slist.size() - 1)
		return;

	auto it = slist.begin();
	it += n;
	slist.erase(it);
}

void addat(std::vector<stroke> &slist, int n, stroke in) {
	if (n < 0 || n > slist.size())
		return;

	auto it = slist.begin();
	it += n;
	slist.insert(it, in);
}

void reRealtime() {
	WaitForSingleObject(sharedData.lockprocessing, INFINITE);

	CloseHandle(projectdata.realtime);

	tstring realtime = projectdata.file;
	realtime = realtime.replace(realtime.find(TEXT(".prj")), 4, TEXT(".srf"));

	projectdata.realtime = CreateFile(realtime.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	for (auto i = projectdata.filetimes.cbegin(); i != projectdata.filetimes.cend(); i++) {
		RegisterFile(*i);
	}

	if (projectdata.realtime != INVALID_HANDLE_VALUE) {

		DB_TXN* trans;
		projectdata.d->env->txn_begin(projectdata.d->env, NULL, &trans, DB_READ_UNCOMMITTED);

		DBC* startcursor;

		DBT keyin;
		keyin.data = new unsigned __int8[projectdata.d->longest * 3];
		keyin.size = 0;
		keyin.ulen = projectdata.d->longest * 3;
		keyin.dlen = 0;
		keyin.doff = 0;
		keyin.flags = DB_DBT_USERMEM;

		DBT strin;
		strin.data = new unsigned __int8[projectdata.d->lchars + 1];
		strin.size = 0;
		strin.ulen = projectdata.d->lchars + 1;
		strin.dlen = 0;
		strin.doff = 0;
		strin.flags = DB_DBT_USERMEM;

		projectdata.d->contents->cursor(projectdata.d->contents, trans, &startcursor, 0);
		int result = startcursor->get(startcursor, &keyin, &strin, DB_FIRST);

		while (result == 0) {
			tstring acc;
			stroketocsteno((unsigned __int8*)(keyin.data), acc, sharedData.currentd->format);
			for (unsigned int i = 1; i * 3 < keyin.size; i++) {
				acc += TEXT("/");
				tstring tmp;
				stroketocsteno(&(((unsigned __int8*)(keyin.data))[i * 3]), tmp, sharedData.currentd->format);
				acc += tmp;
			}
			writestr(projectdata.realtime, "\r\nX ");
			writestr(projectdata.realtime, ttostr(acc));
			writestr(projectdata.realtime, " ");
			writestr(projectdata.realtime, (char*)(strin.data));

			result = startcursor->get(startcursor, &keyin, &strin, DB_NEXT);
		}

		startcursor->close(startcursor);
		trans->commit(trans, 0);

		for (auto i = projectdata.strokes.cbegin(); i != projectdata.strokes.cend(); i++) {
			RegisterStroke((*i)->value.ival, 0, (*i)->timestamp);
		}
	}

	ReleaseMutex(sharedData.lockprocessing);
}

void LoadRealtime() {
	const static std::regex parsefile("^(\\S+?)\\s(\\S+?)\\s(.*)$");
	std::cmatch m;
	
	WaitForSingleObject(sharedData.lockprocessing, INFINITE);

	std::vector<stroke> slist;

	char c;
	DWORD bytes;
	std::string cline("");

	ULONGLONG latest = 0;
	projectdata.filetimes.push_back(0);

	int totalb = 0;
	ReadFile(projectdata.realtime, &c, 1, &bytes, NULL);
	while (bytes > 0) {
		totalb++;
		if (c != '\r')
			cline += c;
		std::string::size_type r = cline.find("\n");
		if (r != std::string::npos) {
			cline.erase(r, 1);
			if (std::regex_match(cline.c_str(), m, parsefile)) {
				if (m[1].str().compare("X") == 0) {
					int numstrokes = 0;
					unsigned __int8* sdata = texttomultistroke(strtotstr(m[2].str()), numstrokes, sharedData.currentd->format);

					DB_TXN* trans;
					projectdata.d->env->txn_begin(projectdata.d->env, NULL, &trans, 0);
					projectdata.d->addDItem(sdata, numstrokes * 3, m[3].str(), trans);
					trans->commit(trans, 0);


					delete sdata;
				}
				else if (m[1].str().compare("D") == 0) {
					delat(slist, atoi(m[3].str().c_str()));
				}
				else if (m[1].str().compare("F") == 0) {
					projectdata.filetimes.push_back(_atoi64(m[2].str().c_str()));
				}
				else {
					stroke ns;
					textToStroke(strtotstr(m[3].str()), ns.sval, sharedData.currentd->format);
					ns.timestamp = _atoi64(m[2].str().c_str());
					if (ns.timestamp > latest) {
						latest = ns.timestamp;
					}
					addat(slist, atoi(m[1].str().c_str()), ns);

					//MessageBox(NULL, TEXT("ADD"), TEXT("ADD"), MB_OK);
				}
			}
			
			cline.clear();

		}
		ReadFile(projectdata.realtime, &c, 1, &bytes, NULL);
	}

	if (std::regex_match(cline.c_str(), m, parsefile)) {
		if (m[1].str().compare("X") == 0) {
			int numstrokes = 0;
			unsigned __int8* sdata = texttomultistroke(strtotstr(m[2].str()), numstrokes, sharedData.currentd->format);

			DB_TXN* trans;
			projectdata.d->env->txn_begin(projectdata.d->env, NULL, &trans, 0);
			projectdata.d->addDItem(sdata, numstrokes * 3, m[3].str(), trans);
			trans->commit(trans, 0);


			delete sdata;
		}
		else if (m[1].str().compare("D") == 0) {
			delat(slist, atoi(m[3].str().c_str()));
		}
		else if (m[1].str().compare("F") == 0) {
			projectdata.filetimes.push_back(_atoi64(m[2].str().c_str()));
		}
		else {
			stroke ns;
			textToStroke(strtotstr(m[3].str()), ns.sval, sharedData.currentd->format);
			ns.timestamp = _atoi64(m[2].str().c_str());
			if (ns.timestamp > latest) {
				latest = ns.timestamp;
			}
			addat(slist, atoi(m[1].str().c_str()), ns);
		}
	}

	
	if (latest > projectdata.filetimes[projectdata.filetimes.size() - 1]) {
		projectdata.filetimes.push_back(latest);
	}
	
	projectdata.reloading = true;

	HANDLE rtback = projectdata.realtime;
	projectdata.realtime = NULL;
	for (auto i = slist.cbegin(); i != slist.cend(); i++) {
		unsigned __int8 temp[3];
		temp[0] = (*i).sval[0];
		temp[1] = (*i).sval[1];
		temp[2] = (*i).sval[2];
		ULONGLONG time = (*i).timestamp;
		processSingleStroke(&(temp[0]), time);
	}
	projectdata.realtime = rtback;

	projectdata.reloading = false;

	ReleaseMutex(sharedData.lockprocessing);
}

void ProcessItem(std::string &cline, textoutput* &last, bool &matchdict, bool &matchfile,  DB_TXN* trans) {
	const static std::regex parsedict("^(\\S+?)\\s(.*)$");
	const static std::regex parsefull("^(\\S+?)\\s(\\S+?)\\s(\\S+?)\\s(.*)$");
	const static std::regex parsestroke("^(\\S+?)\\s(\\S+?)$");
	std::cmatch m;

	if (cline.compare(STROKES) == 0) {
		matchdict = false;
		matchfile = false;
	}
	else if (cline.compare(FILES) == 0) {
		matchdict = false;
		matchfile = true;
	}
	else if (matchfile) {
		projectdata.filetimes.push_back(_atoi64(cline.c_str()));
	}
	else if (matchdict) {
		if (std::regex_match(cline.c_str(), m, parsedict)) {
			int len = 0;
			unsigned __int8* strokes = texttomultistroke(strtotstr(m[1].str()), len, sharedData.currentd->format);
			projectdata.d->addDItem(strokes, len * 3, m[2].str(), trans);
			delete strokes;
		}
	}
	else {
		if (std::regex_match(cline.c_str(), m, parsefull)) {
			unsigned __int8 stroke[4];
			textToStroke(strtotstr(m[1].str()), stroke, sharedData.currentd->format);
			singlestroke* s = new singlestroke(stroke, _atoi64(m[3].str().c_str()));
			s->textout = new textoutput();
			s->textout->flags = atoi(m[2].str().c_str());
			s->textout->text = unescapestr(strtotstr(m[4].str()));
			s->textout->first = s;
			projectdata.strokes.push_back(s);
			last = s->textout;

			TCHAR buffer[32] = TEXT("\r\n");
			stroketosteno(s->value.ival, &buffer[2], sharedData.currentd->format);

			CHARRANGE crngb;
			crngb.cpMin = crngb.cpMax = 23;
			SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_EXSETSEL, NULL, (LPARAM)&crngb);
			SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_REPLACESEL, FALSE, (LPARAM)buffer);


			crngb.cpMin = crngb.cpMax = 0;
			SendMessage(GetDlgItem(projectdata.dlg, IDC_MAINTEXT), EM_EXSETSEL, NULL, (LPARAM)&crngb);
			SendMessage(GetDlgItem(projectdata.dlg, IDC_MAINTEXT), EM_REPLACESEL, FALSE, (LPARAM)(s->textout->text.c_str()));
		}
		else if (std::regex_match(cline.c_str(), m, parsestroke)) {
			unsigned __int8 stroke[4];
			textToStroke(strtotstr(m[1].str()), stroke, sharedData.currentd->format);
			singlestroke* s = new singlestroke(stroke, _atoi64(m[2].str().c_str()));
			s->textout = last;
			projectdata.strokes.push_back(s);

			TCHAR buffer[32] = TEXT("\r\n");
			stroketosteno(s->value.ival, &buffer[2], sharedData.currentd->format);

			CHARRANGE crngb;
			crngb.cpMin = crngb.cpMax = 23;
			SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_EXSETSEL, NULL, (LPARAM)&crngb);
			SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_REPLACESEL, FALSE, (LPARAM)buffer);
		}
	}

}

void exDict()
{
	OPENFILENAME file;
	TCHAR buffer[MAX_PATH] = TEXT("\0");
	memset(&file, 0, sizeof(OPENFILENAME));
	file.lStructSize = sizeof(OPENFILENAME);
	file.hwndOwner = projectdata.dlg;
	file.lpstrFilter = TEXT("Json Files\0*.json\0\0");
	file.nFilterIndex = 1;
	file.lpstrFile = buffer;
	file.nMaxFile = MAX_PATH;
	file.Flags = OFN_DONTADDTORECENT | OFN_NOCHANGEDIR;
	if (GetSaveFileName(&file)) {
		tstring filename = buffer;
		if (filename.find(TEXT(".json")) == std::string::npos) {
			filename += TEXT(".json");
		}
		SaveJson(projectdata.d, filename, NULL);
	}
}

bool loadProject(const tstring &file) {
	WaitForSingleObject(sharedData.lockprocessing, INFINITE);
	HANDLE hfile = CreateFile(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
	
	bool matchdict = true;
	bool files = false;
	projectdata.filetimes.clear();

	if (hfile != INVALID_HANDLE_VALUE) {

		char c;
		DWORD bytes;
		std::string cline("");

		//detect and erase BOM
		unsigned __int8 bom[3] = "\0\0";
		ReadFile(hfile, bom, 3, &bytes, NULL);
		if (bom[0] != 239 || bom[1] != 187 || bom[2] != 191) {
			SetFilePointer(hfile, 0, NULL, FILE_BEGIN);
		}
		DB_TXN* trans = NULL;
		projectdata.d->env->txn_begin(projectdata.d->env, NULL, &trans, DB_TXN_BULK);
		textoutput* last = NULL;

		int totalb = 0;
		ReadFile(hfile, &c, 1, &bytes, NULL);
		while (bytes > 0) {
			totalb++;
			if (c != '\r')
				cline += c;
			std::string::size_type r = cline.find("\n");
			if (r != std::string::npos) {
				cline.erase(r, 1);
				ProcessItem(cline, last, matchdict, files, trans);
				cline.clear();

			}
			ReadFile(hfile, &c, 1, &bytes, NULL);
		}

		ProcessItem(cline, last, matchdict, files, trans);

		trans->commit(trans, 0);
		CloseHandle(hfile);

		CHARRANGE crngb;
		crngb.cpMin = crngb.cpMax = GetWindowTextLength(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST));
		SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_EXSETSEL, NULL, (LPARAM)&crngb);
		crngb.cpMin = crngb.cpMax = GetWindowTextLength(GetDlgItem(projectdata.dlg, IDC_MAINTEXT));
		SendMessage(GetDlgItem(projectdata.dlg, IDC_MAINTEXT), EM_EXSETSEL, NULL, (LPARAM)&crngb);

		ReleaseMutex(sharedData.lockprocessing);
		return true;
	}
	ReleaseMutex(sharedData.lockprocessing);
	return false;
}

void PViewNextFocus() {
	if (projectdata.addingnew) {
		switch (projectdata.focusedcontrol) {
		case 0:  projectdata.focusedcontrol = 1; inputstate.sendasstrokes = false; inputstate.redirect = GetDlgItem(projectdata.dlg, IDC_PENTRY);
			SendMessage(projectdata.dlg, WM_NEXTDLGCTL, (WPARAM)inputstate.redirect, TRUE); break;
		case 1:  projectdata.focusedcontrol = 2; SendMessage(projectdata.dlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(projectdata.dlg, IDC_POK), TRUE); break;
		case 2:  projectdata.focusedcontrol = 3; SendMessage(projectdata.dlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(projectdata.dlg, IDC_PCANCEL), TRUE); break;
		default: projectdata.focusedcontrol = 0; inputstate.sendasstrokes = true;  inputstate.redirect = GetDlgItem(projectdata.dlg, IDC_PSTROKE);
			SendMessage(projectdata.dlg, WM_NEXTDLGCTL, (WPARAM)inputstate.redirect, TRUE); break;
		}
	}
}

std::list<singlestroke*>::iterator GetItem(int index) {
	int iindex = 0;

	auto it = (projectdata.strokes.end());
	for (; it != projectdata.strokes.cbegin(); it--) {
		if (iindex == index) {
			return it;
		}
		iindex++;
	}

	return it;
}

int GetTextFromItem(const std::list<singlestroke*>::const_iterator &item) {

	int pos = 0;

	if (item == projectdata.strokes.cend())
		return 0;

	if (projectdata.strokes.size() > 0){
		auto it = (--projectdata.strokes.cend());
		for (; it != projectdata.strokes.cbegin(); it--) {
			if ((*it)->textout->first == *it)
				pos += (*it)->textout->text.length();
			if (it == item)
				return pos;
		}
	}
	pos += (*projectdata.strokes.cbegin())->textout->text.length();
	return pos;
}

void SetTextSel(unsigned int min, unsigned int max) {
	CHARRANGE crnew;
	unsigned int index = 1;
	projectdata.selectionmax = max;
	projectdata.selectionmin = min;


	if (max == 0) {
		crnew.cpMax = 0;
	}
	if (min == 0) {
		crnew.cpMin = 0;
	}

	if (projectdata.strokes.size() > 0){
	
		auto it = (--projectdata.strokes.cend());
		int pos = 0;
		for (; it != projectdata.strokes.cbegin(); it--) {
			if (index == max) {
				crnew.cpMax = pos + (*it)->textout->text.length();
			}
			if (index == min) {
				crnew.cpMin = pos + (*it)->textout->text.length();
			}
			if ((*it)->textout->first == *it)
				pos += (*it)->textout->text.length();
			index++;
		}
		if (index <= max) {
			crnew.cpMax = pos + (*it)->textout->text.length();
		}
		if (index <= min) {
			crnew.cpMin = pos + (*it)->textout->text.length();
		}
	}

	SendMessage(GetDlgItem(projectdata.dlg, IDC_MAINTEXT), EM_EXSETSEL, 0, (LPARAM)&crnew);
}

std::list<singlestroke*>::iterator GetItemByText(unsigned int textindex) {
	int iindex = projectdata.strokes.size();
	if (iindex == 0) {
		return projectdata.strokes.end();
	}


	auto it = (--projectdata.strokes.end());
	int pos = 0;
	for (; it != projectdata.strokes.cbegin(); it--) {
		
			if (textindex < pos + ((*it)->textout->text.length()+1) / 2) {
				it++;
				return it;
			}

		if ((*it)->textout->first == *it) {
			pos += (*it)->textout->text.length();
		}
		iindex--;
	}

	if (it == projectdata.strokes.cbegin()) {
		if (textindex < pos + ((*it)->textout->text.length()+1) / 2) {
			it++;
			return it;
		}
		else {
			return it;
		}
	}

	return it;
}

int StrokeFromTextIndx(unsigned int txtindex) {
	unsigned int index = 1;
	if (projectdata.strokes.size() == 0) {
		return 0;
	}

	auto it = (--projectdata.strokes.cend());
	int pos = 0;
	for (; it != projectdata.strokes.cbegin(); it--) {
		if (txtindex < pos + ((*it)->textout->text.length()+1) / 2) {
			return index - 1;
		}

		if ((*it)->textout->first == *it) {
			pos += (*it)->textout->text.length();
		}
		index++;
	}
	if (it == projectdata.strokes.cbegin()) {
		if (txtindex < pos + ((*it)->textout->text.length()+1) / 2) {
			return index - 1;
		}
		else {
			return index;
		}
	}

	return index;
}


void PlaySelected() {
	
	CHARRANGE crngb;
	SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_EXGETSEL, NULL, (LPARAM)&crngb);
	int line = SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_EXLINEFROMCHAR, 0, crngb.cpMin);
	int lineb = SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_EXLINEFROMCHAR, 0, crngb.cpMax);

	auto max = GetItem(lineb);
	auto min = GetItem(line);

	LONGLONG start = 0;
	LONGLONG end = 0;

	if (min == projectdata.strokes.cend()) {
		start = 0;
	}
	else {
		if (min == max) {
			min++;
			while (min != projectdata.strokes.cend()) {
				if ((*min)->textout->first == (*min))
					break;
				min++;
			}
		}

		if (min != projectdata.strokes.cend()) {
			if ((*min)->timestamp >= projectdata.lead){
				start = ((*min)->timestamp - projectdata.lead) * (LONGLONG)10000;
			}
			else {
				start = 0;
			}
		}
	}

	if (max != projectdata.strokes.cend()) {
		end = ((*max)->timestamp) * (LONGLONG)10000;
	}

	if (end > start) {
		if (playSeeking != NULL && playControl != NULL) {
			playControl->Pause();
			playControl->Stop();
		}
		int filenum = getfilenumber(start / 10000);
		if (filenum == 0)
			return;

		if (filenum != projectdata.currentfile) {
			SafeRelease(&playgraph);
			SafeRelease(&playControl);
			SafeRelease(&playEvent);
			SafeRelease(&playSeeking);
			LoadPlayback(projectdata.file, filenum);

			projectdata.currentfile = filenum;

			if (playSeeking == NULL || playControl == NULL) {
				projectdata.currentfile = 0;
				return;
			}

			start -= projectdata.filetimes[filenum - 1] * 10000;
			end -= projectdata.filetimes[filenum - 1] * 10000;

			playSeeking->SetPositions(&start, AM_SEEKING_AbsolutePositioning, &end, AM_SEEKING_AbsolutePositioning);

			UINT_PTR id = 0;
			SetTimer(projectdata.dlg, id, (end - start) / 10000, NULL);
			playControl->Run();
		}
		else {
			start -= projectdata.filetimes[filenum-1] * 10000;
			end -= projectdata.filetimes[filenum-1] * 10000;

			if (FAILED(playSeeking->SetPositions(&start, AM_SEEKING_AbsolutePositioning, &end, AM_SEEKING_AbsolutePositioning))) {
				//MessageBox(NULL, TEXT("Seeking Failed"), TEXT("Error"), MB_OK);
				//this is really dumb, but it is the only way we can seek backwards -- break down and rebuild the graph
				SafeRelease(&playgraph);
				SafeRelease(&playControl);
				SafeRelease(&playEvent);
				SafeRelease(&playSeeking);
				LoadPlayback(projectdata.file, filenum);

				if (playSeeking == NULL || playControl == NULL) {
					projectdata.currentfile = 0;
					return;
				}

				playSeeking->SetPositions(&start, AM_SEEKING_AbsolutePositioning, &end, AM_SEEKING_AbsolutePositioning);

				UINT_PTR id = 0;
				SetTimer(projectdata.dlg, id, (end - start) / 10000, NULL);
				playControl->Run();
			}
			else {
				UINT_PTR id = 0;
				// because setting the stop position ... doesn't actually work -- a common problem with the ogg libraries for some reason
				SetTimer(projectdata.dlg, id, (end - start) / 10000, NULL);
				playControl->Run();
			}
		}
	}
}


LRESULT CALLBACK StrokeList(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	static int initialchr = 0;
	static bool capturing = false;

	switch (uMsg) {
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_CHAR:
	case WM_LBUTTONDBLCLK:
		return 0;
	case WM_LBUTTONDOWN:
		SetCapture(hWnd);
		POINTL p;
		p.x = GET_X_LPARAM(lParam);
		p.y = GET_Y_LPARAM(lParam);
		initialchr = SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_CHARFROMPOS, 0, (LPARAM)(&p));
		capturing = true;
		//return 0;
	case WM_MOUSEMOVE:
		if (!capturing)
			return DefSubclassProc(hWnd, uMsg, wParam, lParam);
	{
			POINTL p;
			p.x = GET_X_LPARAM(lParam);
			p.y = GET_Y_LPARAM(lParam);
			int cchar = SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_CHARFROMPOS, 0, (LPARAM)(&p));
			 
			int lesser;
			int greater;
			if (initialchr > cchar) {
				lesser = cchar;
				greater = initialchr;
			}
			else {
				lesser = initialchr;
				greater = cchar;
			}
						 projectdata.settingsel = true;
						 int line = SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_EXLINEFROMCHAR, 0, lesser);
						 int lineb = SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_EXLINEFROMCHAR, 0, greater);

						 //protect against selecting in the middle of a word
						 auto it = GetItem(line);
						 if (it != projectdata.strokes.cend()) {
							 while ((*it)->textout->first != (*it)) {
								 it--;
								 line++;
							 }
						 }

						 if (line > lineb)
							 lineb = line;

						 if (lineb != line) {
							 it = GetItem(lineb);
							 if (it != projectdata.strokes.cend()) {
								 while ((*it)->textout->first != (*it)) {
									 it--;
									 lineb++;
								 }
							 }
						 }

						 int lineindex = SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_LINEINDEX, line, 0);
						 int lineindexb = SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_LINEINDEX, lineb, 0);
						 CHARRANGE crnew;
						 crnew.cpMin = lineindex + 23;
						 crnew.cpMax = lineindexb + 23;
						 SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_EXSETSEL, 0, (LPARAM)&crnew);
						 SetTextSel(line, lineb);
						 projectdata.settingsel = false;
	}
		return 0;
	case WM_LBUTTONUP:
		capturing = false;
		ReleaseCapture();
		if (projectdata.autoplayback)
			PlaySelected();
		return 0;
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}


LRESULT CALLBACK MainText(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	static int initialchr = 0;
	static bool capturing = false;

	switch (uMsg) {
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_CHAR:
	case WM_LBUTTONDBLCLK:
		return 0;
	case WM_LBUTTONDOWN:
		SetCapture(hWnd);
		POINTL p;
		p.x = GET_X_LPARAM(lParam);
		p.y = GET_Y_LPARAM(lParam);
		initialchr = SendMessage(GetDlgItem(projectdata.dlg, IDC_MAINTEXT), EM_CHARFROMPOS, 0, (LPARAM)(&p));
		capturing = true;
		//return 0;
	case WM_MOUSEMOVE:
		if (!capturing)
			return DefSubclassProc(hWnd, uMsg, wParam, lParam);
		{
			POINTL p;
			p.x = GET_X_LPARAM(lParam);
			p.y = GET_Y_LPARAM(lParam);
			int cchar = SendMessage(GetDlgItem(projectdata.dlg, IDC_MAINTEXT), EM_CHARFROMPOS, 0, (LPARAM)(&p));

			int lesser;
			int greater;
			if (initialchr > cchar) {
				lesser = cchar;
				greater = initialchr;
			}
			else {
				lesser = initialchr;
				greater = cchar;
			}
			projectdata.settingsel = true;
			int line = StrokeFromTextIndx(lesser);
			int lineb = StrokeFromTextIndx(greater);
			int lineindex = SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_LINEINDEX, line, 0);
			int lineindexb = SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_LINEINDEX, lineb, 0);
			CHARRANGE crnew;
			crnew.cpMin = lineindex + 23;
			crnew.cpMax = lineindexb + 23;
			SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_EXSETSEL, 0, (LPARAM)&crnew);
			SetTextSel(line, lineb);

			projectdata.settingsel = false;
		}
		return 0;
	case WM_LBUTTONUP:
		capturing = false;
		ReleaseCapture();
		if (projectdata.autoplayback)
			PlaySelected();
		return 0;
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void ShowBatch(int show) {
	ShowWindow(GetDlgItem(projectdata.dlg, IDC_PENTRY), show);
	ShowWindow(GetDlgItem(projectdata.dlg, IDC_PSTROKE), show);
	ShowWindow(GetDlgItem(projectdata.dlg, IDC_POK), show);
	ShowWindow(GetDlgItem(projectdata.dlg, IDC_PCANCEL), show);
	ShowWindow(GetDlgItem(projectdata.dlg, IDC_PNEW), show);
}



INT_PTR CALLBACK PlaybackSettings(_In_  HWND hwndDlg, _In_  UINT uMsg, _In_  WPARAM wParam, _In_  LPARAM lParam) {
	switch (uMsg)
	{
	case WM_INITDIALOG:
	{
		SetWindowText(GetDlgItem(hwndDlg, IDC_LEAD), std::to_wstring(projectdata.lead).c_str());

		return TRUE;
	}
	case WM_CLOSE:
		EndDialog(hwndDlg, IDCANCEL);

		return TRUE;
	case WM_COMMAND:
		if (HIWORD(wParam) == BN_CLICKED) {
			switch (LOWORD(wParam))
			{
			case IDOK:
			{
				projectdata.lead = _wtoi(getWinStr(GetDlgItem(hwndDlg, IDC_LEAD)).c_str());
				EndDialog(hwndDlg, IDOK);
				return TRUE;
			}
			case IDCANCEL:


				EndDialog(hwndDlg, IDCANCEL);
				return TRUE;
			}
		}
	}
	return FALSE;
}

INT_PTR CALLBACK PViewProc(_In_  HWND hwndDlg, _In_  UINT uMsg, _In_  WPARAM wParam, _In_  LPARAM lParam) {
	NMHDR* hdr;

	switch (uMsg) {
	case WM_TIMER:
	{
		if (playControl != NULL) {
			playControl->Pause();
			playControl->Stop();
		}
	}
	case WM_ACTIVATE:
		if (wParam == 0) {
			modelesswnd = NULL;
			projectdata.open = false;
		}
		else {
			modelesswnd = hwndDlg;
			projectdata.open = true;
		}
		return FALSE;
	case WM_SIZE:
	{
					RECT rt;
					GetClientRect(hwndDlg, &rt);
					if (projectdata.addingnew) {
						SetWindowPos(GetDlgItem(hwndDlg, IDC_PSTROKELIST), NULL, 0, 0, projectdata.textwidth, rt.bottom - rt.top-30, SWP_NOZORDER);
						SetWindowPos(GetDlgItem(hwndDlg, IDC_MAINTEXT), NULL, projectdata.textwidth, 0, rt.right - rt.left - projectdata.textwidth, rt.bottom - rt.top-30, SWP_NOZORDER);
					}
					else {
						SetWindowPos(GetDlgItem(hwndDlg, IDC_PSTROKELIST), NULL, 0, 0, projectdata.textwidth, rt.bottom - rt.top, SWP_NOZORDER);
						SetWindowPos(GetDlgItem(hwndDlg, IDC_MAINTEXT), NULL, projectdata.textwidth, 0, rt.right - rt.left - projectdata.textwidth, rt.bottom - rt.top, SWP_NOZORDER);
					}
					SetWindowPos(GetDlgItem(hwndDlg, IDC_PNEW), NULL, 0, rt.bottom-rt.top - 30, rt.right - rt.left, 30, SWP_NOZORDER);

					SetWindowPos(GetDlgItem(hwndDlg, IDC_PSTROKE), GetDlgItem(hwndDlg, IDC_PNEW), 5, rt.bottom - rt.top - 25, 160, 20, 0);
					SetWindowPos(GetDlgItem(hwndDlg, IDC_PENTRY), GetDlgItem(hwndDlg, IDC_PNEW), 170, rt.bottom - rt.top - 25, 160, 20, 0);
					SetWindowPos(GetDlgItem(hwndDlg, IDC_POK), GetDlgItem(hwndDlg, IDC_PNEW), 335, rt.bottom - rt.top - 26, 30, 22, 0);
					SetWindowPos(GetDlgItem(hwndDlg, IDC_PCANCEL), GetDlgItem(hwndDlg, IDC_PNEW), 370, rt.bottom - rt.top - 26, 45, 22, 0);
	}
		return TRUE;
	case WM_QUIT:
		projectdata.open = false;
		projectdata.dlg = NULL;
		inputstate.redirect = NULL;
		return TRUE;
	case WM_CLOSE:
		DestroyWindow(projectdata.dlg);
		projectdata.open = false;
		projectdata.dlg = NULL;
		inputstate.redirect = NULL;

		if (playControl != NULL) {
			playControl->Pause();
			playControl->Stop();
		}

		if (recControl != NULL) {
			ULONGLONG thetime = ExposePosition();

			recControl->Pause();
			recControl->Stop();

			projectdata.filetimes.push_back(thetime);
			
			RegisterFile(thetime);
		}

		SafeRelease(&recControl);
		SafeRelease(&recEvent);
		SafeRelease(&recSeeking);
		SafeRelease(&recgraph);

		SafeRelease(&playgraph);
		SafeRelease(&playControl);
		SafeRelease(&playEvent);
		SafeRelease(&playSeeking);


		saveProject(projectdata.file);
		projectdata.d->close();

		
		Sleep(20);

		CloseHandle(projectdata.realtime);
		
		return FALSE;
	case WM_INITDIALOG:
	{
						  
						  projectdata.dlg = hwndDlg;
						  projectdata.d = new dictionary("");
						  tstring form;
						  if (sharedData.currentd != NULL)
							  form = sharedData.currentd->format;
						  else
							  form = TEXT("#STKPWHRAO*EUFRPBLGTSDZ");

						  if (!projectdata.d->opentransient(form))
							  MessageBox(NULL, TEXT("Unable to open project's dictionary"), TEXT("Error"), MB_OK);

						 ShowBatch(SW_HIDE);

						 projectdata.settingsel = false;
						 projectdata.paused = true;
						 projectdata.reloading = false;
						 projectdata.autoplayback = false;
						 projectdata.filetimes.clear();
						 
						  PARAFORMAT2 pf;
						  memset(&pf, 0, sizeof(pf));
						  pf.cbSize = sizeof(PARAFORMAT2);
						  pf.dwMask = PFM_OFFSETINDENT | PFM_SPACEAFTER | PFM_OFFSET;
						  pf.dxStartIndent = (24 * 1440) / 72;
						  pf.dySpaceAfter = (5 * 1440) / 72;
						  pf.dxOffset = -pf.dxStartIndent;
						  SendMessage(GetDlgItem(hwndDlg, IDC_MAINTEXT), EM_SETPARAFORMAT, 0, (LPARAM)&pf);

						  CHARFORMAT2 cf;
						  memset(&cf, 0, sizeof(cf));
						  cf.cbSize = sizeof(CHARFORMAT2);
						  cf.dwMask = CFM_FACE | CFM_SIZE;
						  cf.yHeight = (10 * 1440) / 72;
						  _tcscpy_s(cf.szFaceName, LF_FACESIZE, TEXT("Consolas"));

						  if (SendMessage(GetDlgItem(hwndDlg, IDC_PSTROKELIST), EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf) == 0) {
							  _tcscpy_s(cf.szFaceName, LF_FACESIZE, TEXT("Courier New"));
							  SendMessage(GetDlgItem(hwndDlg, IDC_PSTROKELIST), EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
						  }

						  memset(&cf, 0, sizeof(cf));
						  cf.cbSize = sizeof(CHARFORMAT2);
						  cf.dwMask = CFM_FACE | CFM_SIZE | CFM_WEIGHT;
						  cf.yHeight = (settings.fsize * 1440) / 72;
						  cf.wWeight = settings.fweight;
						  _tcscpy_s(cf.szFaceName, LF_FACESIZE, settings.fname.c_str());

						  SendMessage(GetDlgItem(hwndDlg, IDC_MAINTEXT), EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);

						  SetWindowText(GetDlgItem(hwndDlg, IDC_PSTROKELIST), TEXT("                       "));
						  CHARRANGE crnew;
						  crnew.cpMax = 23;
						  crnew.cpMin = 23;
						  SendMessage(GetDlgItem(hwndDlg, IDC_PSTROKELIST), EM_EXSETSEL, 0, (LPARAM)&crnew);

						  SetWindowSubclass(GetDlgItem(hwndDlg, IDC_PSTROKELIST), &StrokeList, 1245, NULL);
						  SetWindowSubclass(GetDlgItem(hwndDlg, IDC_MAINTEXT), &MainText, 1246, NULL);

						  projectdata.strokes.clear();

						  //projectdata.starttick = projectdata.pausetick = GetTickCount64();

						  projectdata.addingnew = false;
						  projectdata.open = true;
						  projectdata.selectionmax = 0;
						  projectdata.selectionmin = 0;
						  projectdata.cursorpos = 0;

						  if (projectdata.file.find(TEXT(".srf")) != std::string::npos) {
							  projectdata.realtime = CreateFile(projectdata.file.c_str(), GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, 0, NULL);
							  
							  LoadRealtime();

							  projectdata.file = projectdata.file.replace(projectdata.file.find(TEXT(".srf")), 4, TEXT(".prj"));
						  }
						  else {
							  loadProject(projectdata.file);
							  //open realtime file

							  tstring realtime = projectdata.file;
							  realtime = realtime.replace(realtime.find(TEXT(".prj")), 4, TEXT(".srf"));

							  projectdata.realtime = CreateFile(realtime.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, 0, NULL);
							  if (projectdata.realtime != INVALID_HANDLE_VALUE) {
									SetFilePointer(projectdata.realtime, 0, NULL, FILE_END);
							  }

						  }

						  if (projectdata.filetimes.size() == 0) {
							  projectdata.filetimes.push_back(0);
						  }

						  projectdata.currentfile = 0;

						  POINTL pt;
						  SendMessage(GetDlgItem(hwndDlg, IDC_PSTROKELIST), EM_POSFROMCHAR, (WPARAM)&pt, 1);
						  projectdata.textwidth = pt.x * 23;

						  PViewProc(hwndDlg, WM_SIZE, 0, 0);

						  CHARRANGE crngb;
						  crngb.cpMin = crngb.cpMax = GetWindowTextLength(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST));
						  SendMessage(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST), EM_EXSETSEL, NULL, (LPARAM)&crngb);
						  crngb.cpMin = crngb.cpMax = GetWindowTextLength(GetDlgItem(projectdata.dlg, IDC_MAINTEXT));
						  SendMessage(GetDlgItem(projectdata.dlg, IDC_MAINTEXT), EM_EXSETSEL, NULL, (LPARAM)&crngb);

						  SetFocus(GetDlgItem(projectdata.dlg, IDC_MAINTEXT));
	}
		return FALSE;
	case WM_NOTIFY:
		hdr = (NMHDR*)lParam;
		
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDM_PLAY:
			projectdata.autoplayback = !projectdata.autoplayback;
			if (projectdata.autoplayback)
				CheckMenuItem(GetMenu(projectdata.dlg), IDM_PLAY, MF_CHECKED);
			else
				CheckMenuItem(GetMenu(projectdata.dlg), IDM_PLAY, MF_UNCHECKED);
			break;
		case IDM_PPLAY:
			PlaySelected();
			break;
		case IDM_POPTIONS:
			DialogBox(instance, MAKEINTRESOURCE(IDD_POPTIONS), projectdata.dlg, PlaybackSettings);
			break;
		case IDM_REC:
			InitRecording(projectdata.filetimes.size());
			break;
		case IDM_PREC:
			PauseRecording();
			break;
		case IDC_PSTROKE:
			if (HIWORD(wParam) == EN_SETFOCUS) {
				projectdata.focusedcontrol = 0;
				inputstate.redirect = GetDlgItem(projectdata.dlg, IDC_PSTROKE);
				inputstate.sendasstrokes = true;
			}
			break;
		case IDC_PENTRY:
			if (HIWORD(wParam) == EN_SETFOCUS) {
				projectdata.focusedcontrol = 1;
				inputstate.redirect = GetDlgItem(projectdata.dlg, IDC_PENTRY);
				inputstate.sendasstrokes = false;
			}
			break;
		case IDM_RECREATE:
			reRealtime();
			break;
		case IDM_EXDICT:
			exDict();
			break;
		case IDM_PSTROKEEX:
			SaveText(GetDlgItem(projectdata.dlg, IDC_PSTROKELIST));
			break;
		case IDM_PTEXTEX:
			SaveText(GetDlgItem(projectdata.dlg, IDC_MAINTEXT));
			break;
		case IDC_POK:
		{
						SetFocus(GetDlgItem(hwndDlg, IDC_MAINTEXT));
						ShowBatch(SW_HIDE);
						projectdata.addingnew = false;
						inputstate.redirect = NULL;

						tstring txt = getWinStr(GetDlgItem(hwndDlg, IDC_PENTRY));
						tstring strke = getWinStr(GetDlgItem(hwndDlg, IDC_PSTROKE));
						int numstrokes = 0;

						unsigned __int8* sdata = texttomultistroke(strke, numstrokes, sharedData.currentd->format);
						std::string trmd = trimstr(ttostr(txt), " ");

						RegisterDef(strke, strtotstr(trmd));

						DB_TXN* trans;
						projectdata.d->env->txn_begin(projectdata.d->env, NULL, &trans, 0);
						projectdata.d->addDItem(sdata, numstrokes * 3, trmd, trans);
						trans->commit(trans, 0);

						delete sdata;
						PViewProc(hwndDlg, WM_SIZE, 0, 0);

						union {
							unsigned __int8 sval[4];
							unsigned __int32 ival;
						} tstroke;
						memcpy(tstroke.sval, sharedData.currentd->sdelete, 3);
						addStroke(tstroke.ival);
		}
			break;
		case IDC_PCANCEL:
			SetFocus(GetDlgItem(hwndDlg, IDC_MAINTEXT));
			ShowBatch(SW_HIDE);
			projectdata.addingnew = false;
			inputstate.redirect = NULL;
			PViewProc(hwndDlg, WM_SIZE, 0, 0);

			union {
				unsigned __int8 sval[4];
				unsigned __int32 ival;
			} tstroke;
			memcpy(tstroke.sval, sharedData.currentd->sdelete, 3);
			addStroke(tstroke.ival);

			break;
		case IDM_PFONT:
			CHOOSEFONT cf;
			memset(&cf, 0, sizeof(cf));
			cf.lStructSize = sizeof(CHOOSEFONT);
			cf.hwndOwner = hwndDlg;
			cf.Flags =  CF_NOSCRIPTSEL | CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS;
			cf.rgbColors = RGB(0, 0, 0);

			LOGFONT lf;
			memset(&lf, 0, sizeof(lf));
			HDC screen = GetDC(NULL);
			lf.lfHeight = -MulDiv(settings.fsize, GetDeviceCaps(screen, LOGPIXELSY), 72);
			ReleaseDC(NULL, screen);
			lf.lfWeight = settings.fweight;

			_tcscpy_s(lf.lfFaceName, LF_FACESIZE, settings.fname.c_str());
			cf.lpLogFont = &lf;

			if (ChooseFont(&cf)) {
				settings.fsize = cf.iPointSize / 10;
				settings.fweight = lf.lfWeight;
				settings.fname = lf.lfFaceName;

				CHARFORMAT2 cf;
				memset(&cf, 0, sizeof(cf));
				cf.cbSize = sizeof(CHARFORMAT2);
				cf.dwMask = CFM_FACE | CFM_SIZE | CFM_WEIGHT;
				cf.yHeight = (settings.fsize * 1440) / 72;
				cf.wWeight = settings.fweight;
				_tcscpy_s(cf.szFaceName, LF_FACESIZE, settings.fname.c_str());

				SendMessage(GetDlgItem(hwndDlg, IDC_MAINTEXT), EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
			}
			break;
		}

		return TRUE;
	case WM_NEWITEMDLG:
		SetWindowText(GetDlgItem(projectdata.dlg, IDC_PSTROKE), TEXT(""));
		SetWindowText(GetDlgItem(projectdata.dlg, IDC_PENTRY), TEXT(""));
		ShowBatch(SW_SHOW);

		projectdata.focusedcontrol = 0;
		inputstate.redirect = GetDlgItem(projectdata.dlg, IDC_PSTROKE);
		inputstate.sendasstrokes = true;
		//SendMessage(projectdata.dlg, WM_NEXTDLGCTL, (WPARAM)inputstate.redirect, TRUE);
		SetFocus(inputstate.redirect);
		projectdata.addingnew = true;
		PViewProc(hwndDlg, WM_SIZE, 0, 0);
		return TRUE;
	}
	
	return FALSE;
	}


void LaunchProjDlg(HINSTANCE hInst) {
	instance = hInst;
	if (sharedData.currentd == NULL) {
		MessageBox(NULL, TEXT("No dictionary selected"), TEXT(""), MB_OK);
		return;
	}
	if (projectdata.dlg == NULL) {

		OPENFILENAME file;
		TCHAR buffer[MAX_PATH] = TEXT("\0");
		memset(&file, 0, sizeof(OPENFILENAME));
		file.lStructSize = sizeof(OPENFILENAME);
		file.hwndOwner = NULL;
		file.lpstrFilter = TEXT("Project Files\0*.prj\0Stroke Record Files\0*.srf\0\0");
		file.nFilterIndex = 1;
		file.lpstrFile = buffer;
		file.nMaxFile = MAX_PATH;
		file.Flags = OFN_DONTADDTORECENT | OFN_NOCHANGEDIR;
		if (GetOpenFileName(&file)) {
			tstring filename = buffer;
			if (filename.find(TEXT(".srf")) != std::string::npos) {
				//filename += TEXT(".rtf");
			}
			else if (filename.find(TEXT(".prj")) != std::string::npos) {

			}
			else {
				filename += TEXT(".prj");
			}
			projectdata.file = filename;

			projectdata.dlg = CreateDialog(hInst, MAKEINTRESOURCE(IDD_PROJECT), NULL, PViewProc);
		
			ShowWindow(projectdata.dlg, SW_SHOW);
			SetForegroundWindow(projectdata.dlg);
		}
	}
}

ULONGLONG ExposePosition() {
	if (projectdata.filetimes.size() == 0) {
		return 0;
	}
	else if (recSeeking == NULL) {
		return projectdata.filetimes[projectdata.filetimes.size() - 1];
	}
	else {
		LONGLONG position = 0;
		if (recSeeking->GetCurrentPosition(&position) != S_OK)
			MessageBox(NULL, TEXT("GetCurrentPosition"), TEXT("ERR"), MB_OK);
		position /= 10000;
		return position + projectdata.filetimes[projectdata.filetimes.size() - 1];
	}
}