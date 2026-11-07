; Recoil model hook: the cgame's native BG_WeaponFireRecoil call (ps in EAX, view kick output in EDI,
; gun kick output on the stack) is routed through CL_Recoil_FireRecoil(ps, kick, gunKick).

extern CL_Recoil_FireRecoil

SECTION .rodata
	oBG_WeaponFireRecoil dd 0x41A7B0

SECTION .text

; Replaces `call 0x41A7B0` at 0x457D2E. The caller pops its one stack argument.
global CL_Recoil_Stub_FireRecoil
CL_Recoil_Stub_FireRecoil:
	push dword [esp + 4]
	push edi
	push eax
	call CL_Recoil_FireRecoil
	add esp, 12
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
