#include "stdafx.h"
#include <initguid.h>
#include <windows.h>
#include <Winuser.h>
#include <psapi.h>
#include <Audioclient.h>
#include <Mmdeviceapi.h>
#include <Audiopolicy.h>
#include <Shellapi.h>
#include <Strsafe.h>
#include <Commctrl.h>
#include <iostream>

#include "resource.h"


#define SAFE_RELEASE(x) if (x) {x->Release(); x=NULL;}

const int HkId_PlayPause = 0x01;
const int HkId_VolumUp = 0x02;
const int HkId_VolumDown = 0x03;
const int HkId_NextSong = 0x04;
const int HkId_PreviousSong = 0x05;
UINT WM_SYSTRAY_CLICK = 0;
LPCWSTR lpszClass = L"__hidden__";
TCHAR SpotifyProcessName[MAX_PATH] = TEXT("Spotify.exe");
ISimpleAudioVolume* g_pSimpleAudioControl = nullptr;


//template <class T> void SafeRelease(T **ppT)
//{
//	if (*ppT)
//	{
//		(*ppT)->Release();
//		*ppT = NULL;
//	}
//}

void ExitError(int code, LPCTSTR message)
{
	MessageBox(nullptr, message, L"SpotifyHotKeys - Error", MB_OK | MB_ICONERROR);
	PostQuitMessage(code);
}

void SendKey(WORD vKey)
{
	INPUT inputs;
	inputs.type = INPUT_KEYBOARD;
	inputs.ki.wVk = vKey;
	inputs.ki.wScan = 0x45;
	inputs.ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
	inputs.ki.time = 0;

	SendInput(1, &inputs, sizeof(INPUT));
}

bool IsSpotify(DWORD processID)
{
	TCHAR szProcessName[MAX_PATH] = TEXT("<unknown>");

	// Get a handle to the process.
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processID);

	// Get the process name.
	if (NULL != hProcess)
	{
		HMODULE hMod;
		DWORD cbNeeded;

		if (EnumProcessModules(hProcess, &hMod, sizeof(hMod),
			&cbNeeded))
		{
			GetModuleBaseName(hProcess, hMod, szProcessName,
				sizeof(szProcessName) / sizeof(TCHAR));
		}
	}

	CloseHandle(hProcess);
	return _tcscmp(szProcessName, SpotifyProcessName) == 0;
}

HRESULT GetSpotifyAudioVolume(ISimpleAudioVolume** ppVolumeControl)
{
	HRESULT hr = S_OK;

	DWORD aProcesses[1024], cbNeeded, cProcesses;
	unsigned int i;

	if (!EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded))
	{
		return 1; // ERROR
	}

	cProcesses = cbNeeded / sizeof(DWORD);
	for (i = 0; i < cProcesses; i++)
	{
		if (aProcesses[i] != 0 && IsSpotify(aProcesses[i]))
		{
			IMMDevice* pDevice = NULL;
			IMMDeviceEnumerator* pEnumerator = NULL;

			CoInitialize(NULL);

			// get the device enumerator
			hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (LPVOID *)&pEnumerator);
			if (hr != S_OK) return hr;

			// get default audio endpoint
			hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
			if (hr != S_OK) return hr;

			/////////////////////////////////////////////
			IAudioSessionManager2* pSessionManager = NULL;
			hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&pSessionManager);
			if (hr != S_OK) return hr;

			IAudioSessionEnumerator* pSessionEnumerator = NULL;
			hr = pSessionManager->GetSessionEnumerator(&pSessionEnumerator);
			if (hr != S_OK) return hr;
			int count;
			hr = pSessionEnumerator->GetCount(&count);
			if (hr != S_OK) return hr;

			// search for audio session with correct name
			// NOTE: we could also use the process id instead of the app name (with IAudioSessionControl2)
			for (int q = 0; q < count; q++)
			{
				IAudioSessionControl* ctl = NULL;
				hr = pSessionEnumerator->GetSession(q, &ctl);
				if (hr != S_OK) return hr;

				IAudioSessionControl2* ctl2 = NULL;
				hr = ctl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&ctl2);
				if (hr != S_OK) return hr;


				DWORD cpid;
				hr = ctl2->GetProcessId(&cpid);
				if (hr != S_OK) continue;

				if (cpid == aProcesses[i])
				{
					LPWSTR sessionInstanceIdentifier;
					ctl2->GetSessionIdentifier(&sessionInstanceIdentifier);
					
					pSessionManager->GetSimpleAudioVolume(NULL, true, &(*ppVolumeControl));
					ctl2->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&*(ppVolumeControl));
					(*ppVolumeControl)->AddRef();
				}

				// TODO: cleanup

				if (*ppVolumeControl != NULL)
				{
					return S_OK;
				}
			}
		}
	}

	return NULL;
}

ISimpleAudioVolume* GetSimpleAudioControlCached()
{
	if (g_pSimpleAudioControl == nullptr)
	{
		auto hr = GetSpotifyAudioVolume(&g_pSimpleAudioControl);
		if (hr != S_OK)
		{
			ExitError(1000, L"Failed to find volume mixer for Spotify. Try restarting Spotify.");
		}
	}

	return g_pSimpleAudioControl;
}

void VolumeUp(float delta)
{
	UUID* f = new UUID();
	UuidCreateNil(f);
	for (auto s = 0; s < 10; s++)
	{
		float vol;
		GetSimpleAudioControlCached()->GetMasterVolume(&vol);
		vol += delta / 100.0f;
		GetSimpleAudioControlCached()->SetMasterVolume(vol, NULL);
	}

}

LRESULT CALLBACK WndProc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	static NOTIFYICONDATA notifyIconData;

	// systray icon clicked
	if (iMsg == WM_SYSTRAY_CLICK && lParam == WM_LBUTTONUP)
	{
		PostQuitMessage(0);
	}

	// window created
	else if (iMsg == WM_CREATE)
	{
		// setup systray icon
		HICON iconHandle = static_cast<HICON>(LoadImage(GetModuleHandle(nullptr),MAKEINTRESOURCE(IDI_ICON1),IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR));
		if (iconHandle == NULL)
		{
			ExitError(2000, L"Failed to load icon, exiting.");
		}

		WM_SYSTRAY_CLICK = RegisterWindowMessageA("SpotifyHotkeysSystrayClick");
		if (WM_SYSTRAY_CLICK == 0)
		{
			ExitError(3000, L"Failed to register Windows message, exiting.");
		}

		std::memset(&notifyIconData, 0, sizeof(notifyIconData));
		notifyIconData.cbSize = sizeof(notifyIconData);
		notifyIconData.hWnd = hWnd;
		notifyIconData.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
		notifyIconData.uVersion = NOTIFYICON_VERSION;
		notifyIconData.uCallbackMessage = WM_SYSTRAY_CLICK;
		notifyIconData.uID = 1;
		notifyIconData.hIcon = iconHandle;
		StringCchCopy(notifyIconData.szTip, ARRAYSIZE(notifyIconData.szTip), L"Spotify HotKeys");

		bool t = Shell_NotifyIcon(NIM_ADD, &notifyIconData);
		if (!t)
		{
			Shell_NotifyIcon(NIM_DELETE, &notifyIconData);
			ExitError(4000, L"Failed to add SysTray-icon, exiting.");
		}

		DestroyIcon(iconHandle); // Shell_NotifyIcon makes a copy
		return 0;
	}

	// exiting
	else if (iMsg == WM_DESTROY)
	{
		Shell_NotifyIcon(NIM_DELETE, &notifyIconData);
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hWnd, iMsg, wParam, lParam);
}


int main()
{
	HINSTANCE hInstance = GetModuleHandle(nullptr);
	GetSimpleAudioControlCached();

	// create window - and hide it
	WNDCLASS wc;
	HWND hWnd;

	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hbrBackground = nullptr;
	wc.hCursor = nullptr;
	wc.hIcon = nullptr;
	wc.hInstance = hInstance;
	wc.lpfnWndProc = WndProc;
	wc.lpszClassName = lpszClass;
	wc.lpszMenuName = nullptr;
	wc.style = 0;
	RegisterClass(&wc);

	hWnd = CreateWindow(lpszClass, lpszClass, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		nullptr, nullptr, hInstance, nullptr);

	// register hotkeys
	RegisterHotKey(nullptr, HkId_PlayPause,		MOD_NOREPEAT | MOD_ALT | MOD_CONTROL,	VK_HOME);
	RegisterHotKey(nullptr, HkId_VolumUp,					   MOD_ALT | MOD_CONTROL,	VK_UP);
	RegisterHotKey(nullptr, HkId_VolumDown,					   MOD_ALT | MOD_CONTROL,	VK_DOWN);
	RegisterHotKey(nullptr, HkId_NextSong,		MOD_NOREPEAT | MOD_ALT | MOD_CONTROL,	VK_RIGHT);
	RegisterHotKey(nullptr, HkId_PreviousSong,	MOD_NOREPEAT | MOD_ALT | MOD_CONTROL,	VK_LEFT);

	// hide console window
	ShowWindow(GetConsoleWindow(), SW_HIDE);

	// handle window messages
	MSG msg = { 0 };
	while(GetMessage(&msg, nullptr, 0, 0) != 0)
	{ 
		// check for hotkeys
		if (msg.message == WM_HOTKEY)
		{
			switch (msg.wParam)
			{
			case HkId_PlayPause:
				SendKey(0xB3);
				break;
			case HkId_VolumUp:
				VolumeUp(0.1f);
				break;
			case HkId_VolumDown:
				VolumeUp(-0.1f);
				break;
			case HkId_NextSong:
				SendKey(0xB0);
				break;
			case HkId_PreviousSong:
				SendKey(0xB1);
				break;
			}
		}
		else
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return static_cast<int>(msg.wParam);
}

