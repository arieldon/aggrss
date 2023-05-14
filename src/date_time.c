#include <stdio.h>

#include "base.h"
#include "date_time.h"
#include "str.h"

// TODO(ariel) Compress.

typedef struct Date_Time_Parser Date_Time_Parser;
struct Date_Time_Parser
{
	String error;
	String date_time;
	i32 cursor;
};

internal inline i32
parse_week_day(String day)
{
	i32 result = -1;

	String days[] = {
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

	return result;
}

internal inline i32
parse_month(String month)
{
	i32 result = -1;

	String months[] = {
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

	return result;
}

internal inline i32
get_offset_from_zone(String zone)
{
	i32 offset_in_hours = INT32_MIN;

	i32 zone_index = -1;
	String zones[] =
	{
		string_literal("UT"),
		string_literal("GMT"),
		string_literal("Z"),
		string_literal("EST"),
		string_literal("EDT"),
		string_literal("CST"),
		string_literal("CDT"),
		string_literal("MST"),
		string_literal("MDT"),
		string_literal("PST"),
		string_literal("PDT"),
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
		i32 offsets[] =
		{
			+0, +0, +0,
			-5, -4,
			-6, -5,
			-7, -6,
			-8, -7,
		};
		offset_in_hours = offsets[zone_index];
	}

	return offset_in_hours;
}

// NOTE(ariel) The date time format in RSS feeds follows RFC 822, Section 5 --
// https://datatracker.ietf.org/doc/html/rfc822#section-5
internal Date_Time
parse_rfc_822_format(String date_time)
{
	Date_Time result = {0};
	Date_Time_Parser parser =
	{
		.error = {0},
		.date_time = date_time,
		.cursor = 0,
	};

	{
		String week_day =
		{
			.str = parser.date_time.str,
			.len = 0,
		};

		while (parser.cursor < parser.date_time.len)
		{
			char ch = parser.date_time.str[parser.cursor++];
			if (ch == ',')
			{
				if (parser.date_time.str[parser.cursor] == ' ')
				{
					++parser.cursor;
					break;
				}
				else
				{
					parser.error = string_literal(
						"expected space after comma following week day");
				}
			}
			++week_day.len;
		}

		result.week_day = parse_week_day(week_day);
		if (result.week_day == -1)
		{
			parser.error = string_literal("expected week day");
		}
	}

	if (!parser.error.str)
	{
		String month_day =
		{
			.str = parser.date_time.str + parser.cursor,
			.len = 0,
		};

		while (parser.cursor < parser.date_time.len)
		{
			char ch = parser.date_time.str[parser.cursor++];
			if (ch == ' ')
			{
				break;
			}
			else if (ch < '0' || ch > '9')
			{
				parser.error = string_literal("expected integer for day of month");
				break;
			}
			++month_day.len;
		}

		result.month_day = string_to_int(month_day, 10);
	}

	if (!parser.error.str)
	{
		String month =
		{
			.str = parser.date_time.str + parser.cursor,
			.len = 0,
		};

		while (parser.cursor < parser.date_time.len)
		{
			char ch = parser.date_time.str[parser.cursor++];
			if (ch == ' ')
			{
				break;
			}
			++month.len;
		}

		result.month = parse_month(month);
		if (result.month == -1)
		{
			parser.error = string_literal("expected a month");
		}
	}

	if (!parser.error.str)
	{
		String year =
		{
			.str = parser.date_time.str + parser.cursor,
			.len = 0,
		};

		while (parser.cursor < parser.date_time.len)
		{
			char ch = parser.date_time.str[parser.cursor++];
			if (ch == ' ')
			{
				break;
			}
			else if (ch < '0' || ch > '9')
			{
				parser.error = string_literal("expected integer for year");
				break;
			}
			++year.len;
		}

		result.year = string_to_int(year, 10);
	}

	if (!parser.error.str)
	{
		String hours =
		{
			.str = parser.date_time.str + parser.cursor,
			.len = 0,
		};

		while (parser.cursor < parser.date_time.len)
		{
			char ch = parser.date_time.str[parser.cursor++];
			if (ch == ':')
			{
				break;
			}
			else if (ch < '0' || ch > '9')
			{
				parser.error = string_literal("expected integer to define hours");
				break;
			}
			++hours.len;
		}

		result.hours = string_to_int(hours, 10);
	}

	if (!parser.error.str)
	{
		String minutes =
		{
			.str = parser.date_time.str + parser.cursor,
			.len = 0,
		};

		while (parser.cursor < parser.date_time.len)
		{
			char ch = parser.date_time.str[parser.cursor++];
			if (ch == ':')
			{
				break;
			}
			else if (ch < '0' || ch > '9')
			{
				parser.error = string_literal("expected integer define minutes");
				break;
			}
			++minutes.len;
		}

		result.minutes = string_to_int(minutes, 10);
	}

	if (!parser.error.str)
	{
		String seconds =
		{
			.str = parser.date_time.str + parser.cursor,
			.len = 0,
		};

		while (parser.cursor < parser.date_time.len)
		{
			char ch = parser.date_time.str[parser.cursor++];
			if (ch == ' ')
			{
				break;
			}
			else if (ch < '0' || ch > '9')
			{
				parser.error = string_literal("expected integer define seconds");
				break;
			}
			++seconds.len;
		}

		result.seconds = string_to_int(seconds, 10);
	}

	if (!parser.error.str)
	{
		String zone =
		{
			.str = parser.date_time.str + parser.cursor,
			.len = 0,
		};

		while (parser.cursor < parser.date_time.len)
		{
			++parser.cursor;
			++zone.len;
		}

		i32 offset_in_hours = get_offset_from_zone(zone);
		if (offset_in_hours == INT32_MIN)
		{
			parser.error = string_literal("failed to parse time zone");
		}
		result.hours += offset_in_hours;
	}

	if (parser.cursor != parser.date_time.len)
	{
		parser.error = string_literal("failed to parse entire date time string");
	}

	if (parser.error.str)
	{
		fprintf(stderr, "%.*s\n", parser.error.len, parser.error.str);
		MEM_ZERO_STRUCT(&result);
	}

	return result;
}

// NOTE(ariel) The date time format in Atom feeds follows RFC 3339 --
// https://datatracker.ietf.org/doc/html/rfc3339#section-5.6
internal Date_Time
parse_rfc_3339_format(String date_time)
{
	Date_Time result = {0};
	Date_Time_Parser parser =
	{
		.error = {0},
		.date_time = date_time,
		.cursor = 0,
	};

	{
		String year =
		{
			.str = parser.date_time.str,
			.len = 0,
		};

		while (parser.cursor < parser.date_time.len)
		{
			char ch = parser.date_time.str[parser.cursor++];
			if (ch == '-')
			{
				break;
			}
			else if (ch < '0' || ch > '9')
			{
				parser.error = string_literal("expected integer for year");
				break;
			}
			++year.len;
		}

		result.year = string_to_int(year, 10);
	}

	if (!parser.error.str)
	{
		String month =
		{
			.str = parser.date_time.str + parser.cursor,
			.len = 0,
		};

		while (parser.cursor < parser.date_time.len)
		{
			char ch = parser.date_time.str[parser.cursor++];
			if (ch == '-')
			{
				break;
			}
			else if (ch < '0' || ch > '9')
			{
				parser.error = string_literal("expected integer for month");
				break;
			}
			++month.len;
		}

		result.month = string_to_int(month, 10);
	}

	if (!parser.error.str)
	{
		String month_day =
		{
			.str = parser.date_time.str + parser.cursor,
			.len = 0,
		};

		while (parser.cursor < parser.date_time.len)
		{
			char ch = parser.date_time.str[parser.cursor++];
			if (ch == 'T')
			{
				break;
			}
			else if (ch < '0' || ch > '9')
			{
				parser.error = string_literal("expected integer for month_day");
				break;
			}
			++month_day.len;
		}

		result.month_day = string_to_int(month_day, 10);
	}

	if (!parser.error.str)
	{
		String hours =
		{
			.str = parser.date_time.str + parser.cursor,
			.len = 0,
		};

		while (parser.cursor < parser.date_time.len)
		{
			char ch = parser.date_time.str[parser.cursor++];
			if (ch == ':')
			{
				break;
			}
			else if (ch < '0' || ch > '9')
			{
				parser.error = string_literal("expected integer to define hours");
				break;
			}
			++hours.len;
		}

		result.hours = string_to_int(hours, 10);
	}

	if (!parser.error.str)
	{
		String minutes =
		{
			.str = parser.date_time.str + parser.cursor,
			.len = 0,
		};

		while (parser.cursor < parser.date_time.len)
		{
			char ch = parser.date_time.str[parser.cursor++];
			if (ch == ':')
			{
				break;
			}
			else if (ch < '0' || ch > '9')
			{
				parser.error = string_literal("expected integer to define minutes");
				break;
			}
			++minutes.len;
		}

		result.minutes = string_to_int(minutes, 10);
	}

	if (!parser.error.str)
	{
		String seconds =
		{
			.str = parser.date_time.str + parser.cursor,
			.len = 0,
		};

		while (parser.cursor < parser.date_time.len)
		{
			char ch = parser.date_time.str[parser.cursor];
			if (ch < '0' || ch > '9')
			{
				break;
			}
			++seconds.len;
			++parser.cursor;
		}

		result.seconds = string_to_int(seconds, 10);
	}

	// TODO(ariel) Optionally parse fractional seconds.
	{
	}

	if (!parser.error.str)
	{
		String zone =
		{
			.str = parser.date_time.str + parser.cursor,
			.len = 0,
		};

		while (parser.cursor < parser.date_time.len)
		{
			++parser.cursor;
			++zone.len;
		}

		i32 offset_in_hours = get_offset_from_zone(zone);
		if (offset_in_hours == INT32_MIN)
		{
			parser.error = string_literal("failed to parse time zone");
		}
		result.hours += offset_in_hours;
	}

	if (parser.error.str)
	{
		fprintf(stderr, "%.*s\n", parser.error.len, parser.error.str);
		MEM_ZERO_STRUCT(&result);
	}

	return result;
}

Date_Time
parse_date_time(String date_time)
{
	Date_Time result = {0};

	// result = parse_rfc_822_format(date_time);
	result = parse_rfc_3339_format(date_time);

	return result;
}
