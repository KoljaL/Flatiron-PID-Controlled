#pragma once
#include <Arduino.h>

// HTML-Seite im Flash
static const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>Flatiron PID-Controlled</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'%3E%3Ccircle cx='50' cy='50' r='46' fill='%23282c34' stroke='%2361afef' stroke-width='3'/%3E%3Cpath d='M50 20 C58 35 68 54 68 70 C68 76 63 80 50 80 C37 80 32 76 32 70 C32 54 42 35 50 20 Z' fill='%233e4452' stroke='%23abb2bf' stroke-width='3'/%3E%3Cpath d='M50 27 C56 39 64 56 64 68 C64 72 60 74 50 74 C40 74 36 72 36 68 C36 56 44 39 50 27 Z' fill='%2321252b'/%3E%3Cg fill='%2361afef'%3E%3Ccircle cx='50' cy='36' r='2'/%3E%3Ccircle cx='45' cy='45' r='2'/%3E%3Ccircle cx='55' cy='45' r='2'/%3E%3Ccircle cx='42' cy='54' r='2'/%3E%3Ccircle cx='50' cy='54' r='2'/%3E%3Ccircle cx='57' cy='54' r='2'/%3E%3Ccircle cx='40' cy='63' r='2'/%3E%3Ccircle cx='46' cy='63' r='2'/%3E%3Ccircle cx='54' cy='63' r='2'/%3E%3Ccircle cx='60' cy='63' r='2'/%3E%3C/g%3E%3C/svg%3E">
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        :root { --bg: #282c34; --card: #21252b; --fg: #abb2bf; --head: #e6e6e6; --border: #3e4451;
                --blue: #61afef; --red: #e06c75; --green: #98c379; --yellow: #e5c07b; --purple: #c678dd; --cyan: #56b6c2; --orange: #d19a66; }
        body { font-family: Arial, sans-serif; text-align: center; margin: 20px; background: var(--bg); color: var(--fg); }
        .page-header { display: flex; align-items: center; justify-content: flex-start; gap: 14px; position: sticky; top: 0; z-index: 10; width: 80%; box-sizing: border-box; min-height: 76px; margin: 0 auto 24px; padding: 8px 0; background: var(--bg); }
        .page-header h1 { display: flex; align-items: baseline; gap: 9px; margin: 0; }
        .brand-main { font-size: 1.35em; }
        .brand-sub { font-size: .75em; }
        .logo { flex: 0 0 auto; width: 68px; height: 68px; }
        .cards { display: flex; align-items: stretch; justify-content: space-between; gap: 20px; width: 80%; margin: 24px auto 32px; flex-wrap: wrap; }
        .card { display: flex; flex-direction: column; align-items: center; background: var(--card); padding: 20px; border-radius: 8px; border: 1px solid var(--border); box-shadow: 0 4px 8px rgba(0,0,0,0.3); min-width: 250px; box-sizing: border-box; }
        .card h3 { margin-top: 0; }
        h1, h3 { color: var(--head); }
        .value { font-size: 24px; font-weight: bold; color: var(--head); }
        .btn { padding: 10px 20px; background: var(--blue); color: #282c34; border: none; border-radius: 4px; cursor: pointer; font-weight: bold; }
        .btn-tune { background: var(--red); }
        .temperature-card { flex-direction: row; align-items: stretch; gap: 24px; margin-left: auto; padding: 8px 16px; }
        .temperature-card > div { min-width: 150px; }
        .temperature-card h3 { margin: 0 0 4px; font-size: 14px; }
        .temperature-card .value { margin: 0; }
        .pid-card { flex-direction: row; flex-wrap: wrap; align-items: center; justify-content: center; gap: 12px 32px; width: 80%; margin: 24px auto; }
        .pid-card h3, .pid-card p { margin: 0; }
        .setpoint-control { display: flex; align-items: center; justify-content: center; gap: 10px; }
        .setpoint-control button { width: 32px; height: 32px; padding: 0 0 2px; border: 1px solid var(--border); border-radius: 50%; background: var(--blue); color: var(--bg); font-size: 20px; line-height: 1; cursor: pointer; }
        .setpoint-control .value { min-width: 82px; margin: 0; }
        #tuning-report { width: 80%; box-sizing: border-box; margin: 24px auto; padding: 16px; text-align: left; background: var(--card); border: 1px solid var(--border); border-radius: 8px; }
        #tuning-report select { max-width: 100%; margin: 12px 0; padding: 8px; }
        #tuning-report pre { max-height: 32rem; overflow: auto; white-space: pre-wrap; overflow-wrap: anywhere; }
        #tuning-log { width: 80%; box-sizing: border-box; margin: 24px auto; padding: 16px; text-align: left; background: var(--card); border: 1px solid var(--border); border-radius: 8px; }
        #tuning-log pre { max-height: 16rem; overflow: auto; white-space: pre-wrap; overflow-wrap: anywhere; font-size: 13px; }
        #chart-container { width: 80%; box-sizing: border-box; margin: 0 auto; background: var(--card); padding: 20px; border-radius: 8px; border: 1px solid var(--border); }
        @media (max-width: 600px) { .page-header, #tuning-report, #tuning-log, #chart-container { width: 100%; } .logo { width: 52px; height: 52px; } .page-header h1 { font-size: 24px; } .page-header { flex-wrap: wrap; } .temperature-card { flex-direction: row; gap: 10px; margin-left: 0; width: 100%; min-width: 0; } .temperature-card > div { min-width: 0; flex: 1; } .pid-card { width: 100%; } }
    </style>
</head>
<body>
    <header class="page-header">
        <svg class="logo" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" aria-label="Flatiron logo">
            <circle cx="50" cy="50" r="46" fill="#282c34" stroke="#61afef" stroke-width="3"/>
            <path d="M50 20 C58 35 68 54 68 70 C68 76 63 80 50 80 C37 80 32 76 32 70 C32 54 42 35 50 20 Z" fill="#3e4452" stroke="#abb2bf" stroke-width="3" stroke-linejoin="round"/>
            <path d="M50 27 C56 39 64 56 64 68 C64 72 60 74 50 74 C40 74 36 72 36 68 C36 56 44 39 50 27 Z" fill="#21252b"/>
            <g fill="#61afef">
                <circle cx="50" cy="36" r="2"/><circle cx="45" cy="45" r="2"/><circle cx="55" cy="45" r="2"/>
                <circle cx="42" cy="54" r="2"/><circle cx="50" cy="54" r="2"/><circle cx="57" cy="54" r="2"/>
                <circle cx="40" cy="63" r="2"/><circle cx="46" cy="63" r="2"/><circle cx="54" cy="63" r="2"/><circle cx="60" cy="63" r="2"/>
            </g>
        </svg>
        <h1><span class="brand-main">Flatiron</span><span class="brand-sub">pid controlled</span></h1>
    <section class="card temperature-card">
        <div>
            <h3>Ist-Temperatur</h3>
            <p class="value"><span id="temp">--</span> °C</p>
        </div>
        <div>
            <h3>Soll-Temperatur</h3>
            <div class="setpoint-control">
                <button id="setpoint-down" type="button" onclick="changeSetpoint(-5)" aria-label="Solltemperatur um 5 Grad senken">−</button>
                <p class="value"><span id="setpoint">--</span> °C</p>
                <button id="setpoint-up" type="button" onclick="changeSetpoint(5)" aria-label="Solltemperatur um 5 Grad erhöhen">+</button>
            </div>
        </div>
    </section>
    </header>
    <div id="chart-container">
        <canvas id="tempChart"></canvas>
    </div>
    <section class="card pid-card">
        <h3>PID Parameter</h3>
        <p>P: <span id="kp">--</span> | I: <span id="ki">--</span> | D: <span id="kd">--</span></p>
        <p>Ausgang: <span id="output">--</span>%</p>
        <p id="heater-state" role="status">Status wird geladen</p>
        <p id="tuning-status"></p>
        <p id="tuning-suggestion"></p>
        <button id="heater-button" class="btn" onclick="toggleHeater()">Heizung einschalten</button>
        <button id="tune-button" class="btn btn-tune" onclick="toggleAutotune()">Autotune starten</button>
    </section>
    <details id="tuning-log" open>
        <summary>Autotune-Log</summary>
        <pre id="tuning-log-content"></pre>
    </details>
    <details id="tuning-report" hidden>
        <summary>Gespeicherte Autotune-Abschlussberichte</summary>
        <label for="tuning-report-list">Bericht auswählen:</label>
        <select id="tuning-report-list" onchange="showStoredTuningReport(this.value)"></select>
        <pre id="tuning-report-content"></pre>
    </details>
    <script>
        const C = { fg: '#abb2bf', grid: '#3e4451' };
        Chart.defaults.color = C.fg;
        Chart.defaults.borderColor = C.grid;

        const pad = n => String(n).padStart(2, '0');
        const hhmm = t => { const d = new Date(t); return pad(d.getHours()) + ':' + pad(d.getMinutes()); };
        const MIN = 60000, STEP = 5 * MIN;
        const REPORT_INDEX_KEY = 'autotune-report-index';
        let activeTuneReport = null;
        let lastLoggedStatus = null, lastLoggedFault = null, lastBlockers = null;

        function logTune(message) {
            const pre = document.getElementById('tuning-log-content');
            const lines = pre.textContent ? pre.textContent.split('\n') : [];
            lines.push(new Date().toLocaleTimeString() + '  ' + message);
            pre.textContent = lines.slice(-200).join('\n');
            pre.scrollTop = pre.scrollHeight;
        }
        function startBlockers(data) {
            const reasons = [];
            if (data.fault) reasons.push('Fehler verriegelt (' + data.faultReason + ') – lokal am Taster quittieren');
            if (!data.validReading) reasons.push('keine aktuelle Sensormessung (Sensor: ' + data.sensorState + ')');
            else if (Number(data.temp) > data.tuningConfig.maxStartTemperatureC)
                reasons.push('Platte zu warm (' + Number(data.temp).toFixed(1) + ' °C, max. ' + data.tuningConfig.maxStartTemperatureC + ' °C)');
            if (Number(data.setpoint) < data.tuningConfig.setpointMinC || Number(data.setpoint) > data.tuningConfig.setpointMaxC)
                reasons.push('Sollwert ' + data.setpoint + ' °C außerhalb ' + data.tuningConfig.setpointMinC + '–' + data.tuningConfig.setpointMaxC + ' °C');
            return reasons;
        }
        let tuneLogStart = 0, tuneLogLast = 0, wasTuning = false;
        function logTuneProgress(data) {
            const now = Date.now();
            if (data.tuning) {
                if (!wasTuning) { tuneLogStart = now; tuneLogLast = 0; }
                if (now - tuneLogLast >= 5000) {
                    tuneLogLast = now;
                    const elapsed = Math.round((now - tuneLogStart) / 1000);
                    const total = Math.round((data.tuningConfig?.timeoutMs || 0) / 1000);
                    logTune('Lauf ' + elapsed + ' s' + (total ? ' (Rest bis Timeout ' + Math.max(0, total - elapsed) + ' s)' : '') +
                        ' | Ist ' + data.temp.toFixed(1) + ' °C | Soll ' + data.setpoint.toFixed(1) +
                        ' °C | Ausgang ' + data.output.toFixed(1) + ' %');
                }
            } else if (wasTuning && data.tuningSuggestion) {
                logTune('Vorschlag: Kp ' + data.suggestedKp.toFixed(3) + ', Ki ' + data.suggestedKi.toFixed(4) +
                    ', Kd ' + data.suggestedKd.toFixed(2) + ' (nicht aktiv)');
            }
            wasTuning = data.tuning;
        }
        function logStateChanges(data) {
            logTuneProgress(data);
            if (data.tuningStatus !== lastLoggedStatus) {
                logTune('Status: ' + data.tuningStatus);
                lastLoggedStatus = data.tuningStatus;
            }
            const fault = data.fault ? data.faultReason : null;
            if (fault !== lastLoggedFault) {
                logTune(fault ? 'FEHLER: ' + fault + ' (Heizung aus, verriegelt)' : 'Fehler quittiert');
                lastLoggedFault = fault;
            }
            const blockers = data.tuning ? [] : startBlockers(data);
            const key = blockers.join('|');
            if (key !== lastBlockers) {
                if (blockers.length) logTune('Start nicht möglich: ' + blockers.join('; '));
                else if (lastBlockers) logTune('Start möglich');
                lastBlockers = key;
            }
        }

        // Zeichnet Nebenmarkierungen jede Minute unter die X-Achse
        const minorTicks = {
            id: 'minorTicks',
            afterDraw(c) {
                const x = c.scales.x, ctx = c.ctx;
                ctx.save();
                ctx.strokeStyle = C.fg;
                ctx.beginPath();
                for (let t = Math.ceil(x.min / MIN) * MIN; t <= x.max; t += MIN) {
                    const px = x.getPixelForValue(t);
                    ctx.moveTo(px, x.top);
                    ctx.lineTo(px, x.top + 4);
                }
                ctx.stroke();
                ctx.restore();
            }
        };

        const ds = (label, color, extra) => Object.assign({ label, data: [], borderColor: color, backgroundColor: color,
            borderWidth: 2, pointRadius: 0, pointHoverRadius: 0, fill: false, tension: 0 }, extra);

        function readTuningReportIndex() {
            const raw = localStorage.getItem(REPORT_INDEX_KEY);
            if (raw === null) return [];
            const index = JSON.parse(raw);
            if (!Array.isArray(index)) throw new Error('Autotune-Berichtsindex ist ungueltig');
            return index;
        }
        function showStoredTuningReport(key) {
            try {
                const report = JSON.parse(localStorage.getItem(key));
                if (!report) throw new Error('Gespeicherter Autotune-Bericht wurde nicht gefunden');
                document.getElementById('tuning-report-content').textContent = JSON.stringify(report, null, 2);
            } catch (error) {
                document.getElementById('tuning-status').innerText = 'Bericht konnte nicht geladen werden: ' + error.message;
            }
        }
        function refreshTuningReportList(selectedKey) {
            const index = readTuningReportIndex();
            const details = document.getElementById('tuning-report');
            const select = document.getElementById('tuning-report-list');
            select.replaceChildren();
            for (const entry of index.slice().reverse()) {
                const option = document.createElement('option');
                option.value = entry.key;
                option.textContent = entry.finishedAt + ' - ' + entry.outcome + ' - ' + entry.status;
                select.appendChild(option);
            }
            details.hidden = index.length === 0;
            if (index.length) {
                select.value = selectedKey || index[index.length - 1].key;
                if (selectedKey) details.open = true;
                showStoredTuningReport(select.value);
            }
        }
        function initializeTuningReports() {
            try {
                refreshTuningReportList();
            } catch (error) {
                document.getElementById('tuning-status').innerText = 'Berichtsspeicher nicht lesbar: ' + error.message;
            }
        }
        function beginTuningReport(data) {
            const now = Date.now();
            activeTuneReport = {
                startedAt: new Date(now).toISOString(),
                startedAtEpochMs: now,
                captureIntervalMs: 1000,
                startState: data,
                tuningConfig: data.tuningConfig,
                samples: []
            };
        }
        function collectTuningSample(data) {
            const now = Date.now();
            activeTuneReport.samples.push({
                timestamp: new Date(now).toISOString(),
                elapsedMs: now - activeTuneReport.startedAtEpochMs,
                state: data
            });
        }
        async function completeTuningReport(data) {
            const report = activeTuneReport;
            activeTuneReport = null;
            const finished = Date.now();
            report.finishedAt = new Date(finished).toISOString();
            report.durationMs = finished - report.startedAtEpochMs;
            report.finalState = data;
            report.outcome = data.tuningStatus.startsWith('Abgeschlossen') ? 'Erfolgreich' :
                data.tuningStatus.startsWith('Fehler:') ? 'Fehler' : 'Abgebrochen';
            const temperatures = report.samples
                .filter(sample => sample.state.temp !== null && Number.isFinite(sample.state.temp))
                .map(sample => sample.state.temp);
            const outputs = report.samples.map(sample => Number(sample.state.output)).filter(Number.isFinite);
            report.summary = {
                sampleCount: report.samples.length,
                durationSeconds: report.durationMs / 1000,
                minimumTemperatureC: temperatures.length ? Math.min(...temperatures) : null,
                maximumTemperatureC: temperatures.length ? Math.max(...temperatures) : null,
                maximumOutputPercent: outputs.length ? Math.max(...outputs) : null
            };
            try {
                const response = await fetch('/history');
                if (!response.ok) throw new Error('Verlaufsdaten konnten nicht geladen werden (HTTP ' + response.status + ')');
                report.historyAtFinish = await response.json();
            } catch (error) {
                report.historyAtFinish = { error: error.message };
            }
            report.timeIndex = report.finishedAt;
            const key = 'autotune-report:' + report.timeIndex;
            try {
                const index = readTuningReportIndex();
                localStorage.setItem(key, JSON.stringify(report));
                index.push({
                    key,
                    startedAt: report.startedAt,
                    finishedAt: report.finishedAt,
                    outcome: report.outcome,
                    status: data.tuningStatus
                });
                localStorage.setItem(REPORT_INDEX_KEY, JSON.stringify(index));
                refreshTuningReportList(key);
                document.getElementById('tuning-status').innerText =
                    'Autotune: ' + data.tuningStatus + ' – Abschlussbericht lokal gespeichert';
            } catch (error) {
                document.getElementById('tuning-report').hidden = false;
                document.getElementById('tuning-report-content').textContent = JSON.stringify(report, null, 2);
                document.getElementById('tuning-status').innerText =
                    'Autotune beendet, Bericht konnte nicht lokal gespeichert werden: ' + error.message;
            }
        }

        let chart;
        function initChart() {
            const ctx = document.getElementById('tempChart').getContext('2d');
            chart = new Chart(ctx, {
                type: 'line',
                plugins: [minorTicks],
                data: { datasets: [
                    ds('Temperatur (°C)', '#e06c75', { yAxisID: 'y' }),
                    ds('Soll-Temperatur (°C)', '#61afef', { yAxisID: 'y' }),
                    ds('SSR PWM (0–1)', '#98c379', { yAxisID: 'y2', stepped: true })
                ] },
                options: {
                    responsive: true,
                    animation: false,
                    interaction: { mode: 'nearest', axis: 'x', intersect: false },
                    plugins: { tooltip: { callbacks: { title: i => hhmm(i[0].parsed.x) } } },
                    scales: {
                        x: {
                            type: 'linear',
                            title: { display: true, text: 'Uhrzeit' },
                            ticks: { autoSkip: false, maxRotation: 0, callback: v => hhmm(v) },
                            afterBuildTicks(axis) {
                                const t = [];
                                for (let v = Math.ceil(axis.min / STEP) * STEP; v <= axis.max; v += STEP) t.push({ value: v });
                                axis.ticks = t;
                            }
                        },
                        y: { position: 'left', title: { display: true, text: 'Temperatur (°C)' } },
                        y2: { position: 'right', beginAtZero: true, max: 1, title: { display: true, text: 'SSR PWM (0–1)' }, grid: { drawOnChartArea: false } }
                    }
                }
            });
        }
        function updateData() {
            fetch('/data').then(r => {
                if (!r.ok) throw new Error('Status konnte nicht geladen werden');
                return r.json();
            }).then(async data => {
                logStateChanges(data);
                if (data.tuning) {
                    if (!activeTuneReport) beginTuningReport(data);
                    collectTuningSample(data);
                } else if (activeTuneReport) {
                    await completeTuningReport(data);
                }
                document.getElementById('temp').innerText =
                    data.temp == null ? '--' : Number(data.temp).toFixed(1);
                document.getElementById('setpoint').innerText = data.setpoint;
                document.getElementById('kp').innerText = data.kp.toFixed(2);
                document.getElementById('ki').innerText = data.ki.toFixed(2);
                document.getElementById('kd').innerText = data.kd.toFixed(2);
                document.getElementById('output').innerText = Number(data.output).toFixed(1);
                const state = document.getElementById('heater-state');
                state.innerText = !data.controlsEnabled
                    ? 'Web-Steuerung deaktiviert'
                    : data.fault
                        ? 'HEIZUNG AUS – ' + data.faultReason + ' (verriegelt) – Sensor jetzt: ' + data.sensorState
                        : (data.tuning ? 'Autotune aktiv – Heizung wird geregelt' :
                           data.heaterEnabled ? 'Heizung eingeschaltet' : 'Heizung ausgeschaltet') +
                          ' – Sensor: ' + data.sensorState;
                document.getElementById('tuning-status').innerText = 'Autotune: ' + data.tuningStatus;
                document.getElementById('tuning-suggestion').innerText = data.tuningSuggestion
                    ? 'Vorschlag (nicht aktiv): Kp ' + data.suggestedKp.toFixed(3) +
                      ', Ki ' + data.suggestedKi.toFixed(4) + ', Kd ' + data.suggestedKd.toFixed(2)
                    : '';
                const tuneButton = document.getElementById('tune-button');
                tuneButton.innerText = data.tuning ? 'Autotune abbrechen' : 'Autotune starten';
                tuneButton.disabled = !data.controlsEnabled;
                const heaterButton = document.getElementById('heater-button');
                heaterButton.innerText = data.heaterEnabled ? 'Heizung ausschalten' : 'Heizung einschalten';
                heaterButton.disabled = !data.controlsEnabled || data.fault || data.tuning ||
                    (!data.heaterEnabled && !data.validReading);
                document.getElementById('setpoint-down').disabled = !data.controlsEnabled || data.tuning || data.fault;
                document.getElementById('setpoint-up').disabled = !data.controlsEnabled || data.tuning || data.fault;
            }).catch(error => {
                document.getElementById('heater-state').innerText = error.message;
            });
        }
        function updateGraph() {
            if (document.hidden) return;
            const controller = new AbortController();
            const timeout = setTimeout(() => controller.abort(), 5000);
            fetch('/history', { signal: controller.signal }).then(r => {
                if (!r.ok) throw new Error('Verlauf konnte nicht geladen werden (HTTP ' + r.status + ')');
                return r.json();
            }).then(h => {
                const n = h.temp.length;
                if (!n) return;
                const dt = h.interval * 1000, now = Date.now();
                const xs = h.temp.map((_, i) => now - (n - 1 - i) * dt);
                ['temp', 'setpoint', 'pwm'].forEach((k, j) => {
                    chart.data.datasets[j].data = h[k].map((y, i) => ({ x: xs[i], y }));
                });
                chart.options.scales.x.min = xs[0];
                chart.options.scales.x.max = now;
                chart.update();
            }).catch(error => {
                document.getElementById('heater-state').innerText =
                    error.name === 'AbortError' ? 'Verlauf-Timeout' : 'Verlauf: ' + error.message;
            }).finally(() => clearTimeout(timeout));
        }
        async function cancelAutotune() {
            logTune('Abbruch angefordert');
            try {
                const response = await fetch('/cancel-tune', { method: 'POST' });
                logTune((response.ok ? 'OK: ' : 'Abbruch fehlgeschlagen: ') + await response.text());
            } catch (error) {
                logTune('Abbruch fehlgeschlagen: ' + error.message);
            }
            await updateData();
        }
        async function toggleAutotune() {
            if (document.getElementById('tune-button').innerText === 'Autotune abbrechen') {
                await cancelAutotune();
                return;
            }
            await startAutotune();
        }
        async function startAutotune() {
            try {
                const state = await (await fetch('/data')).json();
                const blockers = startBlockers(state);
                if (blockers.length) {
                    logTune('Start abgelehnt: ' + blockers.join('; '));
                    document.getElementById('tuning-log').open = true;
                    return;
                }
            } catch (error) {
                logTune('Status vor Start nicht lesbar: ' + error.message);
                return;
            }
            const confirmed = confirm(
                'Autotune nur mit kalter Platte (max. 40 °C), befestigtem Thermoelement, ' +
                'unabhängiger Temperaturmessung und anwesender Person an der Netztrennung starten. ' +
                'Der Relay-Test schaltet zwischen 0 und 15 % mit ±1,5 °C Hysterese, benötigt sechs Zyklen ' +
                'und bricht bei 120 °C oder nach 10 Minuten ab. Fortfahren?'
            );
            if (!confirmed) return;
            const button = document.getElementById('tune-button');
            button.disabled = true;
            try {
                const response = await fetch('/start-tune', { method: 'POST' });
                const message = await response.text();
                document.getElementById('tuning-status').innerText = 'Autotune: ' + message;
                logTune((response.ok ? 'Start: ' : 'Start abgelehnt: ') + message);
                if (!response.ok) document.getElementById('tuning-log').open = true;
                await updateData();
            } catch (error) {
                document.getElementById('tuning-status').innerText = 'Autotune: ' + error.message;
                logTune('Start fehlgeschlagen: ' + error.message);
            } finally {
                button.disabled = false;
                await updateData();
            }
        }
        async function toggleHeater() {
            const button = document.getElementById('heater-button');
            const turningOn = button.innerText === 'Heizung einschalten';
            if (turningOn && !confirm('Heizung einschalten? Stelle sicher, dass die Heizplatte beaufsichtigt wird und eine unabhängige Temperaturabschaltung vorhanden ist.')) return;
            button.disabled = true;
            try {
                const response = await fetch('/heater?enabled=' + (turningOn ? '1' : '0'), { method: 'POST' });
                const message = await response.text();
                if (!response.ok) throw new Error(message);
                await updateData();
            } catch (error) {
                document.getElementById('heater-state').innerText = error.message;
            } finally {
                button.disabled = false;
                await updateData();
            }
        }
        function changeSetpoint(delta) {
            const current = Number(document.getElementById('setpoint').innerText);
            const value = Math.max(0, Math.min(190, current + delta));
            fetch('/setpoint?value=' + encodeURIComponent(value), { method: 'POST' }).then(response => {
                if (!response.ok) throw new Error('Solltemperatur konnte nicht gesetzt werden');
                updateData();
            }).catch(error => alert(error.message));
        }
        window.onload = () => { initChart(); initializeTuningReports(); updateData(); updateGraph(); setInterval(updateData, 1000); setInterval(updateGraph, 10000); };
    </script>
</body>
</html>
)rawliteral";
