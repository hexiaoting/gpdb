#ifndef __OSS_GIS_READER__
#define __OSS_GIS_READER__

#include "oss/oss.h"
#include <memory>
#include "cpl_string.h"

extern "C" {
// clang-format off
#include "postgres.h"
#include "access/htup.h"
#include "access/relscan.h"
#include "ossDownloader.h"
#include "ossGisRaster.h"
#include "ossGisMeta.h"
#include "ossExtWrapper.h"
#include "executor/execWorkfile.h"
#include "funcapi.h"
// clang-format on
}

class OssGisReader {
public:
    OssGisReader(FileScanDesc scan, ListBucketResult *keys,
              std::unique_ptr<OSSExtBase> base);
    virtual ~OssGisReader();
    HeapTuple read();
    ossContext getContext();
    //LayerResult* ogrListLayers(int *num);
    void setFormat(char f);
    char getFormat();

private:
    FileScanDesc                    scan;
    ListBucketResult               *keys;
    std::unique_ptr<OSSExtBase>     base;
    TupleDesc                       tupdesc;
    AttrNumber                      ncolumns;
    size_t                          fileIndex;
    size_t                          tupleIndex;
    int64_t	curObjectSize;
    char *	curObjectName;
    AttInMetadata *attinmeta;
    char	format;

private:
    void nextFile();
    HeapTuple nextTuple();
};

extern "C" {
    void destroy_import_reader(OssGisReader *state);
    OssGisReader *create_import_reader(FileScanDesc scan, const char *url,
                                          const char *access_key_id,
                                          const char *secret_access_key,
                                          const char *oss_type,
                                          const char *cos_appid,
					  char format);
    HeapTuple oss_gis_read_tuple(OssGisReader *reader);
    void test_oss(ossContext context);
}

#endif /* __OSS_GIS_READER__ */
