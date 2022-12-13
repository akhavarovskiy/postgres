/*-------------------------------------------------------------------------
 *
 * yaml.c
 *		yaml data type support.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/utils/adt/yaml.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "common/yamlapi.h"
#include "catalog/pg_type.h"
#include "funcapi.h"
#include "libpq/pqformat.h"
#include "miscadmin.h"
#include "parser/parse_coerce.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/date.h"
#include "utils/datetime.h"
#include "utils/yaml.h"
#include "utils/yamlfunc.h"
#include "utils/lsyscache.h"
#include "utils/typcache.h"

/*
 * Input.
 */
PG_FUNCTION_INFO_V1(yaml_in);

Datum
yaml_in(PG_FUNCTION_ARGS)
{
	char	   *yaml = PG_GETARG_CSTRING(0);
	text	   *result = cstring_to_text(yaml);

	YamlContext *yamlContext;

	/* validate it */
	yamlContext = makeYamlContext(result, false);
	pg_parse_yaml_or_ereport(yamlContext);

	/* Internal representation is the same as text, for now */
	PG_RETURN_TEXT_P(result);
}

/*
 * Output.
 */
PG_FUNCTION_INFO_V1(yaml_out);

Datum
yaml_out(PG_FUNCTION_ARGS)
{
	/* we needn't detoast because text_to_cstring will handle that */
	Datum		txt = PG_GETARG_DATUM(0);

	PG_RETURN_CSTRING(TextDatumGetCString(txt));
}

/*
 * Binary send.
 */
PG_FUNCTION_INFO_V1(yaml_send);

Datum
yaml_send(PG_FUNCTION_ARGS)
{
	text	   *t = PG_GETARG_TEXT_PP(0);
	StringInfoData buf;

	pq_begintypsend(&buf);
	pq_sendtext(&buf, VARDATA_ANY(t), VARSIZE_ANY_EXHDR(t));
	PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

/*
 * Binary receive.
 */
PG_FUNCTION_INFO_V1(yaml_recv);

Datum
yaml_recv(PG_FUNCTION_ARGS)
{
	StringInfo	buf = (StringInfo) PG_GETARG_POINTER(0);
	char	   *str;
	int			nbytes;

	str = pq_getmsgtext(buf, buf->len - buf->cursor, &nbytes);

	PG_RETURN_TEXT_P(cstring_to_text_with_len(str, nbytes));
}


/** Get the type YAML */
Datum
yaml_typeof(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	text	   *yaml;
	yaml = PG_GETARG_TEXT_PP(0);

	YamlContext* context = makeYamlContext(yaml, false);
	pg_parse_yaml_or_ereport(context);

	PG_RETURN_TEXT_P(
		yaml_get_object_type(context)
	);
}

Datum
yaml_sequence_length(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	text	   *yaml;
	yaml = PG_GETARG_TEXT_PP(0);

	YamlContext* context = makeYamlContext(yaml, false);
	pg_parse_yaml_or_ereport(context);

	text* type = yaml_get_object_type(context);

	if(strncmp(VARDATA_ANY(type), "sequence", VARSIZE_ANY_EXHDR(type)) != 0) {
		ereport(ERROR,
			(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("ERROR: cannot get sequence length of a non-sequence")));
	}
	PG_RETURN_INT32(
		yaml_count_array_size(context)
	);
}


static void
composite_to_yaml(Datum composite, StringInfo result, bool use_line_feeds)
{
	HeapTupleHeader td;
	Oid			tupType;
	int32		tupTypmod;
	TupleDesc	tupdesc;
	HeapTupleData tmptup,
			   *tuple;
	int			i;
	bool		needsep = false;
	bool		typisvarlena;
	const char *sep;

	sep = use_line_feeds ? ",\n " : ",";

	td = DatumGetHeapTupleHeader(composite);

	/* Extract rowtype info and find a tupdesc */
	tupType   = HeapTupleHeaderGetTypeId(td);
	tupTypmod = HeapTupleHeaderGetTypMod(td);
	tupdesc   = lookup_rowtype_tupdesc(tupType, tupTypmod);

	/* Build a temporary HeapTuple control structure */
	tmptup.t_len  = HeapTupleHeaderGetDatumLength(td);
	tmptup.t_data = td;
	tuple         = &tmptup;

	for (i = 0; i < tupdesc->natts; i++)
	{
		Datum		val;
		bool		isnull;
		bool    needs_quotes = false;
		char	   *attname;
		// JsonTypeCategory tcategory;
		// Oid	 outfuncoid;
		Form_pg_attribute att = TupleDescAttr(tupdesc, i);

		if (att->attisdropped)
			continue;

		Oid outfuncoid;
		Oid bstype = getBaseType(att->atttypid);
		getTypeOutputInfo(bstype, &outfuncoid, &typisvarlena);

		attname = NameStr(att->attname);

		val = heap_getattr(tuple, i + 1, tupdesc, &isnull);

		// If the value is null, skip it
		if(isnull) {
			continue;
		}
		char * value = "";
		switch(bstype)
		{
		case OIDOID:
			value = (char *)palloc0(128);
			sprintf(value, "%u", DatumGetObjectId(val));
			break;
		case NAMEOID:
			needs_quotes = true;
			value = NameStr(*DatumGetName(val));
			break;
		case TEXTOID:
			needs_quotes = true;
			value = TextDatumGetCString(val);
			break;
		case CHAROID:
			value = (char *)palloc0(2);
			needs_quotes = true;
			sprintf(value, "%c", DatumGetChar(val));
			break;
		case REGPROCOID:
			value = (char *)palloc0(128);
			sprintf(value, "%u", DatumGetObjectId(val));
			break;
		case BOOLOID:
			value = DatumGetBool(val) ? "true" : "false";
			break;
		case INT2OID:
			value = (char *)palloc0(128);
			sprintf(value, "%d", DatumGetInt16(val));
			break;
		case INT4OID:
			value = (char *)palloc0(128);
			sprintf(value, "%d", DatumGetInt32(val));
			break;
		case INT8OID:
			value = (char *)palloc0(128);
			sprintf(value, "%ld", DatumGetInt64(val));
		case FLOAT4OID:
			value = (char *)palloc0(128);
			sprintf(value, "%f", DatumGetFloat4(val));
		case FLOAT8OID:
			value = (char *)palloc0(128);
			sprintf(value, "%lf", DatumGetFloat8(val));
		case NUMERICOID:
			value = OidOutputFunctionCall(outfuncoid, val);
			break;
		case DATEOID:
			break;
		case TIMESTAMPOID:
			break;
		case TIMESTAMPTZOID:
			break;
		default:
			{
				needs_quotes = true;
				value = OidOutputFunctionCall(outfuncoid, val);
			}

		}
		if(needs_quotes) {
			appendStringInfoString(result, attname);
			appendStringInfoString(result, ": ");
			appendStringInfoString(result, "\"");
			appendStringInfoString(result, value);
			appendStringInfoString(result, "\"");
			appendStringInfoString(result, "\n");
		} else {
			appendStringInfoString(result, attname);
			appendStringInfoString(result, ": ");
			appendStringInfoString(result, value);
			appendStringInfoString(result, "\n");
		}
	}

	// appendStringInfoChar(result, '}');
	ReleaseTupleDesc(tupdesc);
}

/*
 * SQL function row_to_json(row)
 */
Datum
row_to_yaml(PG_FUNCTION_ARGS)
{
	Datum		array = PG_GETARG_DATUM(0);
	StringInfo	result;

	result = makeStringInfo();

	composite_to_yaml(array, result, false);

	PG_RETURN_TEXT_P(cstring_to_text_with_len(result->data, result->len));
}

