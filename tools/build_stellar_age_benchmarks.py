#!/usr/bin/env python3
"""Extract factual age benchmarks from published LEGACY, Gaia, and binary tables.

Inputs: VizieR J/ApJ/835/173 TSV; arXiv 1810.02829 and 2106.10752
source archives. Reference ages are labels only, never fitting observations.
All components of a binary or cluster share a validation partition.
"""

import argparse
import csv
import json
import math
from pathlib import Path
import re
import statistics
import tarfile


def archive_text(path, suffix):
    with tarfile.open(path) as archive:
        member = next(item for item in archive if item.name.endswith(suffix))
        return archive.extractfile(member).read().decode()


def tsv_tables(path):
    blocks = path.read_text().split("#Table")
    for block in blocks[1:]:
        lines = [line for line in block.splitlines()[1:] if line and not line.startswith("#")]
        yield list(csv.DictReader(lines[:1] + lines[3:], delimiter="\t"))


def clean(text):
    text = re.sub(r"\\tnote\{.*?\}", "", text)
    text = re.sub(r"\\(?:hline|cline\{.*?\})", "", text)
    return text.replace("$", "").strip()


def tex_rows(text):
    for raw in text.split(r"\\"):
        columns = [clean(item) for item in raw.strip().split("&")]
        if len(columns) > 3:
            yield columns


def numeric(text):
    values = re.findall(r"[-+]?\d+(?:\.\d+)?", clean(text))
    return float(values[0]) if values else None


def measurement(text):
    parts = re.split(r"\\pm", clean(text))
    return numeric(parts[0]), numeric(parts[1]) if len(parts) > 1 else None


def bolometric_correction(temperature):
    x = math.log10(temperature)
    coefficients = ([381.476328422343, -4212.78819301717, 15514.4866764412, -19053.7291496456]
        if x < 3.70 else [-170.623810323864, 2617.24637119416, -15065.1486316025, 38567.2629965804, -37051.0203809015]
        if x < 3.90 else [78.8731721804990, -1705.87278406872, 14741.2923562646,
            -63623.3812100225, 137145.973583929, -118115.450538963])
    result = 0
    for coefficient in coefficients:
        result = result * x + coefficient
    return result


def build(args):
    rows = []
    atmosphere, models, _ = list(tsv_tables(args.legacy))
    atmosphere = {int(row["KIC"]): row for row in atmosphere}
    by_id = {}
    for row in models:
        by_id.setdefault(int(row["KIC"]), []).append(row)
    for kic, solutions in sorted(by_id.items()):
        observed = atmosphere[kic]
        median = lambda key: statistics.median(float(item[key]) for item in solutions if item[key].strip())
        age = median("Age")
        spread = statistics.pstdev(float(item["Age"]) for item in solutions)
        # Pipeline scatter augments the published within-pipeline statistical errors.
        lower = max(0, age - math.hypot(median("e_Age"), spread))
        upper = age + math.hypot(median("E_Age"), spread)
        system = "16_Cyg" if kic in (12069424, 12069449) else f"KIC_{kic}"
        rows.append(dict(name=f"KIC {kic}", system=system, category="seismic_dwarf_subgiant",
            partition="calibration" if kic % 2 == 0 else "validation",
            source="Silva_Aguirre_2017", method="individual_oscillation_frequencies",
            model_dependence="Seismic masses and luminosities share stellar-model information with ages",
            temperature_k=float(observed["Teff"]), temperature_error_k=float(observed["e_Teff"]),
            metallicity=float(observed["[Fe/H]"]), metallicity_error=float(observed["e_[Fe/H]"]),
            luminosity_solar=median("Lum"), luminosity_error_solar=max(median("e_Lum"), median("E_Lum")),
            mass_solar=median("Mass"), mass_error_solar=max(median("e_Mass"), median("E_Mass")),
            radius_solar=median("Rad"), spectral_type="",
            reference_age_gyr=age, lower_gyr=lower, upper_gyr=upper))
    gaia = archive_text(args.gaia, "gb_ages_arxiv.tex")
    tables = re.findall(r"\\begin\{tabular\}.*?\n(.*?)\\end\{tabular\}", gaia, re.S)
    atmospheric = next(table for table in tables if "$\\sigma(T_{\\mathrm{eff}})$" in table)
    ages = next(table for table in tables if "Rank" in table)
    labels = {}
    for columns in tex_rows(ages):
        if len(columns) == 6 and numeric(columns[1]) and numeric(columns[2]):
            labels[int(numeric(columns[1]))] = (numeric(columns[3]), numeric(columns[4]), columns[5])
    for columns in tex_rows(atmospheric):
        if len(columns) != 13 or not numeric(columns[1]) or not numeric(columns[2]):
            continue
        hd = int(numeric(columns[1]))
        lower, upper, quality = labels[hd]
        if lower is None and upper is None:
            continue
        temperature = numeric(columns[5])
        gravity = numeric(columns[7])
        absolute = numeric(columns[11]) + 5 * math.log10(numeric(columns[9]) / 100)
        system = "Alpha_Cen" if hd in (128620, 128621) else "61_Cyg" if hd in (201091, 201092) else f"HD_{hd}"
        rows.append(dict(name=f"HD {hd}", hd=hd, hip=int(numeric(columns[2])), system=system,
            category="metal_poor" if numeric(columns[3]) < -1 else "giant" if gravity < 3.3 else "nearby_dwarf_subgiant",
            partition="validation", source="Sahlholdt_2019", method="literature_synthesis",
            model_dependence="Mixed isochrone, seismic, activity, and rotation constraints; see source appendix",
            temperature_k=temperature, temperature_error_k=numeric(columns[6]),
            metallicity=numeric(columns[3]), metallicity_error=numeric(columns[4]),
            absolute_magnitude=absolute, spectral_type="G2V" if gravity >= 4.1 else "G2IV" if gravity >= 3.3 else "K0III",
            spectral_type_origin="Gravity proxy for phase grouping; not a measured spectral subtype",
            luminosity_solar=10 ** (.4 * (4.74 - absolute - bolometric_correction(temperature))),
            log_luminosity_error_dex=max(.05, 2 * numeric(columns[10]) / (numeric(columns[9]) * math.log(10))),
            reference_age_gyr=(lower + upper) / 2 if lower is not None and upper is not None else lower or upper,
            lower_gyr=lower, upper_gyr=upper, quality=quality))
    binary = archive_text(args.binaries, "tables_electronic_version.tex")
    # Cluster and companion ages quoted in del Burgo and Allende Prieto 2018, section 5.3.
    cluster_ages = {
        "M55": (11.5, 13.5), "NGC6362": (12, 13), "M4": (11, 12.2), "47Tuc": (9.7, 15.5),
        "NGC6791": (7.2, 9.5), "NGC188": (6, 6.4), "M67": (3.33, 3.59),
        "NGC7142": (2.5, 3.5), "NGC6819": (2.15, 2.61), "NGC6811": (.95, 1.37),
        "M7": (.17, .22), "Pleiades": (.107, .117), "ScoOB2": (.005, .015),
        "UpperSco": (.005, .010), "NGC2244": (.001, .006), "CM_Dra": (5, 12),
    }
    membership = {
        "M55V54": "M55", "NGC6362V40": "NGC6362", "NGC6362V41": "NGC6362",
        "M4V69": "M4", "M4V66": "M4", "M4V65": "M4", "47TucV69": "47Tuc",
        "V565Lyr": "NGC6791", "V568Lyr": "NGC6791", "V785Cep": "NGC188",
        "HATTR318007": "M67", "WOCS12009": "M67", "V375Cep": "NGC7142",
        "WOCS40007": "NGC6819", "KIC9777062": "NGC6811", "V906Sco": "M7",
        "V1229Tau": "Pleiades", "V760Sco": "ScoOB2", "EPIC203710387": "UpperSco",
        "VScoCTIO5": "UpperSco", "V578Mon": "NGC2244", "CMDra": "CM_Dra",
    }
    system = None
    previous = None
    component = 0
    for columns in tex_rows(binary):
        if len(columns) != 10 or numeric(columns[1]) is None or numeric(columns[2]) is None:
            continue
        if columns[0]:
            system, component = columns[0], 0
        if not system:
            continue
        component += 1
        normalized = re.sub(r"[^a-zA-Z0-9]", "", system)
        cluster = membership.get(normalized)
        if not cluster:
            previous = columns
            continue
        if r"\shortparallel" in columns[3] and previous:
            columns[3] = previous[3]
        previous = columns
        temperature, temperature_error = measurement(columns[2])
        mass, mass_error = measurement(columns[4])
        abundance, abundance_error = measurement(columns[3])
        log_luminosity, luminosity_error = measurement(columns[6])
        gravity = numeric(columns[5])
        if any(value is None for value in (mass, abundance, log_luminosity, gravity)):
            continue
        lower, upper = cluster_ages[cluster]
        spectral = columns[7].replace(" ", "")
        if not spectral:
            spectral = "G2V" if gravity >= 4 else "G2IV" if gravity >= 3.3 else "K0III"
        rows.append(dict(name=f"{system} {component}", system=cluster, category="cluster_binary",
            partition="validation", source="del_Burgo_2018", method="cluster_or_companion_age",
            model_dependence="Cluster ages remain model dependent; dynamical masses are independent of HR fits",
            temperature_k=temperature, temperature_error_k=temperature_error or .02 * temperature,
            metallicity=abundance, metallicity_error=abundance_error or .2,
            mass_solar=mass, mass_error_solar=mass_error or .05 * mass,
            radius_solar=numeric(columns[1]), spectral_type=spectral,
            luminosity_solar=10 ** log_luminosity, log_luminosity_error_dex=luminosity_error or .1,
            reference_age_gyr=(lower + upper) / 2, lower_gyr=lower, upper_gyr=upper,
            membership="Published association; assess peculiar or interacting components separately"))
    rows.append(dict(name="Sun", system="Sun", category="solar_anchor", partition="validation",
        source="Connelly_2012", method="meteoritic_chronology", model_dependence="Independent clock; Sun calibrates stellar models",
        temperature_k=5772, temperature_error_k=10, luminosity_solar=1, log_luminosity_error_dex=.01,
        metallicity=0, metallicity_error=.02, mass_solar=1, mass_error_solar=.001,
        spectral_type="G2V", reference_age_gyr=4.5673, lower_gyr=4.56, upper_gyr=4.58))
    rows.append(dict(name="Betelgeuse", hd=39801, hip=27989, system="Betelgeuse", category="supergiant",
        partition="validation", source="Dolan_2016", method="evolutionary_models_and_ejected_mass",
        model_dependence="Model dependent; alternative rotation, distance, and merger histories remain plausible",
        temperature_k=3500, temperature_error_k=200, luminosity_solar=10 ** 5.10,
        log_luminosity_error_dex=.22, metallicity=0, metallicity_error=.2, spectral_type="M2Iab",
        reference_age_gyr=.00825, lower_gyr=.008, upper_gyr=.0085))
    for name, temperature, radius, mass, age, lower, upper, spectrum, source in (
            ("Sirius B", 25369, .008098, 1.018, .242, .237, .247, "DA2", "Bond_2017_Sirius"),
            ("Procyon B", 7740, .01232, .592, 1.87, 1.74, 2.0, "DQZ", "Liebert_2013_Procyon")):
        rows.append(dict(name=name, system=name.split()[0], category="white_dwarf", partition="validation",
            source=source, method="companion_evolution_total_age", spectral_type=spectrum,
            observations_source=source if mass > 1 else "Bond_2015_Procyon",
            model_dependence="Total system age from companion models; white-dwarf cooling age is distinct",
            temperature_k=temperature, temperature_error_k=200 if temperature > 10000 else 50,
            radius_solar=radius, mass_solar=mass, mass_error_solar=.011 if mass > 1 else .006,
            luminosity_solar=radius ** 2 * (temperature / 5772) ** 4, log_luminosity_error_dex=.05,
            metallicity=0, metallicity_error=.2, reference_age_gyr=age, lower_gyr=lower, upper_gyr=upper))
    # A system is indivisible even when individual identifiers have different parity.
    partitions = {}
    for row in rows:
        row["partition"] = partitions.setdefault(row["system"], row["partition"])
    return dict(schema=1, selection="All LEGACY stars; Gaia stars with published bounds; binary components with external age comparisons",
        sources={"Silva_Aguirre_2017": "https://doi.org/10.3847/1538-4357/835/2/173",
            "Sahlholdt_2019": "https://doi.org/10.1093/mnras/sty2732",
            "del_Burgo_2018": "https://doi.org/10.1093/mnras/sty1371",
            "Connelly_2012": "https://doi.org/10.1126/science.1226919",
            "Dolan_2016": "https://doi.org/10.3847/0004-637X/819/1/7",
            "Bond_2017_Sirius": "https://doi.org/10.3847/1538-4357/aa6af8",
            "Liebert_2013_Procyon": "https://doi.org/10.1088/0004-637X/769/1/7",
            "Bond_2015_Procyon": "https://doi.org/10.1088/0004-637X/813/2/106"}, rows=rows)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--legacy", type=Path, required=True)
    parser.add_argument("--gaia", type=Path, required=True)
    parser.add_argument("--binaries", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("src/tests/data/stellar_age_benchmarks.json"))
    arguments = parser.parse_args()
    result = build(arguments)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"Wrote {len(result['rows'])} benchmark stars")
