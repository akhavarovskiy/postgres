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

PG_FUNCTION_INFO_V1(yaml_in);
/*
 * Output.
 */
Datum
yaml_out(PG_FUNCTION_ARGS)
{
	/* we needn't detoast because text_to_cstring will handle that */
	Datum		txt = PG_GETARG_DATUM(0);

	PG_RETURN_CSTRING(TextDatumGetCString(txt));
}
PG_FUNCTION_INFO_V1(yaml_out);

/*
 * Binary send.
 */
Datum
yaml_send(PG_FUNCTION_ARGS)
{
	text	   *t = PG_GETARG_TEXT_PP(0);
	StringInfoData buf;

	pq_begintypsend(&buf);
	pq_sendtext(&buf, VARDATA_ANY(t), VARSIZE_ANY_EXHDR(t));
	PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}
PG_FUNCTION_INFO_V1(yaml_send);

/*
 * Binary receive.
 */
Datum
yaml_recv(PG_FUNCTION_ARGS)
{
	StringInfo	buf = (StringInfo) PG_GETARG_POINTER(0);
	char	   *str;
	int			nbytes;

	str = pq_getmsgtext(buf, buf->len - buf->cursor, &nbytes);

	PG_RETURN_TEXT_P(cstring_to_text_with_len(str, nbytes));
}
PG_FUNCTION_INFO_V1(yaml_recv);
