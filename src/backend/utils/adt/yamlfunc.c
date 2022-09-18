/*-------------------------------------------------------------------------
 *
 * yamlfunc.c
 *		yaml data type support.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/utils/adt/yamlfunc.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include <yaml.h>

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "common/jsonapi.h"
#include "common/yamlapi.h"
#include "common/string.h"
#include "fmgr.h"
#include "funcapi.h"
#include "lib/stringinfo.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/hsearch.h"
#include "utils/yaml.h"
#include "utils/yamlfunc.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/syscache.h"
#include "utils/typcache.h"


/*
 * Report a YAML error.
 */
void yaml_ereport_error(const char * error)
{
  ereport(ERROR,
      (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
        errmsg("invalid input syntax for type %s : %s", "yaml", error)));
}

/*
 * pg_parse_yaml_or_ereport
 *
 * This function is like pg_parse_json, except that it does not return a
 * JsonParseErrorType. Instead, in case of any failure, this function will
 * ereport(ERROR).
 */
void
pg_parse_yaml_or_ereport(YamlContext *yamlContext)
{
  yaml_event_t      event;
  yaml_event_type_t event_type;

  if(!yaml_parser_initialize(&(yamlContext->parser)))
    ereport(ERROR, (errmsg("Could not create parser for YAML")));

  yaml_parser_set_input_string(
    &yamlContext->parser,
    (const unsigned char*)yamlContext->input,
    yamlContext->input_length
  );

  ereport(LOG, (errmsg("YAML: %s\n", yamlContext->input)));
  ereport(LOG, (errmsg("YAML Length: %d\n", yamlContext->input_length)));

  do {
      if (!yaml_parser_parse(&(yamlContext->parser), &event)) {
          yaml_ereport_error(yamlContext->parser.problem);
      }
      event_type = event.type;
      ereport(LOG, (errmsg("EVENT TYPE: %d\n", event_type)));

      yaml_event_delete(&event);
  }
  while (event_type != YAML_STREAM_END_EVENT);

  yaml_parser_delete(&(yamlContext->parser));
  ereport(LOG, (errmsg("yaml_parser_load() said document was valid")));
}

/*
 * makeYamlContext
 *
 * This is like makeJsonLexContextCstringLen, but it accepts a text value
 * directly.
 */
YamlContext *
makeYamlContext(text *yaml, bool need_escapes)
{
	return makeYamlContextCstringLen(
                    VARDATA_ANY(yaml),
										VARSIZE_ANY_EXHDR(yaml),
										GetDatabaseEncoding(),
										need_escapes);
}



