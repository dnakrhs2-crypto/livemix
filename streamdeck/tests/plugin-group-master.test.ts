import test from "node:test";
import assert from "node:assert/strict";
import { validateProjection, validateServerMessage, validAck, type Snapshot, type Delta, type Ack } from "../src/livemix/protocol.js";
import { StateStore } from "../src/livemix/state-store.js";
import { fixture } from "./helpers.mjs";
import { until, pluginRoot } from "./helpers.mjs";
import { setupActions, commands, alerts, options } from "./action-helpers.js";
import { displayName, statusTitle } from "../src/ui/key-renderer.js";
import { strings } from "../src/ui/strings.js";
import { actionSettings } from "../src/livemix/bindings.js";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";

const kind = "plugin-group-master";

test("master plugin groups survive the projection decoder; old servers default to no master groups", async () => {
  const snapshot = await fixture("initial-state");
  const master = { pluginGroups: [{ index: 1, off: true }, { index: 5, off: false }] };
  assert.deepEqual(Reflect.get(validateProjection({ ...snapshot.state, master }), "master"), master);
  delete snapshot.state.master;
  assert.deepEqual(Reflect.get(validateProjection(snapshot.state), "master"), { pluginGroups: [] });
});

test("master settings accept groups 1..5 and toggle/on/off, with the existing group artwork", async () => {
  for (const groupIndex of [1, 2, 3, 4, 5]) for (const mode of ["toggle", "on", "off"])
    assert.equal(actionSettings(kind, { groupIndex, mode }).valid, true);
  for (const bad of [{ groupIndex: 0 }, { groupIndex: 6 }, { groupIndex: 1.5 }, { mode: "open" }])
    assert.equal(actionSettings(kind, bad).valid, false);
  const manifest = JSON.parse(await readFile(resolve(pluginRoot, "manifest.json"), "utf8"));
  const action = manifest.Actions.find((a: any) => a.UUID === "com.gomtwigim.livemix." + kind);
  assert.equal(action.Name, "Plugin Group (Master)");
  for (const name of ["icon", "on", "off", "group-5-off"]) for (const suffix of ["", "@2x"])
    assert.deepEqual(await readFile(resolve(pluginRoot, `imgs/actions/${kind}/${name}${suffix}.svg`)),
      await readFile(resolve(pluginRoot, `imgs/actions/plugin-group/${name}${suffix}.svg`)));
});

test("master deltas replace only master groups and command ACKs validate their own result", async () => {
  const source = await fixture("initial-state");
  source.state.master = { pluginGroups: [{ index: 1, off: false }, { index: 2, off: false }] };
  const snapshot = validateServerMessage(source) as Snapshot;
  const store = new StateStore(); store.accept(snapshot);
  const delta = { v: 1, type: "stateDelta", instanceId: snapshot.instanceId, sessionId: snapshot.sessionId,
    baseRevision: 1, revision: 2, changes: { master: { pluginGroups: [{ index: 1, off: true }, { index: 2, off: false }] } } };
  assert.equal(store.accept(validateServerMessage(delta) as Delta), true);
  assert.deepEqual(store.snapshot!.state.master.pluginGroups, delta.changes.master.pluginGroups);
  assert.deepEqual(store.snapshot!.state.channels, snapshot.state.channels);
  for (const master of [null, [], { pluginGroups: [{ index: 6, off: false }] }, { pluginGroups: [{ index: 1, off: "true" }] }])
    assert.throws(() => validateProjection({ ...source.state, master }), { code: "INVALID_MESSAGE" });
  const command = { command: "setMasterPluginGroupOff" as const, args: { index: 1, off: true } };
  const ack: Ack = { ...snapshot, type: "ack", id: "2", changed: true, result: { index: 1, off: true } };
  assert.equal(validAck(command, ack), true);
  assert.equal(validAck(command, { ...ack, result: { index: 2, off: true } }), false);
  assert.equal(validAck(command, { ...ack, result: { index: 1, off: "true" } }), false);
});

for (const language of ["ko", "en"] as const) test(`master group ${language}: toggle/off/on, microphone isolation and inspector`, async t => {
  const { server, host, settings } = await setupActions(t, language, { masterPluginGroups: true });
  const beforeMics = structuredClone(server.snapshot.state.channels);
  const title = displayName(`${strings[language].master} · ${strings[language].group} 1`);
  host.appear("master", settings, 0, kind);
  await host.wait(() => host.visual("master").title === title && host.visual("master").state === 1);
  for (const [i, mode] of ["toggle", "toggle", "off", "on"].entries()) {
    host.settings("master", { ...settings, mode }); host.keyDown("master");
    await until(server, "command", () => commands(server).length === i + 1);
    const off = i === 0 || i === 2;
    assert.deepEqual(commands(server)[i].args, { index: 1, off });
    assert.equal(commands(server)[i].command, "setMasterPluginGroupOff");
    await host.wait(() => host.visual("master").state === (off ? 0 : 1));
  }
  assert.deepEqual(server.snapshot.state.channels, beforeMics);
  const pi = await options(host, "master");
  assert.deepEqual(pi.pi.messages.at(-1)!.payload.groupIndices, [1, 2]);
  await host.closeInspector("master");
  host.appear("all", settings, 0, "plugin-group-all"); host.keyDown("all");
  await until(server, "command", () => commands(server, "setPluginGroupOffEverywhere").length === 1);
  assert.equal(server.snapshot.state.master.pluginGroups[0].off, false);
  assert.equal(alerts(host, "master").length, 0);
});

for (const language of ["ko", "en"] as const) test(`master group ${language}: absent group shows the existing error`, async t => {
  const { server, host, settings } = await setupActions(t, language, { masterPluginGroups: true });
  host.appear("missing", { ...settings, groupIndex: 5 }, 0, kind);
  await host.wait(() => host.visual("missing").title === statusTitle(strings[language].missingGroup));
  host.keyDown("missing"); await host.wait(() => alerts(host, "missing").length === 1);
  assert.equal(commands(server).length, 0);
});

for (const language of ["ko", "en"] as const) test(`LiveMix 0.22.0 without masterPluginGroups asks for an update (${language}) and sends no command`, async t => {
  const { server, host } = await setupActions(t, language, { masterPluginGroups: false });
  host.appear("old", {}, 0, kind);
  await host.wait(() => host.visual("old").title === statusTitle(strings[language].unsupported));
  host.keyDown("old"); await host.wait(() => alerts(host, "old").length === 1);
  assert.equal(commands(server).length, 0);
});
