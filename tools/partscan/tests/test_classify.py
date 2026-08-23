"""The classifier, against part numbers from the series it claims to know."""

import pytest

from partscan.classify import classify, format_capacitance, format_resistance


@pytest.mark.parametrize(
    "mpn,category,package,value",
    [
        # capacitors, one per encoding style
        ("CC0402BPNPO9BN8R2", "Capacitor", "0402", "8.2 pF"),
        ("CC0402KRX7R9BB104", "Capacitor", "0402", "100 nF"),
        ("GRM188R71C104KA01D", "Capacitor", "0603", "100 nF"),
        ("GRM155R60J475ME87D", "Capacitor", "0402", "4.7 µF"),
        ("GJM1555C1H8R2DB01D", "Capacitor", "0402", "8.2 pF"),
        ("CL10A106MQ8NNNC", "Capacitor", "0603", "10 µF"),
        ("C0402C104K4RACTU", "Capacitor", "0402", "100 nF"),
        ("C1608X7R1H104K080AB", "Capacitor", "0603", "100 nF"),
        ("T491A105K016AT", "Capacitor", "", "1 µF"),
        ("EEU-FR1H101", "Capacitor", "", "100 µF"),  # electrolytic codes are microfarads
        # resistors
        ("RC0402FR-0710KL", "Resistor", "0402", "10 kΩ"),
        ("RC0603JR-071KL", "Resistor", "0603", "1 kΩ"),
        ("CRCW040210K0FKED", "Resistor", "0402", "10 kΩ"),
        ("ERJ-2RKF1002X", "Resistor", "0402", "10 kΩ"),
        ("ERJ-3GEYJ103V", "Resistor", "0603", "10 kΩ"),
        ("RMCF0805JT4K70", "Resistor", "0805", "4.7 kΩ"),
        ("CRCW0603100RFKEA", "Resistor", "0603", "100 Ω"),
        ("CRCW04021R00FKED", "Resistor", "0402", "1 Ω"),
        ("ERJ-2RKF4991X", "Resistor", "0402", "4.99 kΩ"),
        ("RK73H1ETTP1002F", "Resistor", "", "10 kΩ"),
        ("CR0603-FX-1002ELF", "Resistor", "0603", "10 kΩ"),
        # magnetics
        ("LQW18AN10NJ00D", "Inductor", "0603", "10 nH"),
        ("LQH32PN4R7NN0", "Inductor", "1210", "4.7 µH"),
        ("BLM18PG221SN1D", "Ferrite bead", "0603", ""),
    ],
)
def test_passives(mpn, category, package, value):
    result = classify(mpn)
    assert result.category == category
    assert result.package == package
    assert result.value_text == value


@pytest.mark.parametrize(
    "mpn,category",
    [
        ("PMEG3005EB,115", "Diode"),
        ("1N4148W-7-F", "Diode"),
        ("LTST-C170KGKT", "LED"),
        ("BSS138", "Transistor"),
        ("MMBT3904-7-F", "Transistor"),
        ("STM32F103C8T6", "Integrated circuit"),
        ("W25Q128JVSIQ", "Integrated circuit"),
        ("SN74LVC1G14DBVR", "Integrated circuit"),
        ("A121", "Sensor"),
        ("ABM8-16.000MHZ-B2-T", "Crystal / oscillator"),
        ("B2B-PH-K-S(LF)(SN)", "Connector"),
        ("PPTC081LFBN-RC", "Connector"),
        ("SOMETHING-WEIRD-42", "Other"),
        ("", "Other"),
    ],
)
def test_categories(mpn, category):
    assert classify(mpn).category == category


def test_the_digikey_number_is_used_when_there_is_no_manufacturer_one():
    result = classify("", "13-CC0402BPNPO9BN8R2CT-ND")
    assert result.category == "Capacitor"
    assert result.package == "0402"
    assert result.value_text == "8.2 pF"


def test_dielectric_is_reported():
    assert classify("CC0402KRX7R9BB104").detail == "X7R"
    assert classify("C1608C0G1H103J080AA").detail == "C0G/NP0"


def test_values_sort_numerically():
    parts = ["RC0402FR-07100KL", "RC0402FR-071KL", "RC0402FR-0710KL"]
    values = sorted(classify(part).value_num for part in parts)
    assert values == [1e3, 10e3, 100e3]


@pytest.mark.parametrize(
    "farads,text",
    [(8.2e-12, "8.2 pF"), (1e-7, "100 nF"), (4.7e-6, "4.7 µF"), (1e-3, "1 mF")],
)
def test_capacitance_formatting(farads, text):
    assert format_capacitance(farads) == text


@pytest.mark.parametrize("ohms,text", [(0.1, "0.1 Ω"), (49.9, "49.9 Ω"), (4700, "4.7 kΩ"), (1e6, "1 MΩ")])
def test_resistance_formatting(ohms, text):
    assert format_resistance(ohms) == text


def test_nothing_raises_on_odd_input():
    for part in ["", "-", "///", "0000", "1" * 200, "CC0402"]:
        classify(part, part, part)
