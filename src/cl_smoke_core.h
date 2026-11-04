/*
 * Volumetric smoke bridge: helpers without engine dependencies (unit tested on their own).
 */
#ifndef CL_SMOKE_CORE_H
#define CL_SMOKE_CORE_H

#include <stddef.h>
#include <windows.h>
#include "vsmoke_api.h"

#define VSMOKE_POLICY_KEY      "sv_volumetricSmoke"
#define VSMOKE_POLICY_MAX      1024
#define VSMOKE_DIRECTORY       L"vsmoke"
#define VSMOKE_LIBRARY         L"vsmoke.dll"

/* Info-string lookup ("\key\value\key\value"). Returns 1 and copies the value when the key
   occurs exactly once and fits, 0 when absent, -1 when duplicated, oversized or malformed. */
int VSmoke_InfoValue(const char *info, const char *key, char *value, size_t valueSize);

/* The policy a client honours: systeminfo first (servers that register the cvar), then
   serverinfo (servers that publish it with "sets"). An invalid entry in systeminfo does not
   fall back to serverinfo. Returns 1 with the policy, 0 when none (stock smoke). */
int VSmoke_SelectPolicy(const char *systeminfo, const char *serverinfo, char *policy, size_t policySize);

/* "maps/mp/<name>.d3dbsp" -> "maps/mp/<name>.smokegrid". Returns 0 for anything else. */
int VSmoke_GridPath(const char *bspName, char *path, size_t pathSize);

/* Map identity includes the accepted snapshot's restart epoch. The server toggles
   it for same-map fast restarts even when game time keeps moving forward. */
typedef struct
{
	char name[64];
	unsigned int epoch;
} vsmoke_map_binding_t;

/* Update binding: 1 bind/rebind, -1 unbind, 0 unchanged. Inactive/invalid snapshots
   unbind once. Only the caller's masked restart bit belongs in epoch. */
int VSmoke_UpdateMapBinding(vsmoke_map_binding_t *binding, int active, const char *name, unsigned int epoch);

/* Quality enum index (0 full, 1 half, 2 quarter) -> render divisor (1, 2, 4). */
int VSmoke_ResolutionDivisor(int index);

/* <folder of module>\vsmoke. Returns 0 if the path does not fit. */
int VSmoke_RendererDirectory(HMODULE module, wchar_t *directory, size_t directoryCount);

/* nonzero if <directory>\vsmoke.dll is a regular file */
int VSmoke_RendererPresent(const wchar_t *directory);

/* Lowercase hex SHA-256 of a file. Returns 0 on read failure. */
int VSmoke_FileSha256(const wchar_t *path, char hex[65]);

/* Checks the table a renderer returned. Returns 0 and a reason if it is unusable. */
int VSmoke_ValidateRenderer(const vsmoke_renderer_t *renderer, char *reason, size_t reasonSize);

/* Loads <directory>\vsmoke.dll, optionally checks its SHA-256 against pinnedSha256 (NULL or ""
   to skip), resolves VSmoke_GetRenderer and validates the table. On failure the library is
   unloaded, NULL is returned and reason says why. */
const vsmoke_renderer_t *VSmoke_LoadRenderer(const wchar_t *directory, const char *pinnedSha256,
	HMODULE *module, char *reason, size_t reasonSize);

#endif
