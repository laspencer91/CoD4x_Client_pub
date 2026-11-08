; Stock HUD readouts that the RML match HUD draws in its own style while it is drawn (CL_Rml_Patch points only the
; calls named below at these stubs; otherwise the stock routines run unchanged).
;
; The FPS readout (CG_DrawFPSDebugInfo, 0x42B890) prints each line through two corner print routines that take
; register arguments: while the HUD shows the readout (rml_corner_capture), a line goes to CL_Rml_CornerLine instead
; of the screen. Both routines return the line height in st0 and leave the stack to the caller.
;
; The lagometer (CG_DrawLagometer, 0x42FAC0, called once from CG_Draw2D): while the HUD is drawn (rml_hud_drawn) the
; stock graph is skipped, but its tail, CG_DrawDisconnect (0x42F930, local client in eax), still draws the
; connection-interrupted warning.

extern rml_corner_capture
extern rml_hud_drawn
extern CL_Rml_CornerLine

SECTION .rodata
	oCornerPrint dd 0x42B710
	oCornerHeading dd 0x42B800
	oDrawLagometer dd 0x42FAC0
	oDrawDisconnect dd 0x42F930

SECTION .text
global CL_Rml_CornerPrintStub
global CL_Rml_CornerHeadingStub
global CL_Rml_LagometerStub

; 0x42B710: text in ecx, colour in eax, screen placement in edi; stack: three floats, then the label.
CL_Rml_CornerPrintStub:
	cmp dword [rml_corner_capture], 0
	jne .capture
	jmp dword [oCornerPrint]
.capture:
	push eax
	push ecx
	push edx
	push dword [esp+0x1c]	; the label: 0x10 past the return address, plus the three saved registers
	push ecx		; the text
	call CL_Rml_CornerLine
	add esp, 8
	pop edx
	pop ecx
	pop eax
	ret

; 0x42B800 (section headings such as -Scene-): text in eax, screen placement in ecx; stack: three floats.
CL_Rml_CornerHeadingStub:
	cmp dword [rml_corner_capture], 0
	jne .capture
	jmp dword [oCornerHeading]
.capture:
	push eax
	push ecx
	push edx
	push 0			; no label
	push eax		; the text
	call CL_Rml_CornerLine
	add esp, 8
	pop edx
	pop ecx
	pop eax
	ret

; CG_DrawLagometer(localClientNum) is cdecl; the stock function ends by jumping to CG_DrawDisconnect the same way.
CL_Rml_LagometerStub:
	cmp dword [rml_hud_drawn], 0
	jne .hud
	jmp dword [oDrawLagometer]
.hud:
	mov eax, [esp+4]	; the local client
	jmp dword [oDrawDisconnect]
