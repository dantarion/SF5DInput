#include <windows.h>
#include "wrapped.h"
#include "utils.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

#include <unordered_map>
#include <dbt.h>

// Hashing structure for GUID
namespace std
{
	template<> struct hash<GUID> : public std::_Bitwise_hash<GUID>{};
}
// DI stuff
LPDIRECTINPUT8 di;
HWND hWnd;
HDEVNOTIFY hDeviceNotify;
BOOL diAvailable = true;
short mustRefreshDevices = -1;

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

BOOL CALLBACK enumCallback(const DIDEVICEINSTANCE* instance, VOID* context) {
	HRESULT hr;
	BOOL xinput = false;

	if (!instance) {
		return DIENUM_CONTINUE;
	}

	if (instance->wUsagePage != 1 || instance->wUsage != 5) {
		// Not an actual game controller (see issue #3)
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
		// As seen on x360ce, if create device fails on guid instance, try the product
		hr = di->CreateDevice(instance->guidProduct, &joystick, NULL);
		if (FAILED(hr))
			return DIENUM_CONTINUE;
	}

	// First check if the device is known
	auto it = isXInput.find(instance->guidInstance);
	if (it == isXInput.end()) {
		// Not found yet, so check if it is an XInputDevice
		DIPROPGUIDANDPATH  dipdw;  
		dipdw.diph.dwSize = sizeof(DIPROPGUIDANDPATH);
		dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
		dipdw.diph.dwObj = 0; 
		dipdw.diph.dwHow = DIPH_DEVICE;
		hr = joystick->GetProperty(DIPROP_GUIDANDPATH, &dipdw.diph);
		if (FAILED(hr)) {
			printf("Cannot fetch GUID & PATH %x\n", hr);
			joystick->Release();
			return DIENUM_CONTINUE;
		}
		std::wstring asStr(dipdw.wszPath);
		xinput = asStr.find(L"ig_") != std::string::npos;

		isXInput[instance->guidInstance] = xinput;
	} else {
		xinput = it->second;
	}

	if (xinput) {
		joystick->Release();
		// If it is an xinput, we can safely ignore
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

	hr = joystick->SetCooperativeLevel(hWnd, DISCL_EXCLUSIVE | DISCL_BACKGROUND);
	if (FAILED(hr)) {
		hr = joystick->SetCooperativeLevel(hWnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
		if (FAILED(hr)) {
			printf("Cooperative is fucked %x\n", hr);
		}
	}

	DIPROPDWORD dipdw;
	dipdw.diph.dwSize = sizeof(DIPROPDWORD);
	dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	dipdw.diph.dwObj = 0;
	dipdw.diph.dwHow = DIPH_DEVICE;
	dipdw.dwData = DIPROPAUTOCENTER_ON;
	joystick->SetProperty(DIPROP_AUTOCENTER, &dipdw.diph);

	// Acquire the device
	if (FAILED(hr = joystick->Acquire())) {
		printf("Acquire is fucked %x\n", hr);
		joystick->Release();
		return DIENUM_CONTINUE;
	}

	// Store the joystick instance accessible via guid
	joysticks[instance->guidInstance] = joystick;

	// Get other devices
	return DIENUM_CONTINUE;
}
HRESULT poll(LPDIRECTINPUTDEVICE8 joystick, LPDIJOYSTATE2 js) {
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

void refreshDevices() {
	if (!diAvailable || di == NULL) {
		return;
	}
	// Poll all devices
	di->EnumDevices(DI8DEVCLASS_GAMECTRL, enumCallback, NULL, DIEDFL_ATTACHEDONLY);
}
INT_PTR WINAPI messageCallback(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_DEVICECHANGE:
			if (wParam ==  DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
				// Wait for 30 frames, send refresh
				mustRefreshDevices = 30;
			}
			break;

		case WM_CLOSE:
			UnregisterDeviceNotification(hDeviceNotify);
			DestroyWindow(hWnd);

			diAvailable = false;
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
			di = NULL;
			break;

		default:
			// Send all other messages on to the default windows handler.
			return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 1;
}

int setupDInput()
{
	if (!diAvailable) {
		return FALSE;
	}
	// Create a DirectInput8 instance
	if (di != NULL) {
		return TRUE;
	}
	HRESULT hr = DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8, (VOID**)&di, NULL);
	if (FAILED(hr)) {
		// If it is not available for any reason, avoid getting back in setupDInput
		diAvailable = false;
		MessageBox(NULL, GetLastErrorAsString().c_str(), "SF5DInput - Direct Input", MB_ICONERROR);
		exit(hr);
	}

	// DI is ready, now create a message-only window
	WNDCLASSEX wndClass = {};
	wndClass.cbSize = sizeof(WNDCLASSEX);
	wndClass.lpfnWndProc = reinterpret_cast<WNDPROC>(messageCallback);
	GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCTSTR)&wndClass.hInstance, &wndClass.hInstance);
	wndClass.lpszClassName = "SF5DInput";

	if (!RegisterClassEx(&wndClass)) {
		MessageBox(NULL, GetLastErrorAsString().c_str(), "SF5DInput - Registering Window Class", MB_ICONERROR);
		exit(1);
	}

	hWnd = CreateWindowEx(0L, wndClass.lpszClassName, "SF5DInput", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);	
	if (!hWnd) {
		MessageBox(NULL, GetLastErrorAsString().c_str(), "SF5DInput - Create Internal Window", MB_ICONERROR);
		exit(2);
	}

	// Message only window is ready, now we can create a notification filter to register to device notificaitons

	DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;

	ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
	NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
	NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	// This is GUID_DEVINTERFACE_USB_DEVICE, it scans all usb devices
	NotificationFilter.dbcc_classguid = { 0xA5DCBF10L, 0x6530, 0x11D2, { 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED } };

	hDeviceNotify = RegisterDeviceNotification(
		hWnd,                       // events recipient
		&NotificationFilter,        // type of device
		DEVICE_NOTIFY_WINDOW_HANDLE // type of recipient handle
	);

	if (NULL == hDeviceNotify) {
		MessageBox(NULL, GetLastErrorAsString().c_str(), "SF5DInput - Registering Device Notification", MB_ICONERROR);
		exit(3);
	}

	// WOOH we are ready to go!

	// Get all the devices
	refreshDevices();
	return TRUE;
}

// From a DI state, check if we want a controller change
// Returns -1 if nothing must change, or the id of the controller
int readDirectInputControllerChange(DIJOYSTATE2* input) {
	// Home is selected (or SELECT + START)
	BOOL homeSelected = (input->rgbButtons[8] && input->rgbButtons[9]) || input->rgbButtons[12];
	//If DirectInput HOME + LPAD RIGHT
	if ((input->rgdwPOV[0] == 5 * 4500 || input->rgdwPOV[0] == 6 * 4500 || input->rgdwPOV[0] == 7 * 4500) && homeSelected)
		return 0;
	//If DirectInput HOME + DPAD RIGHT
	if ((input->rgdwPOV[0] == 1 * 4500 || input->rgdwPOV[0] == 2 * 4500 || input->rgdwPOV[0] == 3 * 4500) && homeSelected)
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
	if (guideSelected && (input->Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)) {
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

void selectController(int desired, const GUID* dinputGUID, short xinputIndex, BOOL isNew) {
	if (desired < 0) {
		if (!isNew) {
			return;
		}
		// Nothing is desired BUT if a place is free then...
		if (virtualControllers[0].free && !mappingContains(&virtualControllers[1], dinputGUID, xinputIndex)) {
			selectController(0, dinputGUID, xinputIndex, true);
		} else if (virtualControllers[1].free && !mappingContains(&virtualControllers[0], dinputGUID, xinputIndex)) {
			selectController(1, dinputGUID, xinputIndex, true);
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
	if (mappingContains(otherMapping, dinputGUID, xinputIndex)) {
		// If we require a mapping while we were on the other one, swap first
		*otherMapping = *targetMapping;
	}

	// Set the actual mapping
	if (dinputGUID != NULL) {
		targetMapping->guid = *dinputGUID;
	}
	targetMapping->xinput = xinputIndex;
	targetMapping->free = false;
}

// XInputGetState implementation
DWORD WINAPI hooked_XInputGetState(DWORD dwUserIndex, XINPUT_STATE *pState)
{
	// We dont support >= 2
	if (dwUserIndex >= 2 || !diAvailable) {
		return ERROR_DEVICE_NOT_CONNECTED;
	}
	// First init
	if (di == NULL) {
		setupDInput();
	}
	ZeroMemory(pState, sizeof(XINPUT_STATE));

	// Make the reads on 0 then dispatch on the rest
	if (dwUserIndex == 0) {
		// Check if we have to refresh the list of devices
		// basically if < 0 dont do anything, if > 0 reduce by 1, if == 0 refresh, this sets a simple timer
		if (mustRefreshDevices >= 0) {
			if (--mustRefreshDevices == 0) {
				refreshDevices();
			}
		}
		// Read DI joysticks
		for (auto it = joysticks.begin(); it != joysticks.end(); ) {
			BOOL isNew = joystickStates.find(it->first) == joystickStates.end();
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
			selectController(desired, &it->first, -1, isNew);
			++it;
		}

		// Read XInput
		for (int i = 0; i < 4; i++) {
			// Read XInputGetStateEx to get the GUIDE button (though it seems to be broken on win10 now)
			if (XInputGetStateEx_wrapped(i, &xinputStates[i]) != ERROR_SUCCESS) {
				xinputReady[i] = false;
				continue;
			}
			BOOL isNew = !xinputReady[i];
			xinputReady[i] = true;
			// Check if the device wants to be changed
			int desired = readXInputControllerChange(&xinputStates[i]);
			// Set the virtual controller if necessary
			selectController(desired, NULL, i, isNew);
		}
	}

	// Get the mapping
	VirtualControllerMapping* mapping = &virtualControllers[dwUserIndex];
	
	if (mapping->free) {
		// No device for this one, give the empty device
		return ERROR_SUCCESS;
	}

	// We have something

	if (mapping->xinput >= 0) {
		// XInput!
		if (!xinputReady[mapping->xinput]) {
			// But it was disconnected...
			mapping->free = true;
			return ERROR_SUCCESS;
		}
		// Else, just copy the input to the pState
		memcpy(pState, &xinputStates[mapping->xinput], sizeof(XINPUT_STATE));
		return ERROR_SUCCESS;
	}

	// This is a DirectInput device

	if (joysticks.find(mapping->guid) == joysticks.end()) {
		// But it has been destroy
		mapping->free = true;
		return ERROR_SUCCESS;
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
	if (js->rgbButtons[8])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_BACK;
	if (js->rgbButtons[9])
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_START;
	if (js->rgbButtons[10])
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

	// As seen on x360ce
	// prevent sleep
	SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);

	//Controller is connected!
	return ERROR_SUCCESS;
}

