/* Execute the shipped NASM technique stubs against a harmless return trampoline. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

uint32_t smoke_test_snapshot[8], smoke_test_flags;
unsigned char cl_smoke_sceneDepth;
void CL_Smoke_Composite(void) {}
void CL_Smoke_Prepare(void) {}
void CL_Smoke_ParticlesBegin(void) {}
void CL_Smoke_ParticlesEnd(void) {}
void CL_Smoke_FxSpawn(void) {}
void CL_Smoke_WorldBullet(void) {}
void CL_Smoke_SimulatedBullet(void) {}
void Smoke_TestTechnique(int alternate, const void *technique);

int main(void)
{
    unsigned checks = 0;
    for (int alternate = 0; alternate < 2; ++alternate)
    {
        for (uint32_t flags = 0; flags < 65536; ++flags)
        {
            const uint32_t technique[] = {0, flags};
            Smoke_TestTechnique(alternate, technique);
            if (smoke_test_flags != flags || smoke_test_snapshot[0] != flags ||
                smoke_test_snapshot[1] != (uint32_t)(uintptr_t)technique ||
                smoke_test_snapshot[2] != 0x12345678 || smoke_test_snapshot[3] != 0x87654321 ||
                !!(smoke_test_snapshot[4] & 0x40) != !(flags & 1) ||
                smoke_test_snapshot[5] != 0x11223344 || smoke_test_snapshot[6] != 0x55667788)
            { printf("FAIL technique=%u alternate=%d\n", flags, alternate); return 1; }
            ++checks;
        }
    }
    printf("%u technique hook cases passed: tags, return flags, registers and SSE preserved\n", checks);
    return 0;
}
