#ifndef __OSS_EXT_WRAPPER__
#define __OSS_EXT_WRAPPER__

#include "ossDownloader.h"
#include "ossCommon.h"
#include "oss/oss.h"
#include "gpossext.h"

typedef enum ExtWrapperType {
    EWT_NONE,
    EWT_READ,
    EWT_WRITE
} ExtWrapperType;


typedef struct
{
    char       *url;
    char       *path;
    char       *bucket;
    char       *prefix;
    char       *region;
    char       *ossType;
    char       *cosAppid;


    CompressFormat compress_format;

    OSSCredential cred;

    /* Configurable parameters */
    char       *conf_accessid;
    char       *conf_secret;

    int         conf_nconnections;
    int         conf_low_speed_limit;
    int         conf_low_speed_time;
    bool        conf_encryption;

    /* segment layout */
    int         segid;
    int         segnum;

    ossContext     ossContextInt;

    /* for liboss context */
    int64_t     write_buffer_size;
    int64_t     read_buffer_size;

    /* for readers */
    OssDownloader        *downloader;
    char                  tmpFilePath[200];
    char                  buf[65535];

    /* for writers */
    //OssUploader    *uploader;

    // For reader and writer
    struct archive       *archive;
    struct archive_entry *archive_entry;


} OSSExtBase;

/*
extern OSSExtBase *OSSExtBase_create(   const char *url,
                                        const char *access_key_id, const char *secret_key_id,
                                        const char *oss_type,
                                        const char *cos_appid,
                                        ExtWrapperType ewt,
                                        CompressFormat cf);

extern int OSSReader_TransferData(OSSExtBase *base, char *buf, int bufsize);
extern int OSSWriter_TransferData(OSSExtBase *base, char *data, int bufsize);
					*/
extern void InitConfig(OSSExtBase *base, const char *access_key_id, const char *secret_access_key);
extern bool ValidateURL(OSSExtBase *base);
extern bool ValidateURL_QS(OSSExtBase *base);
extern bool ValidateURL_S3(OSSExtBase *base);
extern bool ValidateURL_COS(OSSExtBase *base);
extern bool ValidateURL_OSS(OSSExtBase *base);
extern bool ValidateURL_KS3(OSSExtBase *base);
#endif
