#include "SPIFFS.h"

//**********************************************************************************
//**********************************************************************************
//*                               SPIFFS functions                                 *
//**********************************************************************************
//**********************************************************************************

// ************************************************************
// Test SPIFFS
// ************************************************************
boolean SPIFFS_CLOCK::testMountSpiffs() {
  _spiffsMounted = false;
  if (SPIFFS.begin()) {
    _spiffsMounted = true;
    SPIFFS.end();
  }

  return _spiffsMounted;
}

// ************************************************************
// Return SPIFFS Mounted status
// ************************************************************
boolean SPIFFS_CLOCK::getSpiffsMounted() {
  return _spiffsMounted;
}

// ************************************************************
// Retrieve the config from the SPIFFS
// ************************************************************
boolean SPIFFS_CLOCK::getConfigFromSpiffs(spiffs_config_t* spiffs_config) {
  boolean loaded = false;
  if (SPIFFS.begin()) {
    debugMsg("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      debugMsg("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        debugMsg("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        debugMsg("\n");

        if (json.success()) {
          debugMsg("parsed json");

          spiffs_config->ntpPool = json["ntp_pool"].as<String>();
          debugMsg("Loaded NTP pool: " + spiffs_config->ntpPool);

          spiffs_config->ntpUpdateInterval = json["ntp_update_interval"].as<int>();
          debugMsg("Loaded NTP update interval: " + String(spiffs_config->ntpUpdateInterval));

          spiffs_config->tzs = json["time_zone_string"].as<String>();
          debugMsg("Loaded time zone string: " + spiffs_config->tzs);

          spiffs_config->showSeconds = json["showSeconds"].as<bool>();
          debugMsg("Loaded showSeconds: " + String(spiffs_config->showSeconds));

          spiffs_config->flashSeconds = json["flashSeconds"].as<bool>();
          debugMsg("Loaded flashSeconds: " + String(spiffs_config->flashSeconds));

          spiffs_config->dayBlanking = json["dayBlanking"];
          debugMsg("Loaded dayBlanking: " + String(spiffs_config->dayBlanking));

          spiffs_config->blankHourStart = json["blankHourStart"];
          debugMsg("Loaded blankHourStart: " + String(spiffs_config->blankHourStart));

          spiffs_config->blankHourEnd = json["blankHourEnd"];
          debugMsg("Loaded blankHourEnd: " + String(spiffs_config->blankHourEnd));

          spiffs_config->pirTimeout = json["pirTimeout"];
          debugMsg("Loaded pirTimeout: " + String(spiffs_config->pirTimeout));

          spiffs_config->usePIRPullup = json["usePIRPullup"];
          debugMsg("Loaded usePIRPullup: " + String(spiffs_config->usePIRPullup));

          spiffs_config->testMode = json["testMode"].as<bool>();
          debugMsg("Loaded testMode: " + String(spiffs_config->testMode));

          spiffs_config->webAuthentication = json["webAuthentication"].as<bool>();
          debugMsg("Loaded webAuthentication: " + String(spiffs_config->webAuthentication));

          spiffs_config->webUsername = json["webUsername"].as<String>();
          debugMsg("Loaded webUsername: " + spiffs_config->webUsername);

          spiffs_config->webPassword = json["webPassword"].as<String>();
          debugMsg("Loaded webPassword: " + spiffs_config->webPassword);

          spiffs_config->displayRotate = json["displayRotate"];
          debugMsg("Loaded displayRotate: " + String(spiffs_config->displayRotate));

          spiffs_config->spinUpSpeed = json["spinUpSpeed"];
          debugMsg("Loaded spinUpSpeed: " + String(spiffs_config->spinUpSpeed));

          loaded = true;
        } else {
          debugMsg("failed to load json config");
        }
        debugMsg("Closing config file");
        configFile.close();
      }
    }
  } else {
    debugMsg("failed to mount FS");
  }

  SPIFFS.end();
  return loaded;
}

// ************************************************************
// Save config back to the SPIFFS
// ************************************************************
void SPIFFS_CLOCK::saveConfigToSpiffs(spiffs_config_t* spiffs_config) {
  if (SPIFFS.begin()) {
    debugMsg("mounted file system");
    debugMsg("saving config");

    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["ntp_pool"] = spiffs_config->ntpPool;
    json["ntp_update_interval"] = spiffs_config->ntpUpdateInterval;
    json["time_zone_string"] = spiffs_config->tzs;
    json["showSeconds"] = spiffs_config->showSeconds;
    json["flashSeconds"] = spiffs_config->flashSeconds;
    json["dayBlanking"] = spiffs_config->dayBlanking;
    json["blankHourStart"] = spiffs_config->blankHourStart;
    json["blankHourEnd"] = spiffs_config->blankHourEnd;
    json["pirTimeout"] = spiffs_config->pirTimeout;
    json["usePIRPullup"] = spiffs_config->usePIRPullup;
    json["testMode"] = spiffs_config->testMode;
    json["webAuthentication"] = spiffs_config->webAuthentication;
    json["webUsername"] = spiffs_config->webUsername;
    json["webPassword"] = spiffs_config->webPassword;
    json["displayRotate"] = spiffs_config->displayRotate;
    json["spinUpSpeed"] = spiffs_config->spinUpSpeed;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      debugMsg("failed to open config file for writing");
      configFile.close();
      return;
    }

    json.printTo(Serial);
    debugMsg("\n");

    json.printTo(configFile);
    configFile.close();
    debugMsg("Saved config");
    //end save
  } else {
    debugMsg("failed to mount FS");
  }
  SPIFFS.end();
}

// ************************************************************
// Get the statistics from the SPIFFS
// ************************************************************
boolean SPIFFS_CLOCK::getStatsFromSpiffs(spiffs_stats_t* spiffs_stats) {
  boolean loaded = false;
  if (SPIFFS.begin()) {
    debugMsg("mounted file system");
    if (SPIFFS.exists("/stats.json")) {
      //file exists, reading and loading
      debugMsg("reading stats file");
      File statsFile = SPIFFS.open("/stats.json", "r");
      if (statsFile) {
        debugMsg("opened stats file");
        size_t size = statsFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        statsFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        debugMsg("\n");

        if (json.success()) {
          debugMsg("parsed stats json");

          spiffs_stats->uptimeMins = json.get<unsigned long>("uptime");
          debugMsg("Loaded uptime: " + String(spiffs_stats->uptimeMins));

          spiffs_stats->tubeOnTimeMins = json.get<unsigned long>("tubeontime");
          debugMsg("Loaded tubeontime: " + String(spiffs_stats->tubeOnTimeMins));

          loaded = true;
        } else {
          debugMsg("failed to load json config");
        }
        debugMsg("Closing stats file");
        statsFile.close();
      }
    }
  } else {
    debugMsg("failed to mount FS");
  }

  SPIFFS.end();
  return loaded;
}

// ************************************************************
// Save the statistics back to the SPIFFS
// ************************************************************
void SPIFFS_CLOCK::saveStatsToSpiffs(spiffs_stats_t* spiffs_stats) {
  if (SPIFFS.begin()) {
    debugMsg("mounted file system");
    debugMsg("saving stats");

    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json.set("uptime", spiffs_stats->uptimeMins);
    json.set("tubeontime", spiffs_stats->tubeOnTimeMins);

    File statsFile = SPIFFS.open("/stats.json", "w");
    if (!statsFile) {
      debugMsg("failed to open stats file for writing");
      statsFile.close();
      return;
    }

    json.printTo(Serial);
    debugMsg("\n");

    json.printTo(statsFile);
    statsFile.close();
    debugMsg("Saved stats");
    //end save
  } else {
    debugMsg("failed to mount FS");
  }
  SPIFFS.end();
}

// ************************************************************
// Output a logging message to the debug output, if set
// ************************************************************
void SPIFFS_CLOCK::debugMsg(String message) {
  if (_dbcb != NULL && _debug) {
    _dbcb("SPIFFS: " + message);
  }
}

// ************************************************************
// Set the callback for outputting debug messages
// ************************************************************
void SPIFFS_CLOCK::setDebugCallback(DebugCallback dbcb) {
  _dbcb = dbcb;
  debugMsg("Debugging started, callback set");
}

// ************************************************************
// set the update interval
// ************************************************************
void SPIFFS_CLOCK::setDebugOutput(bool newDebug) {
  _debug = newDebug;
}
