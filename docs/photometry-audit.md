# Bright-object rendering audit

The hidden OpenGL test rendered 25 cases in each color mode at 1280 by 960,
8x zoom, default brightness, and with a dark background. The Moon was at
full phase. Planet-from-Sun scenarios use mean orbital distances and geometric
albedos. Bright Venus and variable stars use representative V magnitudes.

The photometric reference is relative flux, computed as 10 to the power
of -0.4 times the magnitude difference. The display measurement is the sum
of linearized RGB luminance over all lit pixels, including any rendered disc.
The flare column reports the renderer input. Its cap remains 503.

- In realistic colors, the full Moon produced about 34 times the display light of bright Venus, versus about 1,585 times the visual flux. The Sun from Earth produced about 8 times the display light of the full Moon, versus about 413,000 times the visual flux.
- All eight views of the Sun hit the flare cap. The Sun from Mercury was about 6,000 times brighter in reference flux than the Sun from Neptune and about 2.5 times brighter on the display. The 503 cap and display exposure dominate this compression.
- The full Moon painted 112,130 pixels, including its disc and flare. Its roughly 23-pixel projected radius corresponds to about 1,680 disc pixels. Bright Venus painted 11,206 pixels at the tested exposure.
- The Moon and Venus flares cover a large area while their integrated brightness is already strongly compressed. This audit supports keeping the current flare-size cap and avoiding a global reduction in flare size.
- The pretty-color rendering reverses the nominal Vega and Betelgeuse brightness order for these samples. A future adjustment to point-source color normalization could improve that ordering while preserving the flare cap and the intended dynamic range.
- Neptune from the Sun fell below naked-eye visibility at the tested exposure. The expected image is dark at that magnitude.

| Mode | Object | Reference V | Projected radius (px) | Flare input | Lit pixels | Linear luminance |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| realistic | Moon from Earth (full) | -12.7 | 23.1413 | 222.807 | 112130 | 3407.28 |
| realistic | Mercury from Sun | -4.80593 | 0.215786 | 92.1759 | 12928 | 120.459 |
| realistic | Sun from Mercury | -28.8014 | 61.527 | 503 | 579720 | 42310.4 |
| realistic | Venus from Sun | -5.77903 | 0.286487 | 169.733 | 46557 | 395.62 |
| realistic | Sun from Venus | -27.4443 | 32.933 | 503 | 498164 | 30289.2 |
| realistic | Venus from Earth (bright) | -4.7 | 0.4864 | 85.6445 | 11206 | 100.892 |
| realistic | Earth from Sun | -3.98014 | 0.218048 | 48.7817 | 3900 | 47.5265 |
| realistic | Sun from Earth | -26.74 | 23.8105 | 503 | 473433 | 26952.9 |
| realistic | Mars from Sun | 0.237356 | 0.0761306 | 0 | 36 | 13.6663 |
| realistic | Sun from Mars | -25.8251 | 15.6236 | 503 | 451820 | 24106.4 |
| realistic | Jupiter from Sun | -2.25252 | 0.459871 | 0 | 36 | 21.4155 |
| realistic | Sun from Jupiter | -23.1587 | 4.57628 | 503 | 422952 | 18966.5 |
| realistic | Saturn from Sun | 0.857685 | 0.208975 | 0 | 32 | 7.82984 |
| realistic | Sun from Saturn | -21.8429 | 2.49663 | 503 | 417710 | 18082.1 |
| realistic | Uranus from Sun | 5.72341 | 0.0452328 | 0 | 4 | 0.0245216 |
| realistic | Sun from Uranus | -20.3246 | 1.24077 | 503 | 411499 | 17143.6 |
| realistic | Neptune from Sun | 7.84579 | 0.0280243 | 0 | 0 | 0 |
| realistic | Sun from Neptune | -19.3493 | 0.791832 | 503 | 411400 | 16867.4 |
| realistic | Sirius from Earth | -1.46 | 0 | 0 | 36 | 21.4155 |
| realistic | Vega from Earth | 0.03 | 0 | 0 | 32 | 12.5098 |
| realistic | Betelgeuse from Earth | 0.5 | 0 | 0 | 32 | 7.96 |
| realistic | Deneb from Earth | 1.25 | 0 | 0 | 32 | 3.65226 |
| realistic | Epsilon Eridani from Earth | 3.73 | 0 | 0 | 4 | 0.511331 |
| realistic | Alcyone from Earth | 2.87 | 0 | 0 | 16 | 1.00475 |
| realistic | TX Piscium from Earth | 5.1 | 0 | 0 | 4 | 0.0491583 |
| pretty | Moon from Earth (full) | -12.7 | 23.1413 | 222.807 | 112130 | 3037.98 |
| pretty | Mercury from Sun | -4.80593 | 0.215786 | 92.1759 | 12928 | 120.459 |
| pretty | Sun from Mercury | -28.8014 | 61.527 | 503 | 579720 | 19567.4 |
| pretty | Venus from Sun | -5.77903 | 0.286487 | 169.733 | 46557 | 395.62 |
| pretty | Sun from Venus | -27.4443 | 32.933 | 503 | 498164 | 14108.9 |
| pretty | Venus from Earth (bright) | -4.7 | 0.4864 | 85.6445 | 11206 | 100.892 |
| pretty | Earth from Sun | -3.98014 | 0.218048 | 48.7817 | 3900 | 47.5265 |
| pretty | Sun from Earth | -26.74 | 23.8105 | 503 | 473402 | 12534.9 |
| pretty | Mars from Sun | 0.237356 | 0.0761306 | 0 | 36 | 13.6663 |
| pretty | Sun from Mars | -25.8251 | 15.6236 | 503 | 451844 | 11316.3 |
| pretty | Jupiter from Sun | -2.25252 | 0.459871 | 0 | 36 | 21.4155 |
| pretty | Sun from Jupiter | -23.1587 | 4.57628 | 503 | 422992 | 8978.57 |
| pretty | Saturn from Sun | 0.857685 | 0.208975 | 0 | 32 | 7.82984 |
| pretty | Sun from Saturn | -21.8429 | 2.49663 | 503 | 417710 | 8556.65 |
| pretty | Uranus from Sun | 5.72341 | 0.0452328 | 0 | 4 | 0.0245216 |
| pretty | Sun from Uranus | -20.3246 | 1.24077 | 503 | 411499 | 8141.38 |
| pretty | Neptune from Sun | 7.84579 | 0.0280243 | 0 | 0 | 0 |
| pretty | Sun from Neptune | -19.3493 | 0.791832 | 503 | 411400 | 8018.63 |
| pretty | Sirius from Earth | -1.46 | 0 | 0 | 36 | 21.4155 |
| pretty | Vega from Earth | 0.03 | 0 | 0 | 32 | 12.3622 |
| pretty | Betelgeuse from Earth | 0.5 | 0 | 0 | 36 | 18.8078 |
| pretty | Deneb from Earth | 1.25 | 0 | 0 | 32 | 3.81708 |
| pretty | Epsilon Eridani from Earth | 3.73 | 0 | 0 | 12 | 0.812205 |
| pretty | Alcyone from Earth | 2.87 | 0 | 0 | 16 | 1.03611 |
| pretty | TX Piscium from Earth | 5.1 | 0 | 0 | 4 | 0.329495 |
