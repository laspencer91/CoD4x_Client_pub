"""Compile the real disk-exec function against a map-resetting temporary allocator."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'gcc'))
    args = parser.parse_args()
    source = (Path(__file__).resolve().parents[1] / 'src/cmd.c').read_text(encoding='utf-8')
    function = source[source.index('qboolean Cmd_ExecFile('):source.index('qboolean Cmd_ExecUserdir(')]
    fixture = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
typedef int qboolean;
#define CON_CHANNEL_SYSTEM 0
#define CON_CHANNEL_DONT_FILTER 0
static char temporary[256];
static const char *script;
static int alive, missing, no_memory, frees, dispatches, failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
static int FS_ReadFile(const char *path, void **buffer) {
    (void)path;
    if (missing) { *buffer = NULL; return -1; }
    strcpy(temporary, script); *buffer = temporary; alive = 1;
    return (int)strlen(script);
}
static void FS_FreeFile(void *buffer) {
    CHECK(buffer == temporary && alive);
    ++frees; alive = 0; memset(temporary, 0xdd, sizeof(temporary));
}
static void Com_Printf(int channel, const char *format, ...) { (void)channel; (void)format; }
static void Com_PrintError(int channel, const char *format, ...) { (void)channel; (void)format; }
static void Cbuf_ExecuteBuffer(int client, int controller, const char *commands) {
    (void)client; (void)controller;
    CHECK(frees == 1); CHECK(strcmp(commands, script) == 0);
    ++dispatches;
    /* A map load discards temporary hunk storage before exec returns. */
    alive = 0; memset(temporary, 0xdd, sizeof(temporary));
}
static void *fixture_malloc(size_t size) { return no_memory ? NULL : malloc(size); }
#define malloc fixture_malloc
FUNCTION
#undef malloc
static void reset(const char *text) {
    script = text; alive = missing = no_memory = frees = dispatches = 0;
}
int main(void) {
    reset("sets sv_example v1\ndevmap mp_crash\n");
    CHECK(Cmd_ExecFile("map.cfg") == 1); CHECK(frees == 1 && dispatches == 1);
    reset("");
    CHECK(Cmd_ExecFile("empty.cfg") == 1); CHECK(frees == 1 && dispatches == 1);
    reset(""); missing = 1;
    CHECK(Cmd_ExecFile("missing.cfg") == 0); CHECK(frees == 0 && dispatches == 0);
    reset("devmap mp_crash\n"); no_memory = 1;
    CHECK(Cmd_ExecFile("allocation.cfg") == 0); CHECK(frees == 1 && dispatches == 0);
    printf("Disk exec: map-reset ownership, empty/missing files and allocation failure: %d failures\n", failures);
    return failures != 0;
}
'''.replace('FUNCTION', function)
    with tempfile.TemporaryDirectory(prefix='cod4x-cmd-exec-') as directory:
        path = Path(directory)
        (path / 'fixture.c').write_text(fixture, encoding='utf-8')
        executable = path / ('fixture.exe' if os.name == 'nt' else 'fixture')
        subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Wno-unused-function',
                        str(path / 'fixture.c'), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
