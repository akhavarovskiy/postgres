/*-------------------------------------------------------------------------
 *
 * yamlapi.h
 *	  Declarations for YAML API support.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/common/yamlapi.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef YAMLAPI_H
#define YAMLAPI_H

#include <yaml.h>

typedef struct YamlContext {
  yaml_parser_t   parser;
  yaml_document_t document;
  yaml_token_t    token;
  char *          input;
	int             input_length;
} YamlContext;


/*
 * makeYamlContextCstringLen
 *
 * Constructor, with or without StringInfo object for de-escaped lexemes.
 *
 * Without is better as it makes the processing faster, so only make one
 * if really required.
 */
extern YamlContext * makeYamlContextCstringLen(char *yaml, int len, int encoding, bool need_escapes);

#endif /* YAMLAPI_H */
