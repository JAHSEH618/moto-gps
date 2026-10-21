#!/usr/bin/env node
// Fold docs/todo.md (an append-only event log) into the current open list.
//
// The log never edits or deletes lines, so "what is still open" has to be
// derived, not read off the end. This script derives it and, with --check,
// fails when the log closes a task without citing it.
//
// Entry grammar:
//   - <ISO 8601 timestamp> **TODO|DONE|NOTE|PAUSED** — <text>
// A DONE that closes a task cites it: `Ref \`<timestamp>\``.
// A NOTE that replaces a bundled/obsolete task: `Supersedes \`<timestamp>\``.
//
// Exit codes: 0 clean, 1 warnings under --check, 2 unreadable input.

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";

const ENTRY = /^- (\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:[+-]\d{2}:\d{2}|Z)) \*\*(TODO|DONE|NOTE|PAUSED)\*\* — (.+)$/u;
const ENTRY_HEAD = /^- \d{4}-\d{2}-\d{2}T/u;
const REF = /Ref `([^`]+)`/u;
const CLOSES = /(?:^|\s)Closes `([^`]+)`/u;
const SUPERSEDES = /(?:^|\s)Supersedes `([^`]+)`/u;
const STANDALONE = /\((?:standalone|independent)\)/iu;

// Similarity is worked out on character bigrams so it behaves for both the
// English entries and the Chinese documents without a tokeniser.
function normalise(text) {
  return text
    .toLowerCase()
    .replace(/`[^`]*`/g, (match) => match.slice(1, -1))
    .replace(/[^\p{L}\p{N}]+/gu, " ")
    .trim();
}

function bigrams(text) {
  const clean = normalise(text).replace(/\s+/gu, "");
  const out = new Map();
  for (let i = 0; i < clean.length - 1; i += 1) {
    const gram = clean.slice(i, i + 2);
    out.set(gram, (out.get(gram) ?? 0) + 1);
  }
  return out;
}

function dice(a, b) {
  const left = bigrams(a);
  const right = bigrams(b);
  let shared = 0;
  let total = 0;
  for (const [gram, count] of left) total += count;
  for (const [gram, count] of right) total += count;
  for (const [gram, count] of left) {
    const other = right.get(gram);
    if (other) shared += Math.min(count, other);
  }
  return total === 0 ? 0 : (2 * shared) / total;
}

function parse(text) {
  const entries = [];
  const malformed = [];
  text.split("\n").forEach((line, index) => {
    // Only timestamped bullets are log entries; prose and headers also start
    // with "- " in the document preamble.
    if (!ENTRY_HEAD.test(line)) return;
    const match = ENTRY.exec(line);
    if (!match) {
      malformed.push({ line: index + 1, text: line });
      return;
    }
    const [, timestamp, kind, body] = match;
    entries.push({
      timestamp,
      kind,
      body,
      line: index + 1,
      ref: REF.exec(body)?.[1] ?? null,
      closes: CLOSES.exec(body)?.[1] ?? null,
      supersedes: SUPERSEDES.exec(body)?.[1] ?? null,
    });
  });
  return { entries, malformed };
}

function bestMatch(task, candidates) {
  let winner = null;
  for (const candidate of candidates) {
    const score = dice(task.body, candidate.body);
    if (!winner || score > winner.score) winner = { entry: candidate, score };
  }
  return winner;
}

function fold(entries) {
  const warnings = [];
  const infos = [];
  const tasks = [];
  const closed = [];
  const pendingUncited = [];
  // Duplicate timestamps make `Ref` ambiguous by construction; the log has
  // several batches that share one timestamp.
  const seenAt = new Map();
  for (const entry of entries) {
    seenAt.set(entry.timestamp, (seenAt.get(entry.timestamp) ?? 0) + 1);
  }
  const repeated = [...seenAt].filter(([, count]) => count > 1);
  if (repeated.length > 0) {
    const [timestamp, count] = repeated[0];
    infos.push(
      `timestamps are not unique IDs: ${repeated.length} batches share one (e.g. ${timestamp}, ${count} entries); markers disambiguate by text similarity`,
    );
  }

  const openTasks = () => tasks.filter((task) => task.state === "open");

  for (const entry of entries) {
    if (entry.kind === "TODO") {
      tasks.push({ ...entry, state: "open", closedBy: null });
      continue;
    }

    if (entry.kind === "PAUSED") {
      const match = bestMatch(entry, openTasks());
      if (match && match.score >= 0.45) {
        match.entry.state = "paused";
      } else {
        infos.push(`${entry.timestamp}: global pause with no matching task`);
      }
      continue;
    }

    if (entry.kind === "NOTE") {
      const marker = entry.closes ?? entry.supersedes;
      if (!marker) continue;
      const state = entry.closes ? "closed" : "superseded";
      const candidates = openTasks().filter(
        (task) => task.timestamp === marker,
      );
      if (candidates.length === 0) {
        warnings.push(
          `${entry.timestamp}: ${entry.closes ? "Closes" : "Supersedes"} \`${marker}\` matches no open task`,
        );
        continue;
      }
      const match = bestMatch(entry, candidates);
      match.entry.state = state;
      match.entry.closedBy = entry.timestamp;
      closed.push(match.entry);
      continue;
    }

    // DONE
    let candidates = openTasks();
    if (entry.ref) {
      candidates = candidates.filter((task) => task.timestamp === entry.ref);
      if (candidates.length === 0) {
        warnings.push(
          `${entry.timestamp}: Ref \`${entry.ref}\` matches no open task (dangling or already closed)`,
        );
        continue;
      }
    }
    const match = bestMatch(entry, candidates);
    if (!match || (!entry.ref && match.score < 0.55)) {
      // No originating task: standalone work. Fine, but say so when the
      // completion looks like it is restating an existing task.
      if (STANDALONE.test(entry.body)) {
        infos.push(`${entry.timestamp}: standalone completion`);
      }
      continue;
    }
    if (entry.ref && candidates.length > 1 && match.score < 0.35) {
      warnings.push(
        `${entry.timestamp}: Ref \`${entry.ref}\` is ambiguous (${candidates.length} tasks share that timestamp)`,
      );
      continue;
    }
    if (!entry.ref) {
      // Never infer a closure from prose alone: a bundled task can be half
      // done and the rest would silently disappear. Remember the suspicion and
      // decide after the whole log is folded, because a later NOTE with
      // `Closes`/`Supersedes` may legitimately settle it.
      pendingUncited.push({ entry, task: match.entry });
      continue;
    }
    match.entry.state = "closed";
    match.entry.closedBy = entry.timestamp;
    closed.push(match.entry);
  }

  for (const { entry, task } of pendingUncited) {
    if (task.state !== "open") continue;
    warnings.push(
      `${entry.timestamp}: looks like it closes \`${task.timestamp}\` but has no Ref — append a NOTE with \`Closes \`${task.timestamp}\`\``,
    );
  }

  return { tasks, closed, warnings, infos, malformed: [] };
}

function format(report, { check }) {
  const open = report.tasks.filter((task) => task.state === "open");
  const paused = report.tasks.filter((task) => task.state === "paused");
  const lines = [];
  lines.push(
    `open ${open.length} · paused ${paused.length} · closed ${report.closed.length} · tasks ${report.tasks.length}`,
  );
  if (open.length > 0) {
    lines.push("", "Open:");
    for (const task of open) {
      lines.push(`  ${task.timestamp}  ${task.body}`);
    }
  }
  if (paused.length > 0) {
    lines.push("", "Paused:");
    for (const task of paused) {
      lines.push(`  ${task.timestamp}  ${task.body}`);
    }
  }
  if (report.warnings.length > 0) {
    lines.push("", `Warnings (${report.warnings.length}):`);
    for (const warning of report.warnings) lines.push(`  ! ${warning}`);
  }
  if (!check && report.infos.length > 0) {
    lines.push("", `Notes (${report.infos.length}):`);
    for (const info of report.infos) lines.push(`  · ${info}`);
  }
  return lines.join("\n");
}

function main(argv) {
  const here = dirname(fileURLToPath(import.meta.url));
  const check = argv.includes("--check");
  const json = argv.includes("--json");
  const fileArg = argv.indexOf("--file");
  const file =
    fileArg === -1
      ? resolve(here, "..", "docs", "todo.md")
      : resolve(process.cwd(), argv[fileArg + 1]);

  let text;
  try {
    text = readFileSync(file, "utf8");
  } catch (error) {
    console.error(`todo_status: cannot read ${file}: ${error.message}`);
    return 2;
  }

  const { entries, malformed } = parse(text);
  const report = fold(entries);
  report.malformed = malformed;
  for (const line of malformed) {
    report.warnings.push(`line ${line.line}: not an entry: ${line.text}`);
  }

  if (json) {
    console.log(
      JSON.stringify(
        {
          file,
          open: report.tasks.filter((task) => task.state === "open"),
          paused: report.tasks.filter((task) => task.state === "paused"),
          closed: report.closed,
          warnings: report.warnings,
          infos: report.infos,
        },
        null,
        2,
      ),
    );
  } else {
    console.log(format(report, { check }));
  }

  if (check && report.warnings.length > 0) return 1;
  return 0;
}

process.exitCode = main(process.argv.slice(2));
