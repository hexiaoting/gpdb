#ifndef __OSSDOWNLOADER_H__
#define __OSSDOWNLOADER_H__

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "oss/oss.h"
#include "ossCommon.h"

typedef struct
{
    char	   *key;
    uint64	size;
} BucketContent;

typedef struct
{
    char           *name;
    char           *prefix;
    unsigned int    limit;
    BucketContent  *contents; /* palloc'd array of BucketContent */
    int             ncontents;
} ListBucketResult;

typedef struct OssDownloader
{
    ListBucketResult   *keylist;
    int                 currentReadObjectNum;
    ossObject           ossObject ;
} OssDownloader;

// need free
extern ListBucketResult *ListBucket(const ossContext ossContextInt, const char *bucket,
									const char *path, const OSSCredential *cred);
extern void
DownloadObject(const ossContext ossContextInt,  const char *bucket,
	        char *objectName, int64_t size, char *buffer);

#endif
