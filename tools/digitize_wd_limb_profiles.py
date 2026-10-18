#!/usr/bin/env python3
"""Recover plotted model samples from Hollands (2026), Figures B8-B11 (CC BY 4.0).

Offline tool; install PyMuPDF and numpy to run. The input PDF stays local.
The output is a digitized approximation, not the authors' original spectral models.
"""
import argparse
from pathlib import Path
import pymupdf
import numpy as np

LABELS = [['DA20000', 'DA10000', 'DA7000'],
          ['WDJ013655.67+200429.88', 'WDJ090208.37+201051.57', 'WDJ101141.58+284559.07'],
          ['WDJ153505.81+124745.20', 'WDJ091621.31+254029.10', 'WDJ074414.64+464912.63'],
          ['WDJ085100.24+154302.04', 'WDJ133001.17+643523.69', 'WDJ235645.10-205449.94']]

def extract(pdf, output):
    document = pymupdf.open(pdf)
    lines = ['# Claret RGB coefficients digitized from plotted model samples, Hollands et al. (2026).',
             '# DOI 10.1093/rasti/rzag069, Figures B8-B11, CC BY 4.0.',
             '# These are approximate reconstructions, not original atmosphere intensities.',
             '# ID, four R coefficients, four G, four B, three disc-average I/I(center), max fit residual.',
             '# Runtime anchors the integrated hue to the published disc color with one common exposure.']
    for page_index, labels in zip(range(25, 29), LABELS):
        drawings = document[page_index].get_drawings()
        borders = sorted([d['rect'] for d in drawings if d['fill'] == (0., 0., 0.) and
                          65 < d['rect'].x0 < 85 and 65 < d['rect'].width < 75 and
                          0.4 < d['rect'].height < 0.5], key=lambda r: r.y0)
        assert len(borders) == 6, (page_index, len(borders))
        for row, label in enumerate(labels):
            top, bottom = borders[2 * row:2 * row + 2]
            left, right = top.x0 + top.height / 2, top.x1 - top.height / 2
            ticks = [d['rect'].y0 for d in drawings if d['fill'] == (0., 0., 0.) and
                     abs(d['rect'].x0 - left) < 0.01 and abs(d['rect'].height) < 1e-5 and
                     1.9 < d['rect'].width < 2.2 and top.y0 <= d['rect'].y0 <= bottom.y1]
            assert len(ticks) == 3, (label, ticks)
            y_zero, y_one = max(ticks), min(ticks)
            dots = [d['rect'] for d in drawings if d['fill'] == (0., 0., 0.) and
                    len(d['items']) == 8 and all(item[0] == 'c' for item in d['items']) and
                    1.7 < d['rect'].width < 1.8 and 1.7 < d['rect'].height < 1.8 and
                    left - 1 < d['rect'].x0 < right and top.y0 - 1 < d['rect'].y0 < bottom.y1]
            assert len(dots) == 27, (label, len(dots))
            coefficients, means, errors = [], [], []
            for channel in range(3):
                points = dots[9 * channel:9 * (channel + 1)]
                mu = np.array([((r.x0 + r.x1) / 2 - left) / (right - left) for r in points])
                mu = np.clip(mu, 0, 1)
                intensity = np.array([(y_zero - (r.y0 + r.y1) / 2) / (y_zero - y_one) for r in points])
                assert abs(mu[-1] - 1) < 0.0001
                intensity /= intensity[-1]
                basis = np.array([1 - mu ** (k / 2) for k in range(1, 5)]).T
                a = np.linalg.lstsq(basis, 1 - intensity, rcond=None)[0]
                error = np.max(np.abs(1 - basis @ a - intensity))
                assert error < 0.03, (label, channel, error)
                dense_mu = np.linspace(0, 1, 8193)
                dense = np.maximum(0, 1 - sum(a[k - 1] * (1 - dense_mu ** (k / 2)) for k in range(1, 5)))
                mean = np.trapezoid(2 * dense_mu * dense, dense_mu)
                assert 0.05 < mean < 1.5
                coefficients.extend(a.tolist()); means.append(float(mean)); errors.append(float(error))
            lines.append(label + ' ' + ' '.join('%.9g' % v for v in coefficients + means + [max(errors)]))
            print(label, 'max residual', max(errors))
    Path(output).write_text('\n'.join(lines) + '\n')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pdf')
    parser.add_argument('output')
    args = parser.parse_args()
    extract(args.pdf, args.output)
