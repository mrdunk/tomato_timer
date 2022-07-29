#include <Arduino.h>
#include "data.h"
#include "converter.h"

ScheduledPowerEvent sched_power_events[MAX_SCHEDULED_EVENTS];
FuturePowerEvent future_power_events[MAX_FUTURE_EVENTS];
PastPowerEvent past_power_events[MAX_PAST_EVENTS];

static const uint32_t EPPROM_ALLOC = (5 * sizeof(uint16_t) +
                                      MAX_SCHEDULED_EVENTS * sizeof(ScheduledPowerEvent) +
                                      MAX_FUTURE_EVENTS * sizeof(FuturePowerEvent) +
                                      MAX_PAST_EVENTS * sizeof(PastPowerEvent));


void clear_arrays() {
  memset(sched_power_events, 0, sizeof(ScheduledPowerEvent) * MAX_SCHEDULED_EVENTS);
  memset(future_power_events, 0, sizeof(FuturePowerEvent) * MAX_FUTURE_EVENTS);
  memset(past_power_events, 0, sizeof(PastPowerEvent) * MAX_PAST_EVENTS);
}

void clear_eeprom() {
  Serial.println("Clearing EEPROM. Setting checksums.");
  
  uint32_t address = 0;
  uint16_t val = 0;
  
  val = EEPROM_DATA_VER;
  EEPROM.put(address, val);
  address += sizeof(uint16_t);

  val = MAX_SCHEDULED_EVENTS;
  EEPROM.put(address, val);
  address += sizeof(uint16_t);

  val = MAX_FUTURE_EVENTS;
  EEPROM.put(address, val);
  address += sizeof(uint16_t);

  val = MAX_PAST_EVENTS;
  EEPROM.put(address, val);
  address += sizeof(uint16_t);

  val = LABEL_LEN;
  EEPROM.put(address, val);
  address += sizeof(uint16_t);

  for(uint16_t i = 0; i < MAX_SCHEDULED_EVENTS; i++) {
    clear_scheduled_event(i);
  }

  for(uint16_t i = 0; i < MAX_FUTURE_EVENTS; i++) {
    clear_future_event(i);
  }

  for(uint16_t i = 0; i < MAX_PAST_EVENTS; i++) {
    clear_past_event(i);
  }

  EEPROM.commit();
}

uint32_t check_eeprom_checksums() {
  uint32_t address = 0;
  
  uint16_t eeprom_data_ver = 0;
  EEPROM.get(address, eeprom_data_ver);
  if (eeprom_data_ver != EEPROM_DATA_VER) {
    Serial.printf("EEPROM import versions do not match. Saved: %i  Expected: %i\n",
                  eeprom_data_ver, EEPROM_DATA_VER);
    return 0;
  }
  address += sizeof(uint16_t);
  
  uint16_t max_scheduled_events = 0;
  EEPROM.get(address, max_scheduled_events);
  if (max_scheduled_events != MAX_SCHEDULED_EVENTS) {
    Serial.printf("MAX_SCHEDULED_EVENTS value has changed. Saved: %i  Expected: %i\n",
                  max_scheduled_events, MAX_SCHEDULED_EVENTS);
    return 0;
  }
  address += sizeof(uint16_t);
  
  uint16_t max_future_events = 0;
  EEPROM.get(address, max_future_events);
  if (max_future_events != MAX_FUTURE_EVENTS) {
    Serial.printf("MAX_FUTURE_EVENTS value has changed. Saved: %i  Expected: %i\n",
                  max_future_events, MAX_FUTURE_EVENTS);
    return 0;
  }
  address += sizeof(uint16_t);
  
  uint16_t max_past_events = 0;
  EEPROM.get(address, max_past_events);
  if (max_past_events != MAX_PAST_EVENTS) {
    Serial.printf("MAX_PAST_EVENTS value has changed. Saved: %i  Expected: %i\n",
                  max_past_events, MAX_PAST_EVENTS);
    return 0;
  }
  address += sizeof(uint16_t);
  
  uint16_t label_len = 0;
  EEPROM.get(address, label_len);
  if (label_len != LABEL_LEN) {
    Serial.printf("LABEL_LEN value has changed. Saved: %i  Expected: %i\n",
                  label_len, LABEL_LEN);
    return 0;
  }
  address += sizeof(uint16_t);

  Serial.println("EEPROM checksums look good.");
  return address;
}

void pre_populate_events() {
  Serial.println("pre_populate_events");
  EEPROM.begin(EPPROM_ALLOC);
  
  clear_arrays();

  uint32_t address = check_eeprom_checksums();
  if (address == 0) {
    clear_eeprom();
    return;
  }

  for(uint16_t i = 0; i < MAX_SCHEDULED_EVENTS; i++) {
    EEPROM.get(address, sched_power_events[i]);
    address += sizeof(ScheduledPowerEvent);
  }

  for(uint16_t i = 0; i < MAX_FUTURE_EVENTS; i++) {
    EEPROM.get(address, future_power_events[i]);
    address += sizeof(FuturePowerEvent);
  }

  for(uint16_t i = 0; i < MAX_PAST_EVENTS; i++) {
    EEPROM.get(address, past_power_events[i]);
    address += sizeof(PastPowerEvent);
  }

  if (address > EPPROM_ALLOC) {
    Serial.println("ERROR: Exceeded EEPROM allocation.");
  }
}

uint8_t check_dupe_scheduled_event(time_t when, uint16_t duration, const char* label) {
  uint16_t i = 0;
  for(i = 0; i < MAX_SCHEDULED_EVENTS; i++) {
    if (sched_power_events[i].when == when &&
        sched_power_events[i].duration == duration &&
        strncmp(sched_power_events[i].label, label, LABEL_LEN) == 0) {
      Serial.println("Duplicate entry");
      return 1;
    }
  }
  return 0;
}

void set_scheduled_event(time_t when, uint16_t duration, const char* label) {
  Serial.printf("set_scheduled_event(%llu, %u, %s)\n", when, duration, label);
  if(check_dupe_scheduled_event(when, duration, label)) {
    return;
  }
  
  uint16_t i = 0;
  for(i = 0; i < MAX_SCHEDULED_EVENTS; i++) {
    if (sched_power_events[i].duration == 0) {
      break;
    }
  }
  
  if (i >= MAX_SCHEDULED_EVENTS) {
    Serial.println("No free space for new ScheduledPowerEvent.");
  }
 
  sched_power_events[i].when = when;
  sched_power_events[i].duration = duration;
  strncpy(sched_power_events[i].label, label, LABEL_LEN);

  uint32_t address = 5 * sizeof(uint16_t) + i * sizeof(ScheduledPowerEvent);
  EEPROM.put(address, sched_power_events[i]);
  EEPROM.commit();
}

void clear_scheduled_event(uint16_t index) {
  uint32_t address = 5 * sizeof(uint16_t) + index * sizeof(ScheduledPowerEvent);
  ScheduledPowerEvent empty = {};
  EEPROM.put(address, empty);

  sched_power_events[index] = {};
  EEPROM.commit();
}

const ScheduledPowerEvent & get_scheduled_event(uint16_t index) {
  return sched_power_events[index];
}

uint8_t check_dupe_future_event(time_t when, uint16_t duration, const char* label) {
  uint16_t i = 0;
  for(i = 0; i < MAX_SCHEDULED_EVENTS; i++) {
    if (future_power_events[i].when == when &&
        future_power_events[i].duration == duration &&
        strncmp(future_power_events[i].label, label, LABEL_LEN) == 0) {
      Serial.println("Duplicate entry");
      return 1;
    }
  }
  return 0;
}

void set_future_event(time_t when, PowerEventReason why, uint16_t duration, const char* label) {
  Serial.printf("set_future_event(%llu, %u, %s)\n", when, duration, label);
  if (check_dupe_future_event(when, duration, label)) {
    return;
  }
  
  uint16_t i = 0;
  for(i = 0; i < MAX_FUTURE_EVENTS; i++) {
    if (future_power_events[i].when == 0) {
      break;
    }
  }
  
  if (i >= MAX_FUTURE_EVENTS) {
    Serial.println("No free space for new Future Events.");
  }
  
  future_power_events[i].when = when;
  future_power_events[i].why = why;
  future_power_events[i].duration = duration;
  strncpy(future_power_events[i].label, label, LABEL_LEN);

  uint32_t address = (5 * sizeof(uint16_t) + 
                    MAX_SCHEDULED_EVENTS * sizeof(ScheduledPowerEvent) + 
                    i * sizeof(FuturePowerEvent));
  EEPROM.put(address, future_power_events[i]);
  EEPROM.commit();
}

void clear_future_event(uint16_t index) {
  uint32_t address = (5 * sizeof(uint16_t) + 
                    MAX_SCHEDULED_EVENTS * sizeof(ScheduledPowerEvent) + 
                    index * sizeof(FuturePowerEvent));
  FuturePowerEvent empty = {};
  EEPROM.put(address, empty);

  future_power_events[index] = {};
  EEPROM.commit();
}

void clear_expired_future_events(const time_t & now) {
  uint16_t i = 0;
  for(i = 0; i < MAX_FUTURE_EVENTS; i++) {
    if (future_power_events[i].when + EVENT_DELETE_DELAY <= now) {
      clear_future_event(i);
    }
  }
}

const FuturePowerEvent & get_future_event(uint16_t index) {
  return future_power_events[index];
}

void set_past_event(time_t when, PowerEventReason why, uint16_t duration, const char* label) {
  Serial.printf("set_past_event(%llu, %u, %s)\n", when, duration, label);
  uint16_t i = 0;
  for(i = 0; i < MAX_PAST_EVENTS; i++) {
    if (past_power_events[i].when == 0) {
      break;
    }
  }
  
  if (i >= MAX_PAST_EVENTS) {
    Serial.println("No free space for new Future Events.");
  }
 
  past_power_events[i].when = when;
  past_power_events[i].why = why;
  past_power_events[i].duration = duration;
  strncpy(past_power_events[i].label, label, LABEL_LEN);

  uint32_t address = (5 * sizeof(uint16_t) + 
                    MAX_SCHEDULED_EVENTS * sizeof(ScheduledPowerEvent) + 
                    MAX_FUTURE_EVENTS * sizeof(FuturePowerEvent) +
                    i * sizeof(PastPowerEvent));
  EEPROM.put(address, past_power_events[i]);
  EEPROM.commit();
}

void clear_past_event(uint16_t index) {
  uint32_t address = (5 * sizeof(uint16_t) + 
                    MAX_SCHEDULED_EVENTS * sizeof(ScheduledPowerEvent) + 
                    MAX_FUTURE_EVENTS * sizeof(FuturePowerEvent) +
                    index * sizeof(PastPowerEvent));
  PastPowerEvent empty = {};
  EEPROM.put(address, empty);

  past_power_events[index] = {};
  EEPROM.commit();
}

const PastPowerEvent & get_past_event(uint16_t index) {
  return past_power_events[index];
}

uint8_t check_dupe_past_event(time_t sheduled, PowerEventReason why, uint16_t duration, const char* label) {
  for(uint16_t i = 0; i < MAX_PAST_EVENTS; i++) {
    const PastPowerEvent & past_power_event = past_power_events[i];
    if (past_power_event.sheduled == sheduled &&
        past_power_event.why == why &&
        past_power_event.duration == duration &&
        strncmp(past_power_event.label, label, LABEL_LEN) == 0) {
      return 1;    
    }
  }
  return 0;
}

void append_past_event(const time_t & now, const time_t & when, const PowerEventReason & why, const uint16_t & duration, const char* label) {
  uint16_t oldest_index = MAX_PAST_EVENTS;
  time_t oldest_time = std::numeric_limits<time_t>::max();
  for(uint16_t i = 0; i < MAX_PAST_EVENTS; i++) {
    if (past_power_events[i].when == 0) {
      // Free space.
      oldest_index = i;
      break;
    }
    if (past_power_events[i].when < oldest_time) {
      oldest_index = i;
      oldest_time = past_power_events[i].when;
    }
  }  

  past_power_events[oldest_index].when = now;
  past_power_events[oldest_index].sheduled = when;
  past_power_events[oldest_index].why = why;
  past_power_events[oldest_index].duration = duration;
  strncpy(past_power_events[oldest_index].label, label, LABEL_LEN);

  uint32_t address = (5 * sizeof(uint16_t) + 
                      MAX_SCHEDULED_EVENTS * sizeof(ScheduledPowerEvent) + 
                      MAX_FUTURE_EVENTS * sizeof(FuturePowerEvent) +
                      oldest_index * sizeof(PastPowerEvent));
  EEPROM.put(address, past_power_events[oldest_index]);
  EEPROM.commit();
  Serial.printf("append_past_event:  index: %u  eeprom address: %u\n", oldest_index, address);
}

uint32_t oldest_past_event_index() {
  uint16_t oldest_index = MAX_PAST_EVENTS;
  time_t oldest_time = std::numeric_limits<time_t>::max();
  for(uint16_t i = 0; i < MAX_PAST_EVENTS; i++) {
    if (past_power_events[i].when < oldest_time) {
      oldest_index = i;
      oldest_time = past_power_events[i].when;
    }
  }  
  return oldest_index;  
}

void eepromDisplay() {
  
  uint32_t address = 0;
  uint16_t val = 0;

  Serial.printf("EEPROM_DATA_VER: %i\n", EEPROM.get(address, val));
  address += sizeof(uint16_t);
  
  Serial.printf("MAX_SCHEDULED_EVENTS: %i\n", EEPROM.get(address, val));
  address += sizeof(uint16_t);
  
  Serial.printf("MAX_FUTURE_EVENTS: %i\n", EEPROM.get(address, val));
  address += sizeof(uint16_t);
  
  Serial.printf("MAX_PAST_EVENTS: %i\n", EEPROM.get(address, val));
  address += sizeof(uint16_t);
  
  Serial.printf("LABEL_LEN: %i\n", EEPROM.get(address, val));
  address += sizeof(uint16_t);
  
  Serial.println("SCHEDULED_EVENTS");
  ScheduledPowerEvent shed_pow_event_eeprom;
  ScheduledPowerEvent shed_pow_event_ram;
  for(uint16_t i = 0; i < MAX_SCHEDULED_EVENTS; i++) {
    EEPROM.get(address, shed_pow_event_eeprom);
    shed_pow_event_ram = sched_power_events[i];
    if(shed_pow_event_ram.duration > 0) {
      Serial.printf("  %i : %u \t %s , %s \t %u , %i \t %s , %s\n",
                    i, address, 
                    timeOffsetToString(shed_pow_event_eeprom.when).c_str(),
                    timeOffsetToString(shed_pow_event_ram.when).c_str(),
                    shed_pow_event_eeprom.duration,
                    shed_pow_event_ram.duration,
                    shed_pow_event_eeprom.label,
                    shed_pow_event_ram.label
                    );
    } else {
      Serial.printf("  %i : %u\n", i, address);
    }
    address += sizeof(ScheduledPowerEvent);
  }

  Serial.println("FUTURE_EVENTS");
  FuturePowerEvent future_pow_event_eeprom;
  FuturePowerEvent future_pow_event_ram;
  for(uint16_t i = 0; i < MAX_FUTURE_EVENTS; i++) {
    EEPROM.get(address, future_pow_event_eeprom);
    future_pow_event_ram = future_power_events[i];
    if(future_pow_event_ram.when > 0) {
      Serial.printf("  %i : %u \t %s , %s \t %u : %i \t %s , %s\n",
                    i, address,
                    strtok(ctime(&future_pow_event_eeprom.when), "\n"),
                    strtok(ctime(&future_pow_event_ram.when), "\n"),
                    future_pow_event_eeprom.duration,
                    future_pow_event_ram.duration,
                    future_pow_event_eeprom.label,
                    future_pow_event_ram.label
                    );
    } else {
      Serial.printf("  %i : %u\n", i, address);
    }
    address += sizeof(FuturePowerEvent);
  }

  Serial.println("PAST_EVENTS");
  PastPowerEvent past_pow_event_eeprom;
  PastPowerEvent past_pow_event_ram;
  for(uint16_t i = 0; i < MAX_PAST_EVENTS; i++) {
    EEPROM.get(address, past_pow_event_eeprom);
    past_pow_event_ram = past_power_events[i];
    if(past_pow_event_ram.when > 0) {
      Serial.printf("  %i : %u \t %s , %s \t %s , %s \t %u : %i \t %s , %s \t %s , %s\n",
                    i, address,
                    strtok(ctime(&past_pow_event_eeprom.when), "\n"),
                    strtok(ctime(&past_pow_event_ram.when), "\n"),
                    strtok(ctime(&past_pow_event_eeprom.sheduled), "\n"),
                    strtok(ctime(&past_pow_event_ram.sheduled), "\n"),
                    past_pow_event_eeprom.duration,
                    past_pow_event_ram.duration,
                    eventReasonToString(past_pow_event_eeprom.why).c_str(),
                    eventReasonToString(past_pow_event_ram.why).c_str(),
                    past_pow_event_eeprom.label,
                    past_pow_event_ram.label
                    );
    } else {
      Serial.printf("  %i : %u\n", i, address);
    }
    address += sizeof(PastPowerEvent);
  }
}

/* Copy scheduled events to future events.
 * This should be run at the start of each day.
 */
void scheduledToFutureEvents(time_t & now) {
  uint16_t i = 0;
  for(i = 0; i < MAX_SCHEDULED_EVENTS; i++) {
    if (sched_power_events[i].duration > 0 ) {
      time_t event_time = midnightInDaysTime(now, 0) + sched_power_events[i].when;
      if (event_time + EVENT_SYNC_WINDOW >= now) {
        set_future_event(event_time, scheduled_daily, sched_power_events[i].duration, sched_power_events[i].label);
      }
    }
  }
}

FuturePowerEvent * getActiveEvent(time_t & now) {
  uint16_t i = 0;
  for(i = 0; i < MAX_FUTURE_EVENTS; i++) {
    if (future_power_events[i].duration > 0 && future_power_events[i].when <= now) {
      if(! check_dupe_past_event(future_power_events[i].when,
                                 future_power_events[i].why,
                                 future_power_events[i].duration, 
                                 future_power_events[i].label))
      {
        return &(future_power_events[i]);
      } else if (future_power_events[i].when + EVENT_DELETE_DELAY <= now) {
        // Need EVENT_DELETE_DELAY to stop jitter between daily events getting added and this one getting deleted.
        clear_future_event(i);
      }
    }
  }
  return nullptr;
}

