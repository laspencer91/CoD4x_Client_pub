"""Exercise the unchanged 1.7 snapshot decoder with compiled sidecar helpers.

Requires pefile and unicorn. Pass the supported iw3mp.exe and the tickrate_replay
DLL built with TICKRATE_TESTS. No game is launched. Player-state reconstruction
is supplied as a dependency fixture; native header, age guards, entity/client
decoders, ring writes and cgame snapshot retrieval execute unmodified.
"""
import argparse
import hashlib
import struct

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP


NATIVE = 0x400000
CL = 0xC5F930
CL_SIZE = 0x1B1BDC
HISTORY = 0x20000000
FRAME_SIZE = 268436
STACK = 0x30000000
MESSAGE = 0x31000000
DATA = MESSAGE + 0x1000
OUTPUT = MESSAGE + 0x10000
DVAR = MESSAGE + 0x40000
IMPORTS = 0x3FFE0000
STOP = 0x3FFF0000
SNAPSHOT_SIZE = 0x2F94
PUBLIC_SIZE = 0x2307C
ENTITIES = CL + 0x105AD0
CLIENTS = CL + 0x17FAD0
SNAPSHOTS = CL + 0x69850


def align(value):
    return (value + 4095) & ~4095


class Replay:
    def __init__(self, executable, helper):
        raw = open(executable, "rb").read()
        if hashlib.sha256(raw).hexdigest() != "f50f8a520581754e73cb7cae67b02adcbad5f2e71aaa412a013bb3ef57a391a5":
            raise ValueError("Expected the pinned large-address-aware CoD4 1.7 executable")
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.load_pe(pefile.PE(data=raw))
        dll = pefile.PE(helper)
        self.load_pe(dll)
        self.exports = {entry.name.decode(): dll.OPTIONAL_HEADER.ImageBase + entry.address
                        for entry in dll.DIRECTORY_ENTRY_EXPORT.symbols if entry.name}
        self.uc.mem_map(HISTORY, align(FRAME_SIZE * 256))
        self.uc.mem_map(STACK, 0x100000)
        self.uc.mem_map(MESSAGE, 0x100000)
        self.uc.mem_map(IMPORTS, 0x20000)
        self.hooks = {}
        for group in dll.DIRECTORY_ENTRY_IMPORT:
            for imp in group.imports:
                address = IMPORTS + len(self.hooks) * 16
                self.hooks[address] = (imp.name or b"ordinal").decode()
                self.u32(imp.address, address)
        self.uc.hook_add(UC_HOOK_CODE, self.hook)

    def load_pe(self, pe):
        self.uc.mem_map(pe.OPTIONAL_HEADER.ImageBase, align(pe.OPTIONAL_HEADER.SizeOfImage))
        self.uc.mem_write(pe.OPTIONAL_HEADER.ImageBase, pe.get_memory_mapped_image())

    def u32(self, address, value):
        self.uc.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))

    def read32(self, address):
        return struct.unpack("<I", self.uc.mem_read(address, 4))[0]

    def ret(self, value=0):
        stack = self.uc.reg_read(UC_X86_REG_ESP)
        target = self.read32(stack)
        self.uc.reg_write(UC_X86_REG_ESP, stack + 4)
        self.uc.reg_write(UC_X86_REG_EAX, value)
        self.uc.reg_write(UC_X86_REG_EIP, target)

    def args(self, count):
        stack = self.uc.reg_read(UC_X86_REG_ESP)
        return struct.unpack("<" + "I" * count, self.uc.mem_read(stack + 4, count * 4))

    def hook(self, uc, address, size, data):
        name = self.hooks.get(address)
        if address in (0x67CBC0, 0x67C4A0) or name in ("memcpy", "memmove", "memset"):
            dest, source, count = self.args(3)
            if address == 0x67C4A0 or name == "memset":
                uc.mem_write(dest, bytes([source & 255]) * count)
            else:
                uc.mem_write(dest, bytes(uc.mem_read(source, count)))
            self.ret(dest)
        elif address == 0x507110:
            # The feature changes neither PS encoding nor prediction. Supply the
            # historical PS here so this test isolates native delta-ring access.
            time, source, dest, flag = self.args(4)
            uc.mem_write(dest, bytes(uc.mem_read(source, 0x2F64)) if source else bytes(0x2F64))
            self.u32(dest, time)
            self.ret()
        elif address in (0x4FCBC0, 0x4FCC10, 0x4FCC80):
            self.ret()
        elif address == 0x4FD330:
            raise AssertionError("Native decoder raised Com_Error")
        elif name:
            raise AssertionError("Unexpected helper dependency: " + name)

    def call(self, entry, *args, eax=0):
        stack = STACK + 0xF0000 - 4 * (len(args) + 1)
        self.uc.mem_write(stack, struct.pack("<" + "I" * (len(args) + 1), STOP, *args))
        self.uc.reg_write(UC_X86_REG_ESP, stack)
        self.uc.reg_write(UC_X86_REG_EAX, eax)
        self.uc.emu_start(entry, STOP, count=4000000)
        return self.uc.reg_read(UC_X86_REG_EAX)

    def helper(self, name, *args):
        return self.call(self.exports[name], *args)


class Bits:
    def __init__(self):
        self.bits = []
        self.previous = -1

    def write(self, value, count=1):
        self.bits.extend((value >> bit) & 1 for bit in range(count))

    def index(self, number, width):
        difference = number - self.previous
        if difference == 1:
            self.write(1)
        else:
            self.write(0)
            if width == 10:
                self.write(difference > 15)
            self.write(number if width != 10 or difference > 15 else difference,
                       width if width != 10 or difference > 15 else 4)
        self.previous = number

    def body(self):
        data = bytearray((len(self.bits) + 7) // 8)
        for bit, value in enumerate(self.bits):
            data[bit // 8] |= value << (bit & 7)
        return bytes(data)


def run_case(replay, age, count, roster, changed):
    uc = replay.uc
    old, current = 300, 300 + age
    uc.mem_write(CL, bytes(CL_SIZE))
    replay.helper("Tickrate_ClearHistory", HISTORY)
    # Snapshot, entity and client layouts are asserted by the native C suite.
    info = bytearray(SNAPSHOT_SIZE)
    struct.pack_into("<7i", info, 0, 1, 0, 10000, old, old - 1, 40, 0)
    struct.pack_into("<i", info, 28, 10000)
    struct.pack_into("<5i", info, 0x2F80, count, roster, 2000, 2040, 17)
    uc.mem_write(CL + 8, bytes(info))
    for number in range(count):
        entity = bytearray(244)
        struct.pack_into("<i", entity, 0, number)
        struct.pack_into("<i", entity, 0xA0, number * 3)
        uc.mem_write(ENTITIES + ((2000 + number) & 2047) * 244, bytes(entity))
    for number in range(roster):
        client = bytearray(100)
        struct.pack_into("<i", client, 0, number)
        struct.pack_into("<i", client, 8, number * 11)
        uc.mem_write(CLIENTS + ((2040 + number) & 2047) * 100, bytes(client))
    assert replay.helper("Tickrate_SaveSnapshot", HISTORY, CL) == 1
    uc.mem_write(ENTITIES, b"\xCC" * (2048 * 244))
    uc.mem_write(CLIENTS, b"\xDD" * (2048 * 100))
    replay.u32(CL + 0x25628, 90001)
    replay.u32(CL + 0x2562C, 17003)
    replay.u32(CL + 0x14, current - 1)
    replay.u32(0x914E1C, current)
    replay.u32(0x914E20, 19)
    replay.u32(0x934E9C, 50)
    replay.u32(0x956E98, 20000)
    for pointer in (0xD5EC424, 0xC5F92C, 0x14C88A8, 0x14E88C0, 0x1435D28):
        replay.u32(pointer, DVAR)

    body = Bits()
    removed = [1, count - 1] if changed and count > 2 else []
    added = count + 3 if changed and count < 1000 else None
    if added is not None:
        # A gamestate baseline already contains its entity number. The native
        # unchanged-baseline codec copies it without rewriting that field.
        replay.u32(CL + 0xC8AD0 + added * 244, added)
    for number in removed:
        body.index(number, 10)
        body.write(1)  # removed
    if added is not None:
        body.index(added, 10)
        body.write(0)  # not removed
        body.write(0)  # unchanged baseline
    body.index(1023, 10)
    body.previous = -1
    client_removed = [1] if changed and roster > 1 else []
    client_added = None
    for number in client_removed:
        body.write(1)
        body.index(number, 6)
        body.write(1)
    if client_added is not None:
        body.write(1)
        body.index(client_added, 6)
        body.write(0)
        body.write(0)
    body.write(0)
    payload = struct.pack("<iBB", 10000 + age * 10, age, 0) + body.body()
    uc.mem_write(DATA, payload)

    def message():
        uc.mem_write(MESSAGE, struct.pack("<10I", 0, 0, DATA, 0, 65536, len(payload), 0, 0, 0, 0xFFFFFFFF))

    # A control with no staged old base is rejected by the actual native parser.
    message()
    replay.call(0x473710, eax=MESSAGE)
    assert replay.read32(MESSAGE) == 1
    assert replay.helper("Tickrate_StageDelta", HISTORY, CL, current, old) == 1
    message()
    replay.call(0x473710, eax=MESSAGE)
    assert replay.read32(MESSAGE) == 0, (age, count, "native overflow")
    assert replay.read32(CL + 0x14) == current
    assert replay.read32(CL + 0x18) == current - age  # native deltaNum agrees with the peeked header
    expected = [number for number in range(count) if number not in removed]
    if added is not None:
        expected.append(added)
    actual_count = replay.read32(CL + 8 + 0x2F80)
    assert actual_count == len(expected), (actual_count, len(expected))
    start = replay.read32(CL + 8 + 0x2F88)
    for position, number in enumerate(expected):
        address = ENTITIES + ((start + position) & 2047) * 244
        assert replay.read32(address) == number, (age, count, changed, position, number, replay.read32(address))
        assert replay.read32(address + 0xA0) == (number * 3 if number < count else 0)
    expected_clients = [number for number in range(roster) if number not in client_removed]
    if client_added is not None:
        expected_clients.append(client_added)
    assert replay.read32(CL + 8 + 0x2F84) == len(expected_clients)
    start = replay.read32(CL + 8 + 0x2F8C)
    for position, number in enumerate(expected_clients):
        address = CLIENTS + ((start + position) & 2047) * 100
        assert replay.read32(address) == number, (age, roster, changed, position, number, replay.read32(address))
        assert replay.read32(address + 8) == (number * 11 if number < roster else 0)
    assert replay.helper("Tickrate_SaveSnapshot", HISTORY, CL) == 1
    uc.mem_write(OUTPUT, bytes(PUBLIC_SIZE * 2))
    assert replay.call(0x45AB90, current, OUTPUT) == 1
    assert replay.helper("Tickrate_GetSnapshot", HISTORY, current, current, OUTPUT + PUBLIC_SIZE) == 1
    assert uc.mem_read(OUTPUT, PUBLIC_SIZE) == uc.mem_read(OUTPUT + PUBLIC_SIZE, PUBLIC_SIZE)
    assert replay.helper("Tickrate_GetSnapshot", HISTORY, current, old, OUTPUT) == 1
    assert replay.read32(OUTPUT + 8) == 10000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable")
    parser.add_argument("helper")
    args = parser.parse_args()
    replay = Replay(args.executable, args.helper)
    cases = 0
    for age in (1, 31, 32, 33, 127, 128, 129, 254, 255):
        for entities, clients, changed in ((20, 4, False), (20, 4, True), (1023, 64, False)):
            run_case(replay, age, entities, clients, changed)
            cases += 1
    replay.helper("Tickrate_ClearHistory", HISTORY)
    assert replay.helper("Tickrate_GetSnapshot", HISTORY, 555, 300, OUTPUT) == 0
    print(f"Native CoD4 snapshot replay: {cases} cases passed; unchanged decode, removal/spawn, roster, dense rings, age 255, cgame parity and reset")


if __name__ == "__main__":
    main()
