"""
Parse the 2D DataMatrix barcode DigiKey prints on its packing labels.

The label is an ISO/IEC 15434 "format 06" message carrying ANSI MH10.8.2 data
identifiers:

    [)> RS 06 GS P<customer part> GS 1P<manufacturer part> GS ... RS EOT

Most keyboard-wedge scanners drop the unprintable separators (RS 0x1e,
GS 0x1d, EOT 0x04), which is how the text usually arrives when it has been
typed into a form or pasted from a terminal:

    [)>06PC50,C511PCC0402BPNPO9BN8R230P13-CC0402BPNPO9BN8R2CT-ND...

Both forms are handled. With the separators present the split is exact. With
them stripped the fields are recovered by matching the data identifiers in the
order DigiKey emits them, rejecting the splits that would put the wrong sort of
content in a field, and scoring what is left on how much it looks like a
DigiKey label; `_parse_packed` has the detail. That is a reconstruction, not a
decode — two adjacent numeric fields with nothing between them divide only one
way that keeps the sequence valid, and that is the way they are divided — so
`Barcode.parse_mode` says which path ran and callers can flag the records that
were reconstructed.
"""

from __future__ import annotations

import re
from bisect import bisect_left
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

GS = "\x1d"  # field separator
RS = "\x1e"  # record separator
EOT = "\x04"  # end of transmission


class BarcodeError(ValueError):
    """The text is not a barcode this module can read."""


@dataclass(frozen=True)
class Field:
    di: str  # MH10.8.2 data identifier
    key: str  # our name for it
    label: str  # what to call it on screen


# The order matters: it is the order DigiKey prints, and it is what makes the
# separator-less form recoverable.
FIELDS: Tuple[Field, ...] = (
    Field("P", "customer_part", "Customer part number"),
    Field("1P", "mfr_part", "Manufacturer part number"),
    Field("30P", "dk_part", "DigiKey part number"),
    Field("K", "purchase_order", "Purchase order"),
    Field("1K", "sales_order", "Sales order"),
    Field("10K", "invoice", "Invoice"),
    Field("9D", "date_code", "Date code"),
    Field("1T", "lot_code", "Lot code"),
    Field("11K", "packing_list", "Packing list"),
    Field("4L", "country", "Country of origin"),
    Field("Q", "quantity", "Quantity"),
    Field("11Z", "pick_flag", "Pick flag"),
    Field("12Z", "part_id", "DigiKey part ID"),
    Field("13Z", "load_id", "Load ID"),
    Field("20Z", "padding", "Padding"),
)

BY_DI: Dict[str, Field] = {f.di: f for f in FIELDS}
BY_KEY: Dict[str, Field] = {f.key: f for f in FIELDS}

# Fields worth showing on a scan record. The rest (pick flag, padding) are
# DigiKey housekeeping.
INTERESTING_KEYS: Tuple[str, ...] = (
    "mfr_part",
    "dk_part",
    "customer_part",
    "quantity",
    "date_code",
    "lot_code",
    "country",
    "purchase_order",
    "sales_order",
    "invoice",
    "packing_list",
    "part_id",
    "load_id",
)

# Fields DigiKey fills with digits only. Refusing anything else here is what
# stops a part number being read as an invoice number.
_NUMERIC_KEYS = frozenset({
    "sales_order", "invoice", "packing_list", "part_id", "load_id", "padding", "quantity",
})

# A full DigiKey label is a little over 200 characters. The cap keeps a large
# paste from turning into a large search.
_MAX_LENGTH = 1024

_DI_PATTERN = re.compile(r"^(\d{0,3}[A-Z])(.*)$", re.DOTALL)
_PLACEHOLDER = re.compile(r"[<\[{(](GS|RS|EOT)[>\]})]", re.IGNORECASE)
_HEX_ESCAPE = re.compile(r"\\x([0-9a-fA-F]{2})")
_CONTROL = {"GS": GS, "RS": RS, "EOT": EOT}


@dataclass
class Barcode:
    """A decoded label."""

    raw: str
    parse_mode: str  # "delimited" when the separators survived, else "packed"
    fields: Dict[str, str] = field(default_factory=dict)
    extra: Dict[str, str] = field(default_factory=dict)  # data identifiers we do not name

    def get(self, key: str, default: str = "") -> str:
        return self.fields.get(key, default)

    @property
    def quantity(self) -> int:
        digits = re.sub(r"\D", "", self.get("quantity"))
        return int(digits) if digits else 0

    def as_dict(self) -> Dict[str, object]:
        out: Dict[str, object] = {key: self.get(key) for key in BY_KEY}
        out["parse_mode"] = self.parse_mode
        out["quantity"] = self.quantity  # the integer, not the raw field text
        out["date_code_text"] = describe_date_code(self.get("date_code"))
        out["extra"] = dict(self.extra)
        return out


def normalise(text: str) -> str:
    """Undo the ways a scanner or a copy-paste mangles the separators."""
    text = text.strip()
    text = _PLACEHOLDER.sub(lambda m: _CONTROL[m.group(1).upper()], text)
    text = _HEX_ESCAPE.sub(lambda m: chr(int(m.group(1), 16)), text)
    # Scanners in "show control codes" mode print GS as ^] and RS as ^^.
    text = text.replace("^]", GS).replace("^^", RS).replace("^D", EOT)
    # A wedge scanner may end the message with a newline instead of EOT.
    return text.strip("\r\n\t ").rstrip(EOT).strip()


def parse(text: str) -> Barcode:
    """Decode `text`, raising BarcodeError if it is not a DigiKey label."""
    raw = text.strip()
    body = normalise(text)
    if not body:
        raise BarcodeError("Nothing to parse.")
    if len(body) > _MAX_LENGTH:
        raise BarcodeError(
            f"That is {len(body)} characters; a DigiKey label is a little over 200."
        )

    body = _strip_header(body)
    if not body:
        raise BarcodeError("The barcode carries a header but no data.")

    if GS in body or RS in body:
        fields, extra = _parse_delimited(body)
        mode = "delimited"
    else:
        fields, extra = _parse_packed(body)
        mode = "packed"

    if not fields:
        raise BarcodeError(
            "No data identifiers found. Expected a DigiKey 2D label such as "
            "'[)>06P<customer part>1P<manufacturer part>30P<DigiKey part>...'."
        )
    if not (fields.get("mfr_part") or fields.get("dk_part") or fields.get("customer_part")):
        raise BarcodeError("The barcode has no part number in it.")

    return Barcode(raw=raw, parse_mode=mode, fields=fields, extra=extra)


def _strip_header(body: str) -> str:
    """Remove the '[)>' RS '06' GS envelope, however much of it survived."""
    if body.startswith("[)>"):
        body = body[3:]
    body = body.lstrip(RS)
    if body[:2] in ("06", "05", "12"):  # 06 is what DigiKey uses
        body = body[2:]
    return body.lstrip(GS).rstrip(EOT).rstrip(RS)


def _parse_delimited(body: str) -> Tuple[Dict[str, str], Dict[str, str]]:
    """Split on the separators, then read one data identifier per segment."""
    fields: Dict[str, str] = {}
    extra: Dict[str, str] = {}
    for segment in re.split(f"[{GS}{RS}]", body):
        segment = segment.strip()
        if not segment:
            continue
        di, value = _split_identifier(segment)
        if di is None:
            extra.setdefault("unparsed", "")
            extra["unparsed"] = (extra["unparsed"] + " " + segment).strip()
        elif di in BY_DI:
            fields[BY_DI[di].key] = value
        else:
            extra[di] = value
    return fields, extra


def _split_identifier(segment: str) -> Tuple[Optional[str], str]:
    """Take the leading data identifier off a segment, longest known one first."""
    for length in (3, 2, 1):
        head = segment[:length]
        if head in BY_DI:
            return head, segment[length:]
    match = _DI_PATTERN.match(segment)
    if match:
        return match.group(1), match.group(2)
    return None, segment


def _parse_packed(body: str) -> Tuple[Dict[str, str], Dict[str, str]]:
    """
    Recover the fields from a message whose separators were dropped.

    The fields come in a known order, and each value runs up to a point where a
    later data identifier starts. That is ambiguous on its own, because part
    numbers contain the identifier strings all the time: 'RC0402FR-0710KL' has
    a 10K in it, and 10K is the invoice identifier; '311-10.0KLRCT-ND' has a K
    in it, and K is the purchase order.

    Two things settle it. Fields whose content is fixed reject the splits that
    would put a part number in them — the quantity, the order numbers and the
    date code are digits, the country is two letters. What is left is scored on
    how much each field looks like what it claims to be, and the highest
    scoring reading wins. The search over readings is a dynamic program keyed
    on (position, next field), so the best reading is found exactly rather than
    by trying splits until one fits.
    """
    n = len(body)
    # Where every identifier occurs, worked out once: `later[i]` is every
    # position at which a field after i could start, which is exactly the set
    # of places the value of field i may end.
    occurrences = [[at for at in range(n) if body.startswith(entry.di, at)] for entry in FIELDS]
    later: List[List[int]] = []
    for index in range(len(FIELDS)):
        merged = sorted({at for k in range(index + 1, len(FIELDS)) for at in occurrences[k]})
        later.append(merged)

    Reading = Optional[Tuple[int, Tuple[Tuple[str, str], ...]]]
    memo: Dict[Tuple[int, int], Reading] = {}

    def best(pos: int, index: int) -> Reading:
        """The highest scoring way to read body[pos:] using FIELDS[index:]."""
        if pos >= n:
            return 0, ()
        if index >= len(FIELDS):
            return None  # characters left over that no field can hold
        key = (pos, index)
        if key in memo:
            return memo[key]

        found: Reading = None
        entry = FIELDS[index]
        if body.startswith(entry.di, pos):
            value_start = pos + len(entry.di)
            # Shortest value first, so an equal score keeps the earliest split.
            for end in later[index][bisect_left(later[index], value_start):] + [n]:
                value = body[value_start:end]
                if not _plausible(entry.key, value):
                    continue
                rest = best(end, index + 1)
                if rest is None:
                    continue
                score = rest[0] + _field_score(entry.key, value)
                if found is None or score > found[0]:
                    found = score, ((entry.key, value),) + rest[1]

        skipped = best(pos, index + 1)
        if skipped is not None:
            score = skipped[0] + _skip_score(entry.key)
            if found is None or score > found[0]:
                found = score, skipped[1]

        memo[key] = found
        return found

    reading = best(0, 0)
    if reading is None:
        # Not a sequence we recognise. Fall back to reading whatever leading
        # identifiers do line up, so a truncated scan still yields a part number.
        return _parse_best_effort(body)
    return dict(reading[1]), {}


def _plausible(key: str, value: str) -> bool:
    """Whether a value could be what that field holds."""
    if not value:
        return True  # DigiKey prints empty fields, the purchase order especially
    if key in _NUMERIC_KEYS:
        return value.isdigit()
    if key == "date_code":
        return value.isdigit() and 3 <= len(value) <= 5
    if key == "country":
        return len(value) == 2 and value.isalpha()
    return True


def _field_score(key: str, value: str) -> int:
    """
    How much this value looks like what the field is meant to hold. Only ever
    used to choose between readings that are all internally consistent, so the
    weights just need to rank them, and the +1 for reading a field at all is
    what stops a reading dropping fields it could have filled.
    """
    score = 1
    if key == "dk_part":
        if value.upper().endswith("-ND"):
            score += 3  # DigiKey catalogue numbers end in -ND
        elif "-" in value:
            score += 1
    elif key == "mfr_part":
        score += 1 if len(value) >= 3 else 0
    elif key == "date_code":
        if len(value) == 4 and value.isdigit() and 1 <= int(value[2:]) <= 53:
            score += 2
    elif key == "country":
        score += 2 if len(value) == 2 else 0
    elif key == "quantity":
        score += 2 if value.isdigit() and int(value) > 0 else 0
    elif key == "purchase_order":
        score += 1 if not value else 0  # the customer purchase order is usually blank
    return score


def _skip_score(key: str) -> int:
    """A label that leaves the purchase order out is as ordinary as one that
    prints it empty; every other missing field is simply worth nothing."""
    return 1 if key == "purchase_order" else 0


def _parse_best_effort(body: str) -> Tuple[Dict[str, str], Dict[str, str]]:
    """Last resort: take the fields that do match in order and keep the rest."""
    fields: Dict[str, str] = {}
    extra: Dict[str, str] = {}
    pos = 0
    index = 0
    n = len(body)
    while pos < n and index < len(FIELDS):
        matched = None
        for i in range(index, len(FIELDS)):
            if body.startswith(FIELDS[i].di, pos):
                matched = i
                break
        if matched is None:
            break
        value_start = pos + len(FIELDS[matched].di)
        end = n
        for at in range(value_start, n):
            if any(body.startswith(FIELDS[k].di, at) for k in range(matched + 1, len(FIELDS))):
                end = at
                break
        fields[FIELDS[matched].key] = body[value_start:end]
        pos, index = end, matched + 1
    if pos < n:
        extra["unparsed"] = body[pos:]
    return fields, extra


def describe_date_code(code: str) -> str:
    """Render a YYWW date code as a year and week; pass anything else through."""
    code = (code or "").strip()
    if re.fullmatch(r"\d{4}", code):
        year, week = int(code[:2]), int(code[2:])
        if 1 <= week <= 53:
            return f"20{year:02d} week {week}"
    return code
