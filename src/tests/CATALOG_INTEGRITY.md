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

Checkpoint on 2026-10-14: 35 Python tests and 49 native tests pass; CMake configures
successfully. Commit `277efff` centralizes endpoints and corrects fixed-column
rounding/variability serialization. The latest complete input audit had 131,533
rows, 24 errors and 1,437 warnings. A new comparison remains pending while the
working catalog is absent during regeneration; partial preview files are not
valid comparison inputs.

Commit `18472af` makes catalog writes transactional: a field overflow or output
failure removes the temporary output and preserves the previous complete file.
The exact GJ 575 B variability value is covered by a fixed-column regression.
Orbit ranking has separate policies for SBX legacy grades and ORB6 grades, plus
regressions against replacing a higher-quality orbit with a lower-quality one.
The preview loads constellation boundaries before assigning IDs and fails clearly
if they are absent. A comparison made without those boundaries is invalid.
