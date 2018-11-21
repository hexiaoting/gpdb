#ifndef __OSS_SHAPE_READER__
#define __OSS_SHAPE_READER__

#include "oss/oss.h"
#include "gdal.h"
#include "cpl_string.h"
#include "postgres.h"
#include "access/htup.h"
#include "access/relscan.h"
#include "executor/execWorkfile.h"
#include "ossCommon.h"
#include "funcapi.h"
#include "utils/lsyscache.h"
#include "catalog/namespace.h"
#include "utils/builtins.h"
#include "gpossext.h"

#define LAYER_LENGTH 100
#define DEF_LENGTH   1024 

extern Oid GEOMETRYOID;

typedef struct {
    char layerName[LAYER_LENGTH];
    char exttbl_definition[DEF_LENGTH];
} LayerResult;

typedef struct
{
            char *fldname;
	            int fldnum;
} OgrFieldEntry;

typedef enum
{
    OGR_UNMATCHED,
    OGR_GEOMETRY,
    OGR_FID,
    OGR_FIELD
} OgrColumnVariant;

typedef struct OgrFdwColumn
{
    /* PgSQL metadata */
    int pgattnum;            /* PostgreSQL attribute number */
    int pgattisdropped;      /* PostgreSQL attribute dropped? */
    char *pgname;            /* PostgreSQL column name */
    Oid pgtype;              /* PostgreSQL data type */
    int pgtypmod;            /* PostgreSQL type modifier */

    /* For reading */
    Oid pginputfunc;         /* PostgreSQL function to convert cstring to type */
    Oid pginputioparam;
    Oid pgrecvfunc;          /* PostgreSQL function to convert binary to type */
    Oid pgrecvioparam;

    /* For writing */
    Oid pgoutputfunc;        /* PostgreSQL function to convert type to cstring */
    bool pgoutputvarlena;
    Oid pgsendfunc;        /* PostgreSQL function to convert type to binary */
    bool pgsendvarlena;

    /* OGR metadata */
    OgrColumnVariant ogrvariant;
    int ogrfldnum;
    OGRFieldType ogrfldtype;
} OgrFdwColumn;

typedef struct OgrFdwTable
{
    int ncols;
    char *tblname;
    OgrFdwColumn *cols;
} OgrFdwTable;

typedef struct OssShapeReader
{
    FileScanDesc scan;
    TupleDesc tupdesc;
    GDALDatasetH ogr_ds;
    OGRLayerH ogr_lyr;
    AttrNumber                      ncolumns;
    size_t                          tupleIndex;
    Oid setsridfunc;
    Oid typmodsridfunc;    /* postgis_typmod_srid() */
    bool lyr_utf8;
    OgrFdwTable *table;
} OssShapeReader;

void test_oss(ossContext context);
LayerResult* ogrListLayers(char *source, int *num, char *url,
	char *access_key_id, char *secret_access_key);

bool ogrLayerToSQL (const OGRLayerH ogr_lyr,
	int launder_table_names, int launder_column_names,
	int use_postgis_geometry, stringbuffer_t *buf,
	char *url, char *access_key_id, char *secret_access_key);

void destroy_shape_reader(OssShapeReader *reader);
GDALDatasetH ogrGetDataset(char *source);
OGRLayerH ogrGetLayer(GDALDatasetH ogr_ds, char *layer_name);
void init_geomtype();
OssShapeReader *create_shape_reader(FileScanDesc scan, GDALDatasetH ogr_ds, OGRLayerH ogr_lyr);
HeapTuple oss_shape_read_tuple(OssShapeReader * reader);
#endif /* __OSS_SHAPE_READER__ */
