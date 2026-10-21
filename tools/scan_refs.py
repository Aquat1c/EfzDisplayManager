#!/usr/bin/env python3
"""Cross-reference and data inspection for the Revival Ddraw wrapper.

Subcommands:
  refs   <dll> <rva> [<rva>...]   find every byte-pattern reference to base+RVA
                                  (code references get an aligned decode)
  data   <dll> <rva> [len]        hex dump of raw initialized bytes at RVA
  vtable <dll> <funcRva> [slots]  locate the data vtable containing base+funcRva
                                  and dump its slots as VAs
  func   <dll> <rva> [count]      plain disassembly of N instructions at RVA

Address arguments are RVAs relative to the preferred image base.
"""

import argparse
import struct
import sys

import capstone
import pefile

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
MD.detail = True


def load(path):
    pe = pefile.PE(path, fast_load=False)
    data = open(path, "rb").read()
    return pe, data


def rva_to_offset(pe, rva):
    for s in pe.sections:
        if s.contains_rva(rva):
            return s.get_offset_from_rva(rva)
    raise ValueError(f"RVA {rva:#x} outside all sections")


def section_of(pe, rva):
    for s in pe.sections:
        if s.contains_rva(rva):
            return s.Name.decode().rstrip("\0")
    return "?"


def align_decode(data, file_off, va):
    """Decode the instruction covering file_off by trying backward starts.

    x86 back-decoding is ambiguous; prefer starts closest to the target whose
    decode chain reaches it exactly. Returns (start_va, [insns]) with the final
    instruction covering the target, or None.
    """
    best = None
    for back in range(1, 17):
        start = file_off - back
        if start < 0:
            break
        chain = []
        ok = False
        for insn in MD.disasm(data[start:start + back + 16], va - back):
            chain.append(insn)
            if insn.address <= va < insn.address + insn.size:
                ok = True
                break
            if insn.address > va:
                break
        if ok and (best is None or back > best[0]):
            best = (back, chain)
    return best[1] if best else None


def cmd_refs(args):
    pe, data = load(args.dll)
    base = pe.OPTIONAL_HEADER.ImageBase
    for rva in args.rvas:
        va = base + rva
        pat = struct.pack("<I", va)
        print(f"### references to RVA {rva:#x} (VA {va:#x})")
        hits = []
        start = 0
        while True:
            i = data.find(pat, start)
            if i < 0:
                break
            hits.append(i)
            start = i + 1
        if not hits:
            print("  none")
            continue
        for i in hits:
            # file offset -> rva
            hit_rva = None
            for s in pe.sections:
                fo, fs = s.PointerToRawData, s.SizeOfRawData
                if fo <= i < fo + fs:
                    hit_rva = s.VirtualAddress + (i - fo)
                    break
            sec = section_of(pe, hit_rva) if hit_rva is not None else "?"
            print(f"\n  hit at RVA {hit_rva:#x} ({sec})" if hit_rva is not None else f"\n  hit at file+{i:#x} (unmapped)")
            if sec in (".text", ".rdata", ".data") and hit_rva is not None:
                sec_flags = next((s for s in pe.sections if s.contains_rva(hit_rva)), None)
                is_code = sec_flags and (sec_flags.Characteristics & 0x20000000)
                if is_code:
                    chain = align_decode(data, i, base + hit_rva)
                    if chain:
                        for insn in chain:
                            mark = "  <-- reference" if insn.address <= base + hit_rva < insn.address + insn.size else ""
                            print(f"    {insn.address - base:#08x}: {insn.mnemonic} {insn.op_str}{mark}")
                    else:
                        print(f"    (no clean alignment; raw {data[i-4:i+8].hex()})")
        print()


def cmd_data(args):
    pe, data = load(args.dll)
    off = rva_to_offset(pe, args.rva)
    chunk = data[off:off + args.len]
    for row in range(0, len(chunk), 16):
        hexs = " ".join(f"{b:02x}" for b in chunk[row:row + 16])
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk[row:row + 16])
        print(f"  {args.rva + row:#08x}: {hexs:<48} {asc}")


def cmd_vtable(args):
    pe, data = load(args.dll)
    base = pe.OPTIONAL_HEADER.ImageBase
    va = base + args.func_rva
    pat = struct.pack("<I", va)
    start = 0
    while True:
        i = data.find(pat, start)
        if i < 0:
            break
        start = i + 1
        hit_rva = None
        for s in pe.sections:
            fo, fs = s.PointerToRawData, s.SizeOfRawData
            if fo <= i < fo + fs:
                hit_rva = s.VirtualAddress + (i - fo)
                break
        if hit_rva is None or section_of(pe, hit_rva) not in (".rdata", ".data"):
            continue
        print(f"### vtable candidate containing VA {va:#x} at RVA {hit_rva:#x} ({section_of(pe, hit_rva)})")
        # Walk back to the start of the run of code pointers.
        run_off = i
        while True:
            prev = run_off - 4
            if prev < 0:
                break
            v = struct.unpack("<I", data[prev:prev + 4])[0]
            if base <= v < base + pe.OPTIONAL_HEADER.SizeOfImage and section_of(pe, v - base) == ".text":
                run_off = prev
            else:
                break
        slot0_rva = hit_rva - (i - run_off)
        for slot in range(args.slots):
            off = rva_to_offset(pe, slot0_rva) + slot * 4
            v = struct.unpack("<I", data[off:off + 4])[0]
            tag = " <-- searched function" if v == va else ""
            kind = ""
            if base <= v < base + pe.OPTIONAL_HEADER.SizeOfImage:
                kind = section_of(pe, v - base)
            print(f"  slot {slot:2d} (+{slot*4:#06x}): {v:#010x} ({kind}){tag}")
        print()


def cmd_func(args):
    pe, data = load(args.dll)
    base = pe.OPTIONAL_HEADER.ImageBase
    off = rva_to_offset(pe, args.rva)
    for insn in MD.disasm(data[off:off + args.count * 16], base + args.rva, count=args.count):
        ann = ""
        for op in insn.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                ann = f"   ; RVA {op.mem.disp - base:#x}"
        print(f"  {insn.address - base:#08x}: {insn.mnemonic} {insn.op_str}{ann}")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("refs")
    p.add_argument("dll")
    p.add_argument("rvas", nargs="+", type=lambda x: int(x, 0))
    p = sub.add_parser("data")
    p.add_argument("dll")
    p.add_argument("rva", type=lambda x: int(x, 0))
    p.add_argument("len", nargs="?", type=lambda x: int(x, 0), default=64)
    p = sub.add_parser("vtable")
    p.add_argument("dll")
    p.add_argument("func_rva", type=lambda x: int(x, 0))
    p.add_argument("slots", nargs="?", type=int, default=40)
    p = sub.add_parser("func")
    p.add_argument("dll")
    p.add_argument("rva", type=lambda x: int(x, 0))
    p.add_argument("count", nargs="?", type=int, default=40)
    args = ap.parse_args()
    {"refs": cmd_refs, "data": cmd_data, "vtable": cmd_vtable, "func": cmd_func}[args.cmd](args)


if __name__ == "__main__":
    main()
