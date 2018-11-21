#ifndef __OSS_GIS_EXT__
#define __OSS_GIS_EXT__
#include "utils/builtins.h"

Datum gis_file_import(PG_FUNCTION_ARGS);
Datum shape_file_import(PG_FUNCTION_ARGS);
Datum ogr_fdw_info(PG_FUNCTION_ARGS);
Datum nc_subdataset_info(PG_FUNCTION_ARGS);

void set_env(char *access_key_id, char *secret_access_key, char *region);

#endif /* __OSS_GIS_EXT__ */
