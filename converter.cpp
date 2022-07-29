#include <Arduino.h>
#include "data.h"

PowerEventReason stringToEventReason(const String & why) {
  if (why == "reboot") {
    return reboot;
  }
  if (why == "unrecorded_events") {
    return unrecorded_events;
  }
  if (why == "hw_button") {
    return hw_button;
  }
  if (why == "web_button") {
    return web_button;
  }
  if (why == "scheduled_daily") {
    return scheduled_daily;
  }
  if (why == "scheduled_once") {
    return scheduled_once;
  }

  return unknown;
}

String eventReasonToString(const PowerEventReason & reason) {
  switch (reason) {
    case reboot:
      return "reboot";
    case unrecorded_events:
      return "unrecorded_events";
    case hw_button:
      return "hw_button";
    case web_button:
      return "web_button";
    case scheduled_daily:
      return "scheduled_daily";
    case scheduled_once:
      return "scheduled_once";
    default:
      return "unknown";
  }
  return "unknown";
}

String timeOffsetToString(const time_t when) {
  uint8_t hours = when / 60 / 60;
  uint8_t mins = (when / 60) % 60;
  
  String when_str = "";
  if (hours < 10) {
    when_str = "0";      
  }
  when_str += String(hours) + ":";
  if (mins < 10) {
    when_str += "0";
  }
  when_str += String(mins);

  return when_str;
}

time_t midnightInDaysTime(const time_t & now, const uint16_t & days_future) { 
  tm *date = localtime(&now);
  date->tm_mday += days_future;
  date->tm_hour = 0;
  date->tm_min = 0;
  date->tm_sec = 0;
  return mktime(date);
}
