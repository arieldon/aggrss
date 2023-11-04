#ifndef DATE_TIME_H
#define DATE_TIME_H

typedef struct Expanded_Date_Time Expanded_Date_Time;
struct Expanded_Date_Time
{
	i32 day;
	i32 month;
	i32 year;
	i32 seconds;
	i32 minutes;
	i32 hours;
};

typedef struct Timestamp Timestamp;
struct Timestamp
{
	String error;
	Expanded_Date_Time expanded_format;
	i64 unix_format;
};

// NOTE(ariel) Parse date time strings in RFC 822 or 3339 formats:
// "%a, %d %b %Y %H:%M:%S %Z" <--- EXAMPLE --- "Sun, 14 May 2023 19:32:11 GMT"
//      "%Y-%m-%dT%H:%M:%S%z" <--- EXAMPLE --- "2023-14-05T19:32:11Z"
static Timestamp parse_date_time(String date_time);

#endif
