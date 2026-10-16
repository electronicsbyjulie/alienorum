# Stellar catalog reconciliation

The generated catalog keeps measured values, derived estimates, and visual family membership distinct. Full field provenance is written to `catalogs/soles_alienorum.provenance.json` during regeneration.

## Optional metadata in the condensed format

Rows remain 455 bytes wide. Previously reserved columns now carry optional metadata:

| Column | Value | Meaning |
| --- | --- | --- |
| 335 | `V` | The parent field identifies visual family membership; it does not define a physical orbit. |
| 336 | `E` | The relative orbital axis is an estimate. |
| 337 | `E` | The orbital period is an estimate. |
| 338 | `E` | B-V is estimated rather than observed catalog photometry. |
| 339 | `E` | U-B is estimated rather than observed catalog photometry. |
| 340 | `A` through `Z` | Component index within the exported family. |

The parent occupies columns 287 through 300. Visual links carry a component index and leave physical orbital elements blank. The reader restores their family membership without constructing an `Orbit`. This also preserves faint visual families whose Alienorum IDs are blank.

Legacy blank metadata retains its historical interpretation. A blank estimate flag in an older catalog does not establish that a measurement was observed. Catalogs containing `V` links require a reader that recognizes that marker.

## Astrometry

Both Gaia import passes use the same DR3 reader. Accepted parallaxes are positive, have positive uncertainties no greater than 20 percent of the parallax, have RUWE at most 1.4, and have exactly one cross-match neighbour. Rejected or absent solutions leave the prior distance intact. The older HGCA table no longer bypasses these checks.

Resolved companions retain their own astrometry. A primary-only measurement can propagate to an unresolved companion without its own distinct Hipparcos identifier. Gaia positions and tangent proper motions are propagated from the supplied reference epoch to J2000. The independent audit likewise propagates Hipparcos positions from J1991.25, including the cosine-of-declination convention in RA.

## Spectral source selection

An unchanged classification with matching Hipparcos identifiers establishes a 1997 publication baseline. Older unrated replacements cannot displace it. SIMBAD quality A, B, or C classifications outrank unrated entries; D and E entries have lower priority. Skiff's `+` marker identifies an MK standard and receives the highest configured priority. Publication order resolves ties within a priority tier.

The independent audit verifies classifications using catalog aliases, positions, and published dates or explicit quality ratings. Shared system identifiers do not establish individual component spectra. Disagreements without adequate corroboration remain review warnings.

## Orbital solutions and estimates

Spectroscopic projected axes and Hipparcos photocenter axes are retained separately from relative orbital axes. They are not interchangeable. A primary-only spectroscopic cross-reference does not establish which resolved companion supplies the orbital signal.

Complete relative solutions retain their period, axis, epoch, and orientation together. Conflicting spectroscopic periods are recorded for review instead of being mixed into those solutions. ORB6 supplies relative axes with explicit unit conversion and quality selection.

Generated periods use total system mass:

`P = 2 pi sqrt(a^3 / (G (M1 + M2)))`

After source reconciliation, estimated periods are recomputed from the final axis and masses. If a measured period instead supplies an estimated axis, the axis is recomputed from that period and the masses. Published periods and axes are not changed merely to satisfy the Kepler check. The estimate flags survive condensed-catalog and JSON round trips.

Question marks in authored orbit fields mark estimates and are removed before parsing unit suffixes. A lone question mark denotes an unmeasured value. The Algol angular axes follow Baron et al. (2012), and the 99 Herculis period and angular axis follow Kennedy et al. (2012). Mizar's unmeasured wide period is inferred from its estimated separation and system masses.

## Photometry and identity

Photometric provenance records passband, component scope, uncertainty when available, and supplied reference epochs. VSX amplitudes are converted to magnitude ranges; limits and mixed passbands are not treated as exact V ranges. Estimated color outliers remain visible in audit diagnostics.

Final numbering consolidates unambiguous family links while preserving orbit centers and measured positions. Conflicting component slots remain unresolved rather than overwriting another star. When a visual family lacks its A slot, an existing member anchors the family: an explicit orbit center takes precedence, followed by the brightest visually linked member. No star or physical orbit is invented. An isolated component therefore receives an independent ID rather than a suffix pointing to a missing root. Stars in systems whose primary is magnitude 10 or fainter retain blank Alienorum IDs. Cached IDs are cleared before numbering so revised photometry cannot leave obsolete IDs behind.

## Reference documentation

- [SIMBAD contents and quality codes](https://simbad.cds.unistra.fr/Pages/guide/ch15.htx)
- [Hipparcos spectral classifications](https://vizier.cds.unistra.fr/ftp/cats/more/HIP/cdroms/docs/vol11sp.pdf)
- [VSX field definitions](https://cdsarc.cds.unistra.fr/viz-bin/ReadMe/B/vsx?format=html&tex=true)
- [Spectroscopic binary catalog fields](https://cdsarc.cds.unistra.fr/viz-bin/ReadMe/B/sb9?format=html&tex=true)
- [Gaia DR3 binary modeling](https://gea.esac.esa.int/archive/documentation/GDR3/Data_analysis/chap_cu4nss/sec_cu4nss_astrobin/ssec_cu4nss_astrobin_properties.html)
- [Algol interferometry, Baron et al. (2012)](https://arxiv.org/abs/1205.0754)
- [99 Herculis, Kennedy et al. (2012)](https://arxiv.org/abs/1201.1911)
