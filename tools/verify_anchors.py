#!/usr/bin/env python3
"""Verify Ddraw.dll / EfzRevival.dll anchors for the EFZ DisplayManager port.

Usage:
  python tools/verify_anchors.py dd <path-to-Ddraw.dll> [--profile split|orig]
  python tools/verify_anchors.py imports <path-to-EfzRevival.dll>

Profiles encode the RVA anchors from EFZ_DisplayManager_Feasibility_2026-10-20.md
section 5 ("Build-specific anchors"). The script fails loudly when the file
hash does not match the profile: anchors taken from the wrong build would be
meaningless. RVAs are relative to the loaded image base (preferred base
0x64C00000 in both audited J wrappers); the script prints absolute VAs as
base+RVA for cross-checking against disassembler output.
"""

import argparse
import hashlib
import struct
import sys

import capstone
import pefile

PROFILES = {
    "split": {
        "name": "renderer-split Revival 1.02j (2026-08-02)",
        "sha256": "0e072342d87d240e18969bc3663032e0e370eaba24d1fbb8e6c1f6216ad9f603",
        "flip": 0x27860,
        "native_global": 0xDF284,
        "device_global": 0xDF2C4,
        "pp_global": 0xDF28C,
        "conv_blit": 0x27928,
        "final_stretch": 0x279C0,
        "present": 0x279FC,
        "reset_helper": 0x363D0,
        "device_reset": 0x36441,
        "timer_export": 0xCF790,
        "branch_target": 0x27A20,
    },
    "orig": {
        "name": "original Revival 1.02j (MinGW)",
        "sha256": "e18abe007fbb7c48b63ab56027754e0aee196dc753af189949ba2dc46f366f9d",
        "flip": 0x277A0,
        "native_global": 0xDF284,
        "device_global": 0xDF2C4,
        "pp_global": 0xDF28C,
        "conv_blit": 0x27868,
        "final_stretch": 0x27900,
        "present": 0x2793C,
        "reset_helper": 0x36220,
        "device_reset": 0x36291,
        "timer_export": 0xCF4E0,
        "branch_target": 0x27960,
    },
}


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def rva_to_offset(pe, rva):
    for section in pe.sections:
        if section.contains_rva(rva):
            return section.get_offset_from_rva(rva)
    raise ValueError(f"RVA {rva:#x} not inside any section")


def disasm(pe, data, rva, count, label, md=None):
    if md is None:
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        md.detail = True
    off = rva_to_offset(pe, rva)
    base_va = pe.OPTIONAL_HEADER.ImageBase + rva
    print(f"\n--- {label} (RVA {rva:#x}, VA {base_va:#x}) ---")
    for insn in md.disasm(data[off:off + count * 16], base_va, count=count):
        target = ""
        # Annotate absolute memory references that hit profile globals.
        for op in insn.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                target += f"   ; abs {op.mem.disp & 0xFFFFFFFF:#010x} (RVA {op.mem.disp - pe.OPTIONAL_HEADER.ImageBase:#x})"
        print(f"  {insn.address - pe.OPTIONAL_HEADER.ImageBase:#08x}: {insn.bytes.hex():<20} {insn.mnemonic} {insn.op_str}{target}")


def check(profile, pe, data):
    ok = True
    image_base = pe.OPTIONAL_HEADER.ImageBase
    print(f"image base: {image_base:#x}  PE timestamp: {pe.FILE_HEADER.TimeDateStamp:#x}")

    print("\n=== exports ===")
    exports = {}
    for exp in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if exp.name:
            exports[exp.name.decode()] = exp.address
    for name, rva in sorted(exports.items(), key=lambda kv: kv[1]):
        marker = "  <-- profile timer export" if rva == profile["timer_export"] else ""
        print(f"  {rva:#08x} {name}{marker}")
    if profile["timer_export"] not in exports.values():
        print(f"  !! timer export RVA {profile['timer_export']:#x} not found in exports")
        ok = False

    # The documented globals: verify some code near Flip references each.
    globals_to_find = {
        "native_surface": profile["native_global"],
        "device_ptr": profile["device_global"],
        "present_params": profile["pp_global"],
    }
    window_start = profile["flip"]
    window_len = profile["present"] - profile["flip"] + 0x40
    off = rva_to_offset(pe, window_start)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    found = {k: [] for k in globals_to_find}
    for insn in md.disasm(data[off:off + window_len * 2], image_base + window_start):
        for op in insn.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                rva_ref = (op.mem.disp - image_base) & 0xFFFFFFFF
                for label, g in globals_to_find.items():
                    if rva_ref == g:
                        found[label].append(insn.address - image_base)

    print("\n=== global references between Flip and Present ===")
    for label, g in globals_to_find.items():
        refs = ", ".join(f"{r:#x}" for r in found[label]) or "NONE"
        status = "ok" if found[label] else "!! MISSING"
        print(f"  {label:<16} RVA {g:#x}: referenced at {refs}  [{status}]")
        if not found[label]:
            ok = False

    print("\n=== disassembly ===")
    disasm(pe, data, profile["flip"], 90, "Flip entry")
    disasm(pe, data, profile["final_stretch"], 8, "final StretchRect call")
    disasm(pe, data, profile["present"], 4, "device Present call")
    if profile.get("branch_target"):
        disasm(pe, data, profile["branch_target"], 110, "geometry branch target")
    disasm(pe, data, profile["reset_helper"], 60, "reset helper")
    disasm(pe, data, profile["timer_export"], 12, "getEfzTimer export")
    if profile.get("conv_blit"):
        disasm(pe, data, profile["conv_blit"], 4, "conversion blit")

    # Vtable-offset summary for the indirect calls at the three call sites.
    print("\n=== indirect call check ===")
    for label, rva in (("final StretchRect", profile["final_stretch"]),
                       ("device Present", profile["present"]),
                       ("device Reset", profile["device_reset"])):
        off2 = rva_to_offset(pe, rva)
        insns = list(md.disasm(data[off2:off2 + 16], image_base + rva, count=1))
        if insns and insns[0].mnemonic in ("call", "jmp"):
            print(f"  {label:<18} RVA {rva:#x}: {insns[0].mnemonic} {insns[0].op_str}")
        else:
            print(f"  {label:<18} RVA {rva:#x}: !! not an indirect call (got "
                  f"{insns[0].mnemonic + ' ' + insns[0].op_str if insns else 'no decode'})")
            ok = False

    return ok


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    pdd = sub.add_parser("dd")
    pdd.add_argument("path")
    pdd.add_argument("--profile", choices=sorted(PROFILES), default="split")
    pim = sub.add_parser("imports")
    pim.add_argument("path")
    args = ap.parse_args()

    if args.cmd == "imports":
        pe = pefile.PE(args.path, fast_load=False)
        print(f"file: {args.path}")
        print(f"SHA-256: {sha256_file(args.path)}")
        for entry in pe.DIRECTARY_ENTRY_IMPORT if hasattr(pe, "DIRECTARY_ENTRY_IMPORT") else pe.DIRECTORY_ENTRY_IMPORT:
            print(f"\n[{entry.dll.decode()}]")
            for imp in entry.imports:
                print(f"  {imp.address - pe.OPTIONAL_HEADER.ImageBase:#08x}  {imp.name.decode() if imp.name else f'ord{imp.ordinal}'}")
        return

    profile = PROFILES[args.profile]
    actual = sha256_file(args.path)
    print(f"file: {args.path}")
    print(f"profile: {profile['name']}")
    print(f"SHA-256: {actual}")
    if actual != profile["sha256"]:
        print(f"expected: {profile['sha256']}")
        print("HASH MISMATCH - refusing to check anchors against the wrong binary")
        sys.exit(2)
    print("hash ok")

    pe = pefile.PE(args.path, fast_load=False)
    data = open(args.path, "rb").read()
    ok = check(profile, pe, data)
    print(f"\nRESULT: {'PASS' if ok else 'FAIL'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
