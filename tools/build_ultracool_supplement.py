"""Convert the published UltracoolSheet CSV tables into the runtime supplement."""

import argparse
import collections
import csv
import json
import math
import re
import statistics
from pathlib import Path


def text(value):
    value = str(value or "").strip()
    return "" if value.lower() in ("null", "nan", "none") else value


def number(value):
    try:
        result = float(text(value).rstrip("?"))
        return result if math.isfinite(result) else None
    except ValueError:
        return None


def rows(folder, name):
    with (folder / (name + ".csv")).open(newline="", encoding="utf-8-sig") as source:
        return list(csv.DictReader(source))


def aliases(row):
    values = [row.get(key, "") for key in ("name", "name_simbadable", "name_simbad", "designation_2mass", "designation_WISE", "designation_MKO")]
    values += row.get("identifiers_simbad", "").split("|")
    return list(dict.fromkeys(re.sub(r"^(?:NAME|\*\*|V\*)\s+", "", text(value)) for value in values if text(value)))


def spectrum(row):
    return text(row.get("spt_ir")) or text(row.get("spt_opt"))


def spectral_temperature(value):
    match = re.search(r"([MLTY])(\d+(?:\.\d+)?)?", value, re.IGNORECASE)
    if not match:
        return 1500.0
    kind, subtype = match[1].upper(), float(match[2] or 5)
    if kind == "M":
        return max(2300.0, 3850 - 160 * subtype)
    if kind == "L":
        return max(1200.0, 2300 - 100 * subtype)
    if kind == "T":
        return max(450.0, 1300 - 85 * subtype)
    return max(200.0, 450 - 50 * subtype)


def spectral_mass(value):
    match = re.search(r"M(\d+(?:\.\d+)?)", value)
    return max(.055, .105 - .005 * float(match[1])) if match else .04


def coordinate_name(name, ra, dec, precision=4):
    if len(name) <= 22 and not re.search(r"J\d{6}", name):
        return name
    prefix = re.split(r"\s*J\d", name)[0].strip() or "UCS"
    if len(prefix) > 10:
        prefix = "UCS"
    hours = ra / 15
    if precision == 4:
        position = f"{int(hours):02d}{int(hours * 60) % 60:02d}{'+' if dec >= 0 else '-'}{int(abs(dec)):02d}{int(abs(dec) * 60) % 60:02d}"
    else:
        position = f"{int(hours):02d}{int(hours * 60) % 60:02d}{int(hours * 3600) % 60:02d}{'+' if dec >= 0 else '-'}{int(abs(dec)):02d}{int(abs(dec) * 60) % 60:02d}{int(abs(dec) * 3600) % 60:02d}"
    suffix = re.search(r"[0-9]\s*([ABCDabcd]{1,2})$", name)
    return prefix + " J" + position + (" " + suffix[1] if suffix else "")


def build(folder):
    main = rows(folder, "Main_-_In_Progress")
    binaries = {text(row["name"]): row for row in rows(folder, "Binaries")}
    triples = {text(row["name"]): row for row in rows(folder, "Triples+")}
    physical = {text(row["name"]): row for row in rows(folder, "FundamentalProperties")}
    references = {row["code_ref"]: row["ADSkey_ref"] for row in rows(folder, "References")}
    records = []
    # The unfinished rows are explicitly identified as Pleiades members by the release notes.
    cluster = [number(row["dist_formula"]) for row in main if "PLE" in row["age_category"].upper() or "pleiades" in row.get("note", "").lower()]
    cluster = [value for value in cluster if value and 100 < value < 180]
    for row in main:
        original = text(row["name"])
        ra, dec = number(row["ra_j2000_formula"]), number(row["dec_j2000_formula"])
        record = dict(name=original, aliases=aliases(row), spectrum=spectrum(row), ra=ra, dec=dec, epoch=2000.0,
                      hierarchy="UCS:" + original, source="UltracoolSheet 2.1.0")
        parameters = dict(catalog="UltracoolSheet 2.1.0", original_name=original,
                          reference="https://doi.org/10.5281/zenodo.15802304")
        for target, source in [("parallax", "plx_formula"), ("pmra", "pmra_formula"), ("pmdec", "pmdec_formula"), ("radial_velocity", "rv_formula"), ("gmag", "G_Gaia"), ("distance_pc", "dist_formula")]:
            value = number(row.get(source))
            if value is not None:
                record[target] = value
        if record.get("parallax", 0) <= 0:
            record["distance_estimated"] = True
            parameters["distance_method"] = text(row.get("dist_formula_source"))
        if not record.get("distance_pc") and text(row.get("UCS_done")) != "yes":
            record["distance_pc"] = statistics.median(cluster) if cluster else 136.2
            parameters["distance_method"] = "Pleiades membership; cluster-distance estimate"
            parameters["distance_reference"] = "Melis et al. 2014, doi:10.1126/science.1256101"
        details = physical.get(original, {})
        for target, source, factor in [("temperature", "teff_evo", 1), ("radius_jupiter", "radius_evo", 1), ("mass_solar", "mass_evo", 1 / 1047.3486)]:
            value = number(details.get(source))
            if value is not None and value > 0:
                record[target] = value * factor
                parameters[target + "_estimated"] = True
        parameters["physical_model"] = text(details.get("evo_model_name"))
        for key in ("ref_discovery", "ref_plx_formula", "ref_pm_formula", "ref_rv_formula", "ref_spt_ir", "ref_spt_opt"):
            codes = text(row.get(key))
            if codes:
                parameters[key] = [references.get(code.strip(), code.strip()) for code in codes.split(";")]
        if "mass_solar" not in record:
            record["mass_solar"] = spectral_mass(record["spectrum"])
            parameters["mass_solar_estimated"] = True
            parameters["mass_method"] = "Broad spectral-type scenario, not a mass measurement"
        record["parameters"] = parameters
        if text(row.get("multiplesystem_resolved_in_this_table")) not in ("", "N"):
            record["position_from_system_member"] = True
        preferred = next((alias for alias in record["aliases"] if re.match(r"(?:Luhman|Ross|Wolf|Gl |GJ |LHS |LP |VB |TRAPPIST|Teegarden)", alias) and not re.search(r"J\d", alias)), original)
        short = coordinate_name(preferred, ra, dec)
        if original in triples:
            table = triples[original]
            labels = ["A", "B", "C"] if original == "LHS 1070" else ["A", "Ba", "Bb"]
            if text(table.get("spt_4")) or number(table.get("sep_41")) is not None:
                labels.append("D")
            spectra = [text(table.get("spt_" + str(index + 1))) for index in range(len(labels))]
        elif original in binaries:
            table = binaries[original]
            designation = text(table.get("designation_binary"))
            labels = [designation[0] + "a", designation[0] + "b"] if designation in ("Bab", "Aab") else list(designation)
            if len(labels) != 2:
                labels = ["A", "B"]
            resolved = text(row.get("multiplesystem_resolved_in_this_table"))
            if designation == "AB" and resolved not in ("", "N"):
                if original.endswith("A") or "Aa{comma}Ab" in row.get("identifiers_simbad", ""):
                    labels = ["Aa", "Ab"]
            spectra = [text(table.get("spt_pri")), text(table.get("spt_sec"))]
        else:
            labels, spectra = [""], [record["spectrum"]]
        for index, label in enumerate(labels):
            item = json.loads(json.dumps(record))
            if label:
                def component_name(base):
                    designation = text(table.get("designation_binary"))
                    if designation and base.endswith(designation):
                        base = base[:-len(designation)].rstrip()
                    elif designation in ("Bab", "Aab") and base.endswith(designation[0]):
                        base = base[:-1].rstrip()
                    elif labels == ["Aa", "Ab"] and original.endswith("A") and base.endswith("A"):
                        base = base[:-1].rstrip()
                    return base + " " + label
                item["aliases"] = list(dict.fromkeys(component_name(alias) for alias in record["aliases"]))
                if index == 0:
                    item["aliases"] += record["aliases"]
                item["name"] = component_name(short)
                item["hierarchy"] += "." + label
                item["position_from_system_member"] = True
                item["parameters"]["component"] = label
                item["parameters"]["spectrum_estimated"] = not bool(spectra[index])
                item["spectrum"] = spectra[index] or record["spectrum"].split("+")[0]
                # Integrated photometry/model fits describe the unresolved system, not either component.
                for key in ("gmag", "temperature", "radius_jupiter"):
                    item.pop(key, None)
                item["mass_solar"] = spectral_mass(item["spectrum"])
                item["parameters"]["mass_method"] = "Component spectral-type scenario, not a mass measurement"
            else:
                item["name"] = short
            if "temperature" not in item:
                item["temperature"] = spectral_temperature(item["spectrum"])
                item["parameters"]["temperature_estimated"] = True
                item["parameters"]["temperature_method"] = "Spectral subtype scenario" if item["spectrum"] else "Broad ultracool-object scenario; subtype absent"
            item["parameters"]["spectral_classification"] = item["spectrum"]
            if len(item["spectrum"].encode("utf-8")) > 15:
                item["spectrum"] = item["spectrum"].split()[0]
            item["aliases"] = list(dict.fromkeys([item["name"]] + item["aliases"]))
            records.append(item)
    # Retain estimates supplied by the sheet. Fill genuinely absent distances only from photometry.
    source_rows = {text(row["name"]): row for row in main}
    for item in records:
        if item.get("distance_pc", 0) > 0 or item.get("parallax", 0) > 0:
            continue
        row = source_rows[item["parameters"]["original_name"]]
        for band in ("W2", "J_MKO", "J_2MASS", "K_MKO", "i_P1", "z_P1"):
            magnitude = number(row.get(band))
            if magnitude is None:
                continue
            index = number(row.get("sptnum_formula"))
            samples = []
            for other in main:
                other_index, distance, apparent = number(other.get("sptnum_formula")), number(other.get("dist_formula")), number(other.get(band))
                if index is not None and other_index is not None and abs(index - other_index) <= 2 and distance and distance > 0 and apparent is not None:
                    samples.append(apparent - 5 * math.log10(distance / 10))
            if len(samples) >= 3:
                item["distance_pc"] = 10 ** ((magnitude - statistics.median(samples) + 5) / 5)
                item["parameters"]["distance_method"] = band + " median absolute magnitude of supplied spectral neighbors"
                item["distance_estimated"] = True
                break
    used = collections.Counter(item["name"] for item in records)
    for item in records:
        if used[item["name"]] > 1:
            previous = item["name"]
            item["aliases"] = [alias for alias in item["aliases"] if alias != previous]
            base = coordinate_name(item["parameters"]["original_name"], item["ra"], item["dec"], 6)
            item["name"] = base + (" " + item["parameters"]["component"] if "component" in item["parameters"] else "")
            item["aliases"].insert(0, item["name"])
    return dict(source="UltracoolSheet 2.1.0; Best, Dupuy, Liu, Sanghi, Siverd and Zhang", doi="10.5281/zenodo.15802304", records=records)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--output", type=Path, default=Path("catalogs/ultracool_dwarfs.json"))
    arguments = parser.parse_args()
    catalog = build(arguments.folder)
    arguments.output.write_text(json.dumps(catalog, ensure_ascii=True, indent=2, allow_nan=False) + "\n")
    print("Records:", len(catalog["records"]))
    print("Without a distance:", sum(not (item.get("distance_pc", 0) > 0 or item.get("parallax", 0) > 0) for item in catalog["records"]))
