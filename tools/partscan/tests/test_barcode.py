"""The parser, against the packed form a wedge scanner produces and the
delimited form a scanner that keeps its control characters produces."""

import pytest

from conftest import SAMPLE
from partscan.barcode import EOT, GS, RS, BarcodeError, describe_date_code, parse

EXPECTED = {
    "customer_part": "C50,C51",
    "mfr_part": "CC0402BPNPO9BN8R2",
    "supplier_part": "13-CC0402BPNPO9BN8R2CT-ND",
    "purchase_order": "",
    "order_no": "100273272",
    "invoice": "128845720",
    "date_code": "2336",
    "lot_code": "89M3336039",
    "packing_list": "1",
    "country": "TW",
    "quantity": "10",
    "pick_flag": "PICK",
    "part_id": "21285191",
    "load_id": "999999",
}


def test_packed_sample():
    code = parse(SAMPLE)
    assert code.parse_mode == "packed"
    for key, value in EXPECTED.items():
        assert code.get(key) == value, key
    assert code.quantity == 10
    assert code.get("padding") == "0" * 74


def test_delimited_sample_agrees_with_the_packed_one():
    delimited = "[)>" + RS + "06" + GS + GS.join([
        "PC50,C51",
        "1PCC0402BPNPO9BN8R2",
        "30P13-CC0402BPNPO9BN8R2CT-ND",
        "K",
        "1K100273272",
        "10K128845720",
        "9D2336",
        "1T89M3336039",
        "11K1",
        "4LTW",
        "Q10",
        "11ZPICK",
        "12Z21285191",
        "13Z999999",
    ]) + RS + EOT

    code = parse(delimited)
    assert code.parse_mode == "delimited"
    for key, value in EXPECTED.items():
        assert code.get(key) == value, key


@pytest.mark.parametrize(
    "written",
    [
        "[)>{RS}06{GS}1PCC0402BPNPO9BN8R2{GS}30P13-XCT-ND{GS}Q10",
        "[)>\\x1e06\\x1d1PCC0402BPNPO9BN8R2\\x1d30P13-XCT-ND\\x1dQ10",
        "[)>^^06^]1PCC0402BPNPO9BN8R2^]30P13-XCT-ND^]Q10",
    ],
)
def test_escaped_separators_are_understood(written):
    code = parse(written)
    assert code.get("mfr_part") == "CC0402BPNPO9BN8R2"
    assert code.quantity == 10


def test_whitespace_and_a_trailing_newline_are_ignored():
    code = parse("  " + SAMPLE + "\r\n")
    assert code.get("mfr_part") == "CC0402BPNPO9BN8R2"


def test_a_label_without_the_header_still_reads():
    code = parse("1PCC0402BPNPO9BN8R230P13-XCT-NDQ25")
    assert code.get("mfr_part") == "CC0402BPNPO9BN8R2"
    assert code.quantity == 25


def test_unknown_data_identifiers_are_kept_not_dropped():
    code = parse("[)>" + GS.join(["061PABC123", "30PABC-ND", "Q5", "17V9988"]))
    assert code.extra["17V"] == "9988"


def test_an_empty_field_stays_empty():
    assert parse(SAMPLE).get("purchase_order") == ""


@pytest.mark.parametrize("text", ["", "   ", "hello world", "[)>06", "1234567890"])
def test_rubbish_is_rejected(text):
    with pytest.raises(BarcodeError):
        parse(text)


def test_quantity_of_a_label_without_one_is_zero():
    assert parse("1PABC1230PABC-ND").quantity == 0


@pytest.mark.parametrize(
    "code,expected",
    [("2336", "2023 week 36"), ("2401", "2024 week 1"), ("", ""), ("ABCD", "ABCD"), ("2399", "2399")],
)
def test_date_codes(code, expected):
    assert describe_date_code(code) == expected


def packed_label(mfr, dk, quantity, date_code="2418", lot="A1B2C3", country="TW", customer=""):
    """A label in the form a keyboard-wedge scanner produces, with no separators."""
    return (
        f"[)>06P{customer}1P{mfr}30P{dk}K1K10027327210K1288457209D{date_code}"
        f"1T{lot}11K14L{country}Q{quantity}11ZPICK12Z2128519113Z99999920Z" + "0" * 40
    )


@pytest.mark.parametrize(
    "mfr,dk",
    [
        # Every one of these has a data identifier buried in a part number: 10K
        # is the invoice, 1K the sales order, K the purchase order, 4L the
        # country. Read naively, each would cut a part number in half.
        ("RC0402FR-0710KL", "311-10.0KLRCT-ND"),
        ("RC0402FR-071KL", "311-1.00KLRCT-ND"),
        ("ERJ-2RKF4991X", "P4.99KLCT-ND"),
        ("LTST-C170KGKT", "160-1183-1-ND"),
        ("RMCF0805JT4K70", "RMCF0805JT4K70CT-ND"),
        ("CRCW040210K0FKED", "541-10.0KLCT-ND"),
        ("GRM188R71C104KA01D", "490-1524-1-ND"),
        ("B2B-PH-K-S(LF)(SN)", "455-1704-ND"),
        ("ABM8-16.000MHZ-B2-T", "535-10226-1-ND"),
    ],
)
def test_identifiers_buried_in_part_numbers_do_not_split_them(mfr, dk):
    code = parse(packed_label(mfr, dk, 200))
    assert code.get("mfr_part") == mfr
    assert code.get("supplier_part") == dk
    assert code.quantity == 200
    assert code.get("purchase_order") == ""
    assert code.get("country") == "TW"


def test_a_reel_with_a_purchase_order_keeps_it():
    text = packed_label("CC0402BPNPO9BN8R2", "13-CC0402BPNPO9BN8R2CT-ND", 10)
    text = text.replace("30P13-CC0402BPNPO9BN8R2CT-NDK1K", "30P13-CC0402BPNPO9BN8R2CT-NDKPO123451K")
    code = parse(text)
    assert code.get("purchase_order") == "PO12345"
    assert code.get("supplier_part") == "13-CC0402BPNPO9BN8R2CT-ND"


def test_a_long_paste_is_refused_rather_than_searched():
    with pytest.raises(BarcodeError, match="characters"):
        parse("[)>061P" + "A" * 2000 + "30PX-NDQ1")


def test_an_unreasonable_label_still_returns_quickly():
    import time

    start = time.perf_counter()
    for text in ["[)>06" + "1PQ1K10K4LZZ" * 40, "[)>061P" + "K10KQ4L9D" * 110]:
        try:
            parse(text)
        except BarcodeError:
            pass
    assert time.perf_counter() - start < 2.0


# A second real label: a reel of Panasonic 1 kOhm 0805s. It carries no DigiKey
# part number, no date code and no lot, and its customer field holds two
# reference designators with a comma and a space between them.
ERJ_LABEL = (
    "[)>06PR1, R31PERJ-6ENF1001VK1K6544643310K7540567211K14LCNQ611ZPICK"
    "12Z11895713Z25924920Z" + "0" * 182
)


def test_a_label_missing_the_optional_fields():
    code = parse(ERJ_LABEL)
    assert code.supplier == "DigiKey"
    assert code.get("mfr_part") == "ERJ-6ENF1001V"
    assert code.get("customer_part") == "R1, R3"
    assert code.quantity == 6
    assert code.get("country") == "CN"
    assert code.get("order_no") == "65446433"
    assert code.get("invoice") == "75405672"
    # Absent on this label, and absent is not the same as wrong.
    assert code.get("supplier_part") == ""
    assert code.get("date_code") == ""
    assert code.get("lot_code") == ""


def test_the_two_real_labels_classify():
    from partscan.classify import classify

    erj = parse(ERJ_LABEL)
    guess = classify(erj.get("mfr_part"), erj.get("supplier_part"), erj.supplier)
    assert (guess.category, guess.package, guess.value_text) == ("Resistor", "0805", "1 kΩ")

    reel = parse(SAMPLE)
    guess = classify(reel.get("mfr_part"), reel.get("supplier_part"), reel.supplier)
    assert (guess.category, guess.package, guess.value_text) == ("Capacitor", "0402", "8.2 pF")
