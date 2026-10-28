#pragma once

#include <cstdint>

#include "moto/ble_protocol/ble_protocol.hpp"

namespace moto::esp32 {

// Everything this firmware accepts from a phone, advertised in every
// ConnectionStatus the transport sends.
//
// The list is the app's only way to know a screen exists: each bit gates a
// message type on the phone side, so a capability this firmware implements but
// forgets to advertise is a feature that silently disappears — OTA was
// unreachable for exactly that reason until the bit below was added. Keep it
// next to the protocol, out of the NimBLE transport, so the native suite can
// read it (device_capabilities_tests.cpp).
inline constexpr std::uint32_t kDeviceCapabilities =
    moto::ble::CapabilityNavigation | moto::ble::CapabilityRouteGeometry |
    moto::ble::CapabilityTraffic | moto::ble::CapabilityMediaState |
    moto::ble::CapabilityTouchCommands | moto::ble::CapabilityMusicCommands |
    moto::ble::CapabilityCommandAck | moto::ble::CapabilityMapScene |
    moto::ble::CapabilityAttitudeTelemetry | moto::ble::CapabilityHomeLocation |
    moto::ble::CapabilityDeviceSettings | moto::ble::CapabilityCompassHeading |
    moto::ble::CapabilityGnssInput | moto::ble::CapabilityWifiHotspot |
    // OtaCommand is routed in app_main.cpp and written by OtaUpdate, so the
    // bit has to be here: without it the app hides the firmware screen.
    moto::ble::CapabilityOta |
    // PhoneNavBridge sends FirmwareInfo after each handshake; the firmware
    // screen uses it to tell an installed release from a new one.
    moto::ble::CapabilityFirmwareInfo;

}  // namespace moto::esp32
