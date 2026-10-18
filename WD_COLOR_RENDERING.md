# White dwarf color rendering

Published disc colors take precedence over computed atmosphere colors. The shipped
catalog contains all 239 entries in Hollands et al. (2026), Tables C3-C6. Alternative
results and unresolved component models retain explicit flags. Identity association
uses verified aliases and full WDJ identifiers; it does not move objects or alter
magnitudes, distances, or orbital membership. Missing-star imports are separate work.

## Computed colors

The hydrogen and helium reference tracks reproduce Tables C1-C2 at log g = 8.
Hydrogen stars with mass and radius can also use a derived Koester/SVO gravity grid
covering 6000-40000 K and log g = 7-9. Its gravity ratios relative to log g = 8 adjust
the Hollands reference track. This is an approximation across model generations,
with assumed stellar parameters remaining explicitly marked as such. Unsupported
compositions, magnetic/mixed spectra, and temperatures outside a grid retain the
existing B-V fallback.

`tools/wd_spectrum_colors.py` performs offline integration over 360-830 nm using
the official CIE 1931 2-degree functions and the paper's XYZ-to-sRGB matrix. It
checks the CIE dataset checksum, wavelength coverage, sample order, finite fluxes,
and RGB gamut. It converts SVO wavelength/flux units before integration. Source
spectra are already flux calibrated; this tool does not fit or recalibrate arbitrary
observations. Runtime uses only the compact derived grid, with no network or
spectral integration during rendering.

## Angular colors

`catalogs/WD_limb_profiles.dat` contains approximations reconstructed from all
12 model illustrations in Figures B8-B11: three DA reference temperatures and nine
individual DQ/DZ models. The PDF's vector model-sample markers provide nine
viewing angles per channel. `tools/digitize_wd_limb_profiles.py` extracts those
samples and fits a four-term Claret law separately to R, G, and B. Maximum residual
is below 0.008 of channel-center intensity. These are digitized figure data, not
the authors' original spectral models.

Each channel is normalized by its projected-disc integral. One shared exposure
normalization then preserves the published integrated hue while allowing a green
center and a warm limb. There is no independently normalized color at each angle.
DA profiles interpolate only between 7000 and 20000 K. Objects without a supported
angular model retain the common quadratic brightness approximation.

GPU and CPU sphere paths evaluate the same coefficients. Limb color follows the
viewing angle rather than a rotating surface texture. Explicit surface textures
remain intact. Source colors, atmospheric yellowing, red-light mode, and exposure
remain separate. Work per resolved sample is a square root, polynomial terms, and
RGB conversion; profile selection and parsing occur outside the pixel loop.

Figure B11's embedded green-star image has center RGB (217,255,180), while the
prose lists (208,255,180). The reconstruction matches the embedded image within
two levels and preserves the tabulated disc color #DDFFB8. This source discrepancy
is not hidden by forcing an arbitrary red correction.

## Sources

- Hollands et al. (2026), *Any colour you like: the diverse chromaticities of white
  dwarfs*, DOI 10.1093/rasti/rzag069, CC BY 4.0.
- Koester DA model service: https://svo2.cab.inta-csic.es/theory/newov2/index.php?models=koester2
  (Koester 2010 and subsequent updates documented by SVO). Only derived colors ship.
- CIE (2019), *Colour-matching functions of CIE 1931 standard colorimetric observer*,
  DOI 10.25039/CIE.DS.xvudnb9b, CC BY-SA 4.0. The matching functions are downloaded
  for offline processing and are not an application runtime file.
