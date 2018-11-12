#ifndef __OSSGISMEAT_H_
#define __OSSGISMEAT_H_
#include "gdal.h"
#include "gdal_alg.h"
#include "ogr_srs_api.h"
#include "cpl_string.h"
#include "cpl_conv.h"
#include "cpl_multiproc.h"
#include "postgres.h"
//#include "commonutils.h"

//int GetGisMeta( GDALDatasetH        hDataset );
//int GetGisMeta(char *meta);
int GetGisMeta(GDALDatasetH hDataset, char* buffer);
char *getSubDataset(GDALMajorObjectH hObject);

#endif
