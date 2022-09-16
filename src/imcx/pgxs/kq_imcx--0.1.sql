-- Gives information about the extension
CREATE FUNCTION kq_imcx_info()
    RETURNS SETOF RECORD
AS 'MODULE_PATHNAME', 'kq_imcx_info'
    LANGUAGE C STRICT VOLATILE;

-- This caches all the calendars into memory
-- CREATE FUNCTION kq_imcx_load()
--     RETURNS TEXT
-- AS 'MODULE_PATHNAME',
-- 'kq_load_all_calendars'
--     LANGUAGE C IMMUTABLE STRICT ;

-- Clears the cache (and frees memory)
CREATE FUNCTION kq_imcx_invalidate()
    RETURNS TEXT
AS 'MODULE_PATHNAME',
    'kq_invalidate_cache'
LANGUAGE C IMMUTABLE STRICT;

-- Displays as Log Messages the contents of the cache.
CREATE FUNCTION kq_imcx_report(int, int, int)
    RETURNS TEXT
AS 'MODULE_PATHNAME',
'kq_report_cache'
    LANGUAGE C IMMUTABLE STRICT;

-- Calculates next date for the given days (selects calendar by ID).
CREATE FUNCTION kq_imcx_add_days(date, int, int)
    RETURNS DATE
AS 'MODULE_PATHNAME',
    'kq_add_calendar_days'
LANGUAGE C IMMUTABLE STRICT ;

-- Calculates next date for the given days (selects calendar by NAME).
CREATE FUNCTION kq_imcx_add_days_name(date, int, text)
    RETURNS DATE
AS 'MODULE_PATHNAME',
'kq_add_calendar_days_by_calendar_name'
    LANGUAGE C IMMUTABLE STRICT ;

-- -- This caches the specified calendar_id into memory
-- CREATE FUNCTION kq_load_calendar(int)
--     RETURNS TEXT
-- AS 'MODULE_PATHNAME',
-- ''
--     LANGUAGE C IMMUTABLE STRICT ;
