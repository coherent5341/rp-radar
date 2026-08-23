"""The LCSC reader, against the code printed on their bags."""

import pytest

from partscan.barcode import BarcodeError, parse
from partscan.lcsc import looks_like

SAMPLE = (
    "{pbn:PICK2409280015,on:GB2409280135,pc:C1554,pm:0402CG200J500NT,qty:100,"
    "mc:C20, C21,cc:1,pdi:129558054,hp:12,wc:ZH}"
)


def test_the_sample_bag():
    code = parse(SAMPLE)
    assert code.supplier == "LCSC"
    assert code.parse_mode == "labelled"
    assert code.get("supplier_part") == "C1554"
    assert code.get("mfr_part") == "0402CG200J500NT"
    assert code.get("customer_part") == "C20, C21"
    assert code.get("order_no") == "GB2409280135"
    assert code.get("part_id") == "129558054"
    assert code.get("pick_flag") == "PICK2409280015"
    assert code.quantity == 100


def test_a_value_containing_a_comma_is_not_split():
    # mc holds two reference designators. Splitting on every comma would leave
    # "C21" looking like a key with no value, and lose half the field.
    assert parse(SAMPLE).get("customer_part") == "C20, C21"


def test_fields_we_do_not_name_are_kept_verbatim():
    assert parse(SAMPLE).extra == {"cc": "1", "hp": "12", "wc": "ZH"}


def test_the_warehouse_code_is_not_mistaken_for_a_country():
    code = parse(SAMPLE)
    assert code.get("country") == ""
    assert code.extra["wc"] == "ZH"


def test_an_lcsc_bag_has_no_date_code_and_that_is_fine():
    code = parse(SAMPLE)
    assert code.get("date_code") == ""
    assert code.get("lot_code") == ""


@pytest.mark.parametrize(
    "text",
    [
        SAMPLE,
        SAMPLE + "\n",
        "  " + SAMPLE + "  \r\n",
        SAMPLE.strip("{}"),                        # a scanner that ate the braces
        "{pc:C1554,pm:0402CG200J500NT,qty:100}",   # a shorter code
        "{PC:C1554,PM:0402CG200J500NT,QTY:100}",   # upper case keys
        "{ pc : C1554 , pm : 0402CG200J500NT , qty : 100 }",  # spaced out
    ],
)
def test_forms_that_should_all_read(text):
    code = parse(text)
    assert code.get("supplier_part") == "C1554"
    assert code.get("mfr_part") == "0402CG200J500NT"
    assert code.quantity == 100


def test_key_order_does_not_matter():
    code = parse("{qty:5,pm:0402CG200J500NT,pc:C1554}")
    assert code.get("mfr_part") == "0402CG200J500NT"
    assert code.quantity == 5


def test_a_colon_inside_a_value_is_left_alone():
    assert parse("{pc:C1554,mc:U1:pin3,qty:1}").get("customer_part") == "U1:pin3"


def test_an_empty_value_is_empty_not_missing():
    code = parse("{pc:C1554,mc:,qty:1}")
    assert code.get("customer_part") == ""
    assert code.quantity == 1


def test_a_code_with_no_part_number_is_refused():
    with pytest.raises(BarcodeError, match="no part number"):
        parse("{on:GB2409280135,qty:100,wc:ZH}")


@pytest.mark.parametrize("text", ["{}", "{,,}", "{no pairs here}"])
def test_rubbish_in_braces_is_refused(text):
    with pytest.raises(BarcodeError):
        parse(text)


def test_looks_like_tells_the_two_suppliers_apart():
    assert looks_like(SAMPLE)
    assert not looks_like("[)>061PCC0402BPNPO9BN8R2Q10")


def test_a_digikey_label_is_not_read_as_lcsc():
    assert parse("[)>061PCC0402BPNPO9BN8R2Q10").supplier == "DigiKey"
