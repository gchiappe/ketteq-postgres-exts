--
-- (C) KetteQ, Inc.
--

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION kq_imcx" to load this file. \quit

-- Gives information about the extension
CREATE OR REPLACE FUNCTION kq_calendar_cache_info()
    RETURNS TABLE ("property" text, "calendar_id" text)
STRICT
    LANGUAGE c AS 'MODULE_PATHNAME', 'calendar_info';

-- Clears the cache (and frees memory)
CREATE FUNCTION kq_invalidate_calendar_cache()
    RETURNS void
STRICT
    LANGUAGE c AS 'MODULE_PATHNAME', 'calendar_invalidate';

-- Displays as Log Messages the contents of the cache.
CREATE FUNCTION kq_calendar_cache_report(boolean, boolean)
    RETURNS TABLE ("property" text, "calendar_id" text)
STRICT
    LANGUAGE c AS 'MODULE_PATHNAME', 'calendar_report';

-- Calculates next date for the given days (selects calendar by ID).
CREATE FUNCTION kq_add_days_by_id(date, int, int)
    RETURNS DATE
STRICT
   LANGUAGE c AS 'MODULE_PATHNAME', 'add_calendar_days_by_id';

-- Calculates next date for the given days (selects calendar by NAME).
CREATE FUNCTION kq_add_days(date, int, text)
    RETURNS DATE
STRICT
    LANGUAGE c AS 'MODULE_PATHNAME', 'add_calendar_days_by_name';