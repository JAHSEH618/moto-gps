import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { test } from "node:test";

// The OTA image and its manifest are served by Vercel from backend/public.
// A stale manifest would make every phone state the wrong SHA-256, so the
// deployed pair has to agree in the source tree, not only after a manual curl.
test("the published firmware manifest matches the served image", () => {
  const url = new URL("../public/firmware/latest.json", import.meta.url);
  const manifest = JSON.parse(readFileSync(url, "utf8"));
  const image = readFileSync(new URL(manifest.file, url));

  assert.equal(manifest.schema, "moto-gps-firmware/1");
  assert.match(manifest.sha256, /^[0-9a-f]{64}$/u);
  assert.equal(manifest.size_bytes, image.byteLength);
  assert.ok(image.byteLength >= 1 << 20 && image.byteLength <= 7 << 20);
  assert.equal(manifest.url, `https://www.doupaw.site/firmware/${manifest.file}`);

  // The digest the phone will send is the digest of the bytes Vercel serves.
  const digest = createHash("sha256").update(image).digest("hex");
  assert.equal(manifest.sha256, digest);
});
