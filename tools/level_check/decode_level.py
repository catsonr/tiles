#!/usr/bin/env python3
"""Decode one level .tres into the flat form LevelCheck reads on stdin.

This reads only what a LevelResource stores: the geometry domain, the palette
entries in authored order, and the blueprint records in authored order. It
resolves nothing and validates nothing -- every judgement belongs to the
checker, which uses the real compilers.

Output:

    <geometry domain>
    <palette entry count>
    <prototile id> <supply>          x entry count      (-1 supply is unlimited)
    <blueprint record count>
    <id> <step> <order> <x raw> <y raw>   x record count
"""

import re
import sys

SUB_RESOURCE = re.compile(r'\[sub_resource type="(\w+)" id="([^"]+)"\]')
PROPERTY = re.compile(r"(\w+) = (.*)")
REFERENCE = re.compile(r'SubResource\("([^"]+)"\)')


def decode(path):
    blocks = {}
    resource = {}
    current = None
    in_resource = False

    for line in open(path):
        match = SUB_RESOURCE.match(line)
        if match:
            current = {}
            blocks[match.group(2)] = current
            in_resource = False
            continue
        if line.strip() == "[resource]":
            current = None
            in_resource = True
            continue
        match = PROPERTY.match(line)
        if not match:
            continue
        key, value = match.group(1), match.group(2)
        if in_resource:
            resource[key] = value
        elif current is not None:
            current[key] = value

    domain = int(resource.get("geometry_domain", "0"))

    palette_refs = REFERENCE.findall(resource.get("palette", ""))
    entry_refs = []
    if palette_refs:
        entry_refs = REFERENCE.findall(blocks[palette_refs[0]].get("entries", ""))

    entries = []
    for ref in entry_refs:
        block = blocks[ref]
        entries.append((
            int(block.get("prototile_id", "0")),
            int(block.get("supply", "-1")),
        ))

    records = []
    for ref in REFERENCE.findall(resource.get("blueprint", "")):
        block = blocks[ref]
        records.append((
            int(block.get("prototile_id", "0")),
            int(block.get("orientation_step", "0")),
            int(block.get("orientation_order", "1")),
            int(block.get("translation_x_raw", "0")),
            int(block.get("translation_y_raw", "0")),
        ))

    lines = [str(domain), str(len(entries))]
    lines += ["%d %d" % entry for entry in entries]
    lines.append(str(len(records)))
    lines += ["%d %d %d %d %d" % record for record in records]
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    sys.stdout.write(decode(sys.argv[1]))
