#include "postgres.h"

#include "funcapi.h"

#include "access/relscan.h"
#include "access/extprotocol.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_exttable.h"
#include "utils/array.h"
//#include "utils/builtins.h"
#include "utils/memutils.h"
#include "fmgr.h"

#include "oss/oss.h"
#include "ossExtWrapper.h"
#include "ossGisExt.h"

#include <archive.h>
#include <archive_entry.h>

/* Do the module magic dance */
PG_MODULE_MAGIC;
PG_FUNCTION_INFO_V1(oss_export);
PG_FUNCTION_INFO_V1(oss_import);
PG_FUNCTION_INFO_V1(oss_validate_urls);
PG_FUNCTION_INFO_V1(ogr_fdw_info);
PG_FUNCTION_INFO_V1(nc_subdataset_info);

Datum oss_export(PG_FUNCTION_ARGS);
Datum oss_import(PG_FUNCTION_ARGS);
Datum oss_validate_urls(PG_FUNCTION_ARGS);

/*
 *  Import data into GPDB.
 */
Datum
oss_import(PG_FUNCTION_ARGS)
{
    // oss_import must be called via the external table format manager
    if (!CALLED_AS_EXTPROTOCOL(fcinfo))
        elog(ERROR, "EXTPROTOCOL_IMPORT: not called by external protocol manager");

    // If the file is in ORC format, call orc_file_import.
    FileScanDesc scan = (FileScanDesc)fcinfo->flinfo->fn_extra;

    if (scan == NULL)
	elog(ERROR, "you should init scan first in oss_import");

    if (fmttype_is_shapefile(scan->format))
	return shape_file_import(fcinfo);
    if (fmttype_is_gis(scan->format))
        return gis_file_import(fcinfo);
    else
	elog(ERROR, "cannot recgonise this format");
}


/*
 *  Export data out of GPDB.
 */
Datum
oss_export(PG_FUNCTION_ARGS)
{
    OSSExtBase   *myData;

    // oss_export must be called via the external table format manager
    if (!CALLED_AS_EXTPROTOCOL(fcinfo))
        elog(ERROR, "EXTPROTOCO_EXPORT: not called by external protocol manager");

    // Get internal description of the protocol
    elog(ERROR, "call-->oss_export");
    myData = (OSSExtBase *)EXTPROTOCOL_GET_USER_CTX(fcinfo);

    // Check whether it's the final CALL, free the objects
    PG_RETURN_INT32((int)0);
}

Datum
oss_validate_urls(PG_FUNCTION_ARGS)
{
    PG_RETURN_VOID();
}
