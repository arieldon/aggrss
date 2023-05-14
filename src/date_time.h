#ifndef DATE_TIME_H
#define DATE_TIME_H

#include "base.h"

typedef struct Date_Time Date_Time;
struct Date_Time
{
	i32 seconds;    /* Seconds (0-60) */
	i32 minutes;    /* Minutes (0-59) */
	i32 hours;   /* Hours (0-23) */
	i32 month_day;   /* Day of the month (1-31) */
	i32 month;    /* Month (0-11) */
	i32 year;   /* Year - 1900 */
	i32 week_day;   /* Day of the week (0-6, Sunday = 0) */
};

typedef struct Timestamp Timestamp;
struct Timestamp
{
	Date_Time date_time_format;
	u32 unix_format;
};

// TODO(ariel) Include examples because these strings are incomprehensible,
// even if standard.
// NOTE(ariel) Parse date time string in any of these formats:
// "%a, %d %b %Y %H:%M:%S %Z"
// "%Y-%m-%dT%H:%M:%S%z" ()
Date_Time parse_date_time(String date_time);

#endif
