# Lean is a frame angle, estimated by fusion, not an accelerometer angle

**Status:** accepted (DESIGN_V2 §3.3–§3.5, D1–D3)

`lean` is the roll of the bike frame relative to gravity's vertical axis, right
lean positive (ISO 8855, x forward, y left, z up). Zero is a vertical bike: a
straight, constant-speed ride on a cambered road still reads zero. The valid
range for display is ±70°; outside it the sample is flagged, not shown.

The v1 estimator computed `atan2(-g.x, g.z)` from a low-passed accelerometer
vector. In a steady corner the specific force the accelerometer measures is
aligned with the bike's own vertical axis, so that construction reports a
fraction of the real lean — the more settled the corner, the more wrong it is.
A 40° corner could read in the teens. The estimator was therefore replaced:
the gyro integrates the world-up vector in the body frame at the sample rate,
and slow observations correct it.

Observation priority:

1. Speed valid and above 4 m/s: `lean_eff = atan(-v·ψ̇/g)` plus the body/felt
   force alignment `atan2(a.y, a.z)`, gain 1/1.5 s, gated down during fast
   transitions and bumps. This is what makes a coordinated turn read the real
   angle, and it separates hang-off (`lean_eff - lean`).
2. Stationary: the specific force is the full up vector; gain 1/0.5 s. This is
   the only path that observes pitch.
3. No speed, straight: zero pseudo-observation (`roll integral between two
   uprights must vanish`), gain 1/3 s.
4. Otherwise: gyro only, flagged `GYRO`.

Gyro bias is learned only in a stationary window of at least 2 s (τ = 10 s) and
is frozen while moving; v1 learned it whenever the projected yaw rate looked
quiet, which let real cornering write itself into the bias.

The mount is a rotation `R_mount` (forward + up axes in sensor coordinates),
captured by a 2 s stationary sample and refined by one straight-line
acceleration. The scalar `lean_zero_offset_cdeg` is retired: it could trim a
zero, but not a bracket that is rotated, and it was never wired into the
estimator at all (`motion_heading_sensor.cpp` passed `AttitudeCalibration{}`).

Lateral G is expressed in the horizontal frame (`-v·ψ̇/g` when speed is known),
because in a coordinated turn the body-frame lateral acceleration is
approximately zero and a body-frame G plot would show nothing.

Accepted cost: lean is only as good as speed. A 10% speed error near 45° is
about 3° of lean, so speed must be extrapolated between GNSS fixes rather than
held (DESIGN_V2 §3.5.4).
