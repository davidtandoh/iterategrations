// launcher.test.ts — the launcher in launcher/launcher.c, compiled with a scratch home folder and a
// pretend node that writes down the arguments, environment and folder it was started with. Nothing
// here builds the app, signs it, or touches launchd or the privacy settings.
import assert from "node:assert/strict";
import { execFileSync, spawn, spawnSync } from "node:child_process";
import {
  chmodSync,
  existsSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  realpathSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterAll, beforeAll, test } from "vite-plus/test";

const SOURCE = join(import.meta.dirname, "..", "launcher", "launcher.c");
const ITERATE = "/opt/example/iterate/bin/iterate.js";
const ENTRY = "/opt/example/iterategrations/imessage/src/imessage.ts";
const hasCompiler = spawnSync("cc", ["--version"]).status === 0;

let scratch: string;
let home: string;
let launcher: string;
let fakeNode: string;
const dir = () => join(home, ".config", "iterate-imessage");
const record = () => join(scratch, "record.json");

beforeAll(() => {
  if (!hasCompiler) return;
  scratch = mkdtempSync(join(tmpdir(), "imessage-launcher-"));
  home = join(scratch, "home");
  mkdirSync(join(home, ".config", "iterate-imessage"), { recursive: true });
  // The pretend node: node itself, run on a script that keeps what it was given. In "wait" mode it
  // stays up until a signal ends it.
  fakeNode = join(scratch, "fake-node");
  writeFileSync(
    fakeNode,
    `#!${process.execPath}
const fs = require("node:fs");
fs.writeFileSync(${JSON.stringify(join(scratch, "record.json"))}, JSON.stringify({
  argv: process.argv.slice(2), env: process.env, cwd: process.cwd(),
}));
const mode = fs.readFileSync(${JSON.stringify(join(scratch, "mode"))}, "utf8");
if (mode === "wait") setInterval(() => {}, 1000);
else process.exit(Number(mode));
`,
  );
  chmodSync(fakeNode, 0o755);
  launcher = join(scratch, "launcher");
  execFileSync("cc", [
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    `-DLEND_NODE="${fakeNode}"`,
    `-DLEND_ITERATE="${ITERATE}"`,
    `-DLEND_ENTRY="${ENTRY}"`,
    `-DLEND_HOME="${home}"`,
    "-o",
    launcher,
    SOURCE,
  ]);
});

afterAll(() => {
  if (scratch) rmSync(scratch, { recursive: true, force: true });
});

/** Write launcher.conf (mode 600 unless given) and the pretend node's exit code. */
const configure = (text: string, mode = 0o600, exit = "0") => {
  const file = join(dir(), "launcher.conf");
  rmSync(file, { force: true });
  rmSync(record(), { force: true });
  writeFileSync(file, text);
  chmodSync(file, mode);
  writeFileSync(join(scratch, "mode"), exit);
};

/** Run the launcher with an environment it must not pass on. */
const run = (args: string[] = []) =>
  spawnSync(launcher, args, {
    encoding: "utf8",
    env: { ...process.env, NODE_OPTIONS: "--require /tmp/evil.js", IMESSAGE_DB: "/tmp/other.db" },
  });

const GOOD =
  "# the mind and the agent's own address\nMIND=example.com\nIMESSAGE_ACCOUNT=agent@example.com\n";

test.skipIf(!hasCompiler)(
  "it runs the lend with fixed arguments and a fresh environment, from the configuration folder",
  () => {
    configure(GOOD);
    const result = run();
    assert.equal(result.status, 0, result.stderr);
    const seen = JSON.parse(readFileSync(record(), "utf8"));
    assert.deepEqual(seen.argv, [
      ITERATE,
      "provide",
      ENTRY,
      "--mind",
      "example.com",
      "--key",
      join(dir(), "key"),
      "--paths",
      "/integrations/imessage/",
    ]);
    // macOS adds __CF_USER_TEXT_ENCODING to a process of its own accord, after the launcher.
    const { TMPDIR, USER, LOGNAME, __CF_USER_TEXT_ENCODING: _, ...env } = seen.env;
    assert.deepEqual(env, {
      HOME: home,
      PATH: "/usr/bin:/bin:/usr/sbin:/sbin",
      IMESSAGE_ACCOUNT: "agent@example.com",
      IMESSAGE_ALLOW_FILE: join(dir(), "allow"),
      IMESSAGE_STATE_FILE: join(dir(), "state.json"),
    });
    assert.match(TMPDIR, /^\//);
    assert.equal(USER, LOGNAME);
    assert.equal(seen.cwd, realpathSync(dir()));
  },
);

test.skipIf(!hasCompiler)("spaces around keys and values and CRLF line ends are fine", () => {
  configure("  MIND = example.com \r\nIMESSAGE_ACCOUNT=agent@example.com,+447700900123\r\n");
  const result = run();
  assert.equal(result.status, 0, result.stderr);
  const seen = JSON.parse(readFileSync(record(), "utf8"));
  assert.equal(seen.argv[4], "example.com");
  assert.equal(seen.env.IMESSAGE_ACCOUNT, "agent@example.com,+447700900123");
});

test.skipIf(!hasCompiler)("a value may be 512 characters long, and no longer", () => {
  const account = (length: number) => `${"a".repeat(length - "@example.com".length)}@example.com`;
  configure(`MIND=example.com\nIMESSAGE_ACCOUNT=${account(512)}\n`);
  assert.equal(run().status, 0);
  configure(`MIND=example.com\nIMESSAGE_ACCOUNT=${account(513)}\n`);
  const result = run();
  assert.equal(result.status, 78);
  assert.match(result.stderr, /too long/);
});

test.skipIf(!hasCompiler)("the lend's exit code is the launcher's", () => {
  configure(GOOD, 0o600, "3");
  assert.equal(run().status, 3);
});

test.skipIf(!hasCompiler)("it takes no arguments, and starts nothing when given one", () => {
  configure(GOOD);
  const result = run(["/tmp/another-script.js"]);
  assert.equal(result.status, 64);
  assert.match(result.stderr, /takes no arguments/);
  assert.equal(existsSync(record()), false);
});

const refusals: [string, string, RegExp, number?][] = [
  ["an unknown key", `${GOOD}LEND_ENTRY=/tmp/evil.ts\n`, /unknown configuration key/],
  ["NODE_OPTIONS", `${GOOD}NODE_OPTIONS=--require=/tmp/evil.js\n`, /unknown configuration key/],
  ["a key twice", `${GOOD}MIND=example.org\n`, /set twice/],
  ["no MIND", "IMESSAGE_ACCOUNT=agent@example.com\n", /does not set MIND/],
  ["no IMESSAGE_ACCOUNT", "MIND=example.com\n", /does not set IMESSAGE_ACCOUNT/],
  [
    "a MIND that is an option",
    "MIND=--paths=/\nIMESSAGE_ACCOUNT=agent@example.com\n",
    /starts with '-'/,
  ],
  ["a value with a space", "MIND=example.com --key x\nIMESSAGE_ACCOUNT=a@example.com\n", /space/],
  ["an empty value", "MIND=\nIMESSAGE_ACCOUNT=agent@example.com\n", /empty/],
  ["a line without =", `${GOOD}example.com\n`, /not KEY=value/],
  ["a file others may write", GOOD, /writable by nobody else/, 0o664],
];
for (const [what, text, message, mode] of refusals) {
  test.skipIf(!hasCompiler)(`it refuses ${what} and starts nothing`, () => {
    configure(text, mode);
    const result = run();
    assert.equal(result.status, 78);
    assert.match(result.stderr, message);
    assert.equal(existsSync(record()), false);
  });
}

test.skipIf(!hasCompiler)("it refuses a missing configuration", () => {
  configure(GOOD);
  rmSync(join(dir(), "launcher.conf"));
  const result = run();
  assert.equal(result.status, 78);
  assert.match(result.stderr, /cannot read the configuration/);
});

test.skipIf(!hasCompiler)("launchd's stop reaches the lend", async () => {
  configure(GOOD, 0o600, "wait");
  const child = spawn(launcher, [], { stdio: "ignore" });
  const exited = new Promise<number | null>((resolve) => child.on("exit", (code) => resolve(code)));
  for (let i = 0; i < 100 && !existsSync(record()); i++)
    await new Promise((resolve) => setTimeout(resolve, 50));
  assert.ok(existsSync(record()), "the pretend node did not start");
  child.kill("SIGTERM");
  // The lend ends by SIGTERM, and the launcher answers 128 + 15, as a shell would.
  assert.equal(await exited, 143);
});
