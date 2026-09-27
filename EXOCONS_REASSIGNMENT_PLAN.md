# Plan: Constellation Star Reassignment Near Lines (Exocons)

## 1. Executive Summary & Problem Description
When viewing stars from an exoplanet system (e.g., Tau Ceti), constellations are generated algorithmically. In the current implementation:
1. **Stars near lines are not properly incorporated**: Stars that are physically adjacent to a constellation line in the sky projection are frequently bypassed rather than absorbed into the passing line.
2. **Spurious long-distance bridges**: Instead of joining the constellation whose line passes right next to them, these stars remain tethered to distant namesake constellations (or draw long lines across the sky, e.g., Hercules reaching across space to grab Altair in Aquila).
3. **Incomplete divorce/reassignment**: When a passerby line reroutes or attempts to absorb an impinging star, existing divorce safeguards prevent removing the old lines if an endpoint has low degree, leaving ugly dual-constellation networks and cross-sky spiderwebs.
4. **Target Behavior (`00001_preferred.png`)**: Stars close to a constellation line are cleanly reassigned to that constellation; the passing line reroutes through the star; old long-distance lines to former constellations are cleanly excised.
5. **Vantage Line Filtering (< 10 l.y.)**: When viewing from a star that does not have its own lines but is within 10 l.y. of one that does, use **only the single nearest set of lines** (e.g., from Alpha Centauri, show only heliocentric lines, never those of Tau Ceti or other systems).
6. **Teleportation Delay on 'O' Key (~3 seconds)**: Pressing 'O' to jump to another star system causes a ~3-second freeze because constellation generation runs synchronously on the main UI/render thread. This latency must be eliminated through algorithmic optimization, background threading, or both.

---

## 2. Visual & Topological Comparison

Based on differential analysis of `behavior/00001_current.png` vs `behavior/00001_preferred.png` (from vantage point **Tau Ceti**):

### Region 1: Hercules / Aquila Border (Upper Left, near Altair)
- **Current (`00001_current.png`)**:
  - Stars `Alp1Her` (Ras Algethi) and `95 Her` lie immediately adjacent to the passing boundary line of the adjacent constellation.
  - Instead of being integrated into this passing line, two long diagonal lines connect `Alp1Her` and `95 Her` all the way down to `Alp Aql` (Altair) across empty sky.
- **Preferred (`00001_preferred.png`)**:
  - The long diagonal lines across to Altair (`Alp Aql, Alp1Her` and `Alp Aql, 95 Her`) are **completely eliminated**.
  - The adjacent constellation line bends and reroutes directly through the star, cleanly incorporating it.

### Region 2: Scutum / Ophiuchus Border (Below Scutum)
- **Current (`00001_current.png`)**:
  - A horizontal bridge line (`Nu Oph, Zet Sct`) stretches across from the Ophiuchus cluster to Scutum, creating an unnatural connection between distinct groups.
- **Preferred (`00001_preferred.png`)**:
  - The horizontal bridge line `Nu Oph, Zet Sct` is **removed**.
  - `Nu Oph` connects vertically downward to its natural neighbor `Mu Oph`, keeping the constellation shape localized and compact.

### Region 3: Sagittarius / Capricornus Border (Bottom Right)
- **Current (`00001_current.png`)**:
  - A star near a passing constellation line has a single dead-end spur extending into space without joining the passing constellation line.
- **Preferred (`00001_preferred.png`)**:
  - The line continues through the star and joins smoothly into the adjacent constellation line network.

---

## 3. Root Cause Analysis

1. **Premature Long-Distance Linking in Phase 4 Expansion / Step 4**:
   - In Step 4 (intra- and inter-constellation line formation), stars belonging to the same catalog constellation (e.g., Hercules) try to connect to each other or to bright stars (like Altair) across distances up to 15 degrees before proximity to passerby lines is considered.
2. **Phase 4 Impinging Rerouting Constraints**:
   - In Phase 4 (lines 2083–2530), `point_near_arc` checks if a star `sp` impinges on line `(sa, sb)`. However:
     - `min_end_dist_deg` (0.8°) rejects stars close to vertices.
     - `degrees[sp] < 5` allows rerouting, but `is_segment_valid` may reject rerouting if `(sa, sp)` or `(sp, sb)` intersects or nears another segment.
3. **Overly Restrictive Divorce Logic (Lines 2404–2502)**:
   - When an impinging star `sp` is reassigned to the passerby constellation `c_abbrev`, the algorithm tries to remove its old lines (`old_lines_to_remove`).
   - However, lines 2441–2450 reject divorce if *any* neighbor `s_other` has `degrees[s_other] <= 1`:
     ```cpp
     if (degrees[s_other] <= 1)
     {
         safe = false;
         break;
     }
     ```
   - Furthermore, lines 2481–2485 reject divorce if the remaining subcomponent in `old_cons` has `<= 2` stars.
   - **Result**: If Hercules only reached into this region with 1 or 2 stars connected to Altair, `safe` evaluates to `false`. The old lines to Altair are kept, causing the star to remain bound to Hercules or preventing the reroute entirely!
4. **Independent Vantage Filtering in `src/visuals.cpp:4688-4725`**:
   - The line and label rendering loops currently evaluate `if (vantdist < light_year * 10)` independently per constellation without grouping by vantage point.
   - If multiple distinct vantages exist within 10 l.y. (e.g. Earth at 4.37 ly and Tau Ceti or another system also within 10 ly of an intermediate observer), constellations from *multiple vantages* can be drawn simultaneously or erroneously mixed.
5. **Synchronous Execution on Teleport (`src/classes/exocons.cpp:2816-2830`)**:
   - When teleporting via 'O' or CLI, `start_generation_for` calls `generate_constellations(sys_star, pending_conss)` synchronously directly from `update_frame()`.
   - `generate_constellations` analyzes 131,000+ stars, calculates sky coverage, performs candidate expansion, and runs multiple rerouting passes on the main GUI thread, blocking rendering for ~3 seconds.

---

## 4. Implementation Plan

When tokens refresh, implement the changes through the following focused phases:

### Phase A: Reassignment & Proximity Prioritization
1. **Star-to-Line Proximity Identification**:
   - Detect stars within threshold distance (e.g. $\le 2.0^\circ$) of a candidate line segment early or during line generation.
   - If a star is within proximity of an existing line, bias its assignment to that line's constellation rather than allowing distant catalog namesake connections.

### Phase B: Unblock Old Line Divorcement in Passerby Rerouting
1. **Refine Divorce Safety Checks (`exocons.cpp:2441-2488`)**:
   - Allow divorce of `sp` even if `degrees[s_other] == 1`, provided that `s_other`'s orphan line is also pruned or redirected (especially when `s_other` is an inter-constellation reach like Altair).
   - If the old connection was a cross-constellation reach across $> 8^\circ$ while the new line is local ($< 3^\circ$), aggressively favor severing the long-distance link.
   - Prevent constellations from retaining cross-sky bridge lines when local rerouting occurs.

### Phase C: Prohibit Spurious Cross-Constellation Bridges
1. **Enforce Stricter Penalties on Inter-Constellation Lines**:
   - In intra/inter constellation line formation, increase the distance and magnitude penalties for connecting stars across different IAU constellation regions when local alternatives exist.
   - Specifically prevent situations like `Nu Oph, Zet Sct` bridging separate visual figures.

### Phase D: Nearest Vantage Selection Within 10 Light Years
1. **Single Nearest Vantage Determination (`src/visuals.cpp`)**:
   - Before the constellation line and label loops, determine the single active vantage:
     1. If the current star has its own constellations defined (own vantage name or distance $< 0.1$ l.y.), use its own set.
     2. Otherwise, find the vantage star among all defined constellations with the minimum distance to the current observer location.
     3. If that minimum distance is $\le 10.0$ l.y., lock onto that single nearest vantage only.
     4. If no defined vantage is $\le 10.0$ l.y., do not display constellation lines.
   - Render lines and labels exclusively for constellations belonging to this single chosen vantage.
   - Ensures that from Alpha Centauri (or any star $< 10$ l.y. from Sol without its own lines), only heliocentric constellations are rendered, never mixing with Tau Ceti or any other star's lines.

### Phase E: Eliminate Teleport Latency (Algorithmic Speedup & Background Threading)
1. **Algorithmic Efficiency**:
   - Pre-filter star candidates by magnitude limit and spatial bounding to avoid iterating over 131,000+ stars in multiple stages.
   - Spatial indexing (e.g. spherical grid or k-d tree) for line-intersection and candidate-nearness queries to reduce $O(N^2)$ checks in the rerouting and expansion passes.
2. **Asynchronous Background Generation**:
   - Offload `generate_constellations` from the main thread into a detached worker thread (`std::thread` / `std::async`).
   - Snapshot necessary star coordinates and observer vantage to ensure thread safety without racing the render loop.
   - Use an atomic flag or mutex-protected queue to feed completed constellations into `pending_conss`, allowing the main thread to remain at 60 fps with zero teleport stutter.
   - As constellations complete in the background, `update_frame()` seamlessly pops them into the live sky view as originally envisioned ("the user can see the local constellations form").

### Phase F: Validation & Regression Testing
1. **Visual & Performance Verification**:
   - Run the simulator from `Tau Ceti` (`JD2461311.157169`, looking toward Scutum/Aquila) and confirm Regions 1, 2, and 3 match `00001_preferred.png`.
   - Teleport to `Alpha Centauri`: confirm only heliocentric lines are visible, with no lines or labels from Tau Ceti.
   - Measure frame latency when pressing 'O': verify framerate does not stutter and UI remains fully responsive.
2. **Automated Unit Tests (`src/tests/cons_test.cpp`)**:
   - Add targeted test case: `ExoConsTest.TauCetiNearLinesAndReassignment` (verifies divorce of `Alp Aql` - `Alp1Her`/`95 Her`, integration into local line, removal of `Nu Oph` - `Zet Sct`).
   - Add test case: `ExoConsTest.NearestVantageSelectionAlpCen` (verifies that from Alpha Centauri, only Sol's vantage lines are selected and no other sets are active).
   - Add benchmark test measuring `generate_constellations` execution duration.
   - Run existing suite (`bin/cons_test`) to verify all 50+ constellation requirements, non-intersection constraints, and sky coverage thresholds pass.
