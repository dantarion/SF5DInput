/*
@dantarion - Source is a bit messy, but it works! (for me)
*/
#include <windows.h>
#include <stdio.h>
#include "XInput.h"
#pragma comment(lib, "XInput.lib")
#include <dinput.h>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#include <cstdio>
LPDIRECTINPUT8 di;
HRESULT hr;


/* Wrapper DLL stuff...not important */
#pragma region Wrapper DLL Stuff
HINSTANCE mHinst = 0, mHinstDLL = 0;
extern "C" UINT_PTR mProcs[12] = {0};
int setupDInput();
LPCSTR mImportNames[] = {"DllMain", "XInputEnable", "XInputGetBatteryInformation", "XInputGetCapabilities", "XInputGetDSoundAudioDeviceGuids", "XInputGetKeystroke", "XInputGetState", "XInputSetState", (LPCSTR)100, (LPCSTR)101, (LPCSTR)102, (LPCSTR)103};
BOOL WINAPI DllMain( HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved ) {
	mHinst = hinstDLL;
	if ( fdwReason == DLL_PROCESS_ATTACH ) {
		mHinstDLL = LoadLibrary( "ori_xinput1_3.dll" );
		if ( !mHinstDLL )
			return ( FALSE );
		for ( int i = 0; i < 12; i++ )
			mProcs[ i ] = (UINT_PTR)GetProcAddress( mHinstDLL, mImportNames[ i ] );
	} else if ( fdwReason == DLL_PROCESS_DETACH ) {
		FreeLibrary( mHinstDLL );
	}
	return ( TRUE );
}
extern "C" void DllMain_wrapper();
extern "C" void XInputEnable_wrapper();
extern "C" void XInputGetBatteryInformation_wrapper();
extern "C" void XInputGetCapabilities_wrapper();
extern "C" void XInputGetDSoundAudioDeviceGuids_wrapper();
extern "C" void XInputGetKeystroke_wrapper();
extern "C" void XInputGetState_wrapper();
extern "C" void XInputSetState_wrapper();
extern "C" void ExportByOrdinal100();
extern "C" void ExportByOrdinal101();
extern "C" void ExportByOrdinal102();
extern "C" void ExportByOrdinal103();

#pragma endregion
/* GetStateWrapper, Super Important */
#include <wbemidl.h>
#include <oleauto.h>
#define SAFE_RELEASE(p) { if ( (p) ) { (p)->Release(); (p) = 0; } }
#define SAFE_DELETE(a) if( (a) != NULL ) delete (a); (a) = NULL;
/*
The following code checks if a device is an XInput device. We don't need to let these devices be visible through DirectInput, because they already work.
*/
//-----------------------------------------------------------------------------
// Enum each PNP device using WMI and check each device ID to see if it contains 
// "IG_" (ex. "VID_045E&PID_028E&IG_00").  If it does, then it's an XInput device
// Unfortunately this information can not be found by just using DirectInput 
//-----------------------------------------------------------------------------
BOOL IsXInputDevice(const GUID* pGuidProductFromDirectInput)
{
	IWbemLocator*           pIWbemLocator = NULL;
	IEnumWbemClassObject*   pEnumDevices = NULL;
	IWbemClassObject*       pDevices[20] = { 0 };
	IWbemServices*          pIWbemServices = NULL;
	BSTR                    bstrNamespace = NULL;
	BSTR                    bstrDeviceID = NULL;
	BSTR                    bstrClassName = NULL;
	DWORD                   uReturned = 0;
	bool                    bIsXinputDevice = false;
	UINT                    iDevice = 0;
	VARIANT                 var;
	HRESULT                 hr;

	// CoInit if needed
	hr = CoInitialize(NULL);
	bool bCleanupCOM = SUCCEEDED(hr);

	// Create WMI
	hr = CoCreateInstance(__uuidof(WbemLocator),
		NULL,
		CLSCTX_INPROC_SERVER,
		__uuidof(IWbemLocator),
		(LPVOID*)&pIWbemLocator);
	if (FAILED(hr) || pIWbemLocator == NULL)
		goto LCleanup;

	bstrNamespace = SysAllocString(L"\\\\.\\root\\cimv2"); if (bstrNamespace == NULL) goto LCleanup;
	bstrClassName = SysAllocString(L"Win32_PNPEntity");   if (bstrClassName == NULL) goto LCleanup;
	bstrDeviceID = SysAllocString(L"DeviceID");          if (bstrDeviceID == NULL)  goto LCleanup;

	// Connect to WMI 
	hr = pIWbemLocator->ConnectServer(bstrNamespace, NULL, NULL, 0L,
		0L, NULL, NULL, &pIWbemServices);
	if (FAILED(hr) || pIWbemServices == NULL)
		goto LCleanup;

	// Switch security level to IMPERSONATE. 
	CoSetProxyBlanket(pIWbemServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
		RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

	hr = pIWbemServices->CreateInstanceEnum(bstrClassName, 0, NULL, &pEnumDevices);
	if (FAILED(hr) || pEnumDevices == NULL)
		goto LCleanup;

	// Loop over all devices
	for (;; )
	{
		// Get 20 at a time
		hr = pEnumDevices->Next(10000, 20, pDevices, &uReturned);
		if (FAILED(hr))
			goto LCleanup;
		if (uReturned == 0)
			break;

		for (iDevice = 0; iDevice<uReturned; iDevice++)
		{
			// For each device, get its device ID
			hr = pDevices[iDevice]->Get(bstrDeviceID, 0L, &var, NULL, NULL);
			if (SUCCEEDED(hr) && var.vt == VT_BSTR && var.bstrVal != NULL)
			{
				// Check if the device ID contains "IG_".  If it does, then it's an XInput device
				// This information can not be found from DirectInput 
				if (wcsstr(var.bstrVal, L"IG_"))
				{
					// If it does, then get the VID/PID from var.bstrVal
					DWORD dwPid = 0, dwVid = 0;
					WCHAR* strVid = wcsstr(var.bstrVal, L"VID_");
					if (strVid && swscanf(strVid, L"VID_%4X", &dwVid) != 1)
						dwVid = 0;
					WCHAR* strPid = wcsstr(var.bstrVal, L"PID_");
					if (strPid && swscanf(strPid, L"PID_%4X", &dwPid) != 1)
						dwPid = 0;

					// Compare the VID/PID to the DInput device
					DWORD dwVidPid = MAKELONG(dwVid, dwPid);
					if (dwVidPid == pGuidProductFromDirectInput->Data1)
					{
						bIsXinputDevice = true;
						goto LCleanup;
					}
				}
			}
			SAFE_RELEASE(pDevices[iDevice]);
		}
	}

LCleanup:
	if (bstrNamespace)
		SysFreeString(bstrNamespace);
	if (bstrDeviceID)
		SysFreeString(bstrDeviceID);
	if (bstrClassName)
		SysFreeString(bstrClassName);
	for (iDevice = 0; iDevice<20; iDevice++)
		SAFE_RELEASE(pDevices[iDevice]);
	SAFE_RELEASE(pEnumDevices);
	SAFE_RELEASE(pIWbemLocator);
	SAFE_RELEASE(pIWbemServices);

	if (bCleanupCOM)
		CoUninitialize();

	return bIsXinputDevice;
}
//TODO change these to arrays to support more than one DirectInput controller
LPDIRECTINPUTDEVICE8 joystick;
DIJOYSTATE2 js;

BOOL CALLBACK enumCallback(const DIDEVICEINSTANCE* instance, VOID* context)
{
	HRESULT hr;
	if (IsXInputDevice(&instance->guidProduct))//Don't mess with XInput devices
		return DIENUM_CONTINUE;
	// Obtain an interface to the enumerated joystick.
	hr = di->CreateDevice(instance->guidInstance, &joystick, NULL);

	// If it failed, then we can't use this joystick. (Maybe the user unplugged
	// it while we were in the middle of enumerating it.)
	if (FAILED(hr)) {
		return DIENUM_CONTINUE;
	}

	// Stop enumeration. Note: we're just taking the first joystick we get. You
	// could store all the enumerated joysticks and let the user pick.
	//TODO multiple joystick support..i.e. enumerate through all
	return DIENUM_STOP;
}

HRESULT
poll(DIJOYSTATE2 *js)
{
	HRESULT     hr;

	if (joystick == NULL) {
		return S_OK;
	}


	// Poll the device to read the current state
	hr = joystick->Poll();
	if (FAILED(hr)) {
		// DInput is telling us that the input stream has been
		// interrupted. We aren't tracking any state between polls, so
		// we don't have any special reset that needs to be done. We
		// just re-acquire and try again.
		hr = joystick->Acquire();
		while (hr == DIERR_INPUTLOST) {
			hr = joystick->Acquire();
		}

		// If we encounter a fatal error, return failure.
		if ((hr == DIERR_INVALIDPARAM) || (hr == DIERR_NOTINITIALIZED)) {
			return E_FAIL;
		}

		// If another application has control of this device, return successfully.
		// We'll just have to wait our turn to use the joystick.
		if (hr == DIERR_OTHERAPPHASPRIO) {
			return S_OK;
		}
	}

	// Get the input's device state
	if (FAILED(hr = joystick->GetDeviceState(sizeof(DIJOYSTATE2), js))) {
		return hr; // The device should have been acquired during the Poll()
	}

	return S_OK;
}
int setupDInput()
{
	// Create a DirectInput device
	if(hr == NULL)
		if (FAILED(hr = DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION,
			IID_IDirectInput8, (VOID**)&di, NULL))) {
			return (FALSE);
		}
	// Look for the first simple joystick we can find.
	if (FAILED(hr = di->EnumDevices(DI8DEVCLASS_GAMECTRL, enumCallback,
		NULL, DIEDFL_ATTACHEDONLY))) {
		printf("Joystick not found.\n");
	}

	// Make sure we got a joystick
	if (joystick == NULL) {
		printf("Joystick not found.\n");
		return FALSE;
	}
	DIDEVCAPS capabilities;

	// Set the data format to "simple joystick" - a predefined data format 
	//
	// A data format specifies which controls on a device we are interested in,
	// and how they should be reported. This tells DInput that we will be
	// passing a DIJOYSTATE2 structure to IDirectInputDevice::GetDeviceState().
	if (FAILED(hr = joystick->SetDataFormat(&c_dfDIJoystick2))) {
		printf("Format is fucked.\n");
		return hr;
	}

	// Set the cooperative level to let DInput know how this device should
	// interact with the system and with other DInput applications.
	if (FAILED(hr = joystick->SetCooperativeLevel(NULL, DISCL_EXCLUSIVE |
		DISCL_FOREGROUND))) {
		printf("Coop is fucked\n");
		return hr;
	}

	// Determine how many axis the joystick has (so we don't error out setting
	// properties for unavailable axis)
	capabilities.dwSize = sizeof(DIDEVCAPS);
	if (FAILED(hr = joystick->GetCapabilities(&capabilities))) {
		printf("Cap is fucked\n");
		return hr;
	}
}
XINPUT_STATE STATES[2];
int binding = 0;
DWORD WINAPI hooked_XInputGetState(DWORD dwUserIndex, XINPUT_STATE *pState)
{
	if(di == NULL)
		setupDInput();
	poll(&js);

	//Check for binding change...
	//If DirectInput HOME + DPAD LEFT
	if ((js.rgdwPOV[0] == 5 * 4500 || js.rgdwPOV[0] == 6 * 4500 || js.rgdwPOV[0] == 7 * 4500) && js.rgbButtons[12])
		binding = 0;
	//If DirectInput HOME + DPAD RIGHT
	if ((js.rgdwPOV[0] == 1 * 4500 || js.rgdwPOV[0] == 2 * 4500 || js.rgdwPOV[0] == 3 * 4500) && js.rgbButtons[12])
		binding = 1;

	int ret = XInputGetState(dwUserIndex, pState);
	//If the DirectInput Controller is not bound to this slot...
	if (binding != dwUserIndex)
		return ERROR_SUCCESS;//Return normally

	//If the DirectInput Controller is bound to this slot, inject button inputs
	if (js.rgbButtons[0])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_X;
	if (js.rgbButtons[3])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_Y;
	if (js.rgbButtons[1])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_A;
	if (js.rgbButtons[2])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_B;
	if (js.rgbButtons[5])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
	if (js.rgbButtons[7])
		pState->Gamepad.bRightTrigger = 255;
	if (js.rgbButtons[4])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
	if (js.rgbButtons[6])
		pState->Gamepad.bLeftTrigger = 255;
	if (js.rgbButtons[9])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
	if (js.rgbButtons[11])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
	if (js.rgdwPOV[0] == 0 * 4500 || js.rgdwPOV[0] == 1 * 4500 || js.rgdwPOV[0] == 7 * 4500)
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
	if (js.rgdwPOV[0] == 1 * 4500 || js.rgdwPOV[0] == 2 * 4500 || js.rgdwPOV[0] == 3 * 4500)
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
	if (js.rgdwPOV[0] == 3 * 4500 || js.rgdwPOV[0] == 4 * 4500 || js.rgdwPOV[0] == 5 * 4500)
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
	if (js.rgdwPOV[0] == 5 * 4500 || js.rgdwPOV[0] == 6 * 4500 || js.rgdwPOV[0] == 7 * 4500)
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
	//TODO ANALOG JOYSTICK

	//Controller is connected!
	return ERROR_SUCCESS;
}

