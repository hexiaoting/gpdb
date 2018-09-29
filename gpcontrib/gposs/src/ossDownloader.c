/*
 * Functions to issue various commands to OSS.
 *
 * Currently, only List Bucket command is implemented. (The GET operation, to
 * fetch contents of an object, is implemented with the HTTPFetcher mechanism
 * instead)
 */
#include "postgres.h"
#include "oss/oss.h"
#include "ossDownloader.h"
#include <unistd.h>
#include "miscadmin.h"

static int
BucketContentComp(const void *a_, const void *b_)
{
	const BucketContent *a = (const BucketContent *) a_;
	const BucketContent *b = (const BucketContent *) b_;

    return strcmp(a->key, b->key);
}

/*
 * endpoint: host to use in the URL, and to connect to.
 * host: host to send as Host header.
 */
ListBucketResult *
ListBucket(const ossContext ossContextInt,  const char *bucket,
		   const char *prefix, const OSSCredential *cred)
{
    ossObjectResult *objects = ossListObjects(ossContextInt, bucket, prefix);
    
    if(!objects){
        elog(ERROR, "%s", ossGetLastError());
    }

    elog(DEBUG1, "hwt--->ListBucket");
    ListBucketResult *result = palloc0(sizeof(ListBucketResult));
    result->name = strdup(objects->name);
    result->prefix = strdup(objects->prefix);
    result->limit = objects->limit;

    for(int i=0;i<objects->nObjects;i++){
        if (objects->objects[i].size > 0)
        {
            int ncontents = result->ncontents;

            if (result->contents == NULL)
                result->contents = palloc((ncontents + 1) * sizeof(BucketContent));
            else
                result->contents = repalloc(result->contents,
                                            (ncontents + 1) * sizeof(BucketContent));

            BucketContent *item = &result->contents[ncontents];
            item->key = (char*)pstrdup(objects->objects[i].key);
            item->size = objects->objects[i].size;

            result->ncontents++;
        }
        else
        {
            elog(DEBUG2, "object with key %s is empty, skipping", objects->objects[i].key);
        }
    }
    free(objects);

    elog(DEBUG1, "hwt<--->ListBucket");
    if (result->ncontents == 0)
    {
    	elog(DEBUG1, "hwt<--->ListBucket return 0 contents");
	ossDestroyContext(ossContextInt);
	return NULL;
    }
    qsort(result->contents, result->ncontents, sizeof(BucketContent), BucketContentComp);
    return result;
}

void 
DownloadObject(const ossContext ossContextInt,  const char *bucket,
	char *objectName, int64_t size, char *buffer) 
{
    int64_t idx = 0;
    // Download from OSS
    ossObject ossObjectInt = ossGetObject(ossContextInt,
	    bucket,
	    objectName,
	    0,
	    size);
    if (ossObjectInt == NULL) {
	elog(ERROR, "ossGetObject %s failed.", objectName);
    }

    while (idx < size){
	uint64_t tmp;
	elog(DEBUG4, "hwt--->DownloadObj read size=%ld", size - idx);
	tmp = ossRead(ossContextInt, ossObjectInt, buffer + idx, size - idx);
	elog(DEBUG4, "hwt--->DownloadObj size=%ld,idx=%ld, tmp=%ld", size, idx, tmp);
	if (tmp == -1) {
	    elog(ERROR, "%s", ossGetLastError());
	} else if (tmp > 0) {
	    idx += tmp;
	} else {
	    elog(DEBUG4, "hwt--->DownloadObj read over size=%ld", idx);
	}
    }

    ossCloseObject(ossContextInt, ossObjectInt);
}
