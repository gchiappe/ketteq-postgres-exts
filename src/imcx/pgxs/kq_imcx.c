/**
 * (C) KetteQ, Inc.
 */

#include "kq_imcx.h"

// Init of Extension

void _PG_init (void)
{
  init_shared_memory ();
  init_gucs ();
  ereport(INFO, errmsg ("KetteQ In-Memory Calendar Extension Loaded."));
}

void _PG_fini (void)
{
  ereport(INFO, errmsg ("Unloaded KetteQ In-Memory Calendar Extension."));
}

typedef struct {
	int tranche_id;
	LWLock lock;
} IMCXSharedMemory;

IMCXSharedMemory *shared_memory_ptr;
IMCX *imcx_ptr;
HTAB *imcx_calendar_name_hashtable;

char *q1 = QUERY_GET_CAL_MIN_MAX_ID;
char *q2 = QUERY_GET_CAL_ENTRY_COUNT;
char *q3 = QUERY_GET_CAL_GET_ENTRIES;

void init_gucs ()
{
  DefineCustomStringVariable ("kq.calendar.q1_get_calendar_min_max_id",
							  "Query to select the MIN and MAX slices types IDs.",
							  NULL,
							  &q1,
							  QUERY_GET_CAL_MIN_MAX_ID,
							  PGC_USERSET,
							  0,
							  NULL,
							  NULL,
							  NULL);

  DefineCustomStringVariable ("kq.calendar.q2_get_calendars_entry_count",
							  "Query to select the entry count for each slice types.",
							  NULL,
							  &q2,
							  QUERY_GET_CAL_ENTRY_COUNT,
							  PGC_USERSET,
							  0,
							  NULL,
							  NULL,
							  NULL);

  DefineCustomStringVariable ("kq.calendar.q3_get_calendar_entries",
							  "Query to select all calendar entries. This will be copied to the cache.",
							  NULL,
							  &q3,
							  QUERY_GET_CAL_GET_ENTRIES,
							  PGC_USERSET,
							  0,
							  NULL,
							  NULL,
							  NULL);
}

static void init_shared_memory ()
{
  Size z = 0;
  Size hz = hash_estimate_size (MAX_CALENDAR_COUNT, sizeof (CalendarNameEntry));
  z = add_size (z, hz);
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Requested Shared Memory: %ld", z));
  RequestAddinShmemSpace (z);
  LWLockAcquire (AddinShmemInitLock, LW_EXCLUSIVE);
  if (!LWLockHeldByMe (AddinShmemInitLock))
	{
	  ereport(ERROR, errmsg ("Cannot acquire AddinShmemInitLock"));
	}
  bool shared_memory_found;
  bool imcx_found;
  shared_memory_ptr = ShmemInitStruct ("IMCXSharedMemory", sizeof (IMCXSharedMemory), &shared_memory_found);
  imcx_ptr = ShmemInitStruct ("IMCX", sizeof (IMCX), &imcx_found);
  if (!shared_memory_found || !imcx_found)
	{
	  memset (shared_memory_ptr, 0, sizeof (IMCXSharedMemory));
	  memset (imcx_ptr, 0, sizeof (IMCX));
	  shared_memory_ptr->tranche_id = LWLockNewTrancheId ();
	  LWLockRegisterTranche (shared_memory_ptr->tranche_id, TRANCHE_NAME);
	  LWLockInitialize (&shared_memory_ptr->lock, shared_memory_ptr->tranche_id);
	  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Allocated Shared Memory."));
	}
  else
	{
	  ereport(DEF_DEBUG_LOG_LEVEL, errmsg (
		  "CalendarCount: %lu, EntryCount: %lu, Cache Filled: %d, Calendar[0]->dates[0]: %d",
		  imcx_ptr->calendar_count,
		  imcx_ptr->entry_count,
		  imcx_ptr->cache_filled,
		  imcx_ptr->calendars[0]->dates[0]
	  ));
	  int attach_result = pg_cache_attach (imcx_ptr);
	  if (attach_result != RET_SUCCESS)
		{
		  ereport(ERROR, errmsg ("Cannot Attach Names HashTable, Error Code: %d", attach_result));
		}
	  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Shared memory attached."));
	}
  LWLockRelease (AddinShmemInitLock);
  ereport(INFO, errmsg ("Initialized Shared Memory."));
}

void load_cache_concrete ()
{
  if (imcx_ptr->cache_filled)
	{
	  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Cache already filled. Skipping slice loading."));
	  return; // Do nothing if the cache is already filled
	}
  LWLockAcquire (AddinShmemInitLock, LW_EXCLUSIVE);
  if (!LWLockHeldByMe (AddinShmemInitLock))
	{
	  ereport(ERROR, errmsg ("Cannot acquire Exclusive Write Lock."));
	}
  if (!LWLockHeldByMe (AddinShmemInitLock))
	{
	  ereport(ERROR, errmsg ("Cannot acquire AddinShmemInitLock"));
	}
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Exclusive Write Lock Acquired."));
  // Vars
  uint64 prev_calendar_id = 0; // Previous Calendar ID
  uint64 entry_count = 0; // Entry Counter
  // Row Control
  bool entry_is_null; // Pointer to boolean, TRUE if the last entry was NULL
  // Query #1, Get MIN_ID and MAX_ID of Calendars
  char const *sql_get_min_max = q1; // 1 - 74079
  // Query #2, Get Calendar's Entries Count and Names
  char const *sql_get_entries_count_per_calendar_id = q2;
  // Query #3, Get Entries of Calendars
  char const *sql_get_entries = q3;
  // Connect to SPI
  int spi_connect_result = SPI_connect ();
  if (spi_connect_result < 0)
	{
	  ereport(ERROR, errmsg ("SPI_connect returned %d", spi_connect_result));
	}
  // Execute Q1, get the min and max id's
  if (SPI_execute (sql_get_min_max, true, 0) != SPI_OK_SELECT || SPI_processed == 0)
	{
	  SPI_finish ();
	  ereport(ERROR, errmsg ("No calendars."));
	}

  // Get the Data and Descriptor
  HeapTuple min_max_tuple = SPI_tuptable->vals[0];
  //
  int32 min_value = DatumGetInt32(
	  SPI_getbinval (min_max_tuple,
					 SPI_tuptable->tupdesc,
					 1,
					 &entry_is_null));
  int32 max_value = DatumGetInt32(
	  SPI_getbinval (min_max_tuple,
					 SPI_tuptable->tupdesc,
					 2,
					 &entry_is_null));
  // Init the Struct Cache
  if (pg_cache_init (imcx_ptr, min_value, max_value) < 0)
	{
	  ereport(ERROR, errmsg ("Shared Memory Cannot Be Allocated (cache_init, %d, %d)", min_value, max_value));
	}
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg (
	  "Memory Allocated (Q1), Min-Value: '%d' Max-Value: '%d'",
	  min_value,
	  max_value
  ));
  // Init the Structs date property with the count of the entries
  if (SPI_execute (sql_get_entries_count_per_calendar_id, true, 0) != SPI_OK_SELECT || SPI_processed == 0)
	{
	  SPI_finish ();
	  ereport(ERROR, errmsg ("Cannot count calendar's entries."));
	}
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Q2: Got %" PRIu64 " SliceTypes.", SPI_processed));

  for (uint64 row_counter = 0; row_counter < SPI_processed; row_counter++)
	{
	  HeapTuple cal_entries_count_tuple = SPI_tuptable->vals[row_counter];
	  int32 calendar_id = DatumGetInt32(
		  SPI_getbinval (cal_entries_count_tuple,
						 SPI_tuptable->tupdesc,
						 1,
						 &entry_is_null));
	  if (entry_is_null)
		{
		  continue;
		}
	  uint64 calendar_entry_count = DatumGetUInt64(
		  SPI_getbinval (cal_entries_count_tuple,
						 SPI_tuptable->tupdesc,
						 2,
						 &entry_is_null));
	  char *calendar_name = DatumGetCString(
		  SPI_getvalue (
			  cal_entries_count_tuple,
			  SPI_tuptable->tupdesc,
			  3
		  )
	  );
	  ereport(DEF_DEBUG_LOG_LEVEL,
			  errmsg (
				  "Q2 (Cursor): Got: SliceTypeName: %s, SliceType: %d, Entries: %" PRIu64,
				  calendar_name,
				  calendar_id,
				  calendar_entry_count
			  ));
	  // Add to the cache
	  Calendar *calendar = pg_get_calendar (imcx_ptr, calendar_id); // Pointer to current calendar
	  int init_result = pg_calendar_init (
		  calendar,
		  calendar_id,
		  calendar_entry_count,
		  &imcx_ptr->entry_count
	  );
	  if (init_result != RET_SUCCESS)
		{
		  ereport(ERROR, errmsg ("Cannot initialize calendar, ERROR CODE: %d", init_result));
		}
	  ereport(DEF_DEBUG_LOG_LEVEL, errmsg (
		  "Calendar Initialized, Index: '%d', ID: '%d', Entry count: '%ld'",
		  calendar_id - 1,
		  calendar_id,
		  calendar_entry_count
	  ));
	  // Add The Calendar Name
	  int set_name_result = pg_set_calendar_name (imcx_ptr,
												  calendar,
												  calendar_name);
	  if (set_name_result != RET_SUCCESS)
		{
		  ereport(ERROR, errmsg ("Cannot set '%s' as name for calendar id '%d'", calendar_name, calendar_id));
		}
	  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Calendar Name '%s' Set", calendar_name));
	}
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Executing Q3"));
  // -> Exec Q3
  // Check Results
  if (SPI_execute (sql_get_entries, true, 0) != SPI_OK_SELECT || SPI_processed == 0)
	{
	  SPI_finish ();
	  ereport(ERROR, errmsg ("No calendar entries."));
	}
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Q3: RowCount: '%ld'", SPI_processed));
  for (uint64 row_counter = 0; row_counter < SPI_processed; row_counter++)
	{
	  HeapTuple cal_entries_tuple = SPI_tuptable->vals[row_counter];
	  int32 calendar_id = DatumGetInt32(
		  SPI_getbinval (cal_entries_tuple,
						 SPI_tuptable->tupdesc,
						 1,
						 &entry_is_null));
	  int32 calendar_entry = DatumGetDateADT(
		  SPI_getbinval (cal_entries_tuple,
						 SPI_tuptable->tupdesc,
						 2,
						 &entry_is_null));

	  if (prev_calendar_id != calendar_id)
		{
		  prev_calendar_id = calendar_id;
		  entry_count = 0;
		}
	  Calendar * calendar = pg_get_calendar (imcx_ptr, calendar_id);
	  ereport(DEBUG2, errmsg (
		  "Calendar Index: '%d', Calendar Id '%d', Calendar Entry (DateADT): '%d', Entry Count '%ld'",
		  get_calendar_index (imcx_ptr, calendar_id),
		  calendar_id,
		  calendar_entry,
		  entry_count
	  ));
	  // Fill the Dates Entries
	  calendar->dates[entry_count] = calendar_entry;
	  entry_count++;
	  // entry copy complete, calculate page size
	  if (calendar->dates_size == entry_count)
		{
		  int page_size_init_result = pg_init_page_size (calendar);
		  if (page_size_init_result != RET_SUCCESS)
			{
			  ereport(ERROR, errmsg ("Page map could not be initialized. Error Code: %d", page_size_init_result));
			}
		}
	}
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Q3: Cached %" PRIu64 " slices in total.", SPI_processed));
  SPI_finish ();
  cache_finish (imcx_ptr);
  LWLockRelease (AddinShmemInitLock);
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Exclusive Write Lock Released."));
  ereport(INFO, errmsg ("Slices Loaded Into Cache."));
}

void add_row_to_2_col_tuple (
	AttInMetadata *att_in_metadata,
	Tuplestorestate *tuplestorestate,
	char *property,
	char *value
)
{
  HeapTuple tuple;
  char *values[2];
  values[0] = property;
  values[1] = value;
  tuple = BuildTupleFromCStrings (att_in_metadata, values);
  tuplestore_puttuple (tuplestorestate, tuple);
}

void add_row_to_1_col_tuple (
	AttInMetadata *att_in_metadata,
	Tuplestorestate *tuplestorestate,
	char *value
)
{
  HeapTuple tuple;
  char *values[1];
  values[0] = value;
  tuple = BuildTupleFromCStrings (att_in_metadata, values);
  tuplestore_puttuple (tuplestorestate, tuple);
}

PG_FUNCTION_INFO_V1(calendar_info);

Datum calendar_info (PG_FUNCTION_ARGS)
{
  load_cache_concrete ();
  LWLockAcquire (&shared_memory_ptr->lock, LW_SHARED);
  if (!LWLockHeldByMe (&shared_memory_ptr->lock))
	{
	  ereport(ERROR, errmsg ("Cannot Acquire Shared Read Lock."));
	}
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Shared Read Lock Acquired."));

  ReturnSetInfo *pInfo = (ReturnSetInfo *)fcinfo->resultinfo;
  Tuplestorestate *tuplestorestate;
  AttInMetadata *attInMetadata;
  MemoryContext oldContext;
  TupleDesc tupleDesc;

  /* check to see if caller supports us returning a tuplestorestate */
  if (pInfo == NULL || !IsA(pInfo, ReturnSetInfo))
	ereport(ERROR,
			(errcode (ERRCODE_FEATURE_NOT_SUPPORTED),
				errmsg ("set-valued function called in context that cannot accept a set")));
  if (!(pInfo->allowedModes & SFRM_Materialize))
	ereport(ERROR,
			(errcode (ERRCODE_SYNTAX_ERROR),
				errmsg ("materialize mode required, but it is not allowed in this context")));
  /* Build tuplestorestate to hold the result rows */
  oldContext = MemoryContextSwitchTo (pInfo->econtext->ecxt_per_query_memory); // Switch To Per Query Context

  tupleDesc = CreateTemplateTupleDesc (2);
  TupleDescInitEntry (tupleDesc,
					  (AttrNumber)1, "property", TEXTOID, -1, 0);
  TupleDescInitEntry (tupleDesc,
					  (AttrNumber)2, "calendar_id", TEXTOID, -1, 0);

  tuplestorestate = tuplestore_begin_heap (true, false, work_mem); // Create TupleStore
  pInfo->returnMode = SFRM_Materialize;
  pInfo->setResult = tuplestorestate;
  pInfo->setDesc = tupleDesc;
  MemoryContextSwitchTo (oldContext); // Switch back to previous context
  attInMetadata = TupleDescGetAttInMetadata (tupleDesc);

  add_row_to_2_col_tuple (attInMetadata, tuplestorestate,
						  "Version", CMAKE_VERSION);
  add_row_to_2_col_tuple (attInMetadata, tuplestorestate,
						  "Cache Available", imcx_ptr->cache_filled ? "Yes" : "No");
  add_row_to_2_col_tuple (attInMetadata, tuplestorestate,
						  "Slice Cache Size (SliceType Count)", convert_u_long_to_str (imcx_ptr->calendar_count));
  add_row_to_2_col_tuple (attInMetadata, tuplestorestate,
						  "Entry Cache Size (Slices)", convert_u_long_to_str (imcx_ptr->entry_count));
  add_row_to_2_col_tuple (attInMetadata, tuplestorestate,
						  "Shared Memory Requested (MBytes)",
						  convert_double_to_str (MAX_CALENDAR_COUNT / 1024.0 / 1024.0, 2));

  LWLockRelease (&shared_memory_ptr->lock);
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Shared Read Lock Released."));
  return (Datum)0;
}

PG_FUNCTION_INFO_V1(calendar_invalidate);

Datum calendar_invalidate (PG_FUNCTION_ARGS)
{
  if (!imcx_ptr->cache_filled) {
	  ereport(ERROR, errmsg ("Cache cannot be invalidated, is not yet loaded."));
  }
  LWLockAcquire (AddinShmemInitLock, LW_EXCLUSIVE);
  if (!LWLockHeldByMe (AddinShmemInitLock))
	{
	  ereport(ERROR, errmsg ("Cannot Acquire Exclusive Write Lock."));
	}
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Exclusive Write Lock Acquired."));
  int invalidate_result = cache_invalidate (imcx_ptr);
  LWLockRelease (AddinShmemInitLock);
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Exclusive Write Lock Released."));
  if (invalidate_result == 0)
	{
	  ereport(INFO, errmsg ("Cache Invalidated Successfully"));
	}
  else
	{
	  ereport(INFO, errmsg ("Cache Cannot be Invalidated, Error Code: %d", invalidate_result));
	}

	PG_RETURN_VOID();
}

static CalendarNameEntry *find_calendar_name_entry (HTAB *htab, int calendar_id)
{
  CalendarNameEntry *entry = NULL;
  HASH_SEQ_STATUS status;

  long count = hash_get_num_entries (htab);
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("htab size %ld", count));

  hash_seq_init (&status, htab);
  for (int idx = 0; idx < count; idx++)
	{
	  entry = (CalendarNameEntry *)hash_seq_search (&status);
	  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("calendar: %s [%d] (?) %d",
										   entry->key,
										   entry->calendar_id,
										   calendar_id
	  ));
	  if (entry->calendar_id == calendar_id)
		{
		  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("found"));
		  break;
		}

	}
  hash_seq_term (&status);
  return entry;
}

int imcx_report_concrete (int showEntries, int showPageMapEntries, FunctionCallInfo fcinfo)
{
  LWLockAcquire (&shared_memory_ptr->lock, LW_SHARED);
  if (!LWLockHeldByMe (&shared_memory_ptr->lock))
	{
	  ereport(ERROR, errmsg ("Cannot Acquire Shared Read Lock."));
	}
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Shared Read Lock Acquired."));

  ReturnSetInfo *p_return_set_info = (ReturnSetInfo *)fcinfo->resultinfo;
  Tuplestorestate *tuplestorestate;
  AttInMetadata *p_metadata;
  MemoryContext old_context;
  TupleDesc tuple_desc;
  // --
  /* check to see if caller supports us returning a tuplestorestate */
  if (p_return_set_info == NULL || !IsA(p_return_set_info, ReturnSetInfo))
	ereport(ERROR,
			(errcode (ERRCODE_FEATURE_NOT_SUPPORTED),
				errmsg ("set-valued function called in context that cannot accept a set")));
  if (!(p_return_set_info->allowedModes & SFRM_Materialize))
	ereport(ERROR,
			(errcode (ERRCODE_SYNTAX_ERROR),
				errmsg ("materialize mode required, but it is not allowed in this context")));

  /* Build tuplestorestate to hold the result rows */
  old_context = MemoryContextSwitchTo (p_return_set_info->econtext
										   ->ecxt_per_query_memory); // Switch To Per Query Context
  tuple_desc = CreateTemplateTupleDesc (2);
  TupleDescInitEntry (tuple_desc,
					  (AttrNumber)1, "property", TEXTOID, -1, 0);
  TupleDescInitEntry (tuple_desc,
					  (AttrNumber)2, "calendar_id", TEXTOID, -1, 0);
  tuplestorestate = tuplestore_begin_heap (true, false, work_mem); // Create TupleStore
  p_return_set_info->returnMode = SFRM_Materialize;
  p_return_set_info->setResult = tuplestorestate;
  p_return_set_info->setDesc = tuple_desc;
  MemoryContextSwitchTo (old_context); // Switch back to previous context

  p_metadata = TupleDescGetAttInMetadata (tuple_desc);
  // --
  add_row_to_2_col_tuple (p_metadata, tuplestorestate,
						  "Slices-Id Max", convert_u_long_to_str (imcx_ptr->calendar_count));
  add_row_to_2_col_tuple (p_metadata, tuplestorestate,
						  "Cache-Calendars Size", convert_u_long_to_str (imcx_ptr->entry_count));
  // --
  int no_calendar_id_counter = 0;
  for (unsigned long i = 0; i < imcx_ptr->calendar_count; i++)
	{
	  Calendar *curr_calendar = imcx_ptr->calendars[i];
	  if (curr_calendar == NULL)
		{
		  ereport(ERROR, errmsg ("Calendar Index '%lu' is NULL, cannot continue. Total Calendars '%lu'", i, imcx_ptr
			  ->calendar_count));
		}
	  if (curr_calendar->id == 0)
		{
		  no_calendar_id_counter++;
		  continue;
		}
	  add_row_to_2_col_tuple (p_metadata, tuplestorestate,
							  "SliceType-Id", convert_u_long_to_str (curr_calendar->id));
	  add_row_to_2_col_tuple (p_metadata, tuplestorestate,
							  "   Name", curr_calendar->name);
	  add_row_to_2_col_tuple (p_metadata, tuplestorestate,
							  "   Entries", convert_u_long_to_str (curr_calendar->dates_size));
	  add_row_to_2_col_tuple (p_metadata, tuplestorestate,
							  "   Page Map Size", convert_long_to_str (curr_calendar->page_map_size));
	  add_row_to_2_col_tuple (p_metadata, tuplestorestate,
							  "   Page Size", convert_int_to_str (curr_calendar->page_size));
	  if (showEntries)
		{
		  for (unsigned long j = 0; j < curr_calendar->dates_size; j++)
			{
			  ereport(INFO, errmsg ("Entry[%lu]: %d", j, curr_calendar->dates[j]));

			  add_row_to_2_col_tuple (p_metadata, tuplestorestate,
									  "   Entry",
									  convert_int_to_str (curr_calendar->dates[j]));
			}
		}
	  if (showPageMapEntries)
		{
		  for (int j = 0; j < curr_calendar->page_map_size; j++)
			{
			  ereport(INFO, errmsg ("PageMap Entry[%d]: %ld", j, curr_calendar->page_map[j]));
			  add_row_to_2_col_tuple (p_metadata, tuplestorestate,
									  "   PageMap Entry",
									  convert_long_to_str (curr_calendar->page_map[j]));
			}
		}
	}
  add_row_to_2_col_tuple (p_metadata, tuplestorestate,
						  "Missing Slices (id==0)",
						  convert_int_to_str (no_calendar_id_counter));
  LWLockRelease (&shared_memory_ptr->lock);
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Shared Read Lock Released."));
  return 0;
}

PG_FUNCTION_INFO_V1(calendar_report);

Datum calendar_report (PG_FUNCTION_ARGS)
{
  load_cache_concrete ();
  int32 showEntries = PG_GETARG_INT32(0);
  int32 showPageMapEntries = PG_GETARG_INT32(1);
  imcx_report_concrete (showEntries, showPageMapEntries, fcinfo);
  return (Datum)0;
}

PG_FUNCTION_INFO_V1(add_calendar_days_by_id);

Datum
add_calendar_days_by_id (PG_FUNCTION_ARGS)
{
  load_cache_concrete ();
  LWLockAcquire (&shared_memory_ptr->lock, LW_SHARED);
  if (!LWLockHeldByMe (&shared_memory_ptr->lock))
	{
	  ereport(ERROR, errmsg ("Cannot Acquire Shared Read Lock."));
	}
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Shared Read Lock Acquired."));

  int32 date = PG_GETARG_INT32(0);
  int32 calendar_interval = PG_GETARG_INT32(1);
  int32 calendar_id = PG_GETARG_INT32(2);
  Calendar *cal = pg_get_calendar (imcx_ptr, calendar_id);
  //
  unsigned long fd_idx;
  unsigned long rs_idx;
  int32 new_date;
  //
  int add_calendar_days_result = add_calendar_days (imcx_ptr,
													cal,
													date,
													calendar_interval,
													&new_date,
													&fd_idx,
													&rs_idx);
  //
  if (add_calendar_days_result == RET_ERROR_NOT_READY)
	{
	  ereport(ERROR, errmsg ("Cache is not ready."));
	}
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg (
	  "FirstDate-Idx: %lu = %d, ResultDate-Idx: %lu = %d",
	  fd_idx,
	  cal->dates[fd_idx],
	  rs_idx,
	  cal->dates[rs_idx]
  ));
  //
  LWLockRelease (&shared_memory_ptr->lock);
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Shared Read Lock Released."));
  PG_RETURN_DATEADT(new_date);
}

PG_FUNCTION_INFO_V1(add_calendar_days_by_name);

Datum
add_calendar_days_by_name (PG_FUNCTION_ARGS)
{
  load_cache_concrete ();
  LWLockAcquire (&shared_memory_ptr->lock, LW_SHARED);
  if (!LWLockHeldByMe (&shared_memory_ptr->lock))
	{
	  ereport(ERROR, errmsg ("Cannot Acquire Shared Read Lock."));
	}
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Shared Read Lock Acquired."));
  // Vars
  int32 input_date = PG_GETARG_INT32(0);
  int32 calendar_interval = PG_GETARG_INT32(1);
  const text *calendar_name = PG_GETARG_TEXT_P(2);
  // Convert Name to CString
  const char *calendar_name_str = text_to_cstring (calendar_name);
  // Lookup for the Calendar
  int calendar_id = 0;
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Trying to find Calendar with name '%s'", calendar_name_str));
  int get_calendar_result =
	  pg_get_calendar_id_by_name (
		  imcx_ptr,
		  calendar_name_str,
		  &calendar_id
	  );
  if (get_calendar_result != RET_SUCCESS)
	{
	  if (get_calendar_result == RET_ERROR_NOT_FOUND)
		ereport(ERROR, errmsg ("Calendar does not exists."));
	  if (get_calendar_result == RET_ERROR_UNSUPPORTED_OP)
		ereport(ERROR, errmsg ("Cannot get calendar by name. (Out of Bounds)"));
	}
  Calendar *calendar = pg_get_calendar (imcx_ptr, calendar_id);
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Found Calendar with Name '%s' and ID '%d'", calendar_name_str, calendar->id));
  unsigned long fd_idx = 0;
  unsigned long rs_idx = 0;
  DateADT result_date;
  // Result Date
  int add_calendar_days_result = add_calendar_days (imcx_ptr,
													calendar,
													input_date,
													calendar_interval,
													&result_date,
													&fd_idx,
													&rs_idx);
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Add Calendar Days Returned '%d'", add_calendar_days_result));
  if (add_calendar_days_result == RET_ERROR_NOT_READY)
	{
	  ereport(ERROR, errmsg ("Cache is not ready."));
	}
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg (
	  "FirstDate-Idx: %lu = %d, ResultDate-Idx: %lu = %d",
	  fd_idx,
	  calendar->dates[fd_idx],
	  rs_idx,
	  calendar->dates[rs_idx]
  ));
  LWLockRelease (&shared_memory_ptr->lock);
  ereport(DEF_DEBUG_LOG_LEVEL, errmsg ("Shared Read Lock Released."));
  PG_RETURN_DATEADT(result_date);
}