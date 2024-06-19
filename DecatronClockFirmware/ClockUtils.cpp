#include "ClockUtils.h"

boolean useDebug = false;

String formatIPAsString(IPAddress ip) {
  return String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
}

int getIntValue(String data, char separator, int index) {
  String result = getValue(data, separator, index);
  return atoi(result.c_str());
}

/**
   Split a string based on a separator, get the element given by index
*/
String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;
  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}


String secsToReadableString (long secsValue) {
  long upDays = secsValue / 86400;
  long upHours = (secsValue - (upDays * 86400)) / 3600;
  long upMins = (secsValue - (upDays * 86400) - (upHours * 3600)) / 60;
  secsValue = secsValue - (upDays * 86400) - (upHours * 3600) - (upMins * 60);
  String uptimeString = "";
  if (upDays > 0) {
    uptimeString += upDays; 
    uptimeString += " d ";
  }
  if (upHours > 0) {
    uptimeString += upHours;
    uptimeString += " h "; 
  }
  if (upMins > 0) {
    uptimeString += upMins; 
    uptimeString += " m ";
  }
  uptimeString += secsValue; 
  uptimeString += " s";

  return uptimeString;
}

String timeToReadableString(int y, int m, int d, int h, int mi, int s) {
  char buf1[20];
  sprintf(buf1, "%04d:%02d:%02d %02d:%02d:%02d", y, m, d, h, mi, s);
  return String(buf1);
}

String timeStringToReadableString(String timeString){
  char* ptr = strtok((char *)timeString.c_str(), ",");
  int y = atoi(ptr);
  ptr = strtok(NULL, ",");
  int m = atoi(ptr);
  ptr = strtok(NULL, ",");
  int d = atoi(ptr);
  ptr = strtok(NULL, ",");
  int h = atoi(ptr);
  ptr = strtok(NULL, ",");
  int mi = atoi(ptr);
  ptr = strtok(NULL, ",");
  int s = atoi(ptr);
  return timeToReadableString(y,m,d,h,mi,s);
}

void grabInts(String s, int *dest, String sep) {
  int end = 0;
  for (int start = 0; end != -1; start = end + 1) {
    end = s.indexOf(sep, start);
    if (end > 0) {
      *dest++ = s.substring(start, end).toInt();
    } else {
      *dest++ = s.substring(start).toInt();
    }
  }
}

unsigned char hex2bcd (unsigned char x) {
  unsigned char y;
  y = (x / 10) << 4;
  y = y | (x % 10);
  return (y);
}

// ----------------------------------------------------------------------------------------------------
// ------------------------------------------ Debug functions -----------------------------------------
// ----------------------------------------------------------------------------------------------------

void debugMsg(String message) {
  if (useDebug) {
    Serial.println(message);
    Serial.flush();
  }
}

void hexCharacterStringToBytes(byte *byteArray, const char *hexString) {
  byte byteIndex = 0;

  for (byte charIndex = 0; charIndex < strlen(hexString); charIndex++)
  {
    byteArray[byteIndex++] = nibble(hexString[charIndex]);
  }
}

byte nibble(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';

  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;

  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;

  return 0;  // Not a valid hexadecimal character
}
