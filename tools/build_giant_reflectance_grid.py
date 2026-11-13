#!/usr/bin/env python3
"""Build the packaged, full-phase Cahoy et al. (2010) reflection grid.

Source permission requires credit to Kerri Cahoy, Mark Marley, Jonathan Fortney.
Bessell passbands are supplied by the SVO Filter Profile Service.
Run from the repository root; downloads stay in a temporary directory.
"""
import bisect
import io
import json
import hashlib
from pathlib import Path
import tarfile
import urllib.request
import xml.etree.ElementTree as ET

BASE = 'https://roman.ipac.caltech.edu/'


def fetch(url):
    with urllib.request.urlopen(url, timeout=90) as response:
        return response.read()


def interpolate(samples, x):
    if x < samples[0][0] or x > samples[-1][0]:
        return 0.0
    index = max(1, min(len(samples) - 1, bisect.bisect_left([v[0] for v in samples], x)))
    a, b = samples[index - 1:index + 1]
    return a[1] + (b[1] - a[1]) * (x - a[0]) / (b[0] - a[0])


def main():
    archive = tarfile.open(fileobj=io.BytesIO(fetch(BASE + 'data/sims/cahoy2010_spectra.tgz')))
    root = 'Cahoy_et_al_2010_Albedo_Spectra/'
    wavelengths = list(range(350, 1001, 5))
    models = []
    for family, metals in [('Jupiter', (1, 3)), ('Neptune', (10, 30))]:
        for metal in metals:
            for axis in (.8, 2, 5, 10):
                name = f'{family}_{metal}x_{axis:g}AU_0deg.dat'
                rows = [tuple(map(float, row.split())) for row in archive.extractfile(root + 'albedo_spectra/' + name)]
                samples = [(x * 1000, y) for x, y in rows]
                models.append(dict(family=family, enrichment=metal, axis_au=axis,
                    reflectance=[round(interpolate(samples, x), 8) for x in wavelengths]))
    solar_rows = [tuple(map(float, row.split())) for row in archive.extractfile(root + 'SOLARSPECTRUM.DAT')]
    solar = [(x * .1, y) for x, y in solar_rows]
    filters = {}
    for band in ('B', 'V', 'R'):
        xml = ET.fromstring(fetch('https://svo2.cab.inta-csic.es/theory/fps/fps.php?ID=Generic/Bessell.' + band))
        samples = [(float(row[0].text) * .1, float(row[1].text)) for row in xml.findall('.//TABLEDATA/TR')]
        filters[band] = [round(interpolate(samples, x), 8) for x in wavelengths]
    cie_bytes = fetch('https://files.cie.co.at/Publications-datasets/CIE_xyz_1931_2deg.csv')
    if hashlib.md5(cie_bytes).hexdigest() != '17cca777db64b17170f06f67ce9d3ab7':
        raise ValueError('CIE checksum differs from the published dataset')
    cie_rows = [tuple(float(value) for value in row.split(',')) for row in cie_bytes.decode().splitlines()]
    cmf = [[round(interpolate([(row[0], row[channel]) for row in cie_rows], x), 10)
        for x in wavelengths] for channel in (1, 2, 3)]
    result = dict(format=1, source='Cahoy, Marley & Fortney (2010), ApJ 724, 189',
        doi='10.1088/0004-637X/724/1/189',
        permission='Permission to use these spectra is granted under the condition that credit is given to Kerri Cahoy, Mark Marley, and Jonathan Fortney.',
        archive=BASE + 'data/sims/cahoy2010_spectra.tgz',
        passbands='SVO Filter Profile Service: Generic/Bessell.B, Generic/Bessell.V, Generic/Bessell.R; energy response',
        cmf_source='CIE 1931 2 degree observer, CIE 2019, DOI 10.25039/CIE.DS.xvudnb9b, CC BY-SA 4.0',
        cmf_xyz=cmf,
        wavelengths_nm=wavelengths, solar_flux=[round(interpolate(solar, x), 10) for x in wavelengths],
        filters=filters, models=models)
    Path('catalogs/giant_reflectance.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
