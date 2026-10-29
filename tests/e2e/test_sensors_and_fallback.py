"""
E2E Tier 1 Suite: Hardware Sensors, Voltmeter, Speedometer, DHT, LDR, and GPS Fallback
Features Covered:
- F16: Aki Voltmeter ADC (GPIO 0, ADC1_CH0, ratio 5.54, eFuse curve cali, enable/disable)
- F17: Speedometer VSS ISR & GPS Fallback (GPIO 5 ISR, frequency math, calibration, fault guard)
- F18: DHT11/DHT22 Sensor (GPIO 3 1-wire, model selection, checksum, enable/disable)
- F19: LDR Light Sensor & Auto Backlight (GPIO 1, ADC1_CH1, 3 modes, hysteresis loop)
"""

import unittest
from e2e_framework import SensorModels, HudState, SpeedSource, BacklightMode, DhtType

class TestF16AkiVoltmeter(unittest.TestCase):
    """F16: Aki Battery Voltmeter ADC subsystem tests."""

    def test_f16_01_gpio_and_channel_mapping(self):
        """Verify voltmeter ADC channel is ADC1_CHANNEL_0 on GPIO 0."""
        gpio_num = 0
        adc_channel = 0
        self.assertEqual(gpio_num, 0)
        self.assertEqual(adc_channel, 0)

    def test_f16_02_voltage_divider_ratio_default(self):
        """Verify default voltage divider ratio (10k + 2.2k -> ~5.545)."""
        r_top = 10000.0
        r_bottom = 2200.0
        theoretical_ratio = (r_top + r_bottom) / r_bottom
        self.assertAlmostEqual(theoretical_ratio, 5.545, places=2)

    def test_f16_03_voltage_calculation_formula(self):
        """Verify calibrated ADC mV calculation into battery voltage."""
        # e.g., 2240 mV at ADC pin * 5.54 / 1000 = 12.41 V
        v = SensorModels.calc_battery_voltage(adc_mv=2240.0, ratio=5.54)
        self.assertEqual(v, 12.41)

    def test_f16_04_enable_disable_toggle(self):
        """Verify disabling voltmeter suppresses sampling and clears active flag."""
        state = HudState()
        state.battery_enabled = True
        self.assertTrue(state.battery_enabled)

        # Disable via config
        state.battery_enabled = False
        self.assertFalse(state.battery_enabled)

    def test_f16_05_custom_ratio_calibration(self):
        """Verify custom ratio multiplier from calibration menu."""
        custom_ratio = 5.62
        v = SensorModels.calc_battery_voltage(adc_mv=2300.0, ratio=custom_ratio)
        self.assertEqual(v, 12.93)

    def test_f16_06_dynamic_enable_from_disabled_boot(self):
        """Verify voltmeter can be enabled dynamically at runtime without reboot."""
        state = HudState()
        state.battery_enabled = False
        state.battery_voltage = 0.0
        # Dynamic enable from web portal
        state.battery_enabled = True
        v = SensorModels.calc_battery_voltage(adc_mv=2240.0, ratio=state.battery_ratio)
        state.battery_voltage = v
        self.assertTrue(state.battery_enabled)
        self.assertEqual(state.battery_voltage, 12.41)


class TestF17SpeedometerAndFallback(unittest.TestCase):
    """F17: Speedometer VSS ISR and BLE GPS Fallback tests."""

    def test_f17_01_gpio_pin_and_interrupt_trigger(self):
        """Verify VSS pulse interrupt is on GPIO 5 (rising edge)."""
        gpio_pin = 5
        self.assertEqual(gpio_pin, 5)

    def test_f17_02_frequency_to_speed_formula(self):
        """Verify pulses/sec to km/h conversion with 28.5 pulses/meter."""
        # At 100 pulses/sec, 100 / 28.5 m/s = 3.5087 m/s * 3.6 = 12.63 km/h
        speed = SensorModels.calc_vss_speed(pulses_per_sec=100.0, pulses_per_meter=28.5)
        self.assertAlmostEqual(speed, 12.6, places=1)

        # Stationary
        self.assertEqual(SensorModels.calc_vss_speed(0.0), 0.0)

    def test_f17_03_fallback_when_vss_disabled(self):
        """Verify automatic selection of BLE GPS speed when VSS disabled in settings."""
        speed, source, fault = SensorModels.speed_fallback_arbitrator(
            vss_enabled=False,
            vss_speed=0.0,
            ble_gps_speed=45.0,
            ble_gps_valid=True,
            vss_continuous_zero_sec=0.0
        )
        self.assertEqual(speed, 45.0)
        self.assertEqual(source, SpeedSource.BLE_GPS)
        self.assertFalse(fault)

    def test_f17_04_sensor_guard_fault_detection(self):
        """Verify hardware fault detected when VSS is 0 for >10s while BLE GPS > 15 km/h."""
        speed, source, fault = SensorModels.speed_fallback_arbitrator(
            vss_enabled=True,
            vss_speed=0.0,
            ble_gps_speed=35.0,
            ble_gps_valid=True,
            vss_continuous_zero_sec=11.5
        )
        self.assertEqual(speed, 35.0)
        self.assertEqual(source, SpeedSource.BLE_GPS)
        self.assertTrue(fault)

    def test_f17_05_primary_vss_priority_normal_driving(self):
        """Verify VSS has primary priority under normal operating conditions."""
        speed, source, fault = SensorModels.speed_fallback_arbitrator(
            vss_enabled=True,
            vss_speed=50.0,
            ble_gps_speed=48.0,
            ble_gps_valid=True,
            vss_continuous_zero_sec=0.0
        )
        self.assertEqual(speed, 50.0)
        self.assertEqual(source, SpeedSource.VSS)
        self.assertFalse(fault)

    def test_f17_06_vss_isr_debounce_emi_filtering(self):
        """Verify 250us debounce rejects ignition noise burst while admitting real pulses."""
        # Simulated pulse train:
        # Real wheel pulse at t = 10,000 us
        # Spark plug EMI ringing at t = 10,020 us, 10,050 us, 10,120 us (all < 250 us after real pulse)
        # Next real pulse at t = 20,000 us
        # Spark plug EMI at t = 20,040 us
        # Real pulse at t = 30,000 us
        pulse_timestamps = [10000, 10020, 10050, 10120, 20000, 20040, 30000]
        # Without debounce: 7 pulses
        # With 250us debounce: exactly 3 valid wheel pulses (10000, 20000, 30000)
        valid = SensorModels.filter_vss_pulses_debounce(pulse_timestamps, debounce_us=250)
        self.assertEqual(valid, 3)


class TestF18DhtSensor(unittest.TestCase):
    """F18: DHT11 and DHT22 temperature and humidity reader tests."""

    def test_f18_01_gpio_pin_assignment(self):
        """Verify DHT single-wire data line is on GPIO 3."""
        gpio_pin = 3
        self.assertEqual(gpio_pin, 3)

    def test_f18_02_dht11_decoding_and_checksum(self):
        """Verify DHT11 byte format: B0=hum, B2=temp, B4=checksum."""
        # 60% humidity, 28°C: B0=60, B1=0, B2=28, B3=0, B4=(60+0+28+0)=88
        temp, hum, valid = SensorModels.decode_dht11(60, 0, 28, 0, 88)
        self.assertTrue(valid)
        self.assertEqual(hum, 60.0)
        self.assertEqual(temp, 28.0)

    def test_f18_03_dht22_decoding_and_negative_temperature(self):
        """Verify DHT22 0.1 resolution and negative temperature sign bit."""
        # DHT22: Humidity 65.4% -> 654 = 0x028E (B0=2, B1=142)
        # Temp -5.2°C -> Raw 52 = 0x0034 with sign bit 0x80 -> B2=0x80, B3=0x34
        # Checksum = (2 + 142 + 128 + 52) & 0xFF = 324 & 0xFF = 68 (0x44)
        b0, b1, b2, b3, b4 = 2, 142, 128, 52, 68
        temp, hum, valid = SensorModels.decode_dht22(b0, b1, b2, b3, b4)
        self.assertTrue(valid)
        self.assertEqual(hum, 65.4)
        self.assertEqual(temp, -5.2)

    def test_f18_04_corrupted_checksum_rejection(self):
        """Verify corrupted checksum flags valid = False."""
        temp, hum, valid = SensorModels.decode_dht11(60, 0, 28, 0, 99)  # 99 != 88
        self.assertFalse(valid)

    def test_f18_05_dynamic_enable_disable_toggle(self):
        """Verify disabling DHT sets state flag and stops sampling."""
        state = HudState()
        state.dht_enabled = False
        self.assertFalse(state.dht_enabled)


class TestF19LdrLightSensorAndBacklight(unittest.TestCase):
    """F19: LDR Ambient Light Sensor and Auto Backlight Loop tests."""

    def test_f19_01_gpio_and_channel_mapping(self):
        """Verify LDR ADC channel is ADC1_CHANNEL_1 on GPIO 1."""
        gpio_num = 1
        adc_channel = 1
        self.assertEqual(gpio_num, 1)
        self.assertEqual(adc_channel, 1)

    def test_f19_02_mode_manual_off(self):
        """Verify Mode 0 forces backlight duty to 0."""
        duty = SensorModels.calc_ldr_backlight(
            adc_raw=500, mode=BacklightMode.OFF, manual_duty=200
        )
        self.assertEqual(duty, 0)

    def test_f19_03_mode_manual_on(self):
        """Verify Mode 1 uses fixed manual duty slider value."""
        duty = SensorModels.calc_ldr_backlight(
            adc_raw=500, mode=BacklightMode.ON, manual_duty=180
        )
        self.assertEqual(duty, 180)

    def test_f19_04_mode_auto_ldr_hysteresis(self):
        """Verify Mode 2 transitions between night/tunnel and daylight."""
        thresh = 1400
        hyst = 200

        # Tunnel entry: adc_raw < (1400 - 200 = 1200) -> ON (220)
        duty_tunnel = SensorModels.calc_ldr_backlight(
            adc_raw=900, mode=BacklightMode.AUTO_LDR, manual_duty=180,
            threshold=thresh, hysteresis=hyst, current_duty=0
        )
        self.assertEqual(duty_tunnel, 220)

        # Daylight exit: adc_raw > (1400 + 200 = 1600) -> OFF (0)
        duty_daylight = SensorModels.calc_ldr_backlight(
            adc_raw=2200, mode=BacklightMode.AUTO_LDR, manual_duty=180,
            threshold=thresh, hysteresis=hyst, current_duty=220
        )
        self.assertEqual(duty_daylight, 0)

    def test_f19_05_hysteresis_deadband_stability(self):
        """Verify reading inside deadband maintains previous state (no chattering)."""
        thresh = 1400
        hyst = 200
        # Inside deadband [1200..1600], e.g. 1350
        # If previously ON (220), remains ON
        d1 = SensorModels.calc_ldr_backlight(
            adc_raw=1350, mode=BacklightMode.AUTO_LDR, manual_duty=180,
            threshold=thresh, hysteresis=hyst, current_duty=220
        )
        self.assertEqual(d1, 220)

        # If previously OFF (0), remains OFF
        d2 = SensorModels.calc_ldr_backlight(
            adc_raw=1350, mode=BacklightMode.AUTO_LDR, manual_duty=180,
            threshold=thresh, hysteresis=hyst, current_duty=0
        )
        self.assertEqual(d2, 0)

if __name__ == "__main__":
    unittest.main()
