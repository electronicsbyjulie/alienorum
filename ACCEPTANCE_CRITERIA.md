# Alienorum acceptance criteria

Baseline updated: 2026-10-23, including the session deliveries listed in section 18.

This is the product and regression checklist for the current application, including deliberate catalog choices and the fixes made during this session. Each item describes observable acceptance behavior. An unchecked box means that a release reviewer has not recorded a result here; it does not assert that the feature is broken. Record the build, platform, data snapshot, result, and evidence when checking an item. Items marked **OPEN** retain unresolved behavior or a historical choice that still requires verification.

Named examples below are regression fixtures and catalog requirements. Runtime algorithms must obtain identities, physical properties, and special roles from data. Removing a named branch must preserve the deliberate behavior it represented through an appropriate data record or general rule. Historical corrections must not silently become obsolete just because their implementation was removed.

## 1. Deliberate selections and catalog conventions

- [ ] **D01 — Asteroid selection:** Preserve the complete numbered selection in Appendix A and its original loading condition: objects 1–4 with absolute magnitude below 8 load directly, and every explicitly selected number also loads directly regardless of that magnitude cutoff. Other valid records remain available through the asteroid browser.
- [ ] **D02 — Curated names:** Preserve the custom names and cross-identifiers in [starname.dat](starname.dat), [exoname.dat](exoname.dat), and the packaged object catalogs. A refresh must not replace a deliberate name with an unrelated catalog label.
- [ ] **D03 — Constellation vocabulary:** Preserve the packaged constellation names, genitives, and custom figures in [consline.dat](consline.dat). The genitives remain **Canis Majoris** and **Canis Minoris**.
- [ ] **D04 — Accepted Tau Ceti sky:** Preserve the accepted current Tau Ceti figures. From Tau Ceti, the Sun and Arcturus belong to the same constellation; acceptance must not impose the superseded individual-line expectations.
- [ ] **D05 — Custom vantage figures:** Preserve the custom Alpha Mensae figures while generating additional figures only from unused eligible stars. Preserve all other authored vantage figures and their naming data.
- [ ] **D06 — Body roles:** Preserve catalog-defined home-world roles, map inversion, known poles, vegetation colors, and artificial-satellite center codes, including after renaming and saving an object.
- [ ] **D07 — Measured properties:** Preserve measured galaxy orientation, distance, dimensions, band files, texture paths, and label extents. Preserve catalog-defined reference-star poles, including the Kappa Fornacis reference record. General rendering must consume these records without object-name conditions.
- [ ] **D08 — Source identity:** Preserve canonical source names, provenance, component letters, and periods. Source-letter translations must be explicit data with provenance; an alternate host designation alone must never change which physical planet a letter denotes.
- [ ] **D09 — Source disposition:** Reject false-positive or rejected records using source metadata. Do not revive a rejected planet merely because another source uses an alternate host name.
- [ ] **D10 — Wide and imaged objects:** Preserve valid wide companions and directly imaged objects when the source supplies sufficient placement information. Missing period alone is not a reason to discard a valid semimajor axis. Unknown orbits must remain explicitly unknown rather than fabricated.
- [ ] **D11 — Catalog integrity:** Rebuilding the condensed stellar catalog must preserve pre-existing records, identities, deliberate selections, and unrelated metadata. Additions must not silently remove or rewrite existing objects.
- [ ] **D12 — Stellar identifiers:** Temporary internal IDs for dim stars remain distinguishable from stable catalog identities; rebuilding must not treat an internal display ID as a new physical star.

### Historical choices requiring explicit preservation or reconciliation

These entries record the deliberate choices found during the earlier review. They are acceptance obligations to audit against data, not instructions to restore executable name comparisons.

| ID | Recorded choice | Acceptance criterion and current disposition |
| --- | --- | --- |
| H01 | 82 Eridani / HD 20794 / GJ 139 | Every host identifier resolves to one star. Its planets follow its 50° disk from star_orbits.dat through the general inclination rule. Host identity and disk alignment are repaired. None of this behavior is specific to any one star, and no object, 82 Eridani included, is hard-coded anywhere. |
| H02 | 82 Eridani planet letters | **RESOLVED:** The 648 day object is displayed as d and the 90 day object as c. Packaged period-based identities preserve this display convention independently of source letters. Retain the source designation as metadata; equivalent host aliases use the same displayed letters. Fresh/cache round-trips, repeated naming passes, serialization, and the 50° disk plane have focused regression coverage. |
| H03 | Mu Arae / HD 160691 | **RESOLVED:** Match [Wikipedia's Mu Arae system table](https://en.wikipedia.org/wiki/Mu_Arae), checked 2026-10-23: c/Dulcinea is the approximately 9.64-day planet, d/Rocinante approximately 308 days, b/Quijote approximately 645 days, and e/Sancho the outer approximately 4,000-day planet. Packaged identities match physical periods rather than blindly adopting a source's letters; the local NASA snapshot labels the inner-to-outer objects d, e, b, c. Names and canonical display-letter searches survive source-letter variants, cache round-trips, and repeated naming passes. |
| H04 | KOI-5 / TOI-1241 / KIC 8554498 | **OPEN audit:** Preserve the confirmed-source choice and exclude the historical seven-day false-positive interpretation. Source disposition and identity data must explain the choice. |
| H05 | PDS 70 e, HIP 65426 b, WD 0806-661 b | Preserve legitimate imaging records through general import rules and explicit source data. **OPEN:** PDS 70 e is absent from the NASA-only snapshot used by the broader catalog check. Its absence must be reported, not represented as a passing inclusion check. |
| H06 | 55 Cnc B / GJ 324 B | **OPEN audit:** Retain one companion identity and the intended display name across host imports, aliases, and binary reconstruction. |
| H07 | GJ 22 AC / HIP 2552 | **OPEN audit:** Preserve the recorded cross-identification without creating another host. |
| H08 | Proxima / GJ 551 and Alpha Centauri | **OPEN audit:** Preserve the intended multiple-system membership and the separate physical identity of each component through catalog relationships. |
| H09 | Zeta Reticuli / GJ 136 / HD 20766 / HD 20807 | Preserve the binary relationship and the recorded orbit-center direction: Zeta 2 is the primary and Zeta 1 is the companion. An obsolete identifier exclusion must not silently drop either valid star. |
| H10 | HIP 15371 and HIP 15330 | **OPEN audit:** Verify the former duplicate-import exclusions against the current cross-identification data. Both legitimate components must survive exactly once. |
| H11 | 41 The1 Ori and unresolved component labels | **OPEN audit:** Preserve the intended component interpretation through explicit identity and multiplicity data; do not merge separate systems based on similar labels. |
| H12 | QZ Ser (AB) / QZ Ser | **OPEN audit:** Preserve the intended host alias and system association. |
| H13 | Epsilon Eridani, Gamma Cephei, Pollux / GJ 286 | **OPEN audit:** Preserve long and abbreviated designations and authored planet names, including AEgir, through the shared alias path. |
| H14 | Teegarden's Star, Luyten's Star / GJ 273, Barnard's Star, Proxima | Preserve catalog spellings and host aliases without suppressing or duplicating planets. |
| H15 | Epsilon Indi Bb | **OPEN audit:** Preserve its physical identity, multiplicity, and intended object class; avoid representing a stellar or substellar companion twice as a planet. |
| H16 | Asteroid 5747 Williamina | Preserve the deliberate name and numbered identity through catalog naming data. The asteroid remains in the explicit selection. |
| H17 | Pluto, Eris, Quaoar, Sedna, Orcus, Haumea, Makemake | Preserve measured size information and the distinction between diameter, radius, and nonspherical dimensions. Replacing a named radius branch must not silently replace a measurement with an estimate. |
| H18 | Nu Orionis and uppercase NU Orionis | Keep their distinct identities and systems. Case normalization must not turn a variable-star designation into a Greek-letter designation. |
| H19 | Xi Ursae Majoris, Xi Tauri, Atlas, Tau Ophiuchi B | Preserve cached companion identity, intended constellation links, system planes, and resolved companion photometry across reloads. |
| H20 | White dwarf color selections | Preserve all published rows, alternative-model flags, verified identities, custom colors, and supported angular profiles. Color association must not change positions, magnitudes, or binary membership. |
| H21 | WD1011+2845, WD1008+290, LHS2229 and other published labels | Preserve verified alias associations and searchability while respecting the B1950/J2000 distinction. A colliding printed coordinate label is not authorization to merge two stars. |
| H22 | The Sun in overlapping stellar catalogs | Retain one physical reference star and its aliases; importing another source must not create a duplicate Sun. |

## 2. Startup, loading, and shutdown

- [ ] **L01:** Launch the desktop application on supported Linux, macOS, and Windows builds with a visible loading state and responsive progress reporting.
- [ ] **L02:** Obtain missing supported scientific catalogs automatically; reuse valid local catalogs on later launches.
- [ ] **L03:** Either supported condensed stellar-cache form suppresses unnecessary source downloads. The compressed packaged cache expands correctly.
- [ ] **L04:** An unavailable network service, failed TAP request, invalid response, or cancellation does not publish an incomplete snapshot or replace a valid cache with corrupt data.
- [ ] **L05:** Supported TAP requests can use the alternate HTTP method when the first method fails.
- [ ] **L06:** Host and planet passes use the same exoplanet snapshot rather than independently downloading inconsistent data.
- [ ] **L07:** [OBSOLETE] `noexo` defers exoplanet loading; F2 loads it on demand with progress and without duplicate starts. `ExosOnDemand` settings retain their documented effect.
- [ ] **L08:** `nosat` and `nosats` skip artificial satellites for that run. Combining satellite and exoplanet options works.
- [ ] **L09:** Catalog background work publishes fully initialized objects under the catalog synchronization rules; navigation cannot observe half-built positions, radii, or host links.
- [ ] **L10:** Closing the application waits for active texture tasks, including tasks lasting longer than five seconds, and rejects new work after shutdown starts.
- [ ] **L11:** Repeated catalog, constellation, and texture reloads do not retain stale object pointers, duplicate components, or unbounded temporary allocations.
- [ ] **L12:** Ordinary orbit-import failures produce the concise notice; `orbfails` enables detailed diagnostics.
- [ ] **L13 — Newer compressed stellar cache:** If `soles_alienorum.dat.gz` has a later filesystem modification time than `soles_alienorum.dat`, both startup and direct condensed-catalog loading expand and validate the archive before replacing the old data. A missing expanded file is created; an equally recent or newer expanded file is retained. Corrupt or truncated archives leave the old valid file intact and remove temporary output. Successful expansion records the archive's filesystem timestamp so an unchanged archive is not expanded on every launch.

## 3. Identity, search, and selection

- [ ] **I01:** GJ/Gliese, HD, HIP, Bayer, Flamsteed, Gould, custom, and imported aliases for one star resolve to the same object in every shared application search, including searches with fuzzy matching enabled.
- [ ] **I02:** Exact identifiers and exact aliases take priority over approximate or prefix matches to another display name.
- [ ] **I03:** Indexed exact search and the reference search agree; publishing or renaming an imported star updates the active index.
- [ ] **I04:** Fuzzy spelling tolerance works for ordinary names and imported textual aliases without silently changing numeric catalog identity.
- [ ] **I05:** A planet query resolves the host alias first. Equivalent host names plus the same physical planet letter return the same planet in exact and fuzzy searches.
- [ ] **I06:** Asking for an absent planet around a known host returns no result, rather than a similarly named planet around another star.
- [ ] **I07:** White dwarf B1950 catalog identifiers and J2000/WDJ identifiers retain their distinct namespaces. Canonical identifiers outrank colliding published labels; ambiguous or conflicting aliases are rejected.
- [ ] **I08:** WD 0806-661 imports reuse the original host before and after color association. Repeated host passes preserve object count and pointer identity.
- [ ] **I09:** Deleted stars release their HD/HIP lookup entries and registered aliases; later searches cannot access the deleted star.
- [ ] **I10:** Mouse hover displays the correct object's information; clicking selects it; clearing selection and clearing tracking work independently.
- [ ] **I11:** Distant unresolved systems display and select the primary even when a companion is brighter. Local resolved components remain individually visible and selectable.
- [ ] **I12:** Tracking keeps the selected object centered and retains its information until tracking is cleared.
- [ ] **I13:** The main search field, object dialogs, system explorer, stellar neighborhood, and catalog browsers retain consistent selection and navigation behavior.
- [ ] **I14:** Primary search and edit fields receive focus when their dialogs open; typed input does not accidentally activate global shortcuts.

## 4. Universe JSON and add-on import

- [ ] **J01:** Loading a valid universe through F4 and through the command line produces equivalent objects and relationships.
- [ ] **J02:** Missing optional values use supported defaults. Invalid records produce useful diagnostics without crashing or leaving dangling lookup entries.
- [ ] **J03:** Resolve orbit centers, system centers, and the actual illumination star, including when records appear later in the file or use catalog aliases.
- [ ] **J04:** A missing explicit illumination-center field does not prevent identifying the correct illuminating star from valid object ancestry and catalog data.
- [ ] **J05:** Detect self-orbits, missing references, and cyclic ancestry; terminate safely instead of recursing indefinitely or accepting invalid links.
- [ ] **J06:** Failed loading releases temporary records and publishes only valid linked objects according to the loader's documented behavior.
- [ ] **J07:** Saving and reopening preserves object class, names, aliases, physical values, uncertainty metadata, atmosphere, orbits, role metadata, map inversion, and custom colors.
- [ ] **J08:** Older universe files obtain supported missing role metadata from packaged catalogs without object-name branches.
- [ ] **J09:** Import supported SSC add-ons with their host relationships, unit conversions, textures, locales, and overwrite-map setting intact; unavailable optional assets have supported fallbacks.
- [ ] **J10:** User-defined stars, planets, moons, and other object kinds enabled by the Add Object dialog can be created, edited, saved, and reopened. Unsupported kinds are disabled or explained.
- [ ] **J11:** Saving user settings and saving the universe retain their distinct purposes and destinations.
- [ ] **J12:** Validate missing parents, cycles, and capacity before applying edits; rejected input must not partially rename or modify live objects. Bound imported fixed-size stellar fields and reject incompatible object-class updates.
- [ ] **J13:** Repeated loads retain stable legacy identity defaults, explicit zero precession, orbitless objects, correct planet tallies, and existing object identity. Saving only edited objects includes exactly the intended records.

## 5. Views, navigation, flight, and time

- [ ] **V01:** Spaceship view pans and zooms correctly. In Spaceship, Planetfall, and Sky Map views, the exact observing object is excluded from the sky regardless of object class, zoom, selection, or renderer. Its disc, point representation, glare, label, and stale picking bounds must not obscure or intercept the view. Parent planets, host stars, and other distinct bodies remain eligible for normal sky rendering; System View previews and the local ground/atmosphere/cloud/ring layers retain their intended behavior.
- [ ] **V02:** Horizon/planetfall view works on supported planets, moons, and asteroids, including direct entry and arrival during flight.
- [ ] **V03:** Sun Clock shows the illuminated surface in its cylindrical projection; panning, zooming, and locale selection remain coherent.
- [ ] **V04:** Sky Map provides a pannable, zoomable full-sky RA/declination view.
- [ ] **V05:** System view preserves object positions, orbit diagrams, rings, labels, and habitable-zone indications across scale changes.
- [ ] **V06:** Reference-plane controls select system, equatorial, orbital, ICRF, ecliptic, and galactic frames wherever offered.
- [ ] **V07:** Mouse buttons retain coarse, fine, and ultrafine panning. Cardinal azimuth shortcuts, reset controls, and zoom controls behave consistently.
- [ ] **V08:** Teleporting to a selected object establishes the correct local frame; return-home navigation restores the configured location.
- [ ] **V09:** Sublight acceleration, deceleration, and directional thrust remain below light speed and apply the intended relativistic behavior and time dilation.
- [ ] **V10:** Warp mode permits the intended faster-than-light travel, retains its distinct time behavior, and exits cleanly on full stop.
- [ ] **V11:** Once flight starts, changing the viewing direction does not silently redirect the existing flight vector. Tracking a flyby target retains the intended approach behavior.
- [ ] **V12:** Forward/backward surface walking retains its walking behavior rather than invoking spaceflight thrust. Modifier combinations apply the intended speed multipliers.
- [ ] **V13:** Time controls step forward and backward by minutes, hours, days, months, years, decades, centuries, and millennia. Return-to-present and local-timestep controls work.
- [ ] **V14:** Calendar and Julian-date input, including command-line date input, produce the intended epoch without corrupting orbital elements.
- [ ] **V15:** Supported command-line load, import, find, track, go, altitude, azimuth, zoom, theme, fullscreen, horizon, Sun Clock, and simulated-key commands retain their behavior; missing values are handled safely.

## 6. Horizon locales and observing location

- [ ] **P01:** In horizon mode, the S panel displays **View from near [locale]** when the nearest named locale is within 5 km along the body's surface.
- [ ] **P02:** This applies to every body with locale data, without a special Earth branch. The distance calculation uses that body's radius.
- [ ] **P03:** Choose the closest eligible locale when several lie within the cutoff; ignore unnamed and invalid entries.
- [ ] **P04:** Longitude wrapping, crossing ±180°, and positions near either pole produce correct proximity results.
- [ ] **P05:** Recompute the label as the viewer moves. Outside the cutoff, show the body name rather than retaining a stale selected locale.
- [ ] **P06:** Load locale data on first horizon-panel use even if the locale picker has never opened. An absent locale list is not reread from disk every frame.
- [ ] **P07:** Reloading application data allows locales to load again. Inverted-map locales retain correct degree coordinates and viewer-radian conversion.
- [ ] **P08:** Locale search, Go, Set Home, latitude/longitude edits, timezone, daylight-saving rules, and saved home settings remain functional.
- [ ] **P09:** Eye height follows the configured value and affects horizon geometry consistently; latitude, longitude, and elevation do not introduce visible seams or invalid coordinates.
- [ ] **P10:** Status readouts show local time, observing position, temperature, gravity, pressure, and day/twilight/night state consistently with the scene where applicable.

## 7. Stellar catalogs, photometry, and multiplicity

- [ ] **S01:** Catalog readers retain their fixed-column alignment, identifier fields, units, blank-value meaning, and supported metadata when condensed and expanded.
- [ ] **S02:** Proper motion, parallax/distance, stellar rotation, variability, and binary orbits update at the selected epoch.
- [ ] **S03:** Stellar visual and bolometric corrections remain finite and physical across supported temperatures; Earth-view objects do not become spuriously much brighter than catalog photometry.
- [ ] **S04:** Main-sequence corrections do not turn a valid dwarf into an unrelated giant or assign a zero visual magnitude to missing measurements.
- [ ] **S05:** Missing companion magnitudes remain unknown; combined-light measurements do not overwrite a separately measured companion magnitude.
- [ ] **S06:** Companion brightness comparisons use the system distance and preserve valid resolved photometry. Diagnostic brighter-companion reports are gated and list each system once.
- [ ] **S07:** Multiple-system component moves clear the old slot; merges, removals, and relinking preserve unique members and avoid duplicate orbit ownership.
- [ ] **S08:** Binary imports match exact valid identities, not nearby textual names. Cached HD companions and wide companions are reused.
- [ ] **S09:** Baked stellar orbits preserve source planes, full Julian epochs, and component links even when a display identifier is blank.
- [ ] **S10:** Gaia updates consistently adjust distances, companion photometry, and luminosity classification. The automated regression works from its temporary fixture without an optional large external dataset.
- [ ] **S11:** Stellar metallicity, abundance values, errors, source scales, and provenance survive catalog caches and universe saves; unresolved binary identifiers do not receive an arbitrary component's chemistry.
- [ ] **S12:** First-letter, constellation, exact-search, and other catalog indices agree with the live object set after loading and deletion.
- [ ] **S13:** Display names follow the deliberate hierarchy: starname.dat entry, Bayer, Flamsteed, Gliese, Gould, HD, HIP, then other names. Xi Ursae Majoris B must not regress to HD98230 when its preferred designation is available.
- [ ] **S14:** The compressed condensed catalog remains below the hosting service's individual-file size limit; retaining useful human-entered aliases must not require copying every external survey identifier.
- [ ] **S15 — Visual magnitude regression:** V373 Cas B retains a physically consistent visual absolute magnitude, with the synthetic regression between −4 and −2. Luminosity conversions distinguish bolometric and visual magnitude, apply the bolometric correction with the correct sign, and use supplied radius and temperature before a main-sequence guess. A measured zero magnitude remains a measurement, not a missing-value marker.
- [ ] **S16 — Catalog rebuild audit:** Preserve resolved companion photometry and the Gaia distance/photometry corrections when rebuilding `soles_alienorum`. GJ 229 B/C and GJ 758 B must not regain the old luminosity entries whose micro-solar scale was omitted. Bolometric corrections remain finite across cool dwarfs, hot stars, and the supported continuation beyond 50,000 K; main-sequence luminosity estimates are converted to visual magnitudes consistently.

## 8. White dwarf colors and neighborhood swatches

- [ ] **W01:** Recognize supported white dwarf spectra and identifiers; missing or invalid radii receive the compact-star fallback rather than a normal-star radius.
- [ ] **W02:** Preserve all 239 published color-table entries and their alternative/unresolved-model flags. A color row is not proof that its star has already been imported.
- [ ] **W03:** Verified published disc colors take precedence over computed atmosphere colors. Unsupported compositions and temperatures retain the documented fallback.
- [ ] **W04:** Hydrogen/helium reference tracks and supported gravity corrections use their documented ranges and assumptions; no invented atmosphere measurement is presented as observed.
- [ ] **W05:** Supported angular color profiles preserve integrated hue, use shared CPU/GPU coefficients, and follow viewing angle rather than surface rotation. Explicit surface maps remain intact.
- [ ] **W06:** WD1330+643 is present and searchable in the installed condensed catalog with its verified Gaia identity and green custom color, including on a fresh cache load.
- [ ] **W07:** The stellar-neighborhood white dwarf filter shows the relevant stars with colored swatches and supports their normal selection/navigation actions.
- [ ] **W08:** Neighborhood swatches preserve hue before saturation adjustment and use **4× saturation**, clamped to the valid range. They do not retain the superseded 8× or 50× setting.
- [ ] **W09:** Green, pink, purple, and other custom white dwarf colors reach the swatches through the same color path; intensity clipping must not turn the swatches uniformly white.
- [ ] **W10:** Swatch emphasis does not alter scientific source colors, stellar magnitudes, positions, or orbital membership.
- [ ] **W11:** Offline spectrum tools validate wavelength/flux units, sample ordering, coverage, finite values, and source integrity. Runtime rendering does not download spectra or integrate them per pixel.

## 9. Exoplanets and orbital inference

- [ ] **E01:** Supported NASA and EU data retain host identity, source disposition, periods, semimajor axes, eccentricities, masses/radii, errors, inclination errors, and transit evidence.
- [ ] **E02:** Exactly **90° without transit evidence** is suspect and treated as unknown. **89.92° remains valid**; a genuinely transiting 90° measurement remains valid.
- [ ] **E03:** Transit evidence survives boolean NASA flags, combined EU detection-method strings, legacy CSV imports, derived caches, and universe saves. Convert angular units only once.
- [ ] **E04:** A known circumstellar disk anchors otherwise inferred planetary planes through a general rule. For 82 Eridani, the disk reference is 50°.
- [ ] **E05:** Preserve valid measured inclinations, supplementary planes, and supported hot-Jupiter exceptions. Broad uncertain RV inclinations yield only under the documented conflict rule; the two-degree cutoff remains consistent.
- [ ] **E06:** The HD 3167 inferred inclinations checked by the catalog regression lie between **85° and 90°**, inclusive, rather than matching an obsolete single measurement.
- [ ] **E07:** Unknown planetary planes can use the applicable stellar companion plane when no stronger reference exists.
- [ ] **E08:** Minimum-mass corrections use the inferred inclination without losing the original minimum, measurement errors, or provenance.
- [ ] **E09:** Reapplying planet names or reloading a catalog preserves heliocentric source elements; repeated passes do not progressively rotate an already converted orbit.
- [ ] **E10:** Host imports retain every valid source cross-identifier, update the shared index, and reuse the host even when the global numeric lookup arrays are absent.
- [ ] **E11:** Rejected host creation leaves no stale cache pointers. New hosts have valid location, physical defaults, and self/system-center relationships before publication.
- [ ] **E12:** Host reload counts remain stable; the obsolete five-extra-objects note does not return.
- [ ] **E13:** Exoplanet browser loading, searching, selection, and host navigation work during and after background loading.
- [ ] **E14:** Source data and derived caches remain distinguishable during validation. Missing objects in a NASA-only snapshot are not silently treated as proof of a successful combined-source import.
- [ ] **E15 — Stable planetary display identity:** Apply authored host/period/display-letter/proper-name records from packaged data through the general importer. Reject ambiguous period matches rather than silently choosing a planet. Preserve source and display designations separately through universe saves; every equivalent host alias plus a displayed planet letter resolves to the same physical object in exact and fuzzy searches. The source designation remains provenance when its letter conflicts with the accepted display convention. Repeated naming passes retain object count, orbital periods, and orbital planes.

## 10. Planets, atmospheres, climate, and classification

- [ ] **C01:** Object classification uses physical evidence and actual orbital ancestry, respects an explicitly locked type, and distinguishes measured density from density derived from an estimated radius.
- [ ] **C02:** Short-period low-mass planets do not become hot Jupiters solely because of their period. Missing measurements do not leave a planet with a stellar class.
- [ ] **C03:** Explicit bulk mass/radius edits override old estimated envelopes and clear obsolete minimum-mass/error metadata as appropriate.
- [ ] **C04:** Newly added planets around stars begin in the conservative habitable zone; the zone follows the actual host and is shown in system view.
- [ ] **C05:** Close, crossing, or massive neighboring orbits produce the supported spacing warnings. Orbital resonance choices remain sorted and apply the selected relationship correctly.
- [ ] **C06:** The cosmic-shoreline estimate remains finite, continuous at the vacuum limit, and capped at **1000 bar** for inferred pressure. This inference cap must not silently overwrite an explicit observed/user-defined pressure.
- [ ] **C07:** Shoreline inference works for supported real and fictional bodies using the actual illumination context, including fictional objects in the local system.
- [ ] **C08:** Atmosphere accessors tolerate absent atmospheres without creating one accidentally. Composition normalization supports partial gas lists and sensible molar mass.
- [ ] **C09:** Pressure, opacity, particulates, greenhouse temperature, and scale height remain physical for supported rocky, giant, cold, and dense-atmosphere cases; extreme inputs do not overflow.
- [ ] **C10:** Cold atmospheres condense by gas partial pressure, recalculate greenhouse cooling to convergence, and do not restore frozen gases during later temperature queries.
- [ ] **C11:** Stored cold temperatures and incomplete gas lists also undergo phase checks; a valid warm observed temperature is preserved.
- [ ] **C12:** Local climate calculations are shared by the temperature readout, surface/ocean generation, frost, and vegetation decisions rather than contradicting one another.
- [ ] **C13:** Local temperature reflects the supported effects of latitude, illumination, elevation, water/land, and tidal locking; vacuum and extreme cases remain finite.
- [ ] **C14:** Elevation maps use correct meter scaling. Cold poles, warm dry terrain, locked daysides, and gradual frost boundaries appear consistently.
- [ ] **C15:** Lava-world day/night maps share relief, retain dark crust and orange fractures, and increase glow with temperature without leaving stale night maps after regeneration.
- [ ] **C16:** Orbital phase brightness peaks near opposition, is symmetric where expected, wraps correctly, and preserves supported cloud-dependent phase behavior.

## 11. Terrain, clouds, oceans, and atmospheric rendering

- [ ] **R01:** Surface, cloud, night, ring, and elevation maps remain aligned across loading, recoloring, resizing, and generation.
- [ ] **R02:** Ground, sea, terrain, and cloud horizons meet correctly, including apparent-horizon/refraction effects and dense-atmosphere coasts.
- [ ] **R03:** Water contains no land rocks or vegetation. Shorelines show land and shimmering sea correctly from either side, with finite geometry at full resolution.
- [ ] **R04:** Mountain slopes sample their own elevation/climate along their depth; vegetation borders retain both ground types during travel, and rock faces remain opaque.
- [ ] **R05:** Gaseous-body horizons are visible, independent of compass heading, cover nadir, and leave zenith clear as appropriate.
- [ ] **R06:** Cloud interiors and cloud-bank edges remain opaque through distance blending. Distant-cluster cutoffs accept or reject a whole cluster without visible fragments or vertical slicing.
- [ ] **R07:** Cloud geometry remains coherent while looking upward, turning, moving across boundaries, or traveling beneath it. Geometry caches invalidate after relevant travel or map changes.
- [ ] **R08:** Cloud undersides show slightly staggered, rounded oblate lobes rather than a perfectly flat clipping plane; verify views from below as well as oblique views. Twilight color and atmosphere tint remain coherent. In Sun Clock, a generated day-plus-cloud planet with no night map retains its dimmed surface behind nighttime clouds; Ctrl+C changes cloud visibility without restoring an otherwise missing surface. Earth and other bodies with actual night maps retain their night-map behavior.
- [ ] **R09:** Rain requires overhead cumulus or sufficient haze; clear sky does not produce rain. Rain probes agree with visible cloud gaps independently of camera direction and lighting.
- [ ] **R10:** Local illumination and scenery shadows follow the actual host/illuminating star rather than an unrelated system primary.
- [ ] **R11:** CPU and supported GPU ground, terrain, mesh, cloud, and sphere paths agree within their established image/depth tolerances across seams, poles, resizing, display scaling, map changes, and eye heights.
- [ ] **R12:** Independent GPU-stage switches and CPU fallbacks work. Unsupported GPU paths are identified as unverified or skipped rather than reported as executed successfully.
- [ ] **R13:** Biome shortcuts accept only uniform valid map regions and retain the reference result at boundaries and poles; hot dry terrain can skip irrelevant biome work without changing the image.
- [ ] **R14 — Host-sensitive pigments:** Newly generated vegetation uses the actual illuminating star's estimated temperature, including a companion host, to bias absorption bands by photon availability. Unknown, nonpositive, or nonfinite temperatures use the solar fallback. Identical random-engine states and parameters reproduce the palette within the supported build. Host spectra bias a diverse population rather than prescribe one foliage color.
- [ ] **R15 — Neutral spectral base:** Start from neutral reflectance, without the former fixed straw RGB base or a strong arbitrary cutoff extending through visible blue. Combine two narrower primary peaks, optional accessory bands, variable pigment concentration, and scattering. Green, blue, purple, brown, dark, and brighter foliage remain possible; broad absorption is allowed without making every pigment band broad.
- [ ] **R16 — Reflectance edge:** Include an explicit tissue-scattering edge and infrared plateau beyond the pigment absorption envelope. The generated edge follows the host-biased pigments, with the current leaf-like lower bound of 700 nm. An Earth-like test spectrum has strong blue/red absorption, a relative green window, and sharply higher near-infrared reflectance. Reflectance stays within [0, 1] through the tested 350–1,400 nm range.
- [ ] **R17 — Visible integration and model limits:** Derive RGB from the visible spectrum; an infrared-only reflectance change must not appear as visible red light. Treat the current blackbody illumination and leaf-like optical model as approximations: atmospheric spectral filtering, detailed molecular chemistry, and leaf heat balance are not implemented. Unused light can be transmitted as well as reflected; the implementation must not be described as proving thermal equilibrium or universal alien pigment behavior.
- [ ] **R18 — Purple/green population preference:** Target five purple plant palettes for every two green palettes by selecting pigment mixtures under the same host spectrum. Across large deterministic samples at 3,000, 5,100, 6,000, 7,500, and 10,000 K, count approximately 2–3 purple palettes per green palette using intrinsic RGB hue ranges of 260–330° and 80–160°, respectively, with saturation at least 0.15. Retain other hue families. This is an artistic population preference, not a scientific frequency prediction or a guarantee for a small sample of planets.
- [ ] **R19 — Biomes independent of color:** Generated pixels retain ground, water, and vegetation identity independently of RGB. Identically blue ocean and foliage pixels still produce water and plants in their proper locations; pure-blue vegetation is recognized. Terrain, shoreline waves, rocks, and plants use the same displaced biome coordinates. Predominantly frozen generated pixels are solid ground rather than liquid water or foliage.
- [ ] **R20 — Biome persistence and boundaries:** Surface PNG save/load preserves generated material labels without changing RGB or transparency. Uniform-color optimizations must reject regions containing different material labels. Imported and older saved maps without labels retain their fallback behavior; regeneration supplies labels and removes color ambiguity. Elevation-only imports must not discard surface labels.
- [ ] **R21 — Crown clearance:** Plant placement reserves the sum of neighboring crown radii plus clearance in world space, accounting for each generated plant size and the configured crown scale. Retained plants keep their identities and positions during travel, panning, and boundary crossings. Perspective overlap between physically separate crowns is distinct from physical crown intersections.
- [ ] **R22 — Independent scenery ranges:** Plant candidate enumeration reaches the configured 1,000 m plant range plus the crown-clearance margin; rocks retain their 100 m range and existing distribution. A distant-only vegetation strip near the plant cutoff must render plants. Neighbor searches remain local, and panning reuses valid placement data; changing location, map generation, palette, or crown scale invalidates it.
- [ ] **R23 — Saved colors and example hosts:** Preserve explicitly supplied vegetation colors and saved planet parameters during ordinary loading. Randomizing a planet's generated texture applies the current palette model. Use Epsilon Eridani and Zeta Tucanae as example hosts without hard-coded rendering branches; Zeta Tucanae is F9V. Compare intrinsic foliage colors separately from brown bare soil, deserts, frost, and local temperature restrictions.
- [ ] **R24 — Observer exclusion at rendering boundaries:** The object-list, single-object, CPU sphere, direct GPU sphere, and internal galaxy-band paths enforce observer exclusion by object identity. A direct renderer call cannot bypass it. Clear prior screen visibility, label state, projected position, and hit bounds when excluding an observer. Self-tracking must not make the observer a sky hover target. Cover star, planet, moon, artificial satellite, comet, and galaxy observers across all three sky views; verify that a System View preview can still draw the observing planet. The Quijote I snapshots are the reported regression context; the distinct parent Quijote remains a valid sky object.

## 12. Stars, galaxies, labels, and sky presentation

- [ ] **G01:** Stars, galaxies, planets, rings, and comets render at the intended apparent positions, sizes, colors, and brightness over the supported zoom range.
- [ ] **G02:** Extreme zoom preserves projected centers and limb visibility. Offscreen/background discs are culled before expensive glare work and do not produce displaced artifacts.
- [ ] **G03:** Rings can remain visible when their host disc is behind the camera. A planet with an unknown host radius remains visible in system view.
- [ ] **G04:** Labels follow their object's depth and are occluded by opaque discs appropriately. Short label storage remains valid through drawing.
- [ ] **G05:** Choosing a label category preserves label visibility. Brightest, intrinsic-brightness, nearest, Bayer, Flamsteed, Gould, sunlike, planet-host, habitable-zone-host, known-pole, and binary categories retain their filters and thresholds.
- [ ] **G06:** Local-system labels, favorite labels, short labels, galaxy labels, galaxy bands, axes, grids, orbits, and constellation lines can be controlled independently as offered.
- [ ] **G07:** Brightness, gamma, zoom reset, red-light mode, white-background mode, realism mode, default annotations, and cursor visibility retain their intended effects.
- [ ] **G08:** Spheroid and disc galaxies use the correct catalog-driven shape/orientation and their available internal/face-on textures; serialized galaxy properties round-trip.
- [ ] **G09:** Fullscreen, window resizing, and supported display scaling preserve panel usability, selection positions, and rendering bounds.

## 13. Constellations from every vantage

- [ ] **K01:** Heliocentric boundaries, labels, genitives, and figures load correctly; catalog geometry handles polar fallbacks and split regions.
- [ ] **K02:** Reference-vantage selection recognizes the reference star and its aliases, preserving authored figures before generating replacements.
- [ ] **K03:** Nearby views within the documented 10-light-year reuse radius select the appropriate existing vantage rather than generating redundant skies; verify Alpha Centauri, Barnard's Star, Sirius, and the GJ 67/Upsilon Andromedae case.
- [ ] **K04:** 47 Ursae Majoris and GJ 86 generate the expected viable sky coverage; GJ 86 retains the established minimum-figure check.
- [ ] **K05:** Generated segments respect the **15° maximum length**, bright-star coverage, connected-shape constraints, and the minimum of three lines per accepted component.
- [ ] **K06:** Three-star chains can close into triangles; isolated one-line fragments are rejected.
- [ ] **K07:** Separation from other figures follows the documented half-length rule. Near-miss and passerby checks run in both directions and include eligible fainter stars.
- [ ] **K08:** Generated figures remain connected without stray fragments in the Hamal regression and preserve the accepted Tau Ceti criterion in D04.
- [ ] **K09:** Progressive generation leaves the application responsive. Saving vantage constellations, reloading, and cache reuse retain the consline-compatible format and intended figures.

## 14. Asteroids, comets, and artificial satellites

- [ ] **A01:** Astorb parsing preserves numbered identity, name, epochs, orbital elements, magnitude, and measured physical data. Unknown radii use the supported albedo estimate without replacing measured sizes.
- [ ] **A02:** The asteroid browser can find and load valid objects outside the automatic selection; selected objects can be centered, tracked, visited, and edited where supported.
- [ ] **A03:** Comet parsing, designations, display names, orbits, activity, coma, and tail rendering remain coherent across time and viewpoint changes; malformed rows fail safely.
- [ ] **A04:** The comet browser supports search, loading, selection, and navigation without duplicate objects.
- [ ] **A05:** Satellite sources respect their download interval, preserve preferred-source and master identity, and merge supplemental records without duplication.
- [ ] **A06:** Satellite CSV accepts quoted fields, empty fields, and trailing separators. Records without a valid placement are rejected safely.
- [ ] **A07:** Real-time satellite positions and supported nodal precession follow the selected epoch. Host resolution uses catalog center codes even if the host is renamed.
- [ ] **A08:** Satellite browser search works by name and category. Loading/centering and orbital-view orientation, including Earth-up control, remain usable.

## 15. Editing, textures, exports, and settings

- [ ] **U01:** Every enabled editor field applies its value with the displayed units and survives save/reopen; cancel or dialog selection changes do not silently overwrite another object.
- [ ] **U02:** Supported editor controls cover names, classification, mass, size, albedo, rotation, temperature, poles, orbital elements/epoch/precession, atmosphere/composition, vegetation/water, and texture controls.
- [ ] **U03:** Randomization handles absent, negative, nonfinite, and extreme inputs without undefined conversions or representation aliasing; an explicit random seed is retained.
- [ ] **U04:** Texture save, reload, update, regenerate, reseed, and resolution controls retain their distinct actions; long generation exposes progress and prevents conflicting operations.
- [ ] **U05:** Surface and bump-map PNG exports release temporary storage, reject absent elevation data, handle flat elevation ranges without division by zero, and report buffered write errors.
- [ ] **U06:** Grayscale elevation PNGs preserve the correct numeric scale rather than distorting terrain height.
- [ ] **U07:** Texture loaders reject empty/oversized files, failed seeks, failed allocation, and short reads before decoding; existing valid maps remain usable after an input error.
- [ ] **U08:** Snapshot export produces the visible scene in the supported image format without corrupting later frames.
- [ ] **U09:** General and per-weekday themes, theme cycling, brightness settings, home location, eye height, and supported loading preferences survive user-settings saves and restarts.
- [ ] **U10:** The F1 menu and documented shortcuts invoke the same actions. Established shortcuts and top-level menus remain compatible unless an explicitly approved compatibility change is made.
- [ ] **U11:** Developer-dial display, value/step controls, timing switches, magnitude diagnostics, and other enabled diagnostic modes function without changing normal-mode behavior.
- [ ] **U12:** Delete Object removes the intended object safely and leaves selection, tracking, system navigation, and lookup tables in a valid state.

## 16. Browsers, favorites, information, and alerts

- [ ] **B01:** S and N panels reflect the current observing context and selected/hovered/tracked object, with correct units and supported physical/orbital information.
- [ ] **B02:** System Explorer searches and filters supported local-system object types and offers consistent find, center, track, and go actions.
- [ ] **B03:** Stellar Neighborhood displays the intended neighborhood, filters and sorts its rows, and retains distinct stars and components.
- [ ] **B04:** Adding/removing favorite stars, searching favorites, navigating them, and saving their settings work without changing unrelated objects.
- [ ] **B05:** An empty favorites list disables selection, navigation, alerts, and removal actions; no absent row is accessed.
- [ ] **B06:** Rise/set alerts can be enabled and cleared for supported objects, fire for the appropriate observing context, and use the supported sound path without repeated spurious triggering.
- [ ] **B07:** Browser filters, scrolling, and selection remain valid when background catalogs finish loading or rows disappear.
- [ ] **B08:** Custom rise/set sound paths and the enable/disable preference survive settings saves. Missing audio files or unavailable audio devices use the supported fallback without stopping the application; changing observer position resets horizon-event tracking.
- [ ] **B09:** Stellar Neighborhood supports its distance cutoff, minimum planet count, habitable-zone-host, known-pole, sunlike, and white-dwarf filters with the intended combinations and exclusions.

## 17. Ephemerides, numerical safety, and release delivery

- [ ] **Q01:** Ephemeris loading handles missing, empty, short, and unordered files safely, closes streams, retains the final sample, and releases previous storage on reload.
- [ ] **Q02:** Interpolation uses bounded sample pairs and handles both endpoint dates. Eccentricity remains dimensionless and is not interpreted as a time quantity.
- [ ] **Q03:** Core coordinate, orbit, color, chemistry, and projection calculations remain finite for supported inputs and handle invalid data through defined fallbacks.
- [ ] **Q04:** Build and run the applicable automated suites with documented data prerequisites. Record failures individually; do not hide later suite results after an earlier failure.
- [ ] **Q05:** Python data utilities and packaging shell scripts retain their applicable validation and syntax checks.
- [ ] **Q06:** The Windows installer includes every tracked runtime catalog, static map, font, shader, audio file, galaxy asset, and ephemeris without a runtime download/generation path. New runtime assets update both installation rules and packaging guards.
- [ ] **Q07:** Packaging uses the intended tracked assets and excludes local scratch files. The installed application can expand its compressed catalogs and use its bundled ephemerides.
- [ ] **Q08:** Catalog-driven galaxy properties and white dwarf color/gravity/limb data ship in the installer; no runtime calculation silently depends on an unbundled development-only file.
- [ ] **Q09:** Version changes follow the repository's documented compatibility policy. Release validation records platforms, data snapshots, unsupported GPU paths, and any remaining failures.

## 18. Session delivery record and unresolved checks

The following records connect this checklist to the completed work. They are historical evidence, not a fresh execution of every criterion above.

| Delivered work | Criterion groups | Evidence anchor |
| --- | --- | --- |
| Local climate, condensation, stored temperatures, explicit bulk edits, conservative-zone defaults, spacing warnings | C01–C16 | Planet and rendering regressions; session history before the code audit |
| Label-category visibility and extreme-zoom/glare corrections | G02, G05 | `3d6db9e`, `22ca9c2` |
| Earth-view photometry safeguards and corrected stellar catalog | S03–S06 | `e196a77`, `8473ec8` |
| PNG export safety and grayscale elevation scaling | U05–U06 | `85f2462`, `efdaf8e` |
| Constellation segment, component, clearance, and Tau Ceti corrections | D03–D05, K01–K09 | `e11f40f`, `94acef7`, `8f7d8cd`, `a6a79fc` |
| Empty favorites, ephemerides, randomization, texture input validation | B05, Q01–Q03, U03, U07 | `65029ad`, `06ca266`, `1af37aa`, `4e7d2ac` |
| Stable repeated exoplanet orbit conversion and isolated Gaia regression | E09, S10 | `a2a9c39`, `c1a6a65` |
| General exact-90°/transit/disk rule | E02–E05 | `62d3927` |
| Catalog-driven body roles, constellation roles, galaxy properties, and removal of named runtime branches | D06–D10, G08, K01–K02 | `c35a534`, `4f79587`, `d0eb9eb`, `5bbd1cf`, `a1f275d` |
| Exact restoration of deliberate asteroid selection | D01, Appendix A | `c057985` |
| Canis genitives and 1000-bar inferred-pressure cap | D03, C06 | `91c58f0`, `94a7e2c` |
| Robust saved-universe host/illumination resolution | J01–J08 | `69b740b` |
| White dwarf host identity, searchable green star, rebuilt cache, final 4× swatches | I07–I08, W01–W11, E12 | `3eaeb98`, `2f9450e`; the later commit supersedes the temporary saturation settings |
| HD 3167 interval acceptance | E06 | `67ff1ed` |
| Consistent stellar/planet aliases and deleted numeric-cache cleanup | I01–I09, E10–E11 | `71462c2`: 57 search/serialization checks passed; broader catalog run had 53 of 54 checks passing |
| Nearby-locale status, lazy loading, moving-view fallback, inverted-map degree correction | P01–P10 | `5474a15`: application rebuilt; 54 celestial checks passed |
| Rounded cloud undersides and Sun Clock day/cloud fallback without a night map | R06–R08, V03 | `f7c2610`, `85f0f89`; night-map fallback has rendering regression coverage |
| Newer gzip cache refresh, preservation of old data after corrupt/truncated input | L03–L04, L13 | `3b8fe17`; archive regression passed |
| Host-sensitive neutral pigment spectra, biome identity independent of RGB, PNG material persistence, crown clearance | R14–R15, R19–R21, R23 | `a0c0565`; 14 color, 44 utility, 15 map, and the generated-ocean climate checks passed; 42 rendering checks passed with six GPU configuration skips |
| Purple/green population preference | R18 | `e019642`; 15 color checks passed, including five stellar-temperature ensembles |
| Explicit reflectance edge, infrared plateau, and visible-only RGB integration | R16–R18 | `dc804af`; application rebuilt and all 18 color checks passed; this supersedes the earlier color-suite counts |
| Independent plant distance cutoff | R22 | `2472365` established the 1,000 m plant cutoff; the later session range fix extends candidate enumeration while retaining the 100 m rock field |
| Physical planet identities, 82 Eridani c/d, and Wikipedia-aligned Mu Arae names | H01–H03, D08, E09, E15, I05 | `a80348e`; three focused identity/orbit checks passed across fresh/cache round-trips and repeated passes, plus all 57 search/serialization checks |
| Full plant candidate range, local crown-neighbor queries, and placement cache | R19–R22 | `80ddbd6`; application rebuilt; all 45 utility checks and 43 rendering checks passed, with six GPU configuration skips. A 750–950 m vegetation strip renders plants; rocks remain limited to 100 m. |
| Observer excluded at every sky renderer, stale picking-state cleanup, self-tracking rejection, and preserved System View previews | V01, R24 | `ObservingObjectNeverEntersSkyRenderingOrRetainsPickingBounds`, `TrackingTheObserverCannotMakeItTheSkyHoverTarget`, and `SystemPreviewCanStillDrawTheObservingPlanet`; application rebuilt; 46 rendering checks passed, six GPU configuration skips, and five disabled checks were not executed. |

- [ ] **OPEN — Catalog integration:** Resolve the absent PDS 70 e record before marking the broad exoplanet integration case fully passing. The focused 82 Eridani letter/period regressions close H02 but do not establish that the separate missing-source-object case passes.
- [ ] **OPEN — Historical data review:** Complete the remaining audits H04, H06–H08, H10–H13, and H15 against the intended packaged/source data. Preserve the resolved H02/H03 requirements and the other recorded choices during future refactoring.
- [ ] **OPEN — Optical model validation:** Atmospheric spectral filtering, detailed pigment/tissue optics, and a leaf heat-budget calculation remain outside the current implementation. The red edge is a leaf-like optical approximation; the purple preference is artistic. A successful RGB or spectral regression does not establish a universal alien photosynthesis prediction.
- [ ] **OPEN — Barycentric motion proposal:** The earlier request for common-center-of-mass motion of binaries and nested multiples remains a design/investigation item, including consequences for planets and moons. Do not report this as implemented merely because binary membership and orbit-center identity were repaired.
- [ ] **OPEN — Hardware coverage:** Execute GPU-dependent comparisons on supported hardware. Historical skipped GPU checks do not establish runtime parity.
- [ ] **OPEN — Installer execution:** Run the complete Windows packaging/install smoke test in an environment with its build tools. Source-list inspection alone does not establish a successful installer build.

## Appendix A. Protected asteroid numbers

The following is the complete explicit selection restored in `c057985`. Preserve the set and loading condition in D01. A future move into a data file must retain every number and include that file in packaging.

```text
55, 89, 105, 116, 490, 742, 896, 1001, 1006, 1134, 1143, 1221
1388, 1404, 1421, 1566, 1604, 1691, 1693, 1709, 1741, 1772, 1776, 1789
1790, 1791, 1814, 1815, 1823, 1862, 1964, 1991, 2000, 2001, 2002, 2060
2062, 2069, 2101, 2161, 2244, 2247, 2309, 2322, 2362, 2476, 2675, 2688
2709, 2769, 2801, 2807, 2810, 2830, 2937, 2985, 2999, 3130, 3142, 3153
3163, 3313, 3350, 3351, 3352, 3353, 3354, 3355, 3356, 3366, 3412, 3524
3534, 3600, 3768, 3838, 3895, 3905, 3948, 4062, 4147, 4169, 4179, 4180
4221, 4330, 4337, 4444, 4457, 4500, 4513, 4628, 4659, 4716, 4804, 4987
5000, 5020, 5370, 5471, 5535, 5668, 5747, 5773, 5790, 5803, 5811, 6006
6032, 6123, 6143, 6186, 6433, 6469, 6470, 6471, 6486, 6493, 6701, 6714
6826, 6875, 6914, 6999, 7000, 8000, 50000, 90377, 90482, 134340, 136108, 136199
136472, 163693, 486958, 541132
```

## Validation map

| Area | Primary repository evidence |
| --- | --- |
| Menus, shortcuts, views, dialogs | [README.md](README.md), [src/inputs.cpp](src/inputs.cpp), [src/dialogs.cpp](src/dialogs.cpp), [src/alienorum.cpp](src/alienorum.cpp) |
| Catalogs, identities, JSON, add-ons | [src/tests/cat_test.cpp](src/tests/cat_test.cpp), [src/tests/serial_test.cpp](src/tests/serial_test.cpp), [src/classes/sscimport.cpp](src/classes/sscimport.cpp) |
| Stellar physics and colors | [src/tests/star_test.cpp](src/tests/star_test.cpp), [src/tests/color_test.cpp](src/tests/color_test.cpp), [WD_COLOR_RENDERING.md](WD_COLOR_RENDERING.md) |
| Vegetation spectra, categorical biomes, and crown placement | [src/classes/color.cpp](src/classes/color.cpp), [src/classes/surfacescatter.h](src/classes/surfacescatter.h), [src/tests/color_test.cpp](src/tests/color_test.cpp), [src/tests/misc_test.cpp](src/tests/misc_test.cpp), [src/tests/rendering_test.cpp](src/tests/rendering_test.cpp) |
| Planets, climate, locales, ephemerides | [src/tests/planet_test.cpp](src/tests/planet_test.cpp), [src/tests/celestial_test.cpp](src/tests/celestial_test.cpp), [src/tests/moon_test.cpp](src/tests/moon_test.cpp) |
| Sky figures, galaxies, images | [src/tests/cons_test.cpp](src/tests/cons_test.cpp), [src/tests/galaxy_test.cpp](src/tests/galaxy_test.cpp), [src/tests/rendering_test.cpp](src/tests/rendering_test.cpp) |
| Satellites, comets, alerts, navigation | [src/tests/satellite_test.cpp](src/tests/satellite_test.cpp), [src/tests/comet_test.cpp](src/tests/comet_test.cpp), [src/tests/sound_test.cpp](src/tests/sound_test.cpp), [src/tests/housekeeping_test.cpp](src/tests/housekeeping_test.cpp) |
| Utilities and release delivery | [makefile](makefile), [CMakeLists.txt](CMakeLists.txt), [package/build_windows_installer.sh](package/build_windows_installer.sh), [known_bugs](known_bugs) |
