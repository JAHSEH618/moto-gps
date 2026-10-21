# Attitude telemetry flows from the device to the phone

**Status:** accepted

The handlebar ESP32 is the only sensor that can measure bike Lean and G forces, while the phone owns GPS and persistent storage. The device therefore publishes compact Attitude Samples over the existing BLE notify characteristic at 10 Hz, and the phone records them alongside its own GNSS fixes. Buffering on the device and downloading after the ride was rejected because it delays the phone's ride summary and duplicates storage; phone-only sensing cannot measure bike Lean when the phone is in a bag.
