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
#include <assert.h>
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
 	switch(error) {
	case YAML_READER_ERROR:
	case YAML_SCANNER_ERROR:
	case YAML_PARSER_ERROR:
	case YAML_COMPOSER_ERROR:
		ereport(ERROR,
			(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
			errmsg("%s : %s", context->parser.problem, context->parser.context)));
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
	YamlParseErrorType err;
	err = pg_parse_yaml(yamlContext);
	if (err)
		yaml_ereport_error(err, yamlContext);
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
  // yaml_token_t token;
  // int n;
  // int status;
  // int starting_column;

  // // Get the initial token
  // status = yaml_parser_scan(&(context->parser), &token);
  // if (!status) {
  //   yaml_ereport_error(context->parser.error, context);
  // }
  // // Get the initial colum index
  // starting_column = token.start_mark.column;

  // // Loop through the document ignoring key values
  // while(token.type != YAML_STREAM_END_TOKEN)
  // {
  //   if(starting_column != token.start_mark.column)
  //     continue;

  //   switch(token.type)
  //   {
  //     case YAML_KEY_TOKEN:
  //     {
  //       // Delete the current token
  //       yaml_token_delete(&token);

  //       // Get the next token (SCALAR)
  //       status = yaml_parser_scan(&(context->parser), &token);
  //       if (!status) {
  //         yaml_ereport_error(context->parser.error, context);
  //       }
  //       n = strncmp(path, (char*)token.data.scalar.value, token.data.scalar.length);
  //       if(n == 0) {
  //         return cstring_to_text_with_len("Key Found", strlen("Key Found"));
  //       }
  //       break;
  //     }
  //     default:
  //       break;
  //   }
  //   if(token.type != YAML_STREAM_END_TOKEN) {
  //     yaml_token_delete(&token);
  //     status = yaml_parser_scan(&(context->parser), &token);
  //     if (!status) {
  //       yaml_ereport_error(context->parser.error, context);
  //     }
  //   }
  // }
  // yaml_parser_delete(&(context->parser));
	// return cstring_to_text_with_len("Key Not Found", strlen("Key Not Found"));

	yaml_token_t token;
	int n;
	int status;
	int starting_column = -1;
	
	do {
		yaml_parser_scan(&(context->parser), &token);

		if(starting_column == -1)
			starting_column = token.start_mark.column;

		if(starting_column != token.start_mark.column)
			continue;

		switch(token.type) {
		case YAML_KEY_TOKEN:
		{
			/* If we are next to a key token the next value is a scalar */
			yaml_parser_scan(&(context->parser), &token);
			assert(token.type == YAML_SCALAR_TOKEN);

			if(token.data.scalar.length != strlen(path))
				continue;

			n = strncmp(path, (char*)token.data.scalar.value, token.data.scalar.length);
      			
			if(n == 0) {
				yaml_token_delete(&token);
				return cstring_to_text_with_len("Key Found", strlen("Key Found"));
			}
			break;
		}
		default:
			break;
		}
		
		if(token.type != YAML_STREAM_END_TOKEN)
			yaml_token_delete(&token);

	} while(token.type != YAML_STREAM_END_TOKEN);

	yaml_token_delete(&token);
	
	return cstring_to_text_with_len("Key Not Found", strlen("Key Not Found"));
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
	result = yaml_get_sub_tree(yamlContext, pathstr);

	if (result != NULL)
		PG_RETURN_TEXT_P(result);
	else
		PG_RETURN_NULL();
}
