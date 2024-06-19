#ifndef DEBUG_MANAGER_H
#define DEBUG_MANAGER_H

#include <Arduino.h>

// ------------------------ Types ------------------------

// ----------------------------------------------------------------------------------------------------
// ------------------------------------- SPIFFS Clock Component ---------------------------------------
// ----------------------------------------------------------------------------------------------------

class DebugManager
{
  public:
    void setUp(bool newDebug);
    void debugMsg(String message);
    bool getDebug();
  private:
    bool _debug = false;
};

// ----------------- Exported Variables ------------------

// SPIFFS component
static DebugManager debugManager;

#endif
