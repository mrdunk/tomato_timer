/*
   esp8266 Sonoff firmware.
   Update clock by NTP.
   Switch power on once par 24hours after dark.
   For watering plants.
*/

#include "wifi_credentials.h"

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <coredecls.h>                  // settimeofday_cb()
#include <Schedule.h>
#include <Ticker.h>

#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval

#include <sntp.h>                       // sntp_servermode_dhcp()

#include "data.h"
#include "converter.h"


#define ENABLE_RELAY
#define WAIT_CLOCK_SYNC

#define BUTTON 0
#define RELAY 12
#define LED 13

#define WATER_FOR_SECONDS 20  // Seconds
#define WATER_AT 22 // Hour value in time.
#define MAX_SECONDS_BETWEEN_WATERING 21600  // 6 hours

#include <TZ.h>
#define MYTZ TZ_Europe_Dublin


ESP8266WebServer server(80);

static time_t now;
static uint8_t events_synced = 0;
static uint8_t debounce_button = 0;
static uint16_t num_unrecorded_events = 0;

#ifdef WAIT_CLOCK_SYNC
static uint8_t clock_synced = 0;
#else
static uint8_t clock_synced = 1;
#endif

Ticker everyMinute;
Ticker everySecond;
Ticker flashLed;

volatile byte ledState = LOW;
volatile byte relayState = LOW;
uint16_t active_timer = 0;



// OPTIONAL: change SNTP startup delay
// a weak function is already defined and returns 0 (RFC violation)
// it can be redefined:
uint32_t sntp_startup_delay_MS_rfc_not_less_than_60000 ()
{
  //info_sntp_startup_delay_MS_rfc_not_less_than_60000_has_been_called = true;
  return 60000; // 60s (or lwIP's original default: (random() % 5000))
}

// OPTIONAL: change SNTP update delay
// a weak function is already defined and returns 1 hour
// it can be redefined:
//uint32_t sntp_update_delay_MS_rfc_not_less_than_15000 ()
//{
//    //info_sntp_update_delay_MS_rfc_not_less_than_15000_has_been_called = true;
//    return 15000; // 15s
//}

#define PTM(w) \
  Serial.print(" " #w "="); \
  Serial.print(tm->tm_##w);

void printTm(const char* what, const tm* tm) {
  Serial.print(what);
  PTM(isdst); PTM(yday); PTM(wday);
  PTM(year);  PTM(mon);  PTM(mday);
  PTM(hour);  PTM(min);  PTM(sec);
}

void showTime() {
  Serial.println();
  printTm("localtime:", localtime(&now));
  Serial.println();
  printTm("gmtime:   ", gmtime(&now));
  Serial.println();

  // timezone and demo in the future
  Serial.printf("timezone:  %s\n", getenv("TZ") ? : "(none)");

  // human readable
  Serial.print("ctime:     ");
  Serial.print(ctime(&now));

  // lwIP v2 is able to list more details about the currently configured SNTP servers
  for (int i = 0; i < SNTP_MAX_SERVERS; i++) {
    IPAddress sntp = *sntp_getserver(i);
    const char* name = sntp_getservername(i);
    if (sntp.isSet()) {
      Serial.printf("sntp%d:     ", i);
      if (name) {
        Serial.printf("%s (%s) ", name, sntp.toString().c_str());
      } else {
        Serial.printf("%s ", sntp.toString().c_str());
      }
      Serial.printf("- IPv6: %s - Reachability: %o\n",
                    sntp.isV6() ? "Yes" : "No",
                    sntp_getreachability(i));
    }
  }

  Serial.println();
}

void time_is_set(bool from_sntp /* <= this parameter is optional */) {
  now = time(nullptr);

  Serial.println("time_is_set");

  // in CONT stack, unlike ISRs,
  // any function is allowed in this callback

  Serial.print("settimeofday(");
  if (from_sntp) {
    Serial.print("SNTP");
    if (! clock_synced) {
      clock_synced = 1;
      firstBoot();
    }
  } else {
    Serial.print("USER");
  }
  Serial.print(")");

  showTime();
}

void IRAM_ATTR handleButtonPress() {
  if (debounce_button > 0) {
    return;
  }
  debounce_button = 2;
  Serial.println("Button press");
  startWatering(now, WATER_FOR_SECONDS, hw_button, F(""));
  eepromDisplay();
}

void flipLed() {
  ledState = !ledState;
  digitalWrite(LED, ledState);
}

void setup() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);

  Serial.begin(115200);
  Serial.println("\nStarting in 2secs...\n");
  delay(2000);

  // install callback - called when settimeofday is called (by SNTP or user)
  // once enabled (by DHCP), SNTP is updated every hour by default
  // ** optional boolean in callback function is true when triggered by SNTP **
  settimeofday_cb(time_is_set);

  // NTP time updates
  configTime(MYTZ, "pool.ntp.org");

  // OPTIONAL: disable obtaining SNTP servers from DHCP
  //sntp_servermode_dhcp(0); // 0: disable obtaining SNTP servers from DHCP (enabled by default)

  // Give now a chance to the settimeofday callback,
  // because it is *always* deferred to the next yield()/loop()-call.
  yield();

  // start network
  WiFi.mode(WIFI_STA);
  WiFi.begin(STASSID, STAPSK);

  // don't wait for network, observe time changing
  // when NTP timestamp is received
  showTime();

  server.on("/", httpHandleRoot);
  server.on("/water", httpHandleWater);
  server.on("/new_scheduled_event", httpUpdateScheduler);
  server.on("/del_scheduled_event", httpUpdateScheduler);
  server.on("/new_future_event", httpUpdateScheduler);
  server.on("/del_future_event", httpUpdateScheduler);
  server.onNotFound(handleNotFound);
  server.begin(); //Start the server
  Serial.println("http server listening");

  pinMode(BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON), handleButtonPress, FALLING);

  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
#ifdef ENABLE_RELAY
  pinMode(RELAY, OUTPUT);
  digitalWrite(RELAY, LOW);
#endif

  pre_populate_events();

  everyMinute.attach(60, newMinute);
  everySecond.attach(1, newSecond);
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void httpHandleRoot() {
  Serial.println("httpHandleRoot");

  String htmlPage;
  htmlPage.reserve(4096);               // prevent ram fragmentation

  htmlPage = F("<style> table, th, td { border: 1px solid black; border-collapse: collapse; padding-left: 5px; padding-right: 5px;} </style>");

  htmlPage +=  F("<table>");
  htmlPage +=   F("<tr>");
  htmlPage +=     F("<td>");
  htmlPage +=       F("Current time:");
  htmlPage +=     F("</td>");
  htmlPage +=     F("<td>");
  if(clock_synced) {
    htmlPage +=       ctime(&now);
  } else {
    htmlPage +=       F("waiting to sync.");
  }
  htmlPage +=     F("</td>");
  htmlPage +=   F("</tr>");
  htmlPage += F("</table>");
  htmlPage += F("<br/>");

  if(! clock_synced) {
    server.send(200, F("text/html"), htmlPage);
    return;
  }

  htmlPage += F("<a href='water'>Water now</a>");
  htmlPage += F("<br/>");

  htmlPage += F("<form method='get' action='/new_scheduled_event'>");
  htmlPage += F("<table>");
  htmlPage +=   F("<thead>");
  htmlPage +=     F("<tr>");
  htmlPage +=       F("<th colspan='5'>Schedule daily.</th>");
  htmlPage +=     F("</tr>");
  htmlPage +=   F("</thead>");

  uint16_t space = MAX_SCHEDULED_EVENTS;
  for (uint16_t i = 0; i < MAX_SCHEDULED_EVENTS; i++) {
    const ScheduledPowerEvent scheduled_event = get_scheduled_event(i);

    if (scheduled_event.duration != 0) {
      htmlPage +=   F("<tr>");
      htmlPage +=     F("<td>");
      htmlPage +=       i;
      htmlPage +=     F("</td>");
      htmlPage +=     F("<td>");
      htmlPage +=       timeOffsetToString(scheduled_event.when);
      htmlPage +=     F("</td>");
      htmlPage +=     F("<td>");
      htmlPage +=       scheduled_event.duration;
      htmlPage +=     F("</td>");
      htmlPage +=     F("<td>");
      htmlPage +=       scheduled_event.label;
      htmlPage +=     F("</td>");
      htmlPage +=     F("<td>");
      htmlPage +=       F("<a href='/del_scheduled_event?index=");
      htmlPage +=       i;
      htmlPage +=       F("'>delete</a>");
      htmlPage +=     F("</td>");
      htmlPage +=   F("</tr>");
    } else if (space > i) {
      space = i;
    }
  }

  if ( space < MAX_SCHEDULED_EVENTS) {
    htmlPage +=   F("<tr>");
    htmlPage +=     F("<td>");
    htmlPage +=     F("New entry:");
    htmlPage +=     F("</td>");
    htmlPage +=     F("<td>");
    htmlPage +=       F("<input type='time' name='when' required>");
    htmlPage +=     F("</td>");
    htmlPage +=     F("<td>");
    htmlPage +=       F("<input type='number' name='duration' min='1' placeholder='duration in seconds' required>");
    htmlPage +=     F("</td>");
    htmlPage +=     F("<td>");
    htmlPage +=       F("<input type='text' name='label' placeholder='optional label'>");
    htmlPage +=     F("</td>");
    htmlPage +=     F("<td>");
    htmlPage +=     F("<input type='submit' value='add'>");
    htmlPage +=     F("</td>");
    htmlPage +=   F("</tr>");
  }

  htmlPage += F("</table>");
  htmlPage += F("</form>");

  htmlPage += F("<form method='get' action='/new_future_event'>");
  htmlPage += F("<table>");
  htmlPage +=   F("<thead>");
  htmlPage +=     F("<tr>");
  htmlPage +=       F("<th colspan='6'>Pending events</th>");
  htmlPage +=     F("</tr>");
  htmlPage +=   F("</thead>");

  space = MAX_FUTURE_EVENTS;
  for (uint16_t i = 0; i < MAX_FUTURE_EVENTS; i++) {
    const FuturePowerEvent future_event = get_future_event(i);

    if (future_event.when != 0) {
      htmlPage +=   F("<tr>");
      htmlPage +=     F("<td>");
      htmlPage +=       i;
      htmlPage +=     F("</td>");
      htmlPage +=     F("<td colspan='2'>");
      htmlPage +=       ctime(&future_event.when);
      htmlPage +=     F("</td>");
      htmlPage +=     F("<td>");
      htmlPage +=       future_event.duration;
      htmlPage +=     F("</td>");
      htmlPage +=     F("<td>");
      htmlPage +=       future_event.label;
      htmlPage +=     F("</td>");
      htmlPage +=     F("<td>");
      htmlPage +=       eventReasonToString(future_event.why);
      htmlPage +=     F("</td>");
      htmlPage +=     F("<td>");
      htmlPage +=       F("<a href='/del_future_event?index=");
      htmlPage +=       i;
      htmlPage +=       F("'>delete</a>");
      htmlPage +=     F("</td>");
      htmlPage +=   F("</tr>");
    } else if (space > i) {
      space = i;
    }
  }

  if ( space < MAX_FUTURE_EVENTS) {
    htmlPage +=   F("<tr>");
    htmlPage +=     F("<td>");
    htmlPage +=       F("New entry:");
    htmlPage +=     F("</td>");
    htmlPage +=     F("<td>");
    htmlPage +=       F("<input type='number' name='days_future' min='0' default='0' placeholder='days in future'>");
    htmlPage +=     F("</td>");
    htmlPage +=     F("<td>");
    htmlPage +=       F("<input type='time' name='when' required>");
    htmlPage +=     F("</td>");
    htmlPage +=     F("<td>");
    htmlPage +=       F("<input type='number' name='duration' min='1' placeholder='duration in seconds' required>");
    htmlPage +=     F("</td>");
    htmlPage +=     F("<td>");
    htmlPage +=       F("<input type='text' name='label' placeholder='optional label'>");
    htmlPage +=     F("</td>");
    htmlPage +=     F("<td>");
    htmlPage +=     F("</td>");
    htmlPage +=     F("<td>");
    htmlPage +=       F("<input type='hidden' name='why' value='scheduled_once'>");
    htmlPage +=       F("<input type='submit' value='add'>");
    htmlPage +=     F("</td>");
    htmlPage +=   F("</tr>");
  }

  htmlPage += F("</table>");
  htmlPage += F("</form>");

  htmlPage += F("<table>");
  htmlPage +=   F("<thead>");
  htmlPage +=     F("<tr>");
  htmlPage +=       F("<th colspan='8'>History.</th>");
  htmlPage +=     F("</tr>");
  htmlPage +=   F("</thead>");

  time_t prev_time = std::numeric_limits<time_t>::max();
  uint16_t oldest_index = oldest_past_event_index();
  String wrong_order = "";
  for (int16_t i = MAX_PAST_EVENTS - 1; i >= 0; i--) {
    uint16_t index = (i + (uint16_t)MAX_PAST_EVENTS + oldest_index) % (uint16_t)MAX_PAST_EVENTS;
    const PastPowerEvent past_event = get_past_event(index);    
    if (past_event.when != 0) {
      wrong_order = "";
      if(past_event.when > prev_time) {
        wrong_order = "Error: wrong order";
      }
      htmlPage +=   F("<tr>");
      htmlPage +=     F("<td>");
      htmlPage +=       i;
      htmlPage +=     F("</td>");
      htmlPage +=     F("<td>");
      htmlPage +=       index;
      htmlPage +=     F("</td>");
      htmlPage +=     F("<td>");
      htmlPage +=       ctime(&past_event.when);
      htmlPage +=     F("</td>");
      htmlPage +=     F("<td>");
      htmlPage +=       ctime(&past_event.sheduled);
      htmlPage +=     F("</td>");
      htmlPage +=     F("<td>");
      htmlPage +=       past_event.duration;
      htmlPage +=     F("</td>");
      htmlPage +=     F("<td>");
      htmlPage +=       eventReasonToString(past_event.why);
      htmlPage +=     F("</td>");
      htmlPage +=     F("<td>");
      htmlPage +=       past_event.label;
      htmlPage +=     F("</td>");
      htmlPage +=     F("<td>");
      htmlPage +=       wrong_order;
      htmlPage +=     F("</td>");
      htmlPage +=   F("</tr>");
      
      prev_time = past_event.when;
    }
  }

  htmlPage += F("</table>");

  Serial.print(F("Root page length: "));
  Serial.println(htmlPage.length());

  server.send(200, F("text/html"), htmlPage);
}

void httpHandleWater() {
  startWatering(now, WATER_FOR_SECONDS, web_button, F(""));
  server.sendHeader("Location", String("/"), true);
  server.send ( 302, "text/plain", "");
}

void httpUpdateScheduler() {
  Serial.println("httpUpdateScheduler");
  Serial.println(server.uri());

  uint8_t index = MAX_SCHEDULED_EVENTS;
  time_t when = 0;
  PowerEventReason why = unknown;
  uint16_t duration = 0;
  char label[LABEL_LEN + 1] = "";
  uint16_t days_future = 0;

  for (uint8_t i = 0; i < server.args(); i++) {
    String arg = server.arg(i);
    arg.trim();

    if (server.argName(i) == "when") {
      Serial.println("when: " + arg);
      uint8_t hour = arg.toInt();
      arg.remove(0, 3);
      uint8_t minute = arg.toInt();
      when = hour * 60 * 60 + minute * 60;
      Serial.printf("%i   %i\n", hour, minute);
    } else if (server.argName(i) == "why") {
      Serial.println("why: " + arg);
      why = stringToEventReason(arg);
    } else if (server.argName(i) == "duration") {
      Serial.println("duration: " + arg);
      duration = arg.toInt();
    } else if (server.argName(i) == "label") {
      Serial.printf("label: %s\n", arg.c_str());
      strncpy(label, arg.c_str(), arg.length() + 1);
    } else if (server.argName(i) == "index") {
      Serial.printf("index: %s\n", arg.c_str());
      index = arg.toInt();
    } else if (server.argName(i) == "days_future") {
      Serial.printf("days_future: %s\n", arg.c_str());
      days_future = arg.toInt();
    }
  }

  if (server.uri().indexOf("new_scheduled_event") > 0) {
    Serial.println("Adding event");
    set_scheduled_event(when, duration, label);
    events_synced = 0;
  } else if (server.uri().indexOf("del_scheduled_event") > 0) {
    Serial.printf("Deleting event at index %i\n", index);
    clear_scheduled_event(index);
  } else if (server.uri().indexOf("new_future_event") > 0) {
    Serial.println("Adding future event");
    when += midnightInDaysTime(now, days_future);
    set_future_event(when, why, duration, label);
  } else if (server.uri().indexOf("del_future_event") > 0) {
    Serial.printf("Deleting future event at index %i\n", index);
    clear_future_event(index);
  } else {
    Serial.printf("  when: %llu  why: %i  duration: %u  label: %s\n", when, (int)why, duration, label);
  }

  server.sendHeader("Location", String("/"), true);
  server.send ( 302, "text/plain", "");
}

void newMinute() {
  //showTime();

  Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
  Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
  if (WiFi.isConnected()) {
    Serial.print("Local IP: ");
    Serial.println(WiFi.localIP());
  }

  if (clock_synced) {
    FuturePowerEvent* power_event = getActiveEvent(now);
    while(power_event) {
      Serial.printf("Active event: %s\t%s\t%u\t%s\n",
                     ctime(&(power_event->when)),
                     eventReasonToString(power_event->why).c_str(),
                     power_event->duration,
                     power_event->label);
      startWatering(power_event->when, power_event->duration, power_event->why, power_event->label);
                     
      power_event = getActiveEvent(now);
    }
  }

  events_synced = 0;
}

void firstBoot() {
    Serial.println("First boot");
    digitalWrite(LED, LOW);  
    append_past_event(now, now, reboot, 0, "");
    if (num_unrecorded_events > 0) {
      String unrecorded_description = String(F("Unrecorded events before clock sync: ")) + String(num_unrecorded_events);
      append_past_event(now, now, unrecorded_events, 0, unrecorded_description.c_str());
    }
}

void newSecond() {
  now = time(nullptr);

  if (! clock_synced) {
    flipLed();
  }

  if (debounce_button > 0) {
    debounce_button--;
  }

  if (active_timer > 0) {
    active_timer--;
    if (active_timer == 0) {
      stopWatering();
    }
  }

  if (clock_synced) {
    if( ! events_synced) {
      clear_expired_future_events(now);
      scheduledToFutureEvents(now);
      events_synced = 1;
    }
  }
}

void startWatering(const time_t & when, const uint16_t duration, const PowerEventReason reason, const String & label) {
  Serial.print(F("startWatering("));
  Serial.print(reason);
  Serial.println(F(")"));

  if(clock_synced) {
    append_past_event(now, when, reason, duration, label.c_str());
  } else {
    num_unrecorded_events ++;
  }
  
  active_timer = duration;
  flashLed.attach_ms(100, flipLed);
#ifdef ENABLE_RELAY
  digitalWrite(RELAY, HIGH);
#endif
}

void stopWatering() {
  Serial.println(F("stopWatering"));
  flashLed.detach();
  digitalWrite(LED, LOW);
#ifdef ENABLE_RELAY
  digitalWrite(RELAY, LOW);
#endif
}

void loop() {
  server.handleClient(); // Handling incoming http requests
}
