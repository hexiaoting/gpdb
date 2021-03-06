--
-- Create oss protocol (for testing)
--
CREATE OR REPLACE FUNCTION oss_export() RETURNS integer AS  '$libdir/gpossext.so', 'oss_export'  LANGUAGE C STABLE;
CREATE OR REPLACE FUNCTION oss_import() RETURNS integer AS  '$libdir/gpossext.so', 'oss_import'  LANGUAGE C STABLE;
DROP PROTOCOL IF EXISTS oss;
CREATE PROTOCOL oss (writefunc = oss_export, readfunc = oss_import); -- should succeed

--
-- Create table
--
CREATE READABLE EXTERNAL TABLE  osstbl_example(
	filename text,
	rast raster,
	metadata text
	)
    LOCATION('oss://s3.ap-northeast-1.amazonaws.com/hwtoss/test access_key_id=AKIAIH7UJTSAWOAHE5FQ secret_access_key=h2j/BwA8Gi/yGy+cxRDQDNCvJeWIbsN90EiFk8BL oss_type=S3')
    FORMAT 'gis';


--
-- NetCDF: 
-- 1. show subdataset for one netcdf file
-- 2. create table with subdataset=xxx option
--
CREATE OR REPLACE FUNCTION nc_subdataset_info(text) returns setof record as  '$libdir/gpossext.so', 'nc_subdataset_info' LANGUAGE C STRICT;
select * from nc_subdataset_info('oss://s3.ap-northeast-1.amazonaws.com/hwtoss/netcdf access_key_id=AKIAIH7UJTSAWOAHE5FQ secret_access_key=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') AS tbl(name text, sqlq text);
CREATE READABLE EXTERNAL TABLE  osstbl_netcdf(
	filename text,
	rast raster,
	metadata text
	)
    LOCATION('oss://s3.ap-northeast-1.amazonaws.com/hwtoss/netcdf subdataset=1 access_key_id=AKIAIH7UJTSAWOAHE5FQ secret_access_key=h2j/BwA8Gi/yGy+cxRDQDNCvJeWIbsN90EiFk8BL oss_type=S3')
    FORMAT 'gis';

-- Query
--
select filename, st_value(rast, 3, 4), metadata from osstbl_example;

drop external table osstbl_example;


--
-- 矢量数据
--

CREATE OR REPLACE FUNCTION ogr_fdw_info(text) returns setof record as  '$libdir/gpossext.so', 'ogr_fdw_info'  LANGUAGE C STRICT;
select * from ogr_fdw_info('oss://s3.ap-northeast-1.amazonaws.com/hwtoss/shapefile access_key_id=AKIAIH7UJTSAWOAHE5FQ secret_access_key=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') AS tbl(name text, sqlq text);

create readable external table pt_two (
	fid bigint,
	geom Geometry(Point,4326),
	name varchar,
	age integer,
	height real,
	birthdate date
	)
    location('oss://s3.ap-northeast-1.amazonaws.com/hwtoss/shapefile access_key_id=AKIAIH7UJTSAWOAHE5FQ secret_access_key=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 layer=pt_two')
    format 'Shapefile';

select * from pt_two;
