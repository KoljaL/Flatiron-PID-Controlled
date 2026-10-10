#include "web_interface.h"
#include "web_page.h"
#include "config.h"
#include "controller.h"
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <stdlib.h>
#include <math.h>

namespace
{
  AsyncWebServer server(80);

}

namespace WebInterface
{
  void begin()
  {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              {
      request->send_P(200, "text/html; charset=utf-8", index_html); });

    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request)
              {
      JsonDocument doc;
      doc["temp"] = Controller::temperature();
      doc["setpoint"] = Controller::setpoint();
      doc["kp"] = Controller::kp();
      doc["ki"] = Controller::ki();
      doc["kd"] = Controller::kd();
      doc["fault"] = Controller::hasFault();
      doc["faultReason"] = Controller::faultReasonText();
      doc["output"] = Controller::outputPercent();
      doc["tuning"] = Controller::isTuning();
      doc["tuningStatus"] = Controller::autotuneStatus();
      doc["heaterEnabled"] = Controller::isHeaterEnabled();
      doc["validReading"] = Controller::hasValidReading();
      doc["sensorState"] = Controller::sensorStatusText();
      doc["tuningSuggestion"] = Controller::hasTuningSuggestion();
      JsonObject tuningConfig = doc["tuningConfig"].to<JsonObject>();
      tuningConfig["noiseBandC"] = 1.5;
      tuningConfig["outputStepMs"] = WINDOW_SIZE * 0.05;
      tuningConfig["outputMinPercent"] = 5;
      tuningConfig["outputMaxPercent"] = 15;
      tuningConfig["lookbackSeconds"] = 10;
      tuningConfig["timeoutMs"] = AUTOTUNE_TIMEOUT;
      tuningConfig["abortTemperatureC"] = AUTOTUNE_MAX_TEMP;
      tuningConfig["maxStartTemperatureC"] = AUTOTUNE_MAX_START_TEMP;
      tuningConfig["setpointMinC"] = AUTOTUNE_SETPOINT_MIN;
      tuningConfig["setpointMaxC"] = AUTOTUNE_SETPOINT_MAX;
      tuningConfig["maxKp"] = AUTOTUNE_KP_MAX;
      tuningConfig["maxKi"] = AUTOTUNE_KI_MAX;
      tuningConfig["maxKd"] = AUTOTUNE_KD_MAX;
      tuningConfig["historyIntervalMs"] = HISTORY_INTERVAL;
      doc["controlsEnabled"] = true;
      if (Controller::hasTuningSuggestion())
      {
        doc["suggestedKp"] = Controller::suggestedKp();
        doc["suggestedKi"] = Controller::suggestedKi();
        doc["suggestedKd"] = Controller::suggestedKd();
      }
      String response;
      serializeJson(doc, response);
      request->send(200, "application/json", response); });

    // Gestreamt, um den knappen Heap nicht mit einem großen JSON zu belasten
    server.on("/history", HTTP_GET, [](AsyncWebServerRequest *request)
              {
      AsyncResponseStream *res = request->beginResponseStream("application/json");
      int n = Controller::historyCount();
      res->printf("{\"interval\":%lu,\"temp\":[", HISTORY_INTERVAL / 1000);
      for (int i = 0; i < n; i++) {
        if (i) res->print(",");
        float value = Controller::historyAt(i);
        if (isfinite(value)) res->print(value, 1);
        else res->print("null");
      }
      res->print("],\"setpoint\":[");
      for (int i = 0; i < n; i++) {
        if (i) res->print(",");
        res->print(Controller::historySetpointAt(i), 1);
      }
      res->print("],\"pwm\":[");
      for (int i = 0; i < n; i++) {
        if (i) res->print(",");
        res->print(Controller::historyOutputAt(i), 0);
      }
      const char *names[3] = {"kp", "ki", "kd"};
      for (int p = 0; p < 3; p++) {
        res->printf("],\"%s\":[", names[p]);
        for (int i = 0; i < n; i++) {
          float v[3];
          Controller::historyTuningsAt(i, v[0], v[1], v[2]);
          if (i) res->print(",");
          res->print(v[p], 2);
        }
      }
      res->print("]}");
      request->send(res); });

    server.on("/start-tune", HTTP_POST, [](AsyncWebServerRequest *request)
              {
      if (!Controller::startAutotune())
      {
        request->send(409, "text/plain; charset=utf-8", Controller::autotuneStatus());
        return;
      }
      request->send(200, "text/plain; charset=utf-8", "Autotune gestartet"); });

    server.on("/cancel-tune", HTTP_POST, [](AsyncWebServerRequest *request)
              {
      if (!Controller::cancelAutotune())
      {
        request->send(409, "text/plain; charset=utf-8", "Kein Autotune aktiv");
        return;
      }
      request->send(200, "text/plain; charset=utf-8", "Autotune abgebrochen"); });

    server.on("/heater", HTTP_POST, [](AsyncWebServerRequest *request)
              {
      if (!request->hasParam("enabled"))
      {
        request->send(400, "text/plain; charset=utf-8", "Parameter enabled fehlt");
        return;
      }
      const String value = request->getParam("enabled")->value();
      if (value != "0" && value != "1")
      {
        request->send(400, "text/plain; charset=utf-8", "enabled muss 0 oder 1 sein");
        return;
      }
      if (!Controller::setHeaterEnabled(value == "1"))
      {
        request->send(409, "text/plain; charset=utf-8",
                      Controller::hasFault() ? "Heizung wegen verriegeltem Fehler gesperrt" :
                      Controller::isTuning() ? "Heizung waehrend Autotune gesperrt" :
                                               "Keine aktuelle Sensor-Messung");
        return;
      }
      request->send(200, "text/plain; charset=utf-8", value == "1" ? "Heizung eingeschaltet" : "Heizung ausgeschaltet"); });

    server.on("/setpoint", HTTP_POST, [](AsyncWebServerRequest *request)
              {
      if (!request->hasParam("value"))
      {
        request->send(400, "text/plain", "Missing value");
        return;
      }
      const String rawValue = request->getParam("value")->value();
      char *end = nullptr;
      const double value = strtod(rawValue.c_str(), &end);
      if (end == rawValue.c_str() || *end != '\0' || !isfinite(value) ||
          value < SETPOINT_MIN || value > SETPOINT_ABSOLUTE_MAX)
      {
        request->send(400, "text/plain; charset=utf-8", "Ungueltiger Sollwert (zulassig: 0 bis 190 C)");
        return;
      }
      if (value > SETPOINT_NORMAL_MAX)
      {
        request->send(400, "text/plain; charset=utf-8", "Bediengrenze: maximal 190 C");
        return;
      }
      if (!Controller::adjustSetpoint(value - Controller::setpoint()))
      {
        request->send(409, "text/plain; charset=utf-8",
                      Controller::isTuning() ? "Sollwert waehrend Autotune gesperrt" :
                                               "Sollwert bei verriegeltem Fehler gesperrt");
        return;
      }
      request->send(200, "text/plain", "OK"); });

    server.begin();
  }
}
