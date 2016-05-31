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
#include <unordered_map>
#include <fstream>

// Structure change as documented here https://github.com/mumble-voip/mumble/issues/2019
typedef struct
{
	WORD wButtons;
	BYTE bLeftTrigger;
	BYTE bRightTrigger;
	SHORT sThumbLX;
	SHORT sThumbLY;
	SHORT sThumbRX;
	SHORT sThumbRY;
	DWORD dwPaddingReserved; // Adding padding to avoid crashes when using the ExportByOrdinal100
} XINPUT_GAMEPAD_EX;

typedef struct
{
	DWORD dwPacketNumber;
	XINPUT_GAMEPAD_EX Gamepad;
} XINPUT_STATE_EX;

// Hashing structure for GUID
namespace std
{
	template<> struct hash<GUID> : public std::_Bitwise_hash<GUID>{};
}
// DI stuff
LPDIRECTINPUT8 di;
BOOL diAvailable = true;

// Serves as cache
std::unordered_map<GUID, BOOL> isXInput;

// Active DI-only devices only
std::unordered_map<GUID, LPDIRECTINPUTDEVICE8> joysticks;
std::unordered_map<GUID, DIJOYSTATE2> joystickStates;

// All XInput states and wether it is plugged in or not
XINPUT_STATE_EX xinputStates[4];
bool xinputReady[4];

// Defines which device is connected to which port
struct VirtualControllerMapping
{
	bool free = true;
	// if xinput < 0, then guid is used
	short xinput = -1;
	GUID guid;
};
// The mapping table
VirtualControllerMapping virtualControllers[2];

// Internal timer to launch the detection of DI devices
int timer = 0;

/* Wrapper DLL stuff...not important */
#pragma region Wrapper DLL Stuff
HINSTANCE mHinst = 0, mHinstDLL = 0;
extern "C" UINT_PTR mProcs[12] = {0};
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
		// Releasing resources to avoid crashes
		for (auto it : joysticks) {
			if (it.second) {
				it.second->Unacquire();
				it.second->Release();
			}
		}
		joysticks.clear();
		if (di) {
			di->Release();
		}
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
extern "C" DWORD ExportByOrdinal100(_In_  DWORD dwUserIndex, _Out_ XINPUT_STATE_EX *pState);
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
BOOL isXInputDevice(const GUID* pGuidProductFromDirectInput)
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

BOOL CALLBACK enumCallback(const DIDEVICEINSTANCE* instance, VOID* context)
{
	HRESULT hr;
	BOOL xinput = false;

	// First check if the device is known
	auto it = isXInput.find(instance->guidInstance);
	if (it == isXInput.end()) {
		// Not found yet, so check if it is an XInputDevice
		xinput = isXInputDevice(&instance->guidProduct);
		isXInput[instance->guidInstance] = xinput;
	} else {
		xinput = it->second;
	}

	if (xinput) {
		// If it is an xinput, we can safely ignore
		return DIENUM_CONTINUE;
	}

	// Check if we have a live DI instance of this joystick
	if (joysticks.find(instance->guidInstance) != joysticks.end()) {
		return DIENUM_CONTINUE;
	}

	// If not we must build it...

	LPDIRECTINPUTDEVICE8 joystick;
	// Obtain an interface to the enumerated joystick.
	hr = di->CreateDevice(instance->guidInstance, &joystick, NULL);
	// If it failed, then we can't use this joystick. (Maybe the user unplugged
	// it while we were in the middle of enumerating it.)
	if (FAILED(hr)) {
		return DIENUM_CONTINUE;
	}

	// Set the data format to "simple joystick" - a predefined data format 
	//
	// A data format specifies which controls on a device we are interested in,
	// and how they should be reported. This tells DInput that we will be
	// passing a DIJOYSTATE2 structure to IDirectInputDevice::GetDeviceState().
	if (FAILED(hr = joystick->SetDataFormat(&c_dfDIJoystick2))) {
		printf("SetDataFormat is fucked %x\n", hr);
		joystick->Release();
		return DIENUM_CONTINUE;
	}

	// Acquire the device
	if (FAILED(hr = joystick->Acquire())) {
		printf("Acquire is fucked %x\n", hr);
		joystick->Release();
		return DIENUM_CONTINUE;
	}

	// Store the joystick instance accessible via guid
	joysticks[instance->guidInstance] = joystick;
	joystickStates[instance->guidInstance];

	// Set the cooperative level to let DInput know how this device should
	// interact with the system and with other DInput applications.
	if (FAILED(hr = joystick->SetCooperativeLevel(NULL, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND))) {
		printf("Coop is fucked %x\n", hr);
		// Not that important actually
	}

	// Get other devices
	return DIENUM_CONTINUE;
}
HRESULT
poll(LPDIRECTINPUTDEVICE8 joystick, LPDIJOYSTATE2 js)
{
	// Device polling (as seen on x360ce)
	HRESULT hr = E_FAIL;

	if (!joystick) return E_FAIL;

	joystick->Poll();
	hr = joystick->GetDeviceState(sizeof(DIJOYSTATE2), js);

	if (FAILED(hr)) {
		// Reacquire the device (only once)
		hr = joystick->Acquire();
	}

	return hr;
}

int setupDInput()
{
	if (!diAvailable) {
		return FALSE;
	}
	// Create a DirectInput8 instance
	if (di == NULL) {
		if (FAILED(DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION,
			IID_IDirectInput8, (VOID**)&di, NULL))) {
			// If it is not available for any reason, avoid getting back in setupDInput
			diAvailable = false;
			return (FALSE);
		}
	}

	// Poll all devices
	if (FAILED(di->EnumDevices(DI8DEVCLASS_GAMECTRL, enumCallback,
		NULL, DIEDFL_ATTACHEDONLY))) {
		return FALSE;
	}
	return TRUE;
}

// From a DI state, check if we want a controller change
// Returns -1 if nothing must change, or the id of the controller
int readDirectInputControllerChange(DIJOYSTATE2* input) {
	//If DirectInput HOME + LPAD RIGHT
	if ((input->rgdwPOV[0] == 5 * 4500 || input->rgdwPOV[0] == 6 * 4500 || input->rgdwPOV[0] == 7 * 4500) && input->rgbButtons[12])
		return 0;
	//If DirectInput HOME + DPAD RIGHT
	if ((input->rgdwPOV[0] == 1 * 4500 || input->rgdwPOV[0] == 2 * 4500 || input->rgdwPOV[0] == 3 * 4500) && input->rgbButtons[12])
		return 1;
	return -1;
}

// From an XINPUT state, check if we want a controller change
// Returns -1 if nothing must change, or the id of the controller
int readXInputControllerChange(XINPUT_STATE_EX* input) {
	// Select guide (or SELECT + START)
	BOOL guideSelected = (input->Gamepad.wButtons & 0x400) || ((input->Gamepad.wButtons & XINPUT_GAMEPAD_START) && (input->Gamepad.wButtons & XINPUT_GAMEPAD_BACK));
	// ... and direction...
	if (guideSelected && (input->Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)) {
		return 0;
	}
	if (guideSelected && (input->Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)) {
		return 1;
	}
	return -1;
}

// Returns true iff the virtual controller contains the given argument
BOOL mappingContains(VirtualControllerMapping* mapping, const GUID* dinputGUID, short xinputIndex) {
	if (mapping->free) {
		return false;
	}
	if (xinputIndex < 0) {
		return memcmp(dinputGUID, &mapping->guid, sizeof(GUID)) == 0;
	}
	return mapping->xinput == xinputIndex;
}


void selectController(int desired, const GUID* dinputGUID, short xinputIndex) {
	if (desired < 0) {
		// Nothing is desired BUT if a place is free then...
		if (virtualControllers[0].free && !mappingContains(&virtualControllers[1], dinputGUID, xinputIndex)) {
			selectController(0, dinputGUID, xinputIndex);
		} else if (virtualControllers[1].free && !mappingContains(&virtualControllers[0], dinputGUID, xinputIndex)) {
			selectController(1, dinputGUID, xinputIndex);
		}
		return;
	}

	VirtualControllerMapping* targetMapping = &virtualControllers[desired];
	if (mappingContains(targetMapping, dinputGUID, xinputIndex)) {
		// We already have the right one on the desired index
		return;
	}
	
	// Selecting the controller now...
	VirtualControllerMapping* otherMapping = &virtualControllers[1-desired];
	if (!targetMapping->free) {
		// If we require a controller that is already taken, swap the two
		*otherMapping = *targetMapping;
	} else {
		// If we switch to a free space
		if (mappingContains(otherMapping, dinputGUID, xinputIndex)) {
			// Release the one if we had before
			otherMapping->free = true;
		}
	}

	// Set the actual mapping
	if (dinputGUID != NULL) {
		targetMapping->guid = *dinputGUID;
	}
	targetMapping->xinput = xinputIndex;
	targetMapping->free = false;
}
// Returns an empty device instead of ERROR_DEVICE_NOT_CONNECTED
// This asks SFV to always poll device 0 and 1
DWORD emptyDevice(XINPUT_STATE* pState) {
	ZeroMemory(pState, sizeof(XINPUT_STATE));
	return ERROR_SUCCESS;
}

// XInputGetState implementation
DWORD WINAPI hooked_XInputGetState(DWORD dwUserIndex, XINPUT_STATE *pState)
{
	// We dont support >= 2
	if (dwUserIndex >= 2) {
		return ERROR_DEVICE_NOT_CONNECTED;
	}

	// Make the reads on 0 then dispatch on the rest
	if (dwUserIndex == 0) {
		// Once every second
		if (timer % 60 == 0) {
			// Refresh device list
			setupDInput();
			timer = 0;
		}
		++timer;

		// Read DI joysticks
		for (auto it = joysticks.begin(); it != joysticks.end(); ) {
			LPDIJOYSTATE2 state = &joystickStates[it->first];
			// Poll the device
			if (FAILED(poll(it->second, state))) {
				// We cant poll the device
				// it must be removed from the list of available joysticks
				it->second->Unacquire();
				it->second->Release();
				joystickStates.erase(it->first);
				it = joysticks.erase(it);
				continue;
			}

			// Check if the device wants to be changed
			int desired = readDirectInputControllerChange(state);
			// Set the virtual controller if necessary
			selectController(desired, &it->first, -1);
			++it;
		}

		// Read XInput
		for (int i = 0; i < 4; i++) {
			// Read XInputGetStateEx to get the GUIDE button (though it seems to be broken on win10 now)
			if (ExportByOrdinal100(i, &xinputStates[i]) != ERROR_SUCCESS) {
				xinputReady[i] = false;
				continue;
			}
			xinputReady[i] = true;
			// Check if the device wants to be changed
			int desired = readXInputControllerChange(&xinputStates[i]);
			// Set the virtual controller if necessary
			selectController(desired, NULL, i);
		}
	}

	// Get the mapping
	VirtualControllerMapping* mapping = &virtualControllers[dwUserIndex];
	
	if (mapping->free) {
		// No device for this one, give the empty device
		return emptyDevice(pState);
	}

	// We have something

	if (mapping->xinput >= 0) {
		// XInput!
		if (!xinputReady[mapping->xinput]) {
			// But it was disconnected...
			mapping->free = true;
			return emptyDevice(pState);
		}
		// Else, just copy the input to the pState
		memcpy(pState, &xinputStates[mapping->xinput], sizeof(XINPUT_STATE));
		return ERROR_SUCCESS;
	}

	// This is a DirectInput device

	if (joysticks.find(mapping->guid) == joysticks.end()) {
		// But it has been destroy
		mapping->free = true;
		return emptyDevice(pState);
	}

	// Do the actual mapping now
	LPDIJOYSTATE2 js = &joystickStates[mapping->guid];
	pState->dwPacketNumber = GetTickCount();

	//If the DirectInput Controller is bound to this slot, inject button inputs
	if (js->rgbButtons[0])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_X;
	if (js->rgbButtons[3])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_Y;
	if (js->rgbButtons[1])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_A;
	if (js->rgbButtons[2])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_B;
	if (js->rgbButtons[5])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
	if (js->rgbButtons[7])
		pState->Gamepad.bRightTrigger = 255;
	if (js->rgbButtons[4])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
	if (js->rgbButtons[6])
		pState->Gamepad.bLeftTrigger = 255;
	if (js->rgbButtons[7])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_START;
	if (js->rgbButtons[8])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_BACK;
	if (js->rgbButtons[9])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
	if (js->rgbButtons[11])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
	if (js->rgdwPOV[0] == 0 * 4500 || js->rgdwPOV[0] == 1 * 4500 || js->rgdwPOV[0] == 7 * 4500)
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
	if (js->rgdwPOV[0] == 1 * 4500 || js->rgdwPOV[0] == 2 * 4500 || js->rgdwPOV[0] == 3 * 4500)
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
	if (js->rgdwPOV[0] == 3 * 4500 || js->rgdwPOV[0] == 4 * 4500 || js->rgdwPOV[0] == 5 * 4500)
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
	if (js->rgdwPOV[0] == 5 * 4500 || js->rgdwPOV[0] == 6 * 4500 || js->rgdwPOV[0] == 7 * 4500)
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
	//TODO ANALOG JOYSTICK

	//Controller is connected!
	return ERROR_SUCCESS;
}

