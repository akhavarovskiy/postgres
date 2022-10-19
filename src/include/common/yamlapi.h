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

/** Yaml Error types are copies of types in yaml.h */
typedef yaml_token_type_t YamlTokenType;
typedef yaml_error_type_t YamlParseErrorType;

typedef struct YamlContext {
  yaml_parser_t   parser;
  yaml_document_t document;
  yaml_token_t    token;
  char *          input;
	int             input_length;
} YamlContext;


/*
 * pg_parse_yaml
 *
 * Publicly visible entry point for the YAML parser.
 *
 * lex is a lexing context, set up for the YAML to be processed by calling
 * makeYAMLContext(). sem is a structure of function pointers to semantic
 * action routines to be called at appropriate spots during parsing, and a
 * pointer to a state object to be passed to those routines.
 */

YamlParseErrorType pg_parse_yaml(YamlContext *context);
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
