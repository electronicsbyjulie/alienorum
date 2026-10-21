Stellar chemistry and planetary classification implementation plan

Improve Planet::classify() using reliable stellar chemistry, observed planetary properties, and explicit evidence provenance. Keep an observational classification separate from a model of primordial building materials. A stellar iron abundance alone does not determine a planet's core fraction, water inventory, or atmospheric composition.

1. Preserve observational evidence.

   Record whether planetary mass and radius are observed, estimated, or minimum-mass measurements. Preserve this information through catalog ingestion and universe saves. A density calculated from an estimated radius must not become a measured density on a subsequent classification call. Respect explicit type locks and report the inputs behind an inferred type.

2. Keep source catalogs separate.

   Store imported data, source metadata, and any calibration artifacts in separate source-specific folders: catalogs/NASA/, catalogs/ExoplanetEU/, catalogs/XHIP/, catalogs/Hypatia/, and catalogs/GALAH/. Do not introduce a shared folder containing several source catalogs. Preserve established generated catalog paths and support existing caches. Downloaded source catalogs remain outside Git and the installer. The application fetches an absent source only when both forms of the condensed stellar catalog are also absent. Add every shipped runtime file, including the generated classification model, to CMake installation rules and Windows packaging checks.

3. Import detailed stellar abundances.

   Extend stellar chemistry beyond metallicity with Fe, Mg, Si, C, and O measurements from Hypatia and GALAH where accessible and reliable. Preserve abundance scale, solar normalization, uncertainty, source, and quality flags. Match explicit identifiers and skip ambiguous binary components. Retain source coverage and rejection counts. Unknown quantities remain unknown; unavailable data from one source must not invalidate useful data from another.

4. Derive qualified chemistry diagnostics.

   Validate finite metallicities and distinguish [Fe/H] from [M/H]. Exclude uncalibrated XHIP measurements from automatic class adjustments. Derive Fe/Mg, Mg/Si, and C/O only from compatible abundance measurements on a declared solar scale. Compute a clearly labeled primordial iron-to-silicate proxy with propagated uncertainty when the chemical assumptions hold. Keep this proxy separate from an asserted planetary core fraction.

5. Calibrate a limited metallicity contribution.

   Build an observational benchmark from NASA planets with reliable mass, radius, host parameters, and metallicity. Compare a baseline using planetary properties against a model that also includes host [Fe/H], with validation split by host star. Publish sample selection, metrics, fitted coefficients, and applicability limits. Enable a metallicity contribution only when validation supports it. Apply it to ambiguous cases within that domain, preserving stronger direct observations. Giant-planet occurrence rates alone are not posterior probabilities for an already detected planet's type.

6. Make classification repeatable and physically coherent.

   Separate class selection from radius and color estimation. Use measured density when available and keep modeled values explicitly qualified. Resolve the actual host through orbital ancestry without substituting another binary component's chemistry. Distinguish giant planets from short-period low-mass bodies. Distinguish cold rocky surfaces from ice-rich bodies using composition evidence and irradiation, and distinguish ice giants from gas giants without treating low density alone as proof of a gas giant. Report class, evidence, chemistry contribution, and uncertainty without introducing artificial atmospheres. Verify repeated calls and input-order independence.

7. Validate and deliver in three commits.

   First commit and push this plan as an automated commit. Implement the source imports, chemistry persistence, calibrated model, classification changes, and focused feature tests; build, verify packaging, audit changed classifications, then make an automated commit and push. Finally add independent Solar System prediction tests for all eight planets and representative rocky and icy moons, clear preset type locks and classifications before prediction, verify repeated calls and absence of atmosphere side effects, and make the final automated commit and push. Fix implementation defects exposed by those benchmark tests before that final commit.

Scientific basis: [NASA parameter definitions](https://exoplanetarchive.ipac.caltech.edu/docs/API_PS_columns.html), [Hypatia](https://www.hypatiacatalog.com/), [GALAH DR4 quality guidance](https://www.galah-survey.org/dr4/overview/), and [Brinkman et al. on uncertain star and planet composition relations](https://arxiv.org/abs/2409.08361).

Implementation results

The stellar archive now preserves separate Hypatia bundles for 5,994 stars and GALAH bundles for 400 stars, covering 6,371 distinct stars. Source files live in the folders listed above; the supporting Hipparcos crossmatch has its own catalogs/GaiaDR3/ folder. Existing fixed columns and metallicity records are preserved. Chemistry diagnostics carry the source, normalization, uncertainty interpretation, and assumptions; they do not assert planetary core or water fractions.

The NASA benchmark contains 176 qualifying planets around 137 hosts. Five host-grouped validation folds give baseline log loss 0.053507 and metallicity-model log loss 0.052873. The host-bootstrap 95% interval for improvement is [-0.003646, 0.004331]. It crosses zero, so the shipped metallicity adjustment is disabled. Future activation requires stronger independent validation; the infrastructure and diagnostics remain available.

The implementation includes measurement provenance, universe and catalog persistence, deterministic physical classification, source matching checks, and source-specific packaging rules. The classification audit compares the previous decision rules with the new classifier on fixed catalog inputs, with atmosphere and color synthesis excluded. Implementation was committed and pushed as 843d093 after 47 planetary, 49 stellar, 36 catalog, and 12 Python checks passed. Three broader catalog test failures also occur on the prior committed baseline. The application builds successfully; installer file checks and shell syntax pass, while CMake execution is unavailable in this environment.

The subsequent independent Solar System benchmark covers all eight planets and ten moons: Moon, Phobos, Deimos, Io, Europa, Ganymede, Callisto, Enceladus, Titan, and Triton. It imports physical values without catalog classification labels or type locks. Every body is predicted from nine starting classes, with known or unknown solar iron abundance, supplied or omitted surface temperature, and repeated calls. All 18 benchmarks pass; measured masses and radii remain unchanged and no atmospheres are created. The complete planetary suite passes all 65 tests. These are broad class checks, not validation of detailed exoplanet composition or water fractions.

Source cache lifecycle

The application acquires missing NASA, exoplanet.eu, XHIP, Hypatia, GALAH, and supporting Gaia DR3 snapshots directly through their public services during a rebuild of the condensed catalog. Existing source files are reused. Either soles_alienorum.dat or its compressed archive suppresses these source downloads. Accepted chemistry is applied before the condensed catalog is written. Requests have time limits and cancellation; incomplete responses are not published as complete snapshots. The downloaded NASA calibration benchmark remains a development input fetched by tools/calibrate_planet_classifier.py, rather than a runtime dependency. Only generated model and audit artifacts are versioned alongside the condensed stellar archive.

The teleport regression exposed a display-type initialization defect: a planet with no mass or radius could retain the base object's stellar type after classification reported insufficient properties. Planet construction now assigns a planetary display type, and star-label drawing checks the actual object class before casting. A regression test covers planets and moons with absent measurements. All 66 planetary tests pass, and interactive startup checks successfully render Tau Ceti and 82 Eridani after teleporting.
