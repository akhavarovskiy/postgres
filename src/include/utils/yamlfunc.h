/*-------------------------------------------------------------------------
 *
 * yamlfunc.h
 *	  Declarations for JSON data type support.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/utils/yamlfunc.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef YAMLFUNC_H
#define YAMLFUNC_H

extern void pg_parse_yaml_or_ereport(YamlContext *yamlContext);

extern YamlContext *makeYamlContext(text *yaml, bool need_escapes);

extern void yaml_ereport_error(YamlParseErrorType error, YamlContext* context);

extern text * yaml_get_sub_structure(YamlContext * context, char * path);

extern text *yaml_get_object_type(YamlContext *context);

#endif