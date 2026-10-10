# Bericht zu den Sicherheitsbedenken der Heizplatte

**Stand:** 7. Oktober 2026  
**Grundlagen:** [bedenken.md](bedenken.md), [README.md](README.md), die aktuelle Firmware, der
[Schaltplan](docs/schaltplan.svg) und die vorhandenen Hardwaretest-Protokolle in `logs/`.

## 1. Zusammenfassung und Sicherheitsbewertung

Das Projekt steuert eine offen aufgebaute 2-kW-Bügeleisensohle. Wegen der geringen thermischen
Masse kann sie nach einem fehlerhaften oder zu langen Heizeintrag sehr schnell über die
Auslösetemperatur der fest montierten, nicht rückstellbaren 230-°C-Thermosicherung steigen. Die
verbleibenden 15 °C zwischen der in den Bedenken geforderten Softwareabschaltung (215 °C) und
der Thermosicherung sind kein ausreichender Regelpuffer: Sensorlage, Messquantisierung,
Wärmegradienten und Nachheizen können diesen Puffer aufbrauchen.

Die aktuelle Firmware entspricht den genannten Sicherheitsvorgaben noch nicht:

| Thema | Aktueller Stand | Erforderlicher Sollzustand |
|---|---|---|
| Temperaturgrenzen | `SETPOINT_MAX = 250 °C`, `MAX_TEMP = 280 °C` | Normaler Sollwert höchstens 190 °C, niemals über 200 °C; latched Software-Not-Aus ab 215 °C |
| Sensorausfall | Abschaltung erst nach 2.000 ms | Abschaltung nach höchstens 750 ms und sofortiger SSR-Ausgang LOW |
| PID-Startwerte | `Kp=50`, `Ki=1`, `Kd=5` | Vorsichtige Startwerte `Kp=1,8`, `Ki=0,005`, `Kd=55` |
| Autotune | 50 % Grundlast, 50-%-Leistungssprung, 20 s Lookback | 10 % Grundlast, 5-%-Sprung, 1,5 °C Noiseband, 10 s Lookback; nur unter überwachten Bedingungen |
| Bedienoberfläche | Keine Fehler-/Leistungsanzeige, Autotune ohne Sperre auslösbar | Fehlerzustand und Ausgang sichtbar; Autotune vor Start bestätigen und in unsicheren Zuständen verweigern |
| Dokumentation | Schaltplan nennt die bisherigen 280 °C / 2 s | Schaltplan, README und UI-Grenzen müssen die neuen Werte einheitlich ausweisen |

Die 25 in `logs/` liegenden Protokolle sind Aufzeichnungen des auskommentierten
`HardwareTest::run()` und zeigen nur Encoder-, Display- und kurze SSR-Manipulationstests bei
circa Raumtemperatur. Sie sind **kein** thermischer Regelversuch und belegen weder Overshoot
noch sicheres Abschalten.

> **Freigabeentscheidung:** Bis die Maßnahmen aus Abschnitt 2 umgesetzt, elektrisch geprüft und
> die Messreihen aus Abschnitt 5 bestanden sind, darf die 2-kW-Heizung nicht unbeaufsichtigt
> und nicht für Autotune betrieben werden.

## 2. Firmware: verbindliche Änderungen

### 2.1 Zentrale Grenzwerte und konservative Initialisierung

In [src/config.h](src/config.h) sollten die sicherheitsrelevanten Konstanten zentral und
sprechend definiert werden:

```cpp
constexpr unsigned long WINDOW_SIZE = 1000;
constexpr double SETPOINT_MIN = 0.0;
constexpr double SETPOINT_NORMAL_MAX = 190.0;
constexpr double SETPOINT_ABSOLUTE_MAX = 200.0;
constexpr double MAX_TEMP = 215.0;
constexpr unsigned long SENSOR_TIMEOUT = 750;
constexpr unsigned long SENSOR_INTERVAL = 250;
```

`SETPOINT_NORMAL_MAX` ist der einzige Wert, den Encoder und Weboberfläche anbieten. Der
absolute Wert von 200 °C dient ausschließlich als zweite Prüfung auf allen Eingabepfaden; er
ist keine Bedienfreigabe. Damit sind Änderungen an Web-UI, `/setpoint` und
`Controller::adjustSetpoint()` konsistent. Die JavaScript-Grenze von derzeit 250 °C darf nicht
die maßgebliche Begrenzung sein, sondern muss auf 190 °C angepasst werden; der Server bleibt
die verbindliche Instanz.

In [src/controller.cpp](src/controller.cpp) sind vor dem Konstruktor des PID die folgenden
Startwerte zu setzen:

```cpp
double Kp = 1.8, Ki = 0.005, Kd = 55.0;
```

Diese Werte sind eine vorsichtige Erstparametrierung, keine allgemein gültige physikalische
Kalibrierung. Die endgültige Freigabe erfolgt ausschließlich aus gemessenen Heizkurven mit der
konkret aufgebauten Sohle, Thermoelementposition und Umgebung. Parameter aus einem Autotune
dürfen diese Werte nicht automatisch als neue Betriebswerte übernehmen (siehe Abschnitt 2.4).

### 2.2 Fail-safe-Sicherheitszustand vor jeder Regelung

Der aktuelle Ablauf berechnet PID bzw. Autotune vor der Abschaltbehandlung. Das ist zwar im
gleichen Loop-Durchlauf meist unschädlich, ist aber unnötig fehleranfällig und erschwert eine
verlässliche Fehlersemantik. Statt eines einzelnen `fault`-Booles sollte der Controller einen
expliziten Sicherheitszustand mit Fehlerursache führen, zum Beispiel:

```cpp
enum class FaultReason { None, SensorTimeout, SensorValue, OverTemperature };
```

Für jeden `update()`-Durchlauf ist die Reihenfolge verbindlich:

1. Erst bei Fälligkeit eine Messung erfassen und nur eine plausible, nicht-NaN-Messung als
   gültig registrieren. Zusätzlich zum `isnan()`-Test sind unplausible Sensorwerte und die vom
   verwendeten MAX6675-Treiber signalisierten Fehler zu behandeln.
2. Vor PID und Autotune prüfen, ob die letzte gültige Messung älter als 750 ms ist oder die
   Temperatur mindestens 215 °C beträgt.
3. Bei einem Fehler **unmittelbar** `digitalWrite(SSR_PIN, LOW)` ausführen, `out = 0` setzen,
   PID in `MANUAL` versetzen und einen laufenden Autotune abbrechen. Der normale
   Slow-PWM-Ausdruck darf in diesem Pfad nicht mehr entscheiden.
4. Die beiden gefährlichen Fehler (`SensorTimeout`, `SensorValue`, `OverTemperature`) verriegeln.
   Der Ausgang bleibt aus, auch wenn nachträglich wieder ein gültiger Messwert eintrifft oder
   die Temperatur fällt. Ein bewusster, lokaler Reset ist erst nach Abkühlung und
   Fehlerursachenprüfung zulässig; ein automatisches Wiederanheizen ist auszuschließen.
5. Nur im fehlerfreien Zustand PID berechnen und danach Slow-PWM schalten.

Beim Start ist `lastValidReading` nicht als frische reale Messung zu behandeln. Die Heizung
bleibt gesperrt, bis mindestens eine plausible Sensorprobe vorliegt. Der bereits vorhandene
Bootzustand des GPIO15 und das frühe `digitalWrite(SSR_PIN, LOW)` bleiben erhalten, ersetzen
aber diese Freigabelogik nicht.

Der Web-Endpunkt `/data` sollte `fault`, `faultReason`, `output` und `tuning` liefern. OLED
und [src/web_page.h](src/web_page.h) müssen einen verriegelten Fehler deutlich als
„HEIZUNG AUS – <Ursache>“ anzeigen; die aktuelle reine Sensoranzeige ist für Übertemperatur
und andere Sperren missverständlich.

### 2.3 Zeitproportionale SSR-Ansteuerung

Das 1-s-Fenster erzeugt bei 10 % Stellgröße einen zusammenhängenden 100-ms-Impuls mit voller
2-kW-Leistung, dann 900 ms Pause. Das kann bei einer offenliegenden Sohle temperaturseitig
zu Sprüngen führen, obwohl der Mittelwert klein wirkt. Die PWM-Logik muss deshalb:

- bei jeder Sicherheitsabschaltung das SSR ohne Warten auf das Fenster ausschalten;
- beim Start, beim Verlassen eines Fehlers und nach dem Abbruch eines Autotunes mit `out = 0`
  und einer sauber neu gesetzten Fensterzeit beginnen;
- die abgegebene Leistung, die Temperatur und die Zeitbasis in einer für die Auswertung
  ausreichenden Auflösung protokollieren. Die heutige 5-s-Historie ist für 100-ms-Impulse nicht
  geeignet.

Ob das 1-s-Fenster tragbar ist, ist eine Messfrage (Abschnitt 5). Bei erkennbaren
impulsbedingten Temperaturwellen ist zunächst ein kürzeres, zum verwendeten
Zero-Cross-AC-SSR passendes Zeitfenster an einem sicheren Prüfstand zu validieren. Falls das
SSR die notwendige Schalthäufigkeit nicht spezifiziert unterstützt, ist es durch ein für die
Last, Netzspannung und die geprüfte Ansteuerungsart geeignetes Bauteil zu ersetzen. Ein
Phasenanschnitt ist keine ad-hoc-Softwareänderung; er erfordert EMV-, Isolations- und
Sicherheitsbewertung.

### 2.4 Autotune strikt begrenzen und isolieren

In `startAutotune()` sind mindestens diese Werte zu verwenden:

```cpp
out = WINDOW_SIZE * 0.10;
autotune.SetNoiseBand(1.5);
autotune.SetOutputStep(WINDOW_SIZE * 0.05);
autotune.SetLookbackSec(10);
```

Zusätzlich braucht `startAutotune()` eine Rückgabe mit Erfolg/Fehler statt einer
bedingungslosen Zustandsänderung. Start verweigern, wenn ein Fehler verriegelt ist, noch keine
valide Messung vorliegt, die Sohle nicht ausreichend kalt ist, bereits getunt wird oder der
Sollwert nicht in einem eigens festgelegten, niedrigeren Testbereich liegt. Die Webroute muss
diesen Fehler als HTTP-Fehler mit Ursache zurückgeben; der Button zeigt die Antwort an und ist
während des Tunings deaktiviert.

Autotune benötigt außerdem eine unabhängige Laufzeitbegrenzung und eine Temperaturgrenze unter
215 °C. Nach Rückgabe der Bibliothek sind `GetKp()`, `GetKi()` und `GetKd()` auf endliche,
positive und vorab definierte sichere Bereiche zu prüfen. Ungültige oder außerhalb der
Schranken liegende Werte werden verworfen, der Regler bleibt bei den konservativen
Startparametern und ein Fehler wird angezeigt. Auch zulässige Werte sind zunächst nur als
**Vorschlag** zu speichern und mit Temperaturkurve zu dokumentieren; eine automatische
Übernahme für den normalen Betrieb ist bei diesem System nicht vertretbar.

### 2.5 Bedien- und Schnittstellensicherheit

- Der verdrahtete Encoder-Taster (`ENC_SW`) wird derzeit im Regelbetrieb nicht verwendet.
  Er sollte, nach sicherer Entprellung, als lokaler **Heizung-aus-/Quittiertaster** dienen.
  Wegen GPIO0 darf er beim Booten weiterhin nicht gedrückt sein. Ein Reset muss niemals einen
  übertemperaturbedingten Fehler ohne Abkühl- und Ursachenprüfung freigeben.
- Webzugriffe dürfen weder einen verriegelten Fehler löschen noch die Heizung stillschweigend
  wieder starten. Der bestehende Hinweis, das ungeschützte Webinterface nur im
  vertrauenswürdigen Netz zu verwenden, bleibt gültig; für einen Heizaktor ist eine
  Netzwerkfreigabe mit Zugriffsschutz oder vollständiger Deaktivierung der Steuerrouten
  vorzuziehen.
- Die UI muss den Sollwert bei 190 °C begrenzen, den aktuellen Ausgang in Prozent ausweisen,
  Fehlergrund und Verriegelungsstatus darstellen und vor Autotune eine explizite
  Sicherheitsbestätigung verlangen.
- [README.md](README.md) und der Text in [docs/schaltplan.svg](docs/schaltplan.svg) sind
  gleichzeitig mit der Firmware zu ändern. Insbesondere ist die aktuelle Angabe
  „>280 °C oder >2 s Sensorausfall“ falsch, sobald die Vorgaben umgesetzt wurden.

## 3. Hardwaremaßnahmen

Netzspannung und eine offene 2-kW-Heizfläche sind eine eigenständige Gefahr. Die folgenden
Punkte sind keine optionalen Ergänzungen zur Software, sondern Voraussetzungen für einen
sicheren Aufbau:

1. **Unabhängige Temperaturabschaltung:** Die vorhandene, permanent auslösende
   230-°C-Thermosicherung bleibt in Reihe zur Last. Ergänzend muss eine unabhängige,
   korrekt thermisch angekoppelte Schutzabschaltung vorgesehen werden, die vor dem
   Schmelzeinsatz öffnet. Auswahl, Ankopplung und Auslösetemperatur müssen eine
   Elektrofachkraft anhand der realen Temperaturmessung festlegen. Ein Softwarewert ist kein
   Schutzorgan.
2. **Kontakt und Sensorposition:** Das Typ-K-Thermoelement fest, flächig und elektrisch
   sicher an der relevanten Oberfläche ankoppeln; lose aufliegende Fühler messen die
   Heizkörpertemperatur unzuverlässig. Die Temperatur an der Sensorposition und an den
   wahrscheinlich heißesten Stellen mit einem zweiten, kalibrierten Messmittel vergleichen.
3. **SSR und Kühlung:** Das AC-SSR muss ein echtes, für ohmsche AC-Lasten geeignetes
   Zero-Cross-Modell sein, mindestens für 25 A spezifiziert und nach Datenblatt auf einem
   ausreichend dimensionierten Kühlkörper montiert. Die 3,3-V-Kompatibilität des
   Steuereingangs und der erforderliche GPIO-Strom sind zu prüfen; bei Unklarheit ist eine
   galvanisch passende Treiberstufe nötig.
4. **Netzaufbau:** Phase über die zur Leitung und Last passende Sicherung, anschließend
   Thermoschutz und SSR; Schutzleiter dauerhaft und niederohmig an jeden berührbaren
   Metallkörper. Neutralleiter und Schutzleiter werden nicht geschaltet. Netz- und
   Kleinspannungsbereich müssen getrennt geführt, zugentlastet, isoliert, berührungssicher
   gekapselt und normgerecht aufgebaut sein.
5. **Fehlerfall:** Ein spannungsführendes oder kurzgeschlossenes SSR kann dauerhaft heizen.
   Der unabhängige Thermoschutz, eine allpolige, erreichbare Netztrennung und die
   Absicherung müssen auch diesen Fehler beherrschen. Das SSR ist kein Sicherheitsschalter.
6. **Umgebung:** Nicht auf brennbare Unterlage stellen, Freiraum um Sohle und Kühlkörper
   schaffen, kein Wasser zur Kühlung verwenden und die Platte nicht in einem geschlossenen,
   schlecht belüfteten Gehäuse betreiben. Ein geeigneter Feuerlöscher und eine leicht
   erreichbare Trennmöglichkeit sind vor dem Test bereitzuhalten.

Alle Arbeiten im 230-V-Kreis, einschließlich Prüfung von Schutzleiter, Isolation, Sicherung,
Kühlkörper und Gehäuse, sind einer qualifizierten Elektrofachkraft vorbehalten.

## 4. Bedienvorgaben

1. Vor dem Einschalten Sichtprüfung von Gehäuse, Schutzleiter, Sensorbefestigung,
   Thermoschutz, SSR-Kühlkörper und Leitungen. Kein Betrieb bei beschädigten Leitungen,
   lockerem Sensor oder unklarer Fehlermeldung.
2. Beim Booten Encoder-Taster loslassen; er liegt an GPIO0 und würde den ESP sonst in den
   Flash-Modus bringen. Abwarten, bis die UI/OLED einen aktuellen, plausiblen Messwert und den
   fehlerfreien, jedoch zunächst ausgeschalteten Zustand zeigt.
3. Zuerst bei niedrigem Sollwert und unter fortlaufender Aufsicht anfahren. Die Platte nie
   unbeaufsichtigt, nie in Reichweite brennbarer Materialien und nie mit dem Webinterface als
   einzigem Überwachungsweg betreiben.
4. Im Normalbetrieb maximal 190 °C einstellen. 200 °C ist keine zulässige
   Bedienvorgabe, sondern ausschließlich eine technische Obergrenze für alle Eingaben.
5. Autotune nur bei kalter Platte, montiertem Thermoelement, freiem Arbeitsbereich,
   unabhängiger Temperaturbeobachtung und einer Person an der Netztrennung starten. Während
   des Tests keine höheren Sollwerte setzen und den Versuch bei unerwartetem Anstieg sofort
   lokal abbrechen bzw. allpolig trennen.
6. Nach jeder Fehlerverriegelung: Netz trennen, Platte vollständig abkühlen lassen,
   Sensor-/Leitungsursache prüfen und erst danach gezielt quittieren. Hat die
   230-°C-Thermosicherung ausgelöst, darf sie nicht überbrückt werden; Ursache beheben und
   das Schutzteil fachgerecht ersetzen.

## 5. Prüfreihenfolge und Abnahmekriterien

Die Schritte sind in dieser Reihenfolge auszuführen und zu protokollieren. Eine Stufe wird nur
nach bestandener vorheriger Stufe freigegeben.

| Stufe | Durchführung | Bestehenskriterium |
|---|---|---|
| 1. Statischer Firmwaretest | Grenzwerte, Fehlerzustände, verriegeltes Abschalten, ungültige Sensordaten und abgelehnte Autotune-Starts als Unit-/Hardware-Abstraktionstest prüfen. | Jeder Sicherheitsfehler setzt Ausgang unverzüglich auf 0, bricht Autotune ab und lässt sich nicht automatisch zurücksetzen. |
| 2. Kaltstart ohne Netzlast | SSR-Ausgang mit Messgerät/Logikanal prüfen; Sensor abziehen, neu anstecken und unplausible Werte simulieren. | GPIO15 bleibt beim Boot LOW; ohne erste valide Messung kein HIGH; spätestens 750 ms nach letzter gültiger Messung LOW. |
| 3. Niedrige Heizversuche | Unter Aufsicht bei kleinen Sollwerten fahren; Temperatur an Sensor und unabhängiger Referenz im Subsekundenraster erfassen. | Keine nicht erklärbare Differenz zwischen Referenz und Steuerfühler; Abschaltpfad funktioniert wiederholt. |
| 4. PWM-Bewertung | Besonders Stellgrößen 5 %, 10 % und 20 % bei 1-s-Fenster untersuchen. | Keine unzulässigen temperaturgetriebenen Wellen oder Sprünge. Andernfalls Fenster/SSR-Konzept ändern und die Stufe wiederholen. |
| 5. Regelversuch | Mehrere Sollwerte bis höchstens 190 °C, Aufheizen und Abkühlen mit gespeicherten Rohdaten testen. | Kein Überschwingen in Richtung 215 °C; ausreichend großer nachgewiesener Abstand zur Hardwareabschaltung, auch nach „Heizung aus“. |
| 6. Autotune | Erst nach Stufen 1–5, mit unabhängiger Überwachung und den limitierten Parametern. | Basis- und Sprunglast entsprechen Vorgabe, Zeit-/Temperaturgrenzen beenden den Test sicher; Ergebnisse werden vor Übernahme fachlich bewertet. |

Für Stufen 3 bis 6 sind mindestens Zeit, Sensorwert, Referenztemperatur, Sollwert, tatsächlicher
SSR-Zustand, Stellgröße, Fehlerstatus und Umgebungstemperatur zu speichern. Die gegenwärtige
5-s-Historie genügt dafür nicht; die hochaufgelösten Testdaten sollten außerhalb des knappen
ESP-Speichers erfasst werden.

## 6. Priorisierte Umsetzungsreihenfolge

1. Hardware nicht mit Netzlast betreiben, bis unabhängiger Thermoschutz, Erdung, Sicherung,
   Gehäuse und SSR-Kühlung geprüft sind.
2. Grenzen, Sensor-Timeout und conservative PID-Startwerte in Konfiguration und allen
   Eingabepfaden ändern.
3. Verriegelten Sicherheitsautomaten inklusive sofortigem SSR-LOW, Sensorvalidierung und
   Startfreigabe implementieren und testen.
4. Autotune absichern, Ergebnisse nur als Vorschlag behandeln und UI/Web-Fehlerrückmeldungen
   ergänzen.
5. README, Schaltplan und Weboberfläche auf die tatsächlich implementierten Grenzen
   synchronisieren.
6. Die Prüfreihenfolge vollständig durchführen. Erst nach dokumentiert bestandenen Messungen
   den normalen Betrieb bis 190 °C freigeben.
