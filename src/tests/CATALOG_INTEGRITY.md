# Stellar catalog integrity checks

Run the reproducible regression suite from the repository root:

```sh
make -j2 catalog-tests
```

This runs independent Python corruption tests and native C++ reader/importer/writer
tests. Fixtures live in the test code, so this command works without downloaded
catalogs or a graphics context. GitHub Actions runs it on every push and pull
request. The C++ executable uses the project's existing SDL, OpenGL, image,
archive, curl, and GoogleTest libraries; Python uses only its standard library.

Audit a generated catalog separately:

```sh
make catalog-audit
python3 -B src/tests/catalog_integrity.py catalogs/soles_alienorum.dat \
    --sources catalogs --report /tmp/soles_integrity_report.json
```

The live audit is read-only. It reports all failures, prints three examples per
category, and optionally writes every diagnostic to JSON with line numbers,
related rows, source paths, and the catalog's SHA-256. Exit status is 0 for no
hard errors, 1 for integrity failures, and 2 for an unreadable or corrupt input.
`--strict-warnings` also makes review warnings fail the command.

## Coverage

| Area | Checks |
| --- | --- |
| File layout | Exact 455-byte rows, UTF-8, control characters, reserved columns, separator overflow, complete numeric tokens, integer fields, finite values, plain/gzip parity |
| Catalog identity | Exactly one solar row, Sun first, Alienorum ID syntax and constellation, duplicate IDs and repeated rows, HD/HIP cache bounds, HR and SB9 preserved separately |
| Names | Greek letter and numbered Bayer variants against designation metadata, fabricated Alpha names, Flamsteed/Gould numbers and constellations, explicit HD/HIP/GJ names, component suffixes, custom names, inherited identities through nested companions |
| Cross-catalog identity | Unrelated duplicate HD, HIP, HR, and Gliese identities; shared Bayer/Gould/DM/name diagnostics; BSC and Hipparcos identifier conflicts; BSC Bayer and Flamsteed designations |
| Astrometry | RA/declination grammar and ranges, pole boundaries, negative zero column stability, source-coordinate discrepancies, exact and nearby positional duplicates across RA wrap and poles, Hipparcos ICRS epoch propagation and cosine-declination proper motion |
| Photometry | Apparent/absolute magnitude and color ranges, source V and B-V discrepancies, distance modulus and parallax consistency, variability bounds and periods |
| Stellar properties | Spectral families including white dwarfs, Wolf-Rayet, carbon stars, subdwarfs and composite classes; spectral/color and source-class diagnostics; mass, radius, temperature, velocity and rotation sanity |
| Multiple systems | Resolved parents, cycles, nested components, distinct component letters, independent companion identities, orbital ranges, complete negative orbital epochs, Kepler period consistency |
| Source coverage | Missing BSC/Hipparcos entries brighter than magnitude 10, checked by both their primary ID and HD identity |

Native fixtures use literal column offsets as an oracle, rather than trusting a
writer/reader round trip alone. Python parses the schema independently of the
application. Corruption tests verify that each failure is actually detected,
alongside valid counterexamples such as numbered Bayer names, zero coordinates,
faint unnamed IDs, and legitimate shared system identifiers.

## Catalog conventions and review policy

Blank Alienorum IDs are valid for faint standalone stars: catalog completeness is
only claimed for apparent magnitude below 10. An orbiting component still requires
a stable ID so its family can be resolved. No bright-source coverage expectation
is imposed at magnitude 10 or fainter. Missing optional measurements stay unknown,
rather than being interpreted as zero by the Python auditor.

HD, HIP, and HR sometimes describe an unresolved multiple system. Sharing one
within a linked family is a review warning; sharing one across unrelated families
is an error. SB9 identifies systems and is permitted to repeat. DM, Gould, Bayer,
names, and close coordinates alone are insufficient proof of a duplicate, so
those conflicts are presented for review. An unlinked component can represent an
optical or unresolved pair and is also a warning.

Extinction, variability, unresolved photometry, differing measurement epochs,
uncertain parallax, and classification revisions can explain physical or source
discrepancies. Such diagnostics are warnings, with generous tolerances. They do
not automatically delete stars or rewrite scientific measurements. An explicitly
wrong identifier, broken layout, invalid number, duplicate Alienorum ID, or orbit
cycle is a hard error. Custom naming does not excuse a contradictory explicit
designation such as an HD number or Greek constellation name.

## Regeneration and limits

Existing generated catalogs can retain defects after an importer or writer is
fixed. Regenerate through the application's normal catalog-building path, then
run `make catalog-audit` before distributing the resulting file. The suite has
no baseline that silently accepts today's failures.

Downloaded sources and the generated `soles_alienorum.dat` are excluded from Git.
CI therefore validates deterministic fixtures; the separate live audit validates
the actual generated catalog and its locally available BSC/Hipparcos sources.
Both layers serve different purposes, and passing fixture tests does not certify
an existing generated file as clean.

SAO, Tycho, Gaia, CCDM and other identities absent from this condensed file cannot
be checked from its rows. The current source comparison covers BSC and Hipparcos,
not every upstream catalog. No runtime assets or installer dependencies are
introduced by these tests.
