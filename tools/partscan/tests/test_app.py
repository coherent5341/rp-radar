"""The web application: scanning, storing, grouping, sorting and the API."""

import pytest

from conftest import SAMPLE
from partscan import db

SECOND_LABEL = "[)>061PRC0402FR-0710KL30P311-10.0KLRCT-ND9D24011T1234Q100"
THIRD_LABEL = "[)>061PGRM188R71C104KA01D30P490-1524-1-ND9D24051TABCQ50"


def scan(client, text, force=False):
    data = {"barcode": text}
    if force:
        data["force"] = "on"
    return client.post("/scan", data=data, follow_redirects=True)


def test_an_empty_list_says_so(client):
    page = client.get("/").get_data(as_text=True)
    assert "No parts yet" in page


def test_scanning_a_label_puts_it_on_the_list(client):
    page = scan(client, SAMPLE).get_data(as_text=True)
    assert "Recorded 10 x CC0402BPNPO9BN8R2" in page
    assert "CC0402BPNPO9BN8R2" in page
    assert "8.2 pF" in page       # classified
    assert "0402" in page
    assert "Capacitor" in page


def test_a_bad_barcode_is_reported_not_stored(client):
    page = scan(client, "not a barcode").get_data(as_text=True)
    assert "Could not read that barcode" in page
    assert client.application.config["DATABASE"]
    with client.application.app_context():
        conn = db.connect(client.application.config["DATABASE"])
        assert db.totals(conn)["scans"] == 0


def test_the_same_label_twice_is_caught(client):
    scan(client, SAMPLE)
    page = scan(client, SAMPLE).get_data(as_text=True)
    assert "already recorded" in page
    conn = db.connect(client.application.config["DATABASE"])
    assert db.totals(conn)["scans"] == 1


def test_a_duplicate_can_be_forced_through(client):
    scan(client, SAMPLE)
    scan(client, SAMPLE, force=True)
    conn = db.connect(client.application.config["DATABASE"])
    assert db.totals(conn) == {"components": 1, "scans": 2, "parts": 20}


def test_two_labels_for_one_part_share_a_component_and_add_up(client):
    scan(client, "[)>061PRC0402FR-0710KL30P311-10.0KLRCT-ND9D24011TAAAQ100")
    scan(client, "[)>061PRC0402FR-0710KL30P311-10.0KLRCT-ND9D24051TBBBQ250")
    conn = db.connect(client.application.config["DATABASE"])
    totals = db.totals(conn)
    assert totals["components"] == 1
    assert totals["scans"] == 2
    assert totals["parts"] == 350


@pytest.mark.parametrize("sort", sorted(db.SORTS))
@pytest.mark.parametrize("direction", ["asc", "desc"])
def test_every_sort_works_both_ways(client, sort, direction):
    for label in (SAMPLE, SECOND_LABEL, THIRD_LABEL):
        scan(client, label)
    response = client.get(f"/?sort={sort}&dir={direction}")
    assert response.status_code == 200
    assert b"CC0402BPNPO9BN8R2" in response.data


def test_sorting_by_value_orders_by_the_number_not_the_text(client):
    for label in (SAMPLE, THIRD_LABEL):  # 8.2 pF and 100 nF
        scan(client, label)
    body = client.get("/?sort=value&dir=asc").get_data(as_text=True)
    assert body.index("8.2 pF") < body.index("100 nF")
    body = client.get("/?sort=value&dir=desc").get_data(as_text=True)
    assert body.index("100 nF") < body.index("8.2 pF")


def test_components_with_no_value_sort_to_the_end_either_way(client):
    scan(client, SAMPLE)                                   # has a value
    scan(client, "[)>061PBSS13830PBSS138CT-ND9D2401Q3000")  # has none
    for direction in ("asc", "desc"):
        body = client.get(f"/?sort=value&dir={direction}").get_data(as_text=True)
        assert body.index("CC0402BPNPO9BN8R2") < body.index("BSS138"), direction


def test_an_unknown_sort_key_falls_back_rather_than_failing(client):
    scan(client, SAMPLE)
    assert client.get("/?sort=; DROP TABLE components").status_code == 200
    conn = db.connect(client.application.config["DATABASE"])
    assert db.totals(conn)["components"] == 1


def test_filtering_by_category(client):
    scan(client, SAMPLE)         # capacitor
    scan(client, SECOND_LABEL)   # resistor
    body = client.get("/?category=Resistor").get_data(as_text=True)
    assert "RC0402FR-0710KL" in body
    assert "CC0402BPNPO9BN8R2" not in body


def test_searching_part_numbers(client):
    scan(client, SAMPLE)
    scan(client, SECOND_LABEL)
    body = client.get("/?q=RC0402").get_data(as_text=True)
    assert "RC0402FR-0710KL" in body
    assert "CC0402BPNPO9BN8R2" not in body


def test_the_component_page_lists_each_scan(client):
    scan(client, "[)>061PRC0402FR-0710KL30P311-10.0KLRCT-ND9D24011TAAAQ100")
    scan(client, "[)>061PRC0402FR-0710KL30P311-10.0KLRCT-ND9D24051TBBBQ250")
    body = client.get("/component/1").get_data(as_text=True)
    assert "AAA" in body and "BBB" in body
    assert "2024 week 1" in body and "2024 week 5" in body
    assert "350" in body


def test_a_reconstructed_scan_is_flagged_on_the_component_page(client):
    scan(client, SAMPLE)
    assert "reconstructed" in client.get("/component/1").get_data(as_text=True)


def test_the_category_guess_can_be_corrected(client):
    scan(client, "[)>061PSOMETHING-WEIRD-4230PXYZ-NDQ7")
    client.post("/component/1/category", data={"category": "Sensor"}, follow_redirects=True)
    assert "Sensor" in client.get("/component/1").get_data(as_text=True)


def test_a_category_that_is_not_one_is_refused(client):
    scan(client, SAMPLE)
    page = client.post(
        "/component/1/category", data={"category": "Sausages"}, follow_redirects=True
    ).get_data(as_text=True)
    assert "not a category" in page


def test_deleting_the_last_scan_takes_the_component_with_it(client):
    scan(client, SAMPLE)
    client.post("/scan/1/delete", follow_redirects=True)
    conn = db.connect(client.application.config["DATABASE"])
    assert db.totals(conn) == {"components": 0, "scans": 0, "parts": 0}


def test_deleting_one_of_two_scans_keeps_the_component(client):
    scan(client, SAMPLE)
    scan(client, SAMPLE, force=True)
    client.post("/scan/1/delete", follow_redirects=True)
    conn = db.connect(client.application.config["DATABASE"])
    assert db.totals(conn) == {"components": 1, "scans": 1, "parts": 10}


def test_deleting_a_component_deletes_its_scans(client):
    scan(client, SAMPLE)
    scan(client, SAMPLE, force=True)
    client.post("/component/1/delete", follow_redirects=True)
    conn = db.connect(client.application.config["DATABASE"])
    assert db.totals(conn) == {"components": 0, "scans": 0, "parts": 0}


def test_missing_things_are_404(client):
    assert client.get("/component/999").status_code == 404
    assert client.post("/scan/999/delete").status_code == 404
    assert client.post("/component/999/delete").status_code == 404


def test_api_scan_returns_the_decoded_label(client):
    response = client.post("/api/scan", json={"barcode": SAMPLE})
    assert response.status_code == 201
    body = response.get_json()
    assert body["duplicate"] is False
    assert body["barcode"]["mfr_part"] == "CC0402BPNPO9BN8R2"
    assert body["barcode"]["quantity"] == 10
    assert body["barcode"]["date_code_text"] == "2023 week 36"
    assert body["component"]["category"] == "Capacitor"
    assert body["component"]["value_text"] == "8.2 pF"


def test_api_scan_reports_a_duplicate_without_storing_it(client):
    client.post("/api/scan", json={"barcode": SAMPLE})
    response = client.post("/api/scan", json={"barcode": SAMPLE})
    assert response.status_code == 200
    assert response.get_json()["duplicate"] is True


def test_api_scan_rejects_rubbish(client):
    response = client.post("/api/scan", json={"barcode": "nope"})
    assert response.status_code == 400
    assert "error" in response.get_json()


def test_api_components_lists_what_was_scanned(client):
    scan(client, SAMPLE)
    body = client.get("/api/components").get_json()
    assert len(body) == 1
    assert body[0]["mfr_part"] == "CC0402BPNPO9BN8R2"
    assert body[0]["total_quantity"] == 10


def test_api_parse_decodes_without_storing(client):
    body = client.get("/api/parse", query_string={"barcode": SAMPLE}).get_json()
    assert body["supplier"] == "DigiKey"
    assert body["supplier_part"] == "13-CC0402BPNPO9BN8R2CT-ND"
    conn = db.connect(client.application.config["DATABASE"])
    assert db.totals(conn)["scans"] == 0


def test_csv_export(client):
    scan(client, SAMPLE)
    response = client.get("/export.csv")
    assert response.status_code == 200
    assert "text/csv" in response.headers["Content-Type"]
    lines = response.get_data(as_text=True).splitlines()
    assert lines[0].startswith("category,package,value_text")
    assert "CC0402BPNPO9BN8R2" in lines[1]


def test_health(client):
    assert client.get("/healthz").get_json() == {"status": "ok"}


# An LCSC bag of 20 pF 0402 capacitors, as their code comes off the scanner.
LCSC_LABEL = (
    "{pbn:PICK2409280015,on:GB2409280135,pc:C1554,pm:0402CG200J500NT,qty:100,"
    "mc:C20, C21,cc:1,pdi:129558054,hp:12,wc:ZH}"
)
# A DigiKey reel of the same part number, from the other supplier.
SAME_PART_FROM_DIGIKEY = "[)>061P0402CG200J500NT30P1276-1000-1-ND9D2438Q2504LCNQ250"


def test_an_lcsc_code_is_read_and_stored(client):
    page = scan(client, LCSC_LABEL).get_data(as_text=True)
    assert "Recorded 100 x 0402CG200J500NT from LCSC" in page
    assert "0402CG200J500NT" in page
    assert "C1554" in page      # the LCSC catalogue code
    assert "20 pF" in page      # classified from the part number
    assert "0402" in page


def test_an_lcsc_value_containing_a_comma_survives(client):
    scan(client, LCSC_LABEL)
    body = client.get("/component/1").get_data(as_text=True)
    assert "C20, C21" in body


def test_lcsc_fields_we_do_not_name_are_kept(client):
    scan(client, LCSC_LABEL)
    body = client.get("/component/1").get_data(as_text=True)
    for kept in ("cc=1", "hp=12", "wc=ZH"):
        assert kept in body


def test_an_lcsc_scan_is_not_flagged_as_reconstructed(client):
    scan(client, LCSC_LABEL)
    assert "reconstructed" not in client.get("/component/1").get_data(as_text=True)


def test_the_same_part_from_two_suppliers_is_one_component(client):
    scan(client, LCSC_LABEL)                 # 100 from LCSC
    scan(client, SAME_PART_FROM_DIGIKEY)     # 250 from DigiKey
    conn = db.connect(client.application.config["DATABASE"])
    assert db.totals(conn) == {"components": 1, "scans": 2, "parts": 350}
    body = client.get("/component/1").get_data(as_text=True)
    assert "LCSC" in body and "DigiKey" in body


def test_supplier_shows_on_the_list_and_sorts(client):
    scan(client, LCSC_LABEL)
    scan(client, SAMPLE)
    body = client.get("/?sort=supplier&dir=asc").get_data(as_text=True)
    assert body.index("CC0402BPNPO9BN8R2") < body.index("0402CG200J500NT")
    body = client.get("/?sort=supplier&dir=desc").get_data(as_text=True)
    assert body.index("0402CG200J500NT") < body.index("CC0402BPNPO9BN8R2")


def test_searching_finds_a_part_by_its_lcsc_code(client):
    scan(client, LCSC_LABEL)
    scan(client, SAMPLE)
    body = client.get("/?q=C1554").get_data(as_text=True)
    assert "0402CG200J500NT" in body
    assert "CC0402BPNPO9BN8R2" not in body


def test_api_scan_reports_the_supplier(client):
    body = client.post("/api/scan", json={"barcode": LCSC_LABEL}).get_json()
    assert body["barcode"]["supplier"] == "LCSC"
    assert body["barcode"]["supplier_part"] == "C1554"
    assert body["barcode"]["customer_part"] == "C20, C21"
    assert body["barcode"]["extra"] == {"cc": "1", "hp": "12", "wc": "ZH"}
    assert body["component"]["value_text"] == "20 pF"


def test_csv_export_names_the_supplier(client):
    scan(client, LCSC_LABEL)
    lines = client.get("/export.csv").get_data(as_text=True).splitlines()
    assert "supplier" in lines[0] and "supplier_part" in lines[0]
    assert "LCSC" in lines[1] and "C1554" in lines[1]
