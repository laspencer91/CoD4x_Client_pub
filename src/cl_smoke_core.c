/*
 * Volumetric smoke bridge: helpers without engine dependencies.
 */
#include "cl_smoke_core.h"

#include <stdio.h>
#include <string.h>
#include "tomcrypt/tomcrypt.h"

int VSmoke_InfoValue(const char *info, const char *key, char *value, size_t valueSize)
{
	const char *cursor = info;
	size_t keyLength;
	int found = 0;

	if ( !info || !key || !value || !valueSize )
	{
		return -1;
	}
	value[0] = '\0';
	keyLength = strlen(key);
	if ( *cursor == '\\' )
	{
		++cursor;
	}
	while ( *cursor )
	{
		const char *keyEnd = strchr(cursor, '\\');
		const char *valueStart, *valueEnd;
		if ( !keyEnd )
		{
			return -1; /* key without a value */
		}
		valueStart = keyEnd + 1;
		valueEnd = strchr(valueStart, '\\');
		if ( !valueEnd )
		{
			valueEnd = valueStart + strlen(valueStart);
		}
		if ( (size_t)(keyEnd - cursor) == keyLength && !strncmp(cursor, key, keyLength) )
		{
			size_t length = (size_t)(valueEnd - valueStart);
			if ( found || length >= valueSize )
			{
				value[0] = '\0';
				return -1;
			}
			memcpy(value, valueStart, length);
			value[length] = '\0';
			found = 1;
		}
		if ( !*valueEnd )
		{
			break;
		}
		cursor = valueEnd + 1;
	}
	return found;
}

int VSmoke_SelectPolicy(const char *systeminfo, const char *serverinfo, char *policy, size_t policySize)
{
	int result = VSmoke_InfoValue(systeminfo ? systeminfo : "", VSMOKE_POLICY_KEY, policy, policySize);
	if ( result == 0 )
	{
		result = VSmoke_InfoValue(serverinfo ? serverinfo : "", VSMOKE_POLICY_KEY, policy, policySize);
	}
	if ( result != 1 || !policy[0] )
	{
		if ( policySize )
		{
			policy[0] = '\0';
		}
		return 0;
	}
	return 1;
}

int VSmoke_GridPath(const char *bspName, char *path, size_t pathSize)
{
	static const char prefix[] = "maps/mp/";
	static const char suffix[] = ".d3dbsp";
	static const char grid[] = ".smokegrid";
	size_t length, stem, i;

	if ( !bspName || !path || !pathSize )
	{
		return 0;
	}
	path[0] = '\0';
	length = strlen(bspName);
	if ( length <= sizeof(prefix) - 1 + sizeof(suffix) - 1 || _strnicmp(bspName, prefix, sizeof(prefix) - 1)
		|| _strnicmp(bspName + length - (sizeof(suffix) - 1), suffix, sizeof(suffix) - 1) )
	{
		return 0;
	}
	stem = length - (sizeof(suffix) - 1);
	for ( i = sizeof(prefix) - 1; i < stem; ++i )
	{
		const char c = bspName[i];
		if ( !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') )
		{
			return 0; /* no further folders, dots or path tricks */
		}
	}
	if ( stem + sizeof(grid) > pathSize )
	{
		return 0;
	}
	memcpy(path, bspName, stem);
	memcpy(path + stem, grid, sizeof(grid));
	return 1;
}

int VSmoke_ResolutionDivisor(int index)
{
	return index == 0 ? 1 : index == 2 ? 4 : 2;
}

int VSmoke_RendererDirectory(HMODULE module, wchar_t *directory, size_t directoryCount)
{
	wchar_t path[MAX_PATH];
	DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
	wchar_t *slash;

	if ( !length || length >= MAX_PATH )
	{
		return 0;
	}
	slash = wcsrchr(path, L'\\');
	if ( !slash )
	{
		return 0;
	}
	slash[1] = L'\0';
	if ( wcslen(path) + wcslen(VSMOKE_DIRECTORY) + 1 > directoryCount )
	{
		return 0;
	}
	wcscpy(directory, path);
	wcscat(directory, VSMOKE_DIRECTORY);
	return 1;
}

static int VSmoke_LibraryPath(const wchar_t *directory, wchar_t *path, size_t count)
{
	if ( !directory || wcslen(directory) + 1 + wcslen(VSMOKE_LIBRARY) + 1 > count )
	{
		return 0;
	}
	wcscpy(path, directory);
	wcscat(path, L"\\");
	wcscat(path, VSMOKE_LIBRARY);
	return 1;
}

int VSmoke_RendererPresent(const wchar_t *directory)
{
	wchar_t path[MAX_PATH];
	DWORD attributes;

	if ( !VSmoke_LibraryPath(directory, path, MAX_PATH) )
	{
		return 0;
	}
	attributes = GetFileAttributesW(path);
	return attributes != INVALID_FILE_ATTRIBUTES
		&& !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}

int VSmoke_FileSha256(const wchar_t *path, char hex[65])
{
	unsigned char buffer[16384], digest[32];
	hash_state state;
	HANDLE file;
	DWORD count;
	int ok = 1, i;

	hex[0] = '\0';
	file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if ( file == INVALID_HANDLE_VALUE )
	{
		return 0;
	}
	sha256_init(&state);
	for ( ;; )
	{
		if ( !ReadFile(file, buffer, sizeof(buffer), &count, NULL) )
		{
			ok = 0;
			break;
		}
		if ( !count )
		{
			break;
		}
		sha256_process(&state, buffer, count);
	}
	CloseHandle(file);
	if ( !ok || sha256_done(&state, digest) != CRYPT_OK )
	{
		return 0;
	}
	for ( i = 0; i < 32; ++i )
	{
		sprintf(hex + i * 2, "%02x", digest[i]);
	}
	return 1;
}

int VSmoke_ValidateRenderer(const vsmoke_renderer_t *renderer, char *reason, size_t reasonSize)
{
	const char *problem = NULL;

	if ( !renderer )
	{
		problem = "no renderer table";
	}
	else if ( renderer->api_version != VSMOKE_API_VERSION )
	{
		problem = "renderer was built for another interface version";
	}
	else if ( renderer->size < sizeof(vsmoke_renderer_t) )
	{
		problem = "renderer table is too small";
	}
	else if ( !renderer->init || !renderer->set_map )
	{
		problem = "renderer lacks init or set_map";
	}
	else if ( (renderer->flags & VSMOKE_WANTS_PARTICLE_DRAWS) && (!renderer->particles_begin || !renderer->particles_end) )
	{
		problem = "renderer asks for particle draws without particles_begin/end";
	}
	if ( problem && reason && reasonSize )
	{
		snprintf(reason, reasonSize, "%s", problem);
	}
	return problem == NULL;
}

const vsmoke_renderer_t *VSmoke_LoadRenderer(const wchar_t *directory, const char *pinnedSha256,
	HMODULE *module, char *reason, size_t reasonSize)
{
	wchar_t path[MAX_PATH];
	vsmoke_get_renderer_t getRenderer;
	const vsmoke_renderer_t *renderer;
	HMODULE library;
	FARPROC procedure;

	*module = NULL;
	if ( !VSmoke_LibraryPath(directory, path, MAX_PATH) )
	{
		snprintf(reason, reasonSize, "renderer path too long");
		return NULL;
	}
	if ( pinnedSha256 && pinnedSha256[0] )
	{
		char hex[65];
		if ( !VSmoke_FileSha256(path, hex) )
		{
			snprintf(reason, reasonSize, "renderer could not be read");
			return NULL;
		}
		if ( _stricmp(hex, pinnedSha256) )
		{
			snprintf(reason, reasonSize, "renderer SHA-256 %s does not match this client build", hex);
			return NULL;
		}
	}
	library = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if ( !library )
	{
		snprintf(reason, reasonSize, "renderer failed to load (error %lu)", (unsigned long)GetLastError());
		return NULL;
	}
	procedure = GetProcAddress(library, VSMOKE_EXPORT_NAME);
	memcpy(&getRenderer, &procedure, sizeof(getRenderer));
	renderer = getRenderer ? getRenderer(VSMOKE_API_VERSION) : NULL;
	if ( !getRenderer )
	{
		snprintf(reason, reasonSize, "renderer does not export " VSMOKE_EXPORT_NAME);
	}
	else if ( VSmoke_ValidateRenderer(renderer, reason, reasonSize) )
	{
		*module = library;
		return renderer;
	}
	FreeLibrary(library);
	return NULL;
}
