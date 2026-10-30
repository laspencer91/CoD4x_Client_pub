/*
 * Optional volumetric smoke: engine side of the renderer bridge (see vsmoke_api.h).
 *
 * Hook sites are in the stock 1.7 executable and are verified byte for byte before anything
 * is written; if one does not match (another patch got there first), no hook is installed.
 */
#include "q_shared.h"
#include "qcommon.h"
#include "client.h"
#include "cg_shared.h"
#include "r_shared.h"
#include "sys_patch.h"
#include "xassets/gfxworld.h"
#include "xassets/weapondef.h"
#include "cl_smoke.h"
#include "cl_smoke_core.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* Engine data not yet named elsewhere in the client (stock 1.7 addresses). */
#define gfxRenderTargets        ((GfxRenderTarget_t*)(0xD573EB0))
#define r_distortionPending     (*(byte*)(0xCC9F3BC))
#define CODE_IMAGE_FLOATZ       18

/* Stock functions the hooks chain to. */
#define ADDR_RB_DRAW_SCENE_CALL     0x64AD70
#define ADDR_RB_DRAW_SCENE          0x64ACD0
#define ADDR_RB_DISTORTION_CALL     0x64B2F7
#define ADDR_RB_DISTORTION          0x64AD80
#define ADDR_RB_EMISSIVE_CALL       0x64B372
#define ADDR_RB_EMISSIVE            0x64AEF0
#define ADDR_FX_SPAWN               0x4A1150
#define ADDR_FLOATZ_REQUEST         0x5F9C1E
#define ADDR_WORLD_BULLET_CALL      0x4587B0
#define ADDR_FLESH_BULLET_CALL      0x458D6F
#define ADDR_TRACER_DECISION        0x458720
#define ADDR_SIM_TRACE_CALL_0       0x455ADD
#define ADDR_SIM_TRACE_CALL_1       0x455CFD
#define ADDR_SIM_TRACE_CALL_2       0x455E2B
#define ADDR_BULLET_TRACE           0x4558E0
#define ADDR_RESET_RELEASE_CALL     0x5F5278
#define ADDR_RESET_RELEASE          0x5F3FD0

/* GfxViewInfo as far as the renderer needs it. */
typedef struct
{
	struct GfxViewParms viewParms;  /* pad[0] = zNear, pad[1] = zFar */
	struct GfxSceneDef sceneDef;
	GfxViewport sceneViewport;
} vsmokeViewInfo_t;

_Static_assert(sizeof(struct GfxViewParms) == 0x140, "GfxViewParms layout");
_Static_assert(offsetof(vsmokeViewInfo_t, sceneDef) == 0x140, "GfxViewInfo.sceneDef");
_Static_assert(offsetof(vsmokeViewInfo_t, sceneViewport) == 0x154, "GfxViewInfo.sceneViewport");
_Static_assert(sizeof(GfxLightGrid) == 56, "GfxLightGrid layout");
_Static_assert(sizeof(GfxRenderTarget_t) == 20, "GfxRenderTarget layout");

/* Bullet parameters and trace results at the simulated-trace call (stock 1.7 layout). */
typedef struct
{
	byte prefix[28];
	float start[3];
	float end[3];
	float dir[3];
} vsmokeBulletParams_t;

typedef struct
{
	float fraction;
	byte prefix[24];
	int hitType;
	byte rest[16];
	float hitPos[3];
} vsmokeBulletTrace_t;

_Static_assert(sizeof(vsmokeBulletParams_t) == 64, "BulletFireParams layout");
_Static_assert(offsetof(vsmokeBulletTrace_t, hitPos) == 48, "BulletTraceResults layout");

#define CENTITY_CLIENTNUM_OFFSET 0xcc  /* entity number read at the simulated-trace call */
#define VSMOKE_DEVICE_METHODS    119   /* IDirect3DDevice9 interface entries */
#define VSMOKE_SHADOW_ENTRIES    1024  /* the runtime's device class has further internal virtuals */

/* Read by VSmoke_Stub_SceneDepth. */
byte cl_smoke_sceneDepth;

/* Assembly stubs (cl_smoke_hooks.asm). */
void VSmoke_Stub_Composite(void);
void VSmoke_Stub_Prepare(void);
void VSmoke_Stub_Particles(void);
void VSmoke_Stub_FxSpawn(void);
void VSmoke_Stub_SceneDepth(void);
void VSmoke_Stub_WorldBullet(void);
void VSmoke_Stub_FleshBullet(void);
void VSmoke_Stub_SimulatedTrace(void);
void sub_5F3FD0(void);

static struct
{
	qboolean hooked;
	wchar_t directory[MAX_PATH];
	char directoryUtf8[MAX_PATH * 3];
	HMODULE module;
	const vsmoke_renderer_t *renderer;
	vsmoke_host_t host;
	char failure[256];
	char boundMap[MAX_QPATH];
	char policy[VSMOKE_POLICY_MAX];
	int resolution;
	int enabled;
	qboolean settingsSent;
	qboolean deviceLive;
	qboolean featherForced;
	void *shadowVtbl[VSMOKE_SHADOW_ENTRIES];
	void **savedVtbl;
	unsigned int hits[6]; /* composite, prepare, particles, effects, bullets, particle draw routing */
	int shadowEntries;
	vsmoke_draw_table_t nativeDraws;
} smoke;

static cvar_t *r_volumetricSmoke;
static cvar_t *r_volumetricSmokeQuality;
static const char *smokeQualityNames[] = { "full", "half", "quarter", NULL };

typedef struct
{
	DWORD address;
	byte bytes[8];
	int length;
} smokeSite_t;

static qboolean CL_Smoke_CallTargets(DWORD site, DWORD target)
{
	const byte *code = (const byte*)site;
	int32_t displacement;

	if ( code[0] != 0xE8 )
	{
		return qfalse;
	}
	memcpy(&displacement, code + 1, sizeof(displacement));
	return site + 5 + displacement == target;
}

static qboolean CL_Smoke_SitesUnchanged(void)
{
	static const smokeSite_t literal[] =
	{
		{ ADDR_FX_SPAWN, { 0x83, 0xEC, 0x10, 0x53, 0x8B, 0x5C, 0x24, 0x18 }, 8 },
		{ ADDR_FLOATZ_REQUEST, { 0x88, 0x93, 0xA8, 0x13, 0x00, 0x00 }, 6 },
	};
	static const DWORD calls[][2] =
	{
		{ ADDR_RB_DRAW_SCENE_CALL, ADDR_RB_DRAW_SCENE },
		{ ADDR_RB_DISTORTION_CALL, ADDR_RB_DISTORTION },
		{ ADDR_RB_EMISSIVE_CALL, ADDR_RB_EMISSIVE },
		{ ADDR_WORLD_BULLET_CALL, ADDR_TRACER_DECISION },
		{ ADDR_FLESH_BULLET_CALL, ADDR_TRACER_DECISION },
		{ ADDR_SIM_TRACE_CALL_0, ADDR_BULLET_TRACE },
		{ ADDR_SIM_TRACE_CALL_1, ADDR_BULLET_TRACE },
		{ ADDR_SIM_TRACE_CALL_2, ADDR_BULLET_TRACE },
		{ ADDR_RESET_RELEASE_CALL, ADDR_RESET_RELEASE },
	};
	int i;

	for ( i = 0; i < ARRAY_LEN(literal); ++i )
	{
		if ( memcmp((const void*)literal[i].address, literal[i].bytes, literal[i].length) )
		{
			Com_sprintf(smoke.failure, sizeof(smoke.failure), "hook site 0x%lx already modified", literal[i].address);
			return qfalse;
		}
	}
	for ( i = 0; i < ARRAY_LEN(calls); ++i )
	{
		if ( !CL_Smoke_CallTargets(calls[i][0], calls[i][1]) )
		{
			Com_sprintf(smoke.failure, sizeof(smoke.failure), "hook site 0x%lx already modified", calls[i][0]);
			return qfalse;
		}
	}
	return qtrue;
}

static void CL_Smoke_ReleaseForReset(void)
{
	CL_Smoke_DeviceLost();
	sub_5F3FD0();
}

void CL_Smoke_Patch(void)
{
	HMODULE client = NULL;

	if ( !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCWSTR)(void*)CL_Smoke_Patch, &client)
		|| !VSmoke_RendererDirectory(client, smoke.directory, MAX_PATH)
		|| !VSmoke_RendererPresent(smoke.directory) )
	{
		return; /* no renderer: leave the engine exactly as it is */
	}
	if ( !CL_Smoke_SitesUnchanged() )
	{
		return;
	}
	SetCall(ADDR_RB_DRAW_SCENE_CALL, VSmoke_Stub_Composite);
	SetCall(ADDR_RB_DISTORTION_CALL, VSmoke_Stub_Prepare);
	SetCall(ADDR_RB_EMISSIVE_CALL, VSmoke_Stub_Particles);
	SetJump(ADDR_FX_SPAWN, VSmoke_Stub_FxSpawn);
	SetJump(ADDR_FLOATZ_REQUEST, VSmoke_Stub_SceneDepth);
	*(byte*)(ADDR_FLOATZ_REQUEST + 5) = NOP;
	SetCall(ADDR_WORLD_BULLET_CALL, VSmoke_Stub_WorldBullet);
	SetCall(ADDR_FLESH_BULLET_CALL, VSmoke_Stub_FleshBullet);
	SetCall(ADDR_SIM_TRACE_CALL_0, VSmoke_Stub_SimulatedTrace);
	SetCall(ADDR_SIM_TRACE_CALL_1, VSmoke_Stub_SimulatedTrace);
	SetCall(ADDR_SIM_TRACE_CALL_2, VSmoke_Stub_SimulatedTrace);
	SetCall(ADDR_RESET_RELEASE_CALL, CL_Smoke_ReleaseForReset);
	smoke.hooked = qtrue;
}

/* Host services */

static int __cdecl CL_Smoke_FsRead(const char *qpath, void **buffer)
{
	*buffer = NULL;
	if ( !qpath || !qpath[0] )
	{
		return -1;
	}
	return FS_ReadFile(qpath, buffer);
}

static void __cdecl CL_Smoke_FsFree(void *buffer)
{
	if ( buffer )
	{
		FS_FreeFile(buffer);
	}
}

static const char *__cdecl CL_Smoke_CvarString(const char *name)
{
	return name && Cvar_FindVar(name) ? Cvar_GetVariantString(name) : NULL;
}

static void CL_Smoke_Status_f(void)
{
	char text[1024];

	if ( !smoke.hooked )
	{
		Com_Printf(CON_CHANNEL_DONT_FILTER, "Volumetric smoke: off (%s)\n",
			smoke.failure[0] ? smoke.failure : "no vsmoke\\vsmoke.dll next to the client");
		return;
	}
	if ( !smoke.renderer )
	{
		Com_Printf(CON_CHANNEL_DONT_FILTER, "Volumetric smoke: renderer not loaded (%s)\n", smoke.failure);
		return;
	}
	Com_Printf(CON_CHANNEL_DONT_FILTER, "Volumetric smoke: %s %s, client switch %s\n",
		smoke.renderer->name ? smoke.renderer->name : "renderer", smoke.renderer->version ? smoke.renderer->version : "",
		r_volumetricSmoke->boolean ? "on" : "off");
	Com_Printf(CON_CHANNEL_DONT_FILTER, "Volumetric smoke: server policy %s\n",
		smoke.policy[0] ? smoke.policy : "not set (stock smoke)");
	if ( smoke.renderer->status && smoke.renderer->status(text, sizeof(text)) > 0 )
	{
		Com_Printf(CON_CHANNEL_DONT_FILTER, "Volumetric smoke: %s\n", text);
	}
	Com_Printf(CON_CHANNEL_DONT_FILTER, "Volumetric smoke: hooks composite=%u prepare=%u particles=%u effects=%u bullets=%u routed=%u\n",
		smoke.hits[0], smoke.hits[1], smoke.hits[2], smoke.hits[3], smoke.hits[4], smoke.hits[5]);
	Com_Printf(CON_CHANNEL_DONT_FILTER, "Volumetric smoke: quality %s (r_volumetricSmokeQuality full|half|quarter)\n",
		smokeQualityNames[r_volumetricSmokeQuality->integer >= 0 && r_volumetricSmokeQuality->integer < 3 ? r_volumetricSmokeQuality->integer : 1]);
}

void CL_Smoke_Init(void)
{
#ifdef VSMOKE_RENDERER_SHA256
	const char *pinned = VSMOKE_RENDERER_SHA256;
#else
	const char *pinned = NULL;
#endif

	r_volumetricSmoke = Cvar_RegisterBool("r_volumetricSmoke", qtrue, CVAR_ARCHIVE,
		"Use the volumetric smoke renderer on servers that enable it");
	r_volumetricSmokeQuality = Cvar_RegisterEnum("r_volumetricSmokeQuality", smokeQualityNames, 1, CVAR_ARCHIVE,
		"Volumetric smoke render resolution: full, half or quarter");
	Cmd_AddCommand("volumetricsmoke_status", CL_Smoke_Status_f);

	if ( !smoke.hooked || smoke.renderer )
	{
		return;
	}
	if ( !WideCharToMultiByte(CP_UTF8, 0, smoke.directory, -1, smoke.directoryUtf8, sizeof(smoke.directoryUtf8), NULL, NULL) )
	{
		Com_sprintf(smoke.failure, sizeof(smoke.failure), "renderer path not representable");
		return;
	}
	smoke.renderer = VSmoke_LoadRenderer(smoke.directory, pinned, &smoke.module, smoke.failure, sizeof(smoke.failure));
	if ( !smoke.renderer )
	{
		Com_PrintWarning(CON_CHANNEL_GFX, "Volumetric smoke disabled: %s\n", smoke.failure);
		return;
	}
	smoke.host.size = sizeof(smoke.host);
	smoke.host.api_version = VSMOKE_API_VERSION;
	smoke.host.host_name = Q3_VERSION;
	smoke.host.data_directory = smoke.directoryUtf8;
	smoke.host.fs_read = CL_Smoke_FsRead;
	smoke.host.fs_free = CL_Smoke_FsFree;
	smoke.host.cvar_string = CL_Smoke_CvarString;
	if ( !smoke.renderer->init(&smoke.host) )
	{
		Com_sprintf(smoke.failure, sizeof(smoke.failure), "renderer initialisation failed");
		Com_PrintWarning(CON_CHANNEL_GFX, "Volumetric smoke disabled: %s\n", smoke.failure);
		smoke.renderer = NULL;
		FreeLibrary(smoke.module);
		smoke.module = NULL;
		return;
	}
	Com_Printf(CON_CHANNEL_GFX, "Volumetric smoke renderer loaded: %s %s\n",
		smoke.renderer->name ? smoke.renderer->name : "", smoke.renderer->version ? smoke.renderer->version : "");
}

static void CL_Smoke_BindMap(void)
{
	vsmoke_map_t map;
	char gridPath[MAX_QPATH];
	const char *bspName = cgs.mapname;

	if ( clientUIActives.state != CA_ACTIVE || !rgp.world || !bspName[0] )
	{
		if ( smoke.boundMap[0] )
		{
			smoke.boundMap[0] = '\0';
			smoke.renderer->set_map(NULL);
		}
		return;
	}
	if ( !Q_stricmp(smoke.boundMap, bspName) )
	{
		return;
	}
	Q_strncpyz(smoke.boundMap, bspName, sizeof(smoke.boundMap));
	memset(&map, 0, sizeof(map));
	map.size = sizeof(map);
	map.bsp_name = bspName;
	map.grid_path = VSmoke_GridPath(bspName, gridPath, sizeof(gridPath)) ? gridPath : NULL;
	map.light_grid = &rgp.world->lightGrid;
	map.light_grid_size = sizeof(GfxLightGrid);
	VectorCopy(rgp.world->mins, map.world_mins);
	VectorCopy(rgp.world->maxs, map.world_maxs);
	smoke.renderer->set_map(&map);
}

static void CL_Smoke_UpdateSettings(void)
{
	char policy[VSMOKE_POLICY_MAX];
	int resolution = VSmoke_ResolutionDivisor(r_volumetricSmokeQuality->integer);
	int enabled = r_volumetricSmoke->boolean ? 1 : 0;

	policy[0] = '\0';
	if ( clientUIActives.state >= CA_PRIMED )
	{
		/* The raw replicated configstrings, not a client-writable cvar copy. */
		VSmoke_SelectPolicy(CL_GetConfigString(CS_SYSTEMINFO), CL_GetConfigString(CS_SERVERINFO), policy, sizeof(policy));
	}
	if ( smoke.settingsSent && !strcmp(policy, smoke.policy) && resolution == smoke.resolution && enabled == smoke.enabled )
	{
		return;
	}
	Q_strncpyz(smoke.policy, policy, sizeof(smoke.policy));
	smoke.resolution = resolution;
	smoke.enabled = enabled;
	smoke.settingsSent = qtrue;
	if ( smoke.renderer->set_settings )
	{
		smoke.renderer->set_settings(smoke.policy, resolution, enabled);
	}
}

/* The float-Z pass only runs with r_zFeather on. While a smoke-enabled map is active the
   bridge turns it on and restores the player's value afterwards. */
static void CL_Smoke_RequireDepthFeather(qboolean required)
{
	cvar_t *feather = Cvar_FindVar("r_zFeather");

	if ( !feather )
	{
		return;
	}
	if ( required && !smoke.featherForced && !feather->boolean )
	{
		smoke.featherForced = qtrue;
		Cvar_SetBool(feather, qtrue);
		Com_Printf(CON_CHANNEL_GFX, "^3Smoke:^7 r_zFeather enabled for volumetric smoke\n");
	}
	else if ( !required && smoke.featherForced )
	{
		smoke.featherForced = qfalse;
		Cvar_SetBool(feather, qfalse);
	}
}

void CL_Smoke_Frame(void)
{
	char line[512];
	int i;

	if ( !smoke.renderer )
	{
		return;
	}
	CL_Smoke_UpdateSettings();
	CL_Smoke_BindMap();
	cl_smoke_sceneDepth = smoke.enabled && smoke.policy[0] && smoke.boundMap[0]
		&& smoke.renderer->wants_scene_depth && smoke.renderer->wants_scene_depth();
	CL_Smoke_RequireDepthFeather(cl_smoke_sceneDepth);
	if ( smoke.renderer->pop_console )
	{
		for ( i = 0; i < 8 && smoke.renderer->pop_console(line, sizeof(line)) > 0; ++i )
		{
			Com_Printf(CON_CHANNEL_GFX, "^3Smoke:^7 %s\n", line);
		}
	}
}

void CL_Smoke_DeviceLost(void)
{
	if ( smoke.renderer && smoke.deviceLive && smoke.renderer->device_lost )
	{
		smoke.renderer->device_lost();
	}
	smoke.deviceLive = qfalse;
}

/* Render thread */

static qboolean CL_Smoke_BuildFrame(const vsmokeViewInfo_t *info, vsmoke_frame_t *frame, vsmoke_view_t *view)
{
	const struct GfxViewParms *parms;
	struct GfxImage *depth;
	void **vtbl;

	if ( !smoke.renderer || !info || !r_dx.device )
	{
		return qfalse;
	}
	vtbl = smoke.savedVtbl ? smoke.savedVtbl : *(void***)r_dx.device;
	smoke.nativeDraws.size = sizeof(smoke.nativeDraws);
	smoke.nativeDraws.draw_primitive = (vsmoke_draw_primitive_t)vtbl[81];
	smoke.nativeDraws.draw_indexed_primitive = (vsmoke_draw_indexed_primitive_t)vtbl[82];
	smoke.nativeDraws.draw_primitive_up = (vsmoke_draw_primitive_up_t)vtbl[83];
	smoke.nativeDraws.draw_indexed_primitive_up = (vsmoke_draw_indexed_primitive_up_t)vtbl[84];

	memset(frame, 0, sizeof(*frame));
	frame->size = sizeof(*frame);
	frame->device = r_dx.device;
	depth = gfxCmdBufSourceState.input.codeImages[CODE_IMAGE_FLOATZ];
	frame->float_z = depth ? depth->texture.basemap : NULL;
	frame->scene_color = gfxRenderTargets[R_RENDERTARGET_SCENE].surface.color;
	frame->scene_depth = gfxRenderTargets[R_RENDERTARGET_SCENE].surface.depthStencil;
	frame->scene_width = gfxRenderTargets[R_RENDERTARGET_SCENE].width;
	frame->scene_height = gfxRenderTargets[R_RENDERTARGET_SCENE].height;
	frame->distortion_pending = r_distortionPending != 0;
	frame->native_draws = &smoke.nativeDraws;

	parms = &info->viewParms;
	memset(view, 0, sizeof(*view));
	view->size = sizeof(*view);
	memcpy(view->view_matrix, &parms->viewMatrix, sizeof(view->view_matrix));
	memcpy(view->projection_matrix, &parms->projectionMatrix, sizeof(view->projection_matrix));
	memcpy(view->view_projection_matrix, &parms->viewProjectionMatrix, sizeof(view->view_projection_matrix));
	memcpy(view->inverse_view_projection_matrix, &parms->inverseViewProjectionMatrix, sizeof(view->inverse_view_projection_matrix));
	VectorCopy(parms->origin, view->origin);
	memcpy(view->axis, parms->axis, sizeof(view->axis));
	memcpy(&view->z_near, &parms->pad[0], sizeof(float));
	memcpy(&view->z_far, &parms->pad[1], sizeof(float));
	view->depth_hack_near = parms->depthHackNearClip;
	view->time = info->sceneDef.time;
	view->float_time = info->sceneDef.floatTime;
	VectorCopy(info->sceneDef.viewOffset, view->view_offset);
	view->x = info->sceneViewport.x;
	view->y = info->sceneViewport.y;
	view->width = info->sceneViewport.width;
	view->height = info->sceneViewport.height;
	smoke.deviceLive = qtrue;
	return qtrue;
}

void CL_Smoke_Composite(const vsmokeViewInfo_t *info)
{
	vsmoke_frame_t frame;
	vsmoke_view_t view;

	++smoke.hits[0];

	if ( smoke.renderer && smoke.renderer->composite && CL_Smoke_BuildFrame(info, &frame, &view) )
	{
		smoke.renderer->composite(&frame, &view);
	}
}

void CL_Smoke_Prepare(const vsmokeViewInfo_t *info)
{
	vsmoke_frame_t frame;
	vsmoke_view_t view;

	++smoke.hits[1];

	if ( smoke.renderer && smoke.renderer->prepare && CL_Smoke_BuildFrame(info, &frame, &view) )
	{
		smoke.renderer->prepare(&frame, &view);
	}
}

/* Copies the device's whole method table: the public IDirect3DDevice9 entries and the internal
   virtual functions Direct3D's own code calls through the same table (all in one module). */
static qboolean CL_Smoke_CopyMethodTable(void **vtbl)
{
	HMODULE owner = NULL, entry;
	MEMORY_BASIC_INFORMATION info;
	int i;

	if ( !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)vtbl[0], &owner) )
	{
		return qfalse;
	}
	for ( i = 0; i < VSMOKE_SHADOW_ENTRIES; ++i )
	{
		if ( !VirtualQuery(&vtbl[i], &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) )
		{
			break;
		}
		if ( !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)vtbl[i], &entry) || entry != owner )
		{
			break;
		}
		smoke.shadowVtbl[i] = vtbl[i];
	}
	if ( i < VSMOKE_DEVICE_METHODS || i == VSMOKE_SHADOW_ENTRIES )
	{
		return qfalse; /* not a table this bridge understands: leave the device alone */
	}
	smoke.shadowEntries = i;
	return qtrue;
}

void CL_Smoke_ParticlesBegin(const vsmokeViewInfo_t *info)
{
	vsmoke_frame_t frame;
	vsmoke_view_t view;
	const vsmoke_draw_table_t *draws;
	IDirect3DDevice9 *device;
	void **vtbl;

	++smoke.hits[2];
	if ( !smoke.renderer || !smoke.renderer->particles_begin || !CL_Smoke_BuildFrame(info, &frame, &view) )
	{
		return;
	}
	draws = smoke.renderer->particles_begin(&frame, &view);
	if ( !draws || !(smoke.renderer->flags & VSMOKE_WANTS_PARTICLE_DRAWS) || draws->size < sizeof(*draws) || smoke.savedVtbl )
	{
		return;
	}
	/* Route this device's draws through the renderer for the particle pass only, using a private
	   copy of its method table; Direct3D's own code and tables are not modified. */
	device = r_dx.device;
	vtbl = *(void***)device;
	if ( !CL_Smoke_CopyMethodTable(vtbl) )
	{
		return;
	}
	if ( draws->draw_primitive ) smoke.shadowVtbl[81] = (void*)draws->draw_primitive;
	if ( draws->draw_indexed_primitive ) smoke.shadowVtbl[82] = (void*)draws->draw_indexed_primitive;
	if ( draws->draw_primitive_up ) smoke.shadowVtbl[83] = (void*)draws->draw_primitive_up;
	if ( draws->draw_indexed_primitive_up ) smoke.shadowVtbl[84] = (void*)draws->draw_indexed_primitive_up;
	smoke.savedVtbl = vtbl;
	*(void***)device = smoke.shadowVtbl;
	++smoke.hits[5];
}

void CL_Smoke_ParticlesEnd(void)
{
	if ( smoke.savedVtbl )
	{
		if ( r_dx.device && *(void***)r_dx.device == smoke.shadowVtbl )
		{
			*(void***)r_dx.device = smoke.savedVtbl;
		}
		smoke.savedVtbl = NULL;
	}
	if ( smoke.renderer && smoke.renderer->particles_end )
	{
		smoke.renderer->particles_end();
	}
}

/* Game events (main thread) */

int CL_Smoke_FxSpawn(const char *const *effectDef, int time, const float *origin)
{
	++smoke.hits[3];
	if ( !smoke.renderer || !smoke.renderer->fx_spawn || !effectDef || !*effectDef || !origin )
	{
		return 0;
	}
	return smoke.renderer->fx_spawn(*effectDef, time, origin) ? 1 : 0;
}

static qboolean CL_Smoke_ValidPoint(const float *p)
{
	int i;

	for ( i = 0; i < 3; ++i )
	{
		if ( !isfinite(p[i]) || fabsf(p[i]) > 131000.0f )
		{
			return qfalse;
		}
	}
	return qtrue;
}

static void CL_Smoke_QueueBullet(int shooter, const float *start, const float *end)
{
	const char *weaponName = "";
	int weapon;

	++smoke.hits[4];

	if ( shooter < 0 || shooter >= MAX_GENTITIES || !CL_Smoke_ValidPoint(start) || !CL_Smoke_ValidPoint(end) )
	{
		return;
	}
	weapon = cgEntities[shooter].nextState.weapon;
	if ( weapon > 0 && weapon < BG_GetNumWeapons() && BG_GetWeaponDef(weapon) && BG_GetWeaponDef(weapon)->szInternalName )
	{
		weaponName = BG_GetWeaponDef(weapon)->szInternalName;
	}
	smoke.renderer->bullet(shooter, weaponName, start, end, cg.time);
}

/* Remote players' shots, resolved before the tracer decision. */
void CL_Smoke_WorldBullet(const float *start, const float *end, int shooter, int localClientNum)
{
	if ( !smoke.renderer || !smoke.renderer->bullet || localClientNum != 0 || shooter == cg.clientNum )
	{
		return; /* the local player's shots come from the simulated trace, including misses */
	}
	CL_Smoke_QueueBullet(shooter, start, end);
}

/* The local player's simulated bullet trace. */
void CL_Smoke_SimulatedBullet(const vsmokeBulletParams_t *params, const byte *entity, const vsmokeBulletTrace_t *trace)
{
	const float *end;
	int shooter;

	if ( !smoke.renderer || !smoke.renderer->bullet || !params || !entity || !trace )
	{
		return;
	}
	memcpy(&shooter, entity + CENTITY_CLIENTNUM_OFFSET, sizeof(shooter));
	if ( shooter != cg.clientNum || !isfinite(trace->fraction) || trace->fraction < 0.0f || trace->fraction > 1.0f
		|| trace->hitType < 0 || trace->hitType > 3 || (!trace->hitType && trace->fraction < 0.999f) )
	{
		return;
	}
	end = trace->hitType ? trace->hitPos : params->end;
	CL_Smoke_QueueBullet(shooter, params->start, end);
}
