"""Storage: component identity, and bringing an older database forward."""

import sqlite3

from conftest import SAMPLE
from partscan import db
from partscan.barcode import parse

# The schema as version 1 wrote it: DigiKey only, and components keyed on the
# DigiKey part number.
SCHEMA_V1 = """
CREATE TABLE components (
    id INTEGER PRIMARY KEY,
    mfr_part TEXT NOT NULL DEFAULT '',
    dk_part TEXT NOT NULL DEFAULT '',
    customer_part TEXT NOT NULL DEFAULT '',
    category TEXT NOT NULL DEFAULT 'Other',
    package TEXT NOT NULL DEFAULT '',
    value_text TEXT NOT NULL DEFAULT '',
    value_num REAL,
    detail TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL,
    UNIQUE (mfr_part, dk_part)
);
CREATE TABLE scans (
    id INTEGER PRIMARY KEY,
    component_id INTEGER NOT NULL REFERENCES components(id) ON DELETE CASCADE,
    quantity INTEGER NOT NULL DEFAULT 0,
    date_code TEXT NOT NULL DEFAULT '',
    lot_code TEXT NOT NULL DEFAULT '',
    country TEXT NOT NULL DEFAULT '',
    purchase_order TEXT NOT NULL DEFAULT '',
    sales_order TEXT NOT NULL DEFAULT '',
    invoice TEXT NOT NULL DEFAULT '',
    packing_list TEXT NOT NULL DEFAULT '',
    part_id TEXT NOT NULL DEFAULT '',
    load_id TEXT NOT NULL DEFAULT '',
    parse_mode TEXT NOT NULL DEFAULT '',
    extra TEXT NOT NULL DEFAULT '{}',
    raw TEXT NOT NULL,
    scanned_at TEXT NOT NULL
);
"""


def write_v1_database(path):
    conn = sqlite3.connect(path)
    conn.executescript(SCHEMA_V1)
    conn.execute(
        "INSERT INTO components (id, mfr_part, dk_part, customer_part, category, package,"
        " value_text, value_num, detail, created_at) VALUES"
        " (1, 'CC0402BPNPO9BN8R2', '13-CC0402BPNPO9BN8R2CT-ND', 'C50,C51', 'Capacitor',"
        " '0402', '8.2 pF', 8.2e-12, '', '2026-08-01T09:00:00+00:00')"
    )
    # The same part under a second DigiKey number: two rows in version 1, one
    # component now that identity is the manufacturer part number.
    conn.execute(
        "INSERT INTO components (id, mfr_part, dk_part, customer_part, category, package,"
        " value_text, value_num, detail, created_at) VALUES"
        " (2, 'CC0402BPNPO9BN8R2', '13-CC0402BPNPO9BN8R2TR-ND', '', 'Capacitor',"
        " '0402', '8.2 pF', 8.2e-12, '', '2026-08-02T09:00:00+00:00')"
    )
    for scan_id, component_id, quantity in ((1, 1, 10), (2, 1, 40), (3, 2, 50)):
        conn.execute(
            "INSERT INTO scans (id, component_id, quantity, date_code, sales_order, invoice,"
            " parse_mode, extra, raw, scanned_at) VALUES (?, ?, ?, '2336', '100273272',"
            " '128845720', 'packed', '{}', ?, '2026-08-01T09:00:00+00:00')",
            (scan_id, component_id, quantity, f"raw-label-{scan_id}"),
        )
    conn.commit()
    conn.close()


def test_an_old_database_is_brought_forward_without_losing_anything(tmp_path):
    path = str(tmp_path / "v1.sqlite3")
    write_v1_database(path)

    conn = db.connect(path)

    totals = db.totals(conn)
    assert totals["scans"] == 3
    assert totals["parts"] == 100
    assert totals["components"] == 1  # the two DigiKey numbers were one part

    row = db.components(conn)[0]
    assert row["mfr_part"] == "CC0402BPNPO9BN8R2"
    assert row["supplier"] == "DigiKey"
    assert row["supplier_part"] == "13-CC0402BPNPO9BN8R2CT-ND"
    assert row["customer_part"] == "C50,C51"
    assert row["value_text"] == "8.2 pF"
    assert row["total_quantity"] == 100

    scans = db.scans(conn, int(row["id"]))
    assert len(scans) == 3
    assert {scan["raw"] for scan in scans} == {"raw-label-1", "raw-label-2", "raw-label-3"}
    assert all(scan["supplier"] == "DigiKey" for scan in scans)
    assert scans[0]["order_no"] == "100273272"  # was sales_order


def test_migrating_twice_is_a_no_op(tmp_path):
    path = str(tmp_path / "v1.sqlite3")
    write_v1_database(path)
    db.connect(path).close()
    conn = db.connect(path)
    assert db.totals(conn)["scans"] == 3
    assert conn.execute("PRAGMA user_version").fetchone()[0] == db.SCHEMA_VERSION


def test_a_new_database_is_stamped_with_the_schema_version(tmp_path):
    conn = db.connect(str(tmp_path / "new.sqlite3"))
    assert conn.execute("PRAGMA user_version").fetchone()[0] == db.SCHEMA_VERSION


def test_identity_is_the_manufacturer_part_number(tmp_path):
    assert db.part_key("CC0402BPNPO9BN8R2", "LCSC", "C1554") == "CC0402BPNPO9BN8R2"
    assert db.part_key(" cc0402 ", "DigiKey", "x") == "CC0402"


def test_a_label_with_no_manufacturer_part_falls_back_to_the_supplier_code(tmp_path):
    assert db.part_key("", "LCSC", "C1554") == "LCSC:C1554"
    assert db.part_key("", "DigiKey", "296-1234-ND") == "DIGIKEY:296-1234-ND"


def test_two_suppliers_of_one_part_share_a_component(tmp_path):
    conn = db.connect(str(tmp_path / "mixed.sqlite3"))
    db.record(conn, parse("{pc:C1554,pm:0402CG200J500NT,qty:100}"))
    db.record(conn, parse("[)>061P0402CG200J500NT30P1276-1000-1-NDQ250"))
    assert db.totals(conn) == {"components": 1, "scans": 2, "parts": 350}
    row = db.components(conn)[0]
    assert sorted((row["suppliers"] or "").split(",")) == ["DigiKey", "LCSC"]
    # The component carries the most recent bag's code, the scans carry both.
    assert row["supplier_part"] == "1276-1000-1-ND"
    assert {scan["supplier_part"] for scan in db.scans(conn, int(row["id"]))} == {
        "C1554", "1276-1000-1-ND"}


def test_the_original_sample_still_records(tmp_path):
    conn = db.connect(str(tmp_path / "sample.sqlite3"))
    component_id, scan_id = db.record(conn, parse(SAMPLE))
    assert component_id and scan_id
    assert db.component(conn, component_id)["value_text"] == "8.2 pF"
