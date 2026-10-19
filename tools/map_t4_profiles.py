"""Read-only helper for comparing the exact T4 SP and MP executables.

This tool never patches or writes either image.  It normalizes x86 instruction
operands so functions built from the same engine source can be compared even
when their absolute data addresses and relative call targets differ.
"""

from __future__ import annotations

import argparse
import difflib
import re
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

import pefile
from capstone import (
    CS_ARCH_X86,
    CS_MODE_32,
    CS_OP_IMM,
    CS_OP_MEM,
    CS_OP_REG,
    Cs,
    CsError,
)
from capstone.x86 import X86_REG_INVALID


def parse_int(value: str) -> int:
    return int(value, 0)


@dataclass(frozen=True)
class Section:
    start: int
    end: int
    data: bytes
    executable: bool


class Image:
    def __init__(self, path: Path):
        self.path = path
        self.file_data = path.read_bytes()
        self.pe = pefile.PE(data=self.file_data, fast_load=True)
        self.base = int(self.pe.OPTIONAL_HEADER.ImageBase)
        self.size = int(self.pe.OPTIONAL_HEADER.SizeOfImage)
        self.sections: list[Section] = []
        for raw_section in self.pe.sections:
            start = self.base + int(raw_section.VirtualAddress)
            data = bytes(raw_section.get_data())
            self.sections.append(
                Section(
                    start=start,
                    end=start + len(data),
                    data=data,
                    executable=bool(raw_section.Characteristics & 0x20000000),
                )
            )

        self.md = Cs(CS_ARCH_X86, CS_MODE_32)
        self.md.detail = True
        self.md.skipdata = True
        self._instructions = None
        self._candidate_starts = None

    def contains(self, value: int) -> bool:
        return self.base <= value < self.base + self.size

    def code_contains(self, value: int) -> bool:
        return any(s.executable and s.start <= value < s.end for s in self.sections)

    def bytes_at(self, va: int, size: int) -> bytes:
        return bytes(self.pe.get_data(va - self.base, size))

    def instructions(self):
        if self._instructions is None:
            result = []
            for section in self.sections:
                if section.executable:
                    result.extend(self.md.disasm(section.data, section.start))
            self._instructions = result
        return self._instructions

    def candidate_starts(self) -> list[int]:
        if self._candidate_starts is None:
            starts = {self.base + int(self.pe.OPTIONAL_HEADER.AddressOfEntryPoint)}
            for insn in self.instructions():
                try:
                    decoded_operands = insn.operands
                except CsError:
                    continue
                if insn.mnemonic == "call" and len(decoded_operands) == 1:
                    operand = decoded_operands[0]
                    if operand.type == CS_OP_IMM and self.code_contains(operand.imm):
                        starts.add(int(operand.imm))
            self._candidate_starts = sorted(starts)
        return self._candidate_starts

    def function_tokens(self, start: int, max_instructions: int = 160) -> list[str]:
        section = next(
            (s for s in self.sections if s.executable and s.start <= start < s.end),
            None,
        )
        if section is None:
            return []
        data = section.data[start - section.start :]
        tokens: list[str] = []
        for insn in self.md.disasm(data, start, count=max_instructions):
            try:
                decoded_operands = insn.operands
            except CsError:
                tokens.append("data")
                continue
            operands: list[str] = []
            for operand in decoded_operands:
                if operand.type == CS_OP_REG:
                    operands.append(f"r:{insn.reg_name(operand.reg)}")
                elif operand.type == CS_OP_IMM:
                    value = int(operand.imm) & 0xFFFFFFFF
                    if self.code_contains(value):
                        operands.append("code")
                    elif self.contains(value):
                        operands.append("image")
                    elif value <= 0x1000 or value >= 0xFFFFF000:
                        operands.append(f"i:{value:08x}")
                    else:
                        operands.append("imm")
                elif operand.type == CS_OP_MEM:
                    memory = operand.mem
                    if memory.base == X86_REG_INVALID and memory.index == X86_REG_INVALID:
                        value = int(memory.disp) & 0xFFFFFFFF
                        operands.append("m:image" if self.contains(value) else "m:absolute")
                    else:
                        base = insn.reg_name(memory.base) if memory.base else "-"
                        index = insn.reg_name(memory.index) if memory.index else "-"
                        displacement = int(memory.disp)
                        if -0x1000 <= displacement <= 0x1000:
                            disp = f"{displacement:+x}"
                        else:
                            disp = "large"
                        operands.append(f"m:{base}:{index}:{memory.scale}:{disp}")
                else:
                    operands.append(f"o:{operand.type}")
            tokens.append(f"{insn.mnemonic}|{'|'.join(operands)}")
            if insn.mnemonic.startswith("ret") and len(tokens) >= 8:
                break
        return tokens

    def disasm(self, start: int, size: int) -> None:
        for insn in self.md.disasm(self.bytes_at(start, size), start):
            raw = " ".join(f"{byte:02X}" for byte in insn.bytes)
            print(f"{insn.address:08X}  {raw:<32} {insn.mnemonic:<8} {insn.op_str}")


def ngrams(tokens: list[str], width: int = 3) -> Counter:
    return Counter(tuple(tokens[index : index + width]) for index in range(len(tokens) - width + 1))


def similarity(left: list[str], right: list[str]) -> float:
    if not left or not right:
        return 0.0
    sequence = difflib.SequenceMatcher(None, left, right, autojunk=False).ratio()
    left_grams = ngrams(left)
    right_grams = ngrams(right)
    common = sum((left_grams & right_grams).values())
    total = sum((left_grams | right_grams).values())
    jaccard = common / total if total else 0.0
    return (sequence * 0.45) + (jaccard * 0.55)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sp", type=Path, required=True)
    parser.add_argument("--mp", type=Path, required=True)
    commands = parser.add_subparsers(dest="command", required=True)

    match = commands.add_parser("match")
    match.add_argument("sp_address", type=parse_int, nargs="+")
    match.add_argument("--limit", type=int, default=12)
    match.add_argument("--instructions", type=int, default=160)

    disasm = commands.add_parser("disasm")
    disasm.add_argument("image", choices=("sp", "mp"))
    disasm.add_argument("address", type=parse_int)
    disasm.add_argument("size", type=parse_int)

    calls = commands.add_parser("calls")
    calls.add_argument("image", choices=("sp", "mp"))
    calls.add_argument("target", type=parse_int)

    refs = commands.add_parser("refs")
    refs.add_argument("image", choices=("sp", "mp"))
    refs.add_argument("target", type=parse_int, nargs="+")

    strings = commands.add_parser("strings")
    strings.add_argument("image", choices=("sp", "mp"))
    strings.add_argument("terms", nargs="+")

    args = parser.parse_args()
    sp = Image(args.sp)
    mp = Image(args.mp)
    images = {"sp": sp, "mp": mp}

    if args.command == "disasm":
        images[args.image].disasm(args.address, args.size)
        return

    if args.command in ("calls", "refs"):
        image = images[args.image]
        targets = {args.target} if args.command == "calls" else set(args.target)
        for insn in image.instructions():
            try:
                operands = insn.operands
            except CsError:
                continue
            if args.command == "calls" and insn.mnemonic != "call":
                continue
            matched = False
            for operand in operands:
                if operand.type == CS_OP_IMM:
                    matched = (int(operand.imm) & 0xFFFFFFFF) in targets
                elif operand.type == CS_OP_MEM:
                    memory = operand.mem
                    if memory.base == X86_REG_INVALID and memory.index == X86_REG_INVALID:
                        matched = (int(memory.disp) & 0xFFFFFFFF) in targets
                if matched:
                    break
            if matched:
                raw = " ".join(f"{byte:02X}" for byte in insn.bytes)
                print(f"{insn.address:08X}  {raw:<32} {insn.mnemonic:<8} {insn.op_str}")
        return

    if args.command == "strings":
        image = images[args.image]
        terms = [term.casefold() for term in args.terms]
        for found in re.finditer(rb"[\x20-\x7e]{4,}\x00", image.file_data):
            value = found.group()[:-1].decode("ascii", "replace")
            if not any(term in value.casefold() for term in terms):
                continue
            try:
                rva = image.pe.get_rva_from_offset(found.start())
            except pefile.PEFormatError:
                continue
            print(f"{image.base + rva:08X}  {value}")
        return

    candidate_tokens = []
    for address in mp.candidate_starts():
        target = mp.function_tokens(address, args.instructions)
        candidate_tokens.append((address, target))
    for source_address in args.sp_address:
        source = sp.function_tokens(source_address, args.instructions)
        if not source:
            raise SystemExit(f"SP address 0x{source_address:08X} is not executable")
        candidates = [
            (similarity(source, target), address, len(target))
            for address, target in candidate_tokens
        ]
        candidates.sort(reverse=True)
        print(f"SP 0x{source_address:08X}: {len(source)} normalized instructions")
        for score, address, count in candidates[: args.limit]:
            print(f"{score:0.4f}  MP 0x{address:08X}  {count} instructions")
        print()


if __name__ == "__main__":
    main()
