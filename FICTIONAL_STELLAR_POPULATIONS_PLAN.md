# Fictional stellar populations from galaxy imagery

Status: initial version implemented on the existing `fakestars` branch on 2026-10-26; user visual review and performance testing remain. The original plan was written 2026-10-25.

## Intended experience

Choose a location in the Milky Way or a remote galaxy, preview a spherical neighborhood, and create fictional stars whose spatial density and population mix reflect the galaxy imagery. The initial radius is 1,000 light years. Offer a single main-sequence star at the exact center as a convenient destination, with other systems distributed around it. Its spectral-type slider spans F8V through K2V, with a randomized initial selection. Users can visit, rename, edit, save, and remove these stars through the existing application.

Every generated component is a real application object with `user_added=true`. Each system has its own position and system plane; its stars have equatorial planes and rotational periods; companions have actual orbital hierarchies and orbital planes. Existing visibility, label, navigation, and exo-constellation machinery remains authoritative. Generated properties are fictional estimates, never catalog measurements.

## Existing foundations and integration points

| Area | Current foundation | Planned integration |
| --- | --- | --- |
| Galaxy identity and geometry | `src/classes/galaxy.h`, `galaxy.cpp`, `catalogs/galaxy_properties.json` provide galaxy properties, orientation, reference-galaxy identity, internal image, and band data. | A common galaxy-local coordinate frame and image-to-position mapping shared by preview and generation. |
| Source images | `src/gputex.cpp` resolves face-on JPEG/PNG images and the reference galaxy's internal image; GPU uploads may downsample. | Sample the original decoded image through a CPU sampler, before GPU resizing, display gamma, brightness, red-light mode, or view transforms. |
| Object creation | `src/dialogs.cpp` creates stars and other objects through `append_cel`. | Stage a complete generation batch, then initialize and register stars through a shared creation path. |
| Stellar properties | `src/classes/star.h` supplies temperature, mass, radius, luminosity, and color estimation helpers. | Build coherent fictional stellar parameters using these helpers where appropriate, with a population model supplying the inputs. |
| Coordinates | `CelestialLocation` separates galaxy, system, and local coordinates. Its galaxy-center field uses millions of light years; other distances use meters. | Store the remote galaxy offset separately from local stellar and orbital offsets. Never flatten all positions into one enormous meter-valued vector. |
| Persistence | `Star::to_json`, `from_json`, and `Serialization` already save star properties and orbital links. | Serialize generated stars plus generation provenance and stable identities. Verify every supported save mode. |
| Constellations | `ExoConsGenerator::check_should_generate_for` and the existing generation entry points. | Verify new stars pass the existing eligibility checks and invalidate the relevant cached results when a batch changes. |
| Object capacity | `MAX_CELOBJS` is currently 524,288, shared by all objects. | Apply a batch budget and check remaining capacity, including companions, before insertion. |

Implemented: edited-only serialization includes `user_edited` or `user_added` objects. Generated stars set both flags initially; tests also verify a generated star remains saved when its edited flag is cleared.

## Choosing the center

Implemented UI: **Object > Populate Region…** in the galaxy context/menu flow, opening one dialog with a location preview and generation controls. It must also be reachable for the Milky Way without selecting its center in the sky.

1. **Remote galaxy image picker:** display its face-on image in a zoomable panel. Clicking places a crosshair. Show galactocentric radius, physical scale, height above the disk, and the outline of the proposed population sphere. Start at the midplane; a height field and side-view inset resolve the third coordinate.
2. **Milky Way band picker:** display the existing panoramic band. Clicking supplies galactic longitude and latitude, but not distance. A distance slider/numeric field and a top-down/side-view diagram locate the point along that ray. Show all permitted disk intersections rather than silently choosing the nearer one.
3. **Coordinate entry:** allow galactocentric cylindrical coordinates (radius, azimuth, height), with a clear origin and units. This supports precise authoring and locations outside the suggested habitable region.
4. **Suggest Location:** propose a point under the selected density model, preferring the advisory habitable region. Users can adjust it before generation. If a galaxy has no suitable image, offer an explicitly identified analytic model or explain that image-guided placement is unavailable.
5. **Later convenience:** picking directly on the rendered galaxy can open the same dialog. That click defines a ray, so the depth choice still belongs in the preview. It must not pretend to identify a unique 3-D point.

Display the radius, adjustable Star count, randomized F8V–K2V center-star slider, population identifier, random seed, cluster toggle, and Generate Preview / Add Stars / Cancel Preview controls. The initial version always creates a new single center star. Preview does not alter the universe. After insertion offer Visit Center, keep the destination bookmarked, and retain the existing individual-object editing and deletion controls. Remove Region is deferred to a future version. Alienorum has no undo feature.

## What pixels can and cannot determine

A face-on pixel represents integrated light along a column approximately normal to a galaxy's disk. A Milky Way band pixel represents light integrated along a ray from the reference observer inside the galaxy. These are different geometries and require different depth models.

Brightness supplies a relative luminosity-density constraint; it is not a direct star count. Color guides the proportions of stellar populations, rather than assigning that exact RGB value or temperature to every generated star. Dust, emission, foreground contamination, image processing, and different image passbands can alter both signals. Galaxy colors depend on several population and dust parameters, as described by [Conroy (2013)](https://arxiv.org/abs/1301.7095).

Treat this feature as reproducible fictional synthesis informed by imagery. Preserve the source image and the declared assumptions in provenance. Do not describe the result as a recovered star catalog.

### Source preparation

- Decode once and cache bounded CPU sampling data; never read pixels back from the framebuffer.
- Declare image transfer function and preprocessing. Convert known sRGB display imagery to linear RGB before computing luminance and color ratios; support other declared encodings rather than assuming all astronomical images are sRGB.
- Record image center, crop, axis direction, handedness, physical pixel scale, and whether its geometry is already deprojected. Reuse renderer conventions and test registration instead of applying inclination twice.
- Mask borders, annotations, foreground stars, and invalid pixels where supported. Smooth at an explicit physical scale so compression artifacts do not create artificial clusters.
- Save an image hash and calibration/model version. A different image or calibration changes future previews, never existing saved stars.

### Remote galaxy volume

Map a trial 3-D point into the galaxy's disk coordinates, then into the face-on image. Spread each pixel's column luminosity over a normalized vertical profile. A thin/thick disk mixture, optional bulge, and galaxy morphology provide the depth prior; do not extrude every bright pixel into a uniform infinite column.

The local luminosity density is proportional to the image surface brightness multiplied by the normalized depth profile. Restrict sampling to the selected sphere and the modeled galaxy boundary. For spheroids or irregular galaxies, use a declared suitable model rather than forcing disk geometry. All galaxies are supported in the first version, including spheroids and irregulars. Choose plausible morphology-dependent models and an analytic fallback when suitable imagery is absent.

### Milky Way volume

Use the reference image's original observer location and coordinate frame, independent of the current camera. For each direction, distribute its integrated light along distance using an analytic disk/bulge prior and, if supported, an approximate dust prescription. Normalize along that original ray so the volume model projects back to the source angular brightness.

Convert luminosity density to expected counts using the population's mean luminosity. When sampling rays, include the distance-squared volume factor and the pixel's solid angle. When sampling Cartesian volume directly, use the resulting local density without applying that factor twice. Panoramic rows have different solid angles; the longitude seam wraps continuously.

The panoramic image alone cannot locate a spiral arm or dark cloud in depth. The UI and provenance must identify the analytic distance distribution as an assumption. Prefer a shared underlying 3-D Milky Way model if an appropriately calibrated face-on model is available later.

## Generating the population

1. Establish a stable galaxy identity, region center, radius, model version, and deterministic random seed. Use independent random streams for positions, stellar properties, multiplicity, and orientations so a UI detail change cannot silently scramble unrelated results.
2. Construct density and population-mixture fields from the source image and selected depth model. Condition the population on the requested Star count, including companions, and use density-weighted volume sampling for locations; uniform spherical proposals use radius proportional to the cube root of a uniform random variable.
3. Sample an evolved, present-day population consistent with a mass distribution and broad age/metallicity assumptions. Blue areas increase the young, hot component; red areas can contain cool dwarfs and evolved giants. Do not equate redness exclusively with old age, or sample an initial mass function without stellar lifetimes.
4. Derive internally consistent spectral type, mass, radius, temperature, luminosity, colors, age/metallicity estimates, and rotation. Retain the app's metallicity functionality; synthetic metallicities have explicit fictional provenance.
5. Offer a central single star with a spectral-type slider from F8V through K2V and a randomized default. Give it the normal generated designation; do not add a default proper name. Users can rename it with Shift+E. Reusing an existing center star would mean selecting an already present star as the location and destination for a new surrounding population, without creating a second star at the same position or changing that existing star. This alternative is deferred to future work: the initial version always creates a new center star. Supporting an existing center also requires making room for existing objects throughout the 1,000-light-year region. Keep a newly generated anchor outside statistical population-validation totals.
6. Draw multiplicity by stellar population. Each multiple is one system with separately named physical stars; draw compatible masses, orbital sizes, eccentricities, phases, and stable nested hierarchy. Account for all companions in both the object budget and the light budget.
7. Give each system an independently sampled isotropic orientation: uniform node and uniform cosine of inclination, not uniform inclination. Draw stellar spin and companion-plane offsets from an explicit correlation model. The galaxy's disk plane is not automatically every system's plane.
8. Mark these orientations as fictional, with `known_poles=false` and no measured rotation-axis claim. Preserve any geometry locks used for authored synthetic planes without promoting them to measured status. They must not acquire Shift+X known-pole labels, however they should display N-panel pole coordinates. Verify updates and JSON saves/loads retain the assigned planes.
9. Validate all objects before insertion. Commit the batch on the application thread, update search/system/spatial indices and view caches, and trigger the existing exo-constellation refresh path. Cancellation or capacity failure must leave no partial batch.

No planet generation is required for the first version. Planet creation later uses the existing host-statistics synchronization. Galactic rotation, galaxy-scale Keplerian orbits, and galaxy evolution are separate extensions; initial populations remain fixed in the existing galaxy-relative spatial frame unless an established motion model can be reused safely.

## Naming, identity, and saving

Accepted visible designation: `FIC <galaxy-code> <region-code> <system-number>`, for example `FIC M31 237R 000001`. Components receive A, B, C and nested labels according to the existing companion conventions. A newly generated single center star gets system number 000001 and the normal generated designation; users can later assign a proper name with Shift+E.

- Use PGC or another stable existing galaxy key internally; use the key `MW` for the Milky Way. A change in the displayed galaxy name must not change star identity.
- Give regions and stars persistent unique identifiers independent of names, list order, and runtime `seqno`. Check naming collisions on creation and import; increasing the counter after deletion must not rename existing stars.
- Keep HD, HIP, Gaia, Bayer, and Flamsteed fields empty. Fictional identities must never masquerade as real catalog identifiers or merge with a real object through a name collision.
- Store batch/region identity, member index, galaxy identity, local position, random seed, source hash, generation parameters, model version, and fictional-property provenance. Confirm an existing general metadata field is suitable or add a versioned JSON extension.
- Serialize explicit star properties, system links, orientations, and orbital elements, not just a recipe. Saved universes must reopen identically even if the image is absent or the generator changes.
- Preserve renamed stars and edits. Remove Region is deferred to a future version, including decisions about retaining edited systems and user-created descendants.
- Keep generated populations out of `soles_alienorum` and all downloaded catalogs. They belong to the saved universe.

## Population size and performance

A sphere of radius 1,000 light years has roughly 4.19 billion cubic light years of volume. Even an illustrative density of only 0.001 stars per cubic light year would imply about 4.19 million stars, exceeding the current whole-application capacity before companions and existing objects are counted. Consequently, do not fill the entire sphere with a consistent density of stars; use a falloff function that keeps the majority of stars within 100 light years, and only the intrinsically brightest stars are allowed to approach that 1000 l.y. limit. Stars with apparent magnitudes greater than 6.5, as seen from the center point, should be pretty much absent past about 25 l.y. from that point. It is allowed to increase the maximum size of the global `cels` array.

Recommend a bounded, representative fictional population in the first version. The radius controls the region, while a clearly labeled detail/object budget controls how many individual stars are instantiated. Preserve brighter rare populations and a representative faint population under an explicit sampling rule; do not silently claim physical completeness or multiply each retained star's individual luminosity to represent omitted stars. Record the selection rule for reproducibility.

Include fictional star clusters similar to the Pleiades, Hyades, Beehive, etc.

The existing dim-star display culling should handle drawing, but it does not eliminate allocation, updates, searches, constellation generation, or JSON size. Begin with the accepted provisional default of 10,000 individual stars, including the central star and companions, exposed through an adjustable Star count control and capped by available capacity. Benchmark those paths before finalizing that default. Preview should report expected object count including companions, memory, and save size. A later spatial streaming architecture could offer complete large populations, but is a separate implementation decision rather than an assumed capability.

Leave the source galaxy texture intact initially. Adding resolved stars on top of integrated galaxy light can double-count light; quantify the visual effect and decide whether future local background subtraction is warranted. Such subtraction must be local to the populated volume and continuous across view changes.

## Encouraging habitable locations

Show a translucent advisory region and prefer it for Suggest Location; allow unrestricted placement. Explain the advice in a brief tooltip and show galactocentric radius/height. Do not infer habitability from pixel color alone or promise habitable planets.

One historical Milky Way model proposed a 7–9 kpc annulus, while other work questions a narrow fixed boundary: [Lineweaver, Fenner & Gibson (2004)](https://arxiv.org/abs/astro-ph/0401024), [Prantzos (2006 preprint)](https://arxiv.org/abs/astro-ph/0612316). Use this as an explicitly selectable model, not a universal measured ring. Do not copy those physical radii to every remote galaxy. Where the available morphology, scale, and population information cannot support a habitable-zone model, display a heuristic preference with its assumptions or mark the advice unavailable.

## Historical proposals and developer decisions

| Question | Recommended starting point | Remaining uncertainty | Developer notes |
| --- | --- | --- | --- |
| First UI and entry point? | One Populate Region dialog with image picking, depth controls, and coordinate entry. | Menu placement, direct sky-picking priority, and default navigation after insertion. | Try it, let me see how it looks. |
| Scope of supported galaxies? | Milky Way plus disk galaxies with registered face-on imagery. | Include spheroids/irregulars immediately, and permit analytic fallback when imagery is absent? | ALL galaxies are to be supported. |
| Population completeness? | A clearly identified bounded representative population. | Desired default/maximum object budget and whether full faint-star completeness is a requirement. | See my edits in that section. |
| Brightness calibration? | Relative image weighting plus a documented density normalization. | Per-galaxy normalization, handling display-stretched or false-color images, and acceptable dust assumptions. | Just use something plausible; we're inventing stuff so we have some leeway in how we do it. |
| Depth model? | Explicit disk/bulge priors and a Milky Way distance control. | Approved scale heights, arm structure, near/far disk choices, and model sources. | Again, choose soething plausible. |
| Stellar population model? | A small documented set of age/metallicity mixtures with a coherent present-day mass distribution. | Source tables, permitted spectral/evolutionary classes, binary fraction, and parameter calibration. | Make it look like a cross between the Gliese and BSC catalogs. |
| Center star? | Optional single Sunlike anchor, enabled initially. | Exact solar twin versus a range, reuse of an existing center star, and default proper name. | Give the user a slider from F8V to K2V and randomize the default value. Edit this document to clarify what "reuse of an existing center star" means. Leave out default proper name for now; user can always Shift+E it. |
| Names and overlapping regions? | Stable fictional prefix and batch identifiers; do not alter real catalog stars. | Preferred display syntax, minimum spacing, whether overlapping batches are allowed, and whether deterministic spatial cells should prevent duplicate generated systems. | For now, let regions overlap unobstructed. We can try some stress testing later when the basic functionality is in place. |
| Orientation correlations? | Independent isotropic system planes with physically plausible within-system correlations. | How closely to align spins and companion planes, and how fictional orientation provenance appears in the editor. | Like the real world. Multiple comanions can orbit on similar (Gliese 22, Eps Ind) or dissimilar (Alp Cen, HD12447) planes. |
| Galactic habitability advice? | Advisory selectable model, editable center, unrestricted placement. | Initial Milky Way model and remote-galaxy heuristics; no universal fixed annulus. | Pick something plausible. For the Milky Way, choose whichever paper makes a better argument. |
| Background light and motion? | Retain existing imagery; fixed galaxy-relative positions initially. | Is resolved-light subtraction or galactic motion required in the first release? | No, keep it simple to start with and we'll reevaluate it later. |
| Save size and batch lifecycle? | Explicit objects plus provenance and cancellable preview. | The provisional default is 10,000 stars; Remove Region and its descendant policy are deferred. | Alienorum has no undo feature. Please edit this doc to clarify the other items? |

The developer notes above are accepted direction. Plausible calibration, depth, habitability, and orientation parameters are delegated implementation choices, rather than additional approval questions. Preserve both aligned and misaligned companion configurations; use nearby-star and bright-star catalog populations as complementary guides. All galaxies are supported, overlapping regions are allowed, and background subtraction and galactic motion are deferred.

### Settled discussion items

1. **Existing center star — settled, deferred:** The initial version creates a new center star. Future work may allow populating around an existing star, accounting for existing objects throughout the 1,000-light-year radius rather than generating a competing population over them. This added complexity is excluded from the initial version.
2. **Generated designations — settled:** Use `FIC <galaxy-code> <region-code> <system-number>`, for example `FIC M31 237R 000042`, with existing companion suffix conventions. Region codes distinguish generation batches, including overlapping regions. Keep fictional identities separate from real catalog identifiers and allow user renaming with Shift+E. Accepted on 2026-10-26.
3. **Default population budget — settled for prototyping:** Offer an adjustable Star count control with a provisional default of 10,000 individual stars, counting the central star and all companions. Cap generation by available object capacity. Apply the accepted distance/brightness falloff when selecting the population. Evaluate appearance and performance before finalizing the default. Accepted on 2026-10-26.
4. **Batch removal — settled, deferred:** Remove Region is excluded from the initial version. Its handling of renamed or edited stars and later-added planets, moons, or companions will be decided when that feature is developed. Deferred on 2026-10-26.

Menu placement can be proposed in the UI prototype under the instruction to try the interface and review its appearance. All listed discussion items are now settled or explicitly deferred; implementation parameters delegated by the developer remain engineering choices.

## Implementation sequence and verification

1. Settle the table above - apply the developer notes and ask about the remaining items, clarifying what each one is. Audit representative source images and coordinate conventions; establish image scale/orientation metadata and population calibration.
2. Implement pure CPU image sampling and galaxy-coordinate transforms. Test face-on registration, mirrored/rotated images, Milky Way ray depth, seam/poles, solid-angle weighting, zero/invalid pixels, and precision in remote galaxies.
3. Implement deterministic population generation without global insertion. Test density ratios, blue/red population shifts, depth distributions, mass/luminosity consistency, companion stability, isotropic planes, capacity limits, and reproducibility.
4. Add stable identities and versioned persistence. Test save/reload in normal and edited-only modes, duplicate imports, renamed components, missing images, preserved fictional flags, and preservation of edited systems and their descendants.
5. Add the dialog and previews, cancellation, staged insertion, and navigation. Test that cancel/failure has no universe side effects and that adding a batch updates indices and invalidates stale constellation results.
6. Verify actual visits to both a distant Milky Way region and a remote-galaxy region: companion orbits remain precise, local sky stars appear, exo-constellations form using the existing code, and current magnitude/label filters still apply. Shift+X must exclude unmeasured generated poles.
7. Benchmark construction time, frame time near and far from the region, constellation generation, memory, and JSON save/reload at several budgets. Existing draw culling remains enabled throughout.
8. Add acceptance criteria for the approved design and implementation results. If new non-generated runtime model tables or image metadata are added, synchronize CMake and Windows packaging; developer-only tools and this plan do not belong in the installer.

Implementation is committed in the same branch. The following handoff records the shipped initial behavior and its verification limits.

## Future work

- Remove Region, including a policy for edited systems and later-added descendants. Deferred on 2026-10-26.
- Populate around an existing star, preserving its properties and accounting for existing objects throughout the 1,000-light-year generation radius. Deferred by decision on 2026-10-26; the initial version creates a new center star.

## Initial-version handoff — 2026-10-26

### How to try it

1. Restart the built application and press **Ctrl+P** or open **Object > Populate Region…**. The galaxy being visited or containing the current location is selected by default. A selected galaxy, then the Milky Way, provides a fallback when no current galaxy is available. The searchable galaxy list includes every loaded galaxy.
2. For a remote galaxy, click its image or enter galactocentric radius, azimuth, and height. For the Milky Way, choose a direction on the band and adjust distance from the Sun; the coordinate fields remain synchronized. Image zoom and top/side diagrams assist placement. Galaxies without an image use an explicitly described analytic model.
3. Use **Suggest Location** for an advisory habitable location. Default advice uses a broad morphology-scaled region, following Prantzos's caution about a uniquely bounded annulus. The Milky Way also offers the historical 7–9 kpc annulus. These are fictional placement guides, not measured habitability maps.
4. Start with **10,000 stars**, a **1,000-light-year radius**, the randomized **F8V–K2V center slider**, and open clusters enabled. The count includes the center and all companion stars. Change the seed for a different reproducible draw. Minimum system spacing ranges from 1 to 5 light years, default 2; companion orbits remain within their own system.
5. Choose **Generate Preview**. It reports stars, systems, clusters, local and naked-eye counts, elapsed time, and approximate storage, with a population diagram. It does not alter the universe. **Cancel Preview** stops generation without inserting partial objects.
6. Choose **Add Stars**, then **Visit Center**. The center is bookmarked in Favorite Stars. Save the universe through the existing interface; normal automatic universe saving also includes the generated stars. Rename or edit them with the existing controls.
7. Visit a generated companion, check its orbit and N-panel fictional pole coordinates, switch label modes, and inspect the new sky and automatically generated exo-constellations. Shift+X continues to exclude synthetic poles. Save, reload, and revisit the bookmarked center.

### Implementation choices

- `src/classes/fictionalstars.cpp` provides deterministic image sampling, galaxy frames, morphology/depth priors, field and cluster populations, companion generation, staging, validation, and insertion. `src/fictionalstars_ui.cpp` supplies the dialog and preview workflow. No new runtime data assets were added.
- CPU source fields average linearized original pixels at bounded resolution while preserving aspect ratio. External galaxies use the renderer's original solar-view UV conventions. The Milky Way uses the existing band rotation, its actual ±30-degree latitude span, a fixed original solar observer, and numerical line-of-sight normalization of an analytic disk prior. Cartesian sampling avoids applying a distance-squared volume weight twice.
- Relative image brightness controls placement density; image color guides population age and young/old placement. Existing main-sequence tables support a nearby-dwarf population with a bright-star tail, plus evolved giants and white dwarfs. Multiplicity varies with primary population. Clusters have shared ages and compact spatial distributions. This is a plausible fictional mixture inspired by nearby-star and bright-star catalogs, not a fit that reconstructs individual real stars.
- Most represented stars are local to the center. Distant represented stars satisfy the center's magnitude-6.5 selection; faint stars are retained locally within about 25 light years. Only luminous stars reach the outer region. The explicit count is a representative population, not a claim of physical completeness.
- Galaxy coordinates and local offsets remain separate. Random system planes are isotropic, with both correlated and misaligned spin/companion orientations. `known_poles` remains false. Synthetic iron abundances and model uncertainties remain available to existing metallicity-informed planet classification.
- Stable fictional IDs and original galaxy identities are independent of visible star names. Visible sectors use three digits from 2357 and a consonant from BCDJLMNPQRVW, for example 237R. Opaque persistent batch IDs are stored separately. Bounded galaxy codes keep the complete designation and component suffix within existing 40-byte storage. Regions may overlap; generation does not reconcile their objects or existing catalog stars.
- Universe JSON contains explicit properties, positions, planes, orbits, stable identities, generation controls, image hashes, and favorite state. Reload does not require images or regeneration. Fictional stars are excluded from condensed catalog writers. Import rejects duplicate fictional identities and restores companion memberships and favorites without duplication.
- Preview memory and JSON estimates are approximate; actual saved size is recorded in the benchmark below. Existing magnitude and label filters remain active. Synthetic visibility bounds use intrinsic luminosity so a galaxy-relative coordinate distance cannot incorrectly hide locally visible stars.

### Verification and remaining review

Acceptance criteria **F01–F15** document the implementation. Dedicated checks cover deterministic output, counts, falloff, clusters, companions, isotropic planes, source colors, frame transforms, long names, cancellation, capacity, invalid staged physics, overlapping regions, archive isolation, stable imports, fresh reloads, bookmarks, visibility, and existing constellation generation. UI tests exercise image and analytic pickers, Generate Preview, Add Stars at the default budget, and Visit Center in an isolated OpenGL context.

User review still determines the preferred appearance, population density, cluster prominence, center-placement interface, and interactive frame rate. Windows installer execution is outside this session's verification; the CMake sources include the generator and dialog, and the runtime payload has no added assets.

The broad regression run also exposes existing test debt: five cold-atmosphere expectations conflict with condensation code already disabled before this feature; the Alpha Mensae constellation test reports reused Ori/Tau abbreviations; and catalog photometry reports 48 pre-existing discrepancies. A separately built pre-feature revision, `78aa3e9`, reproduces those results with the same catalogs. Its isolated photometry test additionally crashes during teardown after reporting the matching discrepancies. These unrelated behaviors were left unchanged.

Final verification: Linux Make and CMake builds pass; all 17 dedicated fictional-region tests and 68 enabled rendering tests pass, with six optional GPU checks skipped. In the synthetic default-budget fixture, generation of 10,000 stars in 6,840 systems took 0.324 seconds; the actual JSON was 62.51 MiB, saving took 1.58 seconds, and reload took 5.09 seconds. These are fixture measurements on this computer rather than full-universe interactive FPS. Acceptance criteria F01–F15 and their verification entry record the completed initial-version handoff.

### Corrections after initial visual testing — 2026-10-26

- Ctrl+P invokes the population dialog. Current-galaxy selection takes precedence over an unrelated selected galaxy, including visits to M31 and its generated stars.
- New visible sector codes have four restricted characters; persistent identity remains separate. Existing in-memory sectors reserve their codes, and imported identity/name conflicts are rejected rather than merging unrelated objects.
- New systems have an adjustable 1–5 light-year minimum separation, default 2. A spatial index includes the center and cluster members. Faint stars may occupy the full local 25-light-year volume rather than being packed into their tiny naked-eye visibility radius. Placement failures resample or report an impossible population; they never place a system onto an occupied point. This applies within each new batch, retaining the previously accepted freedom to overlap separate regions.
- Free flight preserves the observer's physical location when changing system frames. The galaxy offset is not added twice. Static visible stars can become the current system. A moving observer entering a star stops there without clearing the universe.
- Previously saved names and locations remain intact. Generate a fresh region to assess the new codes and spacing. Reopen the application to run the corrected executable.

Verification: all 20 fictional-region checks, all 71 enabled rendering checks (six optional GPU skips), and 11 housekeeping checks pass. Acceptance criteria F16–F19 cover these corrections. In the updated default-budget fixture, 10,000 stars in 6,812 systems generated in 0.472 seconds; actual JSON was 63.43 MiB, save took 1.61 seconds, and reload took 4.93 seconds. These remain fixture measurements rather than full-universe frame-rate guarantees.
