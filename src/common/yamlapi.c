/*-------------------------------------------------------------------------
 *
 * yamlapi.c
 *		yaml data type support.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/utils/adt/yamlapi.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <yaml.h>
#include "common/yamlapi.h"
#include "utils/yaml.h"
#include "utils/yamlfunc.h"

/*
 * makeYamlContextCstringLen
 *
 * Constructor, with or without StringInfo object for de-escaped lexemes.
 *
 * Without is better as it makes the processing faster, so only make one
 * if really required.
 */
YamlContext *
makeYamlContextCstringLen(char *yaml, int len, int encoding, bool need_escapes)
{
	YamlContext *yamlContext = palloc0(sizeof(YamlContext));

	yamlContext->input = yaml;
	yamlContext->input_length = len;

	if(!yaml_parser_initialize(&(yamlContext->parser)))
    ereport(ERROR, (errmsg("Could not create parser for YAML")));

  yaml_parser_set_input_string(
    &yamlContext->parser,
    (const unsigned char*)yamlContext->input,
    yamlContext->input_length
  );
	return yamlContext;
}


/*
 * pg_parse_YAML
 *
 * Publicly visible entry point for the YAML parser.
 *
 * lex is a lexing context, set up for the YAML to be processed by calling
 * makeYAMLContext(). sem is a structure of function pointers to semantic
 * action routines to be called at appropriate spots during parsing, and a
 * pointer to a state object to be passed to those routines.
 */
YamlParseErrorType
pg_parse_yaml(YamlContext *context)
{
	yaml_event_t      event;
  yaml_event_type_t event_type;
  do {
      int status = yaml_parser_parse(&(context->parser), &event);
      if (!status) {
        return context->parser.error;
      }
      event_type = event.type;
      yaml_event_delete(&event);
  }
  while (event_type != YAML_STREAM_END_EVENT);
  yaml_parser_delete(&(context->parser));
	return YAML_NO_ERROR;
}