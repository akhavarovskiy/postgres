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

#define INDENT "  "
#define STRVAL(x) ((x) ? (char*)(x) : "")

void indent(int level)
{
    int i;
    for (i = 0; i < level; i++) {
        printf("%s", INDENT);
    }
}

void print_yaml_tree(YamlContext *context)
{
    static int level = 0;

    printf("====================================\n");
    printf(" Printing YAML object Tree : %d\n", context->events_length);
    printf("====================================\n");

    for(unsigned int i = 0; i < context->events_length; i++)
    {
        yaml_event_t *event = context->events[i];
        switch (event->type) {
        case YAML_NO_EVENT:
            indent(level);
            printf("no-event (%d)\n", event->type);
            break;
        case YAML_STREAM_START_EVENT:
            indent(level++);
            printf("stream-start-event (%d)\n", event->type);
            break;
        case YAML_STREAM_END_EVENT:
            indent(--level);
            printf("stream-end-event (%d)\n", event->type);
            break;
        case YAML_DOCUMENT_START_EVENT:
            indent(level++);
            printf("document-start-event (%d)\n", event->type);
            break;
        case YAML_DOCUMENT_END_EVENT:
            indent(--level);
            printf("document-end-event (%d)\n", event->type);
            break;
        case YAML_ALIAS_EVENT:
            indent(level);
            printf("alias-event (%d)\n", event->type);
            break;
        case YAML_SCALAR_EVENT:
            indent(level);
            printf("scalar-event (%d) = {value=\"%s\", length=%d}\n",
                event->type,
                event->data.scalar.value,
                (int)event->data.scalar.length);
            break;
        case YAML_SEQUENCE_START_EVENT:
            indent(level++);
            printf("sequence-start-event (%d)\n", event->type);
            break;
        case YAML_SEQUENCE_END_EVENT:
            indent(--level);
            printf("sequence-end-event (%d)\n", event->type);
            break;
        case YAML_MAPPING_START_EVENT:
            indent(level++);
            printf("mapping-start-event (%d)\n", event->type);
            break;
        case YAML_MAPPING_END_EVENT:
            indent(--level);
            printf("mapping-end-event (%d)\n", event->type);
            break;
        }
        if (level < 0) {
            fprintf(stderr, "indentation underflow!\n");
            level = 0;
        }
    }
}

void
cleanYamlContext(YamlContext *context)
{
    for(unsigned int i = 0; i < context->events_length; i++)
    {
        yaml_event_delete(context->events[i]);
        pfree(context->events[i]);
    }
    pfree(context->events);
    yaml_parser_delete(&context->parser);
    pfree(context);
}

/*
 * yaml_count_array_size
 *
 * Counts the elements within a sequence object.
 */
int
yaml_count_array_size(YamlContext *context)
{
    int scope = 0;
    int count = 0;
    assert(context->events[0]->type == YAML_STREAM_START_EVENT);
    assert(context->events[1]->type == YAML_DOCUMENT_START_EVENT);

    for(unsigned int i = 2; i < context->events_length; i++)
    {
        yaml_event_t* event = context->events[i];
        switch (event->type)
        {
        case YAML_SCALAR_EVENT:
            if(scope == 1)
                count++;
            break;

        case YAML_SEQUENCE_START_EVENT:
            if(scope == 1)
                count++;
            scope++;
            break;

        case YAML_SEQUENCE_END_EVENT:
            scope--;
            break;

        case YAML_MAPPING_START_EVENT:
            if(scope == 1)
                count++;
            scope++;
            break;

        case YAML_MAPPING_END_EVENT:
            scope--;
            break;
        default:
            break;
        }
        if(scope == 0)
            break;
    }
    return count;
}

/*
 * yaml_get_object_type
 *
 * Given a YAML object, returns the type of the object.
 */
text *yaml_get_object_type(YamlContext *context)
{
    assert(context->events[0]->type == YAML_STREAM_START_EVENT);
    assert(context->events[1]->type == YAML_DOCUMENT_START_EVENT);
    switch (context->events[2]->type)
    {
    case YAML_SCALAR_EVENT:
        return cstring_to_text("scalar");

    case YAML_SEQUENCE_START_EVENT:
        return cstring_to_text("sequence");

    case YAML_MAPPING_START_EVENT:
        return cstring_to_text("mapping");

    default:
        return cstring_to_text("Unknown Type");
    }
}

static int
yaml_find_key_on_root(YamlContext *context, const char *key)
{
    int n, scope = -1;
    unsigned int kv_toggle = 0; // Key = 0, Value = 1

    for(unsigned int i = 0; i < context->events_length; i++)
    {
        yaml_event_t* event = context->events[i];
        switch (event->type) {
        case YAML_NO_EVENT:
            break;
        case YAML_STREAM_START_EVENT:
            break;
        case YAML_STREAM_END_EVENT:
            break;
        case YAML_DOCUMENT_START_EVENT:
            break;
        case YAML_DOCUMENT_END_EVENT:
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
                n = strncmp((const char*)event->data.scalar.value, key, strlen(key));
                if(n == 0) {
                    return i + 1;
                }
            } else {
                kv_toggle = 0;
            }
            break;
        }
    }
    return -1;
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

text * yaml_get_sub_tree(YamlContext * context, int location)
{
    /** Create the emitter*/
    unsigned int scope = 0, done  = 0;
    yaml_emitter_t emitter;
    text * result;
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

    yaml_version_directive_t document_version_directive = {
        /*Major*/ 1,
        /*Minor*/ 1
    };
    /** Creat the starting emitter events */
    assert(yaml_document_start_event_initialize(&document_start_event,&document_version_directive, NULL, NULL, 1) != 0);
    assert(yaml_document_end_event_initialize(&document_end_event, 1) != 0);
    assert(yaml_stream_start_event_initialize(&stream_start_event, YAML_UTF8_ENCODING) != 0);
    assert(yaml_stream_end_event_initialize(&stream_end_event) != 0);

    /** Emit the start of the document and the stream */
    if(!yaml_emitter_emit(&emitter, &stream_start_event))
        print_emitter_error(&emitter, __LINE__);
    if(!yaml_emitter_emit(&emitter, &document_start_event))
        print_emitter_error(&emitter, __LINE__);

    for(int i = location; (i < context->events_length) && (done == 0); i++)
    {
        int status;
        yaml_event_t event_copy;
        yaml_event_t * event = context->events[i];
        switch (event->type)
        {
        case YAML_SCALAR_EVENT:
            status = yaml_scalar_event_initialize(
                &event_copy,
                event->data.scalar.anchor,
                event->data.scalar.tag,
                event->data.scalar.value,
                event->data.scalar.length,
                event->data.scalar.style,
                event->data.scalar.quoted_implicit,
                event->data.scalar.style
            );
            if(status == 0) {
                print_emitter_error(&emitter, __LINE__);
            }
            if(!yaml_emitter_emit(&emitter, &event_copy))
                print_emitter_error(&emitter, __LINE__);
            if(scope == 0) { done = 1; }
            break;

        case YAML_SEQUENCE_START_EVENT:
            scope++;
            status = yaml_sequence_start_event_initialize(
                &event_copy,
                event->data.sequence_start.anchor,
                event->data.sequence_start.tag,
                event->data.sequence_start.implicit,
                event->data.sequence_start.style
            );
            if(status == 0) {
                print_emitter_error(&emitter, __LINE__);
            }
            if(!yaml_emitter_emit(&emitter, &event_copy))
                print_emitter_error(&emitter, __LINE__);
            break;
        case YAML_SEQUENCE_END_EVENT:
            scope--;
            status = yaml_sequence_end_event_initialize(&event_copy);
            if(status == 0) {
                print_emitter_error(&emitter, __LINE__);
            }
            if(!yaml_emitter_emit(&emitter, &event_copy))
                print_emitter_error(&emitter, __LINE__);
            if(scope == 0) { done = 1; break; }
            break;
        case YAML_MAPPING_START_EVENT:
            scope++;
            status = yaml_mapping_start_event_initialize(
                &event_copy,
                event->data.mapping_start.anchor,
                event->data.mapping_start.tag,
                event->data.mapping_start.implicit,
                event->data.mapping_start.style
            );
            if(status == 0) {
                print_emitter_error(&emitter, __LINE__);
            }
            if(!yaml_emitter_emit(&emitter, &event_copy))
                print_emitter_error(&emitter, __LINE__);
            break;
        case YAML_MAPPING_END_EVENT:
            scope--;
            status = yaml_mapping_end_event_initialize(&event_copy);
            if(status == 0) {
                print_emitter_error(&emitter, __LINE__);
            }
            if(!yaml_emitter_emit(&emitter, &event_copy))
                print_emitter_error(&emitter, __LINE__);
            if(scope == 0) { done = 1; break; }
            break;
        case YAML_STREAM_START_EVENT:
        case YAML_DOCUMENT_START_EVENT:
            break;

        case YAML_STREAM_END_EVENT:
        case YAML_DOCUMENT_END_EVENT:
            done = 1;
            break;
        default:
            break;
        }

        /** Resize buffer if out of space */
        if((allocated_bytes - size_written) < 16384) {
            allocated_bytes = allocated_bytes << 1;
            buffer = (unsigned char*)repalloc(buffer, allocated_bytes);
            // Update the emitter to use the new buffer
            emitter.output.string.buffer = buffer;
            emitter.output.string.size   = allocated_bytes;
        }
    }

    if(!yaml_emitter_emit(&emitter, &document_end_event))
        print_emitter_error(&emitter, __LINE__);
    if(!yaml_emitter_emit(&emitter, &stream_end_event))
        print_emitter_error(&emitter, __LINE__);
    if(!yaml_emitter_flush(&emitter))
        print_emitter_error(&emitter, __LINE__);

    /** Cleanup */
    yaml_event_delete(&stream_start_event);
    yaml_event_delete(&stream_end_event);
    yaml_emitter_delete(&emitter);

    if(size_written == 0) {
        pfree(buffer);
        return NULL;
    }

    result = cstring_to_text_with_len((const char*)(&buffer[14]), size_written - 14);
    pfree(buffer);
    return result;
}

/*
 * yaml getter functions
 * these implement the -> ->> #> and #>> operators
 * and the json{b?}_extract_path*(json, text, ...) functions
 */
Datum
yaml_object_field(PG_FUNCTION_ARGS)
{
    text   *yaml    = PG_GETARG_TEXT_PP(0);
    text   *path    = PG_GETARG_TEXT_PP(1);
    char   *pathstr = text_to_cstring(path);
    text   *result  = NULL;
    int     child_location;

    YamlContext * context = makeYamlContext(yaml, false);
    pg_parse_yaml_or_ereport(context);

    child_location = yaml_find_key_on_root(context, pathstr);
    if(child_location != -1) {
        result = yaml_get_sub_tree(context, child_location);
    }
    pfree(pathstr);
    cleanYamlContext(context);
    if (result != NULL)
        PG_RETURN_TEXT_P(result);
    else
        PG_RETURN_NULL();
}

Datum
yaml_object_field_text(PG_FUNCTION_ARGS)
{
    text   *yaml    = PG_GETARG_TEXT_PP(0);
    text   *path    = PG_GETARG_TEXT_PP(1);
    char   *pathstr = text_to_cstring(path);
    text   *result  = NULL;
    int     child_location;

    YamlContext * context = makeYamlContext(yaml, false);
    pg_parse_yaml_or_ereport(context);

    child_location = yaml_find_key_on_root(context, pathstr);
    if(child_location != -1) {
        result = yaml_get_sub_tree(context, child_location);
    }
    pfree(pathstr);
    cleanYamlContext(context);
    if (result != NULL)
        PG_RETURN_TEXT_P(result);
    else
        PG_RETURN_NULL();
}

Datum
yaml_sequence_elements(PG_FUNCTION_ARGS)
{
    // int   event_type, scope = 0;
    Datum values[1];
    bool  nulls [1] = {false};
    // Get the yaml text
    text* yaml           = PG_GETARG_TEXT_PP(0);
    // Create the context
    YamlContext* context = makeYamlContext(yaml, false);
    // Parse the yaml and check for errors
    pg_parse_yaml_or_ereport(context);
    // Make sure the root is a sequence
    text* type           = yaml_get_object_type(context);
    if(strncmp(VARDATA_ANY(type), "sequence", VARSIZE_ANY_EXHDR(type)) != 0) {
        ereport(ERROR,
            (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("ERROR: cannot get sequence length of a non-sequence")));
    }
    // Setup return table properties
    SetSingleFuncCall(fcinfo, SRF_SINGLE_USE_EXPECTED | SRF_SINGLE_BLESS);
    ReturnSetInfo * rsi = (ReturnSetInfo *)fcinfo->resultinfo;

    int scope = 0;
    for(unsigned int i = 3; i < context->events_length; i++)
    {
        yaml_event_t* event = context->events[i];
        switch(event->type)
        {
            case YAML_SEQUENCE_START_EVENT:
            case YAML_MAPPING_START_EVENT:
                if(scope == 0) {
                    text* val = yaml_get_sub_tree(context, i);
                    values[0] = PointerGetDatum(val);
                    HeapTuple tuple = heap_form_tuple(rsi->setDesc, values, nulls);
                    tuplestore_puttuple(rsi->setResult, tuple);
                }
                scope++;
                break;
            case YAML_SEQUENCE_END_EVENT:
                scope--;
                break;
            case YAML_MAPPING_END_EVENT:
                scope--;
                break;
            case YAML_SCALAR_EVENT:
                if(scope == 0)
                {
                    text* val = cstring_to_text_with_len(
                        (const char*)event->data.scalar.value,
                        event->data.scalar.length
                    );
                    values[0] = PointerGetDatum(val);
                    HeapTuple tuple = heap_form_tuple(rsi->setDesc, values, nulls);
                    tuplestore_puttuple(rsi->setResult, tuple);
                }
                break;
            default:
                break;
        }
    }
    cleanYamlContext(context);
    PG_RETURN_NULL();
}



