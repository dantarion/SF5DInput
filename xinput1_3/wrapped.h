#pragma once
#include <windows.h>
#include "XInput.h"
#pragma comment(lib, "XInput.lib")

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


DWORD WINAPI XInputGetState_wrapped(DWORD dwUserIndex, XINPUT_STATE* pState);
DWORD WINAPI XInputSetState_wrapped(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration);
DWORD WINAPI XInputGetCapabilities_wrapped(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities);
VOID WINAPI XInputEnable_wrapped(BOOL enable);
DWORD WINAPI XInputGetDSoundAudioDeviceGuids_wrapped(DWORD dwUserIndex, GUID* pDSoundRenderGuid, GUID* pDSoundCaptureGuid);
DWORD WINAPI XInputGetBatteryInformation_wrapped(DWORD  dwUserIndex, BYTE devType, XINPUT_BATTERY_INFORMATION* pBatteryInformation);
DWORD WINAPI XInputGetKeystroke_wrapped(DWORD dwUserIndex, DWORD dwReserved, PXINPUT_KEYSTROKE pKeystroke);

// XInput 1.3 undocumented functions
DWORD WINAPI XInputGetStateEx_wrapped(DWORD dwUserIndex, XINPUT_STATE_EX *pState); // 100
DWORD WINAPI XInputWaitForGuideButton_wrapped(DWORD dwUserIndex, DWORD dwFlag, LPVOID pVoid); // 101
DWORD WINAPI XInputCancelGuideButtonWait_wrapped(DWORD dwUserIndex); // 102
DWORD WINAPI XInputPowerOffController_wrapped(DWORD dwUserIndex); // 103
