#ifndef __OSS_COMMON_H__
#define __OSS_COMMON_H__

#include "nodes/pg_list.h"

/* Compile-time Configuration */
#define MAX_FETCH_RETRIES 3

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "ogr_api.h"
#include "ogr_srs_api.h"
#include "cpl_error.h"

#define STRINGBUFFER_STARTSIZE 128
#define STR_MAX_LEN 256

typedef struct
{
    size_t capacity;
    char *str_end;
    char *str_start;
}
stringbuffer_t;

extern stringbuffer_t *stringbuffer_create_with_size(size_t size);
extern stringbuffer_t *stringbuffer_create(void);
extern void stringbuffer_init(stringbuffer_t *s);
extern void stringbuffer_release(stringbuffer_t *s);
extern void stringbuffer_destroy(stringbuffer_t *sb);
extern void stringbuffer_clear(stringbuffer_t *sb);
void stringbuffer_set(stringbuffer_t *sb, const char *s);
void stringbuffer_copy(stringbuffer_t *sb, stringbuffer_t *src);
extern void stringbuffer_append(stringbuffer_t *sb, const char *s);
extern void stringbuffer_append_char(stringbuffer_t *s, char c);
extern int stringbuffer_aprintf(stringbuffer_t *sb, const char *fmt, ...);
extern const char *stringbuffer_getstring(stringbuffer_t *sb);
extern char *stringbuffer_getstringcopy(stringbuffer_t *sb);
extern int stringbuffer_getlength(stringbuffer_t *sb);
extern char stringbuffer_lastchar(stringbuffer_t *s);
extern int stringbuffer_trim_trailing_white(stringbuffer_t *s);
extern int stringbuffer_trim_trailing_zeroes(stringbuffer_t *s);


typedef struct
{
    char	   *keyid;
    char	   *secret;
} OSSCredential;

typedef enum CompressFormat
{
    COMPRESSNONE = 0,
    LZJB,
    ZLIB,
    GZIP
}CompressFormat;

extern char *get_opt_oss(const char* url, const char* key);
extern void truncate_options(const char* url_with_options, char **url,
							char **access_key_id,
							char **secret_access_key,
							char **oss_type,
							char **cos_appid,
							char **layer_name);
extern char *getVFSUrl(char *url);
extern char *getRegion(char *url);
extern void ogrStringLaunder(char *str);
extern void ogrDeparseStringLiteral(stringbuffer_t *buf, const char *val);
extern char *ogrTypeToPgType(OGRFieldDefnH ogr_fld);
#endif  // __OSS_COMMON_H__
