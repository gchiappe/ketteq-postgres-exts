/**
 * (C) KetteQ, Inc.
 */

#ifndef KETTEQ_POSTGRESQL_EXTENSIONS_COMMON_H
#define KETTEQ_POSTGRESQL_EXTENSIONS_COMMON_H

#include <stddef.h>
#include <sys/types.h>
#include <glib.h>
#include "postgres.h"

#define RET_SUCCESS 0 // Success.
#define RET_ERROR -1 // Generic error.
#define RET_ERROR_MIN_GT_MAX -2 //
#define RET_ERROR_NULL_VALUE -3
#define RET_ERROR_CANNOT_ALLOCATE -4
#define RET_ERROR_INVALID_PARAM -5
#define RET_ERROR_NOT_FOUND -6
#define RET_ERROR_UNSUPPORTED_OP -7
#define RET_ERROR_NOT_READY -8 // The Function requires a previous step before using.
#define RET_ADD_DAYS_NEGATIVE -9
#define RET_ADD_DAYS_POSITIVE -10

/**
 *
 */
typedef struct {
	unsigned long id; // Calendar ID (Same as in origin DB)
	int32 * dates; // Dates contained in the Calendar
	long dates_size; // Count of dates.
	int page_size; // Calculated Page Size
	long first_page_offset; // Offset of the First Page
	long * page_map; // Page map contained in the Calendar
	long page_map_size; // Count of Page map entries
} Calendar;

typedef struct {
	Calendar * calendars; // Calendars contained in the ICMX struct
	unsigned long calendar_count; // Count of Calendars
	unsigned long entry_count; // Count of Entries (From all Calendars)
	bool cache_filled; // Control variable set to TRUE when the `cache_finish()` function is called.
	GHashTable * calendar_name_hashtable; // Hashtable containing the names of the Calendars
} IMCX;

#endif //KETTEQ_POSTGRESQL_EXTENSIONS_COMMON_H
