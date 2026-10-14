#!/usr/bin/env python3
"""Independent, read-only audit of the fixed-column condensed stellar catalog."""
import argparse
from collections import Counter
from dataclasses import dataclass, field
import gzip
import hashlib
import json
import math
from pathlib import Path
import re
import sys

# One-based inclusive columns, kept independent of the C++ reader and writer.
FIELDS = dict(
    id=(1, 14), name=(16, 54), ra=(56, 65), dec=(67, 75),
    magnitude=(77, 82), spectral=(84, 98), bv=(100, 105), ub=(107, 112),
    hd=(114, 119), hip=(121, 126), hr=(128, 132), sb9=(134, 140),
    flamsteed=(142, 145), bayer=(146, 152), gliese=(154, 168),
    gould=(170, 172), gould_cons=(174, 176), pm_ra=(178, 188),
    pm_dec=(190, 200), velocity=(202, 212), parallax=(214, 223),
    distance=(224, 235), absolute=(238, 243), mass=(245, 254),
    radius=(257, 266), temperature=(268, 274), rotation=(276, 285),
    parent=(287, 300), period=(302, 312), axis=(315, 326),
    eccentricity=(328, 334), periapsis=(341, 348), anomaly=(350, 357),
    orbit_epoch=(359, 372), inclination=(374, 381), node=(383, 390),
    variability=(392, 403), bright=(405, 410), faint=(412, 417),
    bright_epoch=(419, 435), eclipsing=(437, 437), dm_survey=(439, 440),
    dm_zone=(442, 445), dm_number=(447, 452), custom=(454, 454),
)
INTEGER_FIELDS = frozenset(('hd', 'hip', 'hr', 'sb9', 'flamsteed', 'gould', 'dm_zone', 'dm_number'))
NUMERIC_FIELDS = INTEGER_FIELDS | frozenset((
    'magnitude', 'bv', 'ub', 'pm_ra', 'pm_dec', 'velocity', 'parallax',
    'distance', 'absolute', 'mass', 'radius', 'temperature', 'rotation',
    'period', 'axis', 'eccentricity', 'periapsis', 'anomaly', 'orbit_epoch',
    'inclination', 'node', 'variability', 'bright', 'faint', 'bright_epoch',
))
NUMBER = re.compile(r'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][+-]?\d+)?\Z')
CONSTELLATIONS = dict(zip(
    'And Ant Aps Aqr Aql Ara Ari Aur Boo Cae Cam Cnc CVn CMa CMi Cap Car Cas Cen Cep Cet Cha Cir Col Com CrA CrB Crv Crt Cru Cyg Del Dor Dra Equ Eri For Gem Gru Her Hor Hya Hyi Ind Lac Leo LMi Lep Lib Lup Lyn Lyr Men Mic Mon Mus Nor Oct Oph Ori Pav Peg Per Phe Pic Psc PsA Pup Pyx Ret Sge Sgr Sco Scl Sct Ser Sex Tau Tel Tri TrA Tuc UMa UMi Vel Vir Vol Vul'.split(),
    'Andromedae|Antliae|Apodis|Aquarii|Aquilae|Arae|Arietis|Aurigae|Bootis|Caeli|Camelopardalis|Cancri|Canum Venaticorum|Canis Majoris|Canis Minoris|Capricorni|Carinae|Cassiopeiae|Centauri|Cephei|Ceti|Chamaeleontis|Circini|Columbae|Comae Berenices|Coronae Australis|Coronae Borealis|Corvi|Crateris|Crucis|Cygni|Delphini|Doradus|Draconis|Equulei|Eridani|Fornacis|Geminorum|Gruis|Herculis|Horologii|Hydrae|Hydri|Indi|Lacertae|Leonis|Leonis Minoris|Leporis|Librae|Lupi|Lyncis|Lyrae|Mensae|Microscopii|Monocerotis|Muscae|Normae|Octantis|Ophiuchi|Orionis|Pavonis|Pegasi|Persei|Phoenicis|Pictoris|Piscium|Piscis Austrini|Puppis|Pyxidis|Reticuli|Sagittae|Sagittarii|Scorpii|Sculptoris|Scuti|Serpentis|Sextantis|Tauri|Telescopii|Trianguli|Trianguli Australis|Tucanae|Ursae Majoris|Ursae Minoris|Velorum|Virginis|Volantis|Vulpeculae'.split('|'),
))
# The historical spelling in this catalog is accepted alongside the standard one.
GENITIVE_ALIASES = dict(CMa=('Canes Majoris',), CMi=('Canes Minoris',))
GREEK = tuple('Alpha Beta Gamma Delta Epsilon Zeta Eta Theta Iota Kappa Lambda Mu Nu Xi Omicron Pi Rho Sigma Tau Upsilon Phi Chi Psi Omega'.split())
ABBREVIATIONS = tuple('Alp Bet Gam Del Eps Zet Eta The Iot Kap Lam Mu Nu Xi Omi Pi Rho Sig Tau Ups Phi Chi Psi Ome'.split())
BAYER = re.compile(r'(Alp|Bet|Gam|Del|Eps|Zet|Eta|The|Iot|Kap|Lam|Mu|Nu|Xi|Omi|Pi|Rho|Sig|Tau|Ups|Phi|Chi|Psi|Ome)\s*([1-9]?)\s*([A-Za-z]{3})\Z')
ID = re.compile(r'(?P<bin>-?\d{1,2}[a-z]|V[a-z])\s+(?P<cons>[A-Za-z]{3})(?:\s+(?P<sequence>[1-9]\d*))?(?:\s+(?P<component>[A-Z]))?\Z')
SPECTRAL = re.compile(r'(?:e?sd)?(?:[OBAFGKMLTYRNSC]|D[A-Z]?|W[NCOR]?|[cp]?[OBAFGKM])', re.I)
LY_PER_PC = 3.2615637771674336
RAD_PER_ARCSEC = math.pi / (180.0 * 3600.0)


@dataclass
class Issue:
    severity: str
    code: str
    line: int
    identity: str
    detail: str
    related_line: int = 0


@dataclass
class Report:
    issues: list = field(default_factory=list)
    rows: int = 0
    sources: list = field(default_factory=list)
    digest: str = ''

    def add(self, code, record, detail, severity='error', related=None):
        self.issues.append(Issue(severity, code, record.line, record.identity or record.name,
                                 detail, related.line if related else 0))

    @property
    def errors(self):
        return sum(issue.severity == 'error' for issue in self.issues)

    def counts(self):
        return Counter((issue.severity, issue.code) for issue in self.issues)

    def as_dict(self):
        return dict(rows=self.rows, sha256=self.digest, source_catalogs=self.sources,
                    errors=self.errors, warnings=len(self.issues) - self.errors,
                    issues=[vars(issue) for issue in self.issues])


@dataclass
class Record:
    raw: bytes
    line: int
    identity: str = ''
    name: str = ''
    numbers: dict = field(default_factory=dict)
    ra: float = 0.0
    dec: float = 0.0
    component: str = ''
    constellation: str = ''
    root: str = ''

    def text(self, name):
        start, end = FIELDS[name]
        return self.raw[start - 1:end].decode('utf-8', errors='replace').strip()

    def number(self, name):
        return self.numbers.get(name)


def parse_record(raw, line, report):
    raw = raw.rstrip(b'\r\n')
    row = Record(raw, line)
    row.identity, row.name = row.text('id'), row.text('name')
    if len(raw) != 455:
        report.add('row_width', row, f'Expected 455 bytes, got {len(raw)}')
    if any(c < 32 or c == 127 for c in raw):
        report.add('control_character', row, 'Control character inside a catalog row')
    try:
        raw.decode('utf-8')
    except UnicodeDecodeError:
        report.add('encoding', row, 'Invalid UTF-8')
    occupied = set()
    for start, end in FIELDS.values():
        occupied.update(range(start - 1, end))
    if any(c != 32 for index, c in enumerate(raw) if index not in occupied):
        report.add('separator', row, 'Data spilled into a separator or reserved column')
    if not row.name:
        report.add('empty_name', row, 'Every row must have a display name')
    for name in sorted(NUMERIC_FIELDS):
        value = row.text(name)
        if not value:
            row.numbers[name] = None
            continue
        if not NUMBER.fullmatch(value) or (name in INTEGER_FIELDS and not re.fullmatch(r'[+-]?\d+', value)):
            report.add('numeric_syntax', row, f'{name}: {value!r} is not a complete number')
            row.numbers[name] = None
            continue
        number = int(value) if name in INTEGER_FIELDS else float(value)
        row.numbers[name] = number
        if not math.isfinite(number):
            report.add('nonfinite', row, f'{name} is not finite')
            row.numbers[name] = None
    ra, dec = row.text('ra'), row.text('dec')
    match_ra = re.fullmatch(r'(\d{2}):(\d{2}):(\d{2}\.\d)', ra)
    match_dec = re.fullmatch(r'([+-])(\d{2}):(\d{2}):(\d{2})', dec)
    if not match_ra or any(float(v) >= limit for v, limit in zip(match_ra.groups(), (24, 60, 60))):
        report.add('coordinates', row, f'Invalid right ascension {ra!r}')
    else:
        hh, mm, ss = map(float, match_ra.groups())
        row.ra = (hh + mm / 60 + ss / 3600) * 15
    if not match_dec:
        report.add('coordinates', row, f'Invalid declination {dec!r}')
    else:
        sign, dd, mm, ss = match_dec.groups()
        if int(dd) > 90 or int(mm) >= 60 or int(ss) >= 60 or (int(dd) == 90 and (int(mm) or int(ss))):
            report.add('coordinates', row, f'Invalid declination {dec!r}')
        row.dec = (int(dd) + int(mm) / 60 + int(ss) / 3600) * (-1 if sign == '-' else 1)
    if row.identity == '-26':
        row.root = row.identity
        if row.name not in ('Sun', 'Sol'):
            report.add('solar_identity', row, 'The solar row has an unexpected name')
    elif row.identity:
        identity = ID.fullmatch(row.identity)
        if not identity or identity['cons'] not in CONSTELLATIONS:
            report.add('alienorum_id', row, f'Invalid ID {row.identity!r}')
        else:
            row.component = identity['component'] or ''
            row.constellation = identity['cons']
            row.root = row.identity.rsplit(' ', 1)[0] if row.component else row.identity
    elif row.text('parent'):
        report.add('missing_component_id', row, 'An orbiting component has no stable ID')
    for name, limits in dict(hd=(1, 359083), hip=(1, 120416), hr=(1, 9110), sb9=(1, 9999999),
                             flamsteed=(1, 999), gould=(1, 999), dm_number=(1, 999999)).items():
        value = row.number(name)
        if value is not None and not limits[0] <= value <= limits[1]:
            report.add('identifier_range', row, f'{name}={value} is outside {limits}')
    for name, limits in dict(magnitude=(-40, 99), absolute=(-40, 40), bv=(-5, 10), ub=(-5, 10),
                             eccentricity=(0, 1), inclination=(0, 180),
                             periapsis=(-360, 360), anomaly=(-360, 360), node=(-360, 360)).items():
        value = row.number(name)
        if value is not None and not limits[0] <= value <= limits[1]:
            report.add('value_range', row, f'{name}={value} is outside {limits}')
    if row.number('eccentricity') == 1:
        report.add('unbound_orbit', row, 'A stellar component cannot have a parabolic bound orbit')
    for name in ('distance', 'mass', 'radius', 'temperature', 'rotation', 'period', 'axis', 'variability'):
        value = row.number(name)
        if value is not None and value < 0:
            report.add('negative_physical_value', row, f'{name}={value}')
        if value is not None and value > 1e15:
            report.add('physical_overflow', row, f'{name}={value} exceeds the generous physical envelope')
    for name, choices in dict(eclipsing=('', 'E'), custom=('', 'Y'), dm_survey=('', 'BD', 'CD', 'CP')).items():
        if row.text(name) not in choices:
            report.add('flag', row, f'Invalid {name} flag {row.text(name)!r}')
    zone = row.number('dm_zone')
    if row.text('dm_survey'):
        if zone is None or abs(zone) > 90 or not re.fullmatch(r'[+-]\d{2}', row.text('dm_zone')) or not row.number('dm_number'):
            report.add('durchmusterung', row, 'Incomplete or malformed Durchmusterung identity')
    elif zone is not None or row.number('dm_number') is not None:
        report.add('durchmusterung', row, 'Durchmusterung values have no survey prefix')
    return row


def parse_bayer(value):
    match = BAYER.fullmatch(value)
    if not match or match[3] not in CONSTELLATIONS:
        return None
    return ABBREVIATIONS.index(match[1]), match[2], match[3]


def greek_name(value):
    normalized = ' '.join(value.split())
    for index, letter in enumerate(GREEK):
        if normalized.startswith(letter + ' '):
            rest = normalized[len(letter) + 1:]
            number = ''
            if rest.split()[0].isdigit():
                number, _, rest = rest.partition(' ')
            component = ''
            if rest.rsplit(' ', 1)[-1] in tuple('ABCDEFGHIJKLMNOPQRSTUVWXYZ'):
                rest, component = rest.rsplit(' ', 1)
            for cons, genitive in CONSTELLATIONS.items():
                if rest in (genitive,) + GENITIVE_ALIASES.get(cons, ()):
                    return index, number, cons, component
    return None


def check_photometry(row, report):
    m, absolute, distance = (row.number(key) for key in ('magnitude', 'absolute', 'distance'))
    parallax = row.number('parallax')
    if distance is not None and distance > 0 and parallax and parallax > 0:
        expected = LY_PER_PC * RAD_PER_ARCSEC / parallax
        if abs(distance - expected) / max(expected, distance) > 0.002:
            report.add('distance_parallax', row, f'Distance {distance:g} ly conflicts with parallax ({expected:g} ly)', 'warning')
    if row.identity != '-26' and distance is not None and distance > 0 and m is not None and absolute is not None:
        expected = m - 5 * math.log10(distance / LY_PER_PC) + 5
        if abs(expected - absolute) > 0.15:
            report.add('distance_modulus', row, f'Absolute magnitude {absolute:g}; distance modulus gives {expected:.3f}', 'warning')
    bright, faint = row.number('bright'), row.number('faint')
    if bright is not None and faint is not None and bright > faint:
        report.add('variability_bounds', row, f'Bright magnitude {bright} exceeds faint magnitude {faint}')
    if row.number('variability') and bright is None and faint is None:
        report.add('variable_missing_range', row, 'Variable period without a magnitude range', 'warning')
    if m is not None and bright is not None and faint is not None and not bright - 0.5 <= m <= faint + 0.5:
        report.add('variable_magnitude', row, 'Apparent magnitude lies outside its variability range', 'warning')
    spectral = row.text('spectral').replace(' ', '')
    if spectral and not SPECTRAL.match(spectral):
        report.add('spectral_syntax', row, f'Unrecognized spectral prefix {spectral!r}', 'warning')
    bv = row.number('bv')
    if spectral and bv is not None and '+' not in spectral and not any(marker in spectral.lower() for marker in ('pec', 'sd', 'p:', 'e:')):
        if (spectral[0] in 'OB' and bv > 1.5) or (spectral[0] in 'MLT' and bv < 0.0):
            report.add('spectral_color', row, f'{spectral} with B-V={bv:g}; review extinction, blends, or classification', 'warning')
    if bv is not None and not -0.6 <= bv <= 6:
        report.add('color_outlier', row, f'Unusual B-V={bv:g}', 'warning')
    velocity = row.number('velocity')
    if velocity is not None and abs(velocity) >= 299792458:
        report.add('radial_velocity', row, 'Radial velocity reaches or exceeds light speed')
    if row.number('temperature') and row.number('temperature') > 1000000:
        report.add('temperature_outlier', row, 'Temperature exceeds 1,000,000 K', 'warning')


def related(a, b):
    return bool(a.root and a.root == b.root)


def check_relationships(records, report):
    identities, raw_rows = dict(), dict()
    for row in records:
        if row.identity:
            if row.identity in identities:
                report.add('duplicate_id', row, f'ID already belongs to {identities[row.identity].name}', related=identities[row.identity])
            else:
                identities[row.identity] = row
        if row.raw in raw_rows:
            report.add('duplicate_row', row, 'Identical catalog row repeated', related=raw_rows[row.raw])
        else:
            raw_rows[row.raw] = row
    # Iterative traversal handles long chains without recursion limits.
    for row in records:
        current, chain, seen = row, [], set()
        while current.text('parent'):
            parent_id = current.text('parent')
            if parent_id not in identities:
                report.add('missing_parent', row, f'Parent {parent_id!r} is absent')
                break
            if parent_id == row.identity or parent_id in seen:
                report.add('orbit_cycle', row, f'Orbit hierarchy cycles at {parent_id!r}')
                break
            seen.add(parent_id)
            chain.append(current)
            current = identities[parent_id]
        else:
            for member in chain:
                member.root = current.identity or current.name
        parent = identities.get(row.text('parent'))
        if parent and row.identity and not row.component:
            report.add('component_suffix', row, 'Orbiting star has no component letter in its ID')
        if row.component and not parent:
            report.add('orphan_component', row, 'Component letter without a resolved parent; review optical or unresolved membership', 'warning')
        if parent and row.component and row.component == parent.component:
            report.add('component_identity', row, 'Child and parent use the same component letter')
        if parent and all(value is not None and 0 < value <= 1e15 for value in (row.number('axis'), row.number('period'), row.number('mass'), parent.number('mass'))):
            years = row.number('period') / 365.25
            expected = math.sqrt(row.number('axis') ** 3 / (row.number('mass') + parent.number('mass')))
            ratio = years / expected if expected else 0
            if not 0.25 <= ratio <= 4:
                report.add('orbit_kepler', row, f'Orbital period differs from Kepler estimate by factor {ratio:g}', 'warning')
    for key in ('hd', 'hip', 'hr', 'sb9', 'bayer', 'gliese', 'gould', 'dm', 'name'):
        owners = dict()
        for row in records:
            if key in ('hd', 'hip', 'hr', 'sb9'):
                value = row.number(key)
            elif key == 'gould':
                value = (row.number('gould'), row.text('gould_cons')) if row.number('gould') else None
            elif key == 'dm':
                value = (row.text('dm_survey'), row.text('dm_zone'), row.number('dm_number')) if row.text('dm_survey') else None
            else:
                value = ' '.join((row.name if key == 'name' else row.text(key)).split())
            if not value or (key == 'gliese' and value in ('Sun', 'Sol')):
                continue
            first = owners.get(value)
            if first:
                # HD/HIP/HR and DM can designate an unresolved multiple. SB9
                # indexes systems, so sharing it alone never establishes a duplicate.
                if key == 'sb9' or related(first, row):
                    if key in ('hd', 'hip', 'hr'):
                        report.add('shared_system_identifier', row, f'{key}={value} also identifies a linked component', 'warning', first)
                else:
                    severity = 'warning' if key in ('dm', 'name', 'gould', 'bayer') else 'error'
                    report.add('duplicate_' + key, row, f'{key}={value!r} is shared by unrelated rows', severity, first)
            else:
                owners[value] = row
    positions = dict()
    for row in records:
        position = (row.text('ra'), row.text('dec'))
        if position in positions and row.identity != '-26':
            first = positions[position]
            if not related(first, row):
                report.add('coincident_stars', row, 'Same rounded coordinates in unrelated systems; possible duplicate or unresolved pair', 'warning', first)
        else:
            positions[position] = row
    for row in records:
        designation = parse_bayer(row.text('bayer')) if row.text('bayer') else None
        if row.text('bayer') and designation is None:
            report.add('bayer_syntax', row, f'Invalid Bayer designation {row.text("bayer")!r}')
        if row.number('gould') and row.text('gould_cons') not in CONSTELLATIONS:
            report.add('gould_constellation', row, 'Gould number has no valid constellation')
        explicit = greek_name(row.name)
        if explicit:
            letter, number, cons, component = explicit
            if designation and (letter, number, cons) != designation:
                report.add('bayer_name', row, f'Display name conflicts with {row.text("bayer")!r}')
            elif not designation:
                parent = identities.get(row.text('parent'))
                inherited = greek_name(parent.name) if parent else None
                if not inherited or explicit[:3] != inherited[:3]:
                    report.add('invented_bayer', row, 'Greek display name without its own or its primary\'s Bayer identity')
            if component and row.component and component != row.component:
                report.add('name_component', row, 'Display-name component conflicts with Alienorum ID')
        if re.fullmatch(r'HD\s*\d+(?:\s+[A-Z])?', row.name):
            hd = int(re.search(r'\d+', row.name)[0])
            parent = identities.get(row.text('parent'))
            if hd != row.number('hd') and not (parent and hd == parent.number('hd')):
                report.add('hd_name', row, 'HD display name conflicts with its own or primary identifier')
        if re.fullmatch(r'HIP\s*\d+(?:\s+[A-Z])?', row.name):
            hip = int(re.search(r'\d+', row.name)[0])
            parent = identities.get(row.text('parent'))
            if hip != row.number('hip') and not (parent and hip == parent.number('hip')):
                report.add('hip_name', row, 'HIP display name conflicts with its own or primary identifier')
        if re.match(r'GJ\s*\d', row.name) and row.text('gliese') and ' '.join(row.name.split()) != ' '.join(row.text('gliese').split()):
            report.add('gliese_name', row, 'GJ display name differs from its catalog identity', 'warning')
    return identities


def check_sources(records, report, directory):
    directory = Path(directory)
    bsc_path = directory / 'BSC' / 'catalog'
    if bsc_path.exists():
        report.sources.append(str(bsc_path))
        source = dict()
        for raw in bsc_path.read_text().splitlines():
            if raw[:4].strip().isdigit():
                source[int(raw[:4])] = raw
        for row in records:
            raw = source.get(row.number('hr'))
            if not raw:
                continue
            hd = raw[25:31].strip()
            if hd.isdigit() and row.number('hd') and int(hd) != row.number('hd'):
                report.add('bsc_cross_id', row, f'HR {row.number("hr")} belongs to HD {hd} in BSC')
            source_bayer = raw[7:11].strip() + raw[11:14].strip()
            expected = parse_bayer(source_bayer) if raw[7:11].strip() else None
            actual = parse_bayer(row.text('bayer')) if row.text('bayer') else None
            if expected != actual and not row.component:
                report.add('bsc_bayer', row, f'Bayer identity conflicts with BSC: {source_bayer!r}')
            source_flam = raw[4:7].strip()
            if source_flam.isdigit() and row.number('flamsteed') != int(source_flam) and not row.component:
                report.add('bsc_flamsteed', row, f'Flamsteed number conflicts with BSC: {source_flam}')
    hip_path = directory / 'Hipparcos' / 'hip_main.dat'
    if hip_path.exists():
        report.sources.append(str(hip_path))
        source = dict()
        for raw in hip_path.read_text().splitlines():
            if raw[8:14].strip().isdigit():
                source[int(raw[8:14])] = raw
        for row in records:
            raw = source.get(row.number('hip'))
            if not raw:
                continue
            hd = raw[390:396].strip()
            if hd.isdigit() and row.number('hd') and int(hd) != row.number('hd'):
                report.add('hip_cross_id', row, f'HIP {row.number("hip")} belongs to HD {hd}', 'warning' if row.component else 'error')
            for key, start, end, tolerance in (('magnitude', 42, 46, 0.5), ('bv', 246, 251, 0.4)):
                value = raw[start - 1:end].strip()
                if NUMBER.fullmatch(value) and row.number(key) is not None and abs(row.number(key) - float(value)) > tolerance:
                    report.add('hip_' + key, row, f'{key} differs from Hipparcos; review component/blend/variable photometry', 'warning')
    if not report.sources:
        row = records[0] if records else Record(b'', 0)
        report.add('source_catalogs_absent', row, f'No BSC or Hipparcos source at {directory}')


def audit_bytes(data, sources=None):
    report = Report(digest=hashlib.sha256(data).hexdigest())
    records = [parse_record(raw, index, report) for index, raw in enumerate(data.splitlines(keepends=True), 1)]
    report.rows = len(records)
    if not records:
        report.add('empty_catalog', Record(b'', 0), 'Catalog has no stars')
        return report
    if records[0].identity != '-26':
        report.add('solar_row_order', records[0], 'The solar row must be first; the C++ loader uses cels[0]')
    if sum(row.identity == '-26' for row in records) != 1:
        report.add('solar_row_count', records[0], 'Catalog must contain exactly one solar row')
    for row in records:
        check_photometry(row, report)
    check_relationships(records, report)
    if sources is not None:
        check_sources(records, report, sources)
    return report


def audit_path(path, sources=None):
    path = Path(path)
    data = gzip.decompress(path.read_bytes()) if path.suffix == '.gz' else path.read_bytes()
    report = audit_bytes(data, sources)
    sibling = Path(str(path) + '.gz') if path.suffix != '.gz' else path.with_suffix('')
    if sibling.exists():
        other = gzip.decompress(sibling.read_bytes()) if sibling.suffix == '.gz' else sibling.read_bytes()
        if data != other:
            report.add('compressed_mismatch', Record(b'', 0), 'Plain and compressed catalog contents differ')
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('catalog', nargs='?', default='catalogs/soles_alienorum.dat')
    parser.add_argument('--sources', help='Directory containing downloaded BSC/Hipparcos catalogs')
    parser.add_argument('--report', help='Write a complete JSON diagnostic report')
    parser.add_argument('--strict-warnings', action='store_true')
    args = parser.parse_args(argv)
    try:
        report = audit_path(args.catalog, args.sources)
    except (OSError, EOFError, gzip.BadGzipFile, UnicodeError) as error:
        print(f'Catalog audit could not run: {error}', file=sys.stderr)
        return 2
    print(f'{report.rows} rows; {report.errors} errors; {len(report.issues) - report.errors} review warnings')
    for (severity, code), count in sorted(report.counts().items()):
        print(f'{severity:7} {code:28} {count}')
        for issue in [item for item in report.issues if item.severity == severity and item.code == code][:3]:
            print(f'  line {issue.line}: {issue.identity}: {issue.detail}')
    if args.report:
        Path(args.report).write_text(json.dumps(report.as_dict(), indent=2) + '\n')
    return 1 if report.errors or (args.strict_warnings and report.issues) else 0


if __name__ == '__main__':
    sys.exit(main())
