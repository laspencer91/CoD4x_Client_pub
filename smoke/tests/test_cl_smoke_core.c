/*
 * Tests for the engine-free part of the volumetric smoke bridge (cl_smoke_core.c) and the
 * binary layout promised by vsmoke_api.h. Usage: test_cl_smoke_core <folder with stub DLLs>
 */
#include "cl_smoke_core.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int failures, checks;

#define CHECK(condition) do { ++checks; if ( !(condition) ) { ++failures; \
	printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while ( 0 )

static void TestInfoValue(void)
{
	char value[32];

	CHECK(VSmoke_InfoValue("\\a\\1\\sv_volumetricSmoke\\v1 width=1.3\\b\\2", VSMOKE_POLICY_KEY, value, sizeof(value)) == 1);
	CHECK(!strcmp(value, "v1 width=1.3"));
	CHECK(VSmoke_InfoValue("sv_volumetricSmoke\\v1", VSMOKE_POLICY_KEY, value, sizeof(value)) == 1 && !strcmp(value, "v1"));
	CHECK(VSmoke_InfoValue("\\a\\1\\b\\2", VSMOKE_POLICY_KEY, value, sizeof(value)) == 0 && !value[0]);
	CHECK(VSmoke_InfoValue("", VSMOKE_POLICY_KEY, value, sizeof(value)) == 0);
	CHECK(VSmoke_InfoValue("\\sv_volumetricSmokeX\\v1", VSMOKE_POLICY_KEY, value, sizeof(value)) == 0);
	CHECK(VSmoke_InfoValue("\\xsv_volumetricSmoke\\v1", VSMOKE_POLICY_KEY, value, sizeof(value)) == 0);
	CHECK(VSmoke_InfoValue("\\sv_volumetricSmoke\\v1\\sv_volumetricSmoke\\v1", VSMOKE_POLICY_KEY, value, sizeof(value)) == -1);
	CHECK(VSmoke_InfoValue("\\sv_volumetricSmoke\\0123456789012345678901234567890123456789", VSMOKE_POLICY_KEY, value, sizeof(value)) == -1);
	CHECK(VSmoke_InfoValue("\\sv_volumetricSmoke", VSMOKE_POLICY_KEY, value, sizeof(value)) == -1);
	CHECK(VSmoke_InfoValue("\\a\\\\sv_volumetricSmoke\\v1", VSMOKE_POLICY_KEY, value, sizeof(value)) == 1);
}

static void TestSelectPolicy(void)
{
	char policy[64];

	CHECK(VSmoke_SelectPolicy("\\sv_volumetricSmoke\\v1 a=1", "\\sv_volumetricSmoke\\v1 b=2", policy, sizeof(policy)) == 1);
	CHECK(!strcmp(policy, "v1 a=1"));
	CHECK(VSmoke_SelectPolicy("\\timescale\\1", "\\sv_volumetricSmoke\\v1 b=2", policy, sizeof(policy)) == 1);
	CHECK(!strcmp(policy, "v1 b=2"));
	/* a broken systeminfo entry never falls back to a looser channel */
	CHECK(VSmoke_SelectPolicy("\\sv_volumetricSmoke\\x\\sv_volumetricSmoke\\y", "\\sv_volumetricSmoke\\v1", policy, sizeof(policy)) == 0);
	CHECK(!policy[0]);
	CHECK(VSmoke_SelectPolicy("\\sv_volumetricSmoke\\", "", policy, sizeof(policy)) == 0);
	CHECK(VSmoke_SelectPolicy(NULL, NULL, policy, sizeof(policy)) == 0);
}

static void TestGridPath(void)
{
	char path[64];

	CHECK(VSmoke_GridPath("maps/mp/mp_crash.d3dbsp", path, sizeof(path)) && !strcmp(path, "maps/mp/mp_crash.smokegrid"));
	CHECK(VSmoke_GridPath("maps/mp/mp_backlot_d5c.D3DBSP", path, sizeof(path)) && !strcmp(path, "maps/mp/mp_backlot_d5c.smokegrid"));
	CHECK(!VSmoke_GridPath("maps/mp/../evil.d3dbsp", path, sizeof(path)) && !path[0]);
	CHECK(!VSmoke_GridPath("maps/mp/sub/x.d3dbsp", path, sizeof(path)));
	CHECK(!VSmoke_GridPath("maps/x.d3dbsp", path, sizeof(path)));
	CHECK(!VSmoke_GridPath("maps/mp/.d3dbsp", path, sizeof(path)));
	CHECK(!VSmoke_GridPath("maps/mp/mp_crash.bsp", path, sizeof(path)));
	CHECK(!VSmoke_GridPath("maps/mp/mp_crash.d3dbsp", path, 20));
	CHECK(!VSmoke_GridPath(NULL, path, sizeof(path)));
}

static void TestResolution(void)
{
	CHECK(VSmoke_ResolutionDivisor(0) == 1);
	CHECK(VSmoke_ResolutionDivisor(1) == 2);
	CHECK(VSmoke_ResolutionDivisor(2) == 4);
	CHECK(VSmoke_ResolutionDivisor(7) == 2);
}

static int __cdecl Init(const vsmoke_host_t *host) { (void)host; return 1; }
static void __cdecl SetMap(const vsmoke_map_t *map) { (void)map; }
static const vsmoke_draw_table_t *__cdecl Begin(const vsmoke_frame_t *f, const vsmoke_view_t *v) { (void)f; (void)v; return NULL; }
static void __cdecl End(void) {}
static void __cdecl Technique(uint32_t flags) { (void)flags; }

static void TestValidate(void)
{
	vsmoke_renderer_t renderer;
	char reason[128];

	memset(&renderer, 0, sizeof(renderer));
	renderer.size = sizeof(renderer);
	renderer.api_version = VSMOKE_API_VERSION;
	renderer.init = Init;
	renderer.set_map = SetMap;
	CHECK(VSmoke_ValidateRenderer(&renderer, reason, sizeof(reason)));
	renderer.api_version = VSMOKE_API_VERSION + 1;
	CHECK(!VSmoke_ValidateRenderer(&renderer, reason, sizeof(reason)) && strstr(reason, "version"));
	renderer.api_version = VSMOKE_API_VERSION;
	renderer.size = sizeof(renderer) - 4;
	CHECK(!VSmoke_ValidateRenderer(&renderer, reason, sizeof(reason)));
	renderer.size = sizeof(renderer);
	renderer.set_map = NULL;
	CHECK(!VSmoke_ValidateRenderer(&renderer, reason, sizeof(reason)));
	renderer.set_map = SetMap;
	renderer.flags = VSMOKE_WANTS_PARTICLE_DRAWS;
	CHECK(!VSmoke_ValidateRenderer(&renderer, reason, sizeof(reason)));
	renderer.particles_begin = Begin;
	CHECK(!VSmoke_ValidateRenderer(&renderer, reason, sizeof(reason)));
	renderer.particles_end = End;
	renderer.flags |= VSMOKE_WANTS_HAZE_TECHNIQUES;
	CHECK(!VSmoke_ValidateRenderer(&renderer, reason, sizeof(reason)) && strstr(reason, "haze"));
	renderer.material_technique = Technique;
	CHECK(VSmoke_ValidateRenderer(&renderer, reason, sizeof(reason)));
	renderer.flags = VSMOKE_WANTS_HAZE_TECHNIQUES;
	CHECK(!VSmoke_ValidateRenderer(&renderer, reason, sizeof(reason)));
	renderer.api_version = 1;
	CHECK(!VSmoke_ValidateRenderer(&renderer, reason, sizeof(reason)) && strstr(reason, "version"));
	CHECK(!VSmoke_ValidateRenderer(NULL, reason, sizeof(reason)));
}

/* Stage a stub as <scratch>\<name>\vsmoke.dll and return the folder. */
static int Stage(const wchar_t *stubs, const wchar_t *dll, const wchar_t *name, wchar_t *folder)
{
	wchar_t source[MAX_PATH], target[MAX_PATH];

	GetTempPathW(MAX_PATH, folder);
	wcscat(folder, name);
	CreateDirectoryW(folder, NULL);
	swprintf(source, MAX_PATH, L"%ls\\%ls", stubs, dll);
	swprintf(target, MAX_PATH, L"%ls\\vsmoke.dll", folder);
	return CopyFileW(source, target, FALSE);
}

static void TestLoad(const wchar_t *stubs)
{
	wchar_t folder[MAX_PATH], path[MAX_PATH], missing[MAX_PATH];
	char reason[256], hash[65], wrong[65];
	const vsmoke_renderer_t *renderer;
	HMODULE module;

	CHECK(Stage(stubs, L"vsmoke_stub.dll", L"vsmoke-test-ok", folder));
	CHECK(VSmoke_RendererPresent(folder));
	swprintf(missing, MAX_PATH, L"%ls\\nothing-here", folder);
	CHECK(!VSmoke_RendererPresent(missing));

	renderer = VSmoke_LoadRenderer(folder, NULL, &module, reason, sizeof(reason));
	CHECK(renderer && module && !strcmp(renderer->name, "stub"));
	if ( renderer )
	{
		vsmoke_host_t host;
		char status[256];
		const float origin[3] = { 0, 0, 0 };
		memset(&host, 0, sizeof(host));
		host.size = sizeof(host);
		host.api_version = VSMOKE_API_VERSION;
		CHECK(renderer->init(&host));
		renderer->set_settings("v1", 2, 1);
		CHECK(renderer->fx_spawn("props/american_smoke_grenade_mp", 1000, origin) == 0);
		CHECK(renderer->status(status, sizeof(status)) > 0 && strstr(status, "policy=v1") && strstr(status, "fx=1"));
		FreeLibrary(module);
	}

	swprintf(path, MAX_PATH, L"%ls\\vsmoke.dll", folder);
	CHECK(VSmoke_FileSha256(path, hash) && strlen(hash) == 64);
	renderer = VSmoke_LoadRenderer(folder, hash, &module, reason, sizeof(reason));
	CHECK(renderer != NULL);
	if ( renderer )
	{
		FreeLibrary(module);
	}
	strcpy(wrong, hash);
	wrong[0] = wrong[0] == '0' ? '1' : '0';
	renderer = VSmoke_LoadRenderer(folder, wrong, &module, reason, sizeof(reason));
	CHECK(!renderer && !module && strstr(reason, "does not match"));

	CHECK(Stage(stubs, L"vsmoke_stub_noexport.dll", L"vsmoke-test-noexport", folder));
	renderer = VSmoke_LoadRenderer(folder, NULL, &module, reason, sizeof(reason));
	CHECK(!renderer && !module && strstr(reason, VSMOKE_EXPORT_NAME));

	renderer = VSmoke_LoadRenderer(missing, NULL, &module, reason, sizeof(reason));
	CHECK(!renderer && !module && strstr(reason, "failed to load"));
}

static void TestDirectory(void)
{
	wchar_t directory[MAX_PATH];
	size_t length;

	CHECK(VSmoke_RendererDirectory(NULL, directory, MAX_PATH));
	length = wcslen(directory);
	CHECK(length > 7 && !wcscmp(directory + length - 7, L"\\vsmoke"));
	CHECK(!VSmoke_RendererDirectory(NULL, directory, 4));
}

/* The layout every renderer build must match (also listed in smoke/HANDOFF.md). */
static void TestLayout(void)
{
	CHECK(sizeof(vsmoke_view_t) == 356);
	CHECK(offsetof(vsmoke_view_t, origin) == 260);
	CHECK(offsetof(vsmoke_view_t, time) == 320);
	CHECK(offsetof(vsmoke_view_t, x) == 340);
	CHECK(sizeof(vsmoke_draw_table_t) == 20);
	CHECK(sizeof(vsmoke_frame_t) == 36);
	CHECK(sizeof(vsmoke_map_t) == 44);
	CHECK(sizeof(vsmoke_host_t) == 28);
	CHECK(sizeof(vsmoke_renderer_t) == 80);
	CHECK(offsetof(vsmoke_renderer_t, init) == 20);
	CHECK(offsetof(vsmoke_renderer_t, device_lost) == 72);
}

int main(int argc, char **argv)
{
	wchar_t stubs[MAX_PATH];

	if ( argc < 2 || !MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, stubs, MAX_PATH) )
	{
		printf("usage: test_cl_smoke_core <folder with vsmoke_stub.dll>\n");
		return 2;
	}
	TestInfoValue();
	TestSelectPolicy();
	TestGridPath();
	TestResolution();
	TestValidate();
	TestLoad(stubs);
	TestDirectory();
	TestLayout();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
