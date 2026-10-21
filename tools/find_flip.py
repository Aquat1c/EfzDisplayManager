#!/usr/bin/env python3
"""Locate the Revival DDRAW wrapper's Flip architecture in an arbitrary build.

Strategy (no symbols needed):
  1. Scan .text for `call dword ptr [reg+0x48]` (GetBackBuffer, vtable 18)
     followed within the same function by `call dword ptr [reg+0x88]`
     (StretchRect, vtable 34) and later `call dword ptr [reg+0x44]`
     (Present, vtable 17). The enclosing function is the Flip path.
  2. The FINAL StretchRect (the one after the text/box overlay calls and the
     mode branch) is our hook site; verify the mode branches converge there.
  3. Collect every absolute global referenced inside Flip; cluster them to
     find the renderer-object block (staging, native, PP, device, hwnd,
     filter, borderless, font).

Usage: python tools/find_flip.py <DDRAW.dll>
"""

import sys

import capstone
import pefile

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True


def rva_to_offset(pe, rva):
    for s in pe.sections:
        if s.contains_rva(rva):
            return s.get_offset_from_rva(rva)
    return None


def find_code_runs(pe, data):
    text = next(s for s in pe.sections if b".text" in s.Name)
    return text, data[text.PointerToRawData:text.PointerToRawData + text.SizeOfRawData], text.VirtualAddress


def disasm_window(data, base, start_rva, max_bytes=0x400):
    off = rva_to_offset(pe, start_rva)
    if off is None:
        return []
    return list(md.disasm(data[off:off + max_bytes], base + start_rva))


def function_bounds_via_ret_scan(data, base, text_va, target_rva):
    # Walk backward from target to a "ret" preceded by padding (int3/nop) or
    # to the section start; approximate function start.
    off = rva_to_offset(pe, target_rva)
    i = off
    while i > 0x400:
        # look backwards for ret 0xC (Flip signature) or aligned int3 pad
        if data[i] in (0xCC,) and data[i - 1] in (0xCC, 0x90):
            # scan forward past padding to first real byte
            j = i
            while data[j] in (0xCC, 0x90):
                j += 1
            return text_va + (j - rva_to_offset(pe, text_va) + 0) if False else None
        i -= 1
    return None


def main(path):
    global pe
    pe = pefile.PE(path)
    data = open(path, "rb").read()
    base = pe.OPTIONAL_HEADER.ImageBase
    text, raw, tva = find_code_runs(pe, data)

    # 1) find candidate StretchRect call sites: ff /2 with mod=01 disp8 0x88
    #    (call [reg+0x88]) or disp32 form ff 90 88 00 00 00
    stretch_sites = []
    for i in range(len(raw) - 6):
        if raw[i] == 0xFF and 0x90 <= raw[i + 1] <= 0x97 and raw[i + 2:i + 6] == b"\x88\x00\x00\x00":
            stretch_sites.append(tva + i)
        if raw[i] == 0xFF and 0x50 <= raw[i + 1] <= 0x57 and raw[i + 2] == 0x88:
            stretch_sites.append(tva + i)  # disp8 variant ff 5x 88
    # 2) GetBackBuffer sites (call [reg+0x48])
    gbb_sites = []
    for i in range(len(raw) - 6):
        if raw[i] == 0xFF and 0x90 <= raw[i + 1] <= 0x97 and raw[i + 2:i + 6] == b"\x48\x00\x00\x00":
            gbb_sites.append(tva + i)
        if raw[i] == 0xFF and 0x50 <= raw[i + 1] <= 0x57 and raw[i + 2] == 0x48:
            gbb_sites.append(tva + i)
    # 3) Present sites (call [reg+0x44])
    present_sites = []
    for i in range(len(raw) - 6):
        if raw[i] == 0xFF and 0x90 <= raw[i + 1] <= 0x97 and raw[i + 2:i + 6] == b"\x44\x00\x00\x00":
            present_sites.append(tva + i)
        if raw[i] == 0xFF and 0x50 <= raw[i + 1] <= 0x57 and raw[i + 2] == 0x44:
            present_sites.append(tva + i)

    print(f"{path}")
    print(f"  stretch candidates: {[hex(s) for s in stretch_sites]}")
    print(f"  getbackbuffer candidates: {[hex(s) for s in gbb_sites]}")
    print(f"  present candidates: {[hex(s) for s in present_sites]}")

    # Pair them: a Flip has GBB < STRETCH < PRESENT within ~0x300 bytes
    flips = []
    for st in stretch_sites:
        gbb = [g for g in gbb_sites if 0 < st - g < 0x200]
        pre = [p for p in present_sites if 0 < p - st < 0x200]
        if gbb and pre:
            flips.append((gbb[0], st, pre[0]))
    print(f"  flip-pattern (gbb, stretch, present): {[(hex(a),hex(b),hex(c)) for a,b,c in flips]}")

    for gbb, st, pre in flips:
        # Function start: scan back from GBB for prologue push ebp / sub esp / and esp
        off_gbb = rva_to_offset(pe, gbb)
        start = None
        for back in range(0x400):
            i = off_gbb - back
            if i <= 0:
                break
            # common MSVC/MinGW prologues
            if raw[i - 1] == 0xCC and raw[i] != 0xCC:
                start = tva + i
                break
        if start is None:
            start = gbb - 0x300
        print(f"\n  --- flip window func~{hex(start)} ---")
        # globals referenced between start and present
        seen = {}
        for insn in md.disasm(raw[rva_to_offset(pe, start) - text.PointerToRawData:
                                 rva_to_offset(pe, pre) + 8 - text.PointerToRawData],
                              base + start):
            for op in insn.operands:
                if op.type == capstone.x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                    rva_ref = (op.mem.disp - base) & 0xFFFFFFFF
                    if 0 < rva_ref < pe.OPTIONAL_HEADER.SizeOfImage:
                        seen.setdefault(rva_ref, []).append(insn.address - base)
        for rva_ref in sorted(seen):
            refs = ",".join(hex(a) for a in seen[rva_ref][:4])
            print(f"    global RVA {rva_ref:#08x} refs@{refs}")


if __name__ == "__main__":
    main(sys.argv[1])
