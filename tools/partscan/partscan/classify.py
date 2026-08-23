"""
Work out what a part actually is from its part number.

A DigiKey label carries no description — just part numbers, a quantity and some
order paperwork. To sort a parts list by anything useful (all the 0402
capacitors together, resistors in resistance order) the category, the package
and the value have to be recovered from the manufacturer part number.

That is done with a table of series prefixes, and value readers written per
family, because 104 means 100 nF on an MLCC and 100 uF on an aluminium
electrolytic, and 1002 means 10 kilohm only if the series uses four-digit
codes. Anything not in the table lands in "Other" with no package and no
value: a missing value sorts to the end, a wrong one is worse than useless.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

CATEGORIES: Tuple[str, ...] = (
    "Capacitor",
    "Resistor",
    "Inductor",
    "Ferrite bead",
    "Diode",
    "LED",
    "Transistor",
    "Integrated circuit",
    "Sensor",
    "Crystal / oscillator",
    "Connector",
    "Switch",
    "Fuse",
    "Other",
)

UNKNOWN = "Other"

# (pattern, category, family). Ordered: the first match wins, so the specific
# series come before the loose ones. The family tag picks the value reader.
_SERIES: Tuple[Tuple[str, str, str], ...] = (
    # --- capacitors -------------------------------------------------------
    (r"^(EEU|EEE|EEH|EEV|ECA|ECE|UVR|UVZ|UPW|UHE|UWT|ELXZ|ESK|UCD|UFW)", "Capacitor", "electrolytic"),
    (r"^(CC|CQ|CS|CT)\d{4}", "Capacitor", "mlcc"),                  # Yageo
    (r"^C\d{4}[CXYZJ]", "Capacitor", "mlcc"),                       # TDK / KEMET
    (r"^(GRM|GCM|GJM|GRT|GQM|GA[23]|KRM|NFM|GMD)", "Capacitor", "mlcc"),  # Murata
    (r"^CL\d{2}", "Capacitor", "mlcc"),                             # Samsung
    (r"^(CGA|CKG|CNA)", "Capacitor", "mlcc"),                       # TDK automotive
    (r"^(VJ|MC\d{4})", "Capacitor", "mlcc"),                        # Vishay
    (r"^(TAJ|TPS[A-E]\d|T49\d|T52\d|T35\d|F98)", "Capacitor", "mlcc"),  # tantalum / polymer
    (r"^(ECQ|ECW|R7\d|B32|MKP|PHE|F339)", "Capacitor", "mlcc"),     # film
    (r"^(0075|0100|0201|0402|0603|0805|1206|1210|1812|2220|2225)"
     r"(C0G|CG|NP0|X7R|X5R|X7S|X6S|Y5V|Z5U|[BF]\d)", "Capacitor", "mlcc"),  # Fenghua and the like
    (r"(C0G|NP0|X7R|X5R|X7S|X6S|Y5V|Z5U|X7T)", "Capacitor", "mlcc"),  # dielectric anywhere
    # --- resistors --------------------------------------------------------
    (r"^ERJ", "Resistor", "eia_tail"),                              # Panasonic
    (r"^(RK73|RR\d|SR73|WK73|MCR\d|ESR\d|LTR\d|UCR\d|RG\d{4}|PRL)", "Resistor", "eia_tail"),
    (r"^(RC|AC|AF|AA|RT|RL|RE|PA|MFR|SFR|LR)\d{4}", "Resistor", "yageo"),
    (r"^(CRCW|RCWE|WSL|WSLP|LVK|MCT\d|MCS\d|MCA\d)", "Resistor", "rkm"),  # Vishay
    (r"^(RMCF|RMCP|RNCF|RNCP|CSR\d|CSNL|RNCS)", "Resistor", "rkm"), # Stackpole
    (r"^(CR\d{4}|CRL|CRM|CRA|PWR\d)", "Resistor", "rkm"),           # Bourns
    (r"^(0075|0100|0201|0402|0603|0805|1206|1210|1812|2010|2512)W[A-Z]{2}", "Resistor", "eia_tail"),  # Uniroyal and the like
    # --- magnetics --------------------------------------------------------
    (r"^(BLM|MPZ|MMZ|BK\d|HZ\d|742\d{3})", "Ferrite bead", ""),
    (r"^(LQ[WGHMP]|DFE|NLV|NLC|MLZ|MLF|MLK|SRN|SRR|SRP|SRU|IHLP|XAL|XFL|XGL|SLF|VLS|CBC|LPS|MSS|744\d)", "Inductor", "inductor"),
    (r"^\d{4}(HP|CS|PS)", "Inductor", "inductor"),                  # Coilcraft
    # --- discretes --------------------------------------------------------
    (r"^(LTST|LTW-|SML-|APT\d|APHHS|WP\d|LNJ|VLM|HSMH|HSMG|151\d{3}|597-)", "LED", ""),
    (r"^(1N\d|BAT\d|BAV\d|BAS\d|BAW\d|BZX|BZT|MMSZ|MMBD|MBR|SS1|SS2|SS3|PMEG|RB\d|CDBU|CDSU|SMAJ|SMBJ|SMCJ|SMF\d|PESD|ESD\d|DFLS)", "Diode", ""),
    (r"^(2N\d|MMBT|BC8|BC[45]\d|BSS\d|BSC\d|BSZ\d|IRF|IRL|SI\d{4}|AO\d{3}|DMN\d|DMP\d|FDN\d|FDV\d|NTR\d|PMV\d|SSM\d|CSD\d|SS?80\d{2}|SS?85\d{2}|S90\d{2})", "Transistor", ""),
    # --- timing -----------------------------------------------------------
    (r"^(ABM|ABLS|ABS0|ABS2|ABLNO|ASE\d|ASV\d|ASDMB|ECS-|CSTNE|CSTCE|NX\d{4}|FA-\d|SG-\d|7M-|9B-|LFXTAL|FC-\d|TSX-)", "Crystal / oscillator", ""),
    # --- sensors ----------------------------------------------------------
    (r"^(BME\d|BMP\d|BMI\d|BNO\d|MPU-?\d|LSM\d|LIS\d|SHT\d|HDC\d|TMP\d{3}|MLX\d|VL53|ICM-?\d|VEML|TSL\d|APDS|A11\d|A121)", "Sensor", ""),
    # --- interconnect and electromechanical -------------------------------
    (r"^(PPTC|PPPC|PREC|SSW-|SSQ-|HLE-|TSW-|BCS-|ESW-|SAM\d|TSM-)", "Connector", ""),
    (r"^(00\d{8}|0022\d|0043\d|0533\d|105\d{3}|WM\d{4}|39-\d|47\d{3}-)", "Connector", ""),
    (r"^(B\d{1,2}B-|S\d{1,2}B-|SM\d{2}B-|BM\d{2}B-|PH\d?-|ZH\d?-|GH\d?-)", "Connector", ""),
    (r"^(DF\d{2}|FH\d{2}|HR\d{2}|61\d{4}|62\d{4}|09\d{6})", "Connector", ""),
    (r"^(TL\d{4}|EVQ|EVP|PTS\d|B3[FSU]|KSC\d|SKQ|SKR|1825\d{3}|JS\d{6})", "Switch", ""),
    (r"^(0451|0453|0603L|0805L|1206L|1812L|MF-|MFU|SMD\d{3}|ERB-?RE|SF-\d)", "Fuse", ""),
    # --- integrated circuits ---------------------------------------------
    (r"^(SN74|CD4\d|74[A-Z]{2}|MC74|NC7|TXB|TXS|LSF\d|PCA\d|PCF\d)", "Integrated circuit", ""),
    (r"^(LM\d|TL\d{3}|NE5\d|OPA\d|INA\d|ADS\d|AD\d{3}|MAX\d|LT\d{3}|LTC\d|MCP\d|NCP\d|AP\d{4}|TPS\d|TLV\d|TPD\d|LP\d{4}|REF\d|XC\d{4}|RT\d{4})", "Integrated circuit", ""),
    (r"^(PIC\d|ATMEGA|ATTINY|ATSAM|STM32|STM8|LPC\d|MSP430|NRF5|ESP32|ESP8266|RP2040|RP2350|EFM32|GD32|CH32)", "Integrated circuit", ""),
    (r"^(24[LAC]C|25[LAQ]C|AT24|AT25|W25Q|IS2\d|SST2|GD25)", "Integrated circuit", ""),
)

_SERIES_COMPILED = tuple(
    (re.compile(pattern, re.IGNORECASE), category, family)
    for pattern, category, family in _SERIES
)

# EIA imperial case codes as they appear inside a part number.
_IMPERIAL = ("0075", "0100", "0201", "0402", "0603", "0805", "1206", "1210",
             "1218", "1806", "1812", "2010", "2220", "2225", "2512")
_IMPERIAL_RE = re.compile(r"(?<!\d)(" + "|".join(_IMPERIAL) + r")(?!\d)")
_PREFIX_SIZE_RE = re.compile(r"^([A-Z]{2,4})(\d{4})", re.IGNORECASE)

# TDK and KEMET write the metric case code straight after a leading C.
_METRIC_TO_IMPERIAL = {
    "0603": "0201", "1005": "0402", "1608": "0603", "2012": "0805",
    "3216": "1206", "3225": "1210", "4532": "1812", "5750": "2220",
}
_TDK_METRIC_RE = re.compile(r"^C(\d{4})[A-Z]", re.IGNORECASE)

# Murata, Samsung and TDK write a two-digit case code, or the four-digit
# metric size, straight after the series letters.
_CHIP_SIZE_CODES = {"03": "0201", "05": "0402", "15": "0402", "10": "0603",
                    "18": "0603", "21": "0805", "31": "1206", "32": "1210",
                    "43": "1812", "55": "2220"}
_SIZE_PREFIX_RE = re.compile(
    r"^(GRM|GCM|GJM|GRT|GQM|GA[23]|KRM|NFM|GMD|BLM|MMZ|MPZ|LQ[WGHMP]|CL)(\d{2,4})",
    re.IGNORECASE,
)

# Panasonic encodes the case in the ERJ family number.
_ERJ_SIZES = {"1": "0201", "2": "0402", "3": "0603", "6": "0805",
              "8": "1206", "12": "1210", "14": "1210", "P08": "1206"}
_ERJ_RE = re.compile(r"^ERJ-?(P08|P?\d{1,2})", re.IGNORECASE)

_DIELECTRIC_RE = re.compile(r"(C0G|NP0|X7R|X5R|X7S|X6S|X7T|Y5V|Z5U|(?<=\d)CG)", re.IGNORECASE)

# Value codes. 8R2 is 8.2, 10K0 is 10 000, and a bare 104 is 10 with four
# zeros after it. A tolerance letter or the end of the part number is what
# marks the end of a bare digit code.
_TOLERANCE = "BCDFGJKMZ"
_DECIMAL_CODE_RE = re.compile(r"(?<![0-9])(\d{1,3})R(\d{1,3})(?![0-9])", re.IGNORECASE)
_DIGIT_CODE_RE = re.compile(rf"(?<![0-9])(\d{{3}})(?=[{_TOLERANCE}]|$)", re.IGNORECASE)
_RKM_RE = re.compile(r"(?<![0-9])(\d{1,3})([RKM])(\d{0,3})(?![0-9])", re.IGNORECASE)
# The packaging suffix some makers append can carry digits of its own
# (Uniroyal ends 0603WAF4701T5E with T5E), so it is letters-then-anything.
_EIA_TAIL_RE = re.compile(r"(?<![0-9])(\d{3,4})(?:[A-Z][A-Z0-9]{0,3})?$", re.IGNORECASE)
_INDUCTOR_RE = re.compile(r"(?<![0-9])(\d{1,3})([NR])(\d{0,2})(?![0-9])", re.IGNORECASE)

_RKM_MULTIPLIER = {"R": 1.0, "K": 1e3, "M": 1e6}
_INDUCTOR_SCALE = {"N": 1e-9, "R": 1e-6}


@dataclass
class Classification:
    """What we could work out about a part."""

    category: str = UNKNOWN
    package: str = ""
    value_text: str = ""
    value_num: Optional[float] = None  # base units: farads, ohms or henries
    detail: str = ""  # dielectric, and anything else worth showing

    def as_dict(self) -> Dict[str, object]:
        return {
            "category": self.category,
            "package": self.package,
            "value_text": self.value_text,
            "value_num": self.value_num,
            "detail": self.detail,
        }


def classify(mfr_part: str = "", supplier_part: str = "", supplier: str = "") -> Classification:
    """Categorise a part from its part numbers. Never raises."""
    mpn = (mfr_part or "").strip()
    # A DigiKey number is usually the manufacturer number with a vendor prefix
    # and a packaging suffix bolted on, so it stands in when there is no MPN.
    # An LCSC code is a catalogue number -- C1554 says nothing about the part
    # -- so there is nothing to fall back on.
    fallback = "" if _is_catalogue_code(supplier_part, supplier) else strip_dk_decoration(supplier_part or "")
    subject = mpn or fallback
    if not subject:
        return Classification()

    category, family = _match_series(subject)
    if not category and fallback:
        category, family = _match_series(fallback)
    result = Classification(category=category or UNKNOWN)

    package, body = _split_package(subject)
    result.package = package

    dielectric = _DIELECTRIC_RE.search(subject)
    if dielectric:
        code = dielectric.group(1).upper()
        result.detail = "C0G/NP0" if code in ("C0G", "NP0", "CG") else code

    value = _value(result.category, family, body)
    if value is not None:
        result.value_num, result.value_text = value
    return result


def _is_catalogue_code(supplier_part: str, supplier: str) -> bool:
    """LCSC part codes are the letter C and a number, and nothing else."""
    return supplier.upper() == "LCSC" or bool(re.fullmatch(r"C\d{1,7}", (supplier_part or "").strip()))


def strip_dk_decoration(dk_part: str) -> str:
    """Turn '13-CC0402BPNPO9BN8R2CT-ND' into 'CC0402BPNPO9BN8R2'."""
    part = re.sub(r"^\d{1,4}-", "", dk_part.strip())
    part = re.sub(r"(CT|DKR|TR|TRDKR|OS|1|2|6)?-ND$", "", part, flags=re.IGNORECASE)
    return part.strip()


def _match_series(part: str) -> Tuple[str, str]:
    for pattern, category, family in _SERIES_COMPILED:
        if pattern.search(part):
            return category, family
    return "", ""


def _split_package(part: str) -> Tuple[str, str]:
    """
    Return the case code and the part number with that code blanked out, so a
    value reader cannot mistake the package digits for a value.
    """
    metric = _TDK_METRIC_RE.match(part)
    if metric and metric.group(1) in _METRIC_TO_IMPERIAL:
        return _METRIC_TO_IMPERIAL[metric.group(1)], _blank(part, metric.start(1), metric.end(1))

    sized = _SIZE_PREFIX_RE.match(part)
    if sized:
        digits = sized.group(2)
        package = _METRIC_TO_IMPERIAL.get(digits, "") if len(digits) == 4 else ""
        package = package or _CHIP_SIZE_CODES.get(digits[:2], "")
        return package, _blank(part, sized.start(2), sized.end(2))

    erj = _ERJ_RE.match(part)
    if erj:
        size = erj.group(1).upper().lstrip("0")
        return _ERJ_SIZES.get(size, ""), _blank(part, erj.start(1), erj.end(1))

    prefix = _PREFIX_SIZE_RE.match(part)
    if prefix and prefix.group(2) in _IMPERIAL:
        return prefix.group(2), _blank(part, prefix.start(2), prefix.end(2))

    imperial = _IMPERIAL_RE.search(part)
    if imperial:
        return imperial.group(1), _blank(part, imperial.start(1), imperial.end(1))

    return "", part


def _blank(part: str, start: int, end: int) -> str:
    return part[:start] + "|" + part[end:]


def _value(category: str, family: str, body: str) -> Optional[Tuple[float, str]]:
    if category == "Capacitor":
        return _capacitance(body, micro=(family == "electrolytic"))
    if category == "Resistor":
        return _resistance(body, family)
    if category == "Inductor":
        return _inductance(body)
    return None


def _capacitance(body: str, micro: bool = False) -> Optional[Tuple[float, str]]:
    """
    Read the last value code in the part number. 8R2 is 8.2 pF; 104 is 100 nF,
    or 100 uF on an aluminium electrolytic, where the codes are microfarads.
    """
    unit = 1e-6 if micro else 1e-12
    best: Optional[Tuple[int, float]] = None
    for match in _DECIMAL_CODE_RE.finditer(body):
        best = (match.start(), float(f"{match.group(1)}.{match.group(2)}"))
    for match in _DIGIT_CODE_RE.finditer(body):
        if best is None or match.start() > best[0]:
            best = (match.start(), _eia(match.group(1)))
    if best is None:
        return None
    farads = best[1] * unit
    return farads, format_capacitance(farads)


def _resistance(body: str, family: str) -> Optional[Tuple[float, str]]:
    """
    Series that use R/K/M notation (10K0) are read that way; the rest carry a
    three or four digit EIA code at the end (103, or 1002 at one percent).
    """
    if family != "eia_tail":
        if family == "yageo":
            # Yageo puts a two-digit packaging code between the dash and the value.
            body = re.sub(r"-\d{2}(?=\d)", "-", body)
        match = _RKM_RE.search(body)
        if match:
            whole, letter, frac = match.group(1), match.group(2).upper(), match.group(3)
            ohms = float(f"{whole}.{frac}" if frac else whole) * _RKM_MULTIPLIER[letter]
            return ohms, format_resistance(ohms)
    tail = _EIA_TAIL_RE.search(body)
    if tail:
        ohms = _eia(tail.group(1))
        return ohms, format_resistance(ohms)
    return None


def _inductance(body: str) -> Optional[Tuple[float, str]]:
    """10N is 10 nH and 4R7 is 4.7 uH. Bare digit codes are left alone: on an
    inductor they collide with the tolerance letter that follows them."""
    match = _INDUCTOR_RE.search(body)
    if not match:
        return None
    whole, letter, frac = match.group(1), match.group(2).upper(), match.group(3)
    henries = float(f"{whole}.{frac}" if frac else whole) * _INDUCTOR_SCALE[letter]
    return henries, format_inductance(henries)


def _eia(code: str) -> float:
    """103 is 10 with three zeros; 1002 is 100 with two."""
    significant, exponent = code[:-1], int(code[-1])
    return float(significant) * (10 ** exponent)


def _engineering(value: float, steps: List[Tuple[float, str]], unit: str) -> str:
    for scale, prefix in steps:
        if value >= scale * 0.999:
            text = f"{value / scale:.3f}".rstrip("0").rstrip(".")
            return f"{text} {prefix}{unit}"
    text = f"{value:.4g}"
    return f"{text} {unit}"


def format_capacitance(farads: float) -> str:
    return _engineering(farads, [(1.0, ""), (1e-3, "m"), (1e-6, "µ"), (1e-9, "n"), (1e-12, "p")], "F")


def format_resistance(ohms: float) -> str:
    return _engineering(ohms, [(1e6, "M"), (1e3, "k"), (1.0, "")], "Ω")


def format_inductance(henries: float) -> str:
    return _engineering(henries, [(1.0, ""), (1e-3, "m"), (1e-6, "µ"), (1e-9, "n")], "H")
