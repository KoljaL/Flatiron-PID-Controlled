#include "controller.h"
#include "config.h"
#include <Arduino.h>
#include "max6675_reader.h"
#include <PID_v1.h>
#include <math.h>

namespace
{
  double input = NAN;
  double pidInput = NAN;
  double out = 0.0;
  double sp = 10.0;
  double Kp = 1.8, Ki = 0.005, Kd = 55.0;
  PID pid(&pidInput, &out, &sp, Kp, Ki, Kd, DIRECT);

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
  uint16_t lastSensorRaw = 0;
  bool sensorHasSample = false;
  bool sensorHasValidReading = false;
  bool sensorValid = false;
  bool heaterEnabled = false;
  bool manualMode = false;
  double manualOutput = 10.0;
  bool tuning = false;
  Controller::FaultReason latchedFault = Controller::FaultReason::None;
  const char *tuneStatus = "Bereit";
  bool tuningSuggestionAvailable = false;
  double tuneKp = 0.0;
  double tuneKi = 0.0;
  double tuneKd = 0.0;
  bool tuneHeating = false;
  bool tuneCycleActive = false;
  unsigned long tuneCycleStarted = 0;
  double tuneCycleMin = NAN;
  double tuneCycleMax = NAN;
  uint8_t tuneCycleCount = 0;
  double tuneAmplitudeSum = 0.0;
  double tunePeriodSumMs = 0.0;
  bool thermalResponseWatchActive = false;
  unsigned long thermalResponseWatchStarted = 0;
  double thermalResponseWatchStartTemp = NAN;

  float history[HISTORY_SIZE];
  uint16_t spHistory[HISTORY_SIZE]; // Sollwert * 10
  uint8_t outHistory[HISTORY_SIZE]; // Ausgang in Prozent
  int historyIndex = 0;
  uint32_t historyTotal = 0;

  const char *reasonText(Controller::FaultReason reason)
  {
    switch (reason)
    {
    case Controller::FaultReason::SensorValue: return "Sensorwert ungueltig";
    case Controller::FaultReason::SensorPlausibility: return "Sensor unplausibel";
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

    digitalWrite(SSR_PIN, LOW);
    latchedFault = reason;
    heaterEnabled = false;
    pid.SetMode(MANUAL);
    const unsigned long sensorAge = sensorHasValidReading ? now - lastValidReading : now - bootStarted;
    Serial.printf("FAULT,%lu,%s,age_ms=%lu,loop_gap_ms=%lu,sample_age_ms=%lu,raw_c=%.2f,raw_word=0x%04X\n",
                  now, reasonText(reason), sensorAge, lastUpdateGap,
                  now - lastSample, lastSensorReading, lastSensorRaw);
    if (tuning)
      abortAutotune("Abbruch: Sicherheitsfehler", now);
    else
      resetOutputWindow(now);
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
    const double averageAmplitude = tuneAmplitudeSum / tuneCycleCount;
    const double averagePeriodSeconds = tunePeriodSumMs / tuneCycleCount / 1000.0;
    const double relayAmplitudeMs = WINDOW_SIZE * AUTOTUNE_RELAY_OUTPUT_PERCENT / 200.0;
    const double ultimateGain = 4.0 * relayAmplitudeMs / (PI * averageAmplitude);
    const double candidateKp = 0.2 * ultimateGain;
    const double candidateTi = 0.5 * averagePeriodSeconds;
    const double candidateTd = 0.33 * averagePeriodSeconds;
    const double candidateKi = candidateKp / candidateTi;
    const double candidateKd = candidateKp * candidateTd;
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

  void updateAutotuneOnSample(double reading, unsigned long now)
  {
    if (tuneCycleActive)
    {
      tuneCycleMin = fmin(tuneCycleMin, reading);
      tuneCycleMax = fmax(tuneCycleMax, reading);
    }

    const double lowerThreshold = sp - AUTOTUNE_HYSTERESIS_C;
    const double upperThreshold = sp + AUTOTUNE_HYSTERESIS_C;
    if (tuneHeating && reading >= upperThreshold)
    {
      tuneHeating = false;
      out = 0.0;
    }
    else if (!tuneHeating && reading <= lowerThreshold)
    {
      if (tuneCycleActive)
      {
        const unsigned long period = now - tuneCycleStarted;
        const double amplitude = (tuneCycleMax - tuneCycleMin) * 0.5;
        if (period >= SENSOR_INTERVAL * 2 && amplitude >= 0.25)
        {
          tuneCycleCount++;
          tuneAmplitudeSum += amplitude;
          tunePeriodSumMs += period;
          Serial.printf("TUNE,%lu,Zyklus,%u,%.2f,%.2f,%lu\n",
                        now, tuneCycleCount, amplitude, reading, period);
          if (tuneCycleCount >= AUTOTUNE_REQUIRED_CYCLES)
          {
            finishAutotune(now);
            return;
          }
        }
      }

      tuneCycleActive = true;
      tuneCycleStarted = now;
      tuneCycleMin = reading;
      tuneCycleMax = reading;
      tuneHeating = true;
      out = WINDOW_SIZE * AUTOTUNE_RELAY_OUTPUT_PERCENT / 100.0;
    }
  }

  void checkThermalResponse(unsigned long now)
  {
    const bool heatingExpected =
        ((heaterEnabled && !manualMode) || tuning) &&
        out * 100.0 / WINDOW_SIZE >= THERMAL_RESPONSE_MIN_OUTPUT_PERCENT &&
        sp - input >= THERMAL_RESPONSE_MIN_ERROR_C;
    if (!heatingExpected)
    {
      thermalResponseWatchActive = false;
      return;
    }

    if (!thermalResponseWatchActive)
    {
      thermalResponseWatchActive = true;
      thermalResponseWatchStarted = now;
      thermalResponseWatchStartTemp = input;
      return;
    }

    if (input - thermalResponseWatchStartTemp >= THERMAL_RESPONSE_MIN_RISE_C)
    {
      thermalResponseWatchStarted = now;
      thermalResponseWatchStartTemp = input;
      return;
    }

    if (now - thermalResponseWatchStarted >= THERMAL_RESPONSE_TIMEOUT)
      latchFault(Controller::FaultReason::SensorPlausibility, now);
  }

  void logDiagnostic(unsigned long now)
  {
    if (now - lastDiagnosticLog < DIAGNOSTIC_INTERVAL)
      return;
    lastDiagnosticLog = now;
    const long readingAge = sensorHasValidReading ? (long)(now - lastValidReading) : -1L;
    Serial.printf("D,%lu,%.2f,%.1f,%.1f,%u,%u,%u,%s,%s,%.2f,0x%04X,%u,%ld,%lu,%lu\n",
                  now, input, sp, out * 100.0 / WINDOW_SIZE,
                  digitalRead(SSR_PIN) == HIGH ? 1 : 0,
                  heaterEnabled ? 1 : 0, tuning ? 1 : 0, reasonText(latchedFault),
                  sensorStateTextAt(now), lastSensorReading, lastSensorRaw,
                  sensorValid ? 1 : 0, readingAge, now - lastSample, lastUpdateGap);
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
    Max6675Reader::begin();
    Serial.println("D,millis,temp_c,setpoint_c,output_pct,ssr,enabled,tuning,fault,sensor_state,raw_c,raw_word,sample_valid,reading_age_ms,sample_age_ms,loop_gap_ms");
    pid.SetOutputLimits(0, WINDOW_SIZE);
    pid.SetSampleTime(SENSOR_INTERVAL);
    pid.SetMode(MANUAL);
  }

  bool startAutotune()
  {
    const unsigned long now = millis();
    if (latchedFault != FaultReason::None)
      tuneStatus = "Abgelehnt: Fehler verriegelt";
    else if (tuning)
      tuneStatus = "Abgelehnt: Autotune laeuft bereits";
    else if (manualMode)
      tuneStatus = "Abgelehnt: Autotune nur im Auto-Modus";
    else if (!sensorValid || now - lastValidReading >= SENSOR_TIMEOUT)
      tuneStatus = "Abgelehnt: keine aktuelle Messung";
    else if (input > AUTOTUNE_MAX_START_TEMP)
      tuneStatus = "Abgelehnt: Sohle nicht kalt genug";
    else if (sp < AUTOTUNE_SETPOINT_MIN || sp > AUTOTUNE_SETPOINT_MAX)
      tuneStatus = "Abgelehnt: Sollwert ausserhalb Testbereich";
    else
    {
      heaterEnabled = false;
      tuningSuggestionAvailable = false;
      tuneKp = tuneKi = tuneKd = 0.0;
      tuneHeating = false;
      tuneCycleActive = false;
      tuneCycleCount = 0;
      tuneAmplitudeSum = 0.0;
      tunePeriodSumMs = 0.0;
      out = 0.0;
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
      pid.SetMode(manualMode ? MANUAL : AUTOMATIC);
    }
    else
    {
      pid.SetMode(MANUAL);
      resetOutputWindow(now);
    }
    return true;
  }

  bool setManualMode(bool enabled)
  {
    if (tuning || latchedFault != FaultReason::None)
      return false;
    if (manualMode == enabled)
      return true;

    manualMode = enabled;
    heaterEnabled = false;
    pid.SetMode(MANUAL);
    resetOutputWindow(millis());
    return true;
  }

  bool setManualOutputPercent(double percent)
  {
    if (!isfinite(percent) || percent < 0.0 || percent > 100.0 ||
        !manualMode || tuning || latchedFault != FaultReason::None)
      return false;

    manualOutput = percent;
    return true;
  }

  bool adjustSetpoint(double delta)
  {
    if (!isfinite(delta) || tuning || latchedFault != FaultReason::None)
      return false;
    double requested = sp + delta;
    if (requested > SETPOINT_ABSOLUTE_MAX)
      requested = SETPOINT_ABSOLUTE_MAX;
    sp = constrain(requested, SETPOINT_MIN, SETPOINT_NORMAL_MAX);
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
      pid.SetMode(manualMode ? MANUAL : AUTOMATIC);
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

    bool newSensorReading = false;
    if (now - lastSample >= SENSOR_INTERVAL)
    {
      const Max6675Reader::Reading reading = Max6675Reader::read();
      lastSample = now;
      lastSensorReading = reading.celsius;
      lastSensorRaw = reading.raw;
      sensorHasSample = true;
      if (!reading.valid || !isfinite(reading.celsius) ||
          reading.celsius < 0.0 || reading.celsius > 1024.0)
      {
        sensorValid = false;
        latchFault(FaultReason::SensorValue, now);
      }
      else
      {
        const bool hadValidReading = sensorHasValidReading;
        const double previousInput = input;
        const unsigned long elapsed = now - lastValidReading;
        if (hadValidReading && elapsed > 0 &&
            fabs(reading.celsius - previousInput) * 1000.0 / elapsed >
                SENSOR_MAX_RISE_RATE_C_PER_S)
        {
          sensorValid = false;
          latchFault(FaultReason::SensorPlausibility, now);
        }
        else
        {
          input = reading.celsius;
          if (!hadValidReading)
            pidInput = reading.celsius;
          else
          {
            const double alpha = static_cast<double>(elapsed) /
                                 (PID_D_FILTER_TAU_MS + elapsed);
            pidInput += alpha * (reading.celsius - pidInput);
          }
          sensorValid = true;
          sensorHasValidReading = true;
          lastValidReading = now;
          newSensorReading = true;
        }
        if (sensorValid && reading.celsius >= MAX_TEMP)
          latchFault(FaultReason::OverTemperature, now);
      }
    }

    if (newSensorReading && tuning && latchedFault == FaultReason::None)
      updateAutotuneOnSample(input, now);

    if (latchedFault == FaultReason::None && sensorHasValidReading)
      checkThermalResponse(now);

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
      else
      {
        out = tuneHeating ? WINDOW_SIZE * AUTOTUNE_RELAY_OUTPUT_PERCENT / 100.0 : 0.0;
      }
    }
    else if (heaterEnabled)
    {
      if (manualMode)
      {
        out = WINDOW_SIZE * manualOutput / 100.0;
      }
      else if (newSensorReading)
      {
        const double error = sp - pidInput;
        const double effectiveKi =
            error > 0.0 && error <= PID_INTEGRAL_BAND_C ? Ki : 0.0;
        pid.SetTunings(Kp, effectiveKi, Kd);
        pid.Compute();
      }
      if (input >= sp)
        out = 0.0;
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
  bool isManualMode() { return manualMode; }
  double manualOutputPercent() { return manualOutput; }
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

  int historyStartIndex() { return historyTotal < HISTORY_SIZE ? 0 : historyIndex; }
  static int slot(int i, int start) { return (start + i) % HISTORY_SIZE; }

  float historyAt(int i) { return historyAt(i, historyStartIndex()); }
  float historyAt(int i, int startIndex) { return history[slot(i, startIndex)]; }
  float historySetpointAt(int i) { return historySetpointAt(i, historyStartIndex()); }
  float historySetpointAt(int i, int startIndex) { return spHistory[slot(i, startIndex)] / 10.0f; }
  float historyOutputAt(int i) { return historyOutputAt(i, historyStartIndex()); }
  float historyOutputAt(int i, int startIndex) { return outHistory[slot(i, startIndex)] / 100.0f; }
}
