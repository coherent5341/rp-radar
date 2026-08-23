"""
SQLite storage: one row per component, one row per scan.

A label is a receipt — this reel, this lot, this many parts — so scans are kept
individually and the component row is what the parts list is built from. Two
labels for the same part number join the same component, and the quantity on
the list is the sum of its scans.

A component is identified by its manufacturer part number, not by the
supplier's catalogue number, so the same 100 nF 0402 bought from DigiKey and
from LCSC is one line on the list with both bags counted — which is the whole
point of sorting by component. Only when a label carries no manufacturer part
number does the supplier's own code stand in as the identity.
"""

from __future__ import annotations

import json
import os
import sqlite3
from datetime import datetime, timezone
from typing import Any, Dict, Iterable, List, Optional, Tuple

from . import classify
from .barcode import Barcode

DEFAULT_DB = os.environ.get("PARTSCAN_DB", "partscan.sqlite3")

SCHEMA_VERSION = 2

SCHEMA = """
CREATE TABLE IF NOT EXISTS components (
    id             INTEGER PRIMARY KEY,
    part_key       TEXT NOT NULL UNIQUE,
    mfr_part       TEXT NOT NULL DEFAULT '',
    supplier       TEXT NOT NULL DEFAULT '',
    supplier_part  TEXT NOT NULL DEFAULT '',
    customer_part  TEXT NOT NULL DEFAULT '',
    category       TEXT NOT NULL DEFAULT 'Other',
    package        TEXT NOT NULL DEFAULT '',
    value_text     TEXT NOT NULL DEFAULT '',
    value_num      REAL,
    detail         TEXT NOT NULL DEFAULT '',
    created_at     TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS scans (
    id             INTEGER PRIMARY KEY,
    component_id   INTEGER NOT NULL REFERENCES components(id) ON DELETE CASCADE,
    supplier       TEXT NOT NULL DEFAULT '',
    supplier_part  TEXT NOT NULL DEFAULT '',
    quantity       INTEGER NOT NULL DEFAULT 0,
    date_code      TEXT NOT NULL DEFAULT '',
    lot_code       TEXT NOT NULL DEFAULT '',
    country        TEXT NOT NULL DEFAULT '',
    purchase_order TEXT NOT NULL DEFAULT '',
    order_no       TEXT NOT NULL DEFAULT '',
    invoice        TEXT NOT NULL DEFAULT '',
    packing_list   TEXT NOT NULL DEFAULT '',
    part_id        TEXT NOT NULL DEFAULT '',
    load_id        TEXT NOT NULL DEFAULT '',
    parse_mode     TEXT NOT NULL DEFAULT '',
    extra          TEXT NOT NULL DEFAULT '{}',
    raw            TEXT NOT NULL,
    scanned_at     TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS scans_component ON scans (component_id);
CREATE INDEX IF NOT EXISTS scans_raw ON scans (raw);
CREATE INDEX IF NOT EXISTS components_category ON components (category);
"""

# What the parts list can be ordered by. Sorting on the component itself is the
# default: category first, then package and value, which puts every 0402
# capacitor together in capacitance order.
SORTS: Dict[str, Tuple[str, str]] = {
    "component": ("Component", "c.category, c.package, c.value_num IS NULL, c.value_num, c.mfr_part"),
    "category": ("Category", "c.category, c.package, c.value_num IS NULL, c.value_num"),
    "package": ("Package", "c.package = '', c.package, c.category, c.value_num"),
    "value": ("Value", "c.value_num IS NULL, c.value_num, c.category"),
    "mfr_part": ("Manufacturer part", "c.mfr_part"),
    "supplier": ("Supplier", "c.supplier, c.supplier_part"),
    "supplier_part": ("Supplier part", "c.supplier_part"),
    "quantity": ("Quantity", "total_quantity"),
    "scans": ("Scans", "scan_count"),
    "last_scan": ("Last scan", "last_scan"),
}
DEFAULT_SORT = "component"


def connect(path: Optional[str] = None) -> sqlite3.Connection:
    """Open the database, creating it and its schema if need be."""
    path = path or DEFAULT_DB
    if path != ":memory:":
        parent = os.path.dirname(os.path.abspath(path))
        os.makedirs(parent, exist_ok=True)
    conn = sqlite3.connect(path)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    _migrate(conn)
    conn.executescript(SCHEMA)
    conn.execute(f"PRAGMA user_version = {SCHEMA_VERSION}")
    conn.commit()
    return conn


def _migrate(conn: sqlite3.Connection) -> None:
    """
    Bring a database written by an earlier version up to the current schema.

    Version 1 knew only about DigiKey: components were keyed on the DigiKey
    part number and the columns were named for it. Rather than rename columns
    in place, the old tables are set aside, the new ones built, and the rows
    copied across — which also merges any components that were separate only
    because they had been bought under two DigiKey numbers.
    """
    version = conn.execute("PRAGMA user_version").fetchone()[0]
    if version >= SCHEMA_VERSION:
        return
    tables = {row[0] for row in conn.execute("SELECT name FROM sqlite_master WHERE type = 'table'")}
    if "components" not in tables:
        return  # a new database; the schema below is all it needs
    columns = {row["name"] for row in conn.execute("PRAGMA table_info(components)")}
    if "dk_part" not in columns:
        return

    conn.execute("ALTER TABLE components RENAME TO components_v1")
    conn.execute("ALTER TABLE scans RENAME TO scans_v1")
    conn.executescript(SCHEMA)

    remap: Dict[int, int] = {}
    for old in conn.execute("SELECT * FROM components_v1 ORDER BY id"):
        key = part_key(old["mfr_part"], "DigiKey", old["dk_part"])
        row = conn.execute("SELECT id FROM components WHERE part_key = ?", (key,)).fetchone()
        if row:
            remap[int(old["id"])] = int(row["id"])
            continue
        cursor = conn.execute(
            """
            INSERT INTO components (part_key, mfr_part, supplier, supplier_part, customer_part,
                                    category, package, value_text, value_num, detail, created_at)
            VALUES (?, ?, 'DigiKey', ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (key, old["mfr_part"], old["dk_part"], old["customer_part"], old["category"],
             old["package"], old["value_text"], old["value_num"], old["detail"], old["created_at"]),
        )
        remap[int(old["id"])] = int(cursor.lastrowid)

    for old in conn.execute("SELECT s.*, c.dk_part FROM scans_v1 s"
                            " JOIN components_v1 c ON c.id = s.component_id ORDER BY s.id"):
        conn.execute(
            """
            INSERT INTO scans (component_id, supplier, supplier_part, quantity, date_code,
                               lot_code, country, purchase_order, order_no, invoice,
                               packing_list, part_id, load_id, parse_mode, extra, raw, scanned_at)
            VALUES (?, 'DigiKey', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (remap[int(old["component_id"])], old["dk_part"], old["quantity"], old["date_code"],
             old["lot_code"], old["country"], old["purchase_order"], old["sales_order"],
             old["invoice"], old["packing_list"], old["part_id"], old["load_id"],
             old["parse_mode"], old["extra"], old["raw"], old["scanned_at"]),
        )

    conn.execute("DROP TABLE scans_v1")
    conn.execute("DROP TABLE components_v1")
    conn.commit()


def part_key(mfr_part: str, supplier: str, supplier_part: str) -> str:
    """
    What makes two labels the same component: the manufacturer part number,
    which is the same string whoever sold it. Only a label with no
    manufacturer part number falls back to the supplier's own code.
    """
    mfr_part = (mfr_part or "").strip()
    if mfr_part:
        return mfr_part.upper()
    return f"{(supplier or '?').strip()}:{(supplier_part or '').strip()}".upper()


def _now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def find_scan_by_raw(conn: sqlite3.Connection, raw: str) -> Optional[sqlite3.Row]:
    """A label scanned twice is nearly always a slip, so callers can check."""
    return conn.execute("SELECT * FROM scans WHERE raw = ? LIMIT 1", (raw,)).fetchone()


def record(conn: sqlite3.Connection, code: Barcode) -> Tuple[int, int]:
    """Store a decoded label. Returns (component id, scan id)."""
    component_id = _upsert_component(conn, code)
    cursor = conn.execute(
        """
        INSERT INTO scans (component_id, supplier, supplier_part, quantity, date_code,
                           lot_code, country, purchase_order, order_no, invoice,
                           packing_list, part_id, load_id, parse_mode, extra, raw, scanned_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            component_id,
            code.supplier,
            code.get("supplier_part"),
            code.quantity,
            code.get("date_code"),
            code.get("lot_code"),
            code.get("country"),
            code.get("purchase_order"),
            code.get("order_no"),
            code.get("invoice"),
            code.get("packing_list"),
            code.get("part_id"),
            code.get("load_id"),
            code.parse_mode,
            json.dumps(code.extra),
            code.raw,
            _now(),
        ),
    )
    conn.commit()
    return component_id, int(cursor.lastrowid)


def _upsert_component(conn: sqlite3.Connection, code: Barcode) -> int:
    mfr_part = code.get("mfr_part")
    supplier_part = code.get("supplier_part")
    key = part_key(mfr_part, code.supplier, supplier_part)
    row = conn.execute("SELECT id FROM components WHERE part_key = ?", (key,)).fetchone()
    if row:
        # Keep the supplier and code from the most recent bag, and take a
        # customer part number if the first label to arrive did not carry one.
        conn.execute(
            "UPDATE components SET supplier = ?, supplier_part = ? WHERE id = ?",
            (code.supplier, supplier_part, row["id"]),
        )
        if code.get("customer_part"):
            conn.execute(
                "UPDATE components SET customer_part = ? WHERE id = ? AND customer_part = ''",
                (code.get("customer_part"), row["id"]),
            )
        return int(row["id"])

    guess = classify.classify(mfr_part, supplier_part, code.supplier)
    cursor = conn.execute(
        """
        INSERT INTO components (part_key, mfr_part, supplier, supplier_part, customer_part,
                                category, package, value_text, value_num, detail, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            key,
            mfr_part,
            code.supplier,
            supplier_part,
            code.get("customer_part"),
            guess.category,
            guess.package,
            guess.value_text,
            guess.value_num,
            guess.detail,
            _now(),
        ),
    )
    return int(cursor.lastrowid)


def _order_by(sort: str, descending: bool) -> str:
    """
    Build the ORDER BY. The 'IS NULL' and "= ''" terms are guards that keep
    components with no value or no package at the end of the list, so they stay
    ascending whichever way the column itself is sorted.
    """
    terms = SORTS.get(sort, SORTS[DEFAULT_SORT])[1].split(",")
    direction = " DESC" if descending else ""
    return ", ".join(
        term.strip() + ("" if "IS NULL" in term or "= ''" in term else direction)
        for term in terms
    )


def components(
    conn: sqlite3.Connection,
    sort: str = DEFAULT_SORT,
    descending: bool = False,
    category: str = "",
    query: str = "",
) -> List[sqlite3.Row]:
    """The parts list: one row per component, with its scans rolled up."""
    order = _order_by(sort, descending)

    where: List[str] = []
    params: List[Any] = []
    if category:
        where.append("c.category = ?")
        params.append(category)
    if query:
        where.append(
            "(c.mfr_part LIKE ? OR c.supplier_part LIKE ? OR c.customer_part LIKE ?"
            " OR c.category LIKE ? OR c.value_text LIKE ? OR c.package LIKE ?"
            " OR c.supplier LIKE ?)"
        )
        params.extend([f"%{query}%"] * 7)
    clause = f"WHERE {' AND '.join(where)}" if where else ""

    return conn.execute(
        f"""
        SELECT c.*,
               COALESCE(SUM(s.quantity), 0)       AS total_quantity,
               COUNT(s.id)                        AS scan_count,
               MAX(s.scanned_at)                  AS last_scan,
               GROUP_CONCAT(DISTINCT s.supplier)  AS suppliers
        FROM components c
        LEFT JOIN scans s ON s.component_id = c.id
        {clause}
        GROUP BY c.id
        ORDER BY {order}
        """,
        params,
    ).fetchall()


def component(conn: sqlite3.Connection, component_id: int) -> Optional[sqlite3.Row]:
    return conn.execute(
        """
        SELECT c.*,
               COALESCE(SUM(s.quantity), 0)       AS total_quantity,
               COUNT(s.id)                        AS scan_count,
               MAX(s.scanned_at)                  AS last_scan,
               GROUP_CONCAT(DISTINCT s.supplier)  AS suppliers
        FROM components c
        LEFT JOIN scans s ON s.component_id = c.id
        WHERE c.id = ?
        GROUP BY c.id
        """,
        (component_id,),
    ).fetchone()


def scans(conn: sqlite3.Connection, component_id: int) -> List[sqlite3.Row]:
    return conn.execute(
        "SELECT * FROM scans WHERE component_id = ? ORDER BY scanned_at DESC, id DESC",
        (component_id,),
    ).fetchall()


def categories(conn: sqlite3.Connection) -> List[Tuple[str, int]]:
    """Categories in use, with how many components are in each."""
    rows = conn.execute(
        "SELECT category, COUNT(*) AS n FROM components GROUP BY category ORDER BY category"
    ).fetchall()
    return [(row["category"], int(row["n"])) for row in rows]


def totals(conn: sqlite3.Connection) -> Dict[str, int]:
    row = conn.execute(
        """
        SELECT (SELECT COUNT(*) FROM components)            AS components,
               (SELECT COUNT(*) FROM scans)                 AS scans,
               (SELECT COALESCE(SUM(quantity), 0) FROM scans) AS parts
        """
    ).fetchone()
    return {key: int(row[key]) for key in ("components", "scans", "parts")}


def delete_scan(conn: sqlite3.Connection, scan_id: int) -> Optional[int]:
    """Remove a scan, and the component with it if that was its last one."""
    row = conn.execute("SELECT component_id FROM scans WHERE id = ?", (scan_id,)).fetchone()
    if not row:
        return None
    component_id = int(row["component_id"])
    conn.execute("DELETE FROM scans WHERE id = ?", (scan_id,))
    remaining = conn.execute(
        "SELECT COUNT(*) AS n FROM scans WHERE component_id = ?", (component_id,)
    ).fetchone()
    if not remaining["n"]:
        conn.execute("DELETE FROM components WHERE id = ?", (component_id,))
        component_id = 0
    conn.commit()
    return component_id


def delete_component(conn: sqlite3.Connection, component_id: int) -> bool:
    cursor = conn.execute("DELETE FROM components WHERE id = ?", (component_id,))
    conn.commit()
    return cursor.rowcount > 0


def set_category(conn: sqlite3.Connection, component_id: int, category: str) -> bool:
    """Override what the classifier guessed."""
    if category not in classify.CATEGORIES:
        return False
    cursor = conn.execute(
        "UPDATE components SET category = ? WHERE id = ?", (category, component_id)
    )
    conn.commit()
    return cursor.rowcount > 0


def rows_to_dicts(rows: Iterable[sqlite3.Row]) -> List[Dict[str, Any]]:
    return [dict(row) for row in rows]
