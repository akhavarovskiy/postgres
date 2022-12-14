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
#include "common/yamlapi.h"
#include "utils/yaml.h"
#include "utils/yamlfunc.h"

#include <yaml.h>
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
	YamlContext *yamlContext     = palloc0(sizeof(YamlContext));
	yamlContext->input           = yaml;
	yamlContext->input_length    = len;

	/** Create a parser */
	if(!yaml_parser_initialize(&(yamlContext->parser)))
		ereport(ERROR, (errmsg("Could not create parser for YAML")));

	/** Set input for the parser */
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
	int status;
	yaml_event_t* p_event;

	// Create an array to store yaml events as they are parsed.
	context->events_capacity = 256;
	context->events_length   = 0;
	context->events          = palloc0(
		sizeof(yaml_event_t*) * context->events_capacity
	);

	do {
		p_event = palloc0(sizeof(yaml_event_t));

		/** Parse the YAML file */
		status = yaml_parser_parse(&(context->parser), p_event);
		if(!status)
			return context->parser.error;

		/** Realloc the events array for a grater size */
		if(context->events_length == context->events_capacity) {
			context->events_capacity *= 2;
			context->events           = repalloc(
				context->events,
				sizeof(yaml_event_t*) * context->events_capacity
			);
		}
		context->events[context->events_length++] = p_event;
	}
	while (p_event->type != YAML_STREAM_END_EVENT);
	return YAML_NO_ERROR;
}
