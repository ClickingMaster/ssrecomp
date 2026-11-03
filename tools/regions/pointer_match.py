"""Match complete pointer wrappers while replacing only independently proven pointers.

No address-range masking: every other byte must remain identical. Ambiguous,
unaligned, absent and truncated candidates are rejected.
"""
import struct


def find_pointer_wrapper(reference, target, target_base, replacements, alignment=2):
    if not reference or not replacements or len(reference) % alignment:
        return None
    wanted = bytearray(reference)
    changed = set()
    for old, new in replacements.items():
        encoded = struct.pack('<I', old)
        slots = [i for i in range(0, len(reference)-3, 4) if reference[i:i+4] == encoded]
        if not slots:
            return None
        for slot in slots:
            if slot in changed:
                return None
            changed.add(slot)
            wanted[slot:slot+4] = struct.pack('<I', new)
    hits = []
    at = target.find(wanted)
    while at >= 0:
        if (target_base + at) % alignment == 0:
            hits.append(target_base + at)
        at = target.find(wanted, at + 1)
    return hits[0] if len(hits) == 1 else None
