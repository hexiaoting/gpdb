#include "postgres.h"

#include "ossCommon.h"
#include "miscadmin.h"

char *
get_opt_oss(const char *options, const char *key)
{
    const char *key_f = NULL;
    const char *key_tailing = NULL;
    const char *delimiter = " ";
    char *key_val = NULL;
    int val_len = 0;

    if (!options || !key)
        return NULL;

    char *key2search = (char *) palloc(strlen(key) + 3);
    int key_len = strlen(key);

    key2search[0] = ' ';
    memcpy(key2search + 1, key, key_len);
    key2search[key_len + 1] = '=';
    key2search[key_len + 2] = 0;

    key_f = strstr(options, key2search);
    if (key_f == NULL)
        goto FAIL;

    key_f += strlen(key2search);
    if (*key_f == ' ')
        goto FAIL;

    key_tailing = strstr(key_f, delimiter);
    val_len = 0;
    if (key_tailing)
        val_len = strlen(key_f) - strlen(key_tailing);
    else
        val_len = strlen(key_f);

    key_val = (char *) palloc(val_len + 1);

    memcpy(key_val, key_f, val_len);
    key_val[val_len] = 0;

    pfree(key2search);

    return key_val;

FAIL:
	pfree(key2search);
    return NULL;
}

/*
 * Truncates the given URL at the first space character. Returns
 * the part before the space, as a palloc'd string
 */
void
truncate_options(const char *url_with_options, char **url,
		char **access_key_id, char **secret_access_key,
		char **oss_type, char **cos_appid, char **layer_name)
{
    const char *delimiter = " ";
    char	*options;
    int url_len;

    options = strstr((char *)url_with_options, delimiter);
    url_len = strlen(url_with_options);

    if (options)
        url_len = strlen(url_with_options) - strlen(options);

    *url = (char *) palloc(url_len + 1);
    memcpy(*url, url_with_options, url_len);
    (*url)[url_len] = 0;

    *access_key_id = get_opt_oss(options, "access_key_id");
    if (*access_key_id == NULL)
    {
    		*access_key_id = "";
    }
    *secret_access_key = get_opt_oss(options, "secret_access_key");
    if (*secret_access_key == NULL)
    {
    		*secret_access_key = "";
    }
    *oss_type = get_opt_oss(options, "oss_type");
    if (*oss_type == NULL)
    {
            *oss_type = "";
    }else{
        for(int i=0;(*oss_type)[i];i++){
            if(((*oss_type)[i] >= 'a')&&((*oss_type)[i]) <= 'z')
                (*oss_type)[i] -= 32;
        }
    }
    *cos_appid = get_opt_oss(options, "cos_appid");
    if (*cos_appid == NULL)
    {
            *cos_appid = "";
    }

    *layer_name = get_opt_oss(options, "layer");
    if (*layer_name == NULL)
    {
	*layer_name = "";
    }

    return;
}

/*
 * for oss://s3.ap-northeast-1.amazonaws.com/hwtoss/shapefile
 * return /vsigposs/hwtoss/shapefile
 */
char *getVFSUrl(char *url)
{
    char *bucket = strstr((char *)url + strlen("oss://"), "/");
    char *res = (char *) palloc0(strlen("/vsigposs") + strlen(bucket) + 1);
    if (res == NULL)
	return NULL;

    strcpy(res, "/vsigposs");//, strlen("/vsigposs/"));
    strcat(res, bucket);

    return res;
}

char *getRegion(char *url)
{
    int          regionlen;
    int          pathlen;
    char        *ossdomain;
    char        *s_ossdomain;
    char        *s_region;
    char *region = NULL;

    ossdomain = strdup(".amazonaws.com");
    s_ossdomain = strstr(url, ossdomain);
    if (!s_ossdomain)
	return NULL;
    if (strncmp(url, "s3.", 3) == 0) {
	pathlen = s_ossdomain - url;
	regionlen = pathlen - 3;
	s_region = url + 3;
	region = palloc(regionlen + 1);
	memcpy(region, s_region, regionlen);
	elog(DEBUG1, "hwt-->getRegion region=%s, regionlen=%d", region, regionlen);
	region[regionlen] = '\0';
    }
    elog(DEBUG1, "hwt-->getRegion region=NULL url=%s ", url);
    return region;
}


#ifdef USE_PG_MEM

void * palloc(size_t sz);
void pfree(void *ptr);
void * repalloc(void *ptr, size_t sz);

#define malloc(sz) palloc(sz)
#define free(ptr) pfree(ptr)
#define realloc(ptr,sz) repalloc(ptr,sz)

#endif

/**
 * * Allocate a new stringbuffer_t. Use stringbuffer_destroy to free.
 * */
stringbuffer_t*
stringbuffer_create(void)
{
    return stringbuffer_create_with_size(STRINGBUFFER_STARTSIZE);
}

static void
stringbuffer_init_with_size(stringbuffer_t *s, size_t size)
{
    s->str_start = malloc(size);
    s->str_end = s->str_start;
    s->capacity = size;
    memset(s->str_start, 0, size);
}

void
stringbuffer_release(stringbuffer_t *s)
{
    if ( s->str_start ) free(s->str_start);
}

void
stringbuffer_init(stringbuffer_t *s)
{
    stringbuffer_init_with_size(s, STRINGBUFFER_STARTSIZE);
}

/**
 * * Allocate a new stringbuffer_t. Use stringbuffer_destroy to free.
 * */
stringbuffer_t*
stringbuffer_create_with_size(size_t size)
{
    stringbuffer_t *s;

    s = malloc(sizeof(stringbuffer_t));
    stringbuffer_init_with_size(s, size);
    return s;
}

/**
 * * Free the stringbuffer_t and all memory managed within it.
 * */
void
stringbuffer_destroy(stringbuffer_t *s)
{
    stringbuffer_release(s);
    if ( s ) free(s);
}

/**
 * * Reset the stringbuffer_t. Useful for starting a fresh string
 * * without the expense of freeing and re-allocating a new
 * * stringbuffer_t.
 * */
void
stringbuffer_clear(stringbuffer_t *s)
{
    s->str_start[0] = '\0';
    s->str_end = s->str_start;
}

/**
 * * If necessary, expand the stringbuffer_t internal buffer to accomodate the
 * * specified additional size.
 * */
static inline void
stringbuffer_makeroom(stringbuffer_t *s, size_t size_to_add)
{
    size_t current_size = (s->str_end - s->str_start);
    size_t capacity = s->capacity;
    size_t required_size = current_size + size_to_add;

    while (capacity < required_size)
	capacity *= 2;

    if ( capacity > s->capacity )
    {
	s->str_start = realloc(s->str_start, capacity);
	s->capacity = capacity;
	s->str_end = s->str_start + current_size;
    }
}

/**
 * * Return the last character in the buffer.
 * */
char
stringbuffer_lastchar(stringbuffer_t *s)
{
    if( s->str_end == s->str_start )
	return 0;

    return *(s->str_end - 1);
}

/**
 * * Append the specified string to the stringbuffer_t.
 * */
void
stringbuffer_append(stringbuffer_t *s, const char *a)
{
    int alen = strlen(a); /* Length of string to append */
    int alen0 = alen + 1; /* Length including null terminator */
    stringbuffer_makeroom(s, alen0);
    memcpy(s->str_end, a, alen0);
    s->str_end += alen;
}

/**
 * * Append the specified character to the stringbuffer_t.
 * */
void
stringbuffer_append_char(stringbuffer_t *s, char c)
{
    stringbuffer_makeroom(s, 2); /* space for char + null terminator */
    *(s->str_end) = c; /* add char */
    s->str_end += 1;
    *(s->str_end) = 0; /* null terminate */
}

/**
 * * Returns a reference to the internal string being managed by
 * * the stringbuffer. The current string will be null-terminated
 * * within the internal string.
 * */
const char*
stringbuffer_getstring(stringbuffer_t *s)
{
    return s->str_start;
}

/**
 * * Returns a newly allocated string large enough to contain the
 * * current state of the string. Caller is responsible for
 * * freeing the return value.
 * */
char*
stringbuffer_getstringcopy(stringbuffer_t *s)
{
    size_t size = (s->str_end - s->str_start) + 1;
    char *str = malloc(size);
    memcpy(str, s->str_start, size);
    str[size - 1] = '\0';
    return str;
}

/**
 * * Returns the length of the current string, not including the
 * * null terminator (same behavior as strlen()).
 * */
int
stringbuffer_getlength(stringbuffer_t *s)
{
    return (s->str_end - s->str_start);
}

/**
 * * Clear the stringbuffer_t and re-start it with the specified string.
 * */
void
stringbuffer_set(stringbuffer_t *s, const char *str)
{
    stringbuffer_clear(s);
    stringbuffer_append(s, str);
}

/**
 * * Copy the contents of src into dst.
 * */
void
stringbuffer_copy(stringbuffer_t *dst, stringbuffer_t *src)
{
    stringbuffer_set(dst, stringbuffer_getstring(src));
}

/**
 * * Appends a formatted string to the current string buffer,
 * * using the format and argument list provided. Returns -1 on error,
 * * check errno for reasons, documented in the printf man page.
 * */
static int
stringbuffer_avprintf(stringbuffer_t *s, const char *fmt, va_list ap)
{
    int maxlen = (s->capacity - (s->str_end - s->str_start));
    int len = 0; /* Length of the output */
    va_list ap2;

    /* Make a copy of the variadic arguments, in case we need to print twice */
    /* Print to our buffer */
    va_copy(ap2, ap);
    len = vsnprintf(s->str_end, maxlen, fmt, ap2);
    va_end(ap2);

    /* Propogate errors up */
    if ( len < 0 )
#if defined(__MINGW64_VERSION_MAJOR)
	len = _vscprintf(fmt, ap2);/**Assume windows flaky vsnprintf that returns -1 if initial buffer to small and add more space **/
#else
    return len;
#endif

    /* We didn't have enough space! */
    /* Either Unix vsnprint returned write length larger than our buffer */
    /*     or Windows vsnprintf returned an error code. */
    if ( len >= maxlen )
    {
	stringbuffer_makeroom(s, len + 1);
	maxlen = (s->capacity - (s->str_end - s->str_start));

	/* Try to print a second time */
	len = vsnprintf(s->str_end, maxlen, fmt, ap);

	/* Printing error? Error! */
	if ( len < 0 ) return len;
	/* Too long still? Error! */
	if ( len >= maxlen ) return -1;
    }

    /* Move end pointer forward and return. */
    s->str_end += len;
    return len;
}

/**
 * * Appends a formatted string to the current string buffer,
 * * using the format and argument list provided.
 * * Returns -1 on error, check errno for reasons,
 * * as documented in the printf man page.
 * */
int
stringbuffer_aprintf(stringbuffer_t *s, const char *fmt, ...)
{
    int r;
    va_list ap;
    va_start(ap, fmt);
    r = stringbuffer_avprintf(s, fmt, ap);
    va_end(ap);
    return r;
}

/**
 * * Trims whitespace off the end of the stringbuffer. Returns
 * * the number of characters trimmed.
 * */
int
stringbuffer_trim_trailing_white(stringbuffer_t *s)
{
    char *ptr = s->str_end;
    int dist = 0;

    /* Roll backwards until we hit a non-space. */
    while( ptr > s->str_start )
    {
	ptr--;
	if( (*ptr == ' ') || (*ptr == '\t') )
	{
	    continue;
	}
	else
	{
	    ptr++;
	    dist = s->str_end - ptr;
	    *ptr = '\0';
	    s->str_end = ptr;
	    return dist;
	}
    }
    return dist;
}

/**
 * * Trims zeroes off the end of the last number in the stringbuffer.
 * * The number has to be the very last thing in the buffer. Only the
 * * last number will be trimmed. Returns the number of characters
 * * trimmed.
 * *
 * * eg: 1.22000 -> 1.22
 * *     1.0 -> 1
 * *     0.0 -> 0
 * */
int
stringbuffer_trim_trailing_zeroes(stringbuffer_t *s)
{
    char *ptr = s->str_end;
    char *decimal_ptr = NULL;
    int dist;

    if ( s->str_end - s->str_start < 2)
	return 0;

    /* Roll backwards to find the decimal for this number */
    while( ptr > s->str_start )
    {
	ptr--;
	if ( *ptr == '.' )
	{
	    decimal_ptr = ptr;
	    break;
	}
	if ( (*ptr >= '0') && (*ptr <= '9' ) )
	    continue;
	else
	    break;
    }

    /* No decimal? Nothing to trim! */
    if ( ! decimal_ptr )
	return 0;

    ptr = s->str_end;

    /* Roll backwards again, with the decimal as stop point, trimming contiguous zeroes */
    while( ptr >= decimal_ptr )
    {
	ptr--;
	if ( *ptr == '0' )
	    continue;
	else
	    break;
    }

    /* Huh, we get anywhere. Must not have trimmed anything. */
    if ( ptr == s->str_end )
	return 0;

    /* If we stopped at the decimal, we want to null that out.
     *    It we stopped on a numeral, we want to preserve that, so push the
     *       pointer forward one space. */
    if ( *ptr != '.' )
	ptr++;

    /* Add null terminator re-set the end of the stringbuffer. */
    *ptr = '\0';
    dist = s->str_end - ptr;
    s->str_end = ptr;
    return dist;
}

void
ogrStringLaunder(char *str)
{
    int i, j = 0;
    char tmp[STR_MAX_LEN];
    memset(tmp, 0, STR_MAX_LEN);

    for(i = 0; str[i]; i++)
    {
	char c = tolower(str[i]);

	/* First character is a numeral, prefix with 'n' */
	if ( i == 0 && (c >= 48 && c <= 57) )
	{
	    tmp[j++] = 'n';
	}

	/* Replace non-safe characters w/ _ */
	if ( (c >= 48 && c <= 57) || /* 0-9 */
		(c >= 65 && c <= 90) || /* A-Z */
		(c >= 97 && c <= 122)   /* a-z */ )
	{
	    /* Good character, do nothing */
	}
	else
	{
	    c = '_';
	}
	tmp[j++] = c;

	/* Avoid mucking with data beyond the end of our stack-allocated strings */
	if ( j >= STR_MAX_LEN )
	    j = STR_MAX_LEN - 1;
    }
    strncpy(str, tmp, STR_MAX_LEN);

}

void
ogrDeparseStringLiteral(stringbuffer_t *buf, const char *val)
{
    const char *valptr;

    /*
     * Rather than making assumptions about the remote server's value of
     * standard_conforming_strings, always use E'foo' syntax if there are any
     * backslashes.  This will fail on remote servers before 8.1, but those
     * are long out of support.
     */
    if ( strchr(val, '\\') != NULL )
    {
	stringbuffer_append_char(buf, 'E');
    }
    stringbuffer_append_char(buf, '\'');
    for ( valptr = val; *valptr; valptr++ )
    {
	char ch = *valptr;
	if ( ch == '\'' || ch == '\\' )
	{
	    stringbuffer_append_char(buf, ch);
	}
	stringbuffer_append_char(buf, ch);
    }
    stringbuffer_append_char(buf, '\'');
}

char *
ogrTypeToPgType(OGRFieldDefnH ogr_fld)
{
    OGRFieldType ogr_type = OGR_Fld_GetType(ogr_fld);
    switch(ogr_type)
    {
	case OFTInteger:
#if GDAL_VERSION_MAJOR >= 2
	    if( OGR_Fld_GetSubType(ogr_fld) == OFSTBoolean )
		return "boolean";
	    else
#endif
		return "integer";
	case OFTReal:
	    return "real";
	case OFTString:
	    return "varchar";
	case OFTBinary:
	    return "bytea";
	case OFTDate:
	    return "date";
	case OFTTime:
	    return "time";
	case OFTDateTime:
	    return "timestamp";
	case OFTIntegerList:
	    return "integer[]";
	case OFTRealList:
	    return "real[]";
	case OFTStringList:
	    return "varchar[]";
#if GDAL_VERSION_MAJOR >= 2
	case OFTInteger64:
	    return "bigint";
#endif
	default:
	    CPLError(CE_Failure, CPLE_AssertionFailed,
		    "unsupported GDAL type '%s'",
		    OGR_GetFieldTypeName(ogr_type));
	    return NULL;
    }
    return NULL;
}

