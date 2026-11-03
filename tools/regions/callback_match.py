#!/usr/bin/env python3
"""Conservative callback matching using decoded structure and proven call targets.

Requires Capstone 5.x for this developer tool only. Emits no game instructions.
Non-branch constants may vary; opcode, condition, register, memory-offset and
branch structure remain exact. Candidates require at least three known call
anchors, all known callees in the same slots, and a unique match in the module.
"""
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, CS_GRP_CALL, CS_GRP_JUMP
from capstone.arm import ARM_OP_REG, ARM_OP_IMM, ARM_OP_MEM, ARM_REG_PC, ARM_REG_LR


def decode(data, address, isa, limit):
    engine = Cs(CS_ARCH_ARM, CS_MODE_THUMB if isa == 'thumb' else CS_MODE_ARM)
    engine.detail = True
    instructions = []
    for instruction in engine.disasm(data[:limit], address):
        instructions.append(instruction)
        if instruction.mnemonic == 'pop' and any(o.type == ARM_OP_REG and o.reg == ARM_REG_PC for o in instruction.operands):
            return instructions, True
        if instruction.mnemonic == 'bx' and instruction.operands and instruction.operands[0].type == ARM_OP_REG and instruction.operands[0].reg == ARM_REG_LR:
            return instructions, True
    return instructions, False


def call_targets(instructions):
    return [next((o.imm & ~1 for o in i.operands if o.type == ARM_OP_IMM), None)
            for i in instructions if i.group(CS_GRP_CALL)]


def signature(instructions):
    result = []
    local = {i.address: n for n, i in enumerate(instructions)}
    for i in instructions:
        operands = []
        for o in i.operands:
            if o.type == ARM_OP_REG:
                value = o.reg
            elif o.type == ARM_OP_MEM:
                value = (o.mem.base, o.mem.index, 0 if o.mem.base == ARM_REG_PC else o.mem.disp)
            elif o.type == ARM_OP_IMM:
                value = 0 if i.group(CS_GRP_CALL) else ('branch', local.get(o.imm, 'external')) if i.group(CS_GRP_JUMP) else 0
            else:
                value = None
            operands.append((o.type, value, o.shift.type, o.shift.value))
        result.append((i.id, i.cc, i.update_flags, i.writeback, tuple(operands)))
    return tuple(result)


def find_callback(reference, reference_address, target, target_base, isa, mapped_calls):
    ref, complete = decode(reference, reference_address, isa, len(reference))
    if not complete or not ref:
        return None
    calls = call_targets(ref)
    if len(calls) != len(mapped_calls) or sum(x is not None for x in mapped_calls) < 3:
        return None
    if sum(x is None for x in mapped_calls) > 1:
        return None
    wanted = signature(ref)
    step = 2 if isa == 'thumb' else 4
    candidates = []
    for start in range(0, len(target), step):
        # Same register prologue is part of the exact signature, so this is only a prefilter.
        if target[start:start+step] != reference[:step]:
            continue
        instructions, complete = decode(target[start:start+len(reference)*2+32], target_base+start, isa, len(reference)*2+32)
        if not complete or signature(instructions) != wanted:
            continue
        actual = call_targets(instructions)
        if len(actual) == len(calls) and all(expected is None or expected == got for expected, got in zip(mapped_calls, actual)):
            candidates.append(target_base+start)
    return candidates[0] if len(candidates) == 1 else None
