/*
 * A do-nothing renderer that satisfies vsmoke_api.h. Used by the bridge tests; it can also be
 * dropped in as vsmoke\vsmoke.dll to exercise the loader and the hooks without drawing anything.
 */
#include "vsmoke_api.h"

#include <stdio.h>
#include <string.h>

static const vsmoke_host_t *host;
static char lastMap[256];
static char lastPolicy[1024];
static int lastResolution, lastEnabled, fxCount, bulletCount, composites;

static int __cdecl Stub_Init(const vsmoke_host_t *h)
{
	if ( !h || h->size < sizeof(*h) || h->api_version != VSMOKE_API_VERSION )
	{
		return 0;
	}
	host = h;
	return 1;
}

static void __cdecl Stub_SetMap(const vsmoke_map_t *map)
{
	snprintf(lastMap, sizeof(lastMap), "%s", map && map->grid_path ? map->grid_path : "");
}

static void __cdecl Stub_SetSettings(const char *policy, int resolution, int enabled)
{
	snprintf(lastPolicy, sizeof(lastPolicy), "%s", policy ? policy : "");
	lastResolution = resolution;
	lastEnabled = enabled;
}

static int __cdecl Stub_Status(char *buffer, uint32_t size)
{
	return snprintf(buffer, size, "stub map=%s policy=%s resolution=%d enabled=%d fx=%d bullets=%d composites=%d",
		lastMap[0] ? lastMap : "(none)", lastPolicy[0] ? lastPolicy : "(none)", lastResolution, lastEnabled,
		fxCount, bulletCount, composites);
}

static int __cdecl Stub_FxSpawn(const char *name, int time, const float *origin)
{
	(void)name; (void)time; (void)origin;
	++fxCount;
	return 0; /* never suppress the stock effect */
}

static void __cdecl Stub_Bullet(int shooter, const char *weapon, const float *start, const float *end, int time)
{
	(void)shooter; (void)weapon; (void)start; (void)end; (void)time;
	++bulletCount;
}

static void __cdecl Stub_Composite(const vsmoke_frame_t *frame, const vsmoke_view_t *view)
{
	(void)frame; (void)view;
	++composites;
}

static const vsmoke_renderer_t stub =
{
	sizeof(vsmoke_renderer_t), VSMOKE_API_VERSION, 0, "stub", "0",
	Stub_Init, NULL, Stub_SetMap, Stub_SetSettings, NULL, Stub_Status,
	Stub_FxSpawn, Stub_Bullet,
	NULL, NULL, NULL, NULL, Stub_Composite, NULL, NULL
};

#ifndef STUB_NO_EXPORT
__declspec(dllexport) const vsmoke_renderer_t *__cdecl VSmoke_GetRenderer(uint32_t version)
#else
__declspec(dllexport) const vsmoke_renderer_t *__cdecl VSmoke_WrongName(uint32_t version) /* loader must refuse */
#endif
{
	return version == VSMOKE_API_VERSION ? &stub : NULL;
}
