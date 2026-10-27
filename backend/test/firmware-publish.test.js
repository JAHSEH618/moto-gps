import assert from "node:assert/strict";
import { test } from "node:test";

import { buildManifest, readOptions, sha256Of } from "../../scripts/publish_firmware.mjs";

test("the publish manifest carries the fields the phone reads", () => {
  const manifest = buildManifest({
    version: "v1.2.3",
    url: "https://doupaw.site/firmware/moto-gps.bin",
    sha256: "a".repeat(64),
    sizeBytes: 5 << 20,
    builtAt: "2026-10-28T00:00:00+09:00",
    commit: "abc1234",
  });
  assert.deepEqual(manifest, {
    schema: "moto-gps-firmware/1",
    version: "v1.2.3",
    file: "moto-gps.bin",
    url: "https://doupaw.site/firmware/moto-gps.bin",
    sha256: "a".repeat(64),
    size_bytes: 5 << 20,
    built_at: "2026-10-28T00:00:00+09:00",
    git_commit: "abc1234",
  });
});

test("publish options reject a path as the served file name", () => {
  assert.throws(() => readOptions(["--name=../escape.bin"], {}),
                /plain file name/u);
});

test("the digest is lower-case SHA-256", () => {
  assert.equal(sha256Of(Buffer.from("moto")),
               "7dbb8b144228e2f24fe4279cb204bedbf97914cff022a18a1e73ddfd49b310c4");
});
