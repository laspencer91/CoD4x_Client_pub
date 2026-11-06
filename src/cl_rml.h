#ifndef COD4X_CL_RML_H
#define COD4X_CL_RML_H
#include <stdint.h>

void CL_Rml_Init(void);
void CL_Rml_Shutdown(void);
void CL_Rml_Hide(void);
void CL_Rml_Frame(void);
int CL_Rml_KeyEvent(int key, int down);
int CL_Rml_CharEvent(unsigned int value);
int CL_Rml_MouseEvent(int x, int y);
int R_QueueRmlFrame(uint32_t ticket);
#endif
