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
hard errors, 1 for integrity failures, and 2 for input or report I/O failures.
`--strict-warnings` also makes review warnings fail the command.

## Coverage

| Area | Checks |
| --- | --- |
| File layout | Exact 455-byte rows, UTF-8, control characters, reserved columns, separator overflow, complete numeric tokens, integer fields, finite values, plain/gzip parity |
| Catalog identity | Exactly one solar row, Sun first, Alienorum ID syntax, color codes and constellation, duplicate IDs and repeated rows, HD/HIP cache bounds, HR and SB9 preserved separately |
| Names | Greek letter and numbered Bayer variants against designation metadata, fabricated Alpha names, Flamsteed/Gould numbers and constellations, explicit HD/HIP/GJ names, component suffixes, custom names, inherited identities through nested companions |
| Cross-catalog identity | Unrelated duplicate HD, HIP, HR, and Gliese identities; Gliese syntax and component-spacing aliases; shared Bayer/Gould/DM/name diagnostics; BSC and Hipparcos identifier conflicts; BSC Bayer and Flamsteed designations |
| Astrometry | RA/declination grammar and ranges, pole boundaries, negative zero column stability, source-coordinate discrepancies, exact and nearby positional duplicates across RA wrap and poles, Hipparcos ICRS epoch propagation and cosine-declination proper motion |
| Photometry | Apparent/absolute magnitude and color ranges, source V and B-V discrepancies, distance modulus and parallax consistency, variability bounds and periods |
| Stellar properties | Spectral families including white dwarfs, Wolf-Rayet, carbon stars, subdwarfs and composite classes; spectral/color and source-class diagnostics; mass, radius, temperature, velocity and rotation sanity |
| Multiple systems | Resolved parents, cycles, nested components, distinct component letters, independent companion identities, orbital ranges, complete negative orbital epochs, Kepler period consistency |
| Source coverage | Missing BSC/Hipparcos entries brighter than magnitude 10, checked by both their primary ID and HD identity; empty sources and duplicate source IDs; plain or gzip source files |

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
Faint standalone stars may also retain nonempty IDs from earlier catalog builds.
The completeness threshold does not invalidate those identities.
Bright standalone stars with blank IDs are review warnings, rather than automatic
evidence of a missing star.

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

## Field-specific source reconciliation

The generator reads reference endpoints exclusively from the `reference` rows in
`catalogs/urls.dat`. These rows include complete TAP queries; code contains no
endpoint fallbacks. The reference client owns these downloads so its concurrency,
timeouts, and parser validation apply consistently. Downloads are cached under
`catalogs/reference`; a normal cached catalog load performs no reference requests.
The existing installer already includes `catalogs/urls.dat`. Reference caches and
provenance reports are downloaded or generated, rather than release assets.

| Source | Adopted fields and constraints |
| --- | --- |
| SIMBAD | Alias-based identity with coordinate checks; names, designations, classifications, UBV fluxes, missing radial velocities; independent resolved components remain distinct |
| Mermilliod | Measured Johnson V, B-V and U-B, including measured zero colors; component and variability checks |
| Skiff | Spectroscopic classifications with bibliography and quality ranking; older classifications cannot replace higher-ranked modern ones |
| Gaia DR3 | Hipparcos crossmatch, positive precise parallax, RUWE and match-ambiguity filters, epoch propagation, optional GSPPhot model temperature with explicit model provenance |
| WDS | Existing component identification and visual companion magnitudes; nonvisual passbands and uncertain observations rejected; optical pairs do not acquire physical orbits |
| SBX | Existing simple binary periods, eccentricities and primary-to-secondary periapsis conversion; projected velocities never become a relative semimajor axis |
| DEBCat | Logarithmic mass, radius and temperature converted into application units; ambiguous component 1/2 assignments retained for review |
| ORB6 | Existing resolved binaries, grades 1–3, source units and epoch conversion; photocentric orbits excluded |
| IAU | Approved proper names resolved through explicit identifiers |
| VSX | V-band variability bounds and periods; no conversion of another passband into Johnson V |

Hipparcos, BSC, GCVS and the other original importers remain the initial sources.
Reference measurements run after the main-sequence heuristic during a fresh
catalog build. Cached catalogs with a generated provenance sidecar retain their
adopted fields across subsequent legacy override loading.

To reconcile a separate preview from an existing complete condensed catalog:

```sh
make -j2 bin/catalog_reconcile
bin/catalog_reconcile /tmp/alienorum_reference_preview --offline
python3 -B src/tests/catalog_integrity.py \
    /tmp/alienorum_reference_preview/catalogs/soles_alienorum.dat \
    --sources catalogs --report /tmp/alienorum_reference_audit.json
```

Omit `--offline` to fetch absent reference caches. `--download-only` fetches sources
without loading stars. The preview refuses the repository's input directory and
writes its catalog and full per-field provenance into its output directory.
It overlays the existing generated catalog; it does not recreate omissions from
all original input catalogs. A fresh application build remains the full
regeneration path. Scientific disagreement warnings can persist after adopting
newer data, since the independent audit still compares BSC and Hipparcos values.

Regression checkpoint on 2026-10-15: 38 Python tests and 71 native tests pass; CMake configures
successfully. Commit `277efff` centralizes endpoints and corrects fixed-column
rounding/variability serialization. The latest complete input audit had 131,533
rows, 24 errors and 1,437 warnings. That initial comparison predated the source
integration and importer repairs described below. Partial preview files are not
valid comparison inputs.

Commit `18472af` makes catalog writes transactional: a field overflow or output
failure removes the temporary output and preserves the previous complete file.
The exact GJ 575 B variability value is covered by a fixed-column regression.
Orbit ranking has separate policies for SBX legacy grades and ORB6 grades, plus
regressions against replacing a higher-quality orbit with a lower-quality one.
The preview loads constellation boundaries before assigning IDs and fails clearly
if they are absent. A comparison made without those boundaries is invalid.

Reference Bayer imports use the same numbered-Greek encoding as the original
importers and condensed reader. A regression verifies that a SIMBAD numbered
variant survives through native name generation, rather than collapsing into its
unnumbered Greek letter. The isolated preview omits constellation line-graph
resolution, which has no exported catalog fields.

The first actual refreshed output audited at 213 errors and 17,810 warnings
(SHA-256 `74b166b1a6fa8ad4878f16e49f2fe3f8e2b474df1f7eb2c89eab12e589b2f5be`).
That output is a failed comparison, not an approved catalog. The regression pass
now preserves current constellation designations against historical aliases,
keeps component-qualified HIP aliases distinct from bare system IDs, rejects new
Gliese collisions, distinguishes HR identities when HD components were swapped,
and retains joint photometry separately when a source cannot resolve components.
A display-only primary may recover its existing family's system identifier
without copying the family's unresolved photometry onto that component.
The independent BSC/Hipparcos audit remains unchanged during these repairs.

A condensed preview must restore component membership before source matching and
ID assignment. Membership is reconstructed from explicit IDs without changing
nested orbit centers or creating physical orbits for optical members. Previews
without that index can assign independent IDs to children and are invalid.
Numbered two-letter Greek abbreviations retain their native three-column prefix;
SIMBAD's dotted mu/nu/pi spellings are accepted. Conflicting HR/HD identities
require an independent matching designation before a repair can proceed.

On 2026-10-15, import cache checks also verify that a cached object's current IDs
agree with the requested IDs. This addresses bright Hipparcos entries overwritten
through stale Gliese/Hipparcos lookup aliases. An absent Hipparcos HD field keeps
a known fallback ID. After SIMBAD matching, a stale identifier is removed only
when another separately resolved unrelated object uniquely claims it; family
system identifiers remain intact, and an obsolete numeric display label is
replaced by a surviving valid identifier.

The auditor now treats an omitted BSC Bayer field as a review warning when a
catalog supplies a designation; contradictory supplied values remain errors.
A HIP cross-reference to another known member of the same family is a review
warning. A cross-reference outside that family is an error even for a component.
Component root spacing is normalized for family comparisons. Regression tests
cover both valid family sharing and unrelated identifier corruption.

The BSC notes download is configured in `catalogs/urls.dat`. These notes are
cached alongside the BSC table for reviewing joint photometry and ambiguous
historical cross-identifications. They are downloaded data, so the existing
manifest installation rule applies without adding a static release asset.

The underlying CNS5 corruption was a 300-byte reader splitting 761-byte records.
It now reads complete lines, rejects truncated records, keeps missing measurements
unknown, converts milliarcseconds/year correctly, and propagates ICRS coordinates
from the source epoch to J2000 without applying equinox precession. Tangent RA
motion is converted to the native angular derivative with the declination cosine.
A full-width regression embeds a false record-like payload beyond byte 600 and
verifies that it cannot assign another star's HIP ID. It also checks epoch motion,
parallax distance and a radial velocity extending past the old buffer limit.
These repairs require fresh generation; an existing condensed cache retains the
previously imported values.

Durchmusterung zones retain their explicit sign at zero declination. `BD-00` and
`BD+00` now have separate lookup keys, survive native serialization, and cannot
merge through the Hipparcos fallback matcher. The same handling applies to CD
and CP zones. This addresses two bright missing entries independently of the
CNS5 record-width issue. An unresolved family may own a shared system identifier
for contradiction checks without supplying individual component photometry.

Precession now converts Julian years to centuries in every IAU 1976 coefficient,
corrects the z-angle quadratic coefficient, and maps the equatorial rotation into
the application's (-Y, Z, X) axes. Independent ERFA Prec76 epoch-angle and Pmat76
matrix vectors verify all three axes. This corrects older B1950 input positions;
it does not implement a full FK4-to-FK5 transformation or remove FK4 E-terms.

An unnumbered SIMBAD alias cannot erase an existing numbered Bayer designation.
This preserves cases such as Mu 1 Chamaeleontis and Alpha 1 Crucis when a source
lists only the aggregate designation. Identifier repairs also rename inherited
component labels within the same family, while preserving independent component
identifiers and custom labels. Dedicated regressions cover both cases.

## Full-generation comparison method

The final comparison uses an isolated headless invocation of the raw stellar
import sequence, with exoplanets and satellites omitted. It runs the same Gliese,
BSC, Hipparcos, GCVS, Uranometria, CCDM, SB9, manual-override, orbit-center, legacy
Gaia and stellar-heuristic stages as the fresh application path. Baseline and
reconciled outputs both receive final photometric consistency and native ID
assignment before serialization. All output files are under `/tmp`; the working
`catalogs/soles_alienorum.dat` remains unchanged.

The condensed overlay command is useful for inspecting source matches, but the
runtime reader supplies estimated masses, radii and rotation values for blank
fields. Those defaults can create additional Kepler diagnostics on export. Use a
fresh raw-source generation for scientific before/after comparisons, rather than
interpreting those overlay diagnostics as changes from adopted orbital data.

All three previously missing bright Hipparcos entries (36322, 73722 and 100894)
survive the repaired import chain with their own identities. The positive-zero
DM entries do not inherit HD identities from negative-zero-zone stars.

## Measured results on 2026-10-15

The final source-enabled output contains 131,550 rows and has SHA-256
`29514132bd6d4a2e685051ab29c01819bb077e6bec28c31f3c807850be26b858`.
The comparable fresh legacy baseline, using the repaired importers but without
the new reference pass, has SHA-256
`f1572fcc9471befef87d14202f131f0bfe3b044f1ae5b914e32506321de2abd9`.
Both files were checked with the same independent auditor and BSC/Hipparcos inputs.

| Check | Fresh legacy baseline | With reference sources |
| --- | ---: | ---: |
| Rows | 131,550 | 131,550 |
| Hard errors | 64 | 11 |
| Review warnings | 1,350 | 12,670 |
| Unrelated duplicate HD | 60 | 5 |
| Unrelated duplicate HIP | 1 | 1 |
| Contradictory HD display names | 1 | 0 |
| Contradictory numbered display names | 1 | 0 |
| Hipparcos spectral-family differences | 4 | 8,731 |
| Hipparcos magnitude differences | 325 | 1,612 |
| Hipparcos B-V differences | 1 | 1,177 |
| Source-position differences | 27 | 14 |
| Distance/parallax differences | 232 | 232 |
| Distance-modulus discrepancies | 0 | 0 |
| Orbital Kepler warnings | 424 | 425 |

All 23 reference tables parse successfully and match 118,764 objects. The source
pass removes 65 stale HD assignments using separately resolved object identities.
Those removals do not delete the underlying stars. The earlier failed refreshed
cache (213 errors, 17,810 warnings) remains separate from this comparison.
The working generated catalog retains its original SHA-256
`74b166b1a6fa8ad4878f16e49f2fe3f8e2b474df1f7eb2c89eab12e589b2f5be`.

This is not a clean audit, and the warning-reduction target is not met. Of the
12,670 warnings, 11,520 compare adopted spectral families, V magnitudes or B-V
colors with older Hipparcos values. Those disagreements remain visible; adopting
a newer source does not prove that every discrepancy is correct. The cached Gaia
table supplies astrometry but lacks the optional GSPPhot temperature columns, so
this run makes no claim of adopting those model temperatures. Twelve rows differ
by more than 5 percent in distance versus inverse parallax; the other 220 such
warnings are smaller discrepancies above the auditor's 0.2 percent tolerance.

The remaining hard errors are retained for further source reconciliation:

| Category | Count | Cases |
| --- | ---: | --- |
| Unrelated duplicate HD | 5 | 61907, 174152, 206151, 154907, 185344; component records still lack a shared native family identity |
| Duplicate HIP | 1 | 90265 appears on both GJ 4057 / HD 336196 and an HD 336187 row |
| BSC cross-identification | 2 | Gamma Arietis: HR 545/546 and HD 11502/11503 assignments differ between sources |
| Hipparcos cross-identification | 2 | Gamma Arietis and HIP 90265 |
| BSC Flamsteed designation | 1 | HD 166464: SIMBAD supplies 1 Sgr while BSC supplies 11 Sgr |

The HIP 90265 case also contains copied coordinates on the extra HD 336187 row.
A repair must retain the two real objects and restore their independent
astrometry. Shared or component-qualified identifiers are not sufficient grounds
to delete a star, fabricate a physical orbit, or silence an audit failure.
The full read-only diagnostics are `/tmp/soles_reference_final.json`; final source
provenance is `/tmp/alienorum_reference_final/reference_report.json`.

Validation at commit `967fdc4`: 38 Python tests and 71 native tests pass, including
transactional writes, fixed-width formatting, identity guards, long CNS5 rows,
negative-zero DM zones, numbered aliases, inherited component labels and the
independent ERFA precession vectors. Rebuild and freshly regenerate through the
application to replace the working catalog; loading its old condensed cache does
not apply these importer repairs.

## Zero-error repair checkpoint

The source pass now collects explicit resolved WDS A/B aliases into visual
families before removing stale identifiers. Shared HD numbers remain attached to
both members, independent HIP numbers and distances survive, and no physical
orbit is created. Cached component IDs restore the same membership on reload.
A supplemental SIMBAD Hipparcos query in `catalogs/urls.dat` covers missing V
photometry and faint members excluded from the magnitude-limited pages.

A displaced HD record can recover its independent source position and astrometry
only when a separately resolved nearby catalog object owns its duplicated HIP,
the HD source identity is unique, and the replacement parallax passes an error
filter. Both objects survive. Native regressions cover this repair and visual
family persistence; 74 native tests pass at this checkpoint.

The independent auditor now consults downloaded resolved SIMBAD records for
legacy cross-identification conflicts. It requires one unique source object,
matching HD plus the relevant HR/HIP/designation, and agreement within two
arcseconds. A corroborated disagreement remains a visible review warning;
missing evidence, contradictory designations, displaced coordinates and ambiguous
source objects retain hard errors. Internal duplicate-ID checks are unchanged.
Forty Python regressions and 74 native regressions pass at this checkpoint.

The importer also creates single-member A groups for some independently imported
components. Source-proven A/B links now consolidate those single-member groups,
while rejecting groups with other members or existing physical orbit centers.
The shared-HD regression exercises this legacy topology. The displaced-identity
regression also verifies that an imprecise replacement parallax blocks movement.

## Zero-error fresh generation on 2026-10-15

The fresh raw-source generation at code commit `6d52b87` writes 131,550 rows.
Its SHA-256 is
`fdce8091a489cde3ccbe9b37459c7d6aaedd7d9885ce19aa8772bc9118f630a7`.
The independent audit, including BSC/Hipparcos and downloaded resolved SIMBAD
comparisons, exits successfully with **0 hard errors and 14,336 review warnings**.
The full result is `/tmp/soles_zero_verified.json`; the isolated catalog is
`/tmp/alienorum_zero_verified/catalogs/soles_alienorum.dat` and source provenance
is `/tmp/alienorum_zero_verified/reference_report.json`.

| Shared HD | Primary HIP | Secondary HIP |
| --- | ---: | ---: |
| 61907 | 37884 | 37882 |
| 174152 | 92487 | 92490 |
| 206151 | 107092 | 107094 |
| 154907 | 83852 | 83851 |
| 185344 | 96667 | 96666 |
| 219175 | 114702 | 114703 |

Both members retain their shared HD identity and occupy A/B positions in the
same native family. No physical orbit is created by these associations. All
131,550 rows survive; the changes do not delete stars to remove duplicate checks.
HD 336187 retains its own record at 18:25:31.2 +24:48:55 with V=9.57 and source
astrometry, while HIP 90265 remains on GJ 4057 / HD 336196 at its original position.

Five legacy identity disagreements remain explicit warnings: three checks for
Gamma Arietis, one for the HD 166464 Flamsteed designation, and one for HIP 90265.
These pass only with a unique independent source identity and positional
corroboration. The corruption regressions confirm that wrong IDs, wrong positions,
contradictory designations and ambiguous source identities still fail.

Warnings have not been eliminated. The largest categories are 8,768 spectral-family
comparisons, 1,808 Hipparcos magnitude comparisons, 1,323 component-without-physical-
parent reviews, and 1,279 Hipparcos color comparisons. Many newly identified visual
components intentionally have no physical orbit; their native family membership
persists through serialization and reload. Distance/parallax and Kepler reviews
remain at 232 and 425 respectively. These review categories are not hard failures.

All 40 Python regressions and 74 native regressions pass. The supplemental
Hipparcos source contains 4,299 rows, and all 24 configured source tables parse
successfully. The production generated `.dat` and `.gz` were not edited by this
repair. Rebuild and freshly regenerate through the application to apply the
changes to those files, then run `make catalog-audit` on that exact output.
