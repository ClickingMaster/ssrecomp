"""Reviewed regional callbacks with explicit field and dispatch-table differences.

Unlike the default callback matcher, this opt-in helper accepts a declared
halfword field relocation and an embedded signed-relative dispatch table.
Every table slot must still reach a matching handler. Call targets and all
remaining decoded structure are checked, and the module match must be unique.
Requires Capstone 5.x; retains no ROM bytes.
"""
import struct
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_ARM
from capstone.arm import ARM_OP_MEM, ARM_REG_PC
from callback_match import decode, signature, call_targets


def normalised(instructions, target_fields):
    result = []
    for instruction, entry in zip(instructions, signature(instructions)):
        row = list(entry)
        operands = list(row[-1])
        if instruction.mnemonic in ('ldrh', 'strh'):
            for n, operand in enumerate(instruction.operands):
                if operand.type == ARM_OP_MEM and operand.mem.base != ARM_REG_PC and operand.mem.disp in target_fields:
                    kind, value, shift, amount = operands[n]
                    operands[n] = (kind, (value[0], value[1], target_fields[operand.mem.disp]), shift, amount)
        row[-1] = tuple(operands)
        result.append(tuple(row))
    return tuple(result)


def dispatch_matches(reference, reference_address, candidate, candidate_address, specification, fields, isa):
    offset, count, branch_base = specification
    if offset < 0 or count < 1 or offset + count * 2 > min(len(reference), len(candidate)):
        return False
    engine = Cs(CS_ARCH_ARM, CS_MODE_THUMB if isa == 'thumb' else CS_MODE_ARM)
    engine.detail = True
    for slot in range(count):
        old = branch_base + struct.unpack_from('<h', reference, offset + slot * 2)[0]
        new = branch_base + struct.unpack_from('<h', candidate, offset + slot * 2)[0]
        if old < offset + count * 2 or new < offset + count * 2 or old % 2 or new % 2 or old + 12 > len(reference) or new + 12 > len(candidate):
            return False
        a = list(engine.disasm(reference[old:old+32], reference_address+old))[:6]
        b = list(engine.disasm(candidate[new:new+32], candidate_address+new))[:6]
        if len(a) != 6 or len(b) != 6 or normalised(a, {}) != normalised(b, fields):
            return False
    return True


def find_reviewed_callback(reference, reference_address, target, target_base, isa, mapped_calls, fields, dispatch=None):
    old, complete = decode(reference, reference_address, isa, len(reference))
    if not complete or not old:
        return None
    calls = call_targets(old)
    known = {value for value in mapped_calls if value is not None}
    unknown = {value for value, mapped in zip(calls, mapped_calls) if mapped is None}
    if len(calls) != len(mapped_calls) or len(known) < 2 or len(unknown) > 1 or (unknown and len(known) < 3):
        return None
    def executable(instructions, base):
        if not dispatch:
            return instructions
        offset, count, _ = dispatch
        return [i for i in instructions if not offset <= i.address-base < offset+count*2]
    wanted = normalised(executable(old, reference_address), {})
    step = 2 if isa == 'thumb' else 4
    matches = []
    for at in range(0, len(target), step):
        if target[at:at+step] != reference[:step]:
            continue
        body = target[at:at+len(reference)*2+32]
        new, complete = decode(body, target_base+at, isa, len(body))
        if not complete or normalised(executable(new, target_base+at), fields) != wanted:
            continue
        actual = call_targets(new)
        if len(actual) != len(calls) or any(expected is not None and expected != got for expected, got in zip(mapped_calls, actual)):
            continue
        if dispatch and not dispatch_matches(reference, reference_address, body, target_base+at, dispatch, fields, isa):
            continue
        matches.append(target_base+at)
    return matches[0] if len(matches) == 1 else None
