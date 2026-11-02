SECTION .data
align 16
smoke_test_vector dd 0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00

SECTION .text
global Smoke_TestReturn
Smoke_TestReturn:
	ret
extern smoke_test_snapshot
extern smoke_test_flags
extern VSmoke_Stub_Technique
extern VSmoke_Stub_TechniqueAlt
global CL_Smoke_Technique
CL_Smoke_Technique:
	mov eax, [esp+4]
	movzx eax, word [eax+4]
	mov [smoke_test_flags], eax
	mov eax, 0xdeadbeef
	xor ecx, ecx
	xor edx, edx
	pxor xmm0, xmm0
	stc
	ret

global Smoke_TestTechnique
Smoke_TestTechnique:
	push ebp
	mov ebp, esp
	push ebx
	push esi
	mov ecx, [ebp+12]
	mov ebx, 0x12345678
	mov esi, 0x87654321
	movdqu xmm0, [smoke_test_vector]
	cmp dword [ebp+8], 0
	jne .alternate
	call VSmoke_Stub_Technique
	jmp .capture
.alternate:
	call VSmoke_Stub_TechniqueAlt
.capture:
	mov [smoke_test_snapshot], eax
	mov [smoke_test_snapshot+4], ecx
	mov [smoke_test_snapshot+8], ebx
	mov [smoke_test_snapshot+12], esi
	pushfd
	pop dword [smoke_test_snapshot+16]
	movq [smoke_test_snapshot+20], xmm0
	pop esi
	pop ebx
	pop ebp
	ret
