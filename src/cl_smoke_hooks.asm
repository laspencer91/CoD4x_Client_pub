; Optional volumetric smoke: register-preserving stubs for the hook sites in cl_smoke.c.
; Each stub saves the integer registers, flags and the x87/SSE state around the C callback,
; so the engine code it interrupts sees exactly what it would have seen without it.

; Save everything; afterwards [ebp] = EDI, [ebp+4] = ESI, [ebp+8] = EBP, [ebp+16] = EBX,
; [ebp+20] = EDX, [ebp+24] = ECX, [ebp+28] = EAX, [ebp+32] = EFLAGS, [ebp+36] = return address.
%macro SMOKE_SAVE 0
	pushfd
	pushad
	mov ebp, esp
	sub esp, 512
	and esp, -16
	fxsave [esp]
%endmacro

%macro SMOKE_RESTORE 0
	fxrstor [esp]
	mov esp, ebp
	popad
	popfd
%endmacro

SECTION .rodata
	oRB_DrawScene        dd 0x64ACD0
	oRB_Distortion       dd 0x64AD80
	oRB_Emissive         dd 0x64AEF0
	oFX_SpawnContinue    dd 0x4A1158
	oFloatZContinue      dd 0x5F9C24
	oTracerDecision      dd 0x458720
	oBulletTrace         dd 0x4558E0

SECTION .text

extern CL_Smoke_Composite
extern CL_Smoke_Prepare
extern CL_Smoke_ParticlesBegin
extern CL_Smoke_ParticlesEnd
extern CL_Smoke_FxSpawn
extern CL_Smoke_WorldBullet
extern CL_Smoke_SimulatedBullet
extern cl_smoke_sceneDepth

; call 0x64ACD0 at 0x64AD70 (3D scene drawn; ESI = GfxViewInfo*)
global VSmoke_Stub_Composite
VSmoke_Stub_Composite:
	call dword [oRB_DrawScene]
	SMOKE_SAVE
	push dword [ebp + 4]
	call CL_Smoke_Composite
	add esp, 4
	SMOKE_RESTORE
	ret

; call 0x64AD80 at 0x64B2F7 (before the distortion pass; EDI = GfxViewInfo*)
global VSmoke_Stub_Prepare
VSmoke_Stub_Prepare:
	call dword [oRB_Distortion]
	SMOKE_SAVE
	push dword [ebp]
	call CL_Smoke_Prepare
	add esp, 4
	SMOKE_RESTORE
	ret

; call 0x64AEF0 at 0x64B372 (emissive/particle pass; EDI = GfxViewInfo*)
global VSmoke_Stub_Particles
VSmoke_Stub_Particles:
	SMOKE_SAVE
	push dword [ebp]
	call CL_Smoke_ParticlesBegin
	add esp, 4
	SMOKE_RESTORE
	call dword [oRB_Emissive]
	SMOKE_SAVE
	call CL_Smoke_ParticlesEnd
	SMOKE_RESTORE
	ret

; jmp at 0x4A1150, the shared effect spawn (EAX = FxEffectDef*, stack: ?, time, origin).
; A nonzero callback result suppresses the stock effect and returns NULL to the caller.
global VSmoke_Stub_FxSpawn
VSmoke_Stub_FxSpawn:
	SMOKE_SAVE
	push dword [ebp + 48]
	push dword [ebp + 44]
	push dword [ebp + 28]
	call CL_Smoke_FxSpawn
	add esp, 12
	test eax, eax
	jnz .suppress
	SMOKE_RESTORE
	sub esp, 0x10                 ; the eight bytes the jump replaced
	push ebx
	mov ebx, [esp + 0x18]
	jmp dword [oFX_SpawnContinue]
.suppress:
	SMOKE_RESTORE
	xor eax, eax
	ret

; jmp at 0x5F9C1E: mov [ebx+0x13A8], dl sets GfxViewInfo.needsFloatZ
global VSmoke_Stub_SceneDepth
VSmoke_Stub_SceneDepth:
	pushfd
	cmp byte [cl_smoke_sceneDepth], 0
	je .original
	mov byte [ebx + 0x13A8], 1
	popfd
	jmp dword [oFloatZContinue]
.original:
	mov byte [ebx + 0x13A8], dl
	popfd
	jmp dword [oFloatZContinue]

; call 0x458720 at 0x4587B0: world impact (start on the caller's stack, EBX = end,
; EDI = shooter, ESI = local client)
global VSmoke_Stub_WorldBullet
VSmoke_Stub_WorldBullet:
	SMOKE_SAVE
	push dword [ebp + 4]
	push dword [ebp]
	push dword [ebp + 16]
	lea eax, [ebp + 48]
	push eax
	call CL_Smoke_WorldBullet
	add esp, 16
	SMOKE_RESTORE
	jmp dword [oTracerDecision]

; call 0x458720 at 0x458D6F: flesh impact (start on the caller's stack, EDI = end,
; EBP = shooter, EBX = local client)
global VSmoke_Stub_FleshBullet
VSmoke_Stub_FleshBullet:
	SMOKE_SAVE
	push dword [ebp + 16]
	push dword [ebp + 8]
	push dword [ebp]
	lea eax, [ebp + 56]
	push eax
	call CL_Smoke_WorldBullet
	add esp, 16
	SMOKE_RESTORE
	jmp dword [oTracerDecision]

; call 0x4558E0 at 0x455ADD / 0x455CFD / 0x455E2B: the local player's simulated bullet
; trace (EAX = trace results, stack: params, entity, ...). Runs the trace, then reports it.
global VSmoke_Stub_SimulatedTrace
VSmoke_Stub_SimulatedTrace:
	push ebp
	mov ebp, esp
	push eax
	push dword [ebp + 16]
	push dword [ebp + 12]
	push dword [ebp + 8]
	call dword [oBulletTrace]
	add esp, 12
	pushfd
	pushad
	mov ebx, esp
	sub esp, 512
	and esp, -16
	fxsave [esp]
	push dword [ebp - 4]
	push dword [ebp + 12]
	push dword [ebp + 8]
	call CL_Smoke_SimulatedBullet
	add esp, 12
	fxrstor [esp]
	mov esp, ebx
	popad
	popfd
	mov esp, ebp
	pop ebp
	ret
