**Catalog audit snapshot, 2026-10-15**

131,541 catalog rows; **0 errors; 787 review warnings remain**.

All nine entries in `known_bugs` are marked fixed and have representative regression coverage. The approved reconciliation changes are implemented. Remaining source disagreements stay visible for individual review.

| Stage | Errors | Review warnings |
| --- | ---: | ---: |
| Original text catalog | 679 | 2,097 |
| First regeneration with all reference tables active | 648 | 14,779 |
| Start of this warning pass | 0 | 5,965 |
| Current regeneration | 0 | 787 |

The independent auditor records 6,501 explicit source verifications. Alias checks require corroborating positions and component scope; spectral checks also examine publication dates, SIMBAD quality, or MK-standard status. The warning reductions include corrected imports and explicit, evidence-backed verifications of retained measurements.

Quality-checked Gaia DR3 remains the preferred distance input. Accepted solutions require positive parallax and uncertainty, relative uncertainty at most 20 percent, RUWE at most 1.4, and exactly one cross-match neighbour. Rejected or absent solutions retain prior distances. Resolved companions retain their own measured astrometry.

Photocenter and spectroscopic projected axes are separate from relative orbital axes. Complete relative solutions stay together. Estimated geometry and periods carry persistent flags; final derived values use total system mass. Decimal-year orbital epochs use days in the Julian-date conversion. Visual family membership survives catalog reload without fabricated physical orbits.

Algol and 99 Herculis source scales are corrected against published solutions. Mizar's wide orbit is marked as estimated. The remaining 12 Kepler warnings involve unresolved source, component, or physical-parameter conflicts; published values were not adjusted merely to clear the check.

Implementation and condensed-format details are documented in [catalog_reconciliation.md](catalog_reconciliation.md).

| Remaining warning category | Count |
| --- | ---: |
| `source_spectral_class` | 200 |
| `hip_magnitude` | 199 |
| `variable_magnitude` | 143 |
| `duplicate_gould` | 46 |
| `spectral_color` | 40 |
| `duplicate_dm` | 38 |
| `duplicate_name` | 31 |
| `hip_bv` | 30 |
| `near_duplicate_position` | 14 |
| `orbit_kepler` | 12 |
| `coincident_stars` | 11 |
| `source_position` | 10 |
| `duplicate_bayer` | 7 |
| `color_outlier` | 4 |
| `gliese_name` | 2 |

The remaining flags cover competing spectra and photometry, possible duplicate or unresolved identities, positions, orbital inconsistencies, and estimated colors. The four color outliers are explicitly estimated values for faint companions. These cases are not all resolved.

**Validation**

- Application and all test binaries built successfully.
- Full C++ suite: **389 of 396 passed**. The seven failure names below also occur in the recorded baseline; no new failure names appeared.
- All **106 catalog regression tests** and **six OpenGL rendering checks** passed.
- All **51 Python audit tests** passed.
- This pass added 23 C++ catalog tests and six Python audit tests, alongside stronger existing checks.
- Compressed and text catalogs match exactly.
- No local stellar name from `exoname.dat` appears globally, including after removing a component suffix.
- All 24 configured stellar reference sources were available and valid; provenance reports complete.
- Existing runtime source data remains covered by the installation rules; no new static runtime asset was introduced.

Full-suite failures:

- `ExoConsTest.BrightStarsJoinedAndNormalSizedConstellationsFormed`
- `ExoConsTest.Generate47UrsaeMajoris_CriteriaVerification`
- `ExoConsTest.GenerateGJ86_CausesAtLeast50Constellations`
- `ExoConsTest.HamalConstellationsFormConnectedShapesWithoutStraySingleLines`
- `ExoConsTest.LineSeparationFromOtherConstellations`
- `ExoConsTest.PasserbyLinesConnectToImpingingStars`
- `ExoConsTest.TauCetiNearLinesAndReassignment`

**Bug-list regression coverage**

| Bug | Representative test |
| --- | --- |
| Magnitude-10 ID boundary | `CatalogIdsStopAtTenExceptForBrightSystemCompanions` |
| Local stellar names | `ExoplanetHostNamesStayLocalDuringIauReconciliation` |
| Cloud opacity | `CumulusInteriorsRemainOpaqueDuringDistanceBlending` |
| Rings behind the camera | `RingsQueueWhenThePlanetDiscIsBehindTheCamera` |
| Rock opacity | `RockFacesStayOpaqueAtEveryVisibleDistance` |
| Vegetation borders | `WalkingAcrossVegetationBordersKeepsAMixedSceneryPopulation` |
| Cloud silhouette clipping | `DistantCloudLobesExtendBeyondTheShellSearchCell` |
| Publication folders and downloads | `SourceDownloadsFollowManifestPublicationFolders` |
| Secondary-star shadows | `SceneryShadowsFollowTheHostInsteadOfTheSystemPrimary` |


**Wikipedia cross-checks**

- [Algol](https://en.wikipedia.org/wiki/Algol): relative angular axes, with [Baron et al. (2012)](https://arxiv.org/abs/1205.0754).
- [99 Herculis](https://en.wikipedia.org/wiki/99_Herculis): 56.3-year period and 1.06-arcsecond relative axis, with [Kennedy et al. (2012)](https://arxiv.org/abs/1201.1911).
- [Mizar](https://en.wikipedia.org/wiki/Mizar): the wide pair has no precise measured orbit in the adopted sources.
- [61 Cygni](https://en.wikipedia.org/wiki/61_Cygni): A is HD201091 and B is HD201092; the authored center identifier is corrected.
- [HD 10307 / GJ 67](https://en.wikipedia.org/wiki/HD_10307): retained Hipparcos parallax of 79.09 mas agrees with the cited orbital solution; the older HGCA value no longer overrides it.
- [Gamma Andromedae](https://en.wikipedia.org/wiki/Gamma_Andromedae): K2+IIb primary classification agrees with the adopted source.
- [Gliese 623](https://en.wikipedia.org/wiki/Gliese_623): component membership is preserved.

**Reproduce the audit**

```sh
make catalog-audit CATALOG_AUDIT_FLAGS='--report /tmp/alienorum-warning-audit.json'
```

Add `--strict-warnings` to those flags to make review warnings fail the target. Complete diagnostics from this run are available at `/tmp/alienorum-warning-audit.json`. Full C++ results are in `/tmp/alienorum-stage7-full-tests.log`; Python results are in `/tmp/alienorum-stage7-python-tests.log`. These temporary reports are local artifacts.

Catalog SHA-256: `7e43906ee6142222a2b2a9b3ac3903ebe93c46b20bcc22e9531b3cb06ac3c66d`.
