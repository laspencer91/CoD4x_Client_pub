; Recoil hooks. The engine's BG_WeaponFireRecoil takes ps in EAX, the view kick output (set) in EDI and the
; gun kick output (added) on the stack; both of its call sites are routed to cdecl C handlers.
; Prediction's Pmove call is routed with the command number, which the loop keeps in EBX.

extern CL_Recoil_FireRecoil
extern CL_Recoil_ListenFireRecoil
extern CL_Recoil_PredictPmove

SECTION .rodata
	oBG_WeaponFireRecoil dd 0x41A7B0

SECTION .text

; Replaces a `call 0x41A7B0`: handler(ps, kick, gunKick). The caller pops its one stack argument.
%macro FIRE_RECOIL_STUB 2
global %1
%1:
	push dword [esp + 4]
	push edi
	push eax
	call %2
	add esp, 12
	ret
%endmacro

FIRE_RECOIL_STUB CL_Recoil_Stub_FireRecoil, CL_Recoil_FireRecoil              ; 0x457D2E, cgame
FIRE_RECOIL_STUB CL_Recoil_Stub_ListenFireRecoil, CL_Recoil_ListenFireRecoil  ; 0x4A9CB2, listen server

; Replaces `call 0x414D10` (Pmove(pmove_t *)) at 0x4476A3 in CG_PredictPlayerState_Internal.
global CL_Recoil_Stub_PredictPmove
CL_Recoil_Stub_PredictPmove:
	push ebx
	push dword [esp + 8]
	call CL_Recoil_PredictPmove
	add esp, 8
	ret

; cdecl CL_Recoil_NativeFireRecoil(ps, kick, gunKick): the unmodified engine function.
global CL_Recoil_NativeFireRecoil
CL_Recoil_NativeFireRecoil:
	push edi
	mov eax, [esp + 8]
	mov edi, [esp + 12]
	push dword [esp + 16]
	call [oBG_WeaponFireRecoil]
	add esp, 4
	pop edi
	ret
