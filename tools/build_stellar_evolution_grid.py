#!/usr/bin/env python3
"""Compress the public MIST isochrones into a runtime HR-diagram fitting grid."""

import argparse
import bisect
import math
from pathlib import Path
import re
import tarfile
import urllib.request


URL = "https://mist.science/data/tarballs_v1.2/MIST_v1.2_vvcrit0.0_basic_isos.txz"
COOLING_URL = "https://www.astro.umontreal.ca/~bergeron/CoolingModels/CoolingModels/AllSequences.tar.gz"


def append_cooling(output, archive):
    # Phase 9 is Wolf-Rayet in MIST. Remnant cooling models have separate phase codes.
    original = output.read_text().splitlines()
    lifetimes = {}
    retained = []
    for line in original:
        if line.startswith("# Montreal") or line.startswith("# WD"):
            continue
        if line.startswith("#"):
            retained.append(line)
            continue
        fields = line.split()
        phase = int(fields[1])
        if phase >= 100:
            continue
        retained.append(line)
        if abs(float(fields[0])) < .01 and phase == 2:
            key = round(math.log10(float(fields[5])) / .01) * .01
            lifetimes[key] = min(lifetimes.get(key, math.inf), float(fields[6]))
    masses = sorted(lifetimes)

    def progenitor(initial):
        coordinate = math.log10(max(.8, initial))
        right = bisect.bisect_left(masses, coordinate)
        if right == 0 or right == len(masses):
            return 1e10 * max(.8, initial) ** -2.5
        left = right - 1
        fraction = (coordinate - masses[left]) / (masses[right] - masses[left])
        return 1.15 * 10 ** (lifetimes[masses[left]] * (1 - fraction) + lifetimes[masses[right]] * fraction)

    cooling = []
    with tarfile.open(archive) as models:
        for member in models:
            match = re.fullmatch(r"seq_(\d+)_(thick|thin)\.txt", Path(member.name).name)
            if not match:
                continue
            mass = int(match[1]) / 100
            phase = 101 if match[2] == "thick" else 102
            initial = max(.8, (mass - .394) / .109)
            for raw in models.extractfile(member):
                fields = raw.split()
                if len(fields) != 6 or not fields[0].isdigit():
                    continue
                temperature, age, luminosity = float(fields[1]), float(fields[4]), float(fields[5])
                if age > 13.8e9:
                    continue
                if mass < .5:
                    # Stripped low-mass remnants have an unconstrained binary history.
                    total, lower, upper = (age + 13.8e9) / 2, max(1, age), 13.8e9
                    initial = 1.5
                else:
                    total = age + progenitor(initial)
                    lower = .8 * age + .8 * progenitor(max(.8, (mass + .05 - .394) / .109))
                    upper = 1.2 * age + 1.25 * progenitor(max(.8, (mass - .05 - .394) / .109))
                if not 0 < total <= 13.8e9:
                    continue
                lower = max(1, min(lower, total))
                upper = max(total, min(13.8e9, upper))
                cooling.append(f"0.00 {phase} {math.log10(temperature):.6f} "
                    f"{math.log10(luminosity / 3.828e33):.6f} {mass:.6f} {initial:.6f} "
                    f"{math.log10(total):.6f} {math.log10(lower):.6f} {math.log10(upper):.6f}")
    temporary = Path(str(output) + ".tmp")
    with temporary.open("w") as stream:
        stream.write("\n".join(retained) + "\n")
        stream.write("# Montreal cooling: https://www.astro.umontreal.ca/~bergeron/CoolingModels/\n")
        stream.write("# WD references: Bedard et al. (2020), ApJ 901, 93; Kalirai et al. (2008), arXiv:0706.3894\n")
        stream.write("# WD phases 101/102: thick/thin H layers; Kalirai 2008 initial-final mass prior\n")
        stream.write("\n".join(cooling) + "\n")
    temporary.replace(output)
    print(f"Appended {len(cooling)} white-dwarf cooling grid points")


def build(archive, output):
    cells = {}
    with tarfile.open(archive, "r|xz") as models:
        for member in models:
            if not member.name.endswith(".iso"):
                continue
            match = re.search(r"feh_([mp])(\d+\.\d+)", member.name)
            metallicity = float(match[2]) * (-1 if match[1] == "m" else 1)
            isochrones = {}
            for raw in models.extractfile(member):
                if raw.startswith(b"#") or not raw.strip():
                    continue
                fields = raw.split()
                age, initial, mass = map(float, fields[1:4])
                luminosity, temperature = float(fields[7]), float(fields[10])
                phase = int(float(fields[24]))
                if age > math.log10(13.8e9) or mass <= 0:
                    continue
                if not all(map(math.isfinite, (age, initial, mass, luminosity, temperature))):
                    continue
                isochrones.setdefault(age, []).append((initial, mass, luminosity, temperature, phase))
            ages = sorted(isochrones)
            for age_index, age in enumerate(ages):
                previous = ages[age_index - 1] if age_index else age
                following = ages[age_index + 1] if age_index + 1 < len(ages) else age
                time_width = (10 ** ((age + following) / 2) - 10 ** ((age + previous) / 2)) / 1e9
                rows = sorted(isochrones[age])
                for index, (initial, mass, luminosity, temperature, phase) in enumerate(rows):
                    left = rows[index - 1][0] if index else initial
                    right = rows[index + 1][0] if index + 1 < len(rows) else initial
                    weight = .5 * max(0, right - left) * time_width * initial ** -2.35
                    if weight <= 0:
                        continue
                    key = (metallicity, phase, round(temperature / .0125),
                           round(luminosity / .075), round(age / .15))
                    values = (temperature, luminosity, mass, initial, age)
                    if key not in cells:
                        cells[key] = [weight, *(weight * value for value in values), age, age]
                    else:
                        cell = cells[key]
                        cell[0] += weight
                        for i, value in enumerate(values, 1):
                            cell[i] += weight * value
                        cell[6], cell[7] = min(cell[6], age), max(cell[7], age)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w") as stream:
        stream.write("# MIST 1.2 nonrotating; https://doi.org/10.3847/0004-637X/823/2/102\n")
        stream.write("# Grid revision 2: Salpeter IMF integrated over initial-mass and linear-age cells\n")
        stream.write("# [Fe/H] phase logTeff logL current_mass initial_mass logAgeYr lower_logAge upper_logAge lnPopulationWeight\n")
        for key, values in sorted(cells.items()):
            count, temperature, luminosity, mass, initial, age, lower, upper = values
            stream.write(f"{key[0]:.2f} {key[1]} " + " ".join(
                f"{value / count:.6f}" for value in (temperature, luminosity, mass, initial, age))
                + f" {lower:.6f} {upper:.6f} {math.log(count):.8f}\n")
    print(f"Wrote {len(cells)} evolutionary grid cells to {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=Path("/tmp/alienorum-mist-basic.txz"))
    parser.add_argument("--output", type=Path, default=Path("catalogs/stellar_evolution.dat"))
    parser.add_argument("--download", action="store_true")
    parser.add_argument("--cooling-archive", type=Path, default=Path("/tmp/alienorum-wd-cooling.tar.gz"))
    parser.add_argument("--cooling-only", action="store_true")
    parser.add_argument("--retain-cooling", type=Path,
                        help="Copy cooling rows from an existing grid without rebuilding them")
    args = parser.parse_args()
    if args.download:
        urllib.request.urlretrieve(URL, args.archive)
        urllib.request.urlretrieve(COOLING_URL, args.cooling_archive)
    cooling_rows = []
    if args.retain_cooling:
        for line in args.retain_cooling.read_text().splitlines():
            if line and not line.startswith("#") and int(line.split()[1]) >= 100:
                cooling_rows.append(line)
        if not cooling_rows:
            raise ValueError("The supplied grid contains no cooling rows")
    if not args.cooling_only:
        build(args.archive, args.output)
    if args.retain_cooling:
        with args.output.open("a") as stream:
            stream.write("# Montreal cooling rows retained; Bedard 2020 and Kalirai 2008\n")
            stream.write("# Montreal cooling: https://www.astro.umontreal.ca/~bergeron/CoolingModels/\n")
            stream.write("# WD references: Bedard et al. (2020), ApJ 901, 93; Kalirai et al. (2008), arXiv:0706.3894\n")
            stream.write("\n".join(cooling_rows) + "\n")
    else:
        append_cooling(args.output, args.cooling_archive)
