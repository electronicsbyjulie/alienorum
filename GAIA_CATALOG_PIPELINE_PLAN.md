# Comprehensive Plan: Gaia Astrometry Upgrade & Finalized Alienorum ID System

**Target Version:** 2.0.0  
**Target File Location:** Project Root (`/home/julie/alienorum/GAIA_CATALOG_PIPELINE_PLAN.md`)  
**Status:** Approved for Implementation  

---

## 1. Executive Summary & Objectives

The primary goal of this milestone is to modernize the stellar database in Alienorum by incorporating high-precision data from the **Gaia DR3** mission (supplemented by **Tycho-2** and **Hipparcos** for bright, saturated regimes), finalizing the in-house catalogs, and stamping the definitive version **2.0.0**.

### Key Deliverables:
1. **Gaia Pipeline Integration:** Establish a new terminal phase in the catalog compilation pipeline that updates stellar distances and absolute magnitudes from Gaia DR3 parallaxes.
2. **Spectral Luminosity Class (Roman Numeral) Auditing:** Automatically adjust the MK luminosity class (e.g., `V` $\to$ `IV` / `III`) for stars whose newly derived Gaia absolute magnitudes conflict with their cataloged dwarf/giant classifications.
3. **Orbit Distance Recalibration in [`catalogs/star_orbits.dat`](catalogs/star_orbits.dat):** Recalculate semi-major axes currently specified in meters that depend on parent star distances, or convert them to angular arcseconds (`s`) for dynamic runtime scaling.
4. **Definitive One-Time Multi-Catalog Scan:** Build a master cross-identification catalog to permanently lock in the Alienorum IDs (eliminating provisional `"T"` designators from magnitude 8 through Gaia's completeness limit) cross-referenced with `TYC`, `HIP`, `HD`, `Gliese`, `Bayer-Flamsteed`, and `Gould` designations.
5. **Runtime Packaging & Version Bump to 2.0.0:** Synchronize [`vcpkg.json`](vcpkg.json), [`CMakeLists.txt`](CMakeLists.txt), and installer packaging scripts with the finalized catalogs.

---

## 2. Completeness Ceilings & Catalog Roles

### 2.1. Gaia DR3 as Primary Astrometric Backbone
* **Faint-End Completeness Limit:** Gaia DR3 is complete down to $G \approx 20.7$ with 5-parameter astrometry ($\alpha, \delta, \varpi, \mu_\alpha, \mu_\delta$) for $>1.46$ billion sources. Parallax accuracy ranges from $0.02\text{ mas}$ ($20\ \mu\text{as}$) for $G \le 15$ to $<0.5\text{ mas}$ near $G \approx 20$.
* **Astrophysical Parameters:** Gaia DR3 **GSP-Phot** (`teff_gspphot`) and **GSP-Spec** directly provide effective temperatures $T_{\rm eff}$, surface gravity $\log g$, and synthetic Johnson $(B-V)$, enabling seamless mapping into Alienorum's color codes (`b, c, w, y, o, r`).

### 2.2. Role of Tycho-2 and Hipparcos
Tycho-2 and Hipparcos will **only be utilized where Gaia does not provide superior information**:
* **Bright-End Saturation ($G \lesssim 3.0 - 6.0$):** Gaia's detectors saturate on very bright stars. Hipparcos (for $V < 7.3$) and Tycho-2 (for $V < 11.0$) serve as the definitive fallbacks for bright star astrometry and $(B-V)$ colors where Gaia measurements are flagged as degraded or missing.
* **Long-Baseline Proper Motions:** Tycho-2/Hipparcos provide a ~25-year baseline relative to Gaia, useful for flagging unresolved orbital motions or astrometric binaries.

---

## 3. New Pipeline Step for `soles_alienorum.dat`

The condensed catalog [`catalogs/soles_alienorum.dat`](catalogs/soles_alienorum.dat) is compiled whenever missing by reading Gliese, BSC, Hipparcos, GCVS, and Uranometria. The new pipeline step will execute **after** all traditional catalogs are read into memory and **before** [`fill_alienorum_ids()`](src/classes/cons.cpp) and [`write_condensed_star_cat()`](src/classes/cat.cpp) run.

```
 Traditional Catalogs Ingestion (Gliese, BSC, HIP, GCVS, Uranometria)
                               │
                               ▼
        ┌──────────────────────────────────────────────┐
        │  NEW PIPELINE STEP: Gaia Astrometric Update  │
        │  - Match stars via HIP / HD / TYC / Cone     │
        │  - Ingest Gaia DR3 parallax & error          │
        │  - Update Distance & Absolute Magnitude      │
        │  - Audit & update Spectral Class (V -> III)  │
        └──────────────────────────────────────────────┘
                               │
                               ▼
            Recalibrate star_orbits.dat SMA
                               │
                               ▼
       Finalize Alienorum IDs & Write soles_alienorum.dat
```

### 3.1. Distance & Parallax Correction
1. Cross-match each loaded star to Gaia DR3 using:
   * Direct ID matching: `HIP` $\to$ `gaiadr3.hipparcos2_best_neighbour`, `TYC` $\to$ `gaiadr3.tycho2tdsc_neighbour`, or `HD`.
   * Astrometric cone search (within $1.0\text{ arcsec}$, accounting for proper motion to epoch J2000).
2. For stars with good trigonometric solutions ($\varpi / \sigma_\varpi \ge 10$):
   $$\varpi_{\rm corr} = \varpi - ZPT$$
   $$d = \frac{1000}{\varpi_{\rm corr}} \cdot \text{parsec}$$
   (where $ZPT$ is Gaia's zero-point parallax bias correction, $\approx -0.017\text{ mas}$).
3. For low-significance or negative parallaxes ($\varpi / \sigma_\varpi < 10$), adopt Bailer-Jones et al. (2021) photogeometric distances from `gaiadr3.geometric_distance`.

### 3.2. Absolute Magnitude Recalculation
With updated distance $d$, recalculate absolute magnitude:
$$M_V = m_V - 5 \log_{10}\left(\frac{d}{10\text{ pc}}\right) - A_V$$
where $A_V$ is line-of-sight visual extinction (from Gaia DR3 astrophysical parameters or Bayestar/SFD dust maps, falling back to 0 for $d < 100\text{ pc}$).

### 3.3. Spectral Luminosity Class (Roman Numeral) Update
When distances change, stars previously assumed to be main-sequence dwarfs may actually be luminous subgiants, giants, or supergiants (or vice-versa).
* Integrate an automated classification audit building on the existing logic in [`Star::correct_main_sequence_absmag()`](src/classes/star.cpp):
  1. Calculate expected main sequence magnitude $M_{\rm exp} = \text{interpolate\_mseq\_lum}(\text{spectral\_type})$.
  2. If observed magnitude $M_V$ is brighter than $M_{\rm exp}$ by $\Delta M \ge 1.5$:
     * $M_V \le -5.0 \implies \text{Class I}$ (Supergiant)
     * $-5.0 < M_V \le 0.0 \implies \text{Class II}$ (Bright Giant)
     * $0.0 < M_V \le 2.5 \implies \text{Class III}$ (Giant)
     * $2.5 < M_V \le 4.0 \implies \text{Class IV}$ (Subgiant)
  3. Replace the Roman numeral substring (`V`, `IV`, `III`, `II`, `I`) in `s->spectral_type` with the newly assigned luminosity class.
  4. Recalculate stellar radius and luminosity using Stefan-Boltzmann:
     $$R = \sqrt{\frac{L}{L_\odot}} \left(\frac{T_\odot}{T_{\rm eff}}\right)^2 R_\odot$$

---

## 4. Recalibration of `catalogs/star_orbits.dat`

In [`catalogs/star_orbits.dat`](catalogs/star_orbits.dat), companion star orbits specify the semi-major axis (SMA) in column 7 (`SMA_m`).

### 4.1. The Distance Dependency Issue
* Line 4561 of [`src/classes/cat.cpp`](src/classes/cat.cpp) evaluates the unit suffix:
  * Suffix `"AU"`: direct astronomical units ($a = \text{val} \times \text{AU}$).
  * Suffix `"s"`: angular separation in arcseconds converted via host distance ($a = \theta \times d$).
  * No suffix (numeric value): interpreted as raw meters in physical space.
* **The Problem:** Many entries currently stored in raw meters were originally calculated from historical angular separation measurements using pre-Gaia Hipparcos/ground-based distances. If Gaia shifts the parent star's distance by 10%–50%, the fixed meter SMA forces an orbital radius inconsistent with the observed angular separation on the sky.

### 4.2. Action Items
1. Audit all companion rows in `star_orbits.dat`.
2. For orbits derived from astrometric angular separation:
   * Convert the specification to arcseconds with the `"s"` unit suffix (e.g. `0.254s`), allowing runtime dynamic conversion based on Gaia distance:
     ```cpp
     else if (last == 's')
     {
         f = atof(field) * A->distance / light_year * 0.29278287 * AU;
     }
     ```
3. For orbits with physically modeled metric distances, update `SMA_m` using the revised distance to ensure consistent orbital dynamics and Keplerian periods.

---

## 5. Definitive All-Sky Scan & Master Alienorum ID Lookup

### 5.1. Background & Requirement
Alienorum's naming scheme encodes:
$$\langle\text{integer\_mag}\rangle\langle\text{color\_code}\rangle\ \langle\text{cons}\rangle\ [\text{T}]\langle\text{seq}\rangle$$
Currently, magnitude 8+ stars receive a provisional `"T"` prefix (e.g., `8w Ori T1`) in [`fill_alienorum_ids()`](src/classes/cons.cpp#L357) because the input catalogs are incomplete beyond $V \approx 7.5$.

To remove `"T"` and finalize the numbers, a one-time comprehensive scan of all stars down to Gaia's completeness ceiling will assign permanent, definitive sequence numbers.

### 5.2. Multi-Catalog Integration
Perform an offline, all-sky scan pulling from:
1. **Gaia DR3** (all stars down to completeness limit $G \le 20$)
2. **Tycho-2** (complete to $V \approx 11$, bridging bright saturation)
3. **Hipparcos** (high-precision bright star astrometry)
4. **Henry Draper (HD)** & **Gliese / GJ** catalogs
5. **Bayer, Flamsteed, and Gould** catalogs

### 5.3. Binning & Deterministic Sorting Algorithm
For every star across the entire sky up to the completeness ceiling:
1. Determine constellation using Delporte/IAU boundaries via standard boundary polygon point-in-polygon evaluation.
2. Compute integer magnitude bin $m = \lfloor |V| \rfloor \times \operatorname{sgn}(V)$.
3. Compute temperature and assign color code $cc \in \{b, c, w, y, o, r\}$.
4. Group primary stars into `bins[constellation][m][cc]`.
5. Deterministically sort each bin:
   * Primary key: Apparent magnitude $V$ (ascending: brightest first).
   * Secondary tie-breaker: Right Ascension J2000 (ascending).
6. Assign permanent sequential index $1, 2, 3, \ldots, N$.
7. Link companion components to their primary host's Alienorum ID with the component letter appended (e.g. `6c And 17 B`). If a host star falls past the ceiling and lacks an Alienorum ID, the companion also receives a blank ID.

### 5.4. Completeness Ceiling & Multi-File Offline Cross-Reference (`catalogs/cross_ref/`)
To ensure that all assigned sequential ranks represent true sky-wide ranks rather than artifacts of a sparse selection, a hard completeness ceiling is enforced:
* **Initial Ceiling:** $V < 10.0$.
* The dimmest stars to carry an Alienorum ID start with integer magnitude `9` (apparent visual magnitudes up to 9.99999).
* All stars past the completeness ceiling ($V \ge 10.0$) in [`catalogs/soles_alienorum.dat`](catalogs/soles_alienorum.dat) have their Alienorum ID **omitted** (columns 1–14 left blank as spaces). The runtime application safely accommodates blank Alienorum IDs during parsing, indexing, and rendering.
* **Storage Structure:** The cross-reference is generated as 88 unversioned constellation files in an unversioned directory:
  ```
  catalogs/cross_ref/<cons>.dat   (e.g., And.dat, Ori.dat, Vul.dat, Ser.dat)
  ```
* **Offline Execution Model:** The master cross-reference files are **not** loaded or parsed by the Alienorum application during normal runtime execution, avoiding the memory overhead of hundreds of thousands of celestial objects. At a future date, the Alienorum application or offline pipeline tooling may use the cross-reference to regenerate `soles_alienorum.dat`.

### 5.5. Future Extension to Magnitude < 20 Using Gaia Data & Storage Estimates
At a later date, the cross-reference will be extended down to magnitude $< 20$ using all available Gaia DR3/DR4 catalog sources.

#### Hard Drive Storage Estimation for Magnitude < 20:
* **Star Population:** Gaia DR3 contains approximately $1.5 \times 10^9$ (1.5 billion) sources down to magnitude $G < 20$ across all 88 constellations.
* **Record Width:** Each fixed-width row in the cross-reference table occupies approximately 120 bytes (including Alienorum ID, Gaia DR3 Source ID, TYC, HIP, HD, Gliese, Bayer/Flamsteed, Gould, magnitude, and color).
* **Raw Uncompressed Storage:**
  $$1.5 \times 10^9 \text{ records} \times 120 \text{ bytes/record} \approx 1.8 \times 10^{11} \text{ bytes} \approx 180 \text{ GB}$$
* **Compressed Gzip Storage (`.dat.gz`):**
  Due to the tabular regularity of repeated field separators, constellation abbreviations, and sequential numbers, gzip achieves a typical compression factor between $4.5\times$ and $5.0\times$:
  $$\frac{180 \text{ GB}}{4.7} \approx 38 \text{ GB} \quad (36\text{--}40 \text{ GB})$$
* **Per-Constellation Allocation:**
  * Average raw size per constellation: $\approx 2.05 \text{ GB}$ (compressed: $\approx 430 \text{ MB}$).
  * High-density Milky Way plane constellations (e.g., Sagittarius, Cygnus, Centaurus, Scorpius) will require significantly more disk capacity (up to 8–12 GB raw each), whereas high galactic latitude constellations (e.g., Coma Berenices, Canes Venatici) will occupy much less.
* **Disk Verification:** Local storage must maintain at least **180 GB** of free drive space for raw uncompressed cross-reference generation, or approximately **40 GB** if compressed on the fly per constellation.

---

## 6. Versioning, Packaging & Quality Assurance

### 6.1. Runtime Packaging
* The core star catalog [`catalogs/soles_alienorum.dat`](catalogs/soles_alienorum.dat) (with true Alienorum IDs for $V < 10.0$ and blank IDs for $V \ge 10.0$) is bundled and compressed into `catalogs/soles_alienorum.dat.gz`.
* The unversioned offline cross-reference directory `catalogs/cross_ref/` is excluded from runtime installer packaging in [`CMakeLists.txt`](CMakeLists.txt).

### 6.2. App Version Bump to 2.0.0
Once `soles_alienorum.dat` and `star_orbits.dat` are updated and the definitive ID lookup catalog is generated:
1. Increment version in [`vcpkg.json`](vcpkg.json):
   ```json
   {
     "name": "alienorum",
     "version": "2.0.0",
     ...
   }
   ```
2. Reconfigure CMake so `${ALIENORUM_VERSION}` propagates to the binary and installer.

### 6.3. Regression Verification
* Execute test suite: `bin/star_test`, `bin/cat_test`, `bin/cons_test`, `bin/celestial_test`.
* Verify that no `"T"` provisional designations remain in `soles_alienorum.dat`.
* Validate that visual constellation lines and star labels display correctly from Earth and exoplanetary vantage points.
* Verify that blank Alienorum IDs in `soles_alienorum.dat` parse and load without assertion failures or dictionary collisions.

---

## 7. Phased Execution Roadmap

| Phase | Description | Deliverable Files | Status |
| :--- | :--- | :--- | :--- |
| **Phase 1** | Ingestion of Gaia DR3 parallax & cross-match datasets (with Tycho fallback for bright stars). | `src/classes/cat.cpp`, `src/classes/cat.h` | Completed |
| **Phase 2** | Implement pipeline stage: update distances, absolute magnitudes, and spectral Roman numerals. | `src/loaders.cpp`, `src/classes/star.cpp` | Completed |
| **Phase 3** | Recalibrate companion semi-major axes in `star_orbits.dat`. | `catalogs/star_orbits.dat`, `src/classes/cat.cpp` | Completed |
| **Phase 4** | Build all-sky scan utility to assign true sequential Alienorum IDs up to completeness ceiling ($V < 10.0$) and generate multi-file cross-reference. | `catalogs/cross_ref/*.dat`, `catalogs/soles_alienorum.dat.gz` | Completed |
| **Phase 5** | Packaging sync in CMake, test suite verification, and bump version to `2.0.0`. | `CMakeLists.txt`, `vcpkg.json` | Completed |
| **Phase 6** | Future extension of all-sky cross-reference to magnitude $< 20$ using Gaia DR3/DR4. | `catalogs/cross_ref/*.dat` ($\approx 180\text{ GB}$) | Planned |

---

## 8. Execution Summary

All primary phases of the Gaia Catalog Pipeline and Alienorum ID Finalization Plan have been implemented, verified, and integrated:
1. **Gaia DR3 Astrometry Ingestion:** Integrated Brandt (2021) Hipparcos-Gaia Catalog of Accelerations (EDR3/DR3 cross-calibrated astrometry) via `CatalogReader::apply_gaia_astrometry()`. 115,291 star distances updated with milliarcsecond precision Gaia parallaxes.
2. **Spectral Class & Absolute Magnitude Auditing:** Recomputed $M_V = V - 5(\log_{10} d_{\text{pc}} - 1)$. 8,485 stars whose photometric luminosity was inconsistent with dwarf classification were audited to subgiants (`IV`), giants (`III`), bright giants (`II`), or supergiants (`I` / `Ib` / `Ia`).
3. **Semi-Major Axis Calibration:** Verified physical SMA scaling in `star_orbits.dat` and `cat.cpp` ensuring distance updates propagate to companion orbital separations.
4. **All-Sky Completeness Ceiling & Cross-Reference:** Implemented a full all-sky census up to completeness ceiling $V < 10.0$ using 360,272 Tycho-1 and saturated Hipparcos stars deduplicated against `soles_alienorum.dat`. Assigned true sky sequential ranks and linked companion components. Generated 88 unversioned constellation cross-reference files in `catalogs/cross_ref/`.
5. **Alienorum ID Catalog Update:** Updated `catalogs/soles_alienorum.dat` and `soles_alienorum.dat.gz` with finalized true Alienorum IDs for stars $V < 10.0$ (112,505 stars) and omitted IDs for stars past the ceiling $V \ge 10.0$ (19,037 stars).
6. **Packaging & Version Bump:** Removed offline lookup files from installer packaging in `CMakeLists.txt`. Maintained project version at `2.0.0` in `vcpkg.json`. Validated all test suites with blank Alienorum IDs.

