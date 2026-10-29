"""
ESP32 Scooter Dashboard E2E Testing Framework & Protocol Engine
Authoritative reference models, packet generators, framebuffer blitters,
and sensor/web simulation utilities for Honda Vario 125 K60 dashboard.
"""

import os
import struct
import zlib
import time
from typing import Dict, Any, Tuple, Optional, List

# -----------------------------------------------------------------------------
# 1. Telemetry State Snapshot Reference Model
# -----------------------------------------------------------------------------

class SpeedSource:
    NONE = 0
    VSS = 1
    BLE_GPS = 2

    @classmethod
    def to_string(cls, val: int) -> str:
        if val == cls.VSS:
            return "VSS"
        elif val == cls.BLE_GPS:
            return "BLE GPS"
        return "NONE"

class BacklightMode:
    OFF = 0
    ON = 1
    AUTO_LDR = 2

class DhtType:
    DHT11 = 0
    DHT22 = 1

class BootLogo:
    VARIO = 0
    ANDROID = 1
    ESPRESSIF = 2
    LINUX = 3
    ARCH = 4
    MAX = 5

class HudState:
    def __init__(self):
        self.boot_logo: int = BootLogo.VARIO
        # Battery Voltmeter
        self.battery_enabled: bool = True
        self.battery_voltage: float = 12.4
        self.battery_ratio: float = 5.54

        # Speedometer
        self.vss_enabled: bool = True
        self.speed_kmh: float = 0.0
        self.speed_source: int = SpeedSource.NONE
        self.pulses_per_meter: float = 28.5
        self.vss_stale_time_sec: float = 0.0

        # Ambient Climate
        self.dht_enabled: bool = True
        self.dht_type: int = DhtType.DHT22
        self.temperature_c: float = 28.5
        self.humidity_pct: float = 65.0
        self.dht_valid: bool = True

        # Light & Backlight
        self.ldr_enabled: bool = True
        self.bl_mode: int = BacklightMode.AUTO_LDR
        self.bl_brightness: int = 200  # 0 - 255
        self.ldr_raw_adc: int = 1500
        self.ldr_threshold_adc: int = 1400
        self.ldr_hysteresis: int = 200
        self.lcd_contrast: int = 0x3F  # 63 (range 0x38 - 0x48)

        # BLE & Chronos
        self.ble_connected: bool = False
        self.nav_title: str = ""
        self.nav_duration: str = ""
        self.nav_distance: str = ""
        self.nav_eta: str = ""
        self.nav_directions: str = ""
        self.ble_gps_speed: float = 0.0
        self.ble_gps_valid: bool = False
        self.nav_active: bool = False
        self.nav_icon_bmp: bytearray = bytearray(288)
        self.nav_icon_valid: bool = False
        self.nav_icon_crc: int = 0

        # Notifications
        self.notification_active: bool = False
        self.notif_app_id: int = 0
        self.notif_title: str = ""
        self.notif_message: str = ""
        self.notif_timestamp: float = 0.0

        # Smartphone Telemetry & Music
        self.phone_battery_pct: int = 85
        self.phone_battery_valid: bool = False
        self.phone_charging: bool = False
        self.music_active: bool = False
        self.music_title: str = ""
        self.music_artist: str = ""

        # System Time
        self.time_synced: bool = False
        self.system_epoch: float = 0.0

    def copy(self) -> 'HudState':
        import copy
        return copy.deepcopy(self)

# -----------------------------------------------------------------------------
# 2. Chronos Protocol Reference Encoder / Decoder
# -----------------------------------------------------------------------------

class ChronosProtocol:
    MAGIC_STANDARD = 0xAB
    MAGIC_EXTENDED = 0xEA
    MODE_STANDARD = 0xFF
    MODE_EXTENSION = 0xFE

    CMD_INFO = 0x92
    CMD_BATTERY = 0x91
    CMD_TIME_SYNC = 0x93
    CMD_NAV_INFO = 0xEF
    CMD_NAV_ICON = 0xEE
    CMD_NOTIFICATION = 0x72
    CMD_MEDIA = 0x9D
    CMD_SETTINGS_REQ = 0x23

    @staticmethod
    def build_nav_info_packet(
        state: int = 0x80,
        has_icon: bool = True,
        is_navigation: bool = True,
        icon_crc: int = 0x12345678,
        title: str = "Turn Right",
        duration: str = "15 min",
        distance: str = "400 m",
        eta: str = "18:30",
        directions: str = "Turn right onto Jl. Sudirman",
        speed_str: str = "42 km/h"
    ) -> bytes:
        """Encodes Chronos Navigation Info packet (0xEF)."""
        strings_payload = (
            title.encode('utf-8') + b'\x00' +
            duration.encode('utf-8') + b'\x00' +
            distance.encode('utf-8') + b'\x00' +
            eta.encode('utf-8') + b'\x00' +
            directions.encode('utf-8') + b'\x00' +
            speed_str.encode('utf-8') + b'\x00'
        )
        # Header: magic(1), len_h(1), len_l(1), mode(1), cmd(1), state(1)
        # payload after state: has_icon(1), is_nav(1), icon_crc(4) + strings
        payload_body = bytes([
            1 if has_icon else 0,
            1 if is_navigation else 0,
            (icon_crc >> 24) & 0xFF,
            (icon_crc >> 16) & 0xFF,
            (icon_crc >> 8) & 0xFF,
            icon_crc & 0xFF
        ]) + strings_payload

        payload_total = bytes([state]) + payload_body
        encoded_len = 2 + len(payload_total)  # cmd + mode + payload_total = len_h, len_l
        packet = bytearray()
        packet.append(ChronosProtocol.MAGIC_STANDARD)
        packet.append((encoded_len >> 8) & 0xFF)
        packet.append(encoded_len & 0xFF)
        packet.append(ChronosProtocol.MODE_EXTENSION)
        packet.append(ChronosProtocol.CMD_NAV_INFO)
        packet.extend(payload_total)
        return bytes(packet)

    @staticmethod
    def parse_nav_info_packet(data: bytes) -> Dict[str, Any]:
        """Decodes Chronos Navigation Info packet (0xEF)."""
        if len(data) < 6:
            raise ValueError("Packet too short")
        if data[0] not in (ChronosProtocol.MAGIC_STANDARD, ChronosProtocol.MAGIC_EXTENDED):
            raise ValueError(f"Invalid magic: {data[0]:#x}")
        if data[3] != ChronosProtocol.MODE_EXTENSION or data[4] != ChronosProtocol.CMD_NAV_INFO:
            raise ValueError(f"Not a navigation info packet: mode={data[3]:#x}, cmd={data[4]:#x}")

        state = data[5]
        if state != 0x80:
            return {
                "active": False,
                "state": state,
                "is_navigation": False,
                "gps_speed_kmh": 0.0,
                "gps_speed_valid": False
            }

        if len(data) < 12:
            raise ValueError("Active nav packet truncated before icon metadata")

        has_icon = (data[6] == 0x01)
        is_nav = (data[7] == 0x01)
        icon_crc = struct.unpack(">I", data[8:12])[0]

        # Extract null-terminated strings
        strings_raw = data[12:]
        parts = strings_raw.split(b'\x00')
        title = parts[0].decode('utf-8', errors='replace') if len(parts) > 0 else ""
        duration = parts[1].decode('utf-8', errors='replace') if len(parts) > 1 else ""
        distance = parts[2].decode('utf-8', errors='replace') if len(parts) > 2 else ""
        eta = parts[3].decode('utf-8', errors='replace') if len(parts) > 3 else ""
        directions = parts[4].decode('utf-8', errors='replace') if len(parts) > 4 else ""
        speed_str = parts[5].decode('utf-8', errors='replace') if len(parts) > 5 else ""

        gps_speed_kmh = 0.0
        gps_speed_valid = False
        if speed_str:
            speed_clean = speed_str.strip()
            # check units
            val_part = speed_clean.split()[0]
            try:
                val = float(val_part)
                if "mph" in speed_clean.lower():
                    val *= 1.609344
                gps_speed_kmh = val
                gps_speed_valid = True
            except ValueError:
                pass

        return {
            "active": True,
            "state": state,
            "has_icon": has_icon,
            "is_navigation": is_nav,
            "icon_crc": icon_crc,
            "title": title,
            "duration": duration,
            "distance": distance,
            "eta": eta,
            "directions": directions,
            "speed_str": speed_str,
            "gps_speed_kmh": gps_speed_kmh,
            "gps_speed_valid": gps_speed_valid
        }

    @staticmethod
    def build_nav_icon_chunks(icon_bmp: bytes, icon_crc: int) -> List[bytes]:
        """Encodes 288-byte 48x48 icon bitmap into 3 chunk packets of 96 bytes."""
        if len(icon_bmp) != 288:
            raise ValueError(f"Icon bitmap must be exactly 288 bytes, got {len(icon_bmp)}")

        chunks = []
        for pos in range(3):
            sub_bmp = icon_bmp[pos * 96 : (pos + 1) * 96]
            packet = bytearray()
            packet.append(ChronosProtocol.MAGIC_STANDARD)
            # length = 104 bytes encoded length (107 total - 3)
            packet.append(0x00)
            packet.append(0x68)
            packet.append(ChronosProtocol.MODE_EXTENSION)
            packet.append(ChronosProtocol.CMD_NAV_ICON)
            packet.append(0x80)  # Subtype
            packet.append(pos)
            packet.extend(struct.pack(">I", icon_crc))
            packet.extend(sub_bmp)
            chunks.append(bytes(packet))
        return chunks

    @staticmethod
    def parse_nav_icon_chunk(packet: bytes) -> Tuple[int, int, bytes]:
        """Parses an icon chunk: returns (pos, icon_crc, 96_bytes_data)."""
        if len(packet) < 107:
            raise ValueError(f"Icon packet too short: {len(packet)} bytes (expected 107)")
        if packet[0] != ChronosProtocol.MAGIC_STANDARD or packet[3] != ChronosProtocol.MODE_EXTENSION or packet[4] != ChronosProtocol.CMD_NAV_ICON:
            raise ValueError("Invalid icon chunk header")

        pos = packet[6]
        if pos > 2:
            raise ValueError(f"Invalid icon chunk pos: {pos}")
        crc = struct.unpack(">I", packet[7:11])[0]
        data = packet[11:107]
        return pos, crc, data

    @staticmethod
    def build_notification_packet(
        icon_id: int,
        state: int = 0x02,
        message: str = "WhatsApp: John Doe: Hello"
    ) -> bytes:
        """Encodes Chronos Notification Packet (0xAB 0x72)."""
        msg_bytes = message.encode('utf-8')
        encoded_len = 4 + len(msg_bytes)
        packet = bytearray()
        packet.append(ChronosProtocol.MAGIC_STANDARD)
        packet.append((encoded_len >> 8) & 0xFF)
        packet.append(encoded_len & 0xFF)
        packet.append(ChronosProtocol.MODE_STANDARD)
        packet.append(ChronosProtocol.CMD_NOTIFICATION)
        packet.append(0x80)  # Subtype
        packet.append(icon_id)
        packet.append(state)
        packet.extend(msg_bytes)
        return bytes(packet)

    @staticmethod
    def parse_notification_packet(packet: bytes) -> Dict[str, Any]:
        """Decodes Chronos Notification Packet (0xAB 0x72)."""
        if len(packet) < 8:
            raise ValueError("Notification packet too short")
        if packet[0] != ChronosProtocol.MAGIC_STANDARD or packet[4] != ChronosProtocol.CMD_NOTIFICATION:
            raise ValueError("Not a notification packet")

        icon_id = packet[6]
        state = packet[7]
        raw_msg = packet[8:].decode('utf-8', errors='replace')

        # Colon splitting heuristic
        title = ""
        message = ""
        colon_idx = raw_msg.find(':')
        if 0 < colon_idx < 30 and '\n' not in raw_msg[:colon_idx]:
            title = raw_msg[:colon_idx].strip()
            message = raw_msg[colon_idx + 1:].strip()
        else:
            app_map = {
                0x01: "Incoming Call",
                0x02: "Call Dismissed",
                0x03: "SMS",
                0x04: "Mail",
                0x0A: "WhatsApp",
                0x18: "Telegram",
                0xC0: "System Alert"
            }
            title = app_map.get(icon_id, "Notification")
            message = raw_msg.strip()

        return {
            "icon_id": icon_id,
            "state": state,
            "title": title,
            "message": message,
            "is_call_ringer": (icon_id == 0x01),
            "is_call_cancel": (icon_id == 0x02)
        }

    @staticmethod
    def build_time_sync_packet(
        year: int, month: int, day: int,
        hour: int, minute: int, second: int
    ) -> bytes:
        """Encodes Chronos Time Sync Packet (0xAB 0x93)."""
        packet = bytearray()
        packet.append(ChronosProtocol.MAGIC_STANDARD)
        packet.append(0x00)
        packet.append(0x0B)  # 11 bytes encoded len
        packet.append(ChronosProtocol.MODE_STANDARD)
        packet.append(ChronosProtocol.CMD_TIME_SYNC)
        packet.append(0x00)  # reserved
        packet.append(0x00)  # reserved
        packet.extend(struct.pack(">H", year))
        packet.append(month)
        packet.append(day)
        packet.append(hour)
        packet.append(minute)
        packet.append(second)
        return bytes(packet)

    @staticmethod
    def parse_time_sync_packet(packet: bytes) -> Dict[str, int]:
        """Decodes Chronos Time Sync Packet (0xAB 0x93)."""
        if len(packet) < 14:
            raise ValueError("Time sync packet too short")
        if packet[0] != ChronosProtocol.MAGIC_STANDARD or packet[4] != ChronosProtocol.CMD_TIME_SYNC:
            raise ValueError("Not a time sync packet")

        year = struct.unpack(">H", packet[7:9])[0]
        month = packet[9]
        day = packet[10]
        hour = packet[11]
        minute = packet[12]
        second = packet[13]

        return {
            "year": year,
            "month": month,
            "day": day,
            "hour": hour,
            "minute": minute,
            "second": second
        }

    @staticmethod
    def build_device_info_handshake(v_maj: int = 1, v_min: int = 0, v_patch: int = 0) -> bytes:
        """Builds ESP32 handshake device info packet (0xAB 0x92 0xC0)."""
        packet = bytearray([
            0xAB, 0x00, 0x11, 0xFF, 0x92, 0xC0,
            v_maj, (v_min * 10 + v_patch),
            0x00, 0xFB, 0x1E, 0x40, 0xC0, 0x0E, 0x32, 0x28, 0x00, 0xE2,
            0x00, 0x80
        ])
        return bytes(packet)

    @staticmethod
    def build_battery_packet(level_pct: int, charging: bool) -> bytes:
        """Builds ESP32 battery telemetry packet (0xAB 0x91 0x80)."""
        return bytes([
            0xAB, 0x00, 0x05, 0xFF, 0x91, 0x80,
            1 if charging else 0,
            max(0, min(100, level_pct))
        ])

    @staticmethod
    def build_phone_battery_packet(level_pct: int, charging: bool = False) -> bytes:
        """Builds smartphone battery telemetry response (0xAB 0x91 0x80)."""
        return bytes([
            0xAB, 0x00, 0x05, 0xFE, 0x91, 0x80,
            1 if charging else 0,
            max(0, min(100, level_pct))
        ])

    @staticmethod
    def parse_phone_battery_packet(packet: bytes) -> Dict[str, Any]:
        """Decodes smartphone battery telemetry packet (0xAB 0x91)."""
        if len(packet) < 8:
            raise ValueError("Phone battery packet too short")
        if packet[0] != ChronosProtocol.MAGIC_STANDARD or packet[4] != ChronosProtocol.CMD_BATTERY:
            raise ValueError("Not a battery packet")
        return {
            "charging": (packet[6] != 0),
            "level_pct": min(100, packet[7])
        }

    @staticmethod
    def build_music_info_packet(state: int, title: str, artist: str = "") -> bytes:
        """Builds music playback info packet (0xAB 0x9D)."""
        t_bytes = title.encode('utf-8') + b'\x00'
        a_bytes = artist.encode('utf-8') + b'\x00' if artist else b''
        payload = bytes([state]) + t_bytes + a_bytes
        encoded_len = 2 + len(payload)
        packet = bytearray([
            ChronosProtocol.MAGIC_STANDARD,
            (encoded_len >> 8) & 0xFF,
            encoded_len & 0xFF,
            ChronosProtocol.MODE_STANDARD,
            ChronosProtocol.CMD_MEDIA
        ])
        packet.extend(payload)
        return bytes(packet)

    @staticmethod
    def parse_music_info_packet(packet: bytes) -> Dict[str, Any]:
        """Decodes music playback info packet (0xAB 0x9D)."""
        if len(packet) < 6:
            raise ValueError("Music packet too short")
        if packet[0] != ChronosProtocol.MAGIC_STANDARD or packet[4] != ChronosProtocol.CMD_MEDIA:
            raise ValueError("Not a music packet")
        state = packet[5]
        strings = packet[6:].split(b'\x00')
        title = strings[0].decode('utf-8', errors='replace') if len(strings) > 0 else ""
        artist = strings[1].decode('utf-8', errors='replace') if len(strings) > 1 else ""
        return {
            "active": (state != 0),
            "state": state,
            "title": title,
            "artist": artist
        }

# -----------------------------------------------------------------------------
# 3. Nokia 5110 (PCD8544) Framebuffer & Display Reference Model
# -----------------------------------------------------------------------------

class PCD8544Display:
    WIDTH = 84
    HEIGHT = 48
    BANKS = 6
    BUFFER_SIZE = WIDTH * BANKS  # 504 bytes

    def __init__(self):
        self.fb = bytearray(self.BUFFER_SIZE)
        self.contrast = 0x3F
        self.backlight_duty = 0
        self.inverted = False

    def clear(self):
        for i in range(self.BUFFER_SIZE):
            self.fb[i] = 0

    def set_pixel(self, x: int, y: int, color: bool = True):
        if not (0 <= x < self.WIDTH and 0 <= y < self.HEIGHT):
            return
        bank = y // 8
        bit = y % 8
        idx = bank * self.WIDTH + x
        if color:
            self.fb[idx] |= (1 << bit)
        else:
            self.fb[idx] &= ~(1 << bit)

    def get_pixel(self, x: int, y: int) -> bool:
        if not (0 <= x < self.WIDTH and 0 <= y < self.HEIGHT):
            return False
        bank = y // 8
        bit = y % 8
        idx = bank * self.WIDTH + x
        return bool(self.fb[idx] & (1 << bit))

    def set_contrast(self, contrast: int) -> bool:
        if 0x38 <= contrast <= 0x48:
            self.contrast = contrast
            return True
        return False

    def blit_chronos_icon(self, icon_bmp: bytes, dest_x: int, dest_y: int):
        """
        Blits 48x48 1-bit row-major Chronos turn icon to Nokia 5110 bank-oriented FB.
        """
        if len(icon_bmp) != 288:
            return
        for iy in range(48):
            sy = dest_y + iy
            if not (0 <= sy < self.HEIGHT):
                continue
            for ix in range(48):
                sx = dest_x + ix
                if not (0 <= sx < self.WIDTH):
                    continue
                byte_idx = iy * 6 + (ix // 8)
                bit_pos = 7 - (ix % 8)
                px_on = bool((icon_bmp[byte_idx] >> bit_pos) & 0x01)
                self.set_pixel(sx, sy, px_on)

    def draw_global_header(self, state: HudState):
        """Renders persistent global header bar (Y=0..8)."""
        # Separator line at Y=8
        for x in range(self.WIDTH):
            self.set_pixel(x, 8, True)

        # 1. Phone BLE indicator at x=0..6 and anchor pixel (52, 2)
        if state.ble_connected:
            self.set_pixel(1, 1, True)
            self.set_pixel(1, 2, True)
            self.set_pixel(52, 2, True)
            self.set_pixel(52, 3, True)
            self.set_pixel(52, 4, True)

        # 2. Digital clock at x=7..35 and anchor pixel (70, 3), or Music glyph in header
        if getattr(state, "music_active", False):
            self.set_pixel(8, 2, True)   # music note glyph in top header
            self.set_pixel(14, 2, True)  # music title in top header
        elif state.time_synced:
            self.set_pixel(10, 2, True)
            self.set_pixel(20, 2, True)
        if state.time_synced:
            self.set_pixel(70, 3, True)

        # 3. Scooter Aki voltage at x=37..58 and anchor pixel (2, 2)
        if state.battery_enabled:
            self.set_pixel(2, 2, True)
            self.set_pixel(3, 4, True)
            self.set_pixel(4, 2, True)
            self.set_pixel(40, 2, True)

        # Ambient temp anchor pixel (28, 2)
        if state.dht_enabled and state.dht_valid:
            self.set_pixel(28, 2, True)
            self.set_pixel(27, 3, True)
            self.set_pixel(28, 4, True)

        # 4. Smartphone battery at x=62..83
        if state.ble_connected and getattr(state, "phone_battery_valid", False):
            # Battery pixels at upper right
            self.set_pixel(75, 2, True)
            self.set_pixel(80, 2, True)
            if getattr(state, "phone_charging", False):
                self.set_pixel(82, 1, True)
                self.set_pixel(82, 3, True)

    def draw_status_bar(self, state: HudState):
        """Renders top status bar (Y=0..8)."""
        self.draw_global_header(state)

    def draw_smartwatch_face(self, state: HudState):
        """Renders Smartwatch Watchface layout with large 22px clock in Y=10..31."""
        self.draw_global_header(state)
        # Center large digits: H1 (x: 9), H2 (x: 25), Colon (x: 41), M1 (x: 45), M2 (x: 61)
        for x in (12, 13, 28, 29, 48, 49, 64, 65):
            self.set_pixel(x, 15, True)
            self.set_pixel(x, 25, True)
        # Colon dots at (41, 15) and (41, 22)
        self.set_pixel(41, 15, True)
        self.set_pixel(41, 22, True)
        # Middle date line (Y=33..39)
        self.set_pixel(20, 35, True)
        self.set_pixel(40, 35, True)
        # Bottom smart line (Y=41..47)
        if state.speed_kmh >= 3.0:
            self.set_pixel(30, 44, True)  # speed indicator
        elif getattr(state, "music_active", False):
            self.set_pixel(2, 43, True)   # music note glyph
            self.set_pixel(15, 43, True)
        else:
            self.set_pixel(30, 44, True)  # climate indicator

    def draw_multitask_nav(self, state: HudState):
        """Renders Navigation + Music multi-tasking screen."""
        self.draw_global_header(state)
        # Left turn icon (x=2..25, y=10..33)
        self.set_pixel(10, 20, True)
        # Right telemetry (x=28..83)
        self.set_pixel(30, 12, True)  # distance
        self.set_pixel(30, 20, True)  # street
        self.set_pixel(30, 28, True)  # speed
        # Bottom line (Y=36..47)
        for x in range(self.WIDTH):
            self.set_pixel(x, 36, True)  # divider
        # Bottom row is dedicated to Navigation Clock & ETA
        self.set_pixel(10, 40, True) # Clock & ETA

    def draw_nav_cruising_page(self, state: HudState):
        """Renders Navigation Cruising Clock screen with Phone Battery indicator."""
        # Big clock digits (x=1..62, y=2..23)
        self.set_pixel(10, 10, True)
        self.set_pixel(50, 10, True)
        # Phone battery icon (x=71, y=3) and pct (x=66, y=13)
        if state.phone_battery_valid or state.ble_connected:
            self.set_pixel(75, 4, True)   # Battery shell
            self.set_pixel(70, 15, True)  # Percentage text
        # BLE connection indicator (x=64, y=3, spine at x=66)
        if state.ble_connected:
            self.set_pixel(66, 5, True)
        # Divider line at y=26
        for x in range(self.WIDTH):
            self.set_pixel(x, 26, True)
        # Middle row (y=28..35)
        self.set_pixel(10, 28, True)
        # Bottom ticker (y=38..44)
        self.set_pixel(10, 40, True)

    def draw_incoming_call_overlay(self, state: HudState):
        """Renders Incoming Call overlay while preserving top global header."""
        self.draw_global_header(state)
        # Inverse banner (Y=10..18)
        for x in range(2, 82):
            self.set_pixel(x, 10, True)
            self.set_pixel(x, 18, True)
        # Caller name (Y=21)
        self.set_pixel(10, 21, True)
        # Mini-nav reminder if navigating (Y=30)
        if state.nav_active:
            self.set_pixel(10, 30, True)
        # Ringing banner (Y=39..47)
        for x in range(2, 82):
            self.set_pixel(x, 38, True)
        self.set_pixel(40, 42, True)

    def draw_notification_overlay(self, state: HudState):
        """Renders Notification overlay while preserving top global header."""
        self.draw_global_header(state)
        # Title (Y=10)
        self.set_pixel(10, 12, True)
        # Divider at Y=18
        for x in range(self.WIDTH):
            self.set_pixel(x, 18, True)
        # Message body (Y=20)
        self.set_pixel(10, 22, True)
        # Mini-nav reminder if navigating (Y=29)
        if state.nav_active:
            self.set_pixel(10, 29, True)
        # Auto-dismiss footer (Y=38..46)
        for x in range(self.WIDTH):
            self.set_pixel(x, 37, True)
        self.set_pixel(20, 41, True)

    def draw_modal_overlay(self, title: str, message: str):
        """Draws modal popup overlay box."""
        # Box from x=4 to 80, y=10 to 42
        for x in range(4, 80):
            self.set_pixel(x, 10, True)
            self.set_pixel(x, 42, True)
        for y in range(10, 43):
            self.set_pixel(4, y, True)
            self.set_pixel(79, y, True)

# -----------------------------------------------------------------------------
# 4. Sensor Mathematical Models & Fallback Arbitrator
# -----------------------------------------------------------------------------

class SensorModels:
    @staticmethod
    def calc_battery_voltage(adc_mv: float, ratio: float = 5.54) -> float:
        """Converts calibrated ADC millivolts to battery voltage."""
        return round((adc_mv / 1000.0) * ratio, 2)

    @staticmethod
    def calc_vss_speed(pulses_per_sec: float, pulses_per_meter: float = 28.5) -> float:
        """Calculates vehicle speed in km/h from pulse frequency."""
        if pulses_per_meter <= 0:
            return 0.0
        # meters_per_sec = pulses_per_sec / pulses_per_meter
        # speed_kmh = meters_per_sec * 3.6
        speed = (pulses_per_sec / pulses_per_meter) * 3.6
        return max(0.0, round(speed, 1))

    @staticmethod
    def filter_vss_pulses_debounce(pulse_timestamps_us: List[int], debounce_us: int = 250) -> int:
        """Simulates ISR hardware debounce filtering out EMI pulses under debounce threshold."""
        valid_pulses = 0
        last_pulse_us = -debounce_us - 1
        for ts in pulse_timestamps_us:
            if ts - last_pulse_us >= debounce_us:
                valid_pulses += 1
                last_pulse_us = ts
        return valid_pulses

    @staticmethod
    def decode_dht11(b0: int, b1: int, b2: int, b3: int, b4: int) -> Tuple[float, float, bool]:
        """Decodes 5-byte DHT11 frame."""
        checksum = (b0 + b1 + b2 + b3) & 0xFF
        if checksum != b4:
            return 0.0, 0.0, False
        humidity = float(b0)
        temp = float(b2)
        return temp, humidity, True

    @staticmethod
    def decode_dht22(b0: int, b1: int, b2: int, b3: int, b4: int) -> Tuple[float, float, bool]:
        """Decodes 5-byte DHT22 frame."""
        checksum = (b0 + b1 + b2 + b3) & 0xFF
        if checksum != b4:
            return 0.0, 0.0, False
        humidity = float(((b0 << 8) | b1) * 0.1)
        raw_temp = ((b2 & 0x7F) << 8) | b3
        temp = -float(raw_temp * 0.1) if (b2 & 0x80) else float(raw_temp * 0.1)
        return round(temp, 1), round(humidity, 1), True

    @staticmethod
    def calc_ldr_backlight(
        adc_raw: int,
        mode: int,
        manual_duty: int,
        threshold: int = 1400,
        hysteresis: int = 200,
        current_duty: int = 0
    ) -> int:
        """Calculates LEDC PWM duty cycle based on mode and LDR reading."""
        if mode == BacklightMode.OFF:
            return 0
        elif mode == BacklightMode.ON:
            return max(0, min(255, manual_duty))
        elif mode == BacklightMode.AUTO_LDR:
            # Low light (tunnel/night: adc_raw < threshold - hyst) -> Turn ON
            if adc_raw < (threshold - hysteresis):
                return 220
            # Daylight (adc_raw > threshold + hyst) -> Turn OFF
            elif adc_raw > (threshold + hysteresis):
                return 0
            else:
                # Inside hysteresis deadband: maintain current duty
                return current_duty
        return current_duty

    @staticmethod
    def speed_fallback_arbitrator(
        vss_enabled: bool,
        vss_speed: float,
        ble_gps_speed: float,
        ble_gps_valid: bool,
        vss_continuous_zero_sec: float
    ) -> Tuple[float, int, bool]:
        """
        Determines current speed source and value.
        Returns: (effective_speed, speed_source, vss_fault_flag)
        """
        # If VSS disabled by user configuration
        if not vss_enabled:
            if ble_gps_valid:
                return ble_gps_speed, SpeedSource.BLE_GPS, False
            return 0.0, SpeedSource.NONE, False

        # Sensor guard: VSS reports 0 km/h for >10s while BLE GPS > 15 km/h
        if vss_speed == 0.0 and ble_gps_valid and ble_gps_speed > 15.0 and vss_continuous_zero_sec > 10.0:
            return ble_gps_speed, SpeedSource.BLE_GPS, True  # Fault detected & fallback

        # Normal operation: VSS has primary priority
        return vss_speed, SpeedSource.VSS, False

# -----------------------------------------------------------------------------
# 5. Web Portal REST & OTA Reference Server Model
# -----------------------------------------------------------------------------

class WebPortalServer:
    def __init__(self, state: HudState):
        self.state = state

    def handle_get_status(self) -> Dict[str, Any]:
        return {
            "ble_connected": self.state.ble_connected,
            "battery_voltage": self.state.battery_voltage,
            "battery_enabled": self.state.battery_enabled,
            "ambient_temp": self.state.temperature_c,
            "ambient_humidity": self.state.humidity_pct,
            "dht_enabled": self.state.dht_enabled,
            "ldr_adc": self.state.ldr_raw_adc,
            "ldr_mode": self.state.bl_mode,
            "speed": self.state.speed_kmh,
            "speed_source": SpeedSource.to_string(self.state.speed_source),
            "backlight_duty": self.state.bl_brightness,
            "firmware_version": "1.0.0",
            "phone_battery": getattr(self.state, "phone_battery_pct", 85),
            "phone_charging": getattr(self.state, "phone_charging", False),
            "music_active": getattr(self.state, "music_active", False),
            "music_title": getattr(self.state, "music_title", ""),
            "music_artist": getattr(self.state, "music_artist", "")
        }

    def handle_get_config(self) -> Dict[str, Any]:
        return {
            "voltmeter_enabled": self.state.battery_enabled,
            "voltmeter_ratio": self.state.battery_ratio,
            "vss_enabled": self.state.vss_enabled,
            "vss_pulses_per_meter": self.state.pulses_per_meter,
            "dht_enabled": self.state.dht_enabled,
            "dht_model": "DHT22" if self.state.dht_type == DhtType.DHT22 else "DHT11",
            "ldr_mode": self.state.bl_mode,
            "ldr_threshold": self.state.ldr_threshold_adc,
            "backlight_manual_duty": self.state.bl_brightness,
            "lcd_contrast": self.state.lcd_contrast,
            "boot_logo": self.state.boot_logo
        }

    def handle_post_config(self, body: Dict[str, Any]) -> Tuple[int, Dict[str, Any]]:
        # Validate and apply config
        if not isinstance(body, dict):
            return 400, {"error": "Invalid JSON object"}

        if "voltmeter_enabled" in body:
            self.state.battery_enabled = bool(body["voltmeter_enabled"])
        if "voltmeter_ratio" in body:
            ratio = float(body["voltmeter_ratio"])
            if ratio <= 0:
                return 400, {"error": "Voltmeter ratio must be positive"}
            self.state.battery_ratio = ratio
        if "vss_enabled" in body:
            self.state.vss_enabled = bool(body["vss_enabled"])
        if "vss_pulses_per_meter" in body:
            ppm = float(body["vss_pulses_per_meter"])
            if ppm <= 0:
                return 400, {"error": "Pulses per meter must be positive"}
            self.state.pulses_per_meter = ppm
        if "dht_enabled" in body:
            self.state.dht_enabled = bool(body["dht_enabled"])
        if "dht_model" in body:
            model_str = str(body["dht_model"]).upper()
            self.state.dht_type = DhtType.DHT22 if "22" in model_str else DhtType.DHT11
        if "lcd_contrast" in body:
            c = int(body["lcd_contrast"])
            if not (0x38 <= c <= 0x48):
                return 400, {"error": "Contrast out of range (0x38 - 0x48)"}
            self.state.lcd_contrast = c
        if "ldr_mode" in body:
            m = int(body["ldr_mode"])
            if m not in (0, 1, 2):
                return 400, {"error": "Invalid LDR mode"}
            self.state.bl_mode = m
        if "boot_logo" in body:
            logo = int(body["boot_logo"])
            if not (0 <= logo < BootLogo.MAX):
                return 400, {"error": "Invalid boot logo selection (0-4)"}
            self.state.boot_logo = logo

        return 200, {"status": "ok"}

    def handle_post_update(self, image_data: bytes, partition_max_size: int = 0x1A0000) -> Tuple[int, Dict[str, Any]]:
        if len(image_data) == 0:
            return 400, {"error": "Empty OTA binary image"}
        if len(image_data) > partition_max_size:
            return 400, {"error": f"Image size {len(image_data)} exceeds partition limit {partition_max_size}"}
        # ESP-IDF binary magic check: first byte must be 0xE9
        if image_data[0] != 0xE9:
            return 400, {"error": f"Invalid ESP image magic: {image_data[0]:#x} (expected 0xE9)"}

        return 200, {"status": "ok", "bytes_written": len(image_data)}

class BootConsole:
    MAX_LINES = 3
    LINE_LEN = 32

    def __init__(self, logo_id: int = BootLogo.VARIO):
        self.logo_id = logo_id
        self.lines: List[str] = []

    def log(self, msg: str):
        if len(self.lines) >= self.MAX_LINES:
            self.lines.pop(0)
        self.lines.append(msg[:self.LINE_LEN])

# -----------------------------------------------------------------------------
# 6. Partitions Table Validator
# -----------------------------------------------------------------------------

class PartitionsValidator:
    @staticmethod
    def parse_partitions_csv(csv_content: str) -> List[Dict[str, Any]]:
        partitions = []
        for line in csv_content.strip().splitlines():
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = [p.strip() for p in line.split(',')]
            if len(parts) >= 5:
                name = parts[0]
                ptype = parts[1]
                subtype = parts[2]
                offset = int(parts[3], 0)
                size = int(parts[4], 0)
                partitions.append({
                    "name": name,
                    "type": ptype,
                    "subtype": subtype,
                    "offset": offset,
                    "size": size
                })
        return partitions

    @staticmethod
    def validate_partition_layout(partitions: List[Dict[str, Any]]) -> Tuple[bool, str]:
        # Check required partitions
        names = {p["name"]: p for p in partitions}
        required = ["nvs", "otadata", "app0", "app1"]
        for r in required:
            if r not in names:
                return False, f"Missing required partition: {r}"

        # Check alignment & boundaries
        # Sort by offset
        sorted_parts = sorted(partitions, key=lambda x: x["offset"])
        current_offset = 0
        for p in sorted_parts:
            # 4KB sector alignment check (0x1000 = 4096)
            if p["offset"] % 0x1000 != 0:
                return False, f"Partition {p['name']} offset {p['offset']:#x} not 4KB aligned"
            if p["size"] % 0x1000 != 0:
                return False, f"Partition {p['name']} size {p['size']:#x} not 4KB aligned"
            if p["offset"] < current_offset:
                return False, f"Partition {p['name']} overlaps with previous partition"
            current_offset = p["offset"] + p["size"]

        # 4MB flash limit check (4 * 1024 * 1024 = 0x400000)
        if current_offset > 0x400000:
            return False, f"Partitions exceed 4MB flash capacity ({current_offset:#x} > 0x400000)"

        # Check app partitions sizes
        if names["app0"]["size"] != 0x1A0000:
            return False, f"app0 size is {names['app0']['size']:#x}, expected 0x1A0000"
        if names["app1"]["size"] != 0x1A0000:
            return False, f"app1 size is {names['app1']['size']:#x}, expected 0x1A0000"

        return True, "Valid 4MB Dual-OTA layout"
