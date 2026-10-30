/*
 * Optional volumetric smoke.
 *
 * The client carries only this bridge. When a renderer DLL (vsmoke\vsmoke.dll next to the
 * client DLL) is present, the bridge installs a handful of engine hooks at startup and feeds
 * the renderer through vsmoke_api.h. Without the DLL nothing is patched and the stock smoke
 * is untouched. Smoke is replaced only on servers that publish a policy (sv_volumetricSmoke),
 * so every player on that server sees the same cloud.
 */
#ifndef CL_SMOKE_H
#define CL_SMOKE_H

/* Patch_Other: install the engine hooks if a renderer is present. */
void CL_Smoke_Patch(void);
/* CL_InitOnceForAllClients: cvars, the status command and the renderer itself. */
void CL_Smoke_Init(void);
/* CL_Frame: map binding, server policy, console lines. */
void CL_Smoke_Frame(void);
/* R_ShutdownD3D: the device is about to be destroyed. */
void CL_Smoke_DeviceLost(void);

#endif
