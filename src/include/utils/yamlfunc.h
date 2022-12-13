/*-------------------------------------------------------------------------
 *
 * yamlfunc.h
 *	  Declarations for YAML data type support.
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

extern text * yaml_get_sub_tree(YamlContext * context, int location);

extern text *yaml_get_object_type(YamlContext *context);

extern int yaml_count_array_size(YamlContext *context);


#endif