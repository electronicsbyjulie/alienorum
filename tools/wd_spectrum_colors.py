#!/usr/bin/env python3
"""Integrate calibrated F_lambda spectra with official CIE 1931 2-degree functions.

CIE data: DOI 10.25039/CIE.DS.xvudnb9b, CIE 2019, CC BY-SA 4.0.
Koester spectra: SVO theoretical spectra service, Koester (2010), with subsequent
Stark-profile updates described by SVO. Only derived color values are shipped.
Inputs must cover 360-830 nm; flux calibration and contamination checks precede
this tool. Shape-only normalization does not determine a stellar magnitude.
"""
import argparse
import bisect
import concurrent.futures
import hashlib
import math
from pathlib import Path
import re
import urllib.parse
import urllib.request

CIE_URL = 'https://files.cie.co.at/Publications-datasets/CIE_xyz_1931_2deg.csv'
SVO_URL = 'https://svo2.cab.inta-csic.es/theory/newov2/'

def fetch(url):
    with urllib.request.urlopen(url, timeout=90) as response:
        return response.read()

def cie_data():
    data = fetch(CIE_URL)
    if hashlib.md5(data).hexdigest() != '17cca777db64b17170f06f67ce9d3ab7':
        raise ValueError('CIE checksum differs from the published dataset')
    return [tuple(float(v) for v in line.split(',')) for line in data.decode().splitlines()
            if 360 <= float(line.split(',')[0]) <= 830]

def integrate(samples, cmf):
    if len(samples) < 2:
        raise ValueError('Spectrum has fewer than two samples')
    if any(not math.isfinite(v) for pair in samples for v in pair):
        raise ValueError('Spectrum has nonfinite values')
    wavelengths = [x for x, y in samples]
    if any(b <= a for a, b in zip(wavelengths, wavelengths[1:])):
        raise ValueError('Wavelengths are not strictly increasing')
    if wavelengths[0] > 360 or wavelengths[-1] < 830:
        raise ValueError('Spectrum does not cover 360-830 nm')
    xyz = [0.0, 0.0, 0.0]
    previous = None
    for wavelength, *weights in cmf:
        i = max(1, bisect.bisect_left(wavelengths, wavelength))
        x0, f0 = samples[i - 1]
        x1, f1 = samples[i]
        flux = f0 + (f1 - f0) * (wavelength - x0) / (x1 - x0)
        values = [flux * weight for weight in weights]
        if previous is not None:
            dx = wavelength - previous[0]
            for channel in range(3):
                xyz[channel] += dx * (previous[1][channel] + values[channel]) / 2
        previous = wavelength, values
    matrix = ((3.2410, -1.5374, -0.4986), (-0.9692, 1.8760, 0.0416), (0.0556, -0.2040, 1.0570))
    rgb = [sum(a * b for a, b in zip(row, xyz)) for row in matrix]
    if max(rgb) <= 0 or min(rgb) < -1e-6 * max(rgb):
        raise ValueError('Spectrum lies outside the positive sRGB gamut')
    return [max(0, value) / max(rgb) for value in rgb]

def koester_grid(destination):
    params = {'models': 'koester2', 'params[koester2][teff][min]': 6000,
              'params[koester2][teff][max]': 40000, 'params[koester2][logg][min]': 7,
              'params[koester2][logg][max]': 9, 'boton': 'Search', 'nres': 'all'}
    html = fetch(SVO_URL + 'index.php?' + urllib.parse.urlencode(params)).decode()
    chosen = []
    temperatures = [6000, 8000, 10000, 12000, 15000, 20000, 30000, 40000]
    gravities = [7, 7.5, 8, 8.5, 9]
    for row in html.split('<tr'):
        fields = re.findall(r'<td class=tabfld[^>]*>([^<]*)', row)
        fid = re.search(r'ssap.php\?model=koester2&fid=(\d+)', row)
        if fid and len(fields) >= 3:
            t, g = float(fields[1]), float(fields[2])
            if t in temperatures and g in gravities:
                chosen.append((t, g, fid[1]))
    chosen = sorted(set(chosen))
    if {(t, g) for t, g, fid in chosen} != {(t, g) for t in temperatures for g in gravities}:
        raise ValueError('SVO did not return the complete requested grid')
    cmf = cie_data()
    def one(item):
        t, g, fid = item
        spectrum = fetch(SVO_URL + 'ssap.php?' + urllib.parse.urlencode(
            {'model': 'koester2', 'fid': fid, 'format': 'ascii'})).decode()
        actual_t = re.search(r'^# teff =\s*([\d.]+)', spectrum, re.M)
        actual_g = re.search(r'^# logg =\s*([\d.]+)', spectrum, re.M)
        if not actual_t or not actual_g or float(actual_t[1]) != t or float(actual_g[1]) != g:
            raise ValueError('SVO spectrum parameters differ from the requested grid node')
        samples = []
        for line in spectrum.splitlines():
            if not line or line.startswith('#'):
                continue
            wavelength, flux = map(float, line.split()[:2])
            samples.append((wavelength / 10, flux * 10))  # Angstrom to nm, per-A to per-nm.
        return t, g, integrate(samples, cmf), fid
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(one, chosen))
    lines = ['# Derived linear sRGB, CIE 1931 2-degree, 360-830 nm; brightest channel normalized to 1.',
             '# Teff(K) logg(cgs) R G B SVO_model_id. No observed stellar magnitudes.',
             '# Koester DA models: https://svo2.cab.inta-csic.es/theory/newov2/index.php?models=koester2',
             '# CIE: DOI 10.25039/CIE.DS.xvudnb9b. Hollands conversion: DOI 10.1093/rasti/rzag069.',
             '# Runtime uses gravity ratios relative to logg=8 to preserve the Hollands C1 track.']
    for t, g, rgb, fid in sorted(results):
        lines.append('%g %g %.12g %.12g %.12g %s' % (t, g, *rgb, fid))
    Path(destination).write_text('\n'.join(lines) + '\n')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--koester-grid', metavar='OUTPUT')
    args = parser.parse_args()
    if not args.koester_grid:
        parser.error('Select --koester-grid OUTPUT')
    koester_grid(args.koester_grid)
