#!/usr/bin/env python3
"""Mutation tests for the independent condensed-catalog auditor."""
from contextlib import redirect_stdout
import gzip
import io
from pathlib import Path
import tempfile
import unittest

from catalog_integrity import FIELDS, audit_bytes, audit_path, main, parse_bayer


def row(**overrides):
    values = dict(id='2c Oph 1', name='Alpha Ophiuchi', ra='17:35:07.1', dec='+12:40:17',
                  magnitude='+02.08', spectral='A5III', bv='+00.15', ub='+00.12',
                  hd='159561', hip='86032', hr='6556', bayer='Alp Oph',
                  parallax='3.3859e-07', distance='4.6701e+01', absolute='+01.30',
                  mass='1.8600e+00', radius='2.5850e+00')
    values.update(overrides)
    data = bytearray(b' ' * 455)
    for key, value in values.items():
        start, end = FIELDS[key]
        encoded = str(value).encode('utf-8')
        if len(encoded) > end - start + 1:
            raise ValueError(key)
        data[start - 1:end] = encoded.ljust(end - start + 1)
    return bytes(data) + b'\n'


def sun():
    return row(id='-26', name='Sun', magnitude='-26.72', spectral='G2V', bv='+00.65', ub='+00.10',
               hd='', hip='', hr='', bayer='', parallax='0', distance='0', absolute='+04.85', mass='1', radius='1')


def companion(**overrides):
    values = dict(id='2c Oph 1 B', name='Alpha Ophiuchi B', hd='159562', hip='86033', hr='',
                  parent='2c Oph 1', axis='1', period='365.25', eccentricity='0.1', mass='1')
    values.update(overrides)
    return row(**values)


class CatalogIntegrityTest(unittest.TestCase):
    def audit(self, *rows, include_sun=True):
        return audit_bytes((sun() if include_sun else b'') + b''.join(rows))

    def assert_code(self, report, code, severity=None):
        matches = [issue for issue in report.issues if issue.code == code]
        self.assertTrue(matches, (code, [issue.code for issue in report.issues]))
        if severity:
            self.assertTrue(all(issue.severity == severity for issue in matches))

    def test_valid_catalog_and_unknown_optional_fields(self):
        report = self.audit(row(), companion(), row(id='', name='HIP 100', hip='100', hd='', hr='',
                          bayer='', spectral='', magnitude='', bv='', ub='', mass='', radius='',
                          distance='', parallax='', absolute=''))
        self.assertEqual(report.errors, 0, report.issues)

    def test_faint_standalone_stars_accept_blank_ids_at_magnitude_ten(self):
        for magnitude in ('+10.00', '+12.50', '+18.25'):
            report = self.audit(row(id='', name='HIP 100', hip='100', hd='', hr='', bayer='', magnitude=magnitude))
            self.assertEqual(report.errors, 0, report.issues)
        self.assert_code(self.audit(companion(id='', magnitude='+12.50')), 'missing_component_id')

    def test_empty_catalog_and_missing_or_duplicate_sun(self):
        self.assert_code(audit_bytes(b''), 'empty_catalog')
        self.assert_code(self.audit(row(), include_sun=False), 'solar_row_order')
        self.assert_code(self.audit(sun(), row()), 'solar_row_count')
        self.assert_code(self.audit(row(id='-26', name='Not the Sun')), 'solar_identity')

    def test_all_numeric_columns_reject_partial_numbers_nan_and_infinity(self):
        from catalog_integrity import NUMERIC_FIELDS
        for key in NUMERIC_FIELDS:
            for token in ('NaN', 'inf', '1x', '+'):
                with self.subTest(field=key, token=token):
                    if len(token) <= FIELDS[key][1] - FIELDS[key][0] + 1:
                        self.assert_code(self.audit(row(**dict([(key, token)]))), 'numeric_syntax')
        self.assert_code(self.audit(row(distance='1e999')), 'nonfinite')

    def test_truncation_separator_spill_tabs_and_bad_encoding(self):
        good = row()
        self.assert_code(self.audit(good[:200] + b'\n'), 'row_width')
        self.assert_code(self.audit(good[:14] + b'X' + good[15:]), 'separator')
        self.assert_code(self.audit(good[:18] + b'\t' + good[19:]), 'control_character')
        self.assert_code(self.audit(good[:18] + b'\xff' + good[19:]), 'encoding')

    def test_integer_identifiers_and_allocated_cache_bounds(self):
        for key, value in (('hd', '359084'), ('hip', '120417'), ('hr', '9111'), ('hd', '-1'), ('hip', '0')):
            with self.subTest(field=key):
                self.assert_code(self.audit(row(**dict([(key, value)]))), 'identifier_range')
        self.assert_code(self.audit(row(hd='1.2')), 'numeric_syntax')

    def test_ra_and_dec_ranges_and_poles(self):
        for ra in ('24:00:00.0', '12:60:00.0', '12:00:60.0', '12:00:0.00'):
            self.assert_code(self.audit(row(ra=ra)), 'coordinates')
        for dec in ('+91:00:00', '+90:00:01', '-12:60:00', '+12:00:60', '12:00:00'):
            self.assert_code(self.audit(row(dec=dec)), 'coordinates')
        self.assertEqual(self.audit(row(ra='23:59:59.9', dec='-90:00:00')).errors, 0)

    def test_ids_constellation_and_faint_rows(self):
        self.assert_code(self.audit(row(id='2c Xxx 1')), 'alienorum_id')
        self.assert_code(self.audit(row(id='2c Oph 0')), 'alienorum_id')
        self.assert_code(self.audit(companion(id='')), 'missing_component_id')
        self.assertEqual(self.audit(row(id='', name='HD159561', bayer='')).errors, 0)

    def test_previous_alpha_regression_and_valid_bayer_numbered_names(self):
        self.assert_code(self.audit(row(bayer='', name='Alpha Ophiuchi')), 'invented_bayer')
        self.assert_code(self.audit(row(name='Beta Ophiuchi')), 'bayer_name')
        self.assert_code(self.audit(row(name='Alpha Tauri')), 'bayer_name')
        self.assert_code(self.audit(row(bayer='Alp1Oph', name='Alpha 2 Ophiuchi')), 'bayer_name')
        self.assertEqual(self.audit(row(bayer='Alp1Oph', name='Alpha 1 Ophiuchi')).errors, 0)
        self.assertEqual(parse_bayer('Pi 1Ori'), (15, '1', 'Ori'))
        self.assertIsNone(parse_bayer('    '))
        self.assertIsNone(parse_bayer('Alp Xxx'))
        self.assert_code(self.audit(row(bayer='Alp Xxx')), 'bayer_syntax')

    def test_custom_names_do_not_mask_explicit_wrong_identifiers(self):
        self.assertEqual(self.audit(row(name='Rasalhague', custom='Y')).errors, 0)
        self.assert_code(self.audit(row(name='Alpha Tauri', custom='Y')), 'bayer_name')
        self.assert_code(self.audit(row(name='HD100', custom='Y')), 'hd_name')
        self.assert_code(self.audit(row(name='HIP100')), 'hip_name')

    def test_anonymous_companion_can_inherit_real_bayer_name(self):
        self.assertEqual(self.audit(row(), companion(bayer='', hd='', hip='')).errors, 0)
        self.assert_code(self.audit(row(name='HD159561', bayer=''), companion(bayer='', hd='', hip='')), 'invented_bayer')
        self.assert_code(self.audit(row(), companion(name='Alpha Ophiuchi C')), 'name_component')

    def test_duplicate_id_and_identical_row_always_fail(self):
        self.assert_code(self.audit(row(), row(name='Unrelated Star', hd='100', hip='100', hr='100', bayer='')), 'duplicate_id')
        self.assert_code(self.audit(row(), row()), 'duplicate_row')

    def test_cross_catalog_duplicates_distinguish_linked_components(self):
        for key in ('hd', 'hip', 'hr', 'gliese'):
            values = dict(id='3c Oph 2', name='Other', bayer='', hd='100', hip='100', hr='100', gliese='GJ 1')
            original = dict(gliese='GJ 1') if key == 'gliese' else dict()
            values[key] = original.get(key, dict(hd='159561', hip='86032', hr='6556').get(key))
            with self.subTest(field=key):
                self.assert_code(self.audit(row(**original), row(**values)), 'duplicate_' + key)
        linked = self.audit(row(), companion(hd='159561', hip='86032', hr='6556'))
        self.assertEqual(linked.errors, 0)
        self.assert_code(linked, 'shared_system_identifier', 'warning')

    def test_shared_sb9_is_a_system_identifier(self):
        report = self.audit(row(sb9='100'), row(id='3c Oph 2', name='Other', hd='1', hip='1', hr='', bayer='', sb9='100'))
        self.assertFalse(any(issue.code == 'duplicate_sb9' for issue in report.issues))

    def test_shared_names_bayer_and_dm_are_reported_for_review(self):
        other = row(id='3c Oph 2', name='Alpha Ophiuchi', hd='1', hip='1', hr='', bayer='Alp Oph',
                    dm_survey='BD', dm_zone='+04', dm_number='123')
        report = self.audit(row(dm_survey='BD', dm_zone='+04', dm_number='123'), other)
        for key in ('name', 'bayer', 'dm'):
            self.assert_code(report, 'duplicate_' + key, 'warning')
        self.assert_code(report, 'coincident_stars', 'warning')

    def test_durchmusterung_gould_and_flags(self):
        self.assert_code(self.audit(row(dm_survey='XX')), 'flag')
        self.assert_code(self.audit(row(dm_survey='BD', dm_zone='+91', dm_number='1')), 'durchmusterung')
        self.assert_code(self.audit(row(dm_number='1')), 'durchmusterung')
        self.assert_code(self.audit(row(gould='187', gould_cons='Xxx')), 'gould_constellation')
        self.assert_code(self.audit(row(custom='N')), 'flag')
        self.assert_code(self.audit(row(eclipsing='Q')), 'flag')

    def test_missing_parent_cycles_and_component_letters(self):
        self.assert_code(self.audit(companion()), 'missing_parent')
        self.assert_code(self.audit(row(), companion(parent='2c Oph 1 B')), 'orbit_cycle')
        self.assert_code(self.audit(row(parent='2c Oph 1 B'), companion()), 'orbit_cycle')
        self.assert_code(self.audit(row(), companion(parent='', period='', axis='')), 'orphan_component')
        self.assert_code(self.audit(row(), companion(id='3c Oph 2')), 'component_suffix')

    def test_nested_systems_and_distinct_companion_catalog_numbers(self):
        nested = companion(id='2c Oph 1 C', name='Alpha Ophiuchi C', hd='1', hip='1', parent='2c Oph 1 B')
        self.assertEqual(self.audit(row(), companion(), nested).errors, 0)
        mismatch = companion(id='2c Oph 2 B', parent='2c Oph 1 B', hd='1', hip='1')
        self.assert_code(self.audit(row(), companion(), mismatch), 'component_identity')

    def test_physical_sanity_and_eccentricity(self):
        for key in ('distance', 'mass', 'radius', 'temperature', 'rotation', 'period', 'axis', 'variability'):
            self.assert_code(self.audit(row(**dict([(key, '-1')]))), 'negative_physical_value')
        self.assert_code(self.audit(companion(eccentricity='1.1')), 'value_range')
        self.assert_code(self.audit(companion(eccentricity='1')), 'unbound_orbit')
        self.assert_code(self.audit(row(velocity='3e8')), 'radial_velocity')

    def test_magnitude_color_spectral_and_distance_consistency(self):
        self.assert_code(self.audit(row(magnitude='100')), 'value_range')
        self.assert_code(self.audit(row(bv='20')), 'value_range')
        self.assert_code(self.audit(row(spectral='M5V', bv='-0.3')), 'spectral_color', 'warning')
        self.assert_code(self.audit(row(spectral='O5V', bv='2.0')), 'spectral_color', 'warning')
        self.assert_code(self.audit(row(spectral='Unknown')), 'spectral_syntax', 'warning')
        self.assert_code(self.audit(row(distance='1000')), 'distance_parallax', 'warning')
        self.assert_code(self.audit(row(absolute='20')), 'distance_modulus', 'warning')
        for spectral in ('G2V', 'sdO', 'DA2', 'WN5', 'C5', 'S3', 'M1/M2III', 'F5III+...'):
            self.assertFalse(any(issue.code == 'spectral_syntax' for issue in self.audit(row(spectral=spectral)).issues))

    def test_variability_and_orbit_diagnostics(self):
        self.assert_code(self.audit(row(bright='8', faint='4')), 'variability_bounds')
        self.assert_code(self.audit(row(bright='4', faint='8', magnitude='10')), 'variable_magnitude', 'warning')
        self.assert_code(self.audit(row(variability='3')), 'variable_missing_range', 'warning')
        self.assert_code(self.audit(row(), companion(period='1e8')), 'orbit_kepler', 'warning')

    def test_plain_gzip_parity_and_cli_exit_status(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'soles.dat'
            data = sun() + row()
            path.write_bytes(data)
            archive = Path(str(path) + '.gz')
            archive.write_bytes(gzip.compress(data))
            self.assertEqual(audit_path(path).errors, 0)
            self.assertEqual(audit_path(archive).errors, 0)
            archive.write_bytes(gzip.compress(sun()))
            self.assert_code(audit_path(path), 'compressed_mismatch')
            with redirect_stdout(io.StringIO()):
                self.assertEqual(main([str(path)]), 1)
                archive.write_bytes(gzip.compress(data))
                self.assertEqual(main([str(path)]), 0)

    def test_source_catalogs_anchor_identity_and_photometry(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'BSC').mkdir()
            source = bytearray(b' ' * 197)
            for start, end, value in ((1, 4, '6556'), (8, 11, 'Alp '), (12, 14, 'Oph'), (26, 31, '159561')):
                source[start - 1:end] = value.encode().ljust(end - start + 1)
            (root / 'BSC' / 'catalog').write_bytes(source + b'\n')
            report = audit_bytes(sun() + row(), root)
            self.assertEqual(report.errors, 0)
            self.assert_code(audit_bytes(sun() + row(hd='1'), root), 'bsc_cross_id')
            self.assert_code(audit_bytes(sun() + row(bayer='', name='HD159561'), root), 'bsc_bayer')
            (root / 'Hipparcos').mkdir()
            hip = bytearray(b' ' * 450)
            for start, end, value in ((9, 14, '86032'), (42, 46, '2.08'), (246, 251, '0.150'), (391, 396, '159561')):
                hip[start - 1:end] = value.encode().ljust(end - start + 1)
            (root / 'Hipparcos' / 'hip_main.dat').write_bytes(hip + b'\n')
            self.assert_code(audit_bytes(sun() + row(hd='1'), root), 'hip_cross_id')
            self.assert_code(audit_bytes(sun() + row(magnitude='8', bv='2'), root), 'hip_magnitude', 'warning')
            self.assert_code(audit_bytes(sun() + row(magnitude='8', bv='2'), root), 'hip_bv', 'warning')
            self.assert_code(audit_bytes(sun() + row(), root / 'absent'), 'source_catalogs_absent')

    def test_numbered_names_and_inherited_ancestor_names(self):
        self.assert_code(self.audit(row(name='56 Ophiuchi', flamsteed='55')), 'numbered_name')
        self.assertEqual(self.audit(row(name='55 Ophiuchi', flamsteed='55')).errors, 0)
        self.assert_code(self.audit(row(name='187 G. Ser', bayer='', gould='187', gould_cons='Oph')), 'gould_name')
        root = row(name='HD159561')
        middle = companion(name='HD159561 B', hd='')
        child = companion(id='2c Oph 1 C', name='HD159561 C', hd='', hip='1', parent='2c Oph 1 B')
        self.assertEqual(self.audit(root, middle, child).errors, 0)

    def test_close_pairs_are_reviewed_across_ra_wrap_and_at_poles(self):
        first = row(ra='23:59:59.9', dec='+00:00:00')
        other = row(id='3c Oph 2', name='Other', hd='1', hip='1', hr='', bayer='', ra='00:00:00.0', dec='+00:00:00')
        self.assert_code(self.audit(first, other), 'near_duplicate_position', 'warning')
        first = row(ra='00:00:00.0', dec='+89:59:59')
        other = row(id='3c Oph 2', name='Other', hd='1', hip='1', hr='', bayer='', ra='06:00:00.0', dec='+89:59:59')
        self.assert_code(self.audit(first, other), 'near_duplicate_position', 'warning')

    def test_large_physical_numbers_are_diagnosed_without_math_overflow(self):
        self.assert_code(self.audit(row(distance='1e308')), 'physical_overflow')
        self.assert_code(self.audit(row(), companion(axis='1e308')), 'physical_overflow')

    def test_negative_magnitude_ids_and_full_negative_epoch_are_valid(self):
        self.assertEqual(self.audit(row(id='-1c CMa', name='HD159561', bayer='')).errors, 0)
        self.assertEqual(self.audit(row(), companion(orbit_epoch='-4.09111277e+09')).errors, 0)

    def test_source_completeness_respects_the_magnitude_ten_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'Hipparcos').mkdir()
            source = []
            for identifier, magnitude in (('101', '9.99'), ('102', '10.00'), ('103', '12.00')):
                raw = bytearray(b' ' * 450)
                raw[8:14] = identifier.encode().ljust(6)
                raw[41:46] = magnitude.encode().ljust(5)
                source.append(bytes(raw))
            (root / 'Hipparcos' / 'hip_main.dat').write_bytes(b'\n'.join(source))
            report = audit_bytes(sun() + row(), root)
            missing = [issue for issue in report.issues if issue.code == 'hip_bright_missing']
            self.assertEqual([issue.identity for issue in missing], ['HIP 101'])

    def test_reports_have_stable_line_numbers_and_checksums(self):
        data = sun() + row() + row()
        first, second = audit_bytes(data), audit_bytes(data)
        self.assertEqual(first.as_dict(), second.as_dict())
        duplicate = next(issue for issue in first.issues if issue.code == 'duplicate_id')
        self.assertEqual((duplicate.line, duplicate.related_line), (3, 2))
        self.assertEqual(len(first.digest), 64)


if __name__ == '__main__':
    unittest.main()
