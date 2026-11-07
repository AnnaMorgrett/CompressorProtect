#!/usr/bin/env python3
"""
===============================================================================
COMPRESSOR PROTECT - AUTOMATED HOST LOGIC & STATE MACHINE TEST SUITE
===============================================================================
Validates pure logic, mathematical routines, voltage/current protection paths,
hysteresis, retry accounting, button ladder decoding, and storage checksums.
"""

import unittest
import math

# =============================================================================
# 1. MATHEMATICAL ALGORITHM TESTS: 32-BIT INTEGER SQUARE ROOT (isqrt32)
# =============================================================================
def isqrt32(n: int) -> int:
    """Bit-by-bit non-restoring integer square root matching firmware/sensor_manager.cpp"""
    c = 0x40000000
    g = 0
    while c != 0:
        if n >= (g + c):
            n -= (g + c)
            g = (g >> 1) + c
        else:
            g >>= 1
        c >>= 2
    return g

class TestMathAlgorithms(unittest.TestCase):
    def test_isqrt32_known_squares(self):
        squares = [0, 1, 4, 9, 16, 25, 100, 10000, 65536, 1000000, 1073741824]
        for sq in squares:
            expected = int(math.isqrt(sq))
            actual = isqrt32(sq)
            self.assertEqual(actual, expected, f"Failed for square {sq}")

    def test_isqrt32_non_squares(self):
        test_values = [2, 3, 5, 8, 15, 99, 500, 12345, 999999, 2147483647]
        for val in test_values:
            expected = int(math.isqrt(val))
            actual = isqrt32(val)
            self.assertEqual(actual, expected, f"Failed for value {val}")


# =============================================================================
# 2. BUTTON RESISTOR LADDER DECODING TESTS (PD6 / AIN6)
# =============================================================================
KEY_ADC_PLUS_MIN = 100
KEY_ADC_PLUS_MAX = 240
KEY_ADC_MENU_MIN = 240
KEY_ADC_MENU_MAX = 420
KEY_ADC_MIN_MIN  = 420
KEY_ADC_MIN_MAX  = 650
KEY_ADC_IDLE_MIN = 850

def decode_button_ladder(adc: int) -> str:
    if KEY_ADC_PLUS_MIN <= adc <= KEY_ADC_PLUS_MAX:
        return "PLUS"
    elif KEY_ADC_MENU_MIN < adc <= KEY_ADC_MENU_MAX:
        return "MENU"
    elif KEY_ADC_MIN_MIN < adc <= KEY_ADC_MIN_MAX:
        return "MINUS"
    elif adc >= KEY_ADC_IDLE_MIN:
        return "IDLE"
    return "AMBIGUOUS"

class TestButtonLadder(unittest.TestCase):
    def test_nominal_button_voltages(self):
        # 10k pullup to 5V:
        # PLUS (U11, 2.2k): 5V * 2.2/(10+2.2) = 0.90V -> ADC ~184
        self.assertEqual(decode_button_ladder(184), "PLUS")
        # MENU (U10, 4.7k): 5V * 4.7/(10+4.7) = 1.60V -> ADC ~327
        self.assertEqual(decode_button_ladder(327), "MENU")
        # MINUS (U9, 10k):  5V * 10/(10+10)   = 2.50V -> ADC ~512
        self.assertEqual(decode_button_ladder(512), "MINUS")
        # IDLE (Open):      5V                = 5.00V -> ADC 1023
        self.assertEqual(decode_button_ladder(1023), "IDLE")

    def test_window_boundaries(self):
        self.assertEqual(decode_button_ladder(100), "PLUS")
        self.assertEqual(decode_button_ladder(240), "PLUS")
        self.assertEqual(decode_button_ladder(241), "MENU")
        self.assertEqual(decode_button_ladder(420), "MENU")
        self.assertEqual(decode_button_ladder(421), "MINUS")
        self.assertEqual(decode_button_ladder(650), "MINUS")
        self.assertEqual(decode_button_ladder(850), "IDLE")

    def test_noise_and_short_rejection(self):
        # Ground short (< 80)
        self.assertEqual(decode_button_ladder(50), "AMBIGUOUS")
        # Deadband between Min and Idle (651..849)
        self.assertEqual(decode_button_ladder(750), "AMBIGUOUS")


# =============================================================================
# 3. VOLTAGE PROTECTION & HYSTERESIS LOGIC TESTS
# =============================================================================
class MockVoltageSupervisor:
    def __init__(self, ovp_cutoff=260, uvp_cutoff=175):
        self.ovp_cutoff = ovp_cutoff
        self.uvp_cutoff = uvp_cutoff
        self.ovp_rec = ovp_cutoff - 10  # 250V
        self.uvp_rec = uvp_cutoff + 10  # 185V
        self.sag_critical = 160

    def evaluate_trip(self, v_fast: int) -> str:
        if v_fast < self.sag_critical:
            return "CRITICAL_SAG_IMMEDIATE"
        elif v_fast > self.ovp_cutoff:
            return "OVERVOLTAGE_WARNING"
        elif v_fast < self.uvp_cutoff:
            return "UNDERVOLTAGE_WARNING"
        return "NORMAL"

    def is_in_recovery_window(self, v_fast: int) -> bool:
        return self.uvp_rec <= v_fast <= self.ovp_rec

class TestVoltageProtection(unittest.TestCase):
    def setUp(self):
        self.sup = MockVoltageSupervisor(260, 175)

    def test_normal_voltage(self):
        self.assertEqual(self.sup.evaluate_trip(220), "NORMAL")
        self.assertTrue(self.sup.is_in_recovery_window(220))

    def test_critical_sag_immediate(self):
        # 159V is below 160V -> immediate cutoff
        self.assertEqual(self.sup.evaluate_trip(159), "CRITICAL_SAG_IMMEDIATE")

    def test_overvoltage_and_hysteresis(self):
        # 261V trips OVP
        self.assertEqual(self.sup.evaluate_trip(261), "OVERVOLTAGE_WARNING")
        # Recovery requires dropping below 250V (ovp_rec)
        self.assertFalse(self.sup.is_in_recovery_window(255))
        self.assertTrue(self.sup.is_in_recovery_window(249))

    def test_undervoltage_and_hysteresis(self):
        # 174V trips UVP
        self.assertEqual(self.sup.evaluate_trip(174), "UNDERVOLTAGE_WARNING")
        # Recovery requires rising above 185V (uvp_rec)
        self.assertFalse(self.sup.is_in_recovery_window(180))
        self.assertTrue(self.sup.is_in_recovery_window(186))

    def test_startup_deadband_elimination(self):
        # 180V is between UVP trip (175V) and UVP recovery (185V).
        # In old firmware, this created an indefinite hang.
        # In our architecture, power qualification explicitly demands is_in_recovery_window:
        self.assertFalse(self.sup.is_in_recovery_window(180))
        # At 185V, it qualifies cleanly:
        self.assertTrue(self.sup.is_in_recovery_window(185))


# =============================================================================
# 4. CURRENT PROTECTION & ROLLING RETRY LOGIC TESTS
# =============================================================================
class MockCurrentProtection:
    def __init__(self, run_max_ma=1200, lra_max_ma=1600):
        self.run_max_ma = run_max_ma
        self.lra_max_ma = lra_max_ma
        self.retry_count = 0
        self.is_locked_out = False

    def trip(self):
        self.retry_count += 1
        if self.retry_count >= 3:
            self.is_locked_out = True

    def reset_sustained_run(self):
        self.retry_count = 0

class TestCurrentProtection(unittest.TestCase):
    def test_rolling_retry_window(self):
        prot = MockCurrentProtection(1200, 1600)

        # Trip 1
        prot.trip()
        self.assertEqual(prot.retry_count, 1)
        self.assertFalse(prot.is_locked_out)

        # Trip 2
        prot.trip()
        self.assertEqual(prot.retry_count, 2)
        self.assertFalse(prot.is_locked_out)

        # Successful sustained run resets retry window
        prot.reset_sustained_run()
        self.assertEqual(prot.retry_count, 0)

        # Trip again
        prot.trip()
        self.assertEqual(prot.retry_count, 1)
        self.assertFalse(prot.is_locked_out)

    def test_lockout_after_three_trips(self):
        prot = MockCurrentProtection(1200, 1600)
        prot.trip()
        prot.trip()
        prot.trip()
        self.assertEqual(prot.retry_count, 3)
        self.assertTrue(prot.is_locked_out)


# =============================================================================
# 5. STORAGE CHECKSUM & INTEGRITY TESTS (Option Bytes)
# =============================================================================
def compute_checksum(cfg_bytes: bytes) -> int:
    c = 0xAA
    for b in cfg_bytes[:8]:
        c ^= b
    return c

class TestStorageIntegrity(unittest.TestCase):
    def test_checksum_computation_and_corruption_detection(self):
        # Default config: magic=0xC3, ver=0x01, ovp=260 (0x0104 -> 4), uvp=175 (175), run=12, lra=16, del=30, fault=0
        raw = bytearray([0xC3, 0x01, 260 % 256, 175, 12, 16, 30, 0])
        valid_checksum = compute_checksum(raw)

        # Append checksum
        raw.append(valid_checksum)
        self.assertEqual(compute_checksum(raw[:8]), raw[8])

        # Corrupt one byte (e.g. OVP changed in flash by bitflip)
        corrupted = bytearray(raw)
        corrupted[2] ^= 0x01
        self.assertNotEqual(compute_checksum(corrupted[:8]), corrupted[8])


if __name__ == '__main__':
    unittest.main()

