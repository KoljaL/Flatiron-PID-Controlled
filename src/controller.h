#pragma once
#include <Arduino.h>

namespace Controller
{
  enum class FaultReason
  {
    None,
    SensorValue,
    SensorPlausibility,
    OverTemperature
  };

  void begin();
  void update(); // regelmäßig in loop() aufrufen
  bool startAutotune();
  bool cancelAutotune();
  bool adjustSetpoint(double delta);
  bool setHeaterEnabled(bool enabled);
  void localButtonPressed();

  double temperature();
  double setpoint();
  double output(); // 0..WINDOW_SIZE
  double outputPercent();
  double kp();
  double ki();
  double kd();
  bool isTuning();
  bool isHeaterEnabled();
  bool hasValidReading();
  const char *sensorStatusText();
  bool hasFault();
  FaultReason faultReason();
  const char *faultReasonText();
  const char *autotuneStatus();
  bool hasTuningSuggestion();
  double suggestedKp();
  double suggestedKi();
  double suggestedKd();

  // Historie (älteste zuerst), i in 0..historyCount()-1
  int historyCount();
  int historyStartIndex();
  float historyAt(int i);
  float historyAt(int i, int startIndex);
  float historySetpointAt(int i);
  float historySetpointAt(int i, int startIndex);
  float historyOutputAt(int i); // 0..1
  float historyOutputAt(int i, int startIndex);
}
