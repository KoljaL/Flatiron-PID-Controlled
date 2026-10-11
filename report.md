# Code-Review: Flatiron PID-Controlled (Heizplatte)

**Stand:** 10. Oktober 2026
**Gegenstand:** Firmware in [src/](src/), [platformio.ini](platformio.ini), [docs/](docs/), Abgleich mit
[README.md](README.md), [bedenken.md](bedenken.md) und [bedenken-report.md](bedenken-report.md)
**Art:** reines Review. Es wurde **kein Projektcode verändert**; einzige neue Datei im Projekt ist dieser Bericht.

---

## 0. Kurzfazit

Die Firmware ist für ein Hobbyprojekt mit 2-kW-Last **überdurchschnittlich sorgfältig**: SSR beim Boot LOW,
verriegelter Fehlerzustand mit Ursache, Sollwert-/Autotune-Grenzen auf allen Eingabepfaden, Stall-Erkennung,
100-ms-Diagnose-CSV, ehrliche Doku. Die Werte aus `bedenken.md` (Kp/Ki/Kd, Autotune-Parameter, 750 ms, 190/200/215 °C)
sind **formal korrekt umgesetzt**.

Der Review zeigt aber, dass **mehrere Dinge nicht das tun, was README und Bedenken-Dokumente beschreiben bzw.
beabsichtigen**. Die wichtigsten:

| #      | Kernaussage                                                                                                                                                                                                                                                                                             |
| ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 🔴 F-05 | Ein „festhängender“ Sensorwert (z. B. `0,00 °C` bei gebrochener SO/SCK-Leitung) wird als **gültig** akzeptiert → Dauerheizen, Software-Not-Aus (215 °C) kann nie greifen.                                                                                                                               |
| 🔴 F-06 | Die 750-ms-Sensor-Timeouts sind **konstruktionsbedingt unerreichbar** (synchrone Messung). Gegen einen echten Firmware-Hänger (Soft-WDT ≈ 3,2 s) schützt die Software nicht; es gibt keinen Hardware-Watchdog.                                                                                          |
| 🟠 F-01 | **Autotune kann mit den vorgegebenen Parametern praktisch nie erfolgreich enden**: Die Bibliothek schwingt um die *Starttemperatur* (nicht um den Sollwert), und selbst 5 % Leistung (≈ 100 W) heizen die Sohle weiter hoch → Abbruch bei 120 °C/Timeout. Das Sollwert-Gate „20–80 °C“ ist wirkungslos. |
| 🟠 F-02 | Der **Integralanteil wird bei jedem Erreichen des Sollwerts gelöscht** (`SetMode(MANUAL→AUTOMATIC)`); die Regelung degeneriert zu P+D mit Dauerschwingung (Simulation: ±10–20 K Hub statt < 1 K).                                                                                                       |
| 🟠 F-03 | PID-Abtastzeit (100 ms), Sensorabtastung (250 ms) und PWM-Fenster (1 s) passen nicht zusammen; der **D-Anteil (Kd = 55) verstärkt 0,25-K-Quantisierung und reales Messrauschen** (Logs: σ = 0,25 K im Ruhezustand).                                                                                     |
| 🟠 F-04 | `/history` puffert ~42 KB im knappen Heap, mit **quadratischem Realloc-Aufwand** → Sekunden-Blockaden, Speicherfehler, Soft-WDT; das Diagramm wird nach ca. 30–60 min unzuverlässig.                                                                                                                    |
| 🟠 F-07 | Die ungeschützten POST-Endpunkte sind per **CSRF** von *jeder Webseite* im Browser eines WLAN-Teilnehmers auslösbar – nicht nur von „Geräten im WLAN“.                                                                                                                                                  |
| 🟠 F-08 | Ein **Reset/Stromausfall löscht die Fehlerverriegelung** – im direkten Widerspruch zu `bedenken-report.md` §2.5.                                                                                                                                                                                        |

Zusätzlich fehlen **Versionskontrolle (kein Git-Repo), automatisierte Tests, CI** und es existiert **kein einziger
aufgezeichneter Heiz-/Regelversuch** (in allen Logs mit Regelzeilen: SSR nie aktiv, max. 75,5 °C von außen). Die Prüfstufen 1–6 aus
`bedenken-report.md` sind damit nicht erfüllt; `README.md` sagt das korrekt.

**Empfehlung:** Bis F-05/F-06 behoben bzw. durch Hardware abgesichert sind und F-01…F-04 geklärt wurden, die Heizung nicht
mit Netzlast über Niedrigtemperaturtests hinaus betreiben und **Autotune nicht verwenden**.

**Nachtrag zum Betreiberstatus:** Die obige Empfehlung beschreibt den Stand des Reviews vom 10. Oktober 2026. Die danach umgesetzten Softwareänderungen sowie die vom Betreiber bestätigte fachmännische Abnahme der vorhandenen Schutzkette (inklusive Dead-Man-Schalter) sind in §14.1 und §15 dokumentiert. Die dort empfohlenen kontrollierten Regelversuche und die Prüfung der Regelgüte bleiben erforderlich; Autotune erst danach testen.

---

## 1. Methodik und Grenzen

| Maßnahme                                                                                                                                                        | Ergebnis                                                                              |
| --------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- |
| Vollständige Lektüre aller Quelldateien (`controller`, `ui`, `web_interface`, `web_page`, `main`, `config`, `hardware_test`)                                    | siehe Funde                                                                           |
| Lektüre der **Bibliotheksquellen** `PID_v1`, `PID_AutoTune_v0`, Adafruit `MAX6675`, `ESPAsyncWebServer` (`AsyncResponseStream`), ESP8266-Core (`cbuf`, `Print`) | Verhalten wurde am Code verifiziert, nicht angenommen                                 |
| `pio run -e nodemcuv2`                                                                                                                                          | **Build OK**; RAM 55,0 % (45 080 / 81 920 B statisch), Flash 34,5 %                   |
| `pio check` (cppcheck)                                                                                                                                          | keine echten Defekte in `src/` (nur Stil/„unusedFunction“-Artefakte)                  |
| Auswertung der 32 Logs in `logs/` (7 davon mit Regelzeilen)                                                                                                     | Rauschen, Stall-Ereignisse, Abdeckung (siehe F-03, F-24)                              |
| **Host-Simulation** (C++, Original-`PID_v1.cpp`/`PID_AutoTune_v0.cpp`, Reglerlogik 1:1 nachgebildet, Zwei-Massen-Modell der Sohle)                              | Abschnitt 5; war temporär außerhalb des Projekts und ist **nicht Teil der Lieferung** |

**Grenzen (bitte beachten):**

- Keine Hardware vorhanden/geprüft. Thermische Modellparameter (Sohle 300 J/K, Heizelement 80 J/K, UA ≈ 0,6 W/K,
  Sensorverzögerung 1 s) sind **Annahmen**. Die *qualitativen* Aussagen (Autotune oszilliert nicht, Integrator wird
  gelöscht, D-Sägezahn) folgen direkt aus Code und Bibliotheksquellen; die *Zahlenwerte* sind Größenordnungen.
- Heap-/Laufzeitaussagen zu `/history` sind aus Quellcode und Speicherbilanz **berechnet, nicht am Gerät gemessen**.
- Die Logs stammen von einem **älteren Firmware-Stand** (vor der Stall-Behandlung, siehe F-24); das aktuelle Verhalten
  ist damit nicht belegt.
- Elektrische Sicherheit (Abschnitt 3 des Bedenken-Reports) ist per Code nicht prüfbar und bleibt offen.

---

## 2. Ergebnisübersicht aller Funde

Legende: 🔴 kritisch (kann ohne weitere Schutzebene zu Überhitzung führen) · 🟠 hoch (Funktion/Sicherheit/Stabilität
deutlich beeinträchtigt) · 🟡 mittel · ⚪ niedrig/Hinweis.

| ID   | Sev. | Titel                                                                                         | Ort                                                                               |
| ---- | ---- | --------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| F-05 | 🔴    | Festhängende/Null-Messwerte werden akzeptiert; keine Heiz-Plausibilisierung                   | [controller.cpp](src/controller.cpp#L327)                                         |
| F-06 | 🔴    | Sensor-Timeouts unerreichbar; kein Schutz vor Firmware-Hänger                                 | [controller.cpp](src/controller.cpp#L349)                                         |
| F-01 | 🟠    | Autotune konvergiert praktisch nie; Sollwert wird ignoriert; ZN-PID neigt zu Überschwingen    | [controller.cpp](src/controller.cpp#L191)                                         |
| F-02 | 🟠    | Integralanteil wird bei Sollwertdurchgang gelöscht                                            | [controller.cpp](src/controller.cpp#L379)                                         |
| F-03 | 🟠    | Abtastzeit-Mismatch, D-Anteil verstärkt Quantisierung/Rauschen                                | [controller.cpp](src/controller.cpp#L21)                                          |
| F-04 | 🟠    | `/history`: Heap + quadratischer Realloc; 63 % konstanter Payload                             | [web_interface.cpp](src/web_interface.cpp#L69)                                    |
| F-07 | 🟠    | CSRF/DNS-Rebinding auf Steuerrouten                                                           | [web_interface.cpp](src/web_interface.cpp#L103)                                   |
| F-08 | 🟠    | Reset löscht Fehlerverriegelung (Bedenken-Report §2.5 verletzt)                               | [controller.cpp](src/controller.cpp#L39)                                          |
| F-09 | 🟡    | Lokaler Taster schaltet **ein**, auch auf dem Startscreen („weiter“)                          | [ui.cpp](src/ui.cpp#L72)                                                          |
| F-10 | 🟡    | Nachheiz-Reserve 215→230 °C bei Vollleistung nicht ausreichend; kein prädiktiver Abschaltpfad | [config.h](src/config.h)                                                          |
| F-11 | 🟡    | `latchFault`: `Serial.printf` vor SSR-LOW                                                     | [controller.cpp](src/controller.cpp#L111)                                         |
| F-12 | 🟡    | Darstellungsfehler: Ki „0.01“, PWM-Verlauf nur 0/1                                            | [web_page.h](src/web_page.h#L351), [web_interface.cpp](src/web_interface.cpp#L88) |
| F-13 | 🟡    | Chart.js ungepinnt/ohne SRI; ohne Internet bricht das JS komplett ab                          | [web_page.h](src/web_page.h#L13)                                                  |
| F-14 | 🟡    | OLED-Texte zu breit (155 px > 128 px)                                                         | [ui.cpp](src/ui.cpp#L120)                                                         |
| F-15 | 🟡    | Autotune-Bericht nur im Browser (Tab-Pflicht, Quota, Redundanz, Text-Parsing)                 | [web_page.h](src/web_page.h#L229)                                                 |
| F-16 | 🟡    | Kein Übernehmen/Persistieren von Parametern; konstante Kp/Ki/Kd-Serien                        | [controller.cpp](src/controller.cpp#L57)                                          |
| F-17 | ⚪    | Duplizierte Konstanten, wirkungslose Schalter (`SETPOINT_ABSOLUTE_MAX`, `controlsEnabled`)    | mehrere                                                                           |
| F-18 | ⚪    | Ein einzelner ungültiger Wert verriegelt dauerhaft (Verfügbarkeit)                            | [controller.cpp](src/controller.cpp#L331)                                         |
| F-19 | ⚪    | PWM-Fenster: Pulse werden bei Ausgangsänderung zerteilt; keine Halbwellen-Quantisierung       | [controller.cpp](src/controller.cpp#L399)                                         |
| F-20 | ⚪    | Encoder: Polling ohne Entprellung/Quadraturdekodierung                                        | [ui.cpp](src/ui.cpp#L79)                                                          |
| F-21 | ⚪    | `hardware_test.cpp` wird immer mitkompiliert (2. U8g2-Instanz)                                | [hardware_test.cpp](src/hardware_test.cpp)                                        |
| F-22 | ⚪    | `platformio.ini`: ungenutzte/ungepinnte Libs, keine Flags, keine Test-Umgebung                | [platformio.ini](platformio.ini)                                                  |
| F-23 | ⚪    | Dokumentationsdrift (`bedenken-report.md` veraltet, Einzel-Absätze zu lang)                   | Doku                                                                              |
| F-24 | ⚪    | Kein Git, keine Tests, keine CI; Logs ohne Heizversuch und von altem Stand                    | Projekt                                                                           |
| F-25 | ⚪    | Plattform: ESP8266 hat keine Hardware-Sicherheitsmerkmale (Hinweis)                           | Architektur                                                                       |

---

## 3. Abgleich mit den Anforderungen

### 3.1 `bedenken.md`

| Anforderung                                                    | Status   | Anmerkung                                                                                                                                      |
| -------------------------------------------------------------- | -------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| Kp = 1,8 / Ki = 0,005 / Kd = 55                                | ✅        | [controller.cpp](src/controller.cpp#L20). Wirkung jedoch durch F-02/F-03 stark verändert.                                                      |
| Autotune: Noiseband 1,5; Basis 10 %; Sprung 5 %; Lookback 10 s | ✅ formal | [controller.cpp](src/controller.cpp#L209). **Funktional ineffektiv (F-01).**                                                                   |
| Sensor-Timeout ≤ 750 ms, Ausgang sofort 0                      | ⚠️        | Konstante 750 ms ✅, Ausgang sofort 0 bei NaN ✅ (≤ 250 ms Latenz). Timeout-Pfad selbst ist toter Code (F-06). Stuck-Werte nicht erkannt (F-05). |
| SETPOINT_MAX 190 (max. 200); MAX_TEMP 215                      | ✅        | 200 °C-Grenze ist im Code wirkungslos, weil direkt danach auf 190 geklammert wird (F-17).                                                      |
| Nachheizen/Thermal Runaway beachten                            | ⚠️        | Nur statische 215 °C. Kein prädiktiver Pfad; Reserve laut Modell bei Vollleistung unzureichend (F-10).                                         |
| PWM-Wellen im Live-Betrieb prüfen                              | ⚠️ offen  | 100-ms-Diagnose-CSV vorhanden ✅; **keine Messung, kein Auswertewerkzeug** (F-24).                                                              |

### 3.2 `bedenken-report.md`

| Abschnitt                                                                                                                        | Status               | Anmerkung                                                                                                               |
| -------------------------------------------------------------------------------------------------------------------------------- | -------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| 2.1 Zentrale Grenzwerte                                                                                                          | ✅                    | [config.h](src/config.h)                                                                                                |
| 2.2 Fail-safe-Reihenfolge (Messung → Check → sofort LOW → Latch → PID)                                                           | ✅ mit Abweichung     | `Serial.printf` läuft **vor** `digitalWrite(LOW)` (F-11).                                                               |
| 2.2 `/data` liefert `fault`, `faultReason`, `output`, `tuning`                                                                   | ✅                    |                                                                                                                         |
| 2.2 OLED „HEIZUNG AUS – Ursache“                                                                                                 | ⚠️                    | Text wird abgeschnitten (F-14).                                                                                         |
| 2.3 Fensterneustart bei Start/Fehler/Autotune-Ende                                                                               | ✅                    | `resetOutputWindow()`                                                                                                   |
| 2.3 Hochaufgelöste Protokollierung                                                                                               | ✅                    | `D`-Zeilen alle 100 ms. Kein Auswerte-Tooling.                                                                          |
| 2.4 Autotune begrenzen, Start verweigern, Zeit-/Temperaturgrenze, Ergebnisprüfung, nur Vorschlag                                 | ✅ formal             | Aber F-01; Vorschlag nicht übernehmbar (F-16).                                                                          |
| 2.5 Taster als **Aus-/Quittiertaster**                                                                                           | ⚠️                    | Taster schaltet auch **ein** (F-09; README dokumentiert das bewusst).                                                   |
| 2.5 „Ein Reset darf niemals einen übertemperaturbedingten Fehler ohne Abkühl-/Ursachenprüfung freigeben“                         | ❌                    | Fehler nur im RAM (F-08).                                                                                               |
| 2.5 „Webzugriffe dürfen Heizung nicht stillschweigend starten“; Zugriffsschutz oder Deaktivierung der Steuerrouten „vorzuziehen“ | ⚠️ bewusst abgewichen | README dokumentiert „keine Authentifizierung“ und Web-Einschalten. CSRF macht das Risiko größer als beschrieben (F-07). |
| 2.5 UI: Sollwert ≤ 190, Ausgang %, Fehlergrund, Autotune-Bestätigung                                                             | ✅                    |                                                                                                                         |
| 2.5 README/Schaltplan synchron                                                                                                   | ✅                    | [docs/schaltplan.svg](docs/schaltplan.svg) trägt 190/200/750/215.                                                       |
| 3 Hardwaremaßnahmen                                                                                                              | ⏳ nicht prüfbar      | Unabhängige Abschaltung, Erdung, SSR etc.                                                                               |
| 5 Prüfstufe 1 (Unit-/HAL-Test)                                                                                                   | ❌                    | Es gibt kein `test/`-Verzeichnis.                                                                                       |
| 5 Stufen 2–6                                                                                                                     | ❌ offen              | Keine Protokolle mit Heizlast.                                                                                          |

### 3.3 `README.md` – Aussagen, die vom Code abweichen oder nicht tragen

| README-Aussage                                                                                     | Befund                                                                                                           |
| -------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| „Sollwert 20–80 °C“ für Autotune                                                                   | Der Sollwert **hat auf den Autotune keinen Einfluss** (Bibliothek nutzt die Starttemperatur als Sollwert, F-01). |
| „Start-Timeout (‚Kein Startwert‘) und ‚Messwert veraltet‘ werden getrennt angezeigt“               | Beide Zustände sind im aktuellen Design **praktisch nicht erreichbar** (F-06).                                   |
| „Verlaufsdiagramm (2 h, 5-s-Raster)“                                                               | Heap-/Laufzeitproblem von `/history` (F-04).                                                                     |
| „Ausgangsleistung/PWM“ im Diagramm                                                                 | PWM-Verlauf wird mit 0 Nachkommastellen ausgegeben (F-12).                                                       |
| „Der aktuelle Sensorzustand … ohne die Fehlerverriegelung aufzuheben“                              | ✅ stimmt.                                                                                                        |
| „Der Bericht … `localStorage` … Seite geöffnet lassen“                                             | ✅ ehrlich, aber fragil (F-15).                                                                                   |
| „Heizung ist nach Boot und Fehler quittiert ausgeschaltet“                                         | ✅ für Boot/Quittierung. Nach Reset zusätzlich ohne Fehlergedächtnis (F-08).                                      |
| Alle übrigen Funktionsbeschreibungen (Encoder, OLED-Retry, STALL, CSV-Spalten, Endpunkte, Grenzen) | ✅ stimmen mit dem Code überein.                                                                                  |

---

## 4. Detailfunde

### 🔴 F-05 – Festhängende und Null-Messwerte werden als gültig akzeptiert

**Ort:** [controller.cpp](src/controller.cpp#L327) (Sensorprüfung), Adafruit-`max6675.cpp`.

**Befund.** Gültigkeit = `isfinite && 0 ≤ T ≤ 1024`. Die Adafruit-Bibliothek erkennt nur das „Thermoelement offen“-Bit (Bit 2
→ `NAN`). Sie wertet weder Bit 15 (Dummy-Vorzeichen) noch Bit 1 (Device-ID) aus und liefert für `0x0000` schlicht `0,00 °C`.
Damit ergibt z. B. eine unterbrochene/abgefallene SO-, SCK- oder VCC-Leitung (Bus liegt auf LOW) einen **gültigen**
Dauerwert von 0 °C.

**Auswirkung.** `input = 0` → bei Sollwert 100 °C liefert der P-Anteil allein 180 ms (18 % ≈ 360 W), bei 190 °C 342 ms
(≈ 680 W). Die Sohle heizt (Modell: ab Sollwerten ≳ 80–100 °C) weit über 230 °C, die Software sieht dauerhaft „0 °C“ → der
215-°C-Not-Aus kann **nie** auslösen, die 230-°C-Schmelzsicherung wird zerstört – genau das, was `bedenken.md` an erster
Stelle verhindern will. Gleiches gilt für einen „eingefrorenen“ Wert (z. B. defekter Konverter, der den letzten Wert wiederholt).

**Es fehlt:**
1. Auswertung des **vollständigen 16-Bit-Rohworts** (Bit 15 = 0, Bit 1 = 0, Bit 2 = 0) – dafür ist ein eigener Lesecode
   nötig (die Bibliothek verbirgt das Rohwort; Bit-Banging ist ohnehin nur ~25 Zeilen).
2. **Plausibilisierung der Heizwirkung** („Thermal Protection“ wie in Marlin: Heizung > X s mit > Y % Leistung ohne
   Temperaturanstieg ≥ Z K → Fehler; sowie „Temperatur steigt trotz Ausgang 0“).
3. **Steigungsgrenze** (|dT/dt| > physikalisch mögliches Maximum, z. B. > 20 K/s → Fehler) und Mindest-/Rausch-Check
   („exakt identischer Wert über N Sekunden bei laufender Heizung“ ist verdächtig).
4. Ein **zweiter, unabhängiger Sensor** (anderer Typ, z. B. NTC/PT1000 oder zweiter Konverter) als Quervergleich. Das ist die
   robusteste Maßnahme und zugleich Voraussetzung für die im Bedenken-Report geforderte „unabhängige Referenz“.

**Empfehlung:** P0. Ein Ersatz des MAX6675 durch **MAX31855** (Fehlerbits: offen/Kurzschluss GND/VCC) oder
**MAX31856** (19 Bit, 50/60-Hz-Filter, Fehlerregister, Hardware-SPI) behebt Teile des Problems an der Wurzel; die
Plausibilisierung (Punkt 2/3) bleibt trotzdem nötig.

---

### 🔴 F-06 – Sensor-Timeouts sind toter Code; Schutz vor Firmware-Hänger fehlt

**Ort:** [controller.cpp](src/controller.cpp#L349), [config.h](src/config.h) (`SENSOR_TIMEOUT`, `LOOP_STALL_THRESHOLD`).

**Befund (Erreichbarkeitsanalyse).** Die Messung geschieht **synchron im selben `update()`**. Zwischen zwei `update()`-Aufrufen
liegen < 400 ms (sonst greift der Stall-Zweig, der sofort neu misst). Eine Messung ist fällig nach ≥ 250 ms. Der maximale
Abstand zweier Messungen ist daher 250 + 399 = **649 ms < 750 ms**. Ein ungültiger Wert verriegelt sofort mit `SensorValue`. Folglich
können weder `SensorStaleTimeout` noch `SensorStartupTimeout` jemals auslösen. Die README-Behauptung, beide würden „getrennt
angezeigt“, beschreibt Zustände, die im aktuellen Design nicht auftreten. Die Logs bestätigen das indirekt: „Messwert veraltet“
trat nur vor Einführung der Stall-Behandlung auf (F-24).

**Eigentliches Risiko.** Ein *Code-Hänger* (Endlosschleife, blockierende Bibliothek, Heap-Fehler) lässt den SSR-Pin auf seinem
letzten Pegel. Erst der Soft-Watchdog des ESP8266 (≈ 3,2 s) setzt zurück. Mit der eigenen Schätzung aus `bedenken.md`
(„+30 K in 2 s“) sind das **≈ +45–50 K** unkontrolliertes Heizen – das Szenario, für das das 750-ms-Timeout gedacht war. Eine Software, die
nicht mehr läuft, kann sich nicht selbst überwachen. Stall-Erkennung wirkt außerdem nur **nachträglich** (nach dem Blockieren; im Log:
887 ms Stall beim WLAN-Aufbau, siehe F-24).

**Empfehlung (P0):**
- **Hardware-Dead-Man-Schalter**: Das SSR-Gate wird nur freigegeben, solange ein vom Regelloop getoggelter GPIO (Heartbeat,
  z. B. 5–10 Hz) einen Retriggerable-Monostable/Watchdog-IC (z. B. TPL5010/STWD100/74HC123-Schaltung) speist. Bleibt der
  Heartbeat aus > 500 ms, fällt der Pfad ab. Ergänzend zur unabhängigen Temperaturabschaltung aus Bedenken-Report §3.
- Software: Die toten Pfade entweder **entfernen und in der Doku ehrlich als „nicht erreichbar“ beschreiben** oder die Messung
  entkoppeln (Timer/zweiter Kern bei ESP32), sodass ein separater Wächter das Messalter prüfen kann.
- Stall-Prävention statt -Erkennung: Heizen nur, wenn keine blockierenden Operationen anstehen; WLAN-/Webcode strikt
  nicht-blockierend halten (F-04).

---

### 🟠 F-01 – Autotune konvergiert praktisch nie; Sollwert wirkt nicht

**Ort:** [controller.cpp](src/controller.cpp#L191), Bibliothek `PID_AutoTune_v0.cpp`.

**Befund aus dem Bibliothekscode.**
1. Beim ersten `Runtime()` setzt die Bibliothek `setpoint = refVal` (aktuelle Temperatur, z. B. 22 °C) und
   `outputStart = *output`. Danach gilt: `T > setpoint + noiseBand → output = start − step`, `T < setpoint − noiseBand → output = start + step`.
   Die Oszillation erfolgt also **um die Starttemperatur**, nicht um den in der UI gesetzten Sollwert. Die Prüfung „Sollwert 20–80 °C“ in
   [controller.cpp](src/controller.cpp#L202) (und der Hinweis in README/Web-UI) ist damit **ohne Funktion**.
2. Relay-Feedback funktioniert nur, wenn der *untere* Ausgangspegel kleiner ist als die Leistung, die zum Halten der Temperatur
   nötig ist. Hier: unterer Pegel 5 % (≈ 100 W), benötigte Haltleistung bei 25–40 °C nur wenige Watt. Die Sohle **kühlt nie
   unter die Schaltschwelle**, es entsteht keine Schwingung, `peakCount` wächst nie.
3. Konsequenz: Der Lauf endet entweder durch die 120-°C-Grenze oder durch das 10-min-Limit.

**Simulation (Abschnitt 5.3):** Ausgang bleibt dauerhaft bei 5 %; Temperatur steigt monoton auf 119,7 °C nach 10 min; kein Ergebnis.
Selbst mit doppelt so großem Wärmeverlust bleibt 100 W weit oberhalb des Gleichgewichts bei 40 °C.

**Weitere Punkte:**
- `SetControlType(1)` = klassisches **Ziegler-Nichols-PID**: bekannt aggressiv (typ. 25–60 % Überschwingen) – widerspricht dem
  Leitziel „absolute Vermeidung von Overshoot“. Zusätzlich liefert die Bibliothek Ki = Kc/Ti ohne Ableitungsfilter.
- Die `AUTOTUNE_K*_MAX`-Grenzen (20 / 1,0 / 500) sind plausibel, aber ungeprüft gegen reale Streckendaten.

**Empfehlung:** Entweder (a) Autotune bis zu einer Neuimplementierung **ausdrücklich als „nicht funktionsfähig“ dokumentieren und im
UI deaktivieren**, oder (b) eigenen Relay-Test implementieren: Ausgang `0 % ↔ x %` mit Hysterese um den **tatsächlichen
Test-Sollwert**, Ku/Tu aus Amplitude und Periode (Åström-Hägglund), anschließend vorsichtige Regeln (Tyreus-Luyben, „no-overshoot“-ZN:
Kp = 0,2·Ku, Ti = 0,5·Tu, Td = 0,33·Tu) und Plausibilisierung gegen Modell/Grenzen. Alternativ die Bibliothek **sTune** prüfen
(offenes Sprungantwort-Verfahren, enthält u. a. „NoOvershoot_PID“ – vor Einsatz Version/Eignung verifizieren).

---

### 🟠 F-02 – Integralanteil wird beim Sollwertdurchgang gelöscht

**Ort:** [controller.cpp](src/controller.cpp#L379) (`if (input >= sp) { pid.SetMode(MANUAL); … }`), analog
[controller.cpp](src/controller.cpp#L269).

**Befund.** `PID::SetMode(AUTOMATIC)` ruft `Initialize()` auf: `outputSum = *myOutput`. Da `out` im `MANUAL`-Zweig auf 0 gesetzt wurde,
startet der Integrator bei **jedem** Übergang `input ≥ sp → input < sp` neu bei 0. Die Regelung ist am Sollwert dadurch effektiv ein
P+D-Regler mit hartem Abschalten; Ki wirkt nur zwischen zwei Durchgängen.

**Folge.** Kp = 1,8 liefert bei 190 °C Haltleistung (≈ 6 % = 60 ms) nur über einen **P-Offset von ≈ 30 K**; der Integrator (Ti = Kp/Ki = 6 min) wird jedes
Mal vor dem Ziel gelöscht. Ergebnis: Dauerschwingung. Simulation (Abschnitt 5.1): **Hub 19,5 K bei 190 °C**, 8,9 K bei 100 °C – gegenüber < 1 K bei
erhaltenem Integrator. Das README erwähnt diese Eigenschaft nicht.

**Warum die Konstruktion wohl entstanden ist:** Anti-Windup/Overshoot-Schutz (kein Heizen oberhalb des Sollwerts). Das Ziel ist richtig, die Mittel
sind es nicht. Reiner Integrator-Erhalt (Simulation B) erzeugt dafür +11 K Überschwingen. Funktionierende Variante (Sim. E): **bedingte Integration
(Integralband ≥ P-Offset, hier ≈ 40 K) + Abtastzeit 250 ms** → Überschwingen +0,5 K, Hub 0,7 K, dafür ≈ 19 min Aufheizzeit auf 190 °C.

**Empfehlungen (Auswahl, in Reihenfolge des Aufwands):**
1. `out = 0` nur für das PWM-Fenster erzwingen, **ohne** `SetMode` umzuschalten; Integrator mit Back-Calculation/Clamping begrenzen.
2. Bedingte Integration (Integral erst bei |e| < Band) – mit `SetTunings(Kp, e<Band ? Ki : 0, Kd)` ohne neue Bibliothek umsetzbar.
3. **Sollwert-Rampe** (z. B. ≤ 1–2 K/s) und `P_ON_M` (Proportional-on-Measurement, in `PID_v1` bereits vorhanden) zur Overshoot-Dämpfung.
4. **Vorsteuerung** (Feedforward): Haltleistung ≈ `UA·(sp − T_amb)/P_max` als Grundduty, dann PID nur für die Abweichung. Das beseitigt den
   30-K-P-Offset an der Wurzel.
5. Langfristig: `QuickPID` (Anti-Windup-Modi, P_ON_M/P_ON_E-Mischung) oder eigener PID mit Zeitdiskretisierung nach Bedarf.

---

### 🟠 F-03 – Abtastzeit-Mismatch und D-Anteil auf quantisiertem, verrauschtem Signal

**Ort:** [controller.cpp](src/controller.cpp#L21) (`PID pid(...)`, kein `SetSampleTime`), `update()` ruft `pid.Compute()` bei jedem Durchlauf.

**Befund.**
- `PID_v1` rechnet standardmäßig alle **100 ms** (`kd = Kd/0,1 s = 550`). Der Sensor liefert aber nur alle **250 ms** einen neuen Wert (0,25 K Auflösung). Von
  2–3 aufeinanderfolgenden Rechnungen sieht nur **eine** eine Differenz `dInput ≠ 0`, die anderen 0. Der D-Anteil ist ein Sägezahn.
- Simulation, Aufheizen (Abschnitt 5.2): Ausgang springt zwischen **217 ms und 79 ms** (±14 Prozentpunkte) im 100-ms-Takt, obwohl die
  Temperatur gleichmäßig steigt. Mit `SampleTime = 250 ms` sind es 55 ms (±5,5 Pp) pro Quantisierungsstufe.
- **Reales Rauschen:** In `logs/device-monitor-261010-140433.log` (kalte Platte, Heizung aus) liegt σ = **0,25 K**; 20 Sprünge ≥ 1 K zwischen
  aufeinanderfolgenden Zeilen in 13 min. In `…261009-231802.log` (~75 °C) 53 Sprünge ≥ 1 K in 749 Zeilen. Bei Kd = 55 und 100 ms erzeugt
  *ein* 1-K-Sprung bis zu **550 ms** Stellgrößenänderung (55 %) – bei Rauschen *in Richtung Abkühlen* ein Heizimpuls.
  (Ursache physikalisch oder elektrisch ungeklärt; beim Heizen/Schalten des SSR ist mit **mehr** Störungen zu rechnen.)
- Die PWM-Logik wertet `out` jede Millisekunde aus; Stellgrößensprünge verändern also mitten im Fenster den Takt (F-19).

**Empfehlungen:**
- `pid.SetSampleTime(SENSOR_INTERVAL)` und `Compute()` **nur bei neuem Messwert**.
- **D-Anteil filtern**: Messwert-Tiefpass (τ ≈ 1–2 s) oder Median-of-3 vor dem Regler; ggf. Kd reduzieren (Kd = 55 wurde ohne Modell gewählt).
- Messkette verbessern: abgeschirmte, verdrillte Thermoelementleitung, Schirm nur an einem Ende, Abstand zu SSR/Netzleitung, 100-nF am Konverter,
  Konverter mit 50/60-Hz-Filter (MAX31856).
- Plausibilitäts-/Rauschmessung als Pflichtteil von Prüfstufe 3 (σ im Ruhezustand und bei laufendem SSR).

---

### 🟠 F-04 – `/history` sprengt Heap und blockiert quadratisch

**Ort:** [web_interface.cpp](src/web_interface.cpp#L69), Kommentar „Gestreamt, um den knappen Heap … nicht zu belasten“.

**Befund (Bibliotheksquellen).** `AsyncResponseStream` **streamt nicht**, sondern sammelt die gesamte Antwort in einem `cbuf`.
Bei jedem `write()`, der über den freien Platz hinausgeht, ruft die Bibliothek `resizeAdd(needed)` auf; `cbuf::resize` **alloziert neu, kopiert alles und nullt den Puffer**
(ESP8266-Core `cbuf.cpp`). Wegen des exakten Wachstums geschieht das bei praktisch jedem weiteren Schreibvorgang → **O(n²)**.

| Historie             | JSON     | Realloc-Zyklen | kopierte Bytes | Dauer bei 30 / 60 MB/s |
| -------------------- | -------- | -------------- | -------------- | ---------------------- |
| 30 min (360 Pkt.)    | 10,6 KiB | 3 842          | 46 MB          | 1,5 / 0,8 s            |
| 60 min (720 Pkt.)    | 21,2 KiB | 8 162          | 187 MB         | 6,2 / 3,1 s            |
| 90 min (1 080 Pkt.)  | 31,7 KiB | 12 482         | 420 MB         | 14 / 7 s               |
| 120 min (1 440 Pkt.) | 42,3 KiB | 16 802         | 747 MB         | 25 / 12 s              |

(Berechnung des tatsächlichen Schreibmusters; Durchsatz ist eine Annahme.) Zusätzlich braucht der letzte Realloc **Alt- und Neupuffer gleichzeitig**
(≈ 2 × 42 KiB). Bei 45 KB statischem RAM bleiben dem Heap grob 35 KB, nach WLAN-Stack deutlich weniger. **Wahrscheinlich** schlägt die Allokation nach etwa
30–60 min fehl; dann liefert die Bibliothek eine abgeschnittene Antwort (`Failed to allocate`), `r.json()` im Browser wirft und das Diagramm bleibt stehen
([web_page.h](src/web_page.h#L381) hat kein `.catch`).

**Zusätzliche Wirkung auf die Regelung:** Der Browser holt `/history` alle 10 s. Blockiert der Handler > 400 ms, löst **jedes Mal** der Stall-Zweig aus
(SSR LOW, Fenster zurückgesetzt) – eine periodische Störung des Regelkreises, die nur mit offener Webseite auftritt.

**Weitere Ineffizienz:** Die drei Serien Kp/Ki/Kd (je 1 440 Werte) sind **konstant** (die Firmware ändert Parameter nie, F-16) und machen ≈ 19 von 30 Byte pro Punkt aus (**63 %**).

**Empfehlungen:**
1. Sofort: `kp/ki/kd`-Serien entfernen (Konstanten einmalig in `/data`/`/config`).
2. `beginChunkedResponse()` mit Zustandsautomat (liefert pro Aufruf ≤ `maxLen` Byte, kein Gesamtpuffer) oder die Historie binär/vorformatiert in
   fester Zeilenlänge ausliefern; alternativ serverseitig gröber (z. B. 1 Pkt./30 s bei Langverlauf) oder **inkrementell** (`/history?since=N`).
3. `ESP.getFreeHeap()`, `getMaxFreeBlockSize()`, `getHeapFragmentation()` in die `D`-Zeile und `/status` aufnehmen, damit der Effekt **gemessen** wird.
4. Browser: `.catch`, Timeout, Backoff; Abfrage pausieren, wenn Tab unsichtbar (`document.hidden`).

---

### 🟠 F-07 – CSRF / DNS-Rebinding auf unauthentifizierte POST-Routen

**Ort:** [web_interface.cpp](src/web_interface.cpp#L103) (`/start-tune`, `/cancel-tune`, `/heater`, `/setpoint`).

**Befund.** README und Bedenken-Report adressieren „jedes Gerät im WLAN“. Technisch kann jedoch **jede Webseite**, die im Browser eines WLAN-Teilnehmers
geöffnet ist, per `fetch(..., {method:'POST', mode:'no-cors'})` oder Formular ein `POST http://<IP>/heater?enabled=1` absetzen. Solche „simple requests“ lösen
keinen CORS-Preflight aus; die Antwort muss der Angreifer nicht lesen. Die UI-Rückfrage (`confirm()`) liegt nur im Browser und wird umgangen. Möglich sind
Heizung ein, Sollwert bis 190 °C, Autotune-Start (bei kalter Platte). Browser-Schutzmechanismen (z. B. Private Network Access) mindern das teilweise, sind
aber nicht verlässlich. DNS-Rebinding umgeht zusätzlich die Same-Origin-Policy (Host-Header wird nicht geprüft).

Das ist der Unterschied zwischen „vertrauenswürdiges WLAN“ (dokumentiert) und „vertrauenswürdige Webseiten“ (nicht garantierbar) und hebelt die Aussage
„Nur lokal bedienbar, Web nicht angreifbar“ praktisch aus. `bedenken-report.md` §2.5 hält Zugriffsschutz oder Deaktivierung der Steuerrouten für „vorzuziehen“.

**Empfehlungen (gestuft, alle klein):**
1. Steuerrouten verlangen einen **benutzerdefinierten Header** (`X-Requested-With`/`Content-Type: application/json`) → erzwingt Preflight; der ESP antwortet auf `OPTIONS`
   nicht mit CORS-Freigabe.
2. **Origin-/Host-Prüfung** (muss leer oder gleich `http://<eigene IP>`/`<hostname>.local` sein).
3. **Kompilierschalter** `WEB_CONTROL_ENABLED` (statt des konstanten `doc["controlsEnabled"] = true`) – Schreibrouten per Build deaktivierbar (schreibgeschützter Monitor).
4. Optional HTTP-Basic-Auth (`request->authenticate`) oder Bedien-PIN, plus *Arm-Schalter*: Web-Einschalten nur, wenn zuvor lokal „Fernbedienung freigegeben“ wurde.
5. Sicherheitsheader (`Content-Security-Policy`, `frame-ancestors 'none'`, `Cache-Control: no-store` für `/data`).

---

### 🟠 F-08 – Reset löscht die Fehlerverriegelung

**Ort:** [controller.cpp](src/controller.cpp#L39) (`latchedFault` nur im RAM).

`bedenken-report.md` §2.5: „Ein Reset muss niemals einen übertemperaturbedingten Fehler ohne Abkühl- und Ursachenprüfung freigeben.“ Nach Reset-Taste, Soft-WDT, Brown-out
oder Netztrennung ist `latchedFault = None`; die Heizung bleibt zwar aus, kann aber über das Web (nach dem ersten gültigen Messwert) sofort wieder freigegeben werden
– ohne lokale Quittierung und ohne Hinweis, **dass** zuvor ein Fehler vorlag. Ein intermittierender Sensor-/Leitungsfehler wird so „weggebootet“.

**Empfehlungen:**
- Fehlergrund beim Verriegeln in **RTC-User-Memory** (überlebt Soft-Reset/WDT) und optional **LittleFS/EEPROM** (überlebt Stromausfall; Schreiben *nach* SSR-LOW) ablegen.
- Beim Boot: `ESP.getResetInfoPtr()` auswerten, **Reset-Grund und gespeicherten Fehler** seriell/OLED/Web anzeigen; nach WDT-/Exception-Reset standardmäßig
  Fehlerzustand „Neustart-Ursache prüfen“ (lokale Quittierung nötig).
- Ein Zähler „Neustarts seit …“ hilft, Heap-/WDT-Probleme (F-04) zu erkennen.

---

### 🟡 F-09 – Lokaler Taster schaltet ein; Startscreen-Tastendruck startet die Heizung

**Ort:** [ui.cpp](src/ui.cpp#L72), [controller.cpp](src/controller.cpp#L276).

Jeder entprellte Tastendruck ruft `localButtonPressed()` auf und **schaltet die Heizung ein**, sofern eine frische Messung vorliegt. Auf dem Startscreen lautet die Anweisung
„Drehen/Druecken: weiter“ – der Druck **navigiert und heizt gleichzeitig**. Dreh-Encoder werden häufig versehentlich gedrückt. Der Bedenken-Report sieht den Taster als
Aus-/Quittiertaster vor. (Das README dokumentiert den Ein-Betrieb, es bleibt eine Abweichung.)

**Empfehlung:** Taster nur als **Aus/Quittieren**; Einschalten über **Langdruck (≥ 1,5 s)** mit Bestätigung auf dem OLED, nie als Nebenwirkung der Bildschirmweiterschaltung. Startscreen mit
eigener Taste/Drehung verlassen, ohne `localButtonPressed()`.

---

### 🟡 F-10 – Reserve 215 → 230 °C bei Vollleistung unzureichend; kein prädiktiver Pfad

**Ort:** [config.h](src/config.h) (`MAX_TEMP = 215`).

Zum Punkt „Thermal Runaway“ aus `bedenken.md`: Simulation (Zwei-Massen-Modell, 250-ms-Abtastung, **100 % Leistung bis Software-Abschaltung bei 215 °C Messwert**):

| C_Element / C_Sohle / Kopplung / τ_Sensor | Abschaltzeit | Spitze Sohle | Spitze Heizelement |
| ----------------------------------------- | ------------ | ------------ | ------------------ |
| 80 J/K / 300 J/K / 25 W/K / 1 s           | 42 s         | **230,5 °C** | 285 °C             |
| 80 / 300 / 25 / 3 s                       | 44 s         | 240 °C       | 295 °C             |
| 150 / 300 / 10 / 1 s                      | 57 s         | 251 °C       | 357 °C             |
| 150 / 500 / 10 / 3 s                      | 80 s         | 249 °C       | 380 °C             |
| 60 / 200 / 40 / 0,5 s                     | 28 s         | 226 °C       | 259 °C             |
| 200 / 300 / 8 / 5 s                       | 71 s         | 273 °C       | 388 °C             |

Unter diesen **Annahmen** würde die 230-°C-Sicherung beim Szenario „Volllast bis Abschaltung“ (Regelfehler, stuck-on-Ausgang bis Software greift, F-05) in der Mehrzahl der Fälle
auslösen – die in `bedenken.md` genannten 15 K sind „das absolute Minimum“, aber nicht bestätigt. Im Regelbetrieb (Ausgang ≪ 100 %) ist die Lage besser; sie ist **nicht
nachgewiesen**, solange Prüfstufe 5 fehlt.

**Empfehlungen:**
- **Prädiktive Abschaltung:** `T_pred = T + τ_pred · dT/dt` (z. B. τ_pred = 3 s, Steigung aus gefiltertem Verlauf) ≥ Schwelle (z. B. 205 °C) → Fehler.
- `MAX_TEMP` nach Messung (Prüfstufe 5) ggf. senken; separate **Warnschwelle** (z. B. 200 °C: Ausgang auf 0, kein Latch) vor dem Latch.
- Ausgangsbegrenzung (`SetOutputLimits(0, MAX_DUTY)`, z. B. 40 %) als Standard, 100 % nur über bewusste Freigabe. Aktuell kann der Integrator bis 100 % wachsen.
- Beim Stall (≥ 400 ms, im Log real 887 ms) zusätzlich Ausgang für eine Erholzeit begrenzen.

---

### 🟡 F-11 – `latchFault`: Serial-Log vor SSR-LOW

**Ort:** [controller.cpp](src/controller.cpp#L111): `Serial.printf(...)` (blockierend, bis zu ~9 ms bei 115 200 Baud) steht **vor** `resetOutputWindow()`/`digitalWrite(LOW)`.
Bedenken-Report §2.2/§3: „unmittelbar“. Der Pfad ist in der Praxis nur Millisekunden lang, widerspricht aber dem eigenen Anspruch und ist trivial zu ändern:
`digitalWrite(SSR_PIN, LOW)` **als erste Anweisung** in `latchFault`, `abortAutotune` und `update()`-Sensorzweig, Logging danach. Zusätzlich: das SSR einmalig zentral über eine
`setSsr(bool)`-Funktion schalten (eine Stelle, die immer LOW bevorzugt), statt `digitalWrite` an acht Stellen.

---

### 🟡 F-12 – Darstellungsfehler in der Web-Anzeige

1. **Ki wird als „0.01“ angezeigt** ([web_page.h](src/web_page.h#L351): `toFixed(2)`; tatsächlich 0,005). Gleiches im Diagrammverlauf: `res->print(v[p], 2)` in
   [web_interface.cpp](src/web_interface.cpp#L97). → 4 Nachkommastellen für Ki, 3 für Kp.
2. **PWM-Verlauf nur 0/1**: [web_interface.cpp](src/web_interface.cpp#L88) `res->print(Controller::historyOutputAt(i), 0)` gibt 0 Nachkommastellen aus; die Werte liegen in 0…1
   (z. B. 0,07). Die Kurve „SSR PWM (0–1)“ zeigt dadurch praktisch durchgehend 0. → 2 Nachkommastellen oder Prozent als Ganzzahl (0…100).
3. [web_page.h](src/web_page.h#L138) (`data.temp.toFixed`) erwartet immer eine Zahl, während `/data` bei fehlendem Messwert `null` liefert (derzeit nicht auslösbar, aber fragil).

---

### 🟡 F-13 – Chart.js: ungepinnt, ohne SRI, harter Ausfall ohne Internet

**Ort:** [web_page.h](src/web_page.h#L13), [web_page.h](src/web_page.h#L101).

- `https://cdn.jsdelivr.net/npm/chart.js` ohne Version und ohne Subresource-Integrity: ein Major-Update bricht die Seite, eine Kompromittierung des CDN führt Fremdcode in
  eine Seite aus, die eine 2-kW-Heizung steuert.
- Ohne Internetzugang wirft `Chart.defaults.color = …` (oberste Ebene des Skripts) einen `ReferenceError`; `window.onload` wird nie gesetzt. **Das Polling der Statusanzeige läuft dann nie an**,
  obwohl die Buttons (hoisted Funktionen) weiter funktionieren – die Heizung ließe sich schalten, ohne Status zu sehen. Das README nennt die CDN-Abhängigkeit, nicht diesen Ausfallmodus.

**Empfehlung:** Chart.js **fest versionieren + SRI** oder (besser) die Bibliothek **lokal aus LittleFS** (gzip ≈ 70 KB; oder leichtgewichtig `uPlot` ≈ 45 KB) ausliefern; die Statusanzeige
unabhängig vom Diagramm initialisieren (`try/catch` um `initChart`).

---

### 🟡 F-14 – OLED-Zeilen zu breit

**Ort:** [ui.cpp](src/ui.cpp#L120), [ui.cpp](src/ui.cpp#L128). Font `u8g2_font_5x7_tf` (5 px/Zeichen, Display 128 px = 25 Zeichen):
`"HEIZUNG AUS - FEHLER VERRIEGELT"` = 31 Zeichen = **155 px** → der entscheidende Teil („VERRIEGELT“) wird abgeschnitten;
`"Taster: quittieren (<60 C)"` = 26 Zeichen = 130 px (letztes Zeichen angeschnitten). Das ist die zentrale Fehleranzeige laut Bedenken-Report §2.2.
→ Kürzen („HEIZUNG AUS: FEHLER“), auf `u8g2.getStrWidth()` prüfen, bei Bedarf scrollen/umbrechen. Zudem fehlt eine **„HEISS“-Restwärmeanzeige** (siehe Abschnitt 10).

---

### 🟡 F-15 – Autotune-Bericht nur im Browser

**Ort:** [web_page.h](src/web_page.h#L229).

- Die Erfassung läuft **nur solange der Tab offen und aktiv ist**. Hintergrund-Tabs werden gedrosselt (`setInterval` ≥ 1 s, nach 5 min bis zu 1/min) → lückenhafte Berichte; Schließen des
  Tabs beendet die Aufzeichnung ohne Hinweis. Auf dem Gerät existiert nur das serielle `TUNE`-Log.
- Jeder Sample speichert den kompletten `data`-Block inkl. `tuningConfig` (~ 0,7 KB × 600 Samples ≈ 0,4 MB pro 10-min-Lauf) plus `historyAtFinish` (F-04). `localStorage` (≈ 5 MB/Origin) ist nach
  ~10 Läufen voll, der Index wird nie bereinigt.
- Der Ausgang wird aus dem **deutschen Statustext** geschlossen (`startsWith('Abgeschlossen')`, `startsWith('Fehler:')`) – eine Textänderung bricht die Auswertung.
- `fetch('/history')` am Ende (F-04) kann blockieren/scheitern.

**Empfehlung:** Autotune-Messreihe **auf dem Gerät** (RAM-Ringpuffer, 600 × 8 B) erfassen und über `/tune-report` ausliefern; Status als **Enum/Code** (`state: "done"|"aborted"|"error"`),
Download als JSON/CSV statt `localStorage`; Bericht mit Firmware-Version/Konfiguration versehen.

---

### 🟡 F-16 – Parameter nicht übernehmbar, nicht persistent; tote Tuning-Historie

- Es gibt **keinen Weg**, einen Autotune-Vorschlag (oder manuell gewählte Kp/Ki/Kd) zu übernehmen, außer Neukompilieren. Das ist sicherheitsbewusst, macht Autotune aber
  nutzlos und erzeugt Komplexität (`logTunings`, `TuningEntry`, `historyTuningsAt`, drei Chart-Serien), die nie einen Wert ändert ([controller.cpp](src/controller.cpp#L57)).
- Nichts wird persistent gespeichert (Sollwert startet immer bei 10 °C – laut README gewollt; PID-Parameter wären aber sinnvoll).

**Empfehlung:** Bewusste „Übernehmen“-Funktion (lokale Bestätigung am Gerät, Grenzprüfung, Speicherung in LittleFS/`Preferences`-Äquivalent, „zurück auf Werkseinstellung“); bis dahin die
konstanten Serien entfernen (F-04).

---

### ⚪ Weitere Funde (niedrig / Hinweis)

**F-17 – Duplizierte Konstanten / wirkungslose Schalter.**
`1.5`, `0.05`, `0.10`, `0.15`, `10` stehen sowohl in [controller.cpp](src/controller.cpp#L209) als auch hart verdrahtet in [web_interface.cpp](src/web_interface.cpp#L43)
(`outputMinPercent 5`, `maxPercent 15`…); eine Änderung an einer Stelle lässt die UI lügen. `SETPOINT_ABSOLUTE_MAX` ist in `adjustSetpoint()` wirkungslos (anschließendes
`constrain(…, 190)`), `controlsEnabled` ist konstant `true`, `190` und `5` sind im JS hart kodiert ([web_page.h](src/web_page.h#L466)), obwohl die Grenzen dem Server gehören. → alles in `config.h`,
`/config`-Endpunkt einmalig laden. `static_assert`s ergänzen (Abschnitt 9, P1).

**F-18 – Ein ungültiger Messwert verriegelt dauerhaft.**
Einzelne NAN-/Spike-Messung (Bit-Banging mit WLAN-Interrupts, EMV) → Latch bis < 60 °C *und* lokale Quittierung. Sicher, aber verfügbarkeitskritisch (Platte bei 190 °C muss erst auf 60 °C fallen). Da das
750-ms-Zeitbudget 3 Messungen erlaubt, kann man „SSR sofort LOW, aber erst nach **2 aufeinanderfolgenden** Fehlern verriegeln“. Spike-Filter (Median-of-3) und Bestätigung von
`T ≥ MAX_TEMP` über 2 Samples (≤ 500 ms) vermeiden Fehlauslösungen durch Einzelglitches.

**F-19 – PWM-Fenster.**
`out` wird mitten im Fenster neu berechnet und sofort ausgewertet (`out > elapsed`): ein Impuls kann zerteilt oder verlängert werden, die effektive Einschaltzeit weicht von `out` ab.
`windowStart = now` statt `+= WINDOW_SIZE` driftet. Bei Zero-Cross-SSR wirken Einschaltzeiten < 10 ms (Halbwelle) nur diskretisiert (1,8 ms „Einschaltzeit“ ist keine definierte Leistung).
→ Einschaltzeit zu **Fensterbeginn einfrieren**, auf **Vielfache von 10 ms** runden mit Fehlerübertrag (Sigma-Delta über Fenster). Zur „Wellen“-Frage in `bedenken.md`: kürzere Fenster
(z. B. 200–500 ms) oder verteilte Burst-Pakete (Bresenham über Halbwellen) glätten die Leistung **ohne** neues SSR.

**F-20 – Encoder.**
Nur fallende CLK-Flanke, keine Entprellung, Richtung durch `analogRead` (≈ 100 µs, Nebenwirkung auf WLAN bei häufigem Aufruf). Während blockierender Phasen (OLED-`sendBuffer`, Serial, WLAN) gehen
Flanken verloren. GPIO16 unterstützt keine Interrupts (Grund der Polling-Lösung). → Zustandsautomat (Buxton) mit beiden Kanälen, RC-Entprellung (10 nF) in Hardware; langfristig ESP32 (PCNT) oder
I²C-Expander mit Interrupt.

**F-21 – `hardware_test.cpp`** wird immer kompiliert; seine **zweite globale `U8G2`-Instanz** (512-B-Puffer) und der zweite `MAX6675`-Treiber belegen RAM, obwohl `HardwareTest::run()` auskommentiert ist
([main.cpp](src/main.cpp#L8)). → eigene Umgebung `[env:hwtest]` mit `build_src_filter` und `-DHARDWARE_TEST`.

**F-22 – `platformio.ini`.**
`Adafruit GFX`/`SSD1306` ungenutzt (Display läuft über U8g2); `ESPAsyncWebServer`/`ESPAsyncTCP`/`platform` **ohne Version** (nicht reproduzierbar); keine `build_flags` (z. B. `-Wall -Wextra -Wshadow`),
keine Umgebung `native`/`test`, kein `esp8266_exception_decoder`-Filter, keine Dateisystem-Konfiguration; Kommentar „Generated by Copilot“. `monitor_filters = log2file` füllt `logs/` unbegrenzt
(32 Dateien, ~0,9 MB, größte 0,5 MB) und ist nicht in `.gitignore`. Beim Kompilieren kollidiert vermutlich das Makro `LIBRARY_VERSION` aus `PID_v1.h` und `PID_AutoTune_v0.h` (Warnung, harmlos).

**F-23 – Dokumentation.**
`bedenken-report.md` beschreibt den Ausgangsstand vom 7.10. („aktuelle Firmware entspricht den Vorgaben noch nicht“, 250/280 °C, 25 Logs) und ist inzwischen inhaltlich überholt; ein Status-Spalte
(„umgesetzt/offen/abweichend“) fehlt. Kleinigkeit: „Die beiden gefährlichen Fehler“ listet drei. README-Aufzählungspunkte „Sicherheit“/„Diagnose“ sind mehrere hundert Wörter lang – besser in `docs/` aufteilen
(Safety-Konzept, API, Diagnose, Autotune). Firmwarestrings sind ASCII-transliteriert („ungueltig“), obwohl die Weboberfläche UTF-8 kann.

**F-24 – Projekt-Hygiene und Evidenz.**
- **Kein Git-Repository** („Not a git repository“): keine Historie, keine Reviews, `.gitignore`/„nicht im Git“ (README) ist gegenstandslos, `include/secrets.h` (echte WLAN-Daten) liegt ungesichert im
  Projektordner.
- **Keine Tests, keine CI.** Prüfstufe 1 (Bedenken-Report) ist damit nicht erfüllt.
- **Logs:** In allen 7 Dateien mit `D`-Regelzeilen (von 32) ist der SSR nie aktiv (`ssr_high = 0`, `enabled = 0`), max. 75,5 °C. Es existiert **keine Aufzeichnung eines geregelten Heizvorgangs**. Die Fehlerzeile
  `FAULT … Messwert veraltet … loop_gap_ms=887` tritt in drei Logs jeweils ≈ 5 s nach dem Boot auf (WLAN-/DHCP-Aufbau blockiert ~0,9 s). Das sind Logs **vor** der Stall-Behandlung (Quelldateien sind
  neuer: 15:41 Uhr vs. letzte Logs 14:18 Uhr); es gibt **kein** `STALL`-Log, die aktuelle Logik ist am Gerät unbestätigt.
- Firmware-Stand ist nicht nachvollziehbar (keine Version/Build-ID in Logs oder `/data`).

**F-25 – Plattform.**
ESP8266: kein Hardware-Task-Watchdog, keine zwei Kerne, keine Interrupts auf GPIO16, knapper Heap, Arduino-Core in Wartung. Für eine sicherheitsrelevante Heizungsregelung ist ein **ESP32(-C3/-S3)** (separater
Regel-Task/Core, Task-WDT, RTC-WDT, PCNT für Encoder, LEDC/MCPWM, Brown-out-Detektor, NVS) der naheliegende Nachfolger; PlatformIO-Umgebung und Abhängigkeiten (ESPAsyncWebServer unterstützt ESP32) sind bereits kompatibel.
**Achtung:** Bei Mehrkern-Umstieg müssen Controller-Zugriffe (heute implizit kooperativ auf ESP8266 serialisiert) per Mutex/Message-Queue geschützt werden.

---

## 5. Simulationsergebnisse (Details)

Alle Läufe: Reglerlogik wie in `controller.cpp`, 250-ms-Messung mit 0,25-K-Quantisierung, 1-s-Fenster, Original-`PID_v1`/`PID_AutoTune_v0`, Plant: Element 80 J/K → Sohle 300 J/K (25 W/K),
Verlust UA = 0,6 W/K + Strahlung, Sensorverzögerung 1 s, Umgebung 22 °C, 2 000 W. **Annahmen, keine Messwerte.**

### 5.1 Regelgüte, 90 min

| Variante                                            | Sollwert | Max. Sohle | Überschw.  | t bis Sollwert −2 K | Hub letzte 20 min        | Ø Duty |
| --------------------------------------------------- | -------- | ---------- | ---------- | ------------------- | ------------------------ | ------ |
| **A: aktueller Code** (Ts 100 ms, Integrator-Reset) | 100 °C   | 100,1      | +0,1 K     | 282 s               | **8,9 K**                | 2,7 %  |
| B: Ts 250 ms, Integrator bleibt                     | 100 °C   | 105,5      | +5,5 K     | 290 s               | 1,0 K                    | 2,9 %  |
| E: Ts 250 ms, Integralband 40 K, Ki 0,005           | 100 °C   | 100,8      | +0,8 K     | 473 s               | 1,0 K                    | 2,9 %  |
| **A: aktueller Code**                               | 190 °C   | 190,2      | +0,2 K     | 352 s               | **19,5 K** (170,1…189,6) | 6,2 %  |
| B                                                   | 190 °C   | 201,0      | +11,0 K    | 310 s               | 0,7 K                    | 6,5 %  |
| **E (Ts 250, Band 40 K, Ki 0,005)**                 | 190 °C   | 190,5      | **+0,5 K** | 1 117 s (≈ 19 min)  | **0,7 K**                | 6,4 %  |
| E (Band 40 K, Ki 0,02)                              | 190 °C   | 196,4      | +6,4 K     | 472 s               | 0,7 K                    | 6,6 %  |
| P-only-Probe (Band 10–25 K)                         | 190 °C   | 163,5      | −26,5 K    | –                   | –                        | 5,4 %  |

Letzte Zeile: Kp = 1,8 erzeugt bei 190 °C einen **P-Offset von ≈ 27–30 K** – ein zu schmales Integralband erreicht den Sollwert nie.
Ableitung für die Praxis: Integralband ≥ P-Offset, oder Feedforward (F-02).

### 5.2 D-Anteil-Sägezahn (A vs. B, Aufheizen auf 190 °C, Minute 1)

```
A (SampleTime 100 ms): out = 217,6 | 79,7 | 217,2 | 217,3 | 79,4 | 216,9 | 79,0 | …   (Temperatur steigt gleichmäßig 0,25 K/Schritt)
B (SampleTime 250 ms): out = 179,3 | 179,3 | 179,3 | 179,0 | … | 233,5 (Quantisierungsstufe) | 178,2 | …
```

### 5.3 Autotune (10 % Basis, ±5 %, Noiseband 1,5, Lookback 10 s, Start 22 °C)

```
t= 1 min  38,9 °C  out 5,0 %      t= 6 min  93,2 °C
t= 2 min  52,2 °C                  t= 8 min 107,9 °C
t= 4 min  74,9 °C                  t=10 min 119,7 °C  → Zeitlimit, kein Ergebnis, keine Schwingung
```

### 5.4 Reproduzierbarkeit

Die Simulationsquellen waren temporär (außerhalb des Projekts) und wurden nach dem Review gelöscht. Auf Wunsch lassen sie sich als **`[env:native]` + Unit-Tests** (Abschnitt 9) in das Projekt überführen, damit
Regel- und Sicherheitslogik künftig ohne Hardware regressionsgetestet werden – gewünscht in Bedenken-Report Stufe 1.

---

## 6. Wartbarkeit

| Aspekt                     | Befund                                                                                                                                           | Bewertung                                                                  |
| -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------- |
| **Struktur**               | Sinnvolle Aufteilung (`controller`, `ui`, `web_interface`, `config`), Namespaces statt Klassen.                                                  | 🟢 gut für die Größe                                                        |
| **Kopplung/Testbarkeit**   | Alles in anonymen Namespace-Globals; direkter Zugriff auf `millis()`, `digitalWrite`, `Serial`, `MAX6675`. Keine Abstraktion, kein Mock möglich. | 🔴 Haupthindernis für Tests                                                 |
| **Zustandsmodell**         | Implizit über Flags (`heaterEnabled`, `tuning`, `latchedFault`, `sensorValid`…). Gültige Kombinationen nicht erzwungen.                          | 🟡 expliziter `enum class State` empfohlen                                  |
| **`update()`**             | ~100 Zeilen mit Sensor, Stall, Fehler, Autotune, Regelung, PWM, Historie, Log in einer Funktion.                                                 | 🟡 in Phasen-Funktionen teilen                                              |
| **Magic Numbers**          | `0x3C`, `250`, `30` ms, `5`, `1.5/0.05/0.15/10`, `190` im JS, …                                                                                  | 🟡 zentralisieren                                                           |
| **Konstantenstil**         | Pins als `#define`, Rest `constexpr`.                                                                                                            | ⚪ einheitlich `constexpr uint8_t`                                          |
| **Strings als Protokoll**  | Deutscher Statustext steuert JS-Logik (`startsWith`, `innerText === …`).                                                                         | 🟡 Codes/Enums + Übersetzung im UI                                          |
| **Webseite**               | 476 Zeilen HTML/CSS/JS als C++-Rohstring in `web_page.h`; Inline-Handler; Logo zweimal (Favicon + Inline-SVG); Zustand im DOM-Text.              | 🟡 LittleFS + `data/index.html`, `app.js`, `style.css` (gzip), Build-Skript |
| **Fehlerbehandlung**       | `begin()`-Funktionen ohne Rückgabewert; OLED-Retry vorbildlich.                                                                                  | 🟢/🟡                                                                        |
| **Kommentare/Doku**        | Sparsam und sachlich; README sehr ausführlich, aber monolithisch.                                                                                | 🟢                                                                          |
| **Reproduzierbarkeit**     | Libs/Plattform ungepinnt (F-22), kein Git, kein CI.                                                                                              | 🔴                                                                          |
| **Sicherheitsarchitektur** | Gute Prinzipien (Default-LOW, Latch). Aber Sicherheitslogik ist mit Regelung, UI-Strings und Autotune im selben File vermischt.                  | 🟡 *Safety Supervisor* trennen                                              |
| **Portabilität**           | Direkte ESP8266-Annahmen (kooperatives Scheduling, `A0`, GPIO16).                                                                                | 🟡 HAL-Schicht                                                              |

**Positiv:** `millis()`-Überlauf wird überall korrekt über Differenzen behandelt; keine `delay()`-Blockaden im Regelpfad; Eingaben werden vor Verwendung geprüft (`strtod` mit `end`-Test,
`isfinite`, Bereichsprüfung); `PROGMEM` für HTML; moderne ArduinoJson-7-API; `.gitignore` + `secrets.example.h`.

### Vorgeschlagene Zielarchitektur

```
lib/
  safety/      SafetySupervisor   (reine Logik: Messung+Zeit → FaultState, Latch, Plausibilitäten)  ← kritisch, 100 % getestet
  regulator/   PidRegulator       (SampleTime, bedingte Integration, D-Filter, Rampe, Feedforward)
  pwm/         TimeProportional   (Fenster, Halbwellen-Quantisierung, Sigma-Delta)
  history/     RingBuffer<T,N>    (Template, Statistik)
  autotune/    RelayAutotune      (eigene Implementierung)
  hal/         ITemperatureSensor, IHeaterOutput, IClock, IStorage   (+ ESP8266-/Mock-Implementierungen)
src/
  app.cpp (Verdrahtung), web_api.cpp, ui.cpp
test/
  test_safety/, test_regulator/, test_pwm/ (Unity, [env:native])
```

Die Sicherheitsregel „**SSR-Pin wird nur über `IHeaterOutput::set()` geschaltet; `SafetySupervisor` hat Veto-Recht**“ lässt sich damit im Typsystem festhalten und testen.

---

## 7. „State of the Art“-Abgleich

| Thema                | Ist                                        | Stand der Technik                                                                                        | Priorität |
| -------------------- | ------------------------------------------ | -------------------------------------------------------------------------------------------------------- | --------- |
| Reglerabtastung      | `PID_v1` Default 100 ms, Mismatch          | Abtastung = Messtakt, Compute nur bei neuem Wert                                                         | P0        |
| Anti-Windup          | Integrator-Reset durch Mode-Wechsel        | Conditional Integration / Back-Calculation / Clamping                                                    | P0        |
| D-Anteil             | Auf Rohwert (0,25 K Quantisierung)         | Gefilterter D (Tiefpass), D auf Messwert ✅(PID_v1 tut das)                                               | P0        |
| Autotune             | Relay-Lib mit Starttemperatur als Sollwert | Relay mit Hysterese um Testsollwert oder Sprungantwort; konservative Regeln (Tyreus-Luyben/No-Overshoot) | P1        |
| Sensor               | MAX6675 (12 Bit, wenig Diagnose)           | MAX31855/MAX31856 mit Fehlerregister, 50/60-Hz-Filter, ggf. zweiter Sensor                               | P0/P1     |
| Plausibilität        | NaN/Bereich                                | + Rohwort-Bits, Steigungsgrenze, „Heizen ohne Anstieg“, Redundanz                                        | P0        |
| Überwachung          | Software-Timeout, Soft-WDT                 | **Hardware-Watchdog/Dead-Man** + unabhängige Temperaturabschaltung                                       | P0        |
| Fehler-Gedächtnis    | RAM                                        | Persistent (RTC/Flash), Reset-Grund-Anzeige                                                              | P1        |
| Leistungsstellung    | 1-s-Fenster, ms-Pulse                      | Burst-Fire über Halbwellen, ggf. Nulldurchgangserkennung                                                 | P2        |
| Web-API              | Query-Parameter-POST, kein Schutz          | JSON-Body, Header-/Origin-Schutz, optional Auth, versionierte Pfade (`/api/v1`)                          | P1        |
| Echtzeitdaten        | Polling 1 s (+ 10 s Historie)              | **SSE/WebSocket** (in ESPAsyncWebServer vorhanden), Delta-Historie                                       | P2        |
| Statischer Webinhalt | C++-Rohstring, CDN                         | LittleFS, gzip, lokal gebündelt, SRI/Versionierung                                                       | P1        |
| Konfiguration        | Compile-Zeit, WLAN in Header               | WiFiManager/Captive Portal, mDNS, LittleFS/Preferences                                                   | P2        |
| Updates              | Kabel                                      | OTA (authentifiziert, nur im Fehlerfrei-/Heizung-aus-Zustand)                                            | P2        |
| Tests/CI             | keine                                      | PlatformIO Unity (`native`), GitHub Actions (`pio run`, `pio test`, `pio check`), clang-format/-tidy     | P1        |
| Build                | ungepinnt                                  | Gepinnte Plattform/Libs, `-Wall -Wextra`, `static_assert` für Sicherheitskonstanten                      | P1        |
| Versionierung        | keine                                      | Git, SemVer, Build-ID in Boot-Log/`/data`                                                                | P1        |
| Beobachtbarkeit      | Serial-CSV                                 | + Heap/RSSI/Reset-Grund/Uptime, `/metrics` (Prometheus) oder MQTT                                        | P2        |
| Doku                 | Fließtext                                  | Zustandsdiagramm (Mermaid), FMEA, Anforderungs-Trace-Matrix                                              | P1        |

---

## 8. Erweiterbarkeit

Wie gut kann das Projekt **heute** erweitert werden?

| Erweiterung                                         | Aufwand heute | Hindernis                                                           |
| --------------------------------------------------- | ------------- | ------------------------------------------------------------------- |
| Weitere Betriebsart (z. B. Temperaturprofil/Reflow) | hoch          | Kein Zustandsmodell, `update()` monolithisch, kein Rampenkonzept    |
| Zweiter Sensor / Redundanz                          | mittel        | Sensor direkt im Controller instanziiert, keine Schnittstelle       |
| Anderer Sensor (MAX31855/31856, PT100)              | mittel        | Gleiche Ursache                                                     |
| Anderer Aktor (Phasenanschnitt, Burst)              | mittel        | PWM-Logik in `update()` verwoben                                    |
| MQTT/Home-Assistant                                 | mittel        | Nur HTTP-Pull-API, Zustand nur über Einzelgetter                    |
| Parameter speichern/übernehmen                      | mittel        | Kein Persistenzlayer, Regler-Parameter `Kp/Ki/Kd` global & statisch |
| Mehrere Clients/Sessions                            | mittel        | Kein Berechtigungsmodell                                            |
| ESP32-Portierung                                    | mittel–hoch   | Kooperatives Scheduling vorausgesetzt (F-25)                        |
| Unit-Tests                                          | hoch          | Keine HAL-Abstraktion (F-24, Abschnitt 6)                           |

Eine **HAL-Schicht + ein explizites Zustandsmodell** wären die Investition mit dem größten Hebel: Sie machen Tests, Redundanz, neue Sensoren und die ESP32-Portierung gleichzeitig möglich.

---

## 9. Verbesserungsvorschläge (Roadmap)

### P0 – vor weiterer Heizlast

1. **F-05**: Rohwort-Validierung (Bit 15/1/2), Steigungsgrenze, „Heizen ohne Anstieg“-Überwachung, ggf. MAX31855/31856 + zweiter Sensor.
2. **F-06**: Hardware-Watchdog/Dead-Man-Schalter und unabhängige Temperaturabschaltung (Bedenken-Report §3) *verbindlich* vorsehen; tote Timeout-Pfade bereinigen oder dokumentieren.
3. **F-01**: Autotune bis zur Neuimplementierung im UI sperren und README-Hinweis („derzeit nicht funktionsfähig“).
4. **F-02/F-03**: `SetSampleTime(250)`, Compute nur bei neuem Messwert, D-Filter, **bedingte Integration** statt `SetMode`-Wechsel.
5. **F-07**: Header-/Origin-Prüfung der Schreibrouten oder Schreibrouten per Kompilierschalter deaktivierbar.
6. **F-04**: Konstante Serien aus `/history` entfernen; Chunked-Response oder Delta-Historie; Heap-Telemetrie ergänzen.
7. **F-24**: `git init`, erster Commit (ohne `secrets.h`, `logs/`), `logs/` in `.gitignore`.

### P1 – kurzfristig

8. **F-11**: SSR-LOW zuerst; zentrale `setSsr()`.
9. **F-08**: Fehler persistent, Reset-Grund anzeigen.
10. **F-09**: Taster nur Aus/Quittieren; Einschalten per Langdruck + Bestätigung.
11. **F-10**: Prädiktiver Abschaltpfad, Warnschwelle < `MAX_TEMP`, Ausgangsbegrenzung (z. B. 40 %).
12. **F-12/F-14**: Darstellungsfehler und OLED-Texte korrigieren.
13. **F-13**: Chart.js lokal/gepinnt mit SRI; Status-Polling unabhängig vom Diagramm.
14. **F-17/F-22**: Konstanten zentralisieren (`/config`), `static_assert`s, Libs pinnen, ungenutzte entfernen, `-Wall -Wextra`.
15. Tests: `[env:native]` mit Unity für `SafetySupervisor`, Regler, PWM; CI mit `pio run`, `pio test -e native`, `pio check`.

Beispielhafte `static_assert`s (Vorschlag, nicht angewendet):

```cpp
static_assert(SETPOINT_NORMAL_MAX <= SETPOINT_ABSOLUTE_MAX, "Bediengrenze > technische Grenze");
static_assert(SETPOINT_ABSOLUTE_MAX < MAX_TEMP,            "Sollwertgrenze muss unter Not-Aus liegen");
static_assert(AUTOTUNE_MAX_TEMP < MAX_TEMP,                "Autotune-Grenze muss unter Not-Aus liegen");
static_assert(SENSOR_TIMEOUT <= 750,                       "Sensor-Timeout-Vorgabe der Bedenken");
static_assert(LOOP_STALL_THRESHOLD < SENSOR_TIMEOUT,       "Stall muss vor Sensor-Timeout greifen");
```

(Hinweis: Wegen `SENSOR_INTERVAL + LOOP_STALL_THRESHOLD = 650 ms < SENSOR_TIMEOUT` ist der Sensor-Timeout bei synchroner Messung nie erreichbar, siehe F-06. Das lässt sich per
`static_assert` nur dokumentieren, nicht verhindern – ein Kommentar neben den Konstanten wäre ehrlicher als ein scheinbar schützender Timeout.)

Skizze für den Header-Schutz (Vorschlag):

```cpp
bool isTrustedRequest(AsyncWebServerRequest *r) {
  if (!r->hasHeader("X-Requested-With")) return false;           // erzwingt CORS-Preflight
  if (r->hasHeader("Origin")) {
    const String o = r->header("Origin");
    if (o != "http://" + WiFi.localIP().toString()) return false;
  }
  return true;
}
```

### P2 – mittelfristig

16. Eigene Relay-Autotune-Implementierung mit Hysterese und konservativen Regeln (F-01), Gerätelog + Download (F-15), „Übernehmen“-Funktion (F-16).
17. Burst-Fire/Sigma-Delta-PWM, Halbwellen-Quantisierung (F-19); Nulldurchgangssensor.
18. SSE/WebSocket statt Polling; mDNS; WiFiManager; OTA mit Heizung-aus-Voraussetzung.
19. ESP32-Portierung (F-25) mit separatem, hochpriorisiertem Regel-Task und Task-WDT.

---

## 10. Weitere Funktionen und verpasste Chancen

**Sicherheit/Betrieb**
- **„HEISS“-Anzeige** (Restwärme > 50 °C) auf OLED/Web auch bei ausgeschalteter Heizung; LED (die Onboard-LED an GPIO2 ist frei) und/oder **Buzzer** bei Fehler/Heizen.
- **Maximale Heizdauer / Auto-Aus** (z. B. 2 h) und **Inaktivitäts-Timer**; „Dead-Man“ für Fernbetrieb (Heizung aus, wenn ≥ N s kein Web-Heartbeat).
- **Stromsensor** (ACS712/ZMCT103C/INA219 mit Shunt): erkennt „SSR defekt ein/aus“, offenen Heizer, tatsächliche Leistung; ermöglicht netzspannungs-kompensierte Regelung (±10 % Netz = ±20 % Leistung).
- **Redundanter Schaltpfad** (zweites SSR/Schütz in Reihe, getrennter GPIO oder Sicherheits-MCU mit Komparator) und **Not-Aus-Taster** in der Netzzuführung.
- **Fehlerlog** (letzte N Fehler mit Ursache/Uptime/Reset-Grund) persistent, per Web abrufbar.
- **Hot-Plate-Handling bei Reset/Brown-out:** Reset-Grund aus `ESP.getResetInfoPtr()` anzeigen.
- **Anzeige des tatsächlichen SSR-Pins ≠ Last:** `digitalRead(SSR_PIN)` im CSV zeigt nur den Pinpegel; echte Rückmeldung nur über Stromsensor/Optokoppler.

**Regelung**
- **Sollwert-Rampen** und **Temperaturprofile** (Preheat/Soak/Reflow/Cool-down) – eine Heizplatte ist der klassische Reflow-Hotplate-Anwendungsfall.
- **Soak-Timer/Haltezeit** („X min halten, dann aus“).
- **Feedforward + Gain-Scheduling** (Strecke ist stark nichtlinear: Strahlung ∝ T⁴).
- **Modellbasierter Beobachter/Smith-Prädiktor** für die Verzögerung Element → Sohle → Sensor (löst „Nachheizen“ konzeptionell).
- Bessere Autotune-Strategie (F-01); Parametersatz-Verwaltung (Profile „Alu-Sohle“, „Reflow“).

**Bedienung**
- Einstellbarer Encoder-Schritt (1/5/10 K) und Beschleunigung; OLED-Mini-Graph; Anzeige von IP/WLAN-Status auch im Betrieb.
- Web: Fehlerbanner mit Zeit und Verbindungs-Heartbeat („Gerät antwortet nicht seit …“), Download von Verlauf/Autotune als CSV/JSON, Dark/Light, `lang`-Attribut, `aria-live`.
- Mehrsprachigkeit (de/en) über Textcodes.

**Vernetzung/Datenhaltung**
- **MQTT/Home-Assistant-Discovery**, Prometheus-`/metrics`, **NTP-Zeit** für Zeitstempel (derzeit nur `millis()` und Browser-Uhr).
- **Python-Logger/Plot-Skript** für die 100-ms-CSV inkl. Referenzthermometer-Eingabe, Kennzahlen (Überschwingen, Anstiegszeit, Hub) – **schließt die Lücke in Bedenken-Report §5**.
- **Alternative Stack-Option:** *ESPHome* bietet fertige `pid`-Climate-, `max6675`-, `ssd1306`-, `rotary_encoder`-, `web_server`- und OTA-Komponenten samt Autotune und Grenzwerten – als Referenz/Plan B sinnvoll
  (die Sicherheits-Spezifika müssten trotzdem separat geprüft werden).

---

## 11. In der Aufgabenstellung/Dokumentation nicht bedachte Punkte

1. **Fehlerinjektion/Negativtests** am Gerät: Sensor ziehen (SO/SCK/CS/VCC einzeln), Kurzschluss gegen GND/3V3, WLAN abschalten, Heap-Erschöpfung provozieren – sind in den Prüfstufen nicht ausdrücklich enthalten.
2. **Firmware-Hänger** (Watchdog) und **Stromausfall während Heizen** (Wiederanlauf!): Heizung bleibt nach Boot aus ✅ – aber was zeigt/loggt das System dazu (F-08)?
3. **Brown-out/Spannungsqualität**: SSR-/WLAN-Stromspitzen am 3,3-V-Regler, Netzstörungen (EMV) auf Thermoelementleitung (Rauschen belegt, F-03).
4. **Netzspannungsschwankung**: Heizleistung ∝ U² (±10 % U → ±20 % P) beeinflusst Haltedauer, PWM-Bewertung und Autotune-Ergebnisse.
5. **Konverter-Eigenschaften**: MAX6675 ±3 °C Genauigkeit, nur 12 Bit, keine Fehlerbits außer „offen“; Kaltstellen-Kompensation sitzt am Konverter (nicht neben der Heizung montieren).
6. **Mechanik/Thermik**: Sensorankopplung (Verzögerung τ_S), Heißpunkte der Sohle vs. Messort, Wärmeleitung in den Sensor, Gradient im Aluminium – wirken auf jede Abschaltschwelle.
7. **Lebensdauer des SSR bei Kurzschluss-Ausfall** (häufiger Fehlermodus „klebt ein“) – nur die Hardwareschutzkette hilft (im Report adressiert); Software kann „Heizen trotz Aus“ nur erkennen.
8. **Datenschutz/Geheimnisse**: WLAN-Passwort im Klartext im Projektordner, im Firmware-Image und im Flash; Projekt nicht unter Versionskontrolle (Sicherung/Weitergabe-Risiko). Logs enthalten keine Zugangsdaten, sollten aber nicht ungeprüft geteilt werden (IP, Zeitverläufe).
9. **Lizenzen** bei Veröffentlichung/Weitergabe der Firmware: ESPAsyncWebServer/ESPAsyncTCP (LGPL-3.0) – statisches Linken bedeutet Pflichten (Quelltext/Relinking-Möglichkeit, Hinweise); U8g2 (BSD), PID (MIT), ArduinoJson (MIT), Adafruit-MAX6675 (Public Domain). Eine eigene Projektlizenz fehlt.
10. **Haftung/Normen**: Bei Weitergabe an Dritte gelten Anforderungen für Heizgeräte (z. B. IEC/EN 60335-1, 60730 Softwareklasse). Das Projekt ist als Eigenbau ausdrücklich keine Freigabe; ein Hinweis zur Weitergabe fehlt.
11. **Mehrbenutzer-/Mehr-Tab-Szenarien**: Zwei offene Browser ⇒ doppelte `/history`-Last (F-04), konkurrierende Setpoint-Befehle; kein „Wer hat zuletzt geändert“.
12. **Zeit/Uhr**: Keine Geräteuhr; Diagrammzeit hängt vom Browser; Logs nur `millis()` → Korrelation mit Referenzmessung mühsam (NTP/RTC).
13. **Langzeitbetrieb**: Heap-Fragmentierung durch per-Sekunde-`JsonDocument` + `String`, 49,7-Tage-Überlauf (korrekt gehandhabt), Flash-Verschleiß bei künftiger Persistenz.
14. **Barrierefreiheit/Internationalisierung** der Weboberfläche (Kontraste gut; `lang`, `aria-live` fehlen).
15. **Branding/Namen**: Projekt „Heizplatte“, Repo-README/Web „Flatiron PID-Controlled“, Firmware-Strings gemischt Deutsch/Englisch.
16. **Wiederholbarkeit des Reviews**: Ohne Git, Version/Build-ID und Tests ist nicht nachvollziehbar, *welcher* Stand gemessen/geloggt wurde (F-24).

---

## 12. Ergänzungen für die Prüfreihenfolge (Bedenken-Report §5)

Empfohlene zusätzliche Kriterien/Prüfschritte:

| Stufe              | Ergänzung                                                                                                                                                                                                                                                          |
| ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 1 (Unit/HAL)       | Tests für: Rohwort-Validierung, Steigungsgrenze, „Heizen ohne Anstieg“, Latch + Reset-Persistenz, Stall-Handling, Grenzprüfungen (alle Eingabepfade), `static_assert`s, Autotune-Abbruchpfade, PWM-Quantisierung. Ziel: 100 % Zweigabdeckung der Sicherheitslogik. |
| 2 (Kaltstart)      | **Fehlerinjektion** je Leitung (SO/SCK/CS/VCC/GND), Messung „Zeit bis SSR LOW“ mit Logikanalysator; Watchdog-/Dead-Man-Test (Loop absichtlich blockieren → SSR muss binnen Soll fallen); Reset während Fehler → Fehlergedächtnis.                                  |
| 3 (niedrig heizen) | σ des Sensors **bei laufendem SSR**; Vergleich mit Referenz; Stallhäufigkeit mit offener Webseite über ≥ 2 h (Heap-/Stall-Telemetrie).                                                                                                                             |
| 4 (PWM)            | Auswertung mit Skript (Hub/Welligkeit pro Duty 5/10/20 %), Vergleich 1000/500/200 ms und Burst-Fire.                                                                                                                                                               |
| 5 (Regelversuche)  | **Nachheiz-Messung** (Sohle *und* Element/Referenz) beim Abschalten aus 100/150/190 °C – belegt oder widerlegt die 15-K-Reserve (F-10).                                                                                                                            |
| 6 (Autotune)       | Erst nach Neuimplementierung (F-01); Akzeptanz: Schwingungen nachweisbar, Ku/Tu plausibel, Vorschlag im Regelversuch ohne Überschwingen über Grenzwert.                                                                                                            |
| Zusätzlich         | Langlaufversuch ≥ 4 h mit Heap-/Reset-Protokoll; CSRF-Test (fremde Seite → POST) nach Umsetzung von F-07.                                                                                                                                                          |

---

## 13. Was bereits gut gelöst ist

- Sicherer Initialzustand (SSR LOW im `begin()`, Pin-Boot-Analyse in README/`config.h`), Heizung nach Boot/Fehler **aus**.
- Latch mit **Fehlerursache**, lokaler Quittierung nur bei gültiger Messung und < 60 °C, kein Wiederanheizen durch Quittierung.
- **Stall-Erkennung** als Reaktion auf einen realen Befund (887 ms beim WLAN-Aufbau) samt `STALL`-Log.
- Autotune: Startverweigerung mit Begründung (HTTP 409 + Log-Fenster), Abbruch bei Temperatur/Zeit/Fehler/Taster/Web, Vorschlagsvalidierung (`isfinite`, positive Grenzen), keine automatische Übernahme.
- Eingabeprüfung an allen Pfaden (`/setpoint` mit `strtod` + End-Zeiger, Wertebereich, 400/409 mit Ursache).
- **Diagnose-CSV** mit 100-ms-Takt, Spalten für Rohwert/Alter/Loop-Gap; separate Hardwaretest-Routine.
- OLED-Init nicht blockierend mit Wiederholung; entprellter Taster; ehrliche, deutlich formulierte Sicherheitshinweise in README.
- Konstanten zentral in `config.h`; Schaltplan/README/Firmware-Grenzen konsistent (190/200/215/750).
- Build läuft sauber (RAM 55 %, Flash 34,5 %), cppcheck ohne Befund im eigenen Code.

---

## 14. Offene Fragen an den Betreiber

1. Wie sind Sensor und Heizelement **mechanisch** gekoppelt (Position, Befestigung, Kontaktmasse)? Gibt es bereits ein **Zweitthermometer**?
2. Welche **unabhängige** Abschaltung (Bimetall/Thermostat, einstellbar, selbsttätig rückstellend?) ist vorgesehen, und gibt es eine Heartbeat-/Watchdog-Schaltung?
3. Ist der Betrieb von mehreren Browsern/Personen gleichzeitig vorgesehen? Soll Web **überhaupt schalten dürfen** oder nur anzeigen?
4. Soll das Gerät für **Reflow** (Profile) oder dauerhaftes Halten genutzt werden? Das entscheidet über Regelkonzept (Rampen, Vorsteuerung).
5. Sollen Weitergabe/Veröffentlichung geplant sein (Lizenz, Haftungshinweis, Normbezug)?
6. Darf ein Plattformwechsel (ESP32) erwogen werden?

## 14.1 Antworten des Betreibers

1. Sensor ist nahe am Heizelement eingeschraubt, ein Zweitthermometer ist nicht vorhanden.
2. In der Zuleitung zum Heizelement befinden sich eine Schmelzsicherung (15 A / 240 °C) und ein Dead-Man-Schalter. Der Betreiber bestätigt, dass die Schutzkette von einem Fachmann abgenommen wurde.
3. Nein, ausschließlich eine Person bedient das Gerät über einen Browser, daher ist volle Bedienung über Browser vorgesehen.
4. Erstmal nur  Temperatur halten, Reflow-Profile sind vorerst nicht relevant.
5. Weitergabe oder Veröffentlichung ist derzeit nicht geplant.
6. Ein Plattformwechsel auf ESP32 ist derzeit nicht vorgesehen.

---

*Hinweis zur Verbindlichkeit:* Dieser Bericht ist eine Software-/Konzeptprüfung aus dem Quelltext, den Bibliotheksquellen, den vorhandenen Logs und einer Modellrechnung. Er ersetzt weder
die elektrische Abnahme durch eine Elektrofachkraft noch die in `bedenken-report.md` geforderten Messreihen am realen Aufbau.

## 15. Umgesetzte Fehlerbehebungen F-05, F-06 und F-01 bis F-04

Die folgenden Änderungen wurden nach Erstellung dieses Reviews umgesetzt. Schwellenwerte und Regelparameter sind Firmware-Startwerte; sie sind **nicht durch Versuche an der realen Heizplatte validiert** und stellen keine Freigabe zum Betrieb mit Netzlast dar.

### F-05 – MAX6675-Rohwort und Messwert-Plausibilisierung

- Die MAX6675-Bibliotheksabstraktion wurde für die Regelungs- und Hardwaretest-Messung durch `src/max6675_reader.*` ersetzt. Der Leser erfasst jetzt das vollständige 16-Bit-Wort, verwirft gesetzte Reserved-/Fehlerbits (Bit 15, Bit 2 und Bit 1) und berechnet die Temperatur aus dem Rohwort. Das Rohwort wird auch in Diagnose- und Fehlerzeilen ausgegeben.
- Die Regelung verriegelt bei einem gemessenen Anstieg über 20 K/s (`SENSOR_MAX_RISE_RATE_C_PER_S`) sowie dann, wenn bei mindestens 15 % Stellgröße und mindestens 5 K Sollwertabstand 30 s lang weniger als 0,5 K Temperaturanstieg erkannt wird. Der Fehlerstatus lautet `Sensor unplausibel`.
- Das erkennt Statusbitfehler, unphysikalische Sprünge und bestimmte eingefrorene Messwerte, aber nicht jeden plausibel erscheinenden Sensorfehler. Die Schwellwerte benötigen Prüfung mit Referenzthermometer und tatsächlichem thermischen Aufbau; es gibt weiterhin keinen zweiten unabhängigen Sensor.

### F-06 – Erreichbare Timeout-Aussage und Hänger-Restgefahr

- Die nicht erreichbaren `SensorStartupTimeout`-/`SensorStaleTimeout`-Latch-Pfade wurden entfernt. `SENSOR_TIMEOUT` bleibt ausdrücklich nur als Frischegrenze für Freigaben und Statusanzeige. Die Stall-Behandlung bleibt eine Reaktion nach Wiederaufnahme des Regel-Loops; sie kann einen vollständig blockierten Loop nicht überwachen.
- Die Firmware enthält weiterhin keinen eigenen Dead-Man/Heartbeat-Ausgang. Der Betreiber bestätigt inzwischen, dass ein externer Dead-Man-Schalter vorhanden und die Schutzkette durch einen Fachmann abgenommen ist; zusätzlich ist eine 15-A-/240-°C-Schmelzsicherung in der Heizerzuleitung vorhanden. Ein echter Firmware-Hänger kann den SSR-Pegel bis zum ESP8266-Watchdog-Reset halten; die externe Schutzkette ist daher die maßgebliche unabhängige Schutzebene. Ihre konkrete Schaltung und Auslösezeit wurden im Rahmen dieses Software-Reviews nicht selbst gemessen.

### F-01 – Relay-Autotune um den eingestellten Test-Sollwert

- Die bisherige `PID_AutoTune_v0`-Verwendung wurde entfernt. Der neue Test schaltet 0 %/15 % mit ±1,5 °C Hysterese um den in der UI eingestellten Test-Sollwert und erfasst Temperaturzyklen, Amplituden und Perioden.
- Nach mindestens sechs Zyklen werden Ku/Tu-basierte konservative Vorschläge (Kp = 0,2·Ku, Ti = 0,5·Tu, Td = 0,33·Tu) berechnet und durch die vorhandenen Positiv-/Maximalgrenzen geprüft. Die PID-Werte werden weiterhin nie automatisch übernommen. Startprüfung, Abbruch bei 120 °C/10 min, Sensorfehler und manuelle Abbruchwege bleiben erhalten.
- Ausgang, Zyklenerkennung und Parameterformeln sind noch am realen Gerät zu validieren. Der Test ist nur beaufsichtigt, mit unabhängiger Temperaturmessung und externer Temperaturabschaltung durchzuführen.

### F-02 – Integrator beim Sollwertdurchgang erhalten

- Die Betriebslogik schaltet den PID-Regler nicht mehr bei jedem Erreichen des Sollwerts auf MANUAL und zurück auf AUTOMATIC. Oberhalb des Sollwerts wird weiterhin die Stellgröße auf null gezwungen; der PID-Integrator wird dabei nicht durch einen Moduswechsel zurückgesetzt.
- Integration ist nur bei positivem Fehler innerhalb eines 40-K-Bands unter dem Sollwert freigegeben (`PID_INTEGRAL_BAND_C`). Damit wird Windup während des großen Aufheizfehlers begrenzt, während der Integrator in Zielnähe Haltleistung aufbauen kann. Das 40-K-Band ist aus der Berichtssimulation übernommen und muss thermisch verifiziert werden.

### F-03 – PID-Berechnung und gefilterte Messgröße synchronisieren

- `SetSampleTime(SENSOR_INTERVAL)` setzt die PID-Abtastzeit auf 250 ms; `Compute()` wird nur nach einem neuen gültigen Messwert aufgerufen. Die dem PID zugeführte Messgröße erhält einen zeitabhängigen Tiefpass mit 1-s-Zeitkonstante (`PID_D_FILTER_TAU_MS`), um quantisierte Sprünge und Rauschen vor allem im D-Anteil zu dämpfen.
- Der Tiefpass liegt auf der PID-Messgröße und wirkt daher auch auf den P-Anteil. Das muss bei realen Aufheiz-/Überschwingversuchen geprüft werden; Kp/Ki/Kd sind gegenüber der ursprünglichen Messkette nicht als neu abgestimmt zu betrachten.

### F-04 – `/history` ohne Gesamtpuffer und quadratische Reallokation

- `/history` wird nun mit `beginChunkedResponse()` und einem zustandsbehafteten Generator mit begrenztem Ausgabepuffer übertragen, statt eine vollständige JSON-Antwort in `AsyncResponseStream` aufzubauen.
- Die drei konstanten Kp/Ki/Kd-Verlaufsarrays wurden aus dem Verlauf entfernt; `/history` liefert weiter Temperatur, Sollwert und PWM mit 5-s-Intervall. Die PWM wird mit zwei Dezimalstellen ausgegeben. Die Browserabfrage pausiert in versteckten Tabs, hat einen Timeout und zeigt Abruffehler an.
- Damit entfällt der berichtete Gesamtpuffer-/Reallocationspfad. Der Chunk-Handler selbst und die maximale Antwortdauer sind noch auf dem Gerät und bei mehreren Clients zu testen.

### Validierung und verbleibende Prüfschritte

- Firmware-Build `pio run -e nodemcuv2`: **erfolgreich**; RAM 53,8 % (44 036 / 81 920 Byte), Flash 34,5 % (360 409 / 1 044 464 Byte). Es gab nur Deprecation-Warnungen aus der ESPAsyncWebServer-Abhängigkeit; keine Buildfehler.
- Der Build validiert weder das Verhalten des Thermoelements/SSR noch Regelgüte oder Sicherheitswirksamkeit. Vor Heizlast sind die Rohwort-Fehlerinjektion, festhängende Sensorwerte, Stall-/Watchdog-Verhalten, `/history`-Antwort über den gesamten Ringpuffer und beaufsichtigte Niedrigtemperatur- sowie Regelversuche mit Referenzthermometer durchzuführen.
- Ergänzung des Betreibers: Dead-Man-Schalter ist vorhanden und die Schutzkette wurde fachmännisch abgenommen. Der externe Schutz wurde für diesen Bericht nicht erneut geprüft; dessen korrekte Funktion und die fachmännische Abnahme werden hier als Betreiberangabe dokumentiert. Der Softwarebefund bleibt: Der ESP8266-Regelloop kann einen eigenen Hänger nicht rechtzeitig selbst abschalten.
