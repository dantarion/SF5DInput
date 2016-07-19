#include "wrapped.h"
#include "utils.h"
#include <mutex>

DWORD(WINAPI* XInputGetState_origin)(DWORD dwUserIndex, XINPUT_STATE* pState);
DWORD(WINAPI* XInputSetState_origin)(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration);
DWORD(WINAPI* XInputGetCapabilities_origin)(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities);
VOID(WINAPI *XInputEnable_origin)(BOOL enable);
DWORD(WINAPI* XInputGetDSoundAudioDeviceGuids_origin)(DWORD dwUserIndex, GUID* pDSoundRenderGuid, GUID* pDSoundCaptureGuid);
DWORD(WINAPI* XInputGetBatteryInformation_origin)(DWORD  dwUserIndex, BYTE devType, XINPUT_BATTERY_INFORMATION* pBatteryInformation);
DWORD(WINAPI* XInputGetKeystroke_origin)(DWORD dwUserIndex, DWORD dwReserved, PXINPUT_KEYSTROKE pKeystroke);

// XInput 1.3 undocumented functions
DWORD(WINAPI* XInputGetStateEx_origin)(DWORD dwUserIndex, XINPUT_STATE_EX *pState); // 100
DWORD(WINAPI* XInputWaitForGuideButton_origin)(DWORD dwUserIndex, DWORD dwFlag, LPVOID pVoid); // 101
DWORD(WINAPI* XInputCancelGuideButtonWait_origin)(DWORD dwUserIndex); // 102
DWORD(WINAPI* XInputPowerOffController_origin)(DWORD dwUserIndex); // 103

template<typename T>
inline void loadDllCall(HINSTANCE mHinstDLL, const char* funcname, T* ppfunc)
{
	*ppfunc = reinterpret_cast<T>(::GetProcAddress(mHinstDLL, funcname));
}
std::mutex initLock;

BOOL initialized = FALSE;
void init() {
	if (initialized) {
		return;
	}
	// Make sure that we only load this once
	initLock.lock();
	if (initialized) {
		initLock.unlock();
		return;
	}
	TCHAR systemPath[MAX_PATH];
	GetSystemDirectory(systemPath, sizeof(systemPath));
	strcat_s(systemPath, "\\xinput1_3.dll");
	HINSTANCE mHinstDLL = LoadLibrary(systemPath);
	if (!mHinstDLL) {
		initialized = true;
		initLock.unlock();
		MessageBox(NULL, GetLastErrorAsString().c_str(), "SF5DInput - Loading DLL", MB_ICONERROR);
		// Cannot load dll just quit
		exit(1);
	}
	loadDllCall(mHinstDLL, "XInputEnable", &XInputEnable_origin);
	loadDllCall(mHinstDLL, "XInputGetState", &XInputGetState_origin);
	loadDllCall(mHinstDLL, "XInputSetState", &XInputSetState_origin);
	loadDllCall(mHinstDLL, "XInputGetCapabilities", &XInputGetCapabilities_origin);
	loadDllCall(mHinstDLL, "XInputGetDSoundAudioDeviceGuids", &XInputGetDSoundAudioDeviceGuids_origin);
	loadDllCall(mHinstDLL, "XInputGetBatteryInformation", &XInputGetBatteryInformation_origin);
	loadDllCall(mHinstDLL, "XInputGetKeystroke", &XInputGetKeystroke_origin);

	// XInput 1.3 undocumented functions
	loadDllCall(mHinstDLL, (const char*)100, &XInputGetStateEx_origin);
	loadDllCall(mHinstDLL, (const char*)101, &XInputWaitForGuideButton_origin);
	loadDllCall(mHinstDLL, (const char*)102, &XInputCancelGuideButtonWait_origin);
	loadDllCall(mHinstDLL, (const char*)103, &XInputPowerOffController_origin);

	initialized = true;
	initLock.unlock();
}

DWORD WINAPI XInputGetState_wrapped(DWORD dwUserIndex, XINPUT_STATE* pState) {
	init();
	return XInputGetState_origin(dwUserIndex, pState);
}
DWORD WINAPI XInputSetState_wrapped(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration) {
	init();
	return XInputSetState_origin(dwUserIndex, pVibration);
}
DWORD WINAPI XInputGetCapabilities_wrapped(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities) {
	init();
	return XInputGetCapabilities_origin(dwUserIndex, dwFlags, pCapabilities);
}
VOID WINAPI XInputEnable_wrapped(BOOL enable) {
	init();
	XInputEnable_origin(enable);
}
DWORD WINAPI XInputGetDSoundAudioDeviceGuids_wrapped(DWORD dwUserIndex, GUID* pDSoundRenderGuid, GUID* pDSoundCaptureGuid) {
	init();
	return XInputGetDSoundAudioDeviceGuids_origin(dwUserIndex, pDSoundRenderGuid, pDSoundCaptureGuid);
}
DWORD WINAPI XInputGetBatteryInformation_wrapped(DWORD  dwUserIndex, BYTE devType, XINPUT_BATTERY_INFORMATION* pBatteryInformation) {
	init();
	return XInputGetBatteryInformation_origin(dwUserIndex, devType, pBatteryInformation);
}
DWORD WINAPI XInputGetKeystroke_wrapped(DWORD dwUserIndex, DWORD dwReserved, PXINPUT_KEYSTROKE pKeystroke) {
	init();
	return XInputGetKeystroke_origin(dwUserIndex, dwReserved, pKeystroke);
}

// XInput 1.3 undocumented functions
DWORD WINAPI XInputGetStateEx_wrapped(DWORD dwUserIndex, XINPUT_STATE_EX *pState) {
	init();
	return XInputGetStateEx_origin(dwUserIndex, pState);
}
DWORD WINAPI XInputWaitForGuideButton_wrapped(DWORD dwUserIndex, DWORD dwFlag, LPVOID pVoid) {
	init();
	return XInputWaitForGuideButton_origin(dwUserIndex, dwFlag, pVoid);
}
DWORD WINAPI XInputCancelGuideButtonWait_wrapped(DWORD dwUserIndex) {
	init();
	return XInputCancelGuideButtonWait_origin(dwUserIndex);
}
DWORD WINAPI XInputPowerOffController_wrapped(DWORD dwUserIndex) {
	init();
	return XInputPowerOffController_origin(dwUserIndex);
}
