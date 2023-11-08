typedef struct Date_Time_Parser Date_Time_Parser;
struct Date_Time_Parser
{
	String date_time;
	String error;
	b32 success;
	s32 cursor;
};

static s32
parse_week_day(Date_Time_Parser *parser, String day)
{
	s32 result = -1;

	String days[] =
	{
		string_literal("Sun"),
		string_literal("Mon"),
		string_literal("Tue"),
		string_literal("Wed"),
		string_literal("Thu"),
		string_literal("Fri"),
		string_literal("Sat"),
	};
	for (u32 i = 0; i < ARRAY_COUNT(days); ++i)
	{
		if (string_match(days[i], day))
		{
			result = i;
			break;
		}
	}

	if (!parser->error.str && result == -1)
	{
		parser->error = string_literal("expected week day as string");
	}

	return result;
}

static s32
parse_month(Date_Time_Parser *parser, String month)
{
	s32 result = -1;

	String months[] =
	{
		string_literal("Jan"),
		string_literal("Feb"),
		string_literal("Mar"),
		string_literal("Apr"),
		string_literal("May"),
		string_literal("Jun"),
		string_literal("Jul"),
		string_literal("Aug"),
		string_literal("Sep"),
		string_literal("Oct"),
		string_literal("Nov"),
		string_literal("Dec"),
	};
	for (u32 i = 0; i < ARRAY_COUNT(months); ++i)
	{
		if (string_match(months[i], month))
		{
			result = i;
			break;
		}
	}

	if (!parser->error.str && result == -1)
	{
		parser->error = string_literal("expected month as string");
	}

	return result;
}

typedef struct Time_Zone_Offset Time_Zone_Offset;
struct Time_Zone_Offset
{
	s32 hours;
	s32 minutes;
};

static Time_Zone_Offset
get_offset_from_zone(Date_Time_Parser *parser, String zone)
{
	Time_Zone_Offset offset = {0};

	if (zone.len == 5)
	{
		b32 negative = zone.str[0] == '-';

		String hours =
		{
			.str = zone.str + 1,
			.len = 2,
		};
		String minutes =
		{
			.str = zone.str + 3,
			.len = 2,
		};

		offset.hours = ((s32)string_to_int(hours, 10) ^ -negative) + negative;
		offset.minutes = ((s32)string_to_int(minutes, 10) ^ -negative) + negative;
	}
	else
	{
		s32 zone_index = -1;

		String zones[] =
		{
			string_literal("UT"), string_literal("GMT"), string_literal("Z"),
			string_literal("EST"), string_literal("EDT"),
			string_literal("CST"), string_literal("CDT"),
			string_literal("MST"), string_literal("MDT"),
			string_literal("PST"), string_literal("PDT"),
		};
		for (u32 i = 0; i < ARRAY_COUNT(zones); ++i)
		{
			if (string_match(zones[i], zone))
			{
				zone_index = i;
				break;
			}
		}

		if (zone_index != -1)
		{
			s32 offsets[] =
			{
				+0, +0, +0,
				-5, -4,
				-6, -5,
				-7, -6,
				-8, -7,
			};
			offset.hours = offsets[zone_index];
		}
		else
		{
			parser->error = string_literal("expected time zone");
		}
	}

	return offset;
}

static s32
parse_number(Date_Time_Parser *parser, char delimiter, String error_message)
{
	String s =
	{
		.str = parser->date_time.str + parser->cursor,
		.len = 0,
	};

	while (parser->cursor < parser->date_time.len && !parser->error.str)
	{
		char ch = parser->date_time.str[parser->cursor++];
		if (ch == delimiter)
		{
			break;
		}
		else if (ch < '0' || ch > '9')
		{
			parser->error = error_message;
			break;
		}
		++s.len;
	}

	s32 number = (s32)string_to_int(s, 10);
	return number;
}

static String
parse_string(Date_Time_Parser *parser, char delimiter)
{
	String s =
	{
		.str = parser->date_time.str + parser->cursor,
		.len = 0,
	};

	while (parser->cursor < parser->date_time.len && !parser->error.str)
	{
		char ch = parser->date_time.str[parser->cursor++];
		if (ch == delimiter)
		{
			break;
		}
		++s.len;
	}

	return s;
}

static inline b32
is_leap_year(s32 year)
{
	b32 result = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
	return result;
}

static inline s32
get_days_in_month(s32 month, s32 year)
{
	s32 days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	b32 leap = month == 1 && is_leap_year(year);
	s32 result = days[month] + leap;
	return result;
}

// NOTE(ariel) This doesn't support dates _before_ the Unix epoch.
static s64
compute_unix_timestamp(Expanded_Date_Time date_time)
{
	const s32 SECONDS_PER_DAY = 86400;
	const s32 SECONDS_PER_HOUR = 3600;
	const s32 SECONDS_PER_MINUTE = 60;

	s64 unix_timestamp = 0;

	// NOTE(ariel) Add date.
	{
		s32 days = 0;

		// NOTE(ariel) Calculate total number of days from 1970 to given year.
		for (s32 year = 1970; year < date_time.year; ++year)
		{
			days += 365 + is_leap_year(year);
		}

		// NOTE(ariel) Calculate total number of days from beginning of year to
		// given month.
		for (s32 month = 0; month < date_time.month; ++month)
		{
			days += get_days_in_month(month, date_time.year);
		}

		// NOTE(ariel) Add remaining days of month.
		days += date_time.day - 1;

		// NOTE(ariel) Convert days to seconds
		unix_timestamp += days * SECONDS_PER_DAY;
	}

	// NOTE(ariel) Add time.
	{
		unix_timestamp += date_time.hours * SECONDS_PER_HOUR;
		unix_timestamp += date_time.minutes * SECONDS_PER_MINUTE;
		unix_timestamp += date_time.seconds;
	}

	return unix_timestamp;
}

// NOTE(ariel) The date time format in RSS feeds follows RFC 822, Section 5 --
// https://datatracker.ietf.org/doc/html/rfc822#section-5
//
// date-time   =  [ day "," ] date time        ; dd mm yy
//                                             ;  hh:mm:ss zzz
//
// day         =  "Mon"  / "Tue" /  "Wed"  / "Thu"
//             /  "Fri"  / "Sat" /  "Sun"
//
// date        =  1*2DIGIT month 2DIGIT        ; day month year
//                                             ;  e.g. 20 Jun 82
//
// month       =  "Jan"  /  "Feb" /  "Mar"  /  "Apr"
//             /  "May"  /  "Jun" /  "Jul"  /  "Aug"
//             /  "Sep"  /  "Oct" /  "Nov"  /  "Dec"
//
// time        =  hour zone                    ; ANSI and Military
//
// hour        =  2DIGIT ":" 2DIGIT [":" 2DIGIT]
//                                             ; 00:00:00 - 23:59:59
//
// zone        =  "UT"  / "GMT"                ; Universal Time
//                                             ; North American : UT
//             /  "EST" / "EDT"                ;  Eastern:  - 5/ - 4
//             /  "CST" / "CDT"                ;  Central:  - 6/ - 5
//             /  "MST" / "MDT"                ;  Mountain: - 7/ - 6
//             /  "PST" / "PDT"                ;  Pacific:  - 8/ - 7
//             /  1ALPHA                       ; Military: Z = UT;
//                                             ;  A:-1; (J not used)
//                                             ;  M:-12; N:+1; Y:+12
//             / ( ("+" / "-") 4DIGIT )        ; Local differential
//                                             ;  hours+min. (HHMM)
static void
parse_rfc_822_format(Date_Time_Parser *parser, Timestamp *timestamp)
{
	Expanded_Date_Time result = {0};

	// NOTE(ariel) Eat week day.
	String week_day = parse_string(parser, ',');
	parse_week_day(parser, week_day);

	// NOTE(ariel) Eat comma-and-space combo that trails week day.
	if (!parser->error.str)
	{
		if (parser->date_time.str[parser->cursor] == ' ')
		{
			++parser->cursor;
		}
		else
		{
			parser->error = string_literal("expected space after comma trailing week day");
		}
	}

	result.day = parse_number(parser, ' ', string_literal("expected day of month"));
	String month = parse_string(parser, ' ');
	result.month = parse_month(parser, month);
	result.year = parse_number(parser, ' ', string_literal("expected year"));

	result.hours = parse_number(parser, ':', string_literal("expected hours"));
	result.minutes = parse_number(parser, ':', string_literal("expected minutes"));
	result.seconds = parse_number(parser, ' ', string_literal("expected seconds"));

	if (!parser->error.str)
	{
		String zone = parse_string(parser, 0);
		Time_Zone_Offset offset = get_offset_from_zone(parser, zone);
		result.hours += offset.hours;
		result.minutes += offset.minutes;
	}

	if (!parser->error.str && parser->cursor != parser->date_time.len)
	{
		parser->error = string_literal("failed to parse date time string from start to finish");
	}

	if (!parser->error.str)
	{
		parser->success = true;
		timestamp->expanded_format = result;
		timestamp->unix_format = compute_unix_timestamp(result);
	}
}

// NOTE(ariel) The date time format in Atom feeds follows RFC 3339 --
// https://datatracker.ietf.org/doc/html/rfc3339#section-5.6
//
// date-fullyear   = 4DIGIT
// date-month      = 2DIGIT  ; 01-12
// date-mday       = 2DIGIT  ; 01-28, 01-29, 01-30, 01-31 based on
//                           ; month/year
// time-hour       = 2DIGIT  ; 00-23
// time-minute     = 2DIGIT  ; 00-59
// time-second     = 2DIGIT  ; 00-58, 00-59, 00-60 based on leap second
//                           ; rules
// time-secfrac    = "." 1*DIGIT
// time-numoffset  = ("+" / "-") time-hour ":" time-minute
// time-offset     = "Z" / time-numoffset
//
// partial-time    = time-hour ":" time-minute ":" time-second
//                   [time-secfrac]
// full-date       = date-fullyear "-" date-month "-" date-mday
// full-time       = partial-time time-offset
//
// date-time       = full-date "T" full-time
static void
parse_rfc_3339_format(Date_Time_Parser *parser, Timestamp *timestamp)
{
	Expanded_Date_Time result = {0};

	result.year = parse_number(parser, '-', string_literal("expected year"));
	result.month = parse_number(parser, '-', string_literal("expected month"));
	result.day = parse_number(parser, 'T', string_literal("expected day of day"));

	// NOTE(ariel) RFC 3339 specifies months from 1 to 12, but I want to use zero
	// later in the code path.
	result.month -= 1;

	result.hours = parse_number(parser, ':', string_literal("expected hours"));
	result.minutes = parse_number(parser, ':', string_literal("expected minutes"));

	if (!parser->error.str)
	{
		String seconds =
		{
			.str = parser->date_time.str + parser->cursor,
			.len = 0,
		};

		while (parser->cursor < parser->date_time.len)
		{
			char ch = parser->date_time.str[parser->cursor];
			if (ch < '0' || ch > '9')
			{
				break;
			}
			++seconds.len;
			++parser->cursor;
		}

		result.seconds = (s32)string_to_int(seconds, 10);
	}

	if (!parser->error.str)
	{
		String zone = parse_string(parser, 0);
		Time_Zone_Offset offset = get_offset_from_zone(parser, zone);
		result.hours += offset.hours;
		result.minutes += offset.minutes;
	}

	if (!parser->error.str && parser->cursor != parser->date_time.len)
	{
		parser->error = string_literal("failed to parse date time string from start to finish");
	}

	if (!parser->error.str)
	{
		parser->success = true;
		timestamp->expanded_format = result;
		timestamp->unix_format = compute_unix_timestamp(result);
	}
}

static Timestamp
parse_date_time(String date_time)
{
	Timestamp timestamp = {0};

	Date_Time_Parser parser =
	{
		.date_time = date_time,
		.success = false,
	};
	if (!parser.success)
	{
		parse_rfc_822_format(&parser, &timestamp);
	}
	if (!parser.success)
	{
		s32 previous_cursor_position = parser.cursor;
		String previous_error_message = parser.error;

		// NOTE(ariel) Reset parser for another pass.
		parser.cursor = 0;
		MEM_ZERO_STRUCT(&parser.error);

		parse_rfc_3339_format(&parser, &timestamp);

		// NOTE(ariel) In the case that both calls fail to parse the date time
		// string, return the error message from whichever path progressed further.
		if (!parser.success && previous_cursor_position > parser.cursor)
		{
			timestamp.error = previous_error_message;
		}
	}

	return timestamp;
}
