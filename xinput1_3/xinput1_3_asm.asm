.model flat
.code
extern mProcs:QWORD
DllMain_wrapper proc
	jmp mProcs[0*8]
DllMain_wrapper endp
XInputEnable_wrapper proc
	jmp mProcs[1*8]
XInputEnable_wrapper endp
XInputGetBatteryInformation_wrapper proc
	jmp mProcs[2*8]
XInputGetBatteryInformation_wrapper endp
XInputGetCapabilities_wrapper proc
	jmp mProcs[3*8]
XInputGetCapabilities_wrapper endp
XInputGetDSoundAudioDeviceGuids_wrapper proc
	jmp mProcs[4*8]
XInputGetDSoundAudioDeviceGuids_wrapper endp
XInputGetKeystroke_wrapper proc
	jmp mProcs[5*8]
XInputGetKeystroke_wrapper endp
XInputGetState_wrapper proc
	jmp mProcs[6*8]
XInputGetState_wrapper endp
XInputSetState_wrapper proc
	jmp mProcs[7*8]
XInputSetState_wrapper endp
ExportByOrdinal100 proc
	jmp mProcs[8*8]
ExportByOrdinal100 endp
ExportByOrdinal101 proc
	jmp mProcs[9*8]
ExportByOrdinal101 endp
ExportByOrdinal102 proc
	jmp mProcs[10*8]
ExportByOrdinal102 endp
ExportByOrdinal103 proc
	jmp mProcs[11*8]
ExportByOrdinal103 endp
end
