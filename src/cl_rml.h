#ifndef COD4X_CL_RML_H
#define COD4X_CL_RML_H
#include <stdint.h>

void CL_Rml_Init(void);
void CL_Rml_Shutdown(void);
void CL_Rml_Hide(void);
void CL_Rml_FocusLost(void);
void CL_Rml_Disconnect(void);
void CL_Rml_Frame(void);
int CL_Rml_KeyEvent(int key, int down);
int CL_Rml_CharEvent(unsigned int value);
int CL_Rml_MouseEvent(int x, int y);
int R_QueueRmlFrame(uint32_t ticket);
struct menuDef_t;
void CL_Rml_MenuOpened(struct menuDef_t *menu);
void CL_Rml_MenuClosing(struct menuDef_t *menu);
void CL_Rml_OpenScriptMenu_f(void);
/* The passive HUD drew the followed player this frame, so the engine's follow text is redundant. */
int CL_Rml_FollowDrawn(void);
/* Stock readouts the match HUD draws in its style: CL_Rml_Patch points the FPS readout's line prints and the lagometer
 * call at cl_rml_stock.asm (startup, code writable); Begin and End wrap each frame's upper-right debug info so the HUD
 * can show the FPS lines instead. */
void CL_Rml_Patch(void);
void CL_Rml_CornerBegin(void);
void CL_Rml_CornerEnd(void);
#endif
