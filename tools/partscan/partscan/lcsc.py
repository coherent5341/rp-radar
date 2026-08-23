"""
The LCSC side: the code printed on their bags, and on JLCPCB's.

It is not a standards-based message like DigiKey's — it is a brace-wrapped
list of key:value pairs, in plain text:

    {pbn:PICK2409280015,on:GB2409280135,pc:C1554,pm:0402CG200J500NT,qty:100,
     mc:C20, C21,cc:1,pdi:129558054,hp:12,wc:ZH}

Which makes it far easier to read than a label whose separators have been
stripped, with one catch: a value may itself contain a comma. `mc` above holds
the two reference designators "C20, C21", so splitting on commas would cut it
in half and leave "C21" looking like a key. The fields are therefore divided at
the commas that are followed by a `key:`, and nowhere else.
"""

from __future__ import annotations

import re
from typing import Dict, Tuple

SUPPLIER = "LCSC"

# What the keys mean. The ones that are not here are kept verbatim rather than
# guessed at: `cc`, `hp` and `wc` appear on every bag, but LCSC document none
# of them, and a wrong label is worse than a raw one. (`wc:ZH` is a warehouse
# code, not a country — it must not end up in the country column.)
KEYS: Dict[str, str] = {
    "pc": "supplier_part",     # LCSC part code, C1554
    "pm": "mfr_part",          # product model, the manufacturer part number
    "mc": "customer_part",     # what the customer called it: "C20, C21"
    "qty": "quantity",
    "on": "order_no",          # LCSC order number, GB2409280135
    "pdi": "part_id",          # LCSC's own product id
    "pbn": "pick_flag",        # pick batch number, PICK2409280015
}

# A key is a bare word followed by a colon, at the start or after a comma.
# Anything else that follows a comma is part of the value it sits in.
_PAIR = re.compile(r"(?:^|,)\s*([A-Za-z][A-Za-z0-9_]*)\s*:")


def looks_like(body: str) -> bool:
    """Whether this text is an LCSC code rather than another supplier's."""
    text = body.strip()
    if text.startswith("{") and text.endswith("}"):
        return True
    return bool(re.search(r"(?:^|,)\s*(pc|pm|pbn|qty)\s*:", text, re.IGNORECASE))


def parse(body: str) -> Tuple[Dict[str, str], Dict[str, str], str]:
    """Decode an LCSC bag code. Returns (fields, extra, parse mode)."""
    inner = body.strip()
    if inner.startswith("{"):
        inner = inner[1:]
    if inner.endswith("}"):
        inner = inner[:-1]

    pairs = list(_PAIR.finditer(inner))
    if not pairs:
        raise ValueError(
            "No key:value pairs found. Expected an LCSC code such as "
            "'{pbn:...,pc:C1554,pm:0402CG200J500NT,qty:100}'."
        )

    fields: Dict[str, str] = {}
    extra: Dict[str, str] = {}
    for index, pair in enumerate(pairs):
        stop = pairs[index + 1].start() if index + 1 < len(pairs) else len(inner)
        key = pair.group(1).lower()
        value = inner[pair.end():stop].strip()
        if key in KEYS:
            fields[KEYS[key]] = value
        elif value:
            extra[key] = value

    if not (fields.get("supplier_part") or fields.get("mfr_part")):
        raise ValueError("The code has no part number in it (no pc: or pm: field).")

    # The bag carries no packaging suffix, so pc is the part number as ordered.
    return fields, extra, "labelled"
