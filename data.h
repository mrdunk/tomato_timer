#ifndef TIMER__DATA_h
#define TIMER__DATA_h

#include <time.h>
#include <EEPROM.h>


#define EEPROM_DATA_VER 2
#define MAX_SCHEDULED_EVENTS 8
#define MAX_FUTURE_EVENTS 16
#define MAX_PAST_EVENTS 32
#define LABEL_LEN 64

#define EVENT_SYNC_WINDOW 60
#define EVENT_DELETE_DELAY 120


enum PowerEventReason {
  unknown,
  reboot,
  unrecorded_events,
  hw_button,
  web_button,
  scheduled_daily,
  scheduled_once
};

struct ScheduledPowerEvent {
  time_t when;
  uint16_t duration;
  char label[LABEL_LEN + 1];
};

struct FuturePowerEvent {
  time_t when;
  PowerEventReason why;
  uint16_t duration;
  char label[LABEL_LEN + 1];
};

struct PastPowerEvent {
  time_t when;
  time_t sheduled;
  PowerEventReason why;
  uint16_t duration;
  char label[LABEL_LEN + 1];
};


void clear_scheduled_event(uint16_t index);
void set_scheduled_event(time_t when, uint16_t duration, const char* label);
const ScheduledPowerEvent & get_scheduled_event(uint16_t index);

void clear_future_event(uint16_t index);
void set_future_event(time_t when, PowerEventReason why, uint16_t duration, const char* label);
const FuturePowerEvent & get_future_event(uint16_t index);
void clear_expired_future_events(const time_t & now);

void clear_past_event(uint16_t index);
void set_past_event(time_t when, PowerEventReason why, uint16_t duration, const char* label);
const PastPowerEvent & get_past_event(uint16_t index);
void append_past_event(const time_t & now, const time_t & when, const PowerEventReason & why, const uint16_t & duration, const char* label);
uint32_t oldest_past_event_index();

void pre_populate_events();

void eepromDisplay();

void scheduledToFutureEvents(time_t & now);

FuturePowerEvent * getActiveEvent(time_t & now);

#endif  // TIMER__DATA_h
