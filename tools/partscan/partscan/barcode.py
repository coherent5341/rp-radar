"""
Read the code off a parts bag or reel, whoever sold it.

Two suppliers, two formats that have nothing in common. DigiKey prints an
ISO/IEC 15434 message of MH10.8.2 data identifiers, whose field separators
most scanners throw away; LCSC prints a brace-wrapped list of key:value pairs.
`digikey.py` and `lcsc.py` do the reading; this module decides which is which,
and gives both the same set of field names, so a part is a part and a quantity
is a quantity no matter which bag it came out of.

    >>> parse("{pc:C1554,pm:0402CG200J500NT,qty:100}").get("mfr_part")
    '0402CG200J500NT'
    >>> parse("[)>061PERJ-6ENF1001VQ6").get("mfr_part")
    'ERJ-6ENF1001V'
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Dict, Tuple

from . import digikey, lcsc

GS = digikey.GS  # field separator, 0x1d
RS = digikey.RS  # record separator, 0x1e
EOT = digikey.EOT  # end of transmission, 0x04

# The fields both suppliers are read into. Not every supplier fills every one:
# an LCSC bag carries no date code or country, a DigiKey reel no warehouse.
FIELD_LABELS: Dict[str, str] = {
    "mfr_part": "Manufacturer part number",
    "supplier_part": "Supplier part number",
    "customer_part": "Customer part number",
    "quantity": "Quantity",
    "order_no": "Order number",
    "invoice": "Invoice",
    "date_code": "Date code",
    "lot_code": "Lot code",
    "country": "Country of origin",
    "purchase_order": "Purchase order",
    "packing_list": "Packing list",
    "part_id": "Supplier product ID",
    "load_id": "Load ID",
    "pick_flag": "Pick reference",
    "padding": "Padding",
}

SUPPLIERS: Tuple[str, ...] = (digikey.SUPPLIER, lcsc.SUPPLIER)

# A full DigiKey label is a little over 200 characters, an LCSC code half that.
# The cap keeps a large paste from turning into a large search.
_MAX_LENGTH = 1024

_PLACEHOLDER = re.compile(r"[<\[{(](GS|RS|EOT)[>\]})]", re.IGNORECASE)
_HEX_ESCAPE = re.compile(r"\\x([0-9a-fA-F]{2})")
_CONTROL = {"GS": GS, "RS": RS, "EOT": EOT}


class BarcodeError(ValueError):
    """The text is not a code this module can read."""


@dataclass
class Barcode:
    """A decoded label or bag code."""

    raw: str
    supplier: str
    parse_mode: str  # "delimited" or "packed" for DigiKey, "labelled" for LCSC
    fields: Dict[str, str] = field(default_factory=dict)
    extra: Dict[str, str] = field(default_factory=dict)  # what we do not name

    def get(self, key: str, default: str = "") -> str:
        return self.fields.get(key, default)

    @property
    def quantity(self) -> int:
        digits = re.sub(r"\D", "", self.get("quantity"))
        return int(digits) if digits else 0

    @property
    def part(self) -> str:
        """What to call this thing when there is room for only one name."""
        return self.get("mfr_part") or self.get("supplier_part") or self.get("customer_part")

    def as_dict(self) -> Dict[str, object]:
        out: Dict[str, object] = {key: self.get(key) for key in FIELD_LABELS}
        out["supplier"] = self.supplier
        out["parse_mode"] = self.parse_mode
        out["quantity"] = self.quantity  # the integer, not the raw field text
        out["date_code_text"] = describe_date_code(self.get("date_code"))
        out["extra"] = dict(self.extra)
        return out


def normalise(text: str) -> str:
    """Undo the ways a scanner or a copy-paste mangles the separators."""
    text = text.strip()
    # An LCSC code is braces and printable text; nothing here applies to it,
    # and {GS} would only be a placeholder in a DigiKey label anyway.
    text = _PLACEHOLDER.sub(lambda m: _CONTROL[m.group(1).upper()], text)
    text = _HEX_ESCAPE.sub(lambda m: chr(int(m.group(1), 16)), text)
    # Scanners in "show control codes" mode print GS as ^] and RS as ^^.
    text = text.replace("^]", GS).replace("^^", RS).replace("^D", EOT)
    # A wedge scanner may end the message with a newline instead of EOT.
    return text.strip("\r\n\t ").rstrip(EOT).strip()


def parse(text: str) -> Barcode:
    """Decode `text`, raising BarcodeError if no supplier's reader takes it."""
    raw = text.strip()
    body = normalise(text)
    if not body:
        raise BarcodeError("Nothing to parse.")
    if len(body) > _MAX_LENGTH:
        raise BarcodeError(
            f"That is {len(body)} characters; the longest of these codes is a "
            "little over 200."
        )

    if lcsc.looks_like(body):
        reader, supplier = lcsc, lcsc.SUPPLIER
    elif digikey.looks_like(body):
        reader, supplier = digikey, digikey.SUPPLIER
    else:
        raise BarcodeError(
            "Not a code I recognise. Expected a DigiKey label starting '[)>06' "
            "or an LCSC code in braces, such as '{pc:C1554,pm:...,qty:100}'."
        )

    try:
        fields, extra, mode = reader.parse(body)
    except ValueError as error:
        raise BarcodeError(str(error)) from error

    if not (fields.get("mfr_part") or fields.get("supplier_part") or fields.get("customer_part")):
        raise BarcodeError("The barcode has no part number in it.")

    return Barcode(raw=raw, supplier=supplier, parse_mode=mode, fields=fields, extra=extra)


def describe_date_code(code: str) -> str:
    """Render a YYWW date code as a year and week; pass anything else through."""
    code = (code or "").strip()
    if re.fullmatch(r"\d{4}", code):
        year, week = int(code[:2]), int(code[2:])
        if 1 <= week <= 53:
            return f"20{year:02d} week {week}"
    return code
