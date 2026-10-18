import unittest
from wd_spectrum_colors import integrate

class SpectralColorTest(unittest.TestCase):
    def setUp(self):
        self.cmf = [(w, 0.95047, 1.0, 1.08883) for w in range(360, 831)]

    def test_d65_reference_and_exposure_invariance(self):
        color = integrate([(360, 1), (830, 1)], self.cmf)
        brighter = integrate([(360, 1000), (830, 1000)], self.cmf)
        for channel, exposed in zip(color, brighter):
            self.assertAlmostEqual(channel, 1, delta=0.001)
            self.assertAlmostEqual(channel, exposed, places=12)

    def test_incomplete_or_invalid_spectra_are_rejected(self):
        for spectrum in [[(361, 1), (830, 1)], [(360, 1), (829, 1)],
                         [(830, 1), (360, 1)], [(360, 1), (360, 2), (830, 1)],
                         [(360, float('nan')), (830, 1)], [(360, -1), (830, -1)]]:
            with self.assertRaises(ValueError):
                integrate(spectrum, self.cmf)

if __name__ == '__main__':
    unittest.main()
