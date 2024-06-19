//**********************************************************************************
//* Code for a Single Decatron Spinner based clock.                                *
//*                                                                                *
//*  nixie@protonmail.ch                                                           *
//*                                                                                *
//* Board: Lolin(Wemos) D1 R2 & Mini                                               *
//* CPU-Frequency: 160MHz                                                           *
//* Flash Size: 4MB (FS: 2MB, OTA:~1019KB)                                         *
//*                                                                                *
//**********************************************************************************
//**********************************************************************************

#include <TimeLib.h>            // http://playground.arduino.cc/code/time (Margolis 1.5.0) // https://github.com/michaelmargolis/arduino_time
#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager  (tzapu 0.14.0)

#include "ClockButton.h"
#include "ClockUtils.h"
#include "DebugManager.h"
#include "ESP_DS1307.h"
#include "HtmlServer.h"
#include "NtpAsync.h"
#include "SPIFFS.h"

#define DEBUG     false

// ------------------ Decatron Control ----------------

#define INT_MUX_COUNTS_ON   1000
#define INT_MUX_COUNTS_OFF  250000

volatile byte digitStep = 0;
volatile byte phaseStep = 0;
volatile byte indexMark = 0;

volatile uint32_t muxCount = INT_MUX_COUNTS_ON;
volatile uint32_t muxCountSecs = muxCount * 5;
volatile uint32_t muxCountMins = muxCount * 15;
volatile uint32_t muxCountHours = muxCount * 60;

int startupDelay = INT_MUX_COUNTS_ON * 150;

byte displayHours = 0;
byte displayMinsPrep = 0;   // prepared before flash status
byte displaySecsPrep = 0;   // prepared before flash status
byte displayMins = 0;
byte displaySecs = 0;
volatile int millisInSecond = 0;

// ------------------ Usage statistics -----------------

int impressionsPerSec = 0;
int lastImpressionsPerSec = 0;

// ----------------- Real time clock -------------------

byte useRTC = false;  // true if we detect an RTC
boolean onceHadAnRTC = false;

// ------------- Time management variables -------------

unsigned long nowMillis = 0;
unsigned long lastCheckMillis = 0;
unsigned long lastSecMillis = nowMillis;
unsigned long lastSecond = second();
boolean secondsChanged = false;
boolean triggeredThisSec = false;
long restartAtMillis = 0;
boolean restartResetWifi = false;

// ----------------- Motion detector -------------------

unsigned long pirLastSeen = 0;
boolean pirInstalled = false;
int pirConsecutiveCounts = 300;  // set with a high value to stop the PIR being falsely detected the first time round the loop
boolean pirStatus = false;

// --------------------- Blanking ----------------------

boolean blanked = false;
byte blankSuppressStep = 0;    // The press we are on: 1 press = suppress for 1 min, 2 press = 1 hour, 3 = 1 day
unsigned long blankSuppressedMillis = 0;   // The end time of the blanking, 0 if we are not suppressed
unsigned long blankSuppressedSelectionTimoutMillis = 0;   // Used for determining the end of the blanking period selection timeout
unsigned int spinUpVal = 0;

// --------------------- Misc ----------------------

bool debugVal = DEBUG;

// ----------------------- Components ----------------------------

ClockButton button1(inputPin1, CLOCK_BUTTON_ACTIVE_LO);

// ************************************************************
// Interrupt routine for scheduled interrupts
// Spin through each of the pins on the decatron, using a short
// interrupt time for "off" pins (not really off, but quite dim)
// and longer dwell times for the lit pins.
// ************************************************************
ICACHE_RAM_ATTR void displayUpdate() {
  uint32_t delayCount = muxCount;
  phaseStep++;

  if (phaseStep == 3) {
    phaseStep = 0;
    digitStep++;
    if (digitStep==10){
      digitStep = 0;
    }
  }

  G_step(phaseStep);

  byte pinStep = digitStep*3+phaseStep;

  if (pinStep == displayHours) {
    delayCount = muxCountHours;
  } else if (pinStep == displayMins) {
    delayCount = muxCountMins;
  } else if (pinStep == displaySecs) {
    delayCount = muxCountSecs;
  }

  timer1_write(delayCount);
}

// ************************************************************
// Interrupt routine for scheduled interrupts while scanning
// Spin through each of the pins on the decatron, using a short
// interrupt time for "off" pins (not really off, but quite dim)
// and longer dwell times for the lit pins.
// ************************************************************
ICACHE_RAM_ATTR void displayUpdateScanning() {
  phaseStep++;

  if (phaseStep == 3) {
    phaseStep = 0;
    digitStep++;
    if (digitStep==10){
      digitStep = 0;
    }
  }

  G_step(phaseStep);

  timer1_write(startupDelay);
}

// ************************************************************
// Perform the actual step
// ************************************************************
ICACHE_RAM_ATTR void G_step(int CINT)
{
  if (CINT == 0)
  {
    digitalWrite(Guide1, LOW);
    digitalWrite(Guide2, LOW);
  }
  if (CINT == 1)
  {
    digitalWrite(Guide1, HIGH);
    digitalWrite(Guide2, LOW);
  }
  if (CINT == 2)
  {
    digitalWrite(Guide1, LOW);
    digitalWrite(Guide2, HIGH);
  }
}

// ************************************************************
// Detect the falling edge of the Index pin and get the index
// value - adjust it to allow the display to rotate
// ************************************************************
ICACHE_RAM_ATTR void indexMarkTrigger() {
  indexMark = digitStep;
  if (digitStep != current_config.displayRotate) {
    digitStep--;
    if (digitStep > 9) {
      digitStep = 9;
    }
  }
}

// ----------------------------------------------------------------------------------------------------
// ----------------------------------------------  Set up  --------------------------------------------
// ----------------------------------------------------------------------------------------------------
void setup() {
  debugManager.setUp(debugVal);

  pinMode(Guide1, OUTPUT);
  pinMode(Guide2, OUTPUT);
  pinMode(Index, INPUT_PULLUP);
  pinMode(HVEnable, OUTPUT);
  digitalWrite(HVEnable, HIGH);

  debugManager.debugMsg("Starting display interrupt handler for scanning");

  attachInterrupt(digitalPinToInterrupt(Index), indexMarkTrigger, FALLING);

  startupDelay = INT_MUX_COUNTS_OFF * 2;
  timer1_attachInterrupt(displayUpdateScanning);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);
  timer1_write(startupDelay);

  debugManager.debugMsg("Started");

  WiFiManager wifiManager;
  wifiManager.setDebugOutput(debugVal);

  // set up the NTP component and wire up the "got time update" callback
  ntpAsync.setUp();
  NewTimeCallback ntcb = newTimeUpdateReceived;
  ntpAsync.setNewTimeCallback(ntcb);

  DebugCallback dbcb = debugMsgLocal;
  ntpAsync.setDebugCallback(dbcb);
  ntpAsync.setDebugOutput(debugVal);

  // Start I2C now so that the update from NTP can be sent to the RTC immediately
  Wire.begin(4, 5); // SDA = D2 = pin 4, SCL = D1 = pin 5
  debugManager.debugMsg("I2C master started");

  button1.reset();

  // Let the user do the factory reset if required
  delay(1000);

  for (int i = 0; i < 25 ; i++) button1.checkButton(millis());

  if (button1.isButtonPressedNow()) {
    factoryReset();
    delay(1000);
    ESP.restart();
  }

  if (!spiffs.getConfigFromSpiffs(&current_config)) {
    if (spiffs.testMountSpiffs()) {
      debugManager.debugMsg("No config found - resetting everything");
      factoryReset();
    } else {
      debugManager.debugMsg("Could not mount SPIFFS - flashing error?");
    }
  } else {
    debugManager.debugMsg("Loaded SPIFFS");

    setWebAuthentication(current_config.webAuthentication);
    setWebUserName(current_config.webUsername);
    setWebPassword(current_config.webPassword);

    ntpAsync.setTZS(current_config.tzs);
    ntpAsync.setNtpPool(current_config.ntpPool);
    ntpAsync.setUpdateInterval(current_config.ntpUpdateInterval);
  }

  setSpinUpVal();

  debugManager.debugMsg("Starting AP as " + WiFi.hostname() + "/SetMeUp!");
  wifiManager.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);
  wifiManager.setAPCallback(configModeCallback);

  startupDelay = INT_MUX_COUNTS_OFF;

  boolean connected = false;
  connected = wifiManager.autoConnect("decatron", "SetMeUp!");

  if (connected) {
    debugManager.debugMsg("Connected!");
  }

  IPAddress apIP = WiFi.softAPIP();
  IPAddress myIP = WiFi.localIP();
  debugManager.debugMsg("AP IP address: " + formatIPAsString(apIP));
  debugManager.debugMsg("IP address: " + formatIPAsString(myIP));

  // If we lose the connection, we try to recover it
  WiFi.setAutoReconnect(true);

  // See if we can already get the time
  ntpAsync.getTimeFromNTP();

  long startSearchMs = millis();
  bool needUpdate = true;
  while (((millis() - startSearchMs) < 10000) && needUpdate) {
    delay(1000);
    ntpAsync.getTimeFromNTP();
    if (ntpAsync.ntpTimeValid(millis()) > 0) {
      debugManager.debugMsg("Got update from NTP, exiting loop");
      needUpdate = false;
    }
  }

  setServerPageHandlers();
  server.begin();
  debugManager.debugMsg("HTTP server started");

  if (getOTAvailable()) {
    debugManager.debugMsg("Can do OTA: sketch size ("+String(ESP.getSketchSize())+"), flash size ("+String(ESP.getFlashChipRealSize())+")");
    httpUpdater.setup(&server, "/update", "admin", "update");
  } else {
    debugManager.debugMsg("NO OTA: sketch size ("+String(ESP.getSketchSize())+"), flash size ("+String(ESP.getFlashChipRealSize())+")");
  }

  if (mdns.begin(WiFi.hostname(), WiFi.localIP())) {
    debugManager.debugMsg("MDNS responder started as http://" + WiFi.hostname() + ".local");
    mdns.addService("http", "tcp", 80);
  }

  // setPIRPullup(current_config.usePIRPullup);

  // initialise the internal time (in case we don't find the time provider)
  nowMillis = millis();

  testRTCTimeProvider();
  if (useRTC) {
    getRTCTime(true);
  } else {
  }

  // Note that we had an RTC to allow the indicator DP to have meaning
  onceHadAnRTC = useRTC;

  debugManager.debugMsg("Exit startup");
  spiffs.getStatsFromSpiffs(&current_stats);

  muxCount = INT_MUX_COUNTS_OFF;

  timer1_attachInterrupt(displayUpdate);
}

void setSpinUpVal(){
  switch (current_config.spinUpSpeed) {
    case SPIN_UP_SLOW: {
      spinUpVal = 200;
      break;
    }
    case SPIN_UP_MEDIUM: {
      spinUpVal = 100;
      break;
    }
    case SPIN_UP_FAST: {
      spinUpVal = 50;
      break;
    }
  }
}

// the loop function runs over and over again forever
void loop() {
  server.handleClient();
  mdns.update();

  nowMillis = millis();

  // shows us how fast the inner loop is running
  impressionsPerSec++;

  // -------------------------------------------------------------------------------

  if (lastSecond != second()) {
    lastSecond = second();
    lastSecMillis = nowMillis;
    secondsChanged = true;
    performOncePerSecondProcessing();

    if ((second() == 0) && (!triggeredThisSec)) {
      if ((minute() == 0)) {
        if (hour() == 0) {
          performOncePerDayProcessing();
        }
        performOncePerHourProcessing();
      }
      performOncePerMinuteProcessing();
    }

    // Make sure we don't call multiple times
    triggeredThisSec = true;

    if ((second() > 0) && triggeredThisSec) {
      triggeredThisSec = false;
    }
  }

  if (digitalRead(pirPin) == HIGH) {
    pirConsecutiveCounts++;
  }

  millisInSecond = nowMillis - lastSecMillis;

  if (current_config.flashSeconds) {
    if (current_config.showSeconds) {
      if (millisInSecond > 500) {
        displayMins = displayMinsPrep;
      } else {
        displayMins = 255;
      }
      if ((millisInSecond / 100) % 2 == 0) {
        displaySecs = displaySecsPrep;
      } else {
        displaySecs = 255;
      }
    }
  } else {
    displayMins = displayMinsPrep;
    displaySecs = displaySecsPrep;
  }

  if (blanked) {
    if (muxCount < INT_MUX_COUNTS_OFF) {
      muxCount += 500;
    } else {
      digitalWrite(HVEnable, false);
    }
  } else {
    digitalWrite(HVEnable, true);
    if (muxCount > INT_MUX_COUNTS_ON) {
      muxCount -= (((muxCount-INT_MUX_COUNTS_ON)/spinUpVal) + 1);
      if (muxCount < INT_MUX_COUNTS_ON) muxCount = INT_MUX_COUNTS_ON;
    }
  }

  // help index location
  if (indexMark != current_config.displayRotate) {
    muxCount = INT_MUX_COUNTS_ON * 200;
  }

  delay(10);
}

// ************************************************************
// Called once per second
// ************************************************************
void performOncePerSecondProcessing() {
  // Store the current value and reset
  lastImpressionsPerSec = impressionsPerSec;
  impressionsPerSec = 0;

  boolean pirBlanked = checkPIR(nowMillis);
  boolean nativeBlanked = checkBlanking();

  // Check if we are in blanking suppression mode
  blanked = (nativeBlanked || pirBlanked) && (blankSuppressedMillis == 0);

  if (blanked) {
    debugManager.debugMsg("blanked");
    if (pirBlanked)
    debugManager.debugMsg(" -> PIR");
    if (nativeBlanked)
      debugManager.debugMsg(" -> Native");
  }

  // reset the blanking period selection timer
  if (nowMillis > blankSuppressedSelectionTimoutMillis) {
    blankSuppressedSelectionTimoutMillis = 0;
    blankSuppressStep = 0;
  }

  // Reset PIR debounce
  pirConsecutiveCounts = 0;

  if (ntpAsync.getNextUpdate(nowMillis) < 0) {
    ntpAsync.getTimeFromNTP();
  }

  // get hour dot, from hours (0-23) mod 12, plus minutes, so we end
  // up with a value in the range of 0 (00:00) to 719 (11:59)
  int adjustedHour = (hour() % 12) * 60 + minute();

  float hourFloat = adjustedHour * 30.0 / 720;
  displayHours = (byte) hourFloat;
  displayMinsPrep = minute()/2;

  if (current_config.showSeconds) {
    displaySecsPrep = second()/2;
  } else {
    displaySecsPrep = 255;
  }

  // Async restart
  if (restartAtMillis > 0) {
    restartAtMillis -=1000;
    if (restartAtMillis <= 0) {
      if (restartResetWifi) {
        WiFiManager wifiManager;
        wifiManager.resetSettings();
        delay(500);
      }
      ESP.restart();
    }
  }
}

// ************************************************************
// Called once per minute
// ************************************************************
void performOncePerMinuteProcessing() {
  debugManager.debugMsg("---> OncePerMinuteProcessing");

  debugManager.debugMsg("nu: " + String(ntpAsync.getNextUpdate(nowMillis)));

  // Set the internal time to the time from the RTC even if we are still in
  // NTP valid time. This is more accurate than using the internal time source
  getRTCTime(true);

  // Usage stats
  current_stats.uptimeMins++;

  if (!blanked) {
    current_stats.tubeOnTimeMins++;
  }

  // RTC - light up only if we have lost our RTC
  if (onceHadAnRTC && !useRTC) {
    // debugManager.debugMsg("lost access to RTC");
  }

  if (getIsConnected()) {
    IPAddress ip = WiFi.localIP();
    debugManager.debugMsg("IP: " + formatIPAsString(ip));
  } else {
    IPAddress ip = WiFi.localIP();
    debugManager.debugMsg("Not connected IP: " + formatIPAsString(ip));
  }
}

// ************************************************************
// Called once per hour
// ************************************************************
void performOncePerHourProcessing() {
  debugManager.debugMsg("---> OncePerHourProcessing");
  reconnectDroppedConnection();
}

// ************************************************************
// Called once per day
// ************************************************************
void performOncePerDayProcessing() {
  debugManager.debugMsg("---> OncePerDayProcessing");
  spiffs.saveStatsToSpiffs(&current_stats);
}


// ************************************************************
// Callback: When the NTP component tells us there is an update
// go and get it
// ************************************************************
void newTimeUpdateReceived() {
  debugManager.debugMsg("Got a new time update");
  setTimeFromServer(ntpAsync.getLastTimeFromServer());
  setRTCTime();
}

// ************************************************************
// Set the time from the value we get back from the timer server
// ************************************************************
void setTimeFromServer(String timeString) {
  int intValues[6];
  grabInts(timeString, &intValues[0], ",");
  setTime(intValues[SYNC_HOURS], intValues[SYNC_MINS], intValues[SYNC_SECS], intValues[SYNC_DAY], intValues[SYNC_MONTH], intValues[SYNC_YEAR]);
  debugManager.debugMsg("Set internal time to NTP time: " + String(year()) + ":" + String(month()) + ":" + String(day()) + " " + String(hour()) + ":" + String(minute()) + ":" + String(second()));
}

//**********************************************************************************
//**********************************************************************************
//*                         RTC Module Time Provider                               *
//**********************************************************************************
//**********************************************************************************

// ************************************************************
// Get the time from the RTC
// ************************************************************
String getRTCTime(boolean setInternalTime) {
  testRTCTimeProvider();
  if (useRTC) {
    Clock.getTime();
    int years = Clock.year + 2000;
    byte months = Clock.month;
    byte days = Clock.dayOfMonth;
    byte hours = Clock.hour;
    byte mins = Clock.minute;
    byte secs = Clock.second;

    String returnValue = String(years) + ":" + String(months) + ":" + String(days) + " " + String(hours) + ":" + String(mins) + ":" + String(secs);
    debugManager.debugMsg("Got RTC time: " + returnValue);

    if (setInternalTime) {
      // Set the internal time provider to the value we got
      setTime(hours, mins, secs, days, months, years);
      debugManager.debugMsg("Set Internal time to: " + returnValue);
    }

    return returnValue;
  }
  return "";
}

// ************************************************************
// Check that we still have access to the time from the RTC
// ************************************************************
void testRTCTimeProvider() {
  // Set up the time provider
  // first try to find the RTC, if not available, go into slave mode
  Wire.beginTransmission(RTC_I2C_ADDRESS);
  useRTC = (Wire.endTransmission() == 0);
}

// ************************************************************
// Set the date/time in the RTC from the internal time
// Always hold the time in 24 format, we convert to 12 in the
// display.
// ************************************************************
void setRTCTime() {
  testRTCTimeProvider();
  if (useRTC) {
    Clock.fillByYMD(year() % 100, month(), day());
    Clock.fillByHMS(hour(), minute(), second());
    Clock.setTime();

    debugManager.debugMsg("Set RTC time to internal time: " + String(year()) + ":" + String(month()) + ":" + String(day()) + " " + String(hour()) + ":" + String(minute()) + ":" + String(second()));
  }
}

// ************************************************************
// Local debug routine
// ************************************************************
void debugMsgLocal(String message) {
  debugManager.debugMsg(message);
}

// ************************************************************
// See if we have enough flash space for OTA
// ************************************************************
boolean getOTAvailable() {
  return ESP.getSketchSize()*2 < ESP.getFlashChipRealSize();
}

// ----------------------------------------------------------------------------------------------------
// ----------------------------------------- Network handling -----------------------------------------
// ----------------------------------------------------------------------------------------------------

// ************************************************************
// What it says on the tin
// ************************************************************
boolean getIsConnected() {
  return WiFi.isConnected();
}

// ************************************************************
// Tell the Diag LED that we have connected
// ************************************************************
void configModeCallback(WiFiManager *myWiFiManager) {
  debugManager.debugMsg("*** Entered config mode");
  startupDelay = INT_MUX_COUNTS_ON * 1000;
}

// ************************************************************
// In the case that we have lost the WiFi connection, try to reconnect
// ************************************************************
void reconnectDroppedConnection() {
  // Try to reconnect if we are  not connected
  if (WiFi.status() != WL_CONNECTED) {
    debugManager.debugMsg("Attepting to reconnect dropped connection WiFi connection to " + WiFi.SSID());
    ETS_UART_INTR_DISABLE();
    wifi_station_disconnect();
    ETS_UART_INTR_ENABLE();
    WiFi.begin();
  }
}

// ************************************************************
// set up the server page handlers
// ************************************************************
void setServerPageHandlers() {
  server.on("/", rootPageHandler);

  server.on("/time", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return timeServerPageHandler();
  });

  server.on("/resetoptions", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return resetOptionsPageHandler();
  });

  server.on("/resetwifi", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return resetWiFiPageHandler();
  });

  server.on("/resetall", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return resetAllPageHandler();
  });

  server.on("/ntpupdate", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return ntpUpdatePageHandler();
  });

  server.on("/clockconfig", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return clockConfigPageHandler();
  });

  server.on("/utility", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return utilityPageHandler();
  });

  server.on("/debug", []() {
    if (getWebAuthentication() && (!server.authenticate(getWebUserName().c_str(), getWebPassword().c_str()))) {
      return server.requestAuthentication();
    }
    return debugPageHandler();
  });

  server.on("/restart", silentRestartPageHandler);

  server.on("/local.css",   localCSSHandler);

  server.onNotFound(handleNotFound);
}

//**********************************************************************************
//**********************************************************************************
//*                                Page Handlers                                   *
//**********************************************************************************
//**********************************************************************************

// ************************************************************
// Summary page
// ************************************************************
void rootPageHandler() {
  debugManager.debugMsg("Root page in");

  String response_message = getHTMLHead(getIsConnected());
  response_message += getNavBar();

  if (restartAtMillis > 0) {
      if (restartResetWifi) {
        response_message += "<div class=\"alert alert-warning fade in\"><strong>Reset scheduled!</strong> Clock will now restart to apply the changes. After the restart you will need to enter the WiFi credentials again</div></div>";
      } else {
        response_message += "<div class=\"alert alert-warning fade in\"><strong>Restart scheduled!</strong> Clock will now restart to apply the changes</div></div>";
      }
  }

  // Status table
  response_message += getTableHead2Col("Current Status", "Name", "Value");

  if (WiFi.status() == WL_CONNECTED)
  {
    IPAddress ip = WiFi.localIP();
    response_message += getTableRow2Col("WLAN IP", formatIPAsString(ip));
    response_message += getTableRow2Col("WLAN MAC", WiFi.macAddress());
    response_message += getTableRow2Col("WLAN SSID", WiFi.SSID());
    response_message += getTableRow2Col("NTP Pool", ntpAsync.getNtpPool());
    response_message += getTableRow2Col("TZ", ntpAsync.getTZS());
    String clockUrl = "http://" + WiFi.hostname() + ".local";
    clockUrl.toLowerCase();
    response_message += getTableRow2Col("Clock Name", WiFi.hostname() + " (<a href = \"" + clockUrl + "\">" + clockUrl + "</a>)");
  }
  else
  {
    IPAddress softapip = WiFi.softAPIP();
    response_message += getTableRow2Col("AP IP", formatIPAsString(softapip));
    response_message += getTableRow2Col("AP MAC", WiFi.softAPmacAddress());
  }

  response_message += getTableRow2Col("Display Time", timeToReadableString(year(),month(),day(),hour(),minute(),second()));
  response_message += getTableRow2Col("Uptime", secsToReadableString(millis() / 1000));

  response_message += getTableRow2Col("Last NTP time", timeStringToReadableString(ntpAsync.getLastTimeFromServer()));
  long lastUpdateTimeSecs = ntpAsync.getLastUpdateTimeSecs(millis());
  response_message += getTableRow2Col("Last NTP update", secsToReadableString(lastUpdateTimeSecs) + " ago");

  signed long absNextUpdate = abs(ntpAsync.getNextUpdate(nowMillis));
  String overdueInd = "";
  if (absNextUpdate < 0) {
    overdueInd = " overdue";
    absNextUpdate = -absNextUpdate;
  }
  response_message += getTableRow2Col("Time before next NTP update", secsToReadableString(absNextUpdate) + overdueInd);

  response_message += getTableRow2Col("Version", SOFTWARE_VERSION);

  // Scan I2C bus
  for (int idx = 0 ; idx < 128 ; idx++)
  {
    Wire.beginTransmission(idx);
    int error = Wire.endTransmission();
    if (error == 0) {
      String slaveMsg = "Found I2C slave at";
      response_message += getTableRow2Col(slaveMsg, idx);
    }
  }

  String connectionInfo = "";
  if (getIsConnected()) {
    connectionInfo += "W";
  } else {
    connectionInfo += "w";
  }
  if (ntpAsync.ntpTimeValid(nowMillis)) {
    connectionInfo += "N";
  } else {
    connectionInfo += "n";
  }
  if (spiffs.getSpiffsMounted()) {
    connectionInfo += "S";
  } else {
    connectionInfo += "s";
  }
  if (getOTAvailable()) {
    connectionInfo += "U";
  } else {
    connectionInfo += "u";
  }
  if (getWebAuthentication()) {
    connectionInfo += "A";
  } else {
    connectionInfo += "a";
  }

  if (debugVal) {
    connectionInfo += "D";
  } else {
    connectionInfo += "d";
  }

  response_message += getTableRow2Col("Status string", connectionInfo);

  String indexInfo = String(indexMark) + "/" + String(current_config.displayRotate);
  response_message += getTableRow2Col("Index", indexInfo);
  response_message += getTableFoot();

  // ******************** Clock Info table ***************************
//  float digitBrightness = (1023 - ldrValue) * 100.0 / 1023.0;
  String motionSensorState = checkPIRInstalled() ? getPIRStateDisplay() : "Not installed";
  String rtcState = useRTC ? "Installed" : "Not installed";
  String timeSource;

  if (ntpAsync.ntpTimeValid(nowMillis)) {
    timeSource = "NTP";
  } else if (useRTC) {
    timeSource = "RTC";
  } else {
    timeSource = "Internal";
  }
  String currentTime = String(year()) + ":" + String(month()) + ":" + String(day()) + " " + String(hour()) + ":" + String(minute()) + ":" + String(second());
  response_message += getTableHead2Col("Clock information", "Name", "Value");

  response_message += getTableRow2Col("Motion Sensor", motionSensorState);
  response_message += getTableRow2Col("Time Source", timeSource);
  response_message += getTableRow2Col("Real Time Clock", rtcState);
  if (useRTC) {
    response_message += getTableRow2Col("RTC Time", getRTCTime(false));
  }
  response_message += getTableRow2Col("Impressions/Sec", lastImpressionsPerSec);
  response_message += getTableRow2Col("Total Clock On Hrs", secsToReadableString(current_stats.uptimeMins * 60));
  response_message += getTableRow2Col("Total Tube On Hrs", secsToReadableString(current_stats.tubeOnTimeMins * 60));
  response_message += getTableFoot();

  // ******************** ESP8266 Info table ***************************
  response_message += getTableHead2Col("ESP8266 information", "Name", "Value");
  response_message += getTableRow2Col("Sketch size", ESP.getSketchSize());
  response_message += getTableRow2Col("Free sketch size", ESP.getFreeSketchSpace());
  response_message += getTableRow2Col("Sketch hash", ESP.getSketchMD5());
  response_message += getTableRow2Col("Free heap", ESP.getFreeHeap());
  response_message += getTableRow2Col("Boot version", ESP.getBootVersion());
  response_message += getTableRow2Col("CPU Freqency (MHz)", ESP.getCpuFreqMHz());
  response_message += getTableRow2Col("Flash speed (MHz)", ESP.getFlashChipSpeed() / 1000000);
  response_message += getTableRow2Col("SDK version", ESP.getSdkVersion());
  response_message += getTableRow2Col("Chip ID", String(ESP.getChipId(), HEX));
  response_message += getTableRow2Col("Flash Chip ID", String(ESP.getFlashChipId(), HEX));
  response_message += getTableRow2Col("Flash size", ESP.getFlashChipRealSize());
  response_message += getTableFoot();

  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);

  debugManager.debugMsg("Root page out");
}

// ************************************************************
// Manage the NTP Settings
// ************************************************************
void timeServerPageHandler() {
  debugMsg("Time page in");

  boolean changed = false;

  changed |= checkServerArgString("ntppool", "NTP Pool", current_config.ntpPool);
  changed |= checkServerArgInt("ntpupdate", "NTP update interval", current_config.ntpUpdateInterval);
  changed |= checkServerArgString("tzs", "Time Zone String", current_config.tzs);
  if (changed) {
    ntpAsync.setTZS(current_config.tzs);
    ntpAsync.setNtpPool(current_config.ntpPool);
    ntpAsync.setUpdateInterval(current_config.ntpUpdateInterval);
  }
  saveToSpiffsIfChanged(changed);

  // -----------------------------------------------------------------------------

  String response_message = getHTMLHead(getIsConnected());
  response_message += getNavBar();

  response_message += getFormHead("Select time server");
  response_message += getTextInputWide("NTP Pool", "ntppool", ntpAsync.getNtpPool(), false);
  response_message += getNumberInput("Update interval:", "ntpupdate", NTP_UPDATE_INTERVAL_MIN, NTP_UPDATE_INTERVAL_MAX, ntpAsync.getUpdateInterval(), false);
  response_message += getTextInputWide("Time Zone String", "tzs", ntpAsync.getTZS(), false);
  response_message += getSubmitButton("Set");

  response_message += "<br/><hr/>";
  response_message += "For a list of commonly used values Time Zone String values, go to ";
  response_message += "<a href=\"https://www.nixieclock.biz/help/TZ-Variable.html\" target=\"_blank\">Time Zone Reference</a>";

  response_message += "<br/><hr/>";
  response_message += "The NTP poll time should <i>not</i> be set to a multiple of 60 seconds. That will help spread ";
  response_message += "out the load on the NTP servers. 7261 seconds is good, 7200 is not. If using an ntp.org pool ";
  response_message += "server, polls should happen no more often than every 1/2 hour (1800 seconds) to comply with ";
  response_message += "their terms of service. Start by setting the polling interval to a high value then observe ";
  response_message += "how well the clock stays in sync. If it gets more than a second out of sync during that period, ";
  response_message += "lower it. NTP providers will thank you. Regardless of the polling period, the clock will ";
  response_message += "be sent a time update every second.";

  response_message += getFormFoot();
  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);

  debugMsg("Time page out");
}

// ************************************************************
// Page for the clock configuration
// ************************************************************
void clockConfigPageHandler() {
  debugManager.debugMsg("Config page in");

  boolean changed = false;
  changed |= checkServerArgBoolean("showSeconds", "Show seconds", "Show", "Hide", current_config.showSeconds);
  changed |= checkServerArgBoolean("flashSeconds", "Flash seconds", "Flash", "Fixed", current_config.flashSeconds);
  changed |= checkServerArgByte("dayBlanking", "dayBlanking", current_config.dayBlanking);
  changed |= checkServerArgByte("blankHourStart", "blankHourStart", current_config.blankHourStart);
  changed |= checkServerArgByte("blankHourEnd", "blankHourEnd", current_config.blankHourEnd);
  changed |= checkServerArgInt("pirTimeout", "pirTimeout", current_config.pirTimeout);
  changed |= checkServerArgByte("displayRotate", "displayRotate", current_config.displayRotate);
  changed |= checkServerArgBoolean("usePIRPullup", "Use PIR pullup", "on", "off", current_config.usePIRPullup);
  changed |= checkServerArgByte("spinUpSpeed", "spinUpSpeed", current_config.spinUpSpeed);
  boolean webConfigChanged = false;
  webConfigChanged |= checkServerArgBoolean("webAuthentication", "Web Authentication", "on", "off", current_config.webAuthentication);
  webConfigChanged |= checkServerArgString("webUsername", "webUsername", current_config.webUsername);
  webConfigChanged |= checkServerArgString("webPassword", "webPassword", current_config.webPassword);
  if (webConfigChanged) {
    changed = true;
    setWebAuthentication(current_config.webAuthentication);
    setWebUserName(current_config.webUsername);
    setWebPassword(current_config.webPassword);
  }

  saveToSpiffsIfChanged(changed);

  if (changed) {
    setSpinUpVal();
  }

  // -----------------------------------------------------------------------------
  String response_message = getHTMLHead(getIsConnected());
  response_message += getNavBar();

  // -----------------------------------------------------------------------------
  response_message += getFormHead("General options");

  // show seconds dot
  response_message += getRadioGroupHeader("Show seconds:");
  if (current_config.showSeconds) {
    response_message += getRadioButton("showSeconds", "Show", "Show", true);
    response_message += getRadioButton("showSeconds", "Hide", "Hide", false);
  } else {
    response_message += getRadioButton("showSeconds", "Show", "Show", false);
    response_message += getRadioButton("showSeconds", "Hide", "Hide", true);
  }
  response_message += getRadioGroupFooter();

  // flash seconds/minutes dot
  response_message += getRadioGroupHeader("Flash seconds/minutes:");
  if (current_config.flashSeconds) {
    response_message += getRadioButton("flashSeconds", "Flash", "Flash", true);
    response_message += getRadioButton("flashSeconds", "Fixed", "Fixed", false);
  } else {
    response_message += getRadioButton("flashSeconds", "Flash", "Flash", false);
    response_message += getRadioButton("flashSeconds", "Fixed", "Fixed", true);
  }
  response_message += getRadioGroupFooter();

  // LDR threshold
  response_message += getNumberInput("Display Rotation:", "displayRotate", DISPLAY_ROTATE_MIN, DISPLAY_ROTATE_MAX, current_config.displayRotate, false);

  // Spin Up Speed
  response_message += getDropDownHeader("Spin Up Speed:", "spinUpSpeed", false, false);
  response_message += getDropDownOption("0", "Slow spin up", (current_config.spinUpSpeed == SPIN_UP_SLOW));
  response_message += getDropDownOption("1", "Medium spin up", (current_config.spinUpSpeed == SPIN_UP_MEDIUM));
  response_message += getDropDownOption("2", "Fast spin up", (current_config.spinUpSpeed == SPIN_UP_FAST));
  response_message += getDropDownFooter();

  response_message += getSubmitButton("Set");

  response_message += getFormFoot();

  // -----------------------------------------------------------------------------
  response_message += getFormHead("Digit blanking");

  if (pirInstalled) {
    response_message += getExplanationText("Motion detector installed - time based blanking is disabled");
  } else {
    response_message += getExplanationText("Motion detector not installed - motion detector blanking is inactive");
  }

  // PIR timeout
  response_message += getNumberInput("Motion timeout:", "pirTimeout", PIR_TIMEOUT_MIN, PIR_TIMEOUT_MAX, current_config.pirTimeout, !pirInstalled);

  // PIR pullup
  response_message += getRadioGroupHeader("Use Motion detector pullup:");
  if (current_config.usePIRPullup) {
    response_message += getRadioButton("usePIRPullup", "On", "on", true);
    response_message += getRadioButton("usePIRPullup", "Off", "off", false);
  } else {
    response_message += getRadioButton("usePIRPullup", "On", "on", false);
    response_message += getRadioButton("usePIRPullup", "Off", "off", true);
  }
  response_message += getRadioGroupFooter();

  // Day blanking
  response_message += getDropDownHeader("Day blanking:", "dayBlanking", true, pirInstalled);
  response_message += getDropDownOption("0", "Never blank", (current_config.dayBlanking == 0));
  response_message += getDropDownOption("1", "Blank all day on weekends", (current_config.dayBlanking == 1));
  response_message += getDropDownOption("2", "Blank all day on week days", (current_config.dayBlanking == 2));
  response_message += getDropDownOption("3", "Blank always", (current_config.dayBlanking == 3));
  response_message += getDropDownOption("4", "Blank during selected hours every day", (current_config.dayBlanking == 4));
  response_message += getDropDownOption("5", "Blank during selected hours on week days and all day on weekends", (current_config.dayBlanking == 5));
  response_message += getDropDownOption("6", "Blank during selected hours on weekends and all day on week days", (current_config.dayBlanking == 6));
  response_message += getDropDownOption("7", "Blank during selected hours on weekends only", (current_config.dayBlanking == 7));
  response_message += getDropDownOption("8", "Blank during selected hours on week days only", (current_config.dayBlanking == 8));
  response_message += getDropDownFooter();

  // disable the hours if we have a pir installed or if the mode doesn't require it
  boolean hoursDisabled = (current_config.dayBlanking < 4) || pirInstalled;

  // Blank hours from
  response_message += getNumberInput("Blank from:", "blankHourStart", 0, 23, current_config.blankHourStart, hoursDisabled);

  // Blank hours to
  response_message += getNumberInput("Blank to:", "blankHourEnd", 0, 23, current_config.blankHourEnd, hoursDisabled);

  response_message += getSubmitButton("Set");

  response_message += getFormFoot();

  // -----------------------------------------------------------------------------
  response_message += getFormHead("Security");

  // web auth mode
  response_message += getRadioGroupHeader("Web Authentication:");
  if (current_config.webAuthentication) {
    response_message += getRadioButton("webAuthentication", " On", "on", true);
    response_message += getRadioButton("webAuthentication", " Off", "off", false);
  } else {
    response_message += getRadioButton("webAuthentication", " On", "on", false);
    response_message += getRadioButton("webAuthentication", " Off", "off", true);
  }
  response_message += getRadioGroupFooter();

  response_message += getTextInput("User Name", "webUsername", current_config.webUsername, !current_config.webAuthentication);
  response_message += getTextInput("Password", "webPassword", current_config.webPassword, !current_config.webAuthentication);

  response_message += getSubmitButton("Set");

  response_message += getFormFoot();

  // all done
  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);

  debugManager.debugMsg("Config page out");
}

// ************************************************************
// Access to utility functions
// ************************************************************
void utilityPageHandler()
{
  debugMsg("Utility page in");
  String response_message = getHTMLHead(getIsConnected());
  response_message += getNavBar();

  response_message += getTableHead2Col("Utilities", "Description", "Action");
  response_message += getTableRow2Col("Reset Wifi Only (Reset WiFi, leaves clock configuration)", "<a href=\"/resetwifi\">Reset WiFi</a>");
  response_message += getTableRow2Col("Reset Configuration Only (Reset clock configuration, leaves WiFi)", "<a href=\"/resetoptions\">Reset Configuration</a>");
  response_message += getTableRow2Col("Reset All (Reset clock configuration, rest WiFi)", "<a href=\"/resetall\">Reset All</a>");
  if(getOTAvailable()) {
    response_message += getTableRow2Col("Update firmware", "<a href=\"/update\">Update</a>");
  } else {
    response_message += getTableRow2Col("Update firmware", "not available - memory too small");
  }
  response_message += getTableRow2Col("Force NTP update right now", "<a href=\"/ntpupdate\">Update NTP</a>");

  response_message += getTableFoot();

  response_message += getHTMLFoot();
  server.send(200, "text/html", response_message);
  debugMsg("Utility page out");
}

// ************************************************************
// Reset just the wifi
// ************************************************************
void resetWiFiPageHandler() {
  debugManager.debugMsg("Reset WiFi page in");

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");

  debugManager.debugMsg("Reset WiFi page out");

  // schedule a restart
  restartAtMillis = 2000;
  restartResetWifi = true;
}

// ************************************************************
// Reset the EEPROM and WiFi values, restart
// ************************************************************
void resetAllPageHandler() {
  debugManager.debugMsg("Reset All page in");

  factoryReset();

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");

  debugManager.debugMsg("Reset All page out");

  // schedule a restart
  restartAtMillis = 2000;
  restartResetWifi = true;
}

// ************************************************************
// Reset the EEPROM and WiFi values, restart
// ************************************************************
void resetOptionsPageHandler() {
  debugManager.debugMsg("Reset Options page in");

  factoryReset();

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");

  debugManager.debugMsg("Reset Options page out");

  // schedule a restart
  restartAtMillis = 2000;
}

// ************************************************************
// Do an NTP update now
// ************************************************************
void ntpUpdatePageHandler() {
  debugManager.debugMsg("NTP Update page in");

  ntpAsync.getTimeFromNTP();

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");

  debugManager.debugMsg("NTP Update page out");
}

// ************************************************************
// Turn on debugging
// ************************************************************
void debugPageHandler() {
  debugManager.debugMsg("Debug page in");

  debugManager.setUp(true);

  server.sendHeader("Location", "/utility", true);
  server.send(302, "text/plain", "");

  debugManager.debugMsg("Debug page out");
}

// ************************************************************
// Emergency restart point
// ************************************************************
void silentRestartPageHandler() {
  ESP.restart();
}

// ************************************************************
// Called if requested page is not found
// ************************************************************
void handleNotFound() {
  debugManager.debugMsg("404 page in");

  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);

  debugManager.debugMsg("404 page out");
}

// ************************************************************
// Called if we need to have a local CSS
// ************************************************************
void localCSSHandler()
{
  PGM_P cssStr = PSTR(".navbar,.table{margin-bottom:20px}.nav>li,.nav>li>a,article,aside,details,figcaption,figure,footer,header,hgroup,main,menu,nav,section,summary{display:block}.btn,.form-control,.navbar-toggle{background-image:none}.table,label{max-width:100%}.sub-header{padding-bottom:10px;border-bottom:1px solid #eee}.h3,h3{font-size:24px}.table{width:100%}table{background-color:transparent;border-spacing:0;border-collapse:collapse}.table-striped>tbody>tr:nth-of-type(2n+1){background-color:#f9f9f9}.table>caption+thead>tr:first-child>td,.table>caption+thead>tr:first-child>th,.table>colgroup+thead>tr:first-child>td,.table>colgroup+thead>tr:first-child>th,.table>thead:first-child>tr:first-child>td,.table>thead:first-child>tr:first-child>th{border-top:0}.table>thead>tr>th{border-bottom:2px solid #ddd}.table>tbody>tr>td,.table>tbody>tr>th,.table>tfoot>tr>td,.table>tfoot>tr>th,.table>thead>tr>td,.table>thead>tr>th{padding:8px;line-height:1.42857143;vertical-align:top;border-top:1px solid #ddd}th{text-align:left}td,th{padding:0}.navbar>.container .navbar-brand,.navbar>.container-fluid .navbar-brand{margin-left:-15px}.container,.container-fluid{padding-right:15px;padding-left:15px;margin-right:auto;margin-left:auto}.navbar-inverse .navbar-brand{color:#9d9d9d}.navbar-brand{float:left;height:50px;padding:15px;font-size:18px;line-height:20px}a{color:#337ab7;text-decoration:none;background-color:transparent}.navbar-fixed-top{border:0;top:0;border-width:0 0 1px}.navbar-inverse{background-color:#222;border-color:#080808}.navbar-fixed-bottom,.navbar-fixed-top{border-radius:0;position:fixed;right:0;left:0;z-index:1030}.nav>li,.nav>li>a,.navbar,.navbar-toggle{position:relative}.navbar{border-radius:4px;min-height:50px;border:1px solid transparent}.container{width:750px}.navbar-right{float:right!important;margin-right:-15px}.navbar-nav{float:left;margin:7.5px -15px}.nav{padding-left:0;margin-bottom:0;list-style:none}.navbar-nav>li{float:left}.navbar-inverse .navbar-nav>li>a{color:#9d9d9d}.navbar-nav>li>a{padding-top:10px;padding-bottom:10px;line-height:20px}.nav>li>a{padding:10px 15px}.navbar-inverse .navbar-toggle{border-color:#333}.navbar-toggle{display:none;float:right;padding:9px 10px;margin-top:8px;margin-right:15px;margin-bottom:8px;background-color:transparent;border:1px solid transparent;border-radius:4px}button,select{text-transform:none}button{overflow:visible}button,html input[type=button],input[type=reset],input[type=submit]{-webkit-appearance:button;cursor:pointer}.btn-primary{color:#fff;background-color:#337ab7;border-color:#2e6da4}.btn{display:inline-block;padding:6px 12px;margin-bottom:0;font-size:14px;font-weight:400;line-height:1.42857143;text-align:center;white-space:nowrap;vertical-align:middle;-ms-touch-action:manipulation;touch-action:manipulation;cursor:pointer;-webkit-user-select:none;-moz-user-select:none;-ms-user-select:none;user-select:none;border:1px solid transparent;border-radius:4px}button,input,select,textarea{font-family:inherit;font-size:inherit;line-height:inherit}input{line-height:normal}button,input,optgroup,select,textarea{margin:0;font:inherit;color:inherit}.form-control,body{font-size:14px;line-height:1.42857143}.form-horizontal .form-group{margin-right:-15px;margin-left:-15px}.form-group{margin-bottom:15px}.form-horizontal .control-label{padding-top:7px;margin-bottom:0;text-align:right}.form-control{display:block;width:100%;height:34px;padding:6px 12px;color:#555;background-color:#fff;border:1px solid #ccc;border-radius:4px;-webkit-box-shadow:inset 0 1px 1px rgba(0,0,0,.075);box-shadow:inset 0 1px 1px rgba(0,0,0,.075);-webkit-transition:border-color ease-in-out .15s,-webkit-box-shadow ease-in-out .15s;-o-transition:border-color ease-in-out .15s,box-shadow ease-in-out .15s;transition:border-color ease-in-out .15s,box-shadow ease-in-out .15s}.col-xs-8{width:66.66666667%}.col-xs-3{width:25%}.col-xs-1,.col-xs-10,.col-xs-11,.col-xs-12,.col-xs-2,.col-xs-3,.col-xs-4,.col-xs-5,.col-xs-6,.col-xs-7,.col-xs-8,.col-xs-9{float:left}.col-lg-1,.col-lg-10,.col-lg-11,.col-lg-12,.col-lg-2,.col-lg-3,.col-lg-4,.col-lg-5,.col-lg-6,.col-lg-7,.col-lg-8,.col-lg-9,.col-md-1,.col-md-10,.col-md-11,.col-md-12,.col-md-2,.col-md-3,.col-md-4,.col-md-5,.col-md-6,.col-md-7,.col-md-8,.col-md-9,.col-sm-1,.col-sm-10,.col-sm-11,.col-sm-12,.col-sm-2,.col-sm-3,.col-sm-4,.col-sm-5,.col-sm-6,.col-sm-7,.col-sm-8,.col-sm-9,.col-xs-1,.col-xs-10,.col-xs-11,.col-xs-12,.col-xs-2,.col-xs-3,.col-xs-4,.col-xs-5,.col-xs-6,.col-xs-7,.col-xs-8,.col-xs-9{position:relative;min-height:1px;padding-right:15px;padding-left:15px}label{display:inline-block;margin-bottom:5px;font-weight:700}*{-webkit-box-sizing:border-box;-moz-box-sizing:border-box;box-sizing:border-box}body{font-family:\"Helvetica Neue\",Helvetica,Arial,sans-serif;color:#333}html{font-size:10px;font-family:sans-serif;-webkit-text-size-adjust:100%}");
  server.send(200, "text/css", String(cssStr));
}

// ************************************************************
// Check the PIR status. If we don't have a PIR installed, we
// don't want to respect the pin value, because it would defeat
// normal day blanking. The first time the PIR takes the pin low
// we mark that we have a PIR and we should start to respect
// the sensor over configured blanking.
// Returns true if PIR sensor is installed and we are blanked
//
// To deal with noise, we use a counter to detect a number of
// consecutive counts before counting the PIR as "detected"
// ************************************************************
boolean checkPIR(unsigned long nowMillis) {
  // debugManager.debugMsg("pirConsecutiveCounts: " + String(pirConsecutiveCounts));
  // debugManager.debugMsg("lastImpressionsPerSec/2: " + String(lastImpressionsPerSec/2));
  // debugManager.debugMsg("pirInstalled: " + String(pirInstalled));
  // debugManager.debugMsg("pirLastSeen: " + String(pirLastSeen));
  if (pirConsecutiveCounts > (lastImpressionsPerSec / 2)) {
    pirLastSeen = nowMillis;
    pirStatus = true;
    return false;
  } else {
    if (!pirInstalled) debugManager.debugMsg("Marking PIR as installed");
    pirInstalled = true;
    if (nowMillis > (pirLastSeen + (current_config.pirTimeout * 1000))) {
      return true;
    } else {
      pirStatus = false;
      return false;
    }
  }
}

// ************************************************************
// Check if the PIR sensor is installed
// ************************************************************
boolean checkPIRInstalled() {
  return pirInstalled;
}

// ************************************************************
// Check the blanking, overridden if we have a PIR installed
// ************************************************************
boolean checkBlanking() {
  // If we are running on PIR, never use native blanking
  if (checkPIRInstalled())
    return false;

  switch (current_config.dayBlanking) {
    case DAY_BLANKING_NEVER:
      return false;
    case DAY_BLANKING_HOURS:
      return getHoursBlanked();
    case DAY_BLANKING_WEEKEND:
      return ((weekday() == 1) || (weekday() == 7));
    case DAY_BLANKING_WEEKEND_OR_HOURS:
      return ((weekday() == 1) || (weekday() == 7)) || getHoursBlanked();
    case DAY_BLANKING_WEEKEND_AND_HOURS:
      return ((weekday() == 1) || (weekday() == 7)) && getHoursBlanked();
    case DAY_BLANKING_WEEKDAY:
      return ((weekday() > 1) && (weekday() < 7));
    case DAY_BLANKING_WEEKDAY_OR_HOURS:
      return ((weekday() > 1) && (weekday() < 7)) || getHoursBlanked();
    case DAY_BLANKING_WEEKDAY_AND_HOURS:
      return ((weekday() > 1) && (weekday() < 7)) && getHoursBlanked();
    case DAY_BLANKING_ALWAYS:
      return true;
  }

  return false;
}

// ************************************************************
// Get the readable version of the PIR state
// ************************************************************
String getPIRStateDisplay() {
  float lastMotionDetection = (nowMillis - pirLastSeen) / 1000.0;

  String part3 = " ago";
  if (blanked) {
    part3 = " ago (blanked)";
  }
  return "Motion detected " + secsToReadableString(lastMotionDetection) + part3;
}

// ************************************************************
// If we are currently blanked based on hours
// ************************************************************
boolean getHoursBlanked() {
  if (current_config.blankHourStart > current_config.blankHourEnd) {
    // blanking before midnight
    return ((hour() >= current_config.blankHourStart) || (hour() < current_config.blankHourEnd));
  } else if (current_config.blankHourStart < current_config.blankHourEnd) {
    // dim at or after midnight
    return ((hour() >= current_config.blankHourStart) && (hour() < current_config.blankHourEnd));
  } else {
    // no dimming if Start = End
    return false;
  }
}


//#define MIN_DIM_DEFAULT       400  // The default minimum dim count
//#define MIN_DIM_MIN           100  // The minimum dim count
//#define MIN_DIM_MAX           800  // The maximum dim count
#define USE_PIR_PULLUP_DEFAULT          true

// ************************************************************
// Reset configuration values back to what they once were
// ************************************************************
void factoryReset() {
  ntpAsync.resetDefaults();
  spiffs_config_t* cc = &current_config;
  cc->ntpPool = ntpAsync.getNtpPool();
  cc->ntpUpdateInterval = ntpAsync.getUpdateInterval();
  cc->tzs = ntpAsync.getTZS();
  cc->dayBlanking = DAY_BLANKING_DEFAULT;
//  cc->thresholdBright = SENSOR_THRSH_DEFAULT;
//  cc->sensorSmoothCountLDR = SENSOR_SMOOTH_READINGS_DEFAULT;
//  cc->sensitivityLDR = SENSOR_SENSIT_DEFAULT;
//  cc->minDim = MIN_DIM_DEFAULT;
  cc->dayBlanking = DAY_BLANKING_DEFAULT;
  cc->blankHourStart = 0;
  cc->blankHourEnd = 7;
  cc->pirTimeout = PIR_TIMEOUT_DEFAULT;
//  cc->useLDR = USE_LDR_DEFAULT;
  cc->usePIRPullup = USE_PIR_PULLUP_DEFAULT;
  cc->testMode = true;
  setWebAuthentication(WEB_AUTH_DEFAULT);
  cc->webAuthentication = getWebAuthentication();
  setWebUserName(WEB_USERNAME_DEFAULT);
  cc->webUsername = getWebUserName();
  setWebPassword(WEB_PASSWORD_DEFAULT);
  cc->webPassword = getWebPassword();

  spiffs.saveConfigToSpiffs(cc);
}

// ************************************************************
// Conditionally trigger the save (don't write if we don't have
// any changes - that just wears out the flash)
// ************************************************************
void saveToSpiffsIfChanged(boolean changed) {
  if (changed) {
    debugManager.debugMsg("Config options changed, saving them");
    spiffs.saveConfigToSpiffs(&current_config);

    // Save the stats while we are at it
    spiffs.saveStatsToSpiffs(&current_stats);
  }
}
