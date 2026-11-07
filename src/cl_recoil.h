#ifndef COD4X_CL_RECOIL_H
#define COD4X_CL_RECOIL_H
#include "q_shared.h"
#include "qcommon.h"
/* Server-authoritative recoil, client side; see cl_recoil.c and docs/recoil-models.md. */
void CL_Recoil_Patch(void);                  /* Patch_Other: hook recoil and prediction calls */
void CL_Recoil_Init(void);
void CL_Recoil_Clear(void);
void CL_Recoil_SystemInfoChanged(const char *systemInfo);
void CL_Recoil_FoldCommand(usercmd_t *cmd);  /* CL_FinishMove: add the simulated view kick */
/* CL_WritePacket: copy commands first..first+count-1 for the wire and adjust the delta base. */
void CL_Recoil_WireCommands(usercmd_t *wire, int first, int count, usercmd_t *base);
void CL_Recoil_ParseCorrection(msg_t *msg);  /* svc_recoil */
struct recoil_contract_s;
const struct recoil_contract_s *CL_RecoilContract(void);
#endif
