#ifndef COD4X_CL_RECOIL_H
#define COD4X_CL_RECOIL_H
void CL_Recoil_Patch(void);                  /* Patch_Other: hook the cgame recoil call */
void CL_Recoil_Init(void);
void CL_Recoil_Clear(void);
void CL_Recoil_SystemInfoChanged(const char *systemInfo);
struct recoil_contract_s;
const struct recoil_contract_s *CL_RecoilContract(void);
#endif
