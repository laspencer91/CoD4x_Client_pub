#ifndef CL_TICKRATE_H
#define CL_TICKRATE_H

#include "client.h"

/* Engine glue for the high-rate client; the testable logic is in cl_tickrate_core. */
void CL_Tickrate_Patch(void);                 /* Patch_Other: hook native CL_GetSnapshot */
void CL_Tickrate_Init(void);                  /* CL_InitOnceForAllClients, after Patch */
void CL_Tickrate_Clear(void);                 /* CL_ClearState */
void CL_Tickrate_SystemInfoChanged(const char *systemInfo);
void CL_Tickrate_ParseSnapshot(msg_t *msg);   /* svc_snapshot, wraps native CL_ParseSnapshot */
void CL_Tickrate_PacketSent(int sequence, const outPacket_t *packet);
int CL_Tickrate_PacketDue(int now, int requestedRate); /* -1: use stock pacing */

#endif
