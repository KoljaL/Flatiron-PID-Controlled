#include "web_interface.h"
#include "web_page.h"
#include "config.h"
#include "controller.h"
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <memory>

namespace
{
  AsyncWebServer server(80);

  class HistoryStreamState
  {
  public:
    HistoryStreamState(int count, int startIndex)
        : count(count), startIndex(startIndex) {}

    size_t write(uint8_t *buffer, size_t capacity)
    {
      size_t written = 0;
      while (written < capacity)
      {
        if (tokenOffset == tokenLength && !nextToken())
          break;
        const size_t remaining = tokenLength - tokenOffset;
        const size_t copied = min(remaining, capacity - written);
        memcpy(buffer + written, token + tokenOffset, copied);
        tokenOffset += copied;
        written += copied;
      }
      return written;
    }

  private:
    enum class Phase
    {
      Header,
      Temperatures,
      SetpointHeader,
      Setpoints,
      PwmHeader,
      PwmValues,
      End,
      Done
    };

    int count;
    int startIndex;
    int item = 0;
    Phase phase = Phase::Header;
    char token[32] = {};
    size_t tokenLength = 0;
    size_t tokenOffset = 0;

    bool setToken(const char *value)
    {
      const size_t length = strlen(value);
      if (length >= sizeof(token))
        return false;
      memcpy(token, value, length + 1);
      tokenLength = length;
      tokenOffset = 0;
      return true;
    }

    bool nextToken()
    {
      while (true)
      {
        switch (phase)
        {
        case Phase::Header:
          phase = Phase::Temperatures;
          snprintf(token, sizeof(token), "{\"interval\":%lu,\"temp\":[",
                   HISTORY_INTERVAL / 1000);
          tokenLength = strlen(token);
          tokenOffset = 0;
          return true;
        case Phase::Temperatures:
          if (item < count)
          {
            const float value = Controller::historyAt(item, startIndex);
            const bool first = item++ == 0;
            if (!isfinite(value))
              return setToken(first ? "null" : ",null");
            char number[24];
            snprintf(number, sizeof(number), "%.1f", value);
            snprintf(token, sizeof(token), "%s%s", first ? "" : ",", number);
            tokenLength = strlen(token);
            tokenOffset = 0;
            return true;
          }
          phase = Phase::SetpointHeader;
          break;
        case Phase::SetpointHeader:
          phase = Phase::Setpoints;
          item = 0;
          return setToken("],\"setpoint\":[");
        case Phase::Setpoints:
          if (item < count)
          {
            const float value = Controller::historySetpointAt(item, startIndex);
            const bool first = item++ == 0;
            snprintf(token, sizeof(token), "%s%.1f", first ? "" : ",", value);
            tokenLength = strlen(token);
            tokenOffset = 0;
            return true;
          }
          phase = Phase::PwmHeader;
          break;
        case Phase::PwmHeader:
          phase = Phase::PwmValues;
          item = 0;
          return setToken("],\"pwm\":[");
        case Phase::PwmValues:
          if (item < count)
          {
            const float value = Controller::historyOutputAt(item, startIndex);
            const bool first = item++ == 0;
            snprintf(token, sizeof(token), "%s%.2f", first ? "" : ",", value);
            tokenLength = strlen(token);
            tokenOffset = 0;
            return true;
          }
          phase = Phase::End;
          break;
        case Phase::End:
          phase = Phase::Done;
          return setToken("]}");
        case Phase::Done:
          return false;
        }
      }
    }
  };

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
      tuningConfig["relayOutputPercent"] = AUTOTUNE_RELAY_OUTPUT_PERCENT;
      tuningConfig["hysteresisC"] = AUTOTUNE_HYSTERESIS_C;
      tuningConfig["requiredCycles"] = AUTOTUNE_REQUIRED_CYCLES;
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

    server.on("/history", HTTP_GET, [](AsyncWebServerRequest *request)
              {
      const int count = Controller::historyCount();
      const std::shared_ptr<HistoryStreamState> stream =
          std::make_shared<HistoryStreamState>(count, Controller::historyStartIndex());
      request->send(request->beginChunkedResponse(
          "application/json",
          [stream](uint8_t *buffer, size_t maxLen, size_t) -> size_t {
            return stream->write(buffer, maxLen);
          })); });

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
