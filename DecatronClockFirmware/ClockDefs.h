//**********************************************************************************
//* Definitions for the basic functions of the clock                               *
//**********************************************************************************

#ifndef ClockDefs_h
#define ClockDefs_h

// ************************** Pin Allocations *************************

#define  Guide1     D7       // Guide 1 - G1 pin of 2-guide Dekatron           // D8
#define  Guide2     D8       // Guide 2 - G2 pin of 2-guide Dekatron           // D7
#define  Index      D6       // Index   - NDX input pin. High when glow at K0  // D6
#define  HVEnable   D5
#define  pirPin     D4
#define  inputPin1  D3
#define  LDRPin     A0       // ADC input

// -------------------------------------------------------------------------------
// Software version shown in config menu
#define SOFTWARE_VERSION      4

// -------------------------------------------------------------------------------
#define SECS_MAX  60
#define MINS_MAX  60
#define HOURS_MAX 24

#define TEMP_DISPLAY_MODE_DUR_MS        5000

// -------------------------------------------------------------------------------
#define DAY_BLANKING_MIN                0
#define DAY_BLANKING_NEVER              0  // Don't blank ever (default)
#define DAY_BLANKING_WEEKEND            1  // Blank during the weekend
#define DAY_BLANKING_WEEKDAY            2  // Blank during weekdays
#define DAY_BLANKING_ALWAYS             3  // Always blank
#define DAY_BLANKING_HOURS              4  // Blank between start and end hour every day
#define DAY_BLANKING_WEEKEND_OR_HOURS   5  // Blank between start and end hour during the week AND all day on the weekend
#define DAY_BLANKING_WEEKDAY_OR_HOURS   6  // Blank between start and end hour during the weekends AND all day on week days
#define DAY_BLANKING_WEEKEND_AND_HOURS  7  // Blank between start and end hour during the weekend
#define DAY_BLANKING_WEEKDAY_AND_HOURS  8  // Blank between start and end hour during week days
#define DAY_BLANKING_MAX                8
#define DAY_BLANKING_DEFAULT            0

// -------------------------------------------------------------------------------
#define BLANK_MODE_MIN                  0
#define BLANK_MODE_TUBES                0  // Use blanking for tubes only 
#define BLANK_MODE_LEDS                 1  // Use blanking for LEDs only
#define BLANK_MODE_BOTH                 2  // Use blanking for tubes and LEDs
#define BLANK_MODE_MAX                  2
#define BLANK_MODE_DEFAULT              2

// -------------------------------------------------------------------------------
#define PIR_TIMEOUT_MIN                 60    // 1 minute in seconds
#define PIR_TIMEOUT_MAX                 3600  // 1 hour in seconds
#define PIR_TIMEOUT_DEFAULT             300   // 5 minutes in seconds

// -------------------------------------------------------------------------------
#define SPIN_UP_MIN                     0
#define SPIN_UP_SLOW                    0
#define SPIN_UP_MEDIUM                  1
#define SPIN_UP_FAST                    2
#define SPIN_UP_MAX                     2
#define SPIN_UP_DEFAULT                 1

// -------------------------------------------------------------------------------
#define USE_LDR_DEFAULT                 true

// -------------------------------------------------------------------------------
// RTC address
#define RTC_I2C_ADDRESS                 0x68

// -------------------------------------------------------------------------------
#define CONFIG_PORTAL_TIMEOUT           60

// -------------------------------------------------------------------------------
#define DISPLAY_ROTATE_MIN              0
#define DISPLAY_ROTATE_MAX              9

// -------------------------------------------------------------------------------
#define SYNC_HOURS 3
#define SYNC_MINS 4
#define SYNC_SECS 5
#define SYNC_DAY 2
#define SYNC_MONTH 1
#define SYNC_YEAR 0

#endif
