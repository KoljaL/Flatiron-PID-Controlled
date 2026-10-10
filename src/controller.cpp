#include "controller.h"
#include "config.h"
#include <Arduino.h>
#include <max6675.h>
#include <PID_v1.h>
#include <PID_AutoTune_v0.h>
#include <math.h>

namespace
{
  MAX6675 &thermocouple()
  {
    static MAX6675 instance(MAX_SCK, MAX_CS, MAX_SO);
    return instance;
  }

  double input = NAN;
  double out = 0.0;
  double sp = 10.0;
  double Kp = 1.8, Ki = 0.005, Kd = 55.0;
  PID pid(&input, &out, &sp, Kp, Ki, Kd, DIRECT);
  PID_ATune autotune(&input, &out);

  unsigned long windowStart = 0;
  unsigned long lastSample = 0;
  unsigned long lastUpdate = 0;
  unsigned long lastUpdateGap = 0;
  unsigned long bootStarted = 0;
  unsigned long lastValidReading = 0;
  unsigned long lastHistoryLog = 0;
  unsigned long autotuneStarted = 0;
  unsigned long lastDiagnosticLog = 0;
  double lastSensorReading = NAN;
  bool sensorHasSample = false;
  bool sensorHasValidReading = false;
  bool sensorValid = false;
  bool heaterEnabled = false;
  bool tuning = false;
  Controller::FaultReason latchedFault = Controller::FaultReason::None;
  const char *tuneStatus = "Bereit";
  bool tuningSuggestionAvailable = false;
  double tuneKp = 0.0;
  double tuneKi = 0.0;
  double tuneKd = 0.0;

  float history[HISTORY_SIZE];
  uint16_t spHistory[HISTORY_SIZE]; // Sollwert * 10
  uint8_t outHistory[HISTORY_SIZE]; // Ausgang in Prozent
  int historyIndex = 0;
  uint32_t historyTotal = 0;

  constexpr int TUNING_LOG_SIZE = 8;
  struct TuningEntry { uint32_t sample; float kp, ki, kd; };
  TuningEntry tuningLog[TUNING_LOG_SIZE];
  int tuningLogCount = 0;

  void logTunings()
  {
    if (tuningLogCount == TUNING_LOG_SIZE)
    {
      for (int i = 1; i < TUNING_LOG_SIZE; i++)
        tuningLog[i - 1] = tuningLog[i];
      tuningLogCount--;
    }
    tuningLog[tuningLogCount++] = {historyTotal, (float)Kp, (float)Ki, (float)Kd};
  }

  const char *reasonText(Controller::FaultReason reason)
  {
    switch (reason)
    {
    case Controller::FaultReason::SensorStartupTimeout: return "Kein Startwert";
    case Controller::FaultReason::SensorStaleTimeout: return "Messwert veraltet";
    case Controller::FaultReason::SensorValue: return "Sensorwert ungueltig";
    case Controller::FaultReason::OverTemperature: return "Uebertemperatur";
    default: return "Keine";
    }
  }

  const char *sensorStateTextAt(unsigned long now)
  {
    if (!sensorHasSample)
      return "PRUEFE";
    if (!sensorValid)
      return "UNGUELTIG";
    if (now - lastValidReading >= SENSOR_TIMEOUT)
      return "VERALTET";
    return "OK";
  }

  void resetOutputWindow(unsigned long now)
  {
    out = 0.0;
    windowStart = now;
    digitalWrite(SSR_PIN, LOW);
  }

  void abortAutotune(const char *status, unsigned long now)
  {
    if (tuning)
      autotune.Cancel();
    tuning = false;
    heaterEnabled = false;
    tuneStatus = status;
    pid.SetMode(MANUAL);
    resetOutputWindow(now);
    Serial.printf("TUNE,%lu,%s,%.2f,%.1f,%.3f,%.4f,%.2f\n",
                  now, status, input, sp, tuneKp, tuneKi, tuneKd);
  }

  void latchFault(Controller::FaultReason reason, unsigned long now)
  {
    if (latchedFault != Controller::FaultReason::None)
      return;

    latchedFault = reason;
    heaterEnabled = false;
    pid.SetMode(MANUAL);
    const unsigned long sensorAge = sensorHasValidReading ? now - lastValidReading : now - bootStarted;
    Serial.printf("FAULT,%lu,%s,age_ms=%lu,loop_gap_ms=%lu,sample_age_ms=%lu,raw_c=%.2f\n",
                  now, reasonText(reason), sensorAge, lastUpdateGap,
                  now - lastSample, lastSensorReading);
    if (tuning)
      abortAutotune("Abbruch: Sicherheitsfehler", now);
    else
      resetOutputWindow(now);
    digitalWrite(SSR_PIN, LOW);
  }

  bool validSuggestion(double candidateKp, double candidateKi, double candidateKd)
  {
    return isfinite(candidateKp) && isfinite(candidateKi) && isfinite(candidateKd) &&
           candidateKp > 0.0 && candidateKp <= AUTOTUNE_KP_MAX &&
           candidateKi > 0.0 && candidateKi <= AUTOTUNE_KI_MAX &&
           candidateKd > 0.0 && candidateKd <= AUTOTUNE_KD_MAX;
  }

  void finishAutotune(unsigned long now)
  {
    const double candidateKp = autotune.GetKp();
    const double candidateKi = autotune.GetKi();
    const double candidateKd = autotune.GetKd();
    tuningSuggestionAvailable = validSuggestion(candidateKp, candidateKi, candidateKd);
    if (tuningSuggestionAvailable)
    {
      tuneKp = candidateKp;
      tuneKi = candidateKi;
      tuneKd = candidateKd;
      tuneStatus = "Abgeschlossen - Vorschlag";
    }
    else
    {
      tuneKp = tuneKi = tuneKd = 0.0;
      tuneStatus = "Fehler: Ergebnis ausserhalb sicherer Grenzen";
    }
    abortAutotune(tuneStatus, now);
  }

  void logDiagnostic(unsigned long now)
  {
    if (now - lastDiagnosticLog < DIAGNOSTIC_INTERVAL)
      return;
    lastDiagnosticLog = now;
    const long readingAge = sensorHasValidReading ? (long)(now - lastValidReading) : -1L;
    Serial.printf("D,%lu,%.2f,%.1f,%.1f,%u,%u,%u,%s,%s,%.2f,%u,%ld,%lu,%lu\n",
                  now, input, sp, out * 100.0 / WINDOW_SIZE,
                  digitalRead(SSR_PIN) == HIGH ? 1 : 0,
                  heaterEnabled ? 1 : 0, tuning ? 1 : 0, reasonText(latchedFault),
                  sensorStateTextAt(now), lastSensorReading, sensorValid ? 1 : 0,
                  readingAge, now - lastSample, lastUpdateGap);
  }
}

namespace Controller
{
  void begin()
  {
    pinMode(SSR_PIN, OUTPUT);
    digitalWrite(SSR_PIN, LOW);
    bootStarted = millis();
    windowStart = bootStarted;
    lastSample = bootStarted;
    lastUpdate = bootStarted;
    thermocouple();
    Serial.println("D,millis,temp_c,setpoint_c,output_pct,ssr,enabled,tuning,fault,sensor_state,raw_c,sample_valid,reading_age_ms,sample_age_ms,loop_gap_ms");
    pid.SetOutputLimits(0, WINDOW_SIZE);
    pid.SetMode(MANUAL);
    logTunings();
  }

  bool startAutotune()
  {
    const unsigned long now = millis();
    if (latchedFault != FaultReason::None)
      tuneStatus = "Abgelehnt: Fehler verriegelt";
    else if (tuning)
      tuneStatus = "Abgelehnt: Autotune laeuft bereits";
    else if (!sensorValid || now - lastValidReading >= SENSOR_TIMEOUT)
      tuneStatus = "Abgelehnt: keine aktuelle Messung";
    else if (input > AUTOTUNE_MAX_START_TEMP)
      tuneStatus = "Abgelehnt: Sohle nicht kalt genug";
    else if (sp < AUTOTUNE_SETPOINT_MIN || sp > AUTOTUNE_SETPOINT_MAX)
      tuneStatus = "Abgelehnt: Sollwert ausserhalb Testbereich";
    else
    {
      tuningSuggestionAvailable = false;
      tuneKp = tuneKi = tuneKd = 0.0;
      out = WINDOW_SIZE * 0.10;
      autotune.SetNoiseBand(1.5);
      autotune.SetOutputStep(WINDOW_SIZE * 0.05);
      autotune.SetLookbackSec(10);
      autotune.SetControlType(1);
      pid.SetMode(MANUAL);
      windowStart = now;
      autotuneStarted = now;
      tuning = true;
      tuneStatus = "Laeuft";
      Serial.printf("TUNE,%lu,Start,%.2f,%.1f,0,0,0\n", now, input, sp);
      return true;
    }
    Serial.printf("TUNE,%lu,%s,%.2f,%.1f,0,0,0\n", now, tuneStatus, input, sp);
    return false;
  }

  bool cancelAutotune()
  {
    if (!tuning)
      return false;
    abortAutotune("Abgebrochen: Weboberflaeche", millis());
    return true;
  }

  bool setHeaterEnabled(bool enabled)
  {
    const unsigned long now = millis();
    if (tuning || latchedFault != FaultReason::None)
      return false;
    if (enabled && (!sensorValid || now - lastValidReading >= SENSOR_TIMEOUT))
      return false;
    if (heaterEnabled == enabled)
      return true;

    heaterEnabled = enabled;
    if (enabled)
    {
      out = 0.0;
      windowStart = now;
      pid.SetMode(AUTOMATIC);
    }
    else
    {
      pid.SetMode(MANUAL);
      resetOutputWindow(now);
    }
    return true;
  }

  bool adjustSetpoint(double delta)
  {
    if (!isfinite(delta) || tuning || latchedFault != FaultReason::None)
      return false;
    const double previousSetpoint = sp;
    double requested = sp + delta;
    if (requested > SETPOINT_ABSOLUTE_MAX)
      requested = SETPOINT_ABSOLUTE_MAX;
    sp = constrain(requested, SETPOINT_MIN, SETPOINT_NORMAL_MAX);
    if (heaterEnabled && sp < input && sp < previousSetpoint)
    {
      pid.SetMode(MANUAL);
      resetOutputWindow(millis());
      pid.SetMode(AUTOMATIC);
    }
    return true;
  }

  void localButtonPressed()
  {
    const unsigned long now = millis();
    if (latchedFault != FaultReason::None)
    {
      if (sensorValid && now - lastValidReading < SENSOR_TIMEOUT &&
          input < FAULT_RESET_MAX_TEMP)
      {
        latchedFault = FaultReason::None;
        heaterEnabled = false;
        pid.SetMode(MANUAL);
        resetOutputWindow(now);
      }
      return;
    }

    if (tuning)
    {
      abortAutotune("Abgebrochen: lokaler Taster", now);
      return;
    }
    if (heaterEnabled)
    {
      heaterEnabled = false;
      pid.SetMode(MANUAL);
      resetOutputWindow(now);
      return;
    }
    if (sensorValid && now - lastValidReading < SENSOR_TIMEOUT)
    {
      out = 0.0;
      windowStart = now;
      pid.SetMode(AUTOMATIC);
      heaterEnabled = true;
    }
  }

  void update()
  {
    const unsigned long now = millis();
    lastUpdateGap = now - lastUpdate;
    lastUpdate = now;

    const bool loopStalled = lastUpdateGap >= LOOP_STALL_THRESHOLD;
    if (loopStalled)
    {
      resetOutputWindow(now);
      lastSample = now - SENSOR_INTERVAL;
      Serial.printf("STALL,%lu,loop_gap_ms=%lu\n", now, lastUpdateGap);
    }

    if (now - lastSample >= SENSOR_INTERVAL)
    {
      const double reading = thermocouple().readCelsius();
      lastSample = now;
      lastSensorReading = reading;
      sensorHasSample = true;
      if (!isfinite(reading) || reading < 0.0 || reading > 1024.0)
      {
        sensorValid = false;
        latchFault(FaultReason::SensorValue, now);
      }
      else
      {
        input = reading;
        sensorValid = true;
        sensorHasValidReading = true;
        lastValidReading = now;
        if (reading >= MAX_TEMP)
          latchFault(FaultReason::OverTemperature, now);
      }
    }

    if (latchedFault == FaultReason::None &&
        ((!sensorValid && now - bootStarted >= SENSOR_TIMEOUT) ||
         (sensorValid && now - lastValidReading >= SENSOR_TIMEOUT)))
      latchFault(sensorValid ? FaultReason::SensorStaleTimeout : FaultReason::SensorStartupTimeout, now);

    if (latchedFault != FaultReason::None)
    {
      digitalWrite(SSR_PIN, LOW);
      out = 0.0;
    }
    else if (tuning)
    {
      if (input >= AUTOTUNE_MAX_TEMP)
      {
        abortAutotune("Abbruch: Autotune-Temperaturgrenze", now);
      }
      else if (now - autotuneStarted >= AUTOTUNE_TIMEOUT)
      {
        abortAutotune("Abbruch: Autotune-Zeitlimit", now);
      }
      else if (autotune.Runtime() != 0)
      {
        finishAutotune(now);
      }
      else
      {
        out = constrain(out, WINDOW_SIZE * 0.05, WINDOW_SIZE * 0.15);
      }
    }
    else if (heaterEnabled)
    {
      if (input >= sp)
      {
        pid.SetMode(MANUAL);
        resetOutputWindow(now);
      }
      else
      {
        pid.SetMode(AUTOMATIC);
        pid.Compute();
      }
    }
    else
    {
      out = 0.0;
      digitalWrite(SSR_PIN, LOW);
    }

    if (latchedFault == FaultReason::None && (heaterEnabled || tuning))
    {
      if (now - windowStart >= WINDOW_SIZE)
        windowStart = now;
      digitalWrite(SSR_PIN, out > (now - windowStart) ? HIGH : LOW);
    }

    if (now - lastHistoryLog >= HISTORY_INTERVAL)
    {
      history[historyIndex] = (float)input;
      spHistory[historyIndex] = (uint16_t)(sp * 10.0);
      outHistory[historyIndex] = (uint8_t)(out * 100.0 / WINDOW_SIZE + 0.5);
      historyIndex = (historyIndex + 1) % HISTORY_SIZE;
      historyTotal++;
      lastHistoryLog = now;
    }

    logDiagnostic(now);
  }

  double temperature() { return input; }
  double setpoint() { return sp; }
  double output() { return out; }
  double outputPercent() { return constrain(out * 100.0 / WINDOW_SIZE, 0.0, 100.0); }
  double kp() { return Kp; }
  double ki() { return Ki; }
  double kd() { return Kd; }
  bool isTuning() { return tuning; }
  bool isHeaterEnabled() { return heaterEnabled; }
  bool hasValidReading()
  {
    return sensorValid && millis() - lastValidReading < SENSOR_TIMEOUT;
  }
  const char *sensorStatusText() { return sensorStateTextAt(millis()); }
  bool hasFault() { return latchedFault != FaultReason::None; }
  FaultReason faultReason() { return latchedFault; }
  const char *faultReasonText() { return reasonText(latchedFault); }
  const char *autotuneStatus() { return tuneStatus; }
  bool hasTuningSuggestion() { return tuningSuggestionAvailable; }
  double suggestedKp() { return tuneKp; }
  double suggestedKi() { return tuneKi; }
  double suggestedKd() { return tuneKd; }

  int historyCount() { return historyTotal < HISTORY_SIZE ? (int)historyTotal : HISTORY_SIZE; }

  static int slot(int i)
  {
    const int start = historyTotal < HISTORY_SIZE ? 0 : historyIndex;
    return (start + i) % HISTORY_SIZE;
  }

  float historyAt(int i) { return history[slot(i)]; }
  float historySetpointAt(int i) { return spHistory[slot(i)] / 10.0f; }
  float historyOutputAt(int i) { return outHistory[slot(i)] / 100.0f; }

  void historyTuningsAt(int i, float &kp, float &ki, float &kd)
  {
    const uint32_t sample = historyTotal - historyCount() + i;
    int k = 0;
    for (int j = 0; j < tuningLogCount; j++)
      if (tuningLog[j].sample <= sample)
        k = j;
    kp = tuningLog[k].kp;
    ki = tuningLog[k].ki;
    kd = tuningLog[k].kd;
  }
}
