#include <memory>
#include "ossGisReader.h"

extern "C" {
#include "postgres.h"
#include "access/extprotocol.h"
#include "access/relscan.h"
#include "fmgr.h"
#include "libpq-fe.h"
#include "ossGisExt.h"
#include "ossExtWrapper.h"
#include "ossShapeReader.h"

static void
materializeResult(FunctionCallInfo fcinfo, LayerResult *result, int count);

Datum gis_file_import(PG_FUNCTION_ARGS) {
    OssGisReader *reader = NULL;
    HeapTuple tuple = NULL;
    MemoryContext old = NULL;
    FileScanDesc scan = NULL;

    /* Get our internal description of the protocol */
    reader = (OssGisReader *)EXTPROTOCOL_GET_USER_CTX(fcinfo);

    if (EXTPROTOCOL_IS_LAST_CALL(fcinfo)) {
        destroy_import_reader(reader);
        PG_RETURN_INT32(0);
    }

    scan = (FileScanDesc)fcinfo->flinfo->fn_extra;
    old = MemoryContextSwitchTo(scan->fs_formatter->fmt_executor_ctx);

    if (reader == NULL) {
        /* first call. do any desired init */
        char *url_with_options = EXTPROTOCOL_GET_URL(fcinfo);
        char *url, *access_key_id, *secret_access_key, *oss_type, *cos_appid, *layer;
       	int subdataset;
        truncate_options(url_with_options, &url, &access_key_id, &secret_access_key, &oss_type, &cos_appid, &layer, &subdataset);
        reader = create_import_reader(scan, url, access_key_id, secret_access_key, oss_type, cos_appid, 'g', subdataset);
	if (reader == NULL)
	    PG_RETURN_INT32((int)0);

        EXTPROTOCOL_SET_USER_CTX(fcinfo, reader);

        pfree(url);
    }

    /* =======================================================================
     *                            DO THE IMPORT
     * =======================================================================
     */

    tuple = oss_gis_read_tuple(reader);
    scan->tuple = tuple;

    MemoryContextSwitchTo(old);

    if (tuple == NULL)
        PG_RETURN_INT32((int)0);

    PG_RETURN_INT32((int)1);
}

/*
 * Return netcdf subdataset
 */
Datum
ogr_fdw_info(PG_FUNCTION_ARGS)
{
    LayerResult *result = NULL;
    ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
    int count = 0;
    char *url_with_options = text_to_cstring(PG_GETARG_TEXT_PP(0));
    char *url, *region, *access_key_id, *secret_access_key, *oss_type, *cos_appid, *vfs_url = NULL, *layer;
    int subdataset;


    /* check to see if caller supports us returning a tuplestore */
    if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
	ereport(ERROR,
		(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		 errmsg("set-valued function called in context that cannot accept a set")));
    if (!(rsinfo->allowedModes & SFRM_Materialize))
	ereport(ERROR,
		(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		 errmsg("materialize mode required, but it is not " \
		     "allowed in this context")));

    GDALAllRegister();
    truncate_options(url_with_options, &url, &access_key_id, &secret_access_key, &oss_type, &cos_appid, &layer, &subdataset);
    region = getRegion(url + strlen("oss://"));
    set_env(access_key_id, secret_access_key, region);
    vfs_url = getVFSUrl(url);
    elog(DEBUG1, "url = %s, vfs_url =%s", url, vfs_url);

    result = ogrListLayers(vfs_url, &count, url, access_key_id, secret_access_key);

    /* let the caller know we're sending back a tuplestore */
    rsinfo->returnMode = SFRM_Materialize;
    rsinfo->setResult = NULL;
    rsinfo->setDesc = NULL;

    /* make sure we have a persistent copy of the tupdesc */
    materializeResult(fcinfo, result, count);

    return (Datum) 0;
}

Datum
nc_subdataset_info(PG_FUNCTION_ARGS)
{
    LayerResult *result = NULL;
    ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
    int k = 0;
    char *url_with_options = text_to_cstring(PG_GETARG_TEXT_PP(0));
    char *url, *region, *access_key_id, *secret_access_key, *oss_type, *cos_appid, *layer, *bucket, *prefix;
    int subdataset;
    char *buffer = NULL;
    ossContext ossContextInt;
    ossObjectResult* objs = NULL;

    /* check to see if caller supports us returning a tuplestore */
    if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
	ereport(ERROR,
		(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		 errmsg("set-valued function called in context that cannot accept a set")));
    if (!(rsinfo->allowedModes & SFRM_Materialize))
	ereport(ERROR,
		(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		 errmsg("materialize mode required, but it is not " \
		     "allowed in this context")));

    truncate_options(url_with_options, &url, &access_key_id, &secret_access_key, &oss_type, &cos_appid, &layer, &subdataset);

    //TODO:
    //Assert all the
    region = getRegion(url + strlen("oss://"));
    bucket = getBucket(url + strlen("oss://"));
    prefix = url + strlen("oss://s3..amazonaws.com//") + strlen(region) + strlen(bucket);
    elog(DEBUG1, "%s-%s-%s", region, bucket, prefix);
    ossContextInt = ossInitContext("S3",
	    region,
	    NULL,
	    access_key_id,
	    secret_access_key,
	    1024 * 2,
	    1024 * 2);
    if(!ossContextInt){
	elog(ERROR, "initialize ossContextInt with error message %s", ossGetLastError());
    }
    objs = ossListObjects(ossContextInt, bucket, prefix);
    if (objs == NULL)
	elog(ERROR, "listobjcet return NULL");
    for (k = 0; k < objs->nObjects; k++)
    {
	elog(DEBUG1, "listobject size = %d, name=%s\n", objs->objects[k].size, objs->objects[k].key);
	if (objs->objects[k].size > 0){ 
	    buffer = (char *)palloc0(sizeof(char) * objs->objects[k].size);
	    DownloadObject(ossContextInt,  bucket,
		    objs->objects[k].key, objs->objects[k].size, buffer);
	    break;
	}
    }
    Assert(k <  objs->nObjects);
    ossDestroyContext(ossContextInt);

    CPLSetConfigOption("GDAL_SKIP", NULL);
    GDALAllRegister();
    if (is_netcdf(objs->objects[k].key)) {
	GDALDatasetH hds;
	saveTmpFile(objs->objects[k].key, buffer, objs->objects[k].size);

	hds = GDALOpenEx("/tmp/temp.netcdffile", GDAL_OF_READONLY | GDAL_OF_RASTER | GDAL_OF_VERBOSE_ERROR, NULL,NULL,NULL);
	if (hds == NULL) {
	    elog(ERROR, "open memory gdal failed.\n");
	}

	result = (LayerResult *)palloc0(sizeof(LayerResult) * 1);
	strcpy(result[0].layerName, objs->objects[k].key);
	getSubDataset(hds, (result[0].exttbl_definition), -1);
	GDALClose(hds);
    }

    /* let the caller know we're sending back a tuplestore */
    rsinfo->returnMode = SFRM_Materialize;
    rsinfo->setResult = NULL;
    rsinfo->setDesc = NULL;

    /* make sure we have a persistent copy of the tupdesc */
    materializeResult(fcinfo, result, 1);

    return (Datum) 0;
}

/*
 * Materialize the PGresult to return them as the function result.
 * The res will be released in this function.
 */
static void
materializeResult(FunctionCallInfo fcinfo, LayerResult *result, int count)
{
    ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;

    Assert(rsinfo->returnMode == SFRM_Materialize);

    PG_TRY();
    {
	TupleDesc       tupdesc;
	int                     ntuples;
	int                     nfields;
	/* get a tuple descriptor for our result type */
	if (TYPEFUNC_COMPOSITE != (get_call_result_type(fcinfo, NULL, &tupdesc))) {
	    elog(ERROR, "result type != TYPEFUNC_COMPOSITE");
	}
	Assert(2 == tupdesc->natts)
	/* make sure we have a persistent copy of the tupdesc */
	tupdesc = CreateTupleDescCopy(tupdesc);
	ntuples = count;
	nfields = 2;

	/*
	 * check result and tuple descriptor have the same number of columns
	 */
	if (nfields != tupdesc->natts)
	    ereport(ERROR,
		    (errcode(ERRCODE_DATATYPE_MISMATCH),
		     errmsg("remote query result rowtype does not match "
			 "the specified FROM clause rowtype")));

	if (ntuples > 0)
	{
	    AttInMetadata *attinmeta;
	    Tuplestorestate *tupstore;
	    MemoryContext oldcontext;
	    int                     row;
	    char      **values;

	    attinmeta = TupleDescGetAttInMetadata(tupdesc);

	    oldcontext = MemoryContextSwitchTo(
		    rsinfo->econtext->ecxt_per_query_memory);
	    tupstore = tuplestore_begin_heap(true, false, work_mem);
	    rsinfo->setResult = tupstore;
	    rsinfo->setDesc = tupdesc;
	    MemoryContextSwitchTo(oldcontext);

	    values = (char **) palloc(nfields * sizeof(char *));

	    /* put all tuples into the tuplestore */
	    for (row = 0; row < ntuples; row++)
	    {
		HeapTuple       tuple;

		values[0] = result[row].layerName;
		values[1] = result[row].exttbl_definition;
		elog(DEBUG1, "put %s %s", result[row].layerName, result[row].exttbl_definition);

		/* build the tuple and put it into the tuplestore. */
		tuple = BuildTupleFromCStrings(attinmeta, values);
		tuplestore_puttuple(tupstore, tuple);
	    }

	    /* clean up and return the tuplestore */
	    tuplestore_donestoring(tupstore);
	}

    }
    PG_CATCH();
    {
	/* be sure to release the libpq result */
	PG_RE_THROW();
    }
    PG_END_TRY();
}

Datum shape_file_import(PG_FUNCTION_ARGS) {
    OssShapeReader *reader = NULL;
    HeapTuple tuple = NULL;
    MemoryContext old = NULL;
    FileScanDesc scan = NULL;
    GDALDatasetH ogr_ds;
    OGRLayerH ogr_lyr;

    /* Get our internal description of the protocol */
    reader = (OssShapeReader *)EXTPROTOCOL_GET_USER_CTX(fcinfo);

    if (GpIdentity.segindex != 0)
        PG_RETURN_INT32((int)0);

    if (EXTPROTOCOL_IS_LAST_CALL(fcinfo)) {
        destroy_shape_reader(reader);
        PG_RETURN_INT32(0);
    }

    scan = (FileScanDesc)fcinfo->flinfo->fn_extra;
    old = MemoryContextSwitchTo(scan->fs_formatter->fmt_executor_ctx);

    if (reader == NULL) {
        /* first call. do any desired init */
        char *url_with_options = EXTPROTOCOL_GET_URL(fcinfo);
	char *vfs_url = NULL;
        char *url, *region, *access_key_id, *secret_access_key, *oss_type, *cos_appid, *layer = NULL;
	int subds;
	GDALAllRegister();

        truncate_options(url_with_options, &url, &access_key_id, &secret_access_key, &oss_type, &cos_appid, &layer, &subds);
		
	if (layer == NULL)
	    elog(ERROR, "set layer_name!");
	vfs_url = getVFSUrl(url);
	region = getRegion(url + strlen("oss://"));
	set_env(access_key_id, secret_access_key, region);
	elog(DEBUG1, "pid=%d line 228getenv=%s ## %s", getpid(), getenv("AWS_SECRET_ACCESS_KEY"), getenv("AWS_ACCESS_KEY_ID"));
	ogr_ds = ogrGetDataset(vfs_url);
	ogr_lyr = ogrGetLayer(ogr_ds, layer);
	elog(DEBUG1, "-->shape_file_import get ogr_lyr from vfs_url=%s, layer=%s", vfs_url, layer);

	init_geomtype();
        reader = create_shape_reader(scan, ogr_ds, ogr_lyr);
	if (reader == NULL)
	    PG_RETURN_INT32((int)0);

	pfree(region);
        EXTPROTOCOL_SET_USER_CTX(fcinfo, reader);

        pfree(url);
    }


    /* =======================================================================
     *                            DO THE IMPORT
     * =======================================================================
     */

    tuple = oss_shape_read_tuple(reader);
    scan->tuple = tuple;

    MemoryContextSwitchTo(old);

    if (tuple == NULL)
        PG_RETURN_INT32((int)0);

    PG_RETURN_INT32((int)1);
}

void set_env(char *access_key_id, char *secret_access_key, char *region)
{
    int size = 100;
    if (access_key_id != "") {
	char *str = (char *)palloc0(sizeof(char) * size);
	strcpy(str, "AWS_ACCESS_KEY_ID=");
	strcat(str, access_key_id);
	elog(DEBUG4, "putenv %s", str);
	putenv(str);
    }
    if (secret_access_key != "") {
	char *str = (char *)palloc0(sizeof(char) * size);
	strcpy(str, "AWS_SECRET_ACCESS_KEY=");
	strcat(str, secret_access_key);
	elog(DEBUG4, "putenv %s", str);
	putenv(str);
    }

    if (region != NULL) {
	char *str = (char *)palloc0(sizeof(char) * size);
	strcpy(str, "AWS_REGION=");
	strcat(str, region);
	elog(DEBUG4, "putenv %s", str);
	putenv(str);
    }
}

}
