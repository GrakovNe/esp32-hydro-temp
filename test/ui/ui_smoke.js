/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
const fs = require("fs");
const vm = require("vm");
const path = require("path");
const WEB = path.join(__dirname, "..", "..", "web");

function mockClassList(el) {
  const has = c => el.className.split(/\s+/).includes(c);
  return {
    add: c => { if (!has(c)) el.className = (el.className + " " + c).trim(); },
    remove: c => { el.className = el.className.split(/\s+/).filter(x => x && x !== c).join(" "); },
    contains: has,
  };
}

function mockEl() {
  const el = {
    textContent: "", className: "", value: "", checked: false, disabled: false,
    innerHTML: "", style: {}, dataset: {}, type: "text", href: "",
    children: [],
    appendChild(c) { this.children.push(c); },
    addEventListener(name, fn) { (this.listeners ||= {})[name] = fn; },
    setAttribute(k, v) { (this.attrs ||= {})[k] = v; },
    getBoundingClientRect() { return { width: 600, height: 190, left: 0, top: 0 }; },
    querySelectorAll() { return []; },
    querySelector() { return null; },
  };
  const cl = mockClassList(el);
  cl.toggle = (c, on) => { const want = on === undefined ? !cl.contains(c) : on; if (want) cl.add(c); else cl.remove(c); };
  el.classList = cl;
  return el;
}

function runPage(file, apiData) {
  const html = fs.readFileSync(file, "utf8");
  const js = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  const els = {};
  const fetchCalls = [];
  const doc = {
    getElementById: (id) => (els[id] ||= mockEl()),
    createElement: () => mockEl(),
    querySelectorAll: () => [],
    body: mockEl(),
    title: "",
  };
  const ctx = {
    document: doc,
    window: { addEventListener() {} },
    location: { origin: "http://hydrotemp-9390.local", href: "", reload() {} },
    fetch: (url, opts) => {
      fetchCalls.push(url + (opts && opts.method ? " " + opts.method : ""));
      const key = url.split("?")[0];
      if (apiData[key] === undefined) {
        return Promise.reject(new Error("network down"));
      }
      return Promise.resolve({ ok: true, json: async () => apiData[key] });
    },
    setInterval: () => 0,
    clearInterval: () => {},
    setTimeout: () => 0,
    console, Date, Math, JSON, String, Number, Array, Promise, Set,
  };
  vm.createContext(ctx);
  vm.runInContext(js, ctx, { filename: file });
  return new Promise((resolve) => setImmediate(() => setImmediate(() => setImmediate(() =>
    resolve({ els, fetchCalls, ctx, doc })))));
}

let fails = 0;
function check(cond, name) {
  if (cond) console.log("PASS " + name);
  else { fails++; console.log("FAIL " + name); }
}

(async () => {
  const now = Math.floor(Date.now() / 1000);
  const sensorA = { slot: "a", name: "Mint", low: 18, high: 23, present: true, temperature: 21.37, age_s: 40, out_of_range: false,
                    rom: "28216531054AC79B", errors: 0, last_reading: now - 40 };
  const sensorB = { slot: "b", name: "Sweet Potato", low: 18, high: 23, present: true, temperature: 23.04, age_s: 40, out_of_range: false,
                    rom: "282167313A7ED4F0", errors: 2, last_reading: now - 40 };
  const status = {
    time: { timestamp: now, synced: true, uptime_s: 5231 },
    device: {
      name: "Tank", id: "9390", hostname: "tank", chip: "ESP32", revision: "v3.1", cores: 2, cpu_mhz: 160,
      firmware: "abc1234", idf: "v5.5.5", build_date: "Oct 25 2026", mac: "28:05:a5:e1:93:90",
      flash_bytes: 4194304, heap_free: 180000, heap_min_free: 150000, reset_reason: "power_on",
    },
    mode: "normal",
    bus: { ok: true, found: 2, polls: 17, poll_interval_s: 300 },
    sensors: [sensorA, { ...sensorB, temperature: 24.5, out_of_range: true }],
    wifi: { connected: true, ssid: "Home", bssid: "aa:bb:cc:dd:ee:ff", rssi: -58, channel: 6,
            ip: "192.168.1.42", gateway: "192.168.1.1", last_disconnect: "other" },
    mqtt: { enabled: true, connected: true, last_publish: now - 10, errors: 0 },
  };

  const cur = { timestamp: now, time_synced: true, uptime_s: 5231, poll_interval_s: 300, sensors: [sensorA, sensorB] };
  const hist = {
    step_s: 300, sensors: ["Mint", "Sweet Potato"],
    records: [
      { t: now - 300, v: [21.4, 24.6] },
      { t: now - 3600, v: [22.0, null] },
      { t: now - 10000, v: [17.5, 23.9] },
    ],
  };
  const m = await runPage(path.join(WEB, "main.html"), {
    "/api/v1/current": cur, "/api/v1/history": hist, "/api/v1/status": status,
  });
  check(m.els.lab0.textContent === "Mint", "main: first label is the sensor name");
  check(m.els.lab1.textContent === "Sweet Potato", "main: second label");
  check(m.els.t0.textContent === "21.4", "main: temp one decimal");
  check(m.els.t0fill.className === "fill good", "main: in-range green");
  check(m.els.t0d.innerHTML.includes("In range"), "main: in-range word");
  check(m.els.t1.textContent === "23.0", "main: 23.04 shows as 23.0");
  check(m.els.t1fill.className === "fill good", "main: verdict from the device, not the raw value");
  const mb = await runPage(path.join(WEB, "main.html"), {
    "/api/v1/current": { ...cur, sensors: [sensorA, { ...sensorB, temperature: 17.95, out_of_range: true }] },
    "/api/v1/history": hist, "/api/v1/status": status,
  });
  check(mb.els.t1.textContent === "18.0", "main: 17.95 rounds half away from zero");
  check(mb.els.t1fill.className === "fill cold", "main: out of range below low is cold");
  check(mb.els.t1d.innerHTML.includes("Too cold"), "main: too cold word");
  check(m.els.t0ticks.innerHTML.includes("18.0") && m.els.t0ticks.innerHTML.includes("23.0"), "main: range ticks");
  check(m.els.upd.textContent.startsWith("Updated "), "main: updated line");
  check(m.els.upd.textContent.includes("every 5 min"), "main: poll period in footer");
  check(m.els.fw.textContent.includes("abc1234"), "main: firmware footer");
  check((m.els["chart-0"].innerHTML.match(/<rect/g) || []).length === 3, "main: 3 bars for sensor 1");
  check((m.els["chart-1"].innerHTML.match(/<rect/g) || []).length === 2, "main: null skipped for sensor 2");
  check(m.els["chart-0"].innerHTML.includes("bar cold"), "main: cold bar colored");
  check(m.els["chart-1"].innerHTML.includes("bar bad"), "main: warm bar colored");
  check(m.els.t0d.innerHTML.includes("40 s ago"), "main: age in seconds");
  check(m.els["chart-0"].innerHTML.includes(">18.0<") && m.els["chart-0"].innerHTML.includes(">23.0<"), "main: zone lines labelled");
  check(m.els["stats-0"].innerHTML.includes("max <b>22.0"), "main: max stat");
  check(m.doc.title === "Tank", "main: title is the device name");
  check(m.els.devname.textContent === "Tank", "main: header is the device name");

  const m2 = await runPage(path.join(WEB, "main.html"), {});
  check(m2.els.upd.textContent === "Device not reachable", "main: unreachable footer");
  check(m2.els.upd.className === "err", "main: unreachable red");

  const cur2 = { ...cur, time_synced: false, timestamp: null,
                 sensors: [{ ...sensorA, temperature: null, age_s: null, out_of_range: null },
                           { ...sensorB, present: false, temperature: null, age_s: null, out_of_range: null }] };
  const m3 = await runPage(path.join(WEB, "main.html"), {
    "/api/v1/current": cur2, "/api/v1/history": hist, "/api/v1/status": status,
  });
  check(m3.els.t0.textContent === "—", "main: null dash");
  check(m3.els.t0d.innerHTML.includes("No data yet"), "main: present but no reading");
  check(m3.els.t0d.className === "d", "main: no reading is neutral");
  check(m3.els.t1d.className === "d err", "main: missing sensor is red");
  check(m3.els.t1d.innerHTML.includes("sensor not found"), "main: missing sensor text");
  check(m3.els.t0fill.style.width === "0", "main: empty track");
  check(m3.els.upd.textContent.includes("clock not synced"), "main: clock line");

  const m4 = await runPage(path.join(WEB, "main.html"), {
    "/api/v1/current": { ...cur, poll_interval_s: 90 }, "/api/v1/history": hist, "/api/v1/status": status,
  });
  check(m4.els.upd.textContent.includes("every 1.5 min"), "main: fractional poll period");
  const m5 = await runPage(path.join(WEB, "main.html"), {
    "/api/v1/current": { ...cur, poll_interval_s: 20 }, "/api/v1/history": hist, "/api/v1/status": status,
  });
  check(m5.els.upd.textContent.includes("every 20 s"), "main: poll period in seconds");

  const cfg = {
    device: { name: "Tank" },
    wifi: { ssid: "Home", password_set: true },
    time: { ntp_server: "pool.ntp.org" },
    sensors: [{ name: "Mint", low: 18, high: 23 }, { name: "Sweet Potato", low: 17.5, high: 22 }],
    polling: { interval_s: 300 },
    mqtt: {
      enabled: true, host: "mqtt.test", port: 1884, username: "u",
      password_set: true, tls: false, ca_cert_set: true, skip_verify: false,
      topic_prefix: "esp32-hydro-temp/9390", discovery: true,
    },
  };
  const i = await runPage(path.join(WEB, "info.html"), { "/api/v1/status": status, "/api/v1/config": cfg });
  check(i.els["l-s0"].textContent === "Mint", "info: sensor label");
  check(i.els["s-s0"].textContent === "In range", "info: sensor state");
  check(i.els["s-s0"].className === "state ok", "info: sensor green");
  check(i.els["s-s1"].textContent === "Out of range", "info: out of range state");
  check(i.els["s-s1"].className === "state err", "info: out of range red");
  check(i.els["v-s0-temp"].textContent === "21.37 °C · 40 s ago", "info: temperature row");
  check(i.els["v-s0-range"].textContent === "18.0 – 23.0 °C", "info: range row");
  check(i.els["v-s0-rom"].textContent === "28 21 65 31 05 4A C7 9B", "info: rom row");
  check(i.els["v-s1-err"].textContent === "2", "info: errors row");
  check(i.els["v-found"].textContent === "2", "info: bus found");
  check(i.els["v-poll"].textContent === "300 s", "info: poll interval");
  check(i.els["v-rssi"].textContent === "-58 dBm", "info: rssi row");
  check(i.els["v-host"].textContent === "tank.local", "info: hostname row");
  check(i.els["v-broker"].textContent === "mqtt.test : 1884", "info: broker from config");
  check(i.els["v-topics"].textContent === "esp32-hydro-temp/9390/mint · esp32-hydro-temp/9390/sweet_potato", "info: topics from config");
  check(i.els["v-pub"].textContent === "10 s ago", "info: last publish");
  check(i.els["v-ntp"].textContent === "pool.ntp.org", "info: ntp from config");
  check(i.els["v-up"].textContent === "1 h 27 min", "info: uptime");
  check(i.els["v-flash"].textContent === "4.0 MB", "info: flash size");
  check(i.els["v-reset"].textContent === "Power on", "info: reset reason");
  check(i.els["v-fw"].textContent === "abc1234", "info: firmware");

  const st2 = { ...status,
                sensors: [{ ...sensorA, temperature: null, age_s: null, out_of_range: null },
                          { ...sensorB, present: false, rom: null, temperature: null, age_s: null, out_of_range: null }],
                mqtt: { enabled: false, connected: false, last_publish: null, errors: 0 },
                time: { ...status.time, synced: false, timestamp: null } };
  const i2 = await runPage(path.join(WEB, "info.html"), { "/api/v1/status": st2, "/api/v1/config": cfg });
  check(i2.els["s-s0"].textContent === "No reading yet", "info: no reading state");
  check(i2.els["s-s0"].className === "state", "info: no reading is neutral");
  check(i2.els["s-s1"].textContent === "Not found", "info: missing state");
  check(i2.els["s-s1"].className === "state err", "info: missing red");
  check(i2.els["v-s1-rom"].textContent === "—", "info: rom dash");
  check(i2.els["s-mqtt"].textContent === "Off", "info: mqtt off");
  check(i2.els["s-mqtt"].className === "state", "info: mqtt off is neutral");
  check(i2.els["s-time"].textContent === "Not synced", "info: time not synced");
  check(i2.els["v-now"].textContent === "—", "info: no device time");

  const i3 = await runPage(path.join(WEB, "info.html"), {});
  check(i3.els.upd.textContent === "Device not reachable", "info: unreachable footer");

  const s = await runPage(path.join(WEB, "setup.html"), {
    "/api/v1/status": status,
    "/api/v1/config": cfg,
    "/api/v1/wifi/scan": { scanning: false, networks: [{ ssid: "Home", rssi: -50, secure: true },
                                                       { ssid: "Guest", rssi: -70, secure: false }] },
  });
  check(s.els.name.value === "Tank", "setup: device name");
  check(s.els.hostprev.textContent === "http://tank.local/", "setup: hostname preview");
  check(s.els.passfor.textContent === "Home", "setup: password for the saved ssid");
  check(s.els.passhint.textContent.includes("leave empty"), "setup: pass hint");
  check(s.els.ntp.value === "pool.ntp.org", "setup: ntp");
  check(s.els.s0_name.value === "Mint", "setup: sensor 1 name");
  check(s.els.s1_name.value === "Sweet Potato", "setup: sensor 2 name");
  check(s.els.s1_low.value === 17.5 && s.els.s1_high.value === 22, "setup: sensor 2 range");
  check(s.els.poll.value === 300, "setup: poll interval");
  check(s.els.mq_en.checked === true, "setup: mqtt enabled");
  check(s.els.mq_host.value === "mqtt.test", "setup: mqtt host");
  check(s.els.mq_port.value === 1884, "setup: mqtt port");
  check(s.els.mq_pfx.value === "esp32-hydro-temp/9390", "setup: topic prefix");
  check(s.els.mqpfxhint.textContent.includes("esp32-hydro-temp/9390/sweet_potato"), "setup: topic preview");
  check(s.els.mq_disc.checked === true, "setup: discovery");
  check(s.els.mq_skip.disabled === true, "setup: skip disabled without TLS");
  check(s.els.cawrap.style.display === "none", "setup: CA hidden without TLS");
  check(s.els.nets.children.length === 2, "setup: scan list rendered");
  check(s.els.nets.children[0].className === "net sel", "setup: saved network selected");
  check(s.els.h1 === undefined, "setup: title untouched on LAN");
  check(s.els.devname.textContent === "Tank", "setup: header is the device name");

  s.els.nets.children[1].onclick();
  check(s.els.passwrap.style.display === "none", "setup: open network hides password");
  check(s.els.savebar.className.includes("show"), "setup: save bar appears");
  check(s.els.nchanges.textContent === "1 change", "setup: one change counted");
  s.els.s0_low.value = "19.5";
  await s.els.save.onclick();
  const post = s.fetchCalls.find(c => c.startsWith("/api/v1/config POST"));
  check(!!post, "setup: config POSTed");
  check(s.els.rtitle.textContent === "Restarting", "setup: restart screen after save");

  let posted = null;
  const s3 = await runPage(path.join(WEB, "setup.html"), {
    "/api/v1/status": status, "/api/v1/config": cfg,
    "/api/v1/wifi/scan": { scanning: false, networks: [] },
  });
  s3.ctx.fetch = (url, opts) => {
    if (opts && opts.method === "POST") posted = JSON.parse(opts.body);
    return Promise.resolve({ ok: true, json: async () => ({ ok: true, url: "http://tank.local/" }) });
  };
  s3.els.s1_name.value = "Potato";
  s3.els.s1_high.value = "24";
  s3.els.s1_low.value = "";
  s3.els.poll.value = "120";
  await s3.els.save.onclick();
  check(posted && posted.sensors[1].name === "Potato", "setup: posts sensor name");
  check(posted && posted.sensors[1].high === 24 && !("low" in posted.sensors[1]), "setup: empty range field is omitted");
  check(posted && posted.sensors[0].low === 18, "setup: filled range field posted as number");
  check(posted && posted.polling.interval_s === 120, "setup: posts poll interval");
  check(posted && posted.mqtt.topic_prefix === "esp32-hydro-temp/9390", "setup: posts topic prefix");

  const s4 = await runPage(path.join(WEB, "setup.html"), {
    "/api/v1/status": { ...status, mode: "provisioning" },
    "/api/v1/config": { ...cfg, wifi: { ssid: "", password_set: false }, mqtt: { ...cfg.mqtt, enabled: false } },
    "/api/v1/wifi/scan": { scanning: false, networks: [] },
  });
  check(s4.els.h1.textContent === "Set up HydroTemp", "setup: first-run title");
  check(s4.els.nav.style.display === "none", "setup: first-run hides nav");
  check(s4.els.mq_disc.disabled === true, "setup: MQTT off disables discovery");
  check(s4.els.mqbody.className.includes("dim"), "setup: MQTT off dims fields");

  console.log(fails === 0 ? "ALL OK" : fails + " FAILURES");
  process.exit(fails ? 1 : 0);
})();
