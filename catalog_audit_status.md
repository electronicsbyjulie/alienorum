**Catalog audit snapshot, 2026-10-15**

131550 catalog rows; **0 errors; 5965 review warnings remain**.

All nine entries in `known_bugs` are marked fixed. The warning sweep remains incomplete; the remaining cases are listed below rather than treated as resolved.

| Stage | Errors | Review warnings |
| --- | ---: | ---: |
| Starting text catalog | 679 | 2,097 |
| First regeneration with all reference tables active | 648 | 14,779 |
| Current regeneration | 0 | 5,965 |

Activating previously unreachable reference tables changed adopted measurements and exposed additional disagreements with older sources. The auditor records 6,842 explicit source verifications separately from warnings. Verification checks use catalog identities and positions; spectral checks also examine publication dates, SIMBAD quality, or MK-standard status.

The Gliese import now derives distance from positive measured parallax. All 229 remaining distance/parallax warnings from the prior iteration are gone. Parenthesized spectral classes and published descriptive markers are accepted; unknown spectral text still produces a warning.

| Remaining warning category | Count |
| --- | ---: |
| `source_spectral_class` | 4732 |
| `orbit_kepler` | 425 |
| `hip_magnitude` | 207 |
| `variable_magnitude` | 124 |
| `shared_system_identifier` | 110 |
| `hip_bv` | 64 |
| `orphan_component` | 58 |
| `spectral_color` | 50 |
| `duplicate_gould` | 47 |
| `duplicate_dm` | 45 |
| `duplicate_name` | 39 |
| `duplicate_bayer` | 15 |
| `source_position` | 14 |
| `near_duplicate_position` | 13 |
| `coincident_stars` | 9 |
| `reference_identity_disagreement` | 5 |
| `color_outlier` | 4 |
| `bsc_bayer_added` | 2 |
| `gliese_name` | 1 |
| `hip_cross_id` | 1 |

The remaining flags cover competing spectral classifications, orbital parameters, variability photometry, shared identifiers, and unresolved component membership. They have not all been resolved.

**Validation**

- `make -j2 bin/alienorum` completed.
- Full C++ suite: 366 of 373 tests passed. The 7 failures below also fail with the starting catalog.
- All 83 catalog regression tests and six hidden OpenGL rendering checks passed.
- All 45 Python audit tests passed.
- Twenty new regression tests were added, alongside stronger existing scenery and import checks.
- The compressed catalog exactly matches the text catalog.
- No stellar name listed in `exoname.dat` appears as an exact global-name match in the generated catalog.
- All configured stellar reference tables were available and valid.

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

- [Gamma Andromedae](https://en.wikipedia.org/wiki/Gamma_Andromedae) gives the primary as K2+IIb, agreeing with the adopted SIMBAD/Skiff classification.
- [Gliese 623](https://en.wikipedia.org/wiki/Gliese_623) is a binary system; preserving its parent link prevents a shared identifier from being misclassified as an unrelated duplicate.
- [HD 10307 / GJ 67](https://en.wikipedia.org/wiki/HD_10307) remains a source-priority review case: the supplied HGCA input has 68.0244 mas from Gaia DR2, while the cited 2022 orbital solution gives 79.08 +/- 0.63 mas.

**Reproduce the audit**

```sh
make catalog-audit CATALOG_AUDIT_FLAGS='--report /tmp/alienorum-final-audit.json'
```

Add `--strict-warnings` to those flags to make review warnings fail the target. Complete diagnostics from this run are available at `/tmp/alienorum-final-audit.json`.

Catalog SHA-256: `12a28d6885b07a5071c3740683fb515a4893aa7b9e1213e04947c3fc9dc34654`.
