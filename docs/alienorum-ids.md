# Alienorum IDs

A stellar ID such as `6c And 17 B` describes a star system as seen from Earth. `6` is the whole-number visual magnitude bin, `c` is the color code, `And` is Andromeda, and `17` is the primary's brightness rank in that bin. A bin with one primary omits the rank. A known companion takes the primary's ID with a component letter such as `B`. Large-amplitude, non-eclipsing variables use `V` in place of the magnitude digit.

The stellar color codes from hottest to coolest are `b` for blue, `c` for blue-white, `w` for white, `y` for yellow, `o` for orange, and `r` for red. Published stellar ranks use a visual-magnitude ceiling of 10.

## Purple IDs

The code `p` stands for purple and follows the magnitude in a brown dwarf's ID. Eligible primaries are ranked by visual brightness within their magnitude and constellation bin. Their companions use the primary's ID and a component suffix. The same numbering ceiling of magnitude 10 applies. Both components of Luhman 16 have blank Alienorum IDs because the primary falls in magnitude bin 22, beyond catalog coverage.

## Coverage of the stellar ranks

The visual-magnitude ceiling describes the numbering rule. Survey completeness depends on the source catalogs, the photometric band, and the treatment of unresolved systems. Gaia's broad-band G magnitudes and Johnson V magnitudes have separate provenance.

In the November 2026 comparison, the shipped app catalog contained 107,795 ranked primaries. Of the 107,692 entries in the bundled cross-reference that matched an app primary uniquely by HD, HIP, or Gaia ID, 102,805 carried a different Alienorum ID. An independent check of Hipparcos rows with V below 10 found 58 HIP numbers absent from the app catalog and 61 whose app rows all had blank IDs. Of the 58 absent HIP numbers, 55 had an exact HD counterpart. The three remaining entries for review are HIP 36322, HIP 73722, and HIP 100894. These results call for reconciling source identities, component membership, and photometry before publishing revised stellar ranks.

Run `python3 tools/audit_alienorum_ids.py` to repeat the cross-reference comparison. Add `--crosswalk path.tsv` to list the conflicting designations and source identifiers for review.

If a future catalog revises a rank, legacy designations can be retained using a cross-reference scoped to the older catalog edition. The same text can identify different objects in different editions, so the edition matters when resolving a legacy ID.
