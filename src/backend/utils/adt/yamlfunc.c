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


static int
yaml_find_key_on_top_scope(YamlContext *yamlContext, const char *key)
{
  int n, done = 0, scope = -1;
  unsigned int kv_toggle = 0; // Key = 0, Value = 1

  while(!done) {
      yaml_event_t event;
      if (!yaml_parser_parse(&(yamlContext->parser), &event))
          return 0;

      switch (event.type) {
      case YAML_NO_EVENT:
          break;
      case YAML_STREAM_START_EVENT:
          break;
      case YAML_STREAM_END_EVENT:
          done = 1;
          break;
      case YAML_DOCUMENT_START_EVENT:
          break;
      case YAML_DOCUMENT_END_EVENT:
          done = 1;
          break;
      case YAML_SEQUENCE_START_EVENT:
          scope++;
          break;
      case YAML_SEQUENCE_END_EVENT:
          scope--;
          kv_toggle = 0;
          break;
      case YAML_MAPPING_START_EVENT:
          scope++;
          break;
      case YAML_MAPPING_END_EVENT:
          scope--;
          kv_toggle = 0;
          break;
      case YAML_ALIAS_EVENT:
          break;
      case YAML_SCALAR_EVENT:
          if(scope != 0)
              continue;
          if(kv_toggle == 0) {
              kv_toggle = !kv_toggle;
              n = strncmp((const char*)event.data.scalar.value, key, strlen(key));
              if(n == 0) {
                  return 1;
              }
          } else {
              kv_toggle = 0;
          }
          break;
      }
      yaml_event_delete(&event);
  }
  return 0;
}

static void print_emitter_error(yaml_emitter_t* emitter, int line)
{
    switch (emitter->error) {
    case YAML_MEMORY_ERROR:
        ereport(ERROR, (errmsg("Memory error: Not enough memory for emitting")));
        break;
    case YAML_WRITER_ERROR:
        ereport(ERROR, (errmsg("Writer error (line %d): %s\n", line, emitter->problem)));
        break;
    case YAML_EMITTER_ERROR:
        ereport(ERROR, (errmsg("Emitter error (line %d): %s\n", line, emitter->problem)));
        break;
    default:
        /* Couldn't happen. */
        ereport(ERROR, (errmsg("Internal error")));
        break;
    }
}

text * yaml_get_sub_tree(YamlContext * context, char * path)
{
    /** Create the emitter*/
    yaml_event_t event;
    unsigned int scope = 0, done  = 0;
    yaml_emitter_t emitter;
    yaml_emitter_initialize(&emitter);
    yaml_emitter_set_canonical(&emitter, 0);
    yaml_emitter_set_unicode(&emitter, 1);
    yaml_emitter_set_indent(&emitter, 2);

    size_t size_written = 0;
    size_t allocated_bytes = emitter.buffer.end - emitter.buffer.start;
    unsigned char * buffer = (unsigned char*)palloc0(allocated_bytes);
    yaml_emitter_set_output_string(&emitter, buffer, allocated_bytes, &size_written);

    /** Initialize Document */
    yaml_event_t document_start_event;
    yaml_event_t document_end_event;
    yaml_event_t stream_start_event;
    yaml_event_t stream_end_event;
    yaml_event_t mapping_start_event;
    yaml_event_t mapping_end_event;
    yaml_event_t sequence_start_event;
    yaml_event_t sequence_end_event;

    yaml_version_directive_t document_version_directive = {
        /*Major*/ 1,
        /*Minor*/ 2
    };
    /** Creat the starting emitter events */
    assert(
        yaml_document_start_event_initialize(&document_start_event,
                        &document_version_directive,
                        NULL,
                        NULL,
                        1) != 0
    );
    assert(yaml_document_end_event_initialize(&document_end_event, 1) != 0);
    assert(yaml_stream_start_event_initialize(&stream_start_event, YAML_UTF8_ENCODING) != 0);
    assert(yaml_stream_end_event_initialize(&stream_end_event) != 0);

    /** Emit the opening events for the YAML document */
    if(!yaml_mapping_start_event_initialize(&mapping_start_event, NULL, NULL, 1, YAML_ANY_MAPPING_STYLE))
        print_emitter_error(&emitter, __LINE__);
    if(!yaml_mapping_end_event_initialize(&mapping_end_event))
        print_emitter_error(&emitter, __LINE__);

    /** Emit the start of the document and the stream */
    if(!yaml_emitter_emit(&emitter, &stream_start_event))
        print_emitter_error(&emitter, __LINE__);
    if(!yaml_emitter_emit(&emitter, &document_start_event))
        print_emitter_error(&emitter, __LINE__);

    /** Get the next event after the key, the next event determines the structure of the keys value. */
    if (!yaml_parser_parse(&(context->parser), &event)) {
        exit(EXIT_FAILURE);
    }

    switch (event.type) {
    /* SCALAR (Value) Event is handled here */
    case YAML_SCALAR_EVENT:
        if(!yaml_emitter_emit(&emitter, &event))
            print_emitter_error(&emitter, __LINE__);
        break;

    /* SEQUENCE (Array) Event is handled here */
    case YAML_SEQUENCE_START_EVENT:
        scope++;
        if(!yaml_emitter_emit(&emitter, &event))
            print_emitter_error(&emitter, __LINE__);

        while(!done)
        {
            if (!yaml_parser_parse(&(context->parser), &event))
                exit(EXIT_FAILURE);

            switch(event.type)
            {
            case YAML_SEQUENCE_START_EVENT:
                scope++;
                if((allocated_bytes - size_written) < 16384) {
                    allocated_bytes = allocated_bytes << 1;
                    buffer = (unsigned char*)repalloc(buffer, allocated_bytes);
                    emitter.output.string.buffer = buffer;
                    emitter.output.string.size   = allocated_bytes;
                }
                if(!yaml_emitter_emit(&emitter, &event)) {
                    print_emitter_error(&emitter, __LINE__);
                }
                break;
            case YAML_SEQUENCE_END_EVENT:
                scope--;
                if((allocated_bytes - size_written) < 16384) {
                    allocated_bytes = allocated_bytes << 1;
                    buffer = (unsigned char*)repalloc(buffer, allocated_bytes);
                    emitter.output.string.buffer = buffer;
                    emitter.output.string.size   = allocated_bytes;
                }
                if(!yaml_emitter_emit(&emitter, &event)) {
                    print_emitter_error(&emitter, __LINE__);
                }
                break;
            case YAML_STREAM_END_EVENT:
                done = 1;
                break;
            default:
                if((allocated_bytes - size_written) < 16384) {
                    allocated_bytes = allocated_bytes << 1;
                    buffer = (unsigned char*)repalloc(buffer, allocated_bytes);
                    emitter.output.string.buffer = buffer;
                    emitter.output.string.size   = allocated_bytes;
                }
                if(!yaml_emitter_emit(&emitter, &event)) {
                    print_emitter_error(&emitter, __LINE__);
                }
                break;
            }
            if(scope == 0) {
                done = 1;
            }
            if((allocated_bytes - size_written) < 16384) {
                allocated_bytes = allocated_bytes << 1;
                buffer = (unsigned char*)repalloc(buffer, allocated_bytes);
                emitter.output.string.buffer = buffer;
                emitter.output.string.size   = allocated_bytes;
            }
        }
        break;

    /* MAPPING (Object) Event is handled here */
    case YAML_MAPPING_START_EVENT:
        scope++;
        if(!yaml_emitter_emit(&emitter, &mapping_start_event))
            print_emitter_error(&emitter, __LINE__);

        while(!done) {
            if (!yaml_parser_parse(&(context->parser), &event))
                break;

            switch(event.type) {
            case YAML_MAPPING_START_EVENT:
                scope++;
                break;
            case YAML_MAPPING_END_EVENT:
                scope--;
                break;
            case YAML_STREAM_END_EVENT:
                done = 1;
                break;
            default:
                break;
            }
            if(scope == 0) {
                done = 1;
            } else {
                if((allocated_bytes - size_written) < 16384) {
                    allocated_bytes = allocated_bytes << 1;
                    buffer = (unsigned char*)repalloc(buffer, allocated_bytes);
                    emitter.output.string.buffer = buffer;
                    emitter.output.string.size   = allocated_bytes;
                }
                if(!yaml_emitter_emit(&emitter, &event))
                    print_emitter_error(&emitter, __LINE__);
            }
        }
        if(!yaml_emitter_emit(&emitter, &mapping_end_event))
            print_emitter_error(&emitter, __LINE__);

        if((allocated_bytes - size_written) < 16384) {
            allocated_bytes = allocated_bytes << 1;
            buffer = (unsigned char*)repalloc(buffer, allocated_bytes);
            emitter.output.string.buffer = buffer;
            emitter.output.string.size   = allocated_bytes;
        }
        break;
    default:
        break;
    }
    yaml_emitter_emit(&emitter, &document_end_event);
    yaml_emitter_emit(&emitter, &stream_end_event);
    yaml_emitter_flush(&emitter);
    /** Cleanup */
    yaml_event_delete(&mapping_start_event);
    yaml_event_delete(&mapping_end_event);
    yaml_event_delete(&stream_start_event);
    yaml_event_delete(&stream_end_event);
    yaml_emitter_delete(&emitter);

    if(size_written == 0)
      return NULL;
    return cstring_to_text_with_len((const char*)(&buffer[14]), size_written - 14);
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
	text	   *result = NULL;
  int       key_found;

  YamlContext * yamlContext = makeYamlContext(yaml, false);

  key_found = yaml_find_key_on_top_scope(yamlContext, pathstr);
  if(key_found) {
    result = yaml_get_sub_tree(yamlContext, pathstr);
  }
	if (result != NULL)
		PG_RETURN_TEXT_P(result);
	else
		PG_RETURN_NULL();
}