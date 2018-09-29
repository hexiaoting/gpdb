#include "postgres.h"

#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "ossExtWrapper.h"

/*
 * Note: This not only checks that the URL is valid, but also extracts some fields
 * from it into fields in 'base'.
 *
 * There are two kinds of URL format:
 *
 * virtual host style:
 * http://<bucket-name>.<zone-id>.qingstor.com/<object-name>
 *
 * path style:
 * http://<zone-id>.qingstor.com/<bucket-name>/<object-name>
 *
 */
    bool
ValidateURL(OSSExtBase *base)
{
    if (strncmp(base->url, "oss://", 6) == 0)
    {
	if(strcmp(base->ossType,"QS")==0)
	    return ValidateURL_QS(base);
	else if(strcmp(base->ossType,"S3B")==0 || strcmp(base->ossType,"S3")==0) {
	    elog(DEBUG1, "hwt ValidateURL base->ossType=%s.--->ValidateURL_S3", base->ossType);
	    return ValidateURL_S3(base);
	}
	else if(strcmp(base->ossType,"COS")==0)
	    return ValidateURL_COS(base);
	else if(strcmp(base->ossType,"ALI")==0)
	    return ValidateURL_OSS(base);
	else if(strcmp(base->ossType,"KS3")==0)
	    return ValidateURL_KS3(base);
    }
    else {
	elog(ERROR, "validate failed return false.");
	return false;
    }

    return true;
}

    bool
ValidateURL_QS(OSSExtBase *base)
{
    char        *ossdomain ;
    char        *s_ossdomain;
    char        *s_region;
    int          regionlen;
    char        *s_path;
    int          pathlen;
    char        *s_bucket;
    char        *s_prefix;
    int          bucketlen;
    bool         pathstyle;
    char        *s_tmp;

    ossdomain = strdup(".qingstor.com");

    s_ossdomain = strstr(base->url, ossdomain);
    if (!s_ossdomain)
	return false;

    s_path = base->url + 6;
    pathlen = s_ossdomain - s_path;

    /* check whether this url is in virtual host style or
     * path path style.
     *
     * virtual host style:
     * QingStor:  http://<bucket-name>.<zone-id>.qingstor.com/<object-name>
     *
     * path style:
     * QingStor:  http://<zone-id>.qingstor.com/<bucket-name>/<object-name>
     */
    s_tmp = strchr(s_path, '.');
    if (s_tmp == s_ossdomain)
    {
	pathstyle = true;
	regionlen = pathlen;
	s_region = s_path;
	base->region = palloc(regionlen + 1);
	memcpy(base->region, s_region, regionlen);
	base->region[regionlen] = '\0';

    }
    else
    {
	pathstyle = false;
	s_bucket = s_path;
	bucketlen = s_tmp - s_path;
	base->bucket = palloc(bucketlen + 1);
	memcpy(base->bucket, s_bucket, bucketlen);
	base->bucket[bucketlen] = '\0';

	s_region = s_tmp + 1;
	if (s_region >= s_ossdomain)
	{
	    return false;
	}
	regionlen = s_ossdomain - s_region;
	base->region = palloc(regionlen + 1);
	memcpy(base->region, s_region, regionlen);
	base->region[regionlen] = '\0';
    }

    if (pathstyle)
    {
	s_tmp = strchr(s_ossdomain, '/');
	if (!s_tmp)
	{
	    return false;
	}
	s_tmp++;
    }
    s_prefix = strchr(s_tmp, '/');
    if (!s_prefix)
    {
	return false;
    }
    if (pathstyle)
    {
	s_bucket = s_tmp;
	bucketlen = s_prefix - s_tmp;
	base->bucket = palloc(bucketlen + 1);
	memcpy(base->bucket, s_bucket, bucketlen);
	base->bucket[bucketlen] = '\0';
    }

    base->prefix = pstrdup(s_prefix+1);

    return true;
}
    bool
ValidateURL_S3(OSSExtBase *base)
{
    char        *ossdomain ;
    char        *s_ossdomain;
    char        *s_region;
    int          regionlen;
    char        *s_path;
    int          pathlen;
    char        *s_bucket;
    char        *s_prefix;
    int          bucketlen;
    bool         pathstyle;
    char        *s_tmp;

    // S3 example: oss://s3.cn-north-1.amazonaws.com.cn/ossext-example/orc
    ossdomain = strdup(".amazonaws.com");

    s_ossdomain = strstr(base->url, ossdomain);
    if (!s_ossdomain)
	return false;

    s_path = base->url + 6;

    pathlen = s_ossdomain - s_path;
    elog(DEBUG1, "hwt-->ValidateURL_S3 s_path=%s, s_ossdomain=%s, pathlen=%d",
	    s_path, s_ossdomain, pathlen);

    /* check whether this url is in virtual host style or
     * path path style.
     *
     * virtual host style:
     * S3:  oss://<bucket-name>.s3.<zone-id>.amazonaws.com.cn/<object-name>
     *
     * path style:
     * S3:  oss://s3.<zone-id>.amazonaws.com.cn/<object-name>
     */

    s_tmp = strchr(s_path, '.');

    elog(DEBUG1, "strncmp(s_path, s3., 3)=%d", strncmp(s_path, "s3.", 3));
    if (strncmp(s_path, "s3.", 3)!=0)
    {
	pathstyle = false;
	s_bucket = s_path;
	bucketlen = s_tmp - s_path;
	base->bucket = palloc(bucketlen + 1);
	memcpy(base->bucket, s_bucket, bucketlen);
	base->bucket[bucketlen] = '\0';

	s_region = s_tmp + 4;
	elog(DEBUG1, "hwt-->ValidateURL_S3 region=%s,", s_region);
	if (s_region >= s_ossdomain)
	{
	    return false;
	}
	regionlen = s_ossdomain - s_region;
	base->region = palloc(regionlen + 1);
	memcpy(base->region, s_region, regionlen);
	base->region[regionlen] = '\0';

    }
    else
    {
	pathstyle = true;
	regionlen = pathlen - 3;
	s_region = s_path + 3;
	base->region = palloc(regionlen + 1);
	memcpy(base->region, s_region, regionlen);
	elog(DEBUG1, "hwt-->ValidateURL_S3 region2=%s, regionlen=%d", base->region, regionlen);
	base->region[regionlen] = '\0';
    }

    if (pathstyle)
    {
	s_tmp = strchr(s_ossdomain, '/');
	if (!s_tmp)
	{
	    return false;
	}
	s_tmp++;
    }
    s_prefix = strchr(s_tmp, '/');
    if (!s_prefix)
    {
	return false;
    }
    if (pathstyle)
    {
	s_bucket = s_tmp;
	bucketlen = s_prefix - s_tmp;
	base->bucket = palloc(bucketlen + 1);
	memcpy(base->bucket, s_bucket, bucketlen);
	base->bucket[bucketlen] = '\0';
    }

    base->prefix = pstrdup(s_prefix+1);

    if(false && base->region){
	// For S3, in order to use liboss S3B context the region should be in format like:
	//   s3.cn-north-1.amazonaws.com.cn

	char endPoint[200];
	sprintf(endPoint, "s3.%s.amazonaws.com.cn", base->region);
	regionlen = strlen(endPoint);
	base->region = palloc(regionlen + 1);
	memcpy(base->region, endPoint, regionlen);
	base->region[regionlen] = '\0';
    }
    return true;
}
    bool
ValidateURL_COS(OSSExtBase *base)
{
    char        *ossdomain ;
    char        *s_ossdomain;
    char        *s_region;
    int          regionlen;
    char        *s_path;
    int          pathlen;
    char        *s_bucket;
    char        *s_prefix;
    int          bucketlen;
    bool         pathstyle;
    char        *s_tmp;

    ossdomain = strdup(".myqcloud.com");

    s_ossdomain = strstr(base->url, ossdomain);
    if (!s_ossdomain)
	return false;

    s_path = base->url + 6;

    pathlen = s_ossdomain - s_path;

    /* check whether this url is in virtual host style or
     * path path style.
     *
     * virtual host style:
     * COS:  oss://<bucket-name>.cos.<zone-id>.myqcloud.com/<object-name>
     *
     * path style:
     * COS:  oss://cos.<zone-id>.myqcloud.com/<object-name>
     */

    s_tmp = strchr(s_path, '.');

    if (strncmp(s_path, "cos.", 4)!=0)
    {
	pathstyle = false;
	s_bucket = s_path;
	bucketlen = s_tmp - s_path;
	base->bucket = palloc(bucketlen + 1);
	memcpy(base->bucket, s_bucket, bucketlen);
	base->bucket[bucketlen] = '\0';

	s_region = s_tmp + 5;
	if (s_region >= s_ossdomain)
	{
	    return false;
	}
	regionlen = s_ossdomain - s_region;
	base->region = palloc(regionlen + 1);
	memcpy(base->region, s_region, regionlen);
	base->region[regionlen] = '\0';
    }
    else
    {
	pathstyle = true;
	regionlen = pathlen - 4;
	s_region = s_path + 4;
	base->region = palloc(regionlen + 1);
	memcpy(base->region, s_region, regionlen);
	base->region[regionlen] = '\0';
    }

    if (pathstyle)
    {
	s_tmp = strchr(s_ossdomain, '/');
	if (!s_tmp)
	{
	    return false;
	}
	s_tmp++;
    }
    s_prefix = strchr(s_tmp, '/');
    if (!s_prefix)
    {
	return false;
    }
    if (pathstyle)
    {
	s_bucket = s_tmp;
	bucketlen = s_prefix - s_tmp;
	base->bucket = palloc(bucketlen + 1);
	memcpy(base->bucket, s_bucket, bucketlen);
	base->bucket[bucketlen] = '\0';
    }

    base->prefix = pstrdup(s_prefix+1);

    if(base->bucket){
	// For COS, in URI the bucket is in format:
	//   bucket-appid.cos.ap-beijing.myqcloud.com

	char *s_temp = strstr(base->bucket, base->cosAppid);
	int bucketLength = s_temp - base->bucket - 1;
	base->bucket[bucketLength] = '\0';
    }

    return true;
}

    bool
ValidateURL_OSS(OSSExtBase *base)
{
    char        *ossdomain ;
    char        *s_ossdomain;
    char        *s_region;
    int          regionlen;
    char        *s_path;
    int          pathlen;
    char        *s_bucket;
    char        *s_prefix;
    int          bucketlen;
    bool         pathstyle;
    char        *s_tmp;

    ossdomain = strdup(".aliyuncs.com");

    s_ossdomain = strstr(base->url, ossdomain);
    if (!s_ossdomain)
	return false;

    s_path = base->url + 6;
    pathlen = s_ossdomain - s_path;

    /* check whether this url is in virtual host style or
     * path path style.
     *
     * virtual host style:
     * OSS:  http://<bucket-name>.<zone-id>.aliyuncs.com/<object-name>
     *
     * path style:
     * OSS:  http://<zone-id>.aliyuncs.com/<bucket-name>/<object-name>
     */
    s_tmp = strchr(s_path, '.');
    if (s_tmp == s_ossdomain)
    {
	pathstyle = true;
	regionlen = pathlen;
	s_region = s_path;
	base->region = palloc(regionlen + 1);
	memcpy(base->region, s_region, regionlen);
	base->region[regionlen] = '\0';

    }
    else
    {
	pathstyle = false;
	s_bucket = s_path;
	bucketlen = s_tmp - s_path;
	base->bucket = palloc(bucketlen + 1);
	memcpy(base->bucket, s_bucket, bucketlen);
	base->bucket[bucketlen] = '\0';

	s_region = s_tmp + 1;
	if (s_region >= s_ossdomain)
	{
	    return false;
	}
	regionlen = s_ossdomain - s_region;
	base->region = palloc(regionlen + 1);
	memcpy(base->region, s_region, regionlen);
	base->region[regionlen] = '\0';
    }

    if (pathstyle)
    {
	s_tmp = strchr(s_ossdomain, '/');
	if (!s_tmp)
	{
	    return false;
	}
	s_tmp++;
    }
    s_prefix = strchr(s_tmp, '/');
    if (!s_prefix)
    {
	return false;
    }
    if (pathstyle)
    {
	s_bucket = s_tmp;
	bucketlen = s_prefix - s_tmp;
	base->bucket = palloc(bucketlen + 1);
	memcpy(base->bucket, s_bucket, bucketlen);
	base->bucket[bucketlen] = '\0';
    }

    base->prefix = pstrdup(s_prefix+1);

    return true;
}

    bool
ValidateURL_KS3(OSSExtBase *base)
{
    char        *ossdomain ;
    char        *s_ossdomain;
    char        *s_region;
    int          regionlen;
    char        *s_path;
    int          pathlen;
    char        *s_bucket;
    char        *s_prefix;
    int          bucketlen;
    bool         pathstyle;
    char        *s_tmp;

    ossdomain = strdup(".ksyun.com");

    s_ossdomain = strstr(base->url, ossdomain);
    if (!s_ossdomain)
	return false;

    s_path = base->url + 6;
    pathlen = s_ossdomain - s_path;

    /* check whether this url is in virtual host style or
     * path path style.
     *
     * virtual host style:
     * KS3:  http://<bucket-name>.<zone-id>.ksyun.com/<object-name>
     *
     * path style:
     * KS3:  http://<zone-id>.ksyun.com/<bucket-name>/<object-name>
     */
    s_tmp = strchr(s_path, '.');
    if (s_tmp == s_ossdomain)
    {
	pathstyle = true;
	regionlen = pathlen;
	s_region = s_path;
	base->region = palloc(regionlen + 1);
	memcpy(base->region, s_region, regionlen);
	base->region[regionlen] = '\0';

    }
    else
    {
	pathstyle = false;
	s_bucket = s_path;
	bucketlen = s_tmp - s_path;
	base->bucket = palloc(bucketlen + 1);
	memcpy(base->bucket, s_bucket, bucketlen);
	base->bucket[bucketlen] = '\0';

	s_region = s_tmp + 1;
	if (s_region >= s_ossdomain)
	{
	    return false;
	}
	regionlen = s_ossdomain - s_region;
	base->region = palloc(regionlen + 1);
	memcpy(base->region, s_region, regionlen);
	base->region[regionlen] = '\0';
    }

    if (pathstyle)
    {
	s_tmp = strchr(s_ossdomain, '/');
	if (!s_tmp)
	{
	    return false;
	}
	s_tmp++;
    }
    s_prefix = strchr(s_tmp, '/');
    if (!s_prefix)
    {
	return false;
    }
    if (pathstyle)
    {
	s_bucket = s_tmp;
	bucketlen = s_prefix - s_tmp;
	base->bucket = palloc(bucketlen + 1);
	memcpy(base->bucket, s_bucket, bucketlen);
	base->bucket[bucketlen] = '\0';
    }

    base->prefix = pstrdup(s_prefix+1);

    return true;
}

/*
 * Read options from configuration file into OSSExtBase object.
 */
    void
InitConfig(OSSExtBase *base, const char *access_key_id, const char *secret_access_key)
{
    base->conf_accessid = pstrdup(access_key_id);
    base->conf_secret = pstrdup(secret_access_key);

    if ((strcmp(base->conf_accessid, "") == 0) ||
	    (strcmp(base->conf_secret, "") == 0))
    {
	elog(DEBUG2, "access id or secret is empty");
    }

    base->conf_nconnections = 2;
    base->conf_low_speed_limit = 10240;
    base->conf_low_speed_time = 60;

    base->write_buffer_size = 1024 * 2;
    base->read_buffer_size  = 1024 * 2;

    base->conf_encryption = false;

    base->segid = GpIdentity.segindex;
    base->segnum = GpIdentity.numsegments;
}
