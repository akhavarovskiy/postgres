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
	return yamlContext;
}
