#include "ossGisReader.h"
#include "ossExtWrapper.h"
#include "gdal.h"

#include <cassert>
#include <list>
#include <sstream>
#include <vector>
#include <set>

extern "C" {
#include "access/heapam.h"
#include "access/tuptoaster.h"
#include "commands/copy.h"
#include "utils/elog.h"
#include "utils/palloc.h"
}

//#define EQUAL(a,b)              (strcmp(a,b)==0)
//#define EQUALN(a,b,n)           (strncmp(a,b,n)==0)
#define streq(s1,s2) (strcmp((s1),(s2)) == 0)
#define strcaseeq(s1,s2) (strcasecmp((s1),(s2)) == 0)

void *safePalloc0(size_t size) {
    void *retval = NULL;

    PG_TRY();
    {
        retval = palloc0(size);
    }
    PG_CATCH();
    {
        EmitErrorReport();
        FlushErrorState();
    }
    PG_END_TRY();

    if (retval == NULL) {
        throw std::runtime_error("cannot palloc memory");
    }

    return retval;
}

void safePfree(void *p) {
    bool fatal = false;
    PG_TRY();
    {
        pfree(p);
    }
    PG_CATCH();
    {
        EmitErrorReport();
        FlushErrorState();
        fatal = true;
    }
    PG_END_TRY();

    if (fatal) {
        throw std::runtime_error("cannot pfree memory");
    }
}

OssGisReader::OssGisReader(FileScanDesc scan, ListBucketResult *keys,
                         std::unique_ptr<OSSExtBase> base)
    : scan(scan), keys(keys), base(std::move(base)), /*tupdesc(scan->fs_tupDesc),*/
      /*ncolumns(tupdesc->natts),*/ fileIndex(0),
      curObjectSize(0)
{
    if (scan != NULL) {
	tupdesc = scan->fs_tupDesc;
	ncolumns = tupdesc->natts;
    	attinmeta = TupleDescGetAttInMetadata(tupdesc);
	for (int i = 0; i < ncolumns; ++i) {
	    if (tupdesc->attrs[i]->attisdropped) {
		throw std::runtime_error("cannot handle external table with dropped columns");
	    }
	}
    }
}

OssGisReader::~OssGisReader()
{
    ossDestroyContext(this->base->ossContextInt);
}

void OssGisReader::nextFile() {
    for (; fileIndex < (std::size_t)keys->ncontents; fileIndex++) {
	if (fileIndex % (size_t)base->segnum != (size_t)base->segid) {
	    continue;
	}
	this->curObjectName = keys->contents[fileIndex].key;
	this->curObjectSize = keys->contents[fileIndex].size;
	fileIndex++;
	return;
    }
    this->curObjectName = NULL;
}

void checkDatum(Datum str) {
    char *p;
    void *tofree;
    int len;
    varattrib_untoast_ptr_len(str, &p, &len, &tofree);

    pg_mbstrlen_with_len(p, len);
    Insist(len < 1024 * 1024 * 1024L);

    if (tofree)
        safePfree(tofree);
}

bool checkTuple(HeapTuple tuple, TupleDesc tupdesc) {
    Datum values[tupdesc->natts];
    bool nulls[tupdesc->natts];

    heap_deform_tuple(tuple, tupdesc, values, nulls);

    for (int i = 0; i < tupdesc->natts && !nulls[i]; ++i) {
        Oid type = tupdesc->attrs[i]->atttypid;
        switch (type) {
            case TEXTOID:
            case BPCHAROID:
            case VARCHAROID:
                checkDatum(values[i]);
        }
    }

    return true;
}

HeapTuple OssGisReader::nextTuple() {
    HeapTuple tuple = NULL;
    GDALDatasetH hds;
    size_t size = 0;
    uint32_t hexlen = 0;
    bytea *result = NULL;

    MemoryContext oldContext = MemoryContextSwitchTo(scan->fs_pstate->rowcontext);
    Datum *values = (Datum *)safePalloc0(sizeof(Datum) * this->ncolumns);
    bool *nulls = (bool *)safePalloc0(sizeof(bool) * this->ncolumns);

    nextFile();
    if (this->curObjectName == NULL)
	return NULL;
    elog(DEBUG1, "hwt--->nextTuple return file %s", this->curObjectName);
    char *buffer = (char *)safePalloc0(sizeof(char) * this->curObjectSize);

    //S1 
    //	First column: filename 
    size = strlen(this->curObjectName);
    result = (bytea *)safePalloc0(size + VARHDRSZ);
    memcpy(VARDATA(result), this->curObjectName, size);
    SET_VARSIZE(result, size + VARHDRSZ);
    Assert(VARSIZE_ANY(result) == size + VARHDRSZ);
    values[0] = PointerGetDatum(result); 

    //S2
    //	Secord column: get raster data and convert to tuple
    //	1) fetch all gis data to memory from oss
    //	2) map memory to gdal_virtualfs
    //	3) open gdal_virtualfs file and convert to hex
    DownloadObject(base->ossContextInt,  base->bucket,
		        this->curObjectName, this->curObjectSize, buffer);

    GDALAllRegister();
    VSIInstallMemFileHandler();
    VSIFCloseL( VSIFileFromMemBuffer( "/vsimem/work.dat", (unsigned char *)buffer, this->curObjectSize, FALSE) );
    hds = GDALOpen("/vsimem/work.dat", GA_ReadOnly);
    if (hds == NULL) {
	elog(ERROR, "open memory gdal failed.\n");
    }
    Assert(GDALGetDriverShortName( GDALGetDatasetDriver(hds) ) == "GTiff");
    rt_raster rast = rt_raster_from_gdal_dataset(hds);
    if (rast == NULL) {
	elog(ERROR, "cannot convert memory to raster");
    }
    char *hex = rt_raster_to_hexwkb(rast, FALSE, &hexlen);
    elog(DEBUG1, "hwt raster———%dx%d", GDALGetRasterXSize(hds), GDALGetRasterYSize(hds));
    raster_destroy(rast);
    values[1] = InputFunctionCall(&attinmeta->attinfuncs[1],
	    hex,
	    attinmeta->attioparams[1],
	    attinmeta->atttypmods[1]
	    );
    safePfree(buffer);

    //S3
    //	Third Column: gdalinfo-metadata
    char *meta = (char *)safePalloc0(3000); 
    GetGisMeta(hds, meta);
    elog(DEBUG4, "--->get meta sucess meta=%s", meta);
    values[2] = InputFunctionCall(&attinmeta->attinfuncs[2],
	    meta,
	    attinmeta->attioparams[2],
	    attinmeta->atttypmods[2]
	    );
    safePfree(meta);


    VSIUnlink( "/vsimem/work.dat" );
    // TODO: why this failed?
    //if (hds != NULL)
    //	GDALClose(hds);

    MemoryContextSwitchTo(oldContext);

    PG_TRY();
    {
        tuple = heap_form_tuple(tupdesc, values, nulls);
    }
    PG_CATCH();
    {
        EmitErrorReport();
        FlushErrorState();
        tuple = NULL;
    }
    PG_END_TRY();

    if (tuple == NULL) {
        throw std::runtime_error("cannot form tuple");
    }

    Assert(checkTuple(tuple, tupdesc));

    elog(DEBUG4, "--->return tuple");
    return tuple;
}

HeapTuple OssGisReader::read() {
    return nextTuple();
}

ossContext OssGisReader::getContext()
{
    return this->base->ossContextInt;
}

void OssGisReader::setFormat(char f)
{
    this->format = f;
}

char OssGisReader::getFormat()
{
    return this->format;
}

ListBucketResult *ListBucketInternal(OSSExtBase *base) {
    ListBucketResult *keylist = NULL;
    int nfailures = 0;

    keylist = ListBucket(   base->ossContextInt,
                            base->bucket,
                            base->prefix,
                            &(base->cred));

    if (!keylist) {
	elog(INFO, "ListBucket return NULL");
        nfailures++;

        if (nfailures >= MAX_FETCH_RETRIES) {
            throw std::runtime_error( std::string("could not list contents of bucket \"") + base->bucket + "\"");
        }
    }

    return keylist;
}

OssGisReader *create_import_reader_internal(FileScanDesc scan, const char *url,
                                           const char *access_key_id,
                                           const char *secret_access_key,
                                           const char *oss_type,
                                           const char *cos_appid) {

    ListBucketResult *keylist = NULL;
    std::unique_ptr<OSSExtBase> base(new OSSExtBase);
    InitConfig(base.get(), access_key_id, secret_access_key);

    base->url = pstrdup(url);
    base->compress_format = COMPRESSNONE;
    base->ossType = pstrdup(oss_type);
    base->cosAppid = pstrdup(cos_appid);

    // Validate url first
    if (!ValidateURL(base.get())) {
        throw std::runtime_error(std::string("given OSS URL \"") + base->url + "\" is not valid");
    }

    /*
     * Copy the credentials into the OSSCredential object, for passing around more
     * easily.
     */
    base->cred.keyid = base->conf_accessid;
    base->cred.secret = base->conf_secret;

    elog(DEBUG1, "hwt -->create_import_reader_internal: ossInitContext OSSType=%s,region=%s,cosAppid=%s,keyid=%s,srcret=%s",
	    base->ossType,
                                      base->region,
                                      base->cosAppid,
                                      base->cred.keyid,
                                      base->cred.secret);
    base->ossContextInt = ossInitContext(base->ossType,
                                      base->region,
                                      base->cosAppid,
                                      base->cred.keyid,
                                      base->cred.secret,
                                      base->write_buffer_size,
                                      base->read_buffer_size);
    if(!base->ossContextInt){
        elog(ERROR, "create_import_reader_internal: could not initialize ossContextInt with error message %s", ossGetLastError());
    } else {
	elog(DEBUG1, "initOK");
    }

    keylist = ListBucketInternal(base.get());
    if (keylist == NULL)
	return NULL;
    
    elog(DEBUG4, "hwt-->hwt keylist name=%s,prefix=%s,limit=%d,ncontents=%d", 
	    keylist->name,
	    keylist->prefix,
	    keylist->limit,
	    keylist->ncontents); 
    for (int i = 0; i < keylist->ncontents ; i++)
    {
	BucketContent tmp = keylist->contents[i];
	elog(DEBUG4, "hwt---->keylist contents %d:%s-%ld",
		i, tmp.key, tmp.size);
    }

    return new OssGisReader(scan, keylist, std::move(base));
}


extern "C" {
void destroy_import_reader(OssGisReader *state) { 
    delete state; 
}

OssGisReader *create_import_reader(FileScanDesc scan, const char *url,
                                  const char *access_key_id,
                                  const char *secret_access_key,
                                  const char *oss_type,
                                  const char *cos_appid,
				  char format) {
    try {
        OssGisReader * reader = 
	    create_import_reader_internal(scan, url, access_key_id, secret_access_key, oss_type, cos_appid);
	reader->setFormat(format);
	return reader;
    } catch (std::exception &e) {
        ereport(ERROR, (errmsg("%s", e.what())));
    }

    return NULL;
}

HeapTuple oss_gis_read_tuple(OssGisReader *reader) {
    assert(reader != NULL);

    try {
	return reader->read();
    } catch (std::exception &e) {
	ereport(ERROR, (errmsg("%s", e.what())));
    }

    return NULL;
}
}
