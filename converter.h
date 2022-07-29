#ifndef TIMER__CONVERTER_h
#define TIMER__CONVERTER_h

#include "data.h"

PowerEventReason stringToEventReason(const String & why);
String eventReasonToString(const PowerEventReason & reason);

String timeOffsetToString(const time_t when);

time_t midnightInDaysTime(const time_t & now, const uint16_t & days_future);

#endif  // TIMER__CONVERTER_h
