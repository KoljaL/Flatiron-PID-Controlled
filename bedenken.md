
System-Kontext & Hardware:

* Aktuator: Eine nackte Bügeleisen-Sohle (Aluminium) ohne Gehäuse und ohne kühlendes Wasser.
* Leistung: Extrem hohe 2000 Watt bei relativ geringer thermischer Masse. Das System reagiert extrem schnell und schießt bei ungebremster Energiezufuhr in Sekunden in gefährliche Temperaturbereiche.
* Sensorik: MAX6675 Thermoelement (Lese-Intervall: 250 ms, Timeout: 2000 ms).
* Steuerung: Software-PWM (Slow-PWM) mit einer WINDOW_SIZE von 1000 ms.
* Hardware-Schutz: Eine mechanische Schmelzsicherung (Thermal Cutoff) ist fest an der Sohle verbaut und trennt bei 230.0°C permanent die Stromzufuhr.
* Abgeänderte Sicherheits-Limits (Software): Um ein versehentliches Auslösen der Hardware-Sicherung durch thermische Trägheit (Nachheizen) zu verhindern, wird SETPOINT_MAX auf 190.0°C (maximal 200.0°C) begrenzt. Das absolute Software-Not-Aus (MAX_TEMP) wird streng auf 215.0°C gesetzt.

Zielsetzung für die PID-Parametrierung:
Generiere Reglerparameter und Steuerungslogiken für die Arduino-Bibliotheken PID_v1 und PID_AutoTune_v0. Da dieses System thermisch einem "Rennwagen auf Eis" gleicht und die Hardware-Sicherung bei 230°C die Schaltung dauerhaft zerstört, steht Sicherheit, Stabilität und die absolute Vermeidung von Überschwingen (Overshoot) an oberster Stelle.
1. Vorgeschlagene, hochgradig vorsichtige Startwerte (PID_v1):

* double Kp = 1.8;
(Extrem niedriger Proportionalanteil. Selbst bei einer Abweichung von 50°C vom Sollwert schaltet das SSR das 2000W-Heizelement im PWM-Fenster nur zu unter 10% (= ~90 ms) ein. Dies verhindert ein unkontrolliertes Hineinschießen in die Zieltemperatur.)
* double Ki = 0.005;
(Ein extrem langsamer Integralanteil, um das Phänomen des "Integral Windup" – das Aufstauen des Fehlers während der Aufheizphase – komplett zu unterbinden.)
* double Kd = 55.0;
(Ein sehr dominanter Derivativanteil. Er fungiert als aggressive "Bremse" und drückt den Ausgang sofort auf null, sobald die Temperatur auch nur minimal zu schnell ansteigt.)

2. Sicherheits-Konfiguration für das PID-Autotuning (PID_AutoTune_v0):
Die Standardwerte für das Autotuning würden dieses 2000W-System sofort über die 230°C-Grenze jagen. Die Konfiguration in startAutotune() muss zwingend wie folgt limitiert werden:

* autotune.SetNoiseBand(1.5); (Ignoriert thermisches Rauschen und Signalsprünge des MAX6675 unterhalb von 1,5°C).
* out = WINDOW_SIZE * 0.10; (Der Autotune-Prozess startet auf einer sehr niedrigen Basis-Leistung von nur 10% (~200W).
* autotune.SetOutputStep(WINDOW_SIZE * 0.05); (Der Schwingungs-Leistungssprung beim Testen darf maximal 5% der Fenstergröße betragen! 5% von 2000W entsprechen bereits 100W Hub, was für dieses System völlig ausreicht).
* autotune.SetLookbackSec(10); (Da das System ohne Gehäuse extrem schnell reagiert, muss das Blickfeld kurz sein (10 Sekunden), um die Schwingungsscheitelpunkte sauber zu erfassen).

3. Kritische Software-Bedenken & Implementierungsvorgaben:

   1. Das Sensor-Timeout ist viel zu lang: Ein SENSOR_TIMEOUT = 2000 bedeutet, dass das System bei einem Sensorausfall (z. B. Kabelbruch) bis zu 2 Sekunden lang mit den alten Werten weiterheizt. Bei 2000W Leistung schießt die nackte Sohle in 2 Sekunden locker um 30°C nach oben und killt deine 230°C-Sicherung. Vorgabe: Setze das Timeout auf maximal 750 ms (3 verpasste Messungen) und zwinge den Ausgang out im Fehlerfall augenblicklich auf 0.
   2. Berücksichtigung des thermischen Nachheizens (Thermal Runaway): Wenn bei 215°C die Software-Abschaltung greift, wandert die Hitze aus dem Inneren des Heizelements noch weiter in die Aluminiumsohle. Das System wird kurzzeitig "nachheizen". Der Puffer von 15°C bis zur Schmelzsicherung (215°C zu 230°C) ist das absolute Minimum.
   3. Plauen-Zeit-Verhalten: Bei WINDOW_SIZE = 1000 und einer berechneten Stellgröße von z. B. 10% heizt das System 100 ms lang mit vollen 2000 Watt durch und bleibt dann 900 ms aus. Überprüfe im Live-Betrieb penibel, ob diese 100-ms-Schübe zu wellenartigen, sprunghaften Temperaturanstiegen führen. Falls ja, muss die Hardware auf ein schnelleres SSR umgestellt werden.
 
