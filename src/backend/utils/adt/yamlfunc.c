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
void yaml_ereport_error(YamlParseErrorType error, YamlContext* context)
{
  ereport(LOG,(errmsg("YAML Error code : %d\n", error)));
  switch(error)
  {
    case YAML_NO_ERROR:
      return;

    case YAML_READER_ERROR:
    case YAML_SCANNER_ERROR:
    case YAML_PARSER_ERROR:
    case YAML_COMPOSER_ERROR:
      ereport(ERROR,
        (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
            errmsg("Invalid input syntax for type YAML : %s", context->parser.problem)));
      break;

    default:
      ereport(ERROR,
        (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
            errmsg("YAML parser is in an invalid state : %s", context->parser.problem)));
      break;
  }
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
    YamlParseErrorType result;
    result = pg_parse_yaml(yamlContext);
    if (result != YAML_NO_ERROR) {
      yaml_ereport_error(result, yamlContext);
    }
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



text * yaml_get_sub_tree(YamlContext * context, char * path)
{

}
/*
 * yaml getter functions
 * these implement the -> ->> #> and #>> operators
 * and the json{b?}_extract_path*(json, text, ...) functions
 */


Datum
yaml_object_field(PG_FUNCTION_ARGS)
{
	text	   *yaml = PG_GETARG_TEXT_PP(0);
	text	   *path = PG_GETARG_TEXT_PP(1);
	char	   *pathstr = text_to_cstring(path);
	text	   *result;

  YamlContext * yamlContext = makeYamlContext(yaml, false);
  pg_parse_yaml_or_ereport(yamlContext);

	if (result != NULL)
		PG_RETURN_TEXT_P(result);
	else
		PG_RETURN_NULL();
}