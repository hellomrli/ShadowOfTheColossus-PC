"""Byte-based diagnostics never treat a serial, version or region as code evidence."""
from paths import ROOT
import unittest

from game_fixture import executable
from prepare import parse_self
from sotc_compatibility import (BASE, VERSION_CHECKS, compatibility_report,
                                content_region, image_bytes, is_supported)


class ExecutableEvidenceTests(unittest.TestCase):
    def fixture(self, supported=True):
        elf, _, ph, _, _ = parse_self(executable(supported))
        return elf, ph

    def test_report_contains_each_original_and_observed_signature(self):
        elf, ph = self.fixture()
        report = compatibility_report(elf, ph)
        self.assertTrue(report['signature_matches'])
        self.assertEqual(report['patch_layout'], 'EU 1.01')
        self.assertIn('not a full-file hash check', report['verification'])
        for check, (address, original) in zip(report['checks'], VERSION_CHECKS):
            self.assertEqual(check, dict(address=hex(address), vaddr=hex(address - BASE),
                                         expected=original.hex(), actual=original.hex(), matches=True))

    def test_mismatch_reports_actual_bytes_and_disables_the_layout(self):
        elf, ph = self.fixture(False)
        report = compatibility_report(elf, ph)
        self.assertFalse(is_supported(elf, ph))
        self.assertFalse(report['signature_matches'])
        self.assertEqual(report['checks'][0]['actual'], '90' * len(VERSION_CHECKS[0][1]))
        self.assertTrue(report['checks'][1]['matches'])

    def test_matching_data_in_nonexecutable_segment_is_not_code_evidence(self):
        elf, ph = self.fixture()
        ph[0]['flags'] = 4
        address, original = VERSION_CHECKS[0]
        self.assertEqual(image_bytes(elf, ph, address - BASE, len(original)), original)
        self.assertFalse(is_supported(elf, ph))
        self.assertIsNone(compatibility_report(elf, ph)['checks'][0]['actual'])

    def test_memory_only_and_missing_signatures_do_not_match(self):
        elf, ph = self.fixture()
        ph[0]['filesz'] = 0
        self.assertFalse(is_supported(elf, ph))
        self.assertIsNone(compatibility_report(elf, ph)['checks'][0]['actual'])
        self.assertFalse(compatibility_report(elf, [])['signature_matches'])

    def test_publisher_region_is_informational_not_patch_authorization(self):
        for prefix, region in (('HP', 'Asia'), ('EP', 'Europe'), ('UP', 'North America'), ('JP', 'Japan')):
            with self.subTest(prefix=prefix):
                self.assertEqual(content_region(f'{prefix}9000-CUSA08809_00-SOTC0000000000AS'), region)
        self.assertEqual(content_region(None), 'unknown')
        self.assertEqual(content_region(''), 'unknown')
        self.assertTrue(is_supported(*self.fixture()))
        self.assertFalse(is_supported(*self.fixture(False)))


if __name__ == '__main__':
    unittest.main()
