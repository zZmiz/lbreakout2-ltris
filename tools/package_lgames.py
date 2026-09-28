
"""Package three relocated UF2 images; reject overlapping/out-of-slot payloads.

The resident emuLoader sees the selector's LGames binary info at 0x10080000.
Padding gaps with erased bytes makes its contiguous flash CRC reproducible.
"""
import argparse
import hashlib
import struct
from pathlib import Path

MAGIC0, MAGIC1, MAGICEND = 0x0A324655, 0x9E5D5157, 0x0AB16F30
FAMILY = 0xE48BFF59  # RP2350 ARM Secure
BASE, SLOT = 0x10080000, 0x100000

def read_image(path, base):
    data = Path(path).read_bytes()
    if not data or len(data) % 512:
        raise ValueError(f"{path}: invalid UF2 length")
    pages = {}
    expected_count = None
    for off in range(0, len(data), 512):
        block = data[off:off + 512]
        m0, m1, flags, addr, size, num, count, family = struct.unpack_from('<8I', block)
        if (m0, m1, struct.unpack_from('<I', block, 508)[0]) != (MAGIC0, MAGIC1, MAGICEND):
            raise ValueError(f"{path}: invalid magic")
        # picotool prepends the RP2350-E10 absolute ignore block. The resident
        # loader skips this family too; it is not part of the ARM image.
        if (flags == 0xa000 and family == 0xe48bff57 and size == 256 and
                num == 0 and count == 2 and block[32:288] == b'\xef' * 256 and
                struct.unpack_from('<I', block, 288)[0] == 0x9957e304):
            continue
        if flags != 0x2000 or family != FAMILY or size != 256:
            raise ValueError(f"{path}: unsupported flags/family/payload: {flags:x}/{family:x}/{size}")
        if addr % 256 or addr < base or addr + size > base + SLOT or addr in pages:
            raise ValueError(f"{path}: invalid/duplicate/out-of-slot address {addr:x}")
        if expected_count is None:
            expected_count = count
        if num != len(pages) or count != expected_count:
            raise ValueError(f"{path}: invalid block sequence")
        pages[addr] = block[32:32 + size]
    if len(pages) != expected_count:
        raise ValueError(f"{path}: missing blocks")
    if base not in pages:
        raise ValueError(f"{path}: missing vector table")
    sp, pc = struct.unpack_from('<II', pages[base])
    if not (0x20000000 <= sp <= 0x20082000 and sp % 8 == 0 and
            base <= pc < base + SLOT and pc & 1):
        raise ValueError(f"{path}: invalid vectors {sp:x}/{pc:x}")
    return pages

def main():
    ap = argparse.ArgumentParser()
    for name in ('selector', 'ltris', 'lbreakout2', 'output'):
        ap.add_argument('--' + name, required=True)
    args = ap.parse_args()
    pages = {}
    for i, name in enumerate(('selector', 'ltris', 'lbreakout2')):
        pages.update(read_image(getattr(args, name), BASE + SLOT * i))
    count = (max(pages) - BASE) // 256 + 1
    result = bytearray()
    for n in range(count):
        addr = BASE + n * 256
        result += struct.pack('<8I', MAGIC0, MAGIC1, 0x2000, addr, 256, n, count, FAMILY)
        result += pages.get(addr, b'\xff' * 256) + bytes(220) + struct.pack('<I', MAGICEND)
    Path(args.output).write_bytes(result)
    print(f"LGames bundle: {count} blocks, flash [{BASE:#x}, {BASE+count*256:#x}), "
          f"SHA256={hashlib.sha256(result).hexdigest()}")

if __name__ == '__main__':
    main()
