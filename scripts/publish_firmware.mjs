#!/usr/bin/env node
// Copy a built ESP32 image into the Vercel static tree and pin its digest in
// latest.json. The round screen fetches the URL over the phone's hotspot; the
// manifest is what the phone reads to fill in the URL and SHA-256.
//
//   node scripts/publish_firmware.mjs
//   node scripts/publish_firmware.mjs --bin=build-release/moto_gps_esp32.bin
//   node scripts/publish_firmware.mjs --check
//
// Vercel serves backend/public at the site root, so the default files are
// https://doupaw.site/firmware/moto-gps.bin and .../latest.json.

import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { copyFileSync, existsSync, mkdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const defaults = {
  bin: resolve(repositoryRoot, "platforms/esp32/build/moto_gps_esp32.bin"),
  out: resolve(repositoryRoot, "backend/public/firmware"),
  baseURL: "https://doupaw.site/firmware",
  name: "moto-gps.bin",
  version: "",
};

const kMinimumImageBytes = 1 << 20;
const kMaximumImageBytes = 7 << 20;
const kSchema = "moto-gps-firmware/1";

export function readOptions(argv = process.argv.slice(2), env = process.env) {
  const options = {
    ...defaults,
    bin: env.MOTO_FIRMWARE_BIN ? resolve(env.MOTO_FIRMWARE_BIN) : defaults.bin,
    out: env.MOTO_FIRMWARE_OUT ? resolve(env.MOTO_FIRMWARE_OUT) : defaults.out,
    baseURL: env.MOTO_FIRMWARE_BASE_URL ?? defaults.baseURL,
    name: env.MOTO_FIRMWARE_NAME ?? defaults.name,
    version: env.MOTO_FIRMWARE_VERSION ?? defaults.version,
    check: false,
  };
  for (const argument of argv) {
    if (argument === "--help" || argument === "-h") {
      options.help = true;
      continue;
    }
    if (argument === "--check") { options.check = true; continue; }
    const match = /^--([a-z-]+)=(.*)$/u.exec(argument);
    if (!match) throw new Error(`unknown argument: ${argument}`);
    const [, key, value] = match;
    if (key === "bin") options.bin = resolve(value);
    else if (key === "out") options.out = resolve(value);
    else if (key === "base-url") options.baseURL = value.replace(/\/+$/u, "");
    else if (key === "name") options.name = value;
    else if (key === "version") options.version = value;
    else throw new Error(`unknown argument: ${argument}`);
  }
  if (!/^[A-Za-z0-9._-]+$/u.test(options.name)) {
    throw new Error("--name must be a plain file name");
  }
  return options;
}

function git(...args) {
  try {
    return execFileSync("git", args, {
      cwd: repositoryRoot,
      encoding: "utf8",
      stdio: ["ignore", "pipe", "ignore"],
    }).trim();
  } catch {
    return "";
  }
}

export function sha256Of(buffer) {
  return createHash("sha256").update(buffer).digest("hex");
}

export function buildManifest({ version, url, sha256, sizeBytes, builtAt, commit }) {
  return {
    schema: kSchema,
    version,
    file: url.slice(url.lastIndexOf("/") + 1),
    url,
    sha256,
    size_bytes: sizeBytes,
    built_at: builtAt,
    git_commit: commit,
  };
}

export function main(argv = process.argv.slice(2)) {
  const options = readOptions(argv);
  const manifestPath = resolve(options.out, "latest.json");
  const binPath = resolve(options.out, options.name);

  if (options.help) {
    console.log(`usage: node scripts/publish_firmware.mjs [options]

  --bin=PATH       built image (default platforms/esp32/build/moto_gps_esp32.bin)
  --out=DIR        static output (default backend/public/firmware)
  --base-url=URL   public base (default https://doupaw.site/firmware)
  --name=FILE      served name (default moto-gps.bin)
  --version=TEXT   override the manifest version
  --check          fail if latest.json is behind the published bin`);
    return 0;
  }

  if (options.check) {
    return check(options, manifestPath, binPath);
  }

  if (!existsSync(options.bin)) {
    throw new Error(`firmware image not found: ${options.bin}\nBuild it first or pass --bin=...`);
  }
  const image = readFileSync(options.bin);
  if (image.byteLength < kMinimumImageBytes || image.byteLength > kMaximumImageBytes) {
    throw new Error(`image is ${image.byteLength} bytes; an OTA slot holds ${kMinimumImageBytes}..${kMaximumImageBytes}`);
  }
  const sha256 = sha256Of(image);
  const commit = git("rev-parse", "--short", "HEAD");
  const builtAt = git("show", "-s", "--format=%cI", "HEAD") || new Date().toISOString();
  const today = new Date();
  const buildDate = `${today.getFullYear()}${String(today.getMonth() + 1).padStart(2, "0")}${String(today.getDate()).padStart(2, "0")}`;
  const dirty = git("status", "--porcelain") === "" ? "" : "-dirty";
  const version = options.version ||
    (commit !== "" ? `${buildDate}-${commit}${dirty}` : "dev");
  const url = `${options.baseURL}/${options.name}`;
  const manifest = buildManifest({
    version,
    url,
    sha256,
    sizeBytes: image.byteLength,
    builtAt,
    commit,
  });

  mkdirSync(options.out, { recursive: true });
  copyFileSync(options.bin, binPath);
  writeFileSync(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
  console.log(`published ${binPath}`);
  console.log(`  bytes  ${manifest.size_bytes}`);
  console.log(`  sha256 ${manifest.sha256}`);
  console.log(`  url    ${manifest.url}`);
  console.log(`  ver    ${manifest.version}`);
  return 0;
}

function check(options, manifestPath, binPath) {
  if (!existsSync(manifestPath) || !existsSync(binPath)) {
    throw new Error("latest.json or the published .bin is missing; run without --check first");
  }
  const manifest = JSON.parse(readFileSync(manifestPath, "utf8"));
  const image = readFileSync(binPath);
  const expected = {
    schema: kSchema,
    file: options.name,
    url: `${options.baseURL}/${options.name}`,
    sha256: sha256Of(image),
    size_bytes: image.byteLength,
  };
  const mismatches = Object.entries(expected)
    .filter(([key, value]) => manifest[key] !== value)
    .map(([key, value]) => `${key}: expected ${JSON.stringify(value)}, got ${JSON.stringify(manifest[key])}`);
  if (mismatches.length > 0) {
    throw new Error(`published firmware is stale:\n  ${mismatches.join("\n  ")}`);
  }
  console.log(`firmware manifest matches ${options.name} (${manifest.sha256})`);
  return 0;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? "").href) {
  try {
    process.exitCode = main();
  } catch (error) {
    console.error(`publish_firmware: ${error.message}`);
    process.exitCode = 1;
  }
}
