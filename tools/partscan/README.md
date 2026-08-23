# partscan

A small web application that reads the 2D barcode DigiKey prints on its packing
labels, stores what it says, and keeps a parts list you can sort. Python and
SQLite, in a container.

Scan a label into the box, and the reel is on the list — categorised, with its
package and its value worked out from the part number:

    [)>06PC50,C511PCC0402BPNPO9BN8R230P13-CC0402BPNPO9BN8R2CT-NDK1K10027327210K...

    Capacitor · 0402 · 8.2 pF · CC0402BPNPO9BN8R2 · 13-CC0402BPNPO9BN8R2CT-ND · 10

## Running it

```sh
docker compose up --build       # http://localhost:8000
```

The database is a SQLite file on the `partscan-data` volume, so the parts list
survives the container. Set `PARTSCAN_SECRET` to anything stable if you would
rather flash messages kept working across restarts.

Without Docker:

```sh
pip install -r requirements.txt
PARTSCAN_DB=partscan.sqlite3 flask --app partscan.app run
```

The tests need no hardware, no container and no network:

```sh
pip install -r requirements-dev.txt && python -m pytest
```

## Scanning

The scan box is the first thing focused on the page. A keyboard-wedge scanner
types the whole label and presses Enter, which submits the form, so scanning a
box of reels is scan, scan, scan with nothing to click. Pasting the text works
the same way.

Scanning the same physical label twice is caught and refused — that is nearly
always a double trigger rather than two reels. Tick **record duplicates** when
it really is two.

A label is a receipt: this reel, this lot, this many parts. Scans are kept
individually, and two labels for the same part number join the same component,
whose quantity is the sum. The component page lists the scans behind it, each
with its date code, lot and order numbers, and the raw label it came from.

## Sorting

The list is sorted by component by default: category, then package, then value,
so every 0402 capacitor sits together in capacitance order. Every column header
sorts, and clicking the sorted one reverses it:

| Sort | What it gives you |
|---|---|
| Category | Everything of one kind together, then package, then value |
| Package | All the 0402s, whatever they are |
| Value | Numeric — 8.2 pF, 100 nF, 10 µF, in that order, not alphabetical |
| Manufacturer part, DigiKey part | Alphabetical, for looking one up |
| Qty | What you have most and least of |
| Scans, Last scan | What has been coming in |

Components with no value or no package sort to the end either way round, rather
than interleaving with the ones that have them. The category chips filter, the
search box matches any of the part numbers, and both survive a re-sort.

## What is in a DigiKey label

The barcode is an ISO/IEC 15434 "format 06" message carrying ANSI MH10.8.2 data
identifiers:

| | | |
|---|---|---|
| `P` | Customer part number | what you called it when you ordered |
| `1P` | Manufacturer part number | `CC0402BPNPO9BN8R2` |
| `30P` | DigiKey part number | `13-CC0402BPNPO9BN8R2CT-ND` |
| `K` | Purchase order | usually blank |
| `1K` `10K` | Sales order, invoice | |
| `9D` | Date code | `2336` — 2023, week 36 |
| `1T` | Lot code | |
| `11K` | Packing list | |
| `4L` | Country of origin | `TW` |
| `Q` | Quantity | |
| `11Z` `12Z` `13Z` `20Z` | DigiKey housekeeping | pick flag, part ID, load ID, padding |

Between the fields sit unprintable separators (`GS`, 0x1d). Most scanners in
keyboard-wedge mode drop them, and so does copy-and-paste, which leaves the
fields run together with nothing but the identifiers to divide them:

    ...30P13-CC0402BPNPO9BN8R2CT-NDK1K10027327210K128845720...

Both forms are read. When the separators survive, the split is exact and the
scan is stored as-is. When they do not, the fields are reconstructed, and the
scan is marked **reconstructed** on the component page.

Reconstruction is not guesswork about where the boundaries probably are. Part
numbers contain the identifier strings constantly — `RC0402FR-0710KL` has a
`10K` in it, which is the invoice identifier, and `311-10.0KLRCT-ND` has a `K`
in it, which is the purchase order — so the naive reading cuts part numbers in
half. Two things prevent that. Fields whose content is fixed reject the splits
that would put a part number in them: the quantity, the order numbers and the
date code hold digits, the country holds two letters. What survives is scored
on how much each field looks like what it claims to be — a DigiKey part number
ends in `-ND`, a date code is four digits with a plausible week, the purchase
order is usually blank — and the best-scoring reading wins, found exactly by a
dynamic program rather than by trying splits until one fits.

What it cannot do: two adjacent numeric fields with no separator between them
divide only one way that keeps the sequence valid, and that is the way they are
divided. If the sales order and invoice on a reconstructed scan look a digit
out, that is why, and the raw label is kept on the component page so you can
see for yourself. A part number consisting entirely of data identifiers would
also defeat it, though no such part number exists.

## What it works out about a part

DigiKey's label carries no description, so the category, package and value are
recovered from the part number by `classify.py`, using a table of series
prefixes and a value reader per family — because `104` means 100 nF on an MLCC
and 100 µF on an aluminium electrolytic, and `1002` means 10 kΩ only on the
series that use four-digit codes.

Covered: MLCCs and electrolytics from Yageo, Murata, Samsung, TDK, KEMET and
AVX; resistors from Yageo, Vishay, Panasonic, Stackpole, KOA, Rohm and Bourns;
Murata and Vishay inductors and ferrite beads; and the category alone for
diodes, LEDs, transistors, ICs, sensors, crystals, connectors, switches and
fuses. Dielectric is reported when the part number spells it out (`X7R`,
`C0G`); Murata's own dielectric codes are not decoded.

Anything not in the table is `Other`, with no package and no value. That is
deliberate — a wrong value is worse than none, and it is what sorts to the end
of the list rather than into the middle of it. The category is a guess, and the
component page has a dropdown to correct it when the guess is wrong.

## Beyond the page

| | |
|---|---|
| `POST /api/scan` | `{"barcode": "[)>06..."}` — decodes, stores, returns the label and what was made of it. 201 for a new scan, 200 with `duplicate: true` for one already recorded, 400 for something unreadable. `{"force": true}` records a duplicate anyway |
| `GET /api/parse` | `?barcode=...` — decode without storing, for checking a label by hand |
| `GET /api/components` | The parts list as JSON. Takes the same `sort`, `dir`, `category` and `q` as the page |
| `GET /export.csv` | The parts list as CSV, filtered and sorted the same way |
| `GET /healthz` | What the container's healthcheck asks |

Enough for a bench script that pushes a USB scanner's output straight at
`/api/scan`, which is the shape this ends up in when the scanner is not sat at
a browser.

## Notes

There is no authentication, and none of this is written for the open internet —
it is a thing to run on the bench, on a machine you already trust. The database
is one SQLite file; copy it to back it up.
