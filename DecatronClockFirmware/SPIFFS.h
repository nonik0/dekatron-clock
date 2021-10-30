#ifndef SPIFFS_H
#define SPIFFS_H

#include <FS.h>
#include <ArduinoJson.h>        // https://github.com/bblanchon/ArduinoJson (bblanchon 5.13.2)

#include "ClockDefs.h"

// ------------------------ Types ------------------------

typedef void (*DebugCallback) (String);

// Used for holding the config set
typedef struct {
  String ntpPool;
  int ntpUpdateInterval;
  String tzs;
  boolean showSeconds;
  boolean flashSeconds;
  byte dayBlanking;
  byte blankHourStart;
  byte blankHourEnd;
  int pirTimeout;
  boolean usePIRPullup;
  boolean testMode;
  boolean webAuthentication;
  String webUsername;
  String webPassword;
  byte displayRotate;
  byte spinUpSpeed;
} spiffs_config_t;

typedef struct {
  unsigned long uptimeMins = 0;
  unsigned long tubeOnTimeMins = 0;
} spiffs_stats_t;

// ----------------------------------------------------------------------------------------------------
// ------------------------------------- SPIFFS Clock Component ---------------------------------------
// ----------------------------------------------------------------------------------------------------

class SPIFFS_CLOCK
{
  public:
    void setDebugOutput(bool newDebug);
    
    boolean testMountSpiffs();
    boolean getSpiffsMounted();

    boolean getConfigFromSpiffs(spiffs_config_t* spiffs_config);
    void    saveConfigToSpiffs(spiffs_config_t* spiffs_config);

    boolean getStatsFromSpiffs(spiffs_stats_t* spiffs_stats);
    void    saveStatsToSpiffs(spiffs_stats_t* spiffs_stats);

    // callbacks
    void setDebugCallback(DebugCallback dbcb);
  private:
    DebugCallback _dbcb;
    bool _debug = false;
    bool _spiffsMounted = false;

    void debugMsg(String message);                        // print a debug message to the callback
};

// ----------------- Exported Variables ------------------

// Config from SPIFFS
static spiffs_config_t current_config;

// Stats from SPIFFS
static spiffs_stats_t current_stats;

// SPIFFS component
static SPIFFS_CLOCK spiffs;

#endif
