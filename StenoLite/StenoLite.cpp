﻿#include "stdafx.h"
#include "StenoLite.h"
#include <commctrl.h>
#include <Windowsx.h>
#include <regex>
#include <queue>
#include <list>
#include <fstream>
#include <string>
#include <locale>
#include <tuple>
#include <stack>
#include <Shobjidl.h>
#include <Strsafe.h>
#include <Richedit.h>
#include <gdiplus.h>
//#include <VersionHelpers.h>

#include "globals.h"
#include "stenodata.h"
#include "fileops.h"
#include "newentrydlg.h"
#include "texthelpers.h"
#include "pstroke.h"
#include "setmode.h"
#include "DView.h"

using namespace Gdiplus;
#pragma comment (lib,"Gdiplus.lib")


#define MAX_LOADSTRING 100
#define CONTROL_HEIGHT 260

HINSTANCE hInst;								// current instance
TCHAR szTitle[MAX_LOADSTRING];					// The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];			// the main window class name

ITaskbarList3* tskbrlst = NULL;


// Forward declarations of functions included in this code module:
ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK	About(HWND, UINT, WPARAM, LPARAM);


HWND CreateTabControl(HWND main);
LRESULT CALLBACK TabProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
LRESULT CALLBACK StaticProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
LRESULT CALLBACK PassBack(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
INT_PTR CALLBACK NewEntryProc(_In_  HWND hwndDlg, _In_  UINT uMsg, _In_  WPARAM wParam, _In_  LPARAM lParam);
LRESULT CALLBACK WordProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);



void stdstrMB(std::string msg, TCHAR* title){
	TCHAR param[200];
	param[msg.size()] = 0;
	std::copy(msg.begin(), msg.end(), param);
	MessageBox(NULL, param, title, MB_OK);
}


bool MyIsWindowsVersionOrGreater(WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor)
{
	OSVERSIONINFOEXW osvi = { sizeof(osvi), 0, 0, 0, 0, { 0 }, 0, 0 };
	DWORDLONG        const dwlConditionMask = VerSetConditionMask(
		VerSetConditionMask(
		VerSetConditionMask(
		0, VER_MAJORVERSION, VER_GREATER_EQUAL),
		VER_MINORVERSION, VER_GREATER_EQUAL),
		VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);

	osvi.dwMajorVersion = wMajorVersion;
	osvi.dwMinorVersion = wMinorVersion;
	osvi.wServicePackMajor = wServicePackMajor;

	return VerifyVersionInfoW(&osvi, VER_MAJORVERSION |
		VER_MINORVERSION | VER_SERVICEPACKMAJOR, dwlConditionMask) != FALSE;
}


LRESULT CALLBACK NoCaret(HWND hUserInfoWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	LRESULT lRes = DefSubclassProc(hUserInfoWnd, uMsg, wParam, lParam);
	if (uMsg == WM_SETFOCUS)
		HideCaret(hUserInfoWnd);
	return lRes;
}


int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPTSTR    lpCmdLine,
	_In_ int       nCmdShow) {

	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);


	sharedData.running = TRUE;
	sharedData.currentd = NULL;
	sharedData.addedtext = FALSE;
	sharedData.searching = TRUE;
	sharedData.newentry = CreateEvent(NULL, FALSE, FALSE, NULL);
	sharedData.newtext = CreateEvent(NULL, FALSE, FALSE, NULL);
	sharedData.protectqueue = CreateMutex(NULL, FALSE, NULL);
	textToStroke(std::string("1234567890"), sharedData.number);

	MSG msg;
	HACCEL hAccelTable;

	memset(inputstate.keys, 0, 4);
	memset(inputstate.stroke, 0, 4);
	inputstate.redirect = NULL;
	inputstate.sendasstrokes = false;
	memset(&settings, 0, sizeof(settings));
	memset(&newwordwin, 0, sizeof(newwordwin));

	controls.width = 0;
	controls.inited = FALSE;
	

	LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadString(hInstance, IDC_STENOLITE, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);

	GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR           gdiplusToken;
	GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

	// Perform application initialization: 
	if (!InitInstance(hInstance, nCmdShow)) {
		return FALSE;
	}

	sharedData.sevenorbetter = MyIsWindowsVersionOrGreater(6, 1, 0);

	HRESULT hr = CoInitializeEx(NULL, 0);
	if (FAILED(hr))
	{
		MessageBox(NULL, TEXT("Could not initialize COM"), TEXT("Error"), MB_OK);
	}
	else {
		hr = CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&tskbrlst));
		if (FAILED(hr))
		{
			if (sharedData.sevenorbetter)
				MessageBox(NULL, TEXT("Failed to create a CLSID_TaskbarList"), TEXT("Error"), MB_OK);
			tskbrlst = NULL;
		}
	}

	hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_STENOLITE));

	// Main message loop:
	BOOL dlgresult = FALSE;
	while (GetMessage(&msg, NULL, 0, 0)) {
		if (!IsDialogMessage(newwordwin.dlgwnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	setMode(0);
	

	if (tskbrlst != NULL) {
		tskbrlst->Release();
	}
	CoUninitialize();

	sharedData.running = FALSE;
	SetEvent(sharedData.newentry);
	SetEvent(sharedData.newtext);
	saveSettings();
	CloseHandle(sharedData.newentry);
	CloseHandle(sharedData.newtext);
	CloseHandle(sharedData.protectqueue);


	std::list<std::tuple<std::string, dictionary*>>::iterator it = sharedData.dicts.begin();
	while (it != sharedData.dicts.end()) {
		saveDictSettings(std::get<1, std::string, dictionary*>(*it));
		std::get<1, std::string, dictionary*>(*it)->close();
		
		it++;
	}

	if (newwordwin.dlgwnd != NULL) {
		DestroyWindow(newwordwin.dlgwnd);
		newwordwin.dlgwnd = NULL;
	}
	return (int)msg.wParam;
}


ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_STENOLITE));
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	//wcex.lpszMenuName = MAKEINTRESOURCE(IDC_STENOLITE);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_STENOLITE));

	return RegisterClassEx(&wcex);
}




void movecontrols(int offset) {

	SetWindowPos(controls.inputs, 0, 5, 30 - 20 -offset, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
	SetWindowPos(controls.dicts, 0, 5, 50 - 20 - offset, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
	SetWindowPos(controls.cktop, 0, 5, 75 - 20 - offset, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
	SetWindowPos(controls.cktrans, 0, 5, 95 - 20 - offset, 0, 0, SWP_NOZORDER | SWP_NOSIZE);

	SetWindowPos(controls.rdfront, 0, 5, 125 - 20 - offset, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
	SetWindowPos(controls.rdback, 0, 5, 145 - 20 - offset, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
	SetWindowPos(controls.rdnone, 0, 5, 165 - 20 - offset, 0, 0, SWP_NOZORDER | SWP_NOSIZE);

	SetWindowPos(controls.ckmistrans, 0, 5, 195 - 20 - offset, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
	SetWindowPos(controls.ckpref, 0, 5, 215 - 20 - offset, 0, 0, SWP_NOZORDER | SWP_NOSIZE);

	SetWindowPos(controls.bdict, 0, 5, 245 - 20 - offset, 0, 0, SWP_NOZORDER | SWP_NOSIZE);

}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	HWND hWnd;

	
	loadSettings();
	if (CreateThread(NULL, 0, processStrokes, &sharedData, 0, NULL) == NULL) {
		MessageBox(NULL, TEXT("FAILED"), TEXT("ERROR"), MB_OK);
	}
	//loadDictionaries();

	hInst = hInstance; // Store instance handle in our global variable

	
	INITCOMMONCONTROLSEX prams;
	prams.dwSize = sizeof(INITCOMMONCONTROLSEX);
	prams.dwICC = ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_WIN95_CLASSES;
	InitCommonControlsEx(&prams);

	hWnd = CreateWindowEx(WS_EX_LAYERED, szWindowClass, szTitle, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
		settings.xpos, settings.ypos, 200, settings.height, NULL, NULL, hInstance, NULL);

	if (!hWnd) {
		return FALSE;
	}
	controls.main = hWnd;

	

	controls.hTab = CreateTabControl(hWnd);

	SetWindowSubclass(controls.hTab, &TabProc, 1234, NULL);
	SetLayeredWindowAttributes(hWnd, NULL, 255, LWA_ALPHA);

	HDC hdc = GetDC(hWnd);
	HGDIOBJ old = SelectObject(hdc, GetStockObject(ANSI_FIXED_FONT));
	SIZE metr;
	GetTextExtentPoint32(hdc, TEXT("#STKPWHRAO*EUFRPBLGTSDZ"), 23, &metr);
	controls.width = metr.cx + 30;
	controls.lineheight = metr.cy;
	SelectObject(hdc, old);
	ReleaseDC(hWnd, hdc);

	SetWindowPos(hWnd, HWND_TOPMOST, settings.xpos, settings.ypos, controls.width, settings.height, SWP_NOMOVE | SWP_NOZORDER);
	RECT rt;
	GetClientRect(hWnd, &rt);
	SetWindowPos(controls.hTab, NULL, 0, 0, rt.right, rt.bottom, SWP_NOMOVE);
	TabCtrl_AdjustRect(controls.hTab, FALSE, &rt);

	controls.mestroke = CreateWindow(TEXT("EDIT"), TEXT("#STKPWHRAO*EUFRPBLGTSDZ"),
		WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_LEFT | WS_DISABLED | ES_READONLY,
		rt.left, rt.top, rt.right - rt.left, rt.bottom - rt.top, controls.hTab, NULL, hInstance, NULL);
	SendMessage(controls.mestroke, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), (LPARAM)true);
	controls.numlines = (rt.bottom - rt.top) / controls.lineheight;
	SetWindowSubclass(controls.mestroke, &PassBack, 1236, NULL);

	controls.mesuggest = CreateWindow(TEXT("EDIT"), TEXT(""),
		WS_CHILD | ES_MULTILINE | ES_LEFT | ES_READONLY | WS_VSCROLL,
		rt.left, rt.top, rt.right - rt.left, rt.bottom - rt.top, controls.hTab, NULL, hInstance, NULL);
	SendMessage(controls.mesuggest, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), (LPARAM)true);
	SetWindowSubclass(controls.mesuggest, &NoCaret, 1237, NULL);

	controls.scontainer = CreateWindow(L"STATIC", L"0",
		WS_CHILD | SS_WHITERECT,
		rt.left, rt.top, rt.right-rt.left, rt.bottom-rt.top, controls.hTab, NULL, hInstance, NULL);

	SetWindowSubclass(controls.scontainer, &StaticProc, 1235, NULL);
	
	controls.inputs = CreateWindow(WC_COMBOBOX, TEXT(""),
		CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE,
		5, 30 - 20, controls.width-50, 300, controls.scontainer, NULL, hInstance, NULL);
	int sel = settings.mode;
	SendMessage(controls.inputs, (UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)(L"Off"));
	SendMessage(controls.inputs, (UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)(L"Keyboard"));
	SendMessage(controls.inputs, (UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)(L"Treal"));
	SendMessage(controls.inputs, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), (LPARAM)true);
	SendMessage(controls.inputs, CB_SETCURSEL, (WPARAM)sel, (LPARAM)0);

	controls.dicts = CreateWindow(WC_COMBOBOX, TEXT(""),
		CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE,
		5, 30 - 20, controls.width - 50, 300, controls.scontainer, NULL, hInstance, NULL);

	TCHAR buffer[MAX_PATH];
	std::list<std::string> dlist = EnumDicts();
	for (std::list<std::string>::iterator i = dlist.begin(); i != dlist.end(); i++) {
		std::copy((*i).begin(), (*i).end(), buffer);
		buffer[(*i).length()] = 0;
		SendMessage(controls.dicts, (UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)(buffer));
	}


	SendMessage(controls.dicts, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), (LPARAM)true);
	std::string temp = settings.dict;
	std::copy(temp.cbegin(), temp.cend(), buffer);
	buffer[temp.length()] = 0;
	sel = SendMessage(controls.dicts, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(buffer));
	SendMessage(controls.dicts, CB_SETCURSEL, (WPARAM)sel, (LPARAM)0);


	controls.cktop = CreateWindow( L"BUTTON", L"Always on top",  
		WS_TABSTOP | WS_CHILD | BS_AUTOCHECKBOX | WS_VISIBLE,
		5, 55 - 20, controls.width - 50, 20, controls.scontainer, NULL, hInstance, NULL);
	controls.cktrans = CreateWindow(L"BUTTON", L"Transparent",
		WS_TABSTOP | WS_CHILD | BS_AUTOCHECKBOX | WS_VISIBLE,
		5, 75 - 20, controls.width - 50, 20, controls.scontainer, NULL, hInstance, NULL);
	SendMessage(controls.cktop, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), (LPARAM)true);
	SendMessage(controls.cktrans, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), (LPARAM)true);

	controls.rdfront = CreateWindow(L"BUTTON", L"Leading space",
		WS_TABSTOP | WS_CHILD | BS_AUTORADIOBUTTON | WS_VISIBLE,
		5, 105 - 20, controls.width - 50, 20, controls.scontainer, NULL, hInstance, NULL);
	controls.rdback = CreateWindow(L"BUTTON", L"Trailing space",
		WS_TABSTOP | WS_CHILD | BS_AUTORADIOBUTTON | WS_VISIBLE,
		5, 125 - 20, controls.width - 50, 20, controls.scontainer, NULL, hInstance, NULL);
	controls.rdnone = CreateWindow(L"BUTTON", L"No space",
		WS_TABSTOP | WS_CHILD | BS_AUTORADIOBUTTON | WS_VISIBLE,
		5, 145 - 20, controls.width - 50, 20, controls.scontainer, NULL, hInstance, NULL);
	
	SendMessage(controls.rdfront, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), (LPARAM)true);
	SendMessage(controls.rdback, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), (LPARAM)true);
	SendMessage(controls.rdnone, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), (LPARAM)true);

	
	controls.ckmistrans = CreateWindow(TEXT("BUTTON"), TEXT("Hide mistranslates"),
		WS_TABSTOP | WS_CHILD | BS_AUTOCHECKBOX | WS_VISIBLE,
		5, 175 - 20, controls.width - 50, 20, controls.scontainer, NULL, hInstance, NULL);
	controls.ckpref = CreateWindow(TEXT("BUTTON"), TEXT("Prefix search"),
		WS_TABSTOP | WS_CHILD | BS_AUTOCHECKBOX | WS_VISIBLE,
		5, 200 - 20, controls.width - 50, 20, controls.scontainer, NULL, hInstance, NULL);

	SendMessage(controls.ckmistrans, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), (LPARAM)true);
	SendMessage(controls.ckpref, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), (LPARAM)true);

	controls.bdict = CreateWindow(L"BUTTON", L"Open Dictionary",
		WS_TABSTOP | WS_CHILD | BS_PUSHBUTTON | WS_VISIBLE,
		5, 55 - 20, controls.width - 50, 20, controls.scontainer, NULL, hInstance, NULL);
	SendMessage(controls.bdict, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), (LPARAM)true);

	controls.sscroll = CreateWindow(L"ScrollBar", L"",
		WS_TABSTOP | WS_CHILD | SBS_VERT,
		rt.right-rt.left-15, 0, 15, rt.bottom - rt.top, controls.scontainer, NULL, hInstance, NULL);
	
	movecontrols(0);
	

	if (settings.top == TRUE) {
		SetWindowPos(controls.main, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		SendMessage(controls.cktop, BM_SETCHECK, BST_CHECKED, 0);
	}
	if (settings.trans == TRUE) {
		SetLayeredWindowAttributes(controls.main, NULL, 175, LWA_ALPHA);
		SendMessage(controls.cktrans, BM_SETCHECK, BST_CHECKED, 0);
	}
	if (settings.prefix == TRUE) {
		SendMessage(controls.ckpref, BM_SETCHECK, BST_CHECKED, 0);
	}
	if (settings.mistrans == TRUE) {
		SendMessage(controls.ckmistrans, BM_SETCHECK, BST_CHECKED, 0);
	}
	if (settings.space == 1) {
		SendMessage(controls.rdback, BM_SETCHECK, BST_CHECKED, 0);
	}
	else if (settings.space == 2) {
		SendMessage(controls.rdnone, BM_SETCHECK, BST_CHECKED, 0);
	}
	else {
		SendMessage(controls.rdfront, BM_SETCHECK, BST_CHECKED, 0);
	}

	controls.inited = TRUE;
	setMode(settings.mode);
	//SetWindowsHookEx(WH_KEYBOARD_LL, ,0);
	/* A P-K- A A A A P- it 3-#-#-2-ST-P-S-
	UINT nDevices;
	PRAWINPUTDEVICELIST pRawInputDeviceList;
	if (GetRawInputDeviceList(NULL, &nDevices, sizeof(RAWINPUTDEVICELIST)) != 0) { }
	pRawInputDeviceList = new RAWINPUTDEVICELIST[nDevices];
	GetRawInputDeviceList(pRawInputDeviceList, &nDevices, sizeof(RAWINPUTDEVICELIST));
	// do the job...
	for (int i = 0; i < nDevices; i++) {
		RID_DEVICE_INFO info;
		UINT size = sizeof(RID_DEVICE_INFO);
		GetRawInputDeviceInfo(pRawInputDeviceList[i].hDevice, RIDI_DEVICEINFO, &info, &size);
		if (info.dwType == RIM_TYPEKEYBOARD) {
			std::wstring mbtext = std::to_wstring(info.hid.dwVendorId) + std::wstring(L", ") + std::to_wstring(info.hid.usUsage) + std::wstring(L", ") + std::to_wstring(info.hid.usUsagePage);
			MessageBox(NULL, mbtext.c_str(), TEXT("KEYBOARD"), MB_OK);
		}
		else if (info.dwType == RIM_TYPEMOUSE) {
			std::wstring mbtext = std::to_wstring(info.hid.dwVendorId) + std::wstring(L", ") + std::to_wstring(info.hid.usUsage) + std::wstring(L", ") + std::to_wstring(info.hid.usUsagePage);
			MessageBox(NULL, mbtext.c_str(), TEXT("MOUSE"), MB_OK);
		}
		else {
			std::wstring mbtext = std::to_wstring(info.hid.dwVendorId) + std::wstring(L", ") + std::to_wstring(info.hid.usUsage) + std::wstring(L", ") + std::to_wstring(info.hid.usUsagePage);
			MessageBox(NULL, mbtext.c_str(), TEXT("HID"), MB_OK);
		}
	}
	// after the job, free the RAWINPUTDEVICELIST
	free(pRawInputDeviceList);
	/**/
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	return TRUE;
}

void sendstroke(unsigned __int8* keys) {
	TCHAR buffer[32] = TEXT("\r\n");
	stroketosteno(keys, &buffer[2]);

	int lines = SendMessage(controls.mestroke, EM_GETLINECOUNT, 0, 0);
	if (lines > controls.numlines-1) {
		int end = SendMessage(controls.mestroke, EM_LINEINDEX, 1, 0);
		SendMessage(controls.mestroke, EM_SETSEL, 0, end);
		SendMessage(controls.mestroke, EM_REPLACESEL, FALSE, (LPARAM)TEXT(""));
	}

	int len = SendMessage(controls.mestroke, WM_GETTEXTLENGTH, 0, 0);
	SendMessage(controls.mestroke, EM_SETSEL, len, len);
	SendMessage(controls.mestroke, EM_REPLACESEL, FALSE, (LPARAM)buffer);
	
	__int32* val = (__int32*)keys;
	addStroke(*val);

	keys[0] = keys[1] = keys[2] = keys[4] = 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	PAINTSTRUCT ps;
	HDC hdc;

	switch (message)
	{
	case WM_LOAD_PROGRESS:
		if (sharedData.sevenorbetter) {
			switch (wParam) {
			case 1:
				if (tskbrlst)
					tskbrlst->SetProgressState(hWnd, TBPF_NORMAL);
				break;
			case 2:
				if (tskbrlst)
					tskbrlst->SetProgressValue(hWnd, sharedData.currentprogress, sharedData.totalprogress);
				break;
			case 3:
				if (tskbrlst) {
					tskbrlst->SetProgressState(hWnd, TBPF_NOPROGRESS);
					FLASHWINFO f;
					f.cbSize = sizeof(FLASHWINFO);
					f.hwnd = hWnd;
					f.uCount = 30;
					f.dwTimeout = 0;
					f.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
					FlashWindowEx(&f);
				}
				break;
			}
		}
		return 0;
	case WM_NEWITEMDLG:
		newwordwin.prevfocus = GetForegroundWindow();
		LaunchEntryDlg(hInst);
		return 0;
	case WM_INPUT:
	{
					 RAWINPUT* input = NULL;
					 UINT dwSize;

					 GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));

					 input = (RAWINPUT*)(new BYTE[dwSize]);

					 GetRawInputData((HRAWINPUT)lParam, RID_INPUT, input, &dwSize, sizeof(RAWINPUTHEADER));

					 if (settings.mode == 2 && input->header.dwType == RIM_TYPEHID)
					 {
						 

						 __int8 pre = inputstate.keys[0] | inputstate.keys[1] | inputstate.keys[2] | inputstate.keys[3];

						 if (input->data.hid.dwSizeHid == 6) {
							 for (int indx = 0; indx < input->data.hid.dwCount; indx++) {
								 if ((input->data.hid.bRawData[indx * 6 + 1] & 0x80) != 0)
									 inputstate.keys[0] |= 0x04;
								 else
									 inputstate.keys[0] &= ~0x04;
								 if ((input->data.hid.bRawData[indx * 6 + 1] & 0x40) != 0)
									 inputstate.keys[0] |= 0x10;
								 else
									 inputstate.keys[0] &= ~0x10;
								 if ((input->data.hid.bRawData[indx * 6 + 1] & 0x20) != 0)
									 inputstate.keys[0] |= 0x40;
								 else
									 inputstate.keys[0] &= ~0x40;
								 if ((input->data.hid.bRawData[indx * 6 + 1] & 0x10) != 0 || (input->data.hid.bRawData[indx * 6 + 2] & 0x80) != 0) //low ast
									 inputstate.keys[1] |= 0x02;
								 else
									 inputstate.keys[1] &= ~0x02;
								 if ((input->data.hid.bRawData[indx * 6 + 1] & 0x08) != 0)
									 inputstate.keys[1] |= 0x20;
								 else
									 inputstate.keys[1] &= ~0x20;
								 if ((input->data.hid.bRawData[indx * 6 + 1] & 0x04) != 0)
									 inputstate.keys[1] |= 0x80;
								 else
									 inputstate.keys[1] &= ~0x80;
								 if ((input->data.hid.bRawData[indx * 6 + 1] & 0x02) != 0)
									 inputstate.keys[2] |= 0x02;
								 else
									 inputstate.keys[2] &= ~0x02;
								 if ((input->data.hid.bRawData[indx * 6 + 1] & 0x01) != 0)
									 inputstate.keys[2] |= 0x08;
								 else
									 inputstate.keys[2] &= ~0x08;
								 if ((input->data.hid.bRawData[indx * 6 + 2] & 0x40) != 0)
									 inputstate.keys[1] |= 0x10;
								 else
									 inputstate.keys[1] &= ~0x10;
								 if ((input->data.hid.bRawData[indx * 6 + 2] & 0x20) != 0)
									 inputstate.keys[1] |= 0x40;
								 else
									 inputstate.keys[1] &= ~0x40;
								 if ((input->data.hid.bRawData[indx * 6 + 2] & 0x10) != 0) //l
									 inputstate.keys[2] |= 0x01;
								 else
									 inputstate.keys[2] &= ~0x01;
								 if ((input->data.hid.bRawData[indx * 6 + 2] & 0x08) != 0)
									 inputstate.keys[2] |= 0x04;
								 else
									 inputstate.keys[2] &= ~0x04;
								 if ((input->data.hid.bRawData[indx * 6 + 2] & 0x04) != 0)
									 inputstate.keys[2] |= 0x10;
								 else
									 inputstate.keys[2] &= ~0x10;
								 if ((input->data.hid.bRawData[indx * 6 + 2] & 0x01) != 0 || (input->data.hid.bRawData[indx * 6 + 3] & 0x08) != 0) //low s
									 inputstate.keys[0] |= 0x01;
								 else
									 inputstate.keys[0] &= ~0x01;
								 if ((input->data.hid.bRawData[indx * 6 + 3] & 0x04) != 0)
									 inputstate.keys[0] |= 0x02;
								 else
									 inputstate.keys[0] &= ~0x02;
								 if ((input->data.hid.bRawData[indx * 6 + 3] & 0x02) != 0)
									 inputstate.keys[0] |= 0x08;
								 else
									 inputstate.keys[0] &= ~0x08;
								 if ((input->data.hid.bRawData[indx * 6 + 3] & 0x01) != 0)
									 inputstate.keys[0] |= 0x20;
								 else
									 inputstate.keys[0] &= ~0x20;
								 if (input->data.hid.bRawData[indx * 6 + 4] != 0 || (input->data.hid.bRawData[indx * 6 + 3] & 0xD0) != 0)
									 inputstate.keys[2] |= 0x40;
								 else
									 inputstate.keys[2] &= ~0x40;
								 if ((input->data.hid.bRawData[indx * 6 + 5] & 0x20) != 0)
									 inputstate.keys[2] |= 0x20;
								 else
									 inputstate.keys[2] &= ~0x20;
								 if ((input->data.hid.bRawData[indx * 6 + 5] & 0x10) != 0)
									 inputstate.keys[0] |= 0x80;
								 else
									 inputstate.keys[0] &= ~0x80;
								 if ((input->data.hid.bRawData[indx * 6 + 5] & 0x08) != 0)
									 inputstate.keys[1] |= 0x01;
								 else
									 inputstate.keys[1] &= ~0x01;
								 if ((input->data.hid.bRawData[indx * 6 + 5] & 0x02) != 0)
									 inputstate.keys[1] |= 0x04;
								 else
									 inputstate.keys[1] &= ~0x04;
								 if ((input->data.hid.bRawData[indx * 6 + 5] & 0x01) != 0)
									 inputstate.keys[1] |= 0x08;
								 else
									 inputstate.keys[1] &= ~0x08;

								 inputstate.stroke[0] |= inputstate.keys[0];
								 inputstate.stroke[1] |= inputstate.keys[1];
								 inputstate.stroke[2] |= inputstate.keys[2];
								 inputstate.stroke[3] |= inputstate.keys[3];
							 
							 }
						 }

						 __int8 post = inputstate.keys[0] | inputstate.keys[1] | inputstate.keys[2] | inputstate.keys[3];
						 if (pre != 0 && post == 0) {
							 sendstroke(inputstate.stroke);
						 }


						 DefRawInputProc(&input, 1, sizeof(RAWINPUTHEADER));
						 return 0;
					 }
					 else {
						 return DefRawInputProc(&input, 1, sizeof(RAWINPUTHEADER));
					 }
	}
		case WM_NOTIFY:
		{
					  
			LPNMHDR lpnmhdr = (LPNMHDR)lParam;

			if (lpnmhdr->code == TCN_SELCHANGE)
			{
				// get the newly selected tab item
				INT nTabItem = TabCtrl_GetCurSel(controls.hTab);

				// hide and show the appropriate tab view
				// based on which tab item was clicked
				controls.currenttab = nTabItem;
				switch (nTabItem)
				{
					// Tab1 (item 0) was clicked
					case 0:
					{
						ShowWindow(controls.scontainer, SW_HIDE);  // first hide tab view 2
						ShowWindow(controls.mestroke, SW_SHOW); 
						ShowWindow(controls.mesuggest, SW_HIDE);
						return 0;
					}
					break;
					 // Tab2 (item 1) was clicked
					case 1:
					{
						ShowWindow(controls.scontainer, SW_SHOW);  
						ShowWindow(controls.mestroke, SW_HIDE);
						ShowWindow(controls.mesuggest, SW_HIDE);
						return 0;
					}
					break;

					case 2:
					{
							  ShowWindow(controls.scontainer, SW_HIDE);
							  ShowWindow(controls.mestroke, SW_HIDE);
							  ShowWindow(controls.mesuggest, SW_SHOW);
							  return 0;
					}
						break;

					default:
						break;
				}
			}
			return 0;
		}
		break;
		case WM_COPYDATA:
		{
							PCOPYDATASTRUCT pMyCDS = (PCOPYDATASTRUCT)lParam;
							__int32* val = (__int32*)(pMyCDS->lpData);
							union {
								__int32 ival;
								unsigned __int8 sval[4];
							} dcopy;
							dcopy.ival = *val;
							sendstroke(dcopy.sval);
		}
			break;
			/*
		case WM_KEYDOWN:
		{
			unsigned __int8 vk = LOBYTE(LOWORD(wParam));
			__int8 map = settings.map[vk];
			if (map != 0) {
				map = map - 1;
				if (map < 8) {
					inputstate.keys[0] |= 1 << map;
					inputstate.stroke[0] |= inputstate.keys[0];
				}
				else if (map < 16) {
					inputstate.keys[1] |= 1 << (map-8);
					inputstate.stroke[1] |= inputstate.keys[1];
				}
				else if (map < 24) {
					inputstate.keys[2] |= 1 << (map - 16);
					inputstate.stroke[2] |= inputstate.keys[2];
				}
				else if (map < 32) {
					inputstate.keys[3] |= 1 << (map - 24);
					inputstate.stroke[3] |= inputstate.keys[3];
				}
				
				//_stprintf_s(dtxt, 99, TEXT("%i"), settings.map[vk]);
				//MessageBox(NULL, dtxt, TEXT("Title"), MB_OK);
			}

			//HKL locale = GetKeyboardLayout(GetCurrentThreadId());
			//TCHAR tmp[4] = TEXT("XX");
			//WORD rchar;
			//ToAsciiEx(i, 0, NULL, &rchar, 0, locale);
			//tmp[0] = MapVirtualKeyEx(LOWORD(wParam), MAPVK_VK_TO_CHAR, locale);
			//tmp[1] = MapVirtualKeyEx(LOBYTE(VkKeyScanEx(TEXT(';'), locale)), MAPVK_VK_TO_CHAR, locale);
			//MessageBox(NULL, tmp, TEXT("Title"), MB_OK);
			//sw->Write(TEXT("MAP = "));
			//sw->Write(String::Format("{0}->{1}", gcnew System::Char(c), settings.map[i]));
			
		}
			break;
		case WM_KEYUP:
		{
						 __int8 pre = inputstate.keys[0] | inputstate.keys[1] | inputstate.keys[2] | inputstate.keys[3];
						 unsigned __int8 vk = LOBYTE(LOWORD(wParam));
						 __int8 map = settings.map[vk];
						 if (map != 0) {
							 map = map - 1;
							 if (map < 8) {
								 inputstate.keys[0] &= ~(1 << map);
							 }
							 else if (map < 16) {
								 inputstate.keys[1] &= ~(1 << (map - 8));
							 }
							 else if (map < 24) {
								 inputstate.keys[2] &= ~(1 << (map - 16));
							 }
							 else if (map < 32) {
								 inputstate.keys[3] &= ~(1 << (map - 24));
							 }
							 __int8 post = inputstate.keys[0] | inputstate.keys[1] | inputstate.keys[2] | inputstate.keys[3];
							 if (pre != 0 && post == 0) {
								 sendstroke(inputstate.stroke);
							 }
						 }
						

		}
			break;
			*/

		case WM_SIZING:
			if (controls.width != 0) {
				((LPRECT)lParam)->right = ((LPRECT)lParam)->left + controls.width;
			}

			return TRUE;

		case WM_COMMAND:
			wmId = LOWORD(wParam);
			wmEvent = HIWORD(wParam);
			// Parse the menu selections:
			
			return DefWindowProc(hWnd, message, wParam, lParam);
			break;
		case WM_MOVE:
			RECT rt;
			GetWindowRect(hWnd, &rt);
			settings.xpos = rt.left;
			settings.ypos = rt.top;
			break;
		case WM_SIZE:
			GetClientRect(hWnd, &rt);
			SetWindowPos(controls.hTab, NULL, 0, 0, rt.right, rt.bottom, SWP_NOMOVE);
			TabCtrl_AdjustRect(controls.hTab, FALSE, &rt);
			SetWindowPos(controls.scontainer, NULL, rt.left, rt.top, rt.right - rt.left, rt.bottom - rt.top, 0);
			SetWindowPos(controls.mestroke, NULL, rt.left, rt.top, rt.right - rt.left, rt.bottom - rt.top, 0);
			if (controls.lineheight > 0) {
				controls.numlines = (rt.bottom - rt.top) / controls.lineheight;
				int lines = SendMessage(controls.mestroke, EM_GETLINECOUNT, 0, 0);
				if (lines > controls.numlines) {
					int end = SendMessage(controls.mestroke, EM_LINEINDEX, lines - controls.numlines, 0);
					SendMessage(controls.mestroke, EM_SETSEL, 0, end);
					SendMessage(controls.mestroke, EM_REPLACESEL, FALSE, (LPARAM)TEXT(""));
				}
			}
			SetWindowPos(controls.sscroll, NULL, rt.right - rt.left - 15, 0, 15, rt.bottom - rt.top, SWP_NOZORDER);

			if (rt.bottom - rt.top < CONTROL_HEIGHT) {
				ShowWindow(controls.sscroll, SW_SHOW);
				SCROLLINFO s;
				s.cbSize = sizeof(SCROLLINFO);
				s.nMin = 0;
				s.nMax = CONTROL_HEIGHT - 1;
				s.nPage = rt.bottom - rt.top;
				s.fMask = SIF_PAGE | SIF_RANGE;
				SetScrollInfo(controls.sscroll, SB_CTL, &s, TRUE);
			}
			else {
				
				movecontrols(0);
				ShowWindow(controls.sscroll, SW_HIDE);
			}
			GetWindowRect(hWnd, &rt);
			settings.height = rt.bottom - rt.top;
			break;
		case WM_PAINT:
			hdc = BeginPaint(hWnd, &ps);
			// TODO: Add any drawing code here...
			EndPaint(hWnd, &ps);
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	return 0;
}


LRESULT CALLBACK PassBack(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	switch (message) {
	case WM_KEYDOWN:
	case WM_KEYUP:
		WndProc(controls.main, message, wParam, lParam);
		break;
	default:
		break;
	}
	return DefSubclassProc(hWnd, message, wParam, lParam);
}

LRESULT CALLBACK TabProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	switch (message) {
	case WM_CTLCOLORSTATIC:
		if ((HWND)lParam == controls.scontainer || (HWND)lParam == controls.mestroke || (HWND)lParam == controls.mesuggest) {
			  return (LRESULT)GetStockObject(HOLLOW_BRUSH);
			  }
		  else {
			  return DefSubclassProc(hWnd, message, wParam, lParam);
			}
	
	default:
		return DefSubclassProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK StaticProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	int wmId, wmEvent;

	int yPos = 0;
	SCROLLINFO si;
	switch (message) {
	case WM_VSCROLL:
		// Get all the vertial scroll bar information.
		si.cbSize = sizeof (si);
		si.fMask = SIF_ALL;
		GetScrollInfo(controls.sscroll, SB_CTL, &si);

		// Save the position for comparison later on.
		yPos = si.nPos;
		switch (LOWORD(wParam))
		{

			// User clicked the HOME keyboard key.
		case SB_TOP:
			si.nPos = si.nMin;
			break;

			// User clicked the END keyboard key.
		case SB_BOTTOM:
			si.nPos = si.nMax;
			break;

			// User clicked the top arrow.
		case SB_LINEUP:
			si.nPos -= 1;
			break;

			// User clicked the bottom arrow.
		case SB_LINEDOWN:
			si.nPos += 1;
			break;

			// User clicked the scroll bar shaft above the scroll box.
		case SB_PAGEUP:
			si.nPos -= si.nPage;
			break;

			// User clicked the scroll bar shaft below the scroll box.
		case SB_PAGEDOWN:
			si.nPos += si.nPage;
			break;

			// User dragged the scroll box.
		case SB_THUMBTRACK:
			si.nPos = si.nTrackPos;
			break;

		default:
			break;
		}

		// Set the position and then retrieve it.  Due to adjustments
		// by Windows it may not be the same as the value set.
		si.fMask = SIF_POS;
		SetScrollInfo(controls.sscroll, SB_CTL, &si, TRUE);
		GetScrollInfo(controls.sscroll, SB_CTL, &si);

		movecontrols(si.nPos);
		return 0;
	case WM_COMMAND:
		if (controls.inited == FALSE)
		{
			return DefSubclassProc(hWnd, message, wParam, lParam);
		}
		wmId = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		// Parse the menu selections:
		switch (wmEvent)
		{
		case EN_CHANGE:
		{
						 
						  HWND b = (HWND)lParam;
						  
		}
		case CBN_SELCHANGE:
		{
						
								  HWND b = (HWND)lParam;
								  if (b == controls.inputs) {
									  int ItemIndex = SendMessage((HWND)lParam, (UINT)CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
									  settings.mode = ItemIndex;
									  setMode(settings.mode);
								  }
								  else if (b == controls.dicts) {
									  int ItemIndex = SendMessage((HWND)lParam, (UINT)CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
									  TCHAR  ListItem[256];
									  (TCHAR)SendMessage((HWND)lParam, (UINT)CB_GETLBTEXT, (WPARAM)ItemIndex, (LPARAM)ListItem);
									  std::string item("");
									  for (int i = 0; ListItem[i] != 0; i++) {
										  item += ListItem[i];
									  }
									  std::list<std::tuple<std::string, dictionary*>>::iterator di = sharedData.dicts.begin();
									  while (di != sharedData.dicts.cend()) {
										  if (std::get<0, std::string, dictionary*>(*di).compare(item) == 0) {
											 settings.dict = item;
											 sharedData.currentd = std::get<1, std::string, dictionary*>(*di);
										  }
										  di++;
									  }
								  }
		}
			break;
		case BN_CLICKED:
		{

						   HWND b = (HWND)lParam;
						   if (b == controls.cktrans) {
							   if (Button_GetCheck(b) == BST_CHECKED) {
								   SetLayeredWindowAttributes(controls.main, NULL, 175, LWA_ALPHA);
								   settings.trans = TRUE;
							   }
							   else {
								   SetLayeredWindowAttributes(controls.main, NULL, 255, LWA_ALPHA);
								   settings.trans = FALSE;
							   }

						   }
						   else if (b == controls.cktop){
							   if (Button_GetCheck(b) == BST_CHECKED) {
								   SetWindowPos(controls.main, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
								   settings.top = TRUE;
							   }
							   else {
								   SetWindowPos(controls.main, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
								   settings.top = FALSE;
							   }
						   }
						   else if (b == controls.ckmistrans){
							   if (Button_GetCheck(b) == BST_CHECKED) {
								   settings.mistrans = TRUE;
							   }
							   else {
								   settings.mistrans = FALSE;
							   }
						   }
						   else if (b == controls.ckpref){
							   if (Button_GetCheck(b) == BST_CHECKED) {
								   settings.prefix = TRUE;
							   }
							   else {
								   settings.prefix = FALSE;
							   }
						   }

						   else if (b == controls.rdfront) {
							   settings.space = 0;
						   }
						   else if (b == controls.rdback) {
							   settings.space = 1;
						   }
						   else if (b == controls.rdnone) {
							   settings.space = 2;
						   }
						   else if (b == controls.bdict) {
							   LaunchViewDlg(hInst, sharedData.currentd);
						   }
		}

			break;
		default:
			return DefSubclassProc(hWnd, message, wParam, lParam);

		}
		break;
	default:
		return DefSubclassProc(hWnd, message, wParam, lParam);
	}
	return 0;
}


HWND CreateTabControl(HWND main)
{
	TCITEM tie = { 0 };  // tab item structure
	
	RECT rt;
	GetClientRect(main, &rt);

	/* create tab control */
	HWND hTab = CreateWindowEx(
		0,                      // extended style
		WC_TABCONTROL,          // tab control constant
		L"",                     // text/caption
		WS_CHILD | WS_VISIBLE,  // is a child control, and visible
		0,                      // X position - device units from left
		0,                      // Y position - device units from top
		rt.right,                    // Width - in device units
		rt.bottom,                    // Height - in device units
		main,					 // parent window
		NULL,                   // no menu
		hInst,                 // instance
		NULL                    // no extra junk
		);


	// set tab control's font
	SendMessage(hTab, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), (LPARAM)true);


	tie.mask = TCIF_TEXT; 
	tie.pszText = TEXT("Strokes");  
	TabCtrl_InsertItem(hTab, 0, &tie);

	tie.pszText = TEXT("Settings");
	TabCtrl_InsertItem(hTab, 1, &tie);
	
	tie.pszText = TEXT("Dictionary");
	TabCtrl_InsertItem(hTab, 2, &tie);

	controls.currenttab = 0;

	return hTab;
}