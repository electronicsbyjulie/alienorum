# Catalog audit snapshot: 2026-10-14

This records the generated catalog inspected during the integrity-test audit.
It is an investigation snapshot, not an accepted-failure baseline. A fresh
generated catalog must satisfy the auditor's rules independently.

- Rows: 131,542.
- SHA-256: `b7173adce23f5c424c9090547277c1a65d68cefc82e9903750aacce9b51ccba1`.
- Hard-error diagnostics: 236 across 190 rows.
- Review warnings: 127,176.
- Source comparisons: locally downloaded BSC and Hipparcos main catalogs.

| Hard-error category | Diagnostics |
| --- | ---: |
| Malformed numeric fields, including bare `+` and `+inf` | 91 |
| Empty display names | 53 |
| Repeated Alienorum IDs | 40 |
| Data overflowing reserved/separator columns | 37 |
| Identical repeated rows | 6 |
| HD display-name/identity conflicts | 3 |
| Negative physical values: orbital semimajor axes | 2 |
| Malformed Gliese identity: `GJ 1255 -` | 1 |
| Identifier outside its valid range | 1 |
| Incomplete Durchmusterung identity | 1 |
| Numbered display-name/identity conflict | 1 |

Counts are diagnostics, so one row can appear in several categories. Duplicate
Alienorum IDs always fail, even inside a multi-star family. Examples include
`Vo Aql 56`, `Vr Aql 4`, and `Vy Aql`; 38 distinct IDs are duplicated. Inspect variable-star numbering and repeated
emission of multi-star families when repairing these records. The audit also
reports nearby or similarly named objects for review without equating those
signals with confirmed duplicates.

Most warnings (125,728) concern source-coordinate discrepancies. The audit traced
a systematic error to the Hipparcos importer precessing ICRS coordinates using
the observation epoch as an equinox. The importer now propagates proper motion
to the cache epoch without that frame rotation, and tests cover zero coordinates,
known zero motion, and the cosine-declination convention. Existing generated
positions retain the old error until the catalog is regenerated. Other import
paths and source epochs can still warrant separate astrometric review.
The frame and epoch convention is described in the
[ESA Hipparcos catalogue documentation](https://hipparcos-tools.cosmos.esa.int/pstex/sect2_01.pdf).

Other warnings include parallax/distance and distance-modulus discrepancies,
source photometry/classification differences, orbital-period consistency, optical
or unresolved components, and potentially shared identifiers. Three Hipparcos
entries brighter than magnitude 10 are absent by both HIP and HD: HIP 36322,
HIP 73722, and HIP 100894. These are review findings, not automatic deletions or
substitutions of scientific measurements.

## Runtime defects corrected during the audit

- The condensed reader loaded SB9 into HR, overwriting the independent HR ID.
- Negative zero in proper motion or radial velocity could overflow a separator.
- Bayer constellation lookup used the current sky bin and the wrong lookup key.
- The condensed orbital epoch field lost the final character of long negative values.
- Invalid or absent HD/HIP values could address cache slots outside their bounds
  or corrupt the reserved slot zero.
- Hipparcos positions and RA proper motion used the wrong frame/epoch interpretation.

The original blank-Bayer-to-Alpha fix is also covered through the Greek parser,
the Bright Star importer, the condensed reader, and independent name validation.
No fabricated Greek display names were found in this regenerated snapshot after
accounting for genuine inherited companion designations.

The generated catalog was not rewritten by this audit. The remaining data errors
are intentionally visible; fixture tests passing does not certify this file as
clean. See [CATALOG_INTEGRITY.md](CATALOG_INTEGRITY.md) for commands and conventions.
The full local diagnostic report is `/tmp/soles_integrity_report.json` and can be
recreated with the documented audit command.
