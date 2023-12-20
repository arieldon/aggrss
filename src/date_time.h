#ifndef DATE_TIME_H
#define DATE_TIME_H

typedef struct Expanded_Date_Time Expanded_Date_Time;
struct Expanded_Date_Time
{
	s32 day;
	s32 month;
	s32 year;
	s32 seconds;
	s32 minutes;
	s32 hours;
};

typedef struct Timestamp Timestamp;
struct Timestamp
{
	string error;
	Expanded_Date_Time expanded_format;
	u64 unix_format;
};

// NOTE(ariel) Parse date time strings in RFC 822 or 3339 formats:
// "%a, %d %b %Y %H:%M:%S %Z" <--- EXAMPLE --- "Sun, 14 May 2023 19:32:11 GMT"
//      "%Y-%m-%dT%H:%M:%S%z" <--- EXAMPLE --- "2023-14-05T19:32:11Z"
static Timestamp parse_date_time(string date_time);

#endif
