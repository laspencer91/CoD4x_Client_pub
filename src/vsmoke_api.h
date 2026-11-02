/*
 * Volumetric smoke renderer interface (binary ABI, version 2).
 *
 * The client does not contain the smoke renderer. It loads an optional, separately
 * built renderer DLL (vsmoke.dll) and talks to it only through the plain C structures
 * and function pointers below. The renderer never patches the game image, never reads
 * engine addresses and never hooks Direct3D: everything it needs is passed in here.
 *
 * Rules shared by both sides:
 *  - 32-bit x86, every function pointer is __cdecl, structures use natural alignment
 *    and contain only 32-bit scalars, floats and pointers (same layout under MSVC and GCC).
 *  - Each structure starts with its own size; a reader rejects a smaller structure than
 *    it needs. New fields are only ever appended. VSMOKE_API_VERSION changes only for
 *    incompatible changes.
 *  - Strings are UTF-8, NUL terminated, owned by the caller and valid only during the call.
 *  - Threads: "main" is the client frame thread (filesystem, cvars, console). "render" is
 *    the renderer backend thread (Direct3D). With r_smp_backend 0 both are the same thread.
 *    Event calls (fx_spawn, bullet) come from the client game code on the main thread.
 */
#ifndef VSMOKE_API_H
#define VSMOKE_API_H

#include <stdint.h>
#include <d3d9.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VSMOKE_API_VERSION 2
#define VSMOKE_EXPORT_NAME "VSmoke_GetRenderer"

/* Renderer capability flags (vsmoke_renderer_t.flags). */
#define VSMOKE_WANTS_PARTICLE_DRAWS 0x1 /* supply particle_draws; see particles_begin */
#define VSMOKE_WANTS_HAZE_TECHNIQUES 0x2 /* supply material_technique during particle draws */

/* One view as the engine renders it (copied from the engine's GfxViewInfo). */
typedef struct vsmoke_view_s
{
	uint32_t size;                         /* sizeof(vsmoke_view_t) */
	float view_matrix[16];                 /* row-major, D3D convention */
	float projection_matrix[16];
	float view_projection_matrix[16];
	float inverse_view_projection_matrix[16];
	float origin[3];                       /* eye position, game units (inches), Z up */
	float axis[3][3];                      /* forward, left, up */
	float z_near;
	float z_far;
	float depth_hack_near;
	int time;                              /* client game time in milliseconds */
	float float_time;
	float view_offset[3];
	int x, y, width, height;               /* scene viewport in render-target pixels */
} vsmoke_view_t;

/* Native Direct3D9 draw entry points (device vtable slots 81..84). */
typedef HRESULT (WINAPI *vsmoke_draw_primitive_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT (WINAPI *vsmoke_draw_indexed_primitive_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT (WINAPI *vsmoke_draw_primitive_up_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
typedef HRESULT (WINAPI *vsmoke_draw_indexed_primitive_up_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT);

typedef struct vsmoke_draw_table_s
{
	uint32_t size;                         /* sizeof(vsmoke_draw_table_t) */
	vsmoke_draw_primitive_t draw_primitive;
	vsmoke_draw_indexed_primitive_t draw_indexed_primitive;
	vsmoke_draw_primitive_up_t draw_primitive_up;
	vsmoke_draw_indexed_primitive_up_t draw_indexed_primitive_up;
} vsmoke_draw_table_t;

/* Per-frame render state. Valid for the duration of one render-thread call. */
typedef struct vsmoke_frame_s
{
	uint32_t size;                         /* sizeof(vsmoke_frame_t) */
	IDirect3DDevice9 *device;
	IDirect3DBaseTexture9 *float_z;        /* the engine's scene linear-depth texture, or NULL */
	IDirect3DSurface9 *scene_color;        /* the engine's scene render target */
	IDirect3DSurface9 *scene_depth;
	uint32_t scene_width, scene_height;
	int distortion_pending;                /* nonzero: the engine will run its distortion copy this frame */
	const vsmoke_draw_table_t *native_draws; /* original device entry points (never NULL) */
} vsmoke_frame_t;

/*
 * The current map, given to the renderer on the main thread after the client has loaded it.
 * light_grid points at the engine's GfxLightGrid (IW3 layout, 56 bytes, see the handoff
 * document); it stays valid until the next set_map call. The renderer must copy what it needs.
 */
typedef struct vsmoke_map_s
{
	uint32_t size;                         /* sizeof(vsmoke_map_t) */
	const char *bsp_name;                  /* "maps/mp/mp_crash.d3dbsp" */
	const char *grid_path;                 /* "maps/mp/mp_crash.smokegrid", for host->fs_read */
	const void *light_grid;                /* GfxLightGrid*, or NULL */
	uint32_t light_grid_size;              /* 56 for IW3 */
	float world_mins[3], world_maxs[3];
} vsmoke_map_t;

/* Services the client offers the renderer. */
typedef struct vsmoke_host_s
{
	uint32_t size;                         /* sizeof(vsmoke_host_t) */
	uint32_t api_version;                  /* VSMOKE_API_VERSION */
	const char *host_name;                 /* "CoD4 MP 21.8" */
	const char *data_directory;            /* folder holding vsmoke.dll and its data files */
	/* main thread only: read a file through the game filesystem (loose files, IWDs,
	   usermaps, honouring sv_pure). Returns the length, or a negative value if missing. */
	int (__cdecl *fs_read)(const char *qpath, void **buffer);
	void (__cdecl *fs_free)(void *buffer);
	/* main thread only: the current string value of a cvar, or NULL if it does not exist. */
	const char *(__cdecl *cvar_string)(const char *name);
} vsmoke_host_t;

/* What the renderer DLL provides. Unused entries may be NULL unless marked required. */
typedef struct vsmoke_renderer_s
{
	uint32_t size;                         /* sizeof(vsmoke_renderer_t) */
	uint32_t api_version;                  /* VSMOKE_API_VERSION */
	uint32_t flags;                        /* VSMOKE_WANTS_* */
	const char *name;                      /* short product name for the console */
	const char *version;

	/* main thread */
	int (__cdecl *init)(const vsmoke_host_t *host);                 /* required; nonzero on success */
	void (__cdecl *shutdown)(void);
	void (__cdecl *set_map)(const vsmoke_map_t *map);                /* required; NULL on disconnect */
	/* policy: the server's raw policy string ("" = none, keep stock smoke);
	   resolution_divisor: 1 full, 2 half, 4 quarter; enabled: the client's own switch */
	void (__cdecl *set_settings)(const char *policy, int resolution_divisor, int enabled);
	int (__cdecl *pop_console)(char *buffer, uint32_t size);        /* one queued status line, length or 0 */
	int (__cdecl *status)(char *buffer, uint32_t size);             /* one-line summary for the status command */

	/* game events (main thread) */
	/* every effect the client spawns; return nonzero to suppress the stock effect */
	int (__cdecl *fx_spawn)(const char *effect_name, int time, const float *origin);
	/* a resolved bullet segment (hit, miss or penetration) */
	void (__cdecl *bullet)(int shooter, const char *weapon_name, const float *start, const float *end, int time);

	/* any thread, must be cheap and thread safe: nonzero asks the engine to produce
	   its float-Z scene depth for every 3D view (read once per client frame) */
	int (__cdecl *wants_scene_depth)(void);

	/* render thread */
	void (__cdecl *prepare)(const vsmoke_frame_t *frame, const vsmoke_view_t *view);     /* before distortion */
	/* bracket around the engine's emissive/particle pass. If the renderer sets
	   VSMOKE_WANTS_PARTICLE_DRAWS and returns a table, the client routes the device's
	   draw calls through it until particles_end; native_draws holds the originals. */
	const vsmoke_draw_table_t *(__cdecl *particles_begin)(const vsmoke_frame_t *frame, const vsmoke_view_t *view);
	void (__cdecl *particles_end)(void);
	void (__cdecl *composite)(const vsmoke_frame_t *frame, const vsmoke_view_t *view);   /* after the 3D scene */
	/* the device is about to be reset or destroyed: release every D3DPOOL_DEFAULT resource */
	void (__cdecl *device_lost)(void);
	/* render thread: selected material technique's flags; bit 0 means distortion.
	   Required with VSMOKE_WANTS_HAZE_TECHNIQUES. No engine pointer crosses the ABI. */
	void (__cdecl *material_technique)(uint32_t flags);
} vsmoke_renderer_t;

typedef const vsmoke_renderer_t *(__cdecl *vsmoke_get_renderer_t)(uint32_t api_version);

#ifdef __cplusplus
}
#endif

#endif
