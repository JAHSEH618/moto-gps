> **Language:** English · [中文](TESTING.md)

> English edition of the Chinese document. The Chinese file is authoritative if the two differ.

# Testing and reproduction

All of the following are local tests/builds; they do not flash the board or install the app on a phone, and they do not access the real AMap service.
Initialise the `third_party/lvgl` submodule first.

## C++ / shared core

```sh
cmake -S . -B build/native -DMOTO_BUILD_WEB=OFF -DMOTO_BUILD_TESTS=ON
cmake --build build/native --parallel 4
ctest --test-dir build/native --output-on-failure
```

Covers coordinates, the route state machine, the protocol, the Presenter, the connection epoch, heading fusion and PhoneNavBridge.
The tests use a Host Stub, which does not mean the real LVGL drawing / Bluetooth timing has been verified.

## Node gateway

```sh
npm --prefix backend test
```

Uses mock fetch / fixtures to check requests, responses, retries, POI, route candidates and error boundaries; no key is needed.

## Swift

```sh
swift test --package-path platforms/ios
```

SPM covers only the portable Swift core and cannot replace a full app build or device testing.
For a full app simulator build see `platforms/ios/README.md`.

## Map data

```sh
node scripts/generate_jinan_demo_fixture.mjs --check
node scripts/offline_map/validate_jinan_sqlite.mjs
```

Node.js 24+ is recommended. Only data already provided with the repository is checked; no AMap responses are fetched.
After changing the map, re-run the generator and keep the data source and attribution per ODbL.

## Web and ESP32

Web needs Emscripten; run `./scripts/build_web.sh`.
ESP32 needs ESP-IDF 5.5.5; run `idf.py -C platforms/esp32 build`.
The first build downloads public SDKs/components; these network accesses do not need the author's service credentials.

## iOS simulator screenshot review

The iOS 26.2 runtime cannot load PingFang (SpringBoard is tofu too), so that runtime can only judge structure,
spacing, colour and dark mode. Any Chinese truncation, wrap or squeeze question goes to 27.0 or a real device,
because a tofu box is not the advance width of the glyph. Run `scripts/sim_font_gate.sh` first; exit code 2
means this runtime cannot judge typography. The runtime's font defect does not affect installation or display
on a real device: the device's system font is intact and the app's deployment target is 17.0.

CI does not flash, deploy or use account keys. Real acceptance still has to cover: phone offline / screen lock / cellular handover,
BLE disconnection recovery, consistency between the preview route and the round-display route, cross-city routes, fast turns, power supply and long-run operation.
