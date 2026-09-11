#!/usr/bin/env node
// ---------------------------------------------------------------------------
// logos-basecamp inspector isolation guard
//
// Asserts the property every app-driving suite depends on: an app launched
// through the framework owns the inspector the framework then talks to.
//
// Two such suites must be able to run at the same time, and they do all the
// time -- `nix build` realises integration-test, host-services-test,
// shutdown-test and smoke-test in parallel under max-jobs, and one machine
// hosts several agents doing the same thing. Until this guard existed they
// could not: every app served its inspector on the same fixed port (3768), and
// the collision was silent in the worst way. The second app's listen() failed
// with EADDRINUSE and its runner then connected to -- and drove -- the FIRST
// app, so the failure surfaced later and elsewhere, as "Cannot connect to
// inspector" on every remaining case of a suite whose own app was healthy.
//
// Usage:
//   node tests/inspector-isolation-tests.mjs <app-binary>
//
// Set LOGOS_QT_MCP to override the framework path (nix builds set this).
//
// Setting QML_INSPECTOR_PORT reproduces the old, broken behaviour on purpose
// -- an explicit port is an instruction the framework honours verbatim -- so
// this suite refuses to run with one set rather than reporting a false red.
// ---------------------------------------------------------------------------

import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const projectRoot = resolve(__dirname, "..");
const qtMcpRoot = process.env.LOGOS_QT_MCP || resolve(projectRoot, "result-mcp");
const { Inspector, launchAppWithInspector } = await import(
  resolve(qtMcpRoot, "test-framework/framework.mjs"));

const APP_BIN = process.argv[2];
if (!APP_BIN) {
  console.error("Usage: node tests/inspector-isolation-tests.mjs <app-binary>");
  process.exit(1);
}

const VERBOSE = process.argv.includes("--verbose");
const CONCURRENCY = 2;

const failures = [];
function check(name, ok, detail) {
  if (ok) {
    console.log(`  \x1b[32mOK\x1b[0m   ${name}`);
  } else {
    console.log(`  \x1b[31mFAIL\x1b[0m ${name} — ${detail}`);
    failures.push(name);
  }
}

console.log("\nlogos-basecamp inspector isolation guard\n");

if (process.env.QML_INSPECTOR_PORT) {
  console.error(
    `QML_INSPECTOR_PORT=${process.env.QML_INSPECTOR_PORT} is set. This suite ` +
    `launches ${CONCURRENCY} apps at once and asserts they do NOT share a port, ` +
    `so a pinned port makes it fail by construction. Unset it.`);
  process.exit(1);
}

// Launched together, not one after the other: the bug only exists while a
// second app is coming up next to a first one that is already listening.
const sessions = await Promise.all(
  Array.from({ length: CONCURRENCY }, () =>
    launchAppWithInspector({ appBin: APP_BIN, verbose: VERBOSE })
      .then((s) => ({ ok: true, ...s }))
      .catch((err) => ({ ok: false, err }))));

try {
  sessions.forEach((s, i) => {
    check(`app ${i + 1} came up with an inspector of its own`,
      s.ok, s.ok ? "" : s.err.message);
  });

  const live = sessions.filter((s) => s.ok);

  live.forEach((s, i) => {
    check(`app ${i + 1} bound its inspector port (no EADDRINUSE)`,
      !s.child.bindFailed.value,
      `the app logged "[QmlInspector] Failed to listen on port ${s.port}" — ` +
      `something else already had it, and this runner would be driving that ` +
      `something else`);
  });

  const ports = live.map((s) => s.port);
  check("the apps got distinct inspector ports",
    new Set(ports).size === ports.length,
    `ports were ${JSON.stringify(ports)} — one fixed port means one of these ` +
    `runners is attached to the other's app`);

  // Each port must answer independently: a shared port answers once and the
  // other runner's queries go to a process it never launched.
  for (const [i, s] of live.entries()) {
    const name = `app ${i + 1}'s inspector answers on port ${s.port}`;
    const inspector = new Inspector(s.port);
    try {
      const res = await inspector.send("getTree", { maxDepth: 1 });
      check(name, !res.error, `getTree returned ${JSON.stringify(res.error)}`);
    } catch (err) {
      check(name, false, err.message);
    } finally {
      inspector.disconnect();
    }
  }
} finally {
  for (const s of sessions) if (s.ok) s.child.kill("SIGKILL");
}

console.log("");
if (failures.length > 0) {
  console.log(`\x1b[31m${failures.length} failed\x1b[0m`);
  process.exit(1);
}
console.log(`\x1b[32m${CONCURRENCY} apps, ${CONCURRENCY} inspectors, no crossed wires\x1b[0m`);
process.exit(0);
