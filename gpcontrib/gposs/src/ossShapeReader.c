#include "ossShapeReader.h"
//#include "gdal.h"

#include "access/heapam.h"
#include "access/tuptoaster.h"
#include "commands/copy.h"
#include "utils/elog.h"
#include "utils/palloc.h"

#define streq(s1,s2) (strcmp((s1),(s2)) == 0)
#define strcaseeq(s1,s2) (strcasecmp((s1),(s2)) == 0)
#define OGR_FDW_FRMT_INT64       "%lld"
#define OGR_FDW_CAST_INT64(x)    (long long)(x)

/* Global to hold GEOMETRYOID */
Oid GEOMETRYOID = InvalidOid;
void init_geomtype();

static Datum
pgDatumFromCString(const char *cstr, Oid pgtype, int pgtypmod, Oid pginputfunc)
{
    Datum value;
    Datum cdata = CStringGetDatum(cstr);

    value = OidFunctionCall3(pginputfunc, cdata,
	    ObjectIdGetDatum(InvalidOid),
	    Int32GetDatum(pgtypmod));

    return value;
}

static void
ogrGeomTypeToPgGeomType(stringbuffer_t *buf, OGRwkbGeometryType gtype)
{
    switch(wkbFlatten(gtype))
    {
	case wkbUnknown:
	    stringbuffer_append(buf, "Geometry");
	    break;
	case wkbPoint:
	    stringbuffer_append(buf, "Point");
	    break;
	case wkbLineString:
	    stringbuffer_append(buf, "LineString");
	    break;
	case wkbPolygon:
	    stringbuffer_append(buf, "Polygon");
	    break;
	case wkbMultiPoint:
	    stringbuffer_append(buf, "MultiPoint");
	    break;
	case wkbMultiLineString:
	    stringbuffer_append(buf, "MultiLineString");
	    break;
	case wkbMultiPolygon:
	    stringbuffer_append(buf, "MultiPolygon");
	    break;
	case wkbGeometryCollection:
	    stringbuffer_append(buf, "GeometryCollection");
	    break;
#if GDAL_VERSION_MAJOR >= 2
	case wkbCircularString:
	    stringbuffer_append(buf, "CircularString");
	    break;
	case wkbCompoundCurve:
	    stringbuffer_append(buf, "CompoundCurve");
	    break;
	case wkbCurvePolygon:
	    stringbuffer_append(buf, "CurvePolygon");
	    break;
	case wkbMultiCurve:
	    stringbuffer_append(buf, "MultiCurve");
	    break;
	case wkbMultiSurface:
	    stringbuffer_append(buf, "MultiSurface");
	    break;
#endif
	case wkbNone:
	    CPLError(CE_Failure, CPLE_AssertionFailed, "Cannot handle OGR geometry type wkbNone");
	default:
	    CPLError(CE_Failure, CPLE_AssertionFailed, "Cannot handle OGR geometry type '%d'", gtype);
    }

#if GDAL_VERSION_MAJOR >= 2
    if ( wkbHasZ(gtype) )
#else
	if ( gtype & wkb25DBit )
#endif
	    stringbuffer_append(buf, "Z");

#if GDAL_VERSION_MAJOR >= 2 && GDAL_VERSION_MINOR >= 1
    if ( wkbHasM(gtype) )
	stringbuffer_append(buf, "M");
#endif

    return;
}

static OGRErr
ogrColumnNameToSQL (const char *ogrcolname, const char *pgtype, int launder_column_names, stringbuffer_t *buf)
{
    char pgcolname[STR_MAX_LEN];
    strncpy(pgcolname, ogrcolname, STR_MAX_LEN);
    ogrStringLaunder(pgcolname);

    if ( launder_column_names )
    {
	stringbuffer_aprintf(buf, ",\n  %s %s", pgcolname, pgtype);
	if ( ! strcaseeq(pgcolname, ogrcolname) )
	{
	    stringbuffer_append(buf, " OPTIONS (column_name ");
	    ogrDeparseStringLiteral(buf, ogrcolname);
	    stringbuffer_append(buf, ")");
	}
    }
    else
    {
	/* OGR column is PgSQL compliant, we're all good */
	if ( streq(pgcolname, ogrcolname) )
	    stringbuffer_aprintf(buf, ",\n  %s %s", ogrcolname, pgtype);
	/* OGR is mixed case or non-compliant, we need to quote it */
	else
	    stringbuffer_aprintf(buf, ",\n  \"%s\" %s", ogrcolname, pgtype);
    }
    return OGRERR_NONE;
}

bool
ogrLayerToSQL (const OGRLayerH ogr_lyr,
	int launder_table_names, int launder_column_names,
	int use_postgis_geometry, stringbuffer_t *buf,
	char *url, char *access_key_id, char *secret_access_key)
{
    int geom_field_count, i;
    char table_name[STR_MAX_LEN];
    OGRFeatureDefnH ogr_fd = OGR_L_GetLayerDefn(ogr_lyr);
    stringbuffer_t gbuf;

    stringbuffer_init(&gbuf);

    if ( ! ogr_fd )
    {
	elog(ERROR, "unable to get OGRFeatureDefnH from OGRLayerH");
	return false;
    }

#if GDAL_VERSION_MAJOR >= 2 || GDAL_VERSION_MINOR >= 11
    geom_field_count = OGR_FD_GetGeomFieldCount(ogr_fd);
#else
    geom_field_count = (OGR_L_GetGeomType(ogr_lyr) != wkbNone);
#endif

    /* Process table name */
    strncpy(table_name, OGR_L_GetName(ogr_lyr), STR_MAX_LEN);

    /* Create table */
    stringbuffer_aprintf(buf, "CREATE READABLE EXTERNAL TABLE %s (\n", table_name);

    /* For now, every table we auto-create will have a FID */
    stringbuffer_append(buf, "  fid bigint");

    /* Handle all geometry columns in the OGR source */
    for ( i = 0; i < geom_field_count; i++ )
    {
	int srid = 0;
#if GDAL_VERSION_MAJOR >= 2 || GDAL_VERSION_MINOR >= 11
	OGRGeomFieldDefnH gfld = OGR_FD_GetGeomFieldDefn(ogr_fd, i);
	OGRwkbGeometryType gtype = OGR_GFld_GetType(gfld);
	const char *geomfldname = OGR_GFld_GetNameRef(gfld);
	OGRSpatialReferenceH gsrs = OGR_GFld_GetSpatialRef(gfld);
#else
	OGRwkbGeometryType gtype = OGR_FD_GetGeomType(ogr_fd);
	const char *geomfldname = "geom";
	OGRSpatialReferenceH gsrs = OGR_L_GetSpatialRef(ogr_lyr);
#endif
	/* Skip geometry type we cannot handle */
	if ( gtype == wkbNone )
	    continue;

	/* Clear out our geometry type buffer */
	stringbuffer_clear(&gbuf);

	/* PostGIS geometry type has lots of complex stuff */
	if ( use_postgis_geometry )
	{
	    /* Add geometry type info */
	    stringbuffer_append(&gbuf, "Geometry(");
	    ogrGeomTypeToPgGeomType(&gbuf, gtype);

	    /* See if we have an EPSG code to work with */
	    if ( gsrs )
	    {
		const char *charAuthType;
		const char *charSrsCode;
		OSRAutoIdentifyEPSG(gsrs);
		charAuthType = OSRGetAttrValue(gsrs, "AUTHORITY", 0);
		charSrsCode = OSRGetAttrValue(gsrs, "AUTHORITY", 1);
		if ( charAuthType && strcaseeq(charAuthType, "EPSG")
		      	&& charSrsCode && atoi(charSrsCode) > 0 )
		{
		    srid = atoi(charSrsCode);
		}
	    }

	    /* Add EPSG number, if figured it out */
	    if ( srid )
	    {
		stringbuffer_aprintf(&gbuf, ",%d)", srid);
	    }
	    else
	    {
		stringbuffer_append(&gbuf, ")");
	    }
	}
	/* Bytea is simple */
	else
	{
	    stringbuffer_append(&gbuf, "bytea");
	}

	/* Use geom field name if we have it */
	if ( geomfldname && strlen(geomfldname) > 0 )
	{
	    ogrColumnNameToSQL(geomfldname, stringbuffer_getstring(&gbuf), launder_column_names, buf);
	}
	/* Or a numbered generic name if we don't */
	else if ( geom_field_count > 1 )
	{
	    stringbuffer_aprintf(buf, ",\n  geom%d %s", i, stringbuffer_getstring(&gbuf));
	}
	/* Or just a generic name */
	else
	{
	    stringbuffer_aprintf(buf, ",\n  geom %s", stringbuffer_getstring(&gbuf));
	}
    }

    /* Write out attribute fields */
    for ( i = 0; i < OGR_FD_GetFieldCount(ogr_fd); i++ )
    {
	OGRFieldDefnH ogr_fld = OGR_FD_GetFieldDefn(ogr_fd, i);
	ogrColumnNameToSQL(OGR_Fld_GetNameRef(ogr_fld),
		ogrTypeToPgType(ogr_fld),
		launder_column_names, buf);
    }

    /*
     *      * Add server name and layer-level options.  We specify remote
     *           * layer name as option
     *                */
    stringbuffer_aprintf(buf, "\n) \nLOCATION ('" );
    stringbuffer_append(buf, url);
    stringbuffer_append(buf, " access_key_id=");
    stringbuffer_append(buf, access_key_id);
    stringbuffer_append(buf, " secret_access_key=");
    stringbuffer_append(buf, secret_access_key);
    stringbuffer_append(buf, " layer=");
    stringbuffer_append(buf, OGR_L_GetName(ogr_lyr));
    stringbuffer_append(buf, "')\n FORMAT 'Shapefile';\n");

    return true;
}


LayerResult* ogrListLayers(char *source, int *num, char *url, char *access_key_id, char *secret_access_key)
{
    GDALDatasetH ogr_ds = NULL;
    int i;
    GDALDriverH ogr_dr = NULL;
    LayerResult *result = NULL;

//    GDALAllRegister();

#if GDAL_VERSION_MAJOR < 2
    ogr_ds = OGROpen(source, FALSE, NULL);
#else
    ogr_ds = GDALOpenEx(source,
	    GDAL_OF_READONLY,
	    NULL, NULL, NULL);
#endif

    ogr_dr = GDALGetDatasetDriver(ogr_ds);

    if ( ! ogr_ds )
    {
	elog(ERROR, "Could not connect to source '%s'", source);
	return NULL;
    }

    *num = GDALDatasetGetLayerCount(ogr_ds);
    if (*num == 0)
	return NULL;
    elog(DEBUG1, "get layer number=%d", *num);
    result = (LayerResult *)palloc0(sizeof(LayerResult) * (*num));
    for ( i = 0; i < *num; i++ )
    {
	stringbuffer_t buf;
	OGRLayerH ogr_lyr = GDALDatasetGetLayer(ogr_ds, i);
	if ( ! ogr_lyr )
	{
	    return NULL;
	}
	strcpy(result[i].layerName, OGR_L_GetName(ogr_lyr));

	stringbuffer_init(&buf);
	ogrLayerToSQL(ogr_lyr,
		TRUE, /* launder table names */
		TRUE, /* launder column names */
		TRUE, /* use postgis geometry */
		&buf,
		url,
		access_key_id, 
		secret_access_key);
	strcpy(result[i].exttbl_definition, stringbuffer_getstring(&buf));
	stringbuffer_release(&buf);
    }

    GDALClose(ogr_ds);

    return result;
}

static void
freeOgrFdwTable(OgrFdwTable *table)
{
    if ( table )
    {
	if ( table->tblname ) pfree(table->tblname);
	if ( table->cols ) pfree(table->cols);
	pfree(table);
    }
}

static int ogrFieldEntryCmpFunc(const void * a, const void * b)
{
    const char *a_name = ((OgrFieldEntry*)a)->fldname;
    const char *b_name = ((OgrFieldEntry*)b)->fldname;

    return strcasecmp(a_name, b_name);
}

static void
ogrReadColumnData(OssShapeReader *reader)
{
    TupleDesc tupdesc;
    int i;
    OgrFdwTable *tbl;
    OGRFeatureDefnH dfn;
    int ogr_ncols;
    int fid_count = 0;
    int geom_count = 0;
    int ogr_geom_count = 0;
    int field_count = 0;
    OgrFieldEntry *ogr_fields;
    int ogr_fields_count = 0;

    /* Blow away any existing table in the reader */
    if ( reader->table )
    {
	freeOgrFdwTable(reader->table);
	reader->table = NULL;
    }

    /* Fresh table */
    tbl = palloc0(sizeof(OgrFdwTable));

    /* One column for each PgSQL foreign table column */
    tupdesc = reader->tupdesc;
    tbl->ncols = tupdesc->natts;
    tbl->cols = palloc0(tbl->ncols * sizeof(OgrFdwColumn));

    elog(DEBUG1, "line363");
    /* Get OGR metadata ready */
    dfn = OGR_L_GetLayerDefn(reader->ogr_lyr);
    elog(DEBUG1, "line366");
    ogr_ncols = OGR_FD_GetFieldCount(dfn);
    elog(DEBUG1, "line368");
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(1,11,0)
    ogr_geom_count = OGR_FD_GetGeomFieldCount(dfn);
#else
    ogr_geom_count = ( OGR_FD_GetGeomType(dfn) != wkbNone ) ? 1 : 0;
#endif


    /* Prepare sorted list of OGR column names */
    /* TODO: change this to a hash table, to avoid repeated strcmp */
    /* We will search both the original and laundered OGR field names for matches */
    ogr_fields_count = 2 * ogr_ncols;
    ogr_fields = palloc0(ogr_fields_count * sizeof(OgrFieldEntry));
    for ( i = 0; i < ogr_ncols; i++ )
    {
	char *fldname = pstrdup(OGR_Fld_GetNameRef(OGR_FD_GetFieldDefn(dfn, i)));
	char *fldname_laundered = palloc(STR_MAX_LEN);
	strncpy(fldname_laundered, fldname, STR_MAX_LEN);
	ogrStringLaunder(fldname_laundered);
	ogr_fields[2*i].fldname = fldname;
	ogr_fields[2*i].fldnum = i;
	ogr_fields[2*i+1].fldname = fldname_laundered;
	ogr_fields[2*i+1].fldnum = i;
    }
    qsort(ogr_fields, ogr_fields_count, sizeof(OgrFieldEntry), ogrFieldEntryCmpFunc);

    /* loop through foreign table columns */
    for ( i = 0; i < tbl->ncols; i++ )
    {
	OgrFieldEntry *found_entry;
	OgrFieldEntry entry;

#if PG_VERSION_NUM >= 110000
	Form_pg_attribute att_tuple = &tupdesc->attrs[i];
#else
	Form_pg_attribute att_tuple = tupdesc->attrs[i];
#endif
	OgrFdwColumn col = tbl->cols[i];
	col.pgattnum = att_tuple->attnum;
	col.pgtype = att_tuple->atttypid;
	col.pgtypmod = att_tuple->atttypmod;
	col.pgattisdropped = att_tuple->attisdropped;

	elog(DEBUG1, "i=%d, line411", i);
	/* Skip filling in any further metadata about dropped columns */
	if ( col.pgattisdropped )
	    continue;

	/* Find the appropriate conversion functions */
	getTypeInputInfo(col.pgtype, &col.pginputfunc, &col.pginputioparam);
	getTypeBinaryInputInfo(col.pgtype, &col.pgrecvfunc, &col.pgrecvioparam);
	getTypeOutputInfo(col.pgtype, &col.pgoutputfunc, &col.pgoutputvarlena);
	getTypeBinaryOutputInfo(col.pgtype, &col.pgsendfunc, &col.pgsendvarlena);

	/* Get the PgSQL column name */
	col.pgname = pstrdup(NameStr(att_tuple->attname));

	/* Handle FID first */
	if ( strcaseeq(col.pgname, "fid") && (col.pgtype == INT4OID || col.pgtype == INT8OID) )
	{
	    if ( fid_count >= 1 )
		elog(ERROR, "FDW table 'DONOTKNOW' includes more than one FID column");

	    col.ogrvariant = OGR_FID;
	    col.ogrfldnum = fid_count++;
	    tbl->cols[i] = col;
	    continue;
	}
	elog(DEBUG1, "i=%d, line436, geom_count=%d, ogr_geom_count=%d, name=%s,col.pgtype=%d,!= %d", i, geom_count, ogr_geom_count, col.pgname,col.pgtype, GEOMETRYOID);

	/* If the OGR source has geometries, can we match them to Pg columns? */
	/* We'll match to the first ones we find, irrespective of name */
	if ( geom_count < ogr_geom_count && col.pgtype == GEOMETRYOID )
	{
	    elog(DEBUG1, "i=%d, get one geom", i);
	    col.ogrvariant = OGR_GEOMETRY;
	    col.ogrfldtype = OFTBinary;
	    col.ogrfldnum = geom_count++;
	    tbl->cols[i] = col;
	    continue;
	}

	/* Now we search for matches in the OGR fields */

	/* By default, search for the PgSQL column name */
	entry.fldname = col.pgname;
	entry.fldnum = 0;

	/* Search PgSQL column name in the OGR column name list */
	found_entry = bsearch(&entry, ogr_fields, ogr_fields_count, sizeof(OgrFieldEntry), ogrFieldEntryCmpFunc);

	/* Column name matched, so save this entry, if the types are consistent */
	if ( found_entry )
	{
	    OGRFieldDefnH fld = OGR_FD_GetFieldDefn(dfn, found_entry->fldnum);
	    OGRFieldType fldtype = OGR_Fld_GetType(fld);

	    /* Error if types mismatched when column names match */
	    //ogrCanConvertToPg(fldtype, col.pgtype, col.pgname, tblname);

	    col.ogrvariant = OGR_FIELD;
	    col.ogrfldnum = found_entry->fldnum;
	    col.ogrfldtype = fldtype;
	    field_count++;
	}
	else
	{
	    col.ogrvariant = OGR_UNMATCHED;
	}
	tbl->cols[i] = col;
    }

    elog(DEBUG2, "ogrReadColumnData matched %d FID, %d GEOM, %d FIELDS out of %d PGSQL COLUMNS", fid_count, geom_count, field_count, tbl->ncols);

    /* Clean up */

    reader->table = tbl;
    for( i = 0; i < 2*ogr_ncols; i++ )
	if ( ogr_fields[i].fldname ) pfree(ogr_fields[i].fldname);
    pfree(ogr_fields);
    //heap_close(rel, NoLock);

    return;
}


static inline void
ogrNullSlot(Datum *values, bool *nulls, int i)
{
    values[i] = PointerGetDatum(NULL);
    nulls[i] = true;
}

static OGRErr
ogrFeatureToSlot(const OGRFeatureH feat,
	Datum *values,
	bool *nulls,
	OssShapeReader *reader)
{
    const OgrFdwTable *tbl = reader->table;
    int i;
    TupleDesc tupdesc = reader->tupdesc;
    int have_typmod_funcs = (reader->setsridfunc && reader->typmodsridfunc);

    elog(DEBUG1, "---->ogrFeatureToSlot");
    /* Check our assumption that slot and setup data match */
    if ( tbl->ncols != tupdesc->natts )
    {
	elog(ERROR, "FDW metadata table and exec table have mismatching number of columns");
	return OGRERR_FAILURE;
    }

    /* For each pgtable column, get a value from OGR */
    for ( i = 0; i < tbl->ncols; i++ )
    {
	OgrFdwColumn col = tbl->cols[i];
	const char *pgname = col.pgname; //fid
	Oid pgtype = col.pgtype;//20
	int pgtypmod = col.pgtypmod;//-1
	Oid pginputfunc = col.pginputfunc;//460
	int ogrfldnum = col.ogrfldnum;//0 0
	OGRFieldType ogrfldtype = col.ogrfldtype;//OFTInteger OFTBinary
	OgrColumnVariant ogrvariant = col.ogrvariant;//OGR_FID OGR_GEOMETRY

	/*
	 * Fill in dropped attributes with NULL
	 */
	if ( col.pgattisdropped ) // false
	{
	    ogrNullSlot(values, nulls, i);
	    continue;
	}

	if ( ogrvariant == OGR_FID )
	{
	    GIntBig fid = OGR_F_GetFID(feat);

	    if ( fid == OGRNullFID )
	    {
		ogrNullSlot(values, nulls, i);
	    }
	    else
	    {
		char fidstr[256];
		snprintf(fidstr, 256, OGR_FDW_FRMT_INT64, OGR_FDW_CAST_INT64(fid));

		nulls[i] = false;
		values[i] = pgDatumFromCString(fidstr, pgtype, pgtypmod, pginputfunc); //0
	    }
	}
	else if ( ogrvariant == OGR_GEOMETRY )
	{
	    int wkbsize;
	    int varsize;
	    bytea *varlena;
	    unsigned char *wkb;
	    OGRErr err;

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(1,11,0)
	    OGRGeometryH geom = OGR_F_GetGeomFieldRef(feat, ogrfldnum);
#else
	    OGRGeometryH geom = OGR_F_GetGeometryRef(feat);
#endif

	    /* No geometry ? NULL */
	    if ( ! geom )
	    {
		/* No geometry column, so make the output null */
		ogrNullSlot(values, nulls, i);
		continue;
	    }

	    /*
	     *                          * Start by generating standard PgSQL variable length byte
	     *                                                   * buffer, with WKB filled into the data area.
	     *                                                                            */
	    wkbsize = OGR_G_WkbSize(geom); //21
	    varsize = wkbsize + VARHDRSZ; //25
	    varlena = palloc(varsize);
	    wkb = (unsigned char *)VARDATA(varlena);
	    err = OGR_G_ExportToWkb(geom, wkbNDR, wkb);
	    SET_VARSIZE(varlena, varsize);

	    /* Couldn't create WKB from OGR geometry? error */
	    if ( err != OGRERR_NONE )
	    {
		return err;
	    }

	    if ( pgtype == BYTEAOID )
	    {
		/*
		 * Nothing special to do for bytea, just send the varlena data through!
		 */
		nulls[i] = false;
		values[i] = PointerGetDatum(varlena);
	    }
	    else if ( pgtype == GEOMETRYOID )
	    {
		/*
		 * For geometry we need to convert the varlena WKB data into a serialized
		 * geometry (aka "gserialized"). For that, we can use the type's "recv" function
		 * which takes in WKB and spits out serialized form, or the "input" function
		 * that takes in HEXWKB. The "input" function is more lax about geometry
		 * structure errors (unclosed polys, etc).
		 */
#ifdef OGR_FDW_HEXWKB
		char *hexwkb = ogrBytesToHex(wkb, wkbsize); //"0101000000C00497D1162CB93F8CBAEF08A080E63F"
		/*
		 * Use the input function to convert the WKB from OGR into
		 * a PostGIS internal format.
		 */
		nulls[i] = false;
		values[i] = OidFunctionCall1(col.pginputfunc, PointerGetDatum(hexwkb));
		pfree(hexwkb);
#else
		/*
		 * The "recv" function expects to receive a StringInfo pointer
		 * on the first argument, so we form one of those ourselves by
		 * hand. Rather than copy into a fresh buffer, we'll just use the
		 * existing varlena buffer and point to the data area.
		 *
		 * The "recv" function tests for basic geometry validity,
		 * things like polygon closure, etc. So don't feed it junk.
		 */
		StringInfoData strinfo;
		strinfo.data = (char *)wkb;
		strinfo.len = wkbsize;
		strinfo.maxlen = strinfo.len;
		strinfo.cursor = 0;

		/*
		 * Use the recv function to convert the WKB from OGR into
		 * a PostGIS internal format.
		 */
		nulls[i] = false;
		values[i] = OidFunctionCall1(col.pgrecvfunc, PointerGetDatum(&strinfo));
#endif

		/*
		 * Apply the typmod restriction to the incoming geometry, so it's
		 * not really a restriction anymore, it's more like a requirement.
		 *
		 * TODO: In the case where the OGR input actually *knows* what SRID
		 * it is, we should actually apply *that* and let the restriction run
		 * its usual course.
		 */
		if ( have_typmod_funcs && col.pgtypmod >= 0 )
		{
		    Datum srid = OidFunctionCall1(reader->typmodsridfunc, Int32GetDatum(col.pgtypmod));
		    values[i] = OidFunctionCall2(reader->setsridfunc, values[i], srid);
		}
	    }
	    else
	    {
		elog(NOTICE, "conversion to geometry called with column type not equal to bytea or geometry");
		ogrNullSlot(values, nulls, i);
	    }

	}
	else if ( ogrvariant == OGR_FIELD )
	{
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(2,2,0)
	    int field_not_null = OGR_F_IsFieldSet(feat, ogrfldnum) && ! OGR_F_IsFieldNull(feat, ogrfldnum);
#else
	    int field_not_null = OGR_F_IsFieldSet(feat, ogrfldnum);
#endif

	    /* Ensure that the OGR data type fits the destination Pg column */
	    //ogrCanConvertToPg(ogrfldtype, pgtype, pgname, tbl->tblname);

	    /* Only convert non-null fields */
	    if ( field_not_null )
	    {
		switch(ogrfldtype)
		{
		    case OFTBinary:
			{
			    /*
			     * Convert binary fields to bytea directly
			     */
			    int bufsize;
			    GByte *buf = OGR_F_GetFieldAsBinary(feat, ogrfldnum, &bufsize);
			    int varsize = bufsize + VARHDRSZ;
			    bytea *varlena = palloc(varsize);
			    memcpy(VARDATA(varlena), buf, bufsize);
			    SET_VARSIZE(varlena, varsize);
			    nulls[i] = false;
			    values[i] = PointerGetDatum(varlena);
			    break;
			}
		    case OFTInteger:
		    case OFTReal:
		    case OFTString:
#if GDAL_VERSION_MAJOR >= 2
		    case OFTInteger64:
#endif
			{
			    const char *cstr_in = OGR_F_GetFieldAsString(feat, ogrfldnum);
			    size_t cstr_len = cstr_in ? strlen(cstr_in) : 0;
			    if ( cstr_in && cstr_len > 0 )
			    {
				char *cstr_decoded;
				if(reader->lyr_utf8)
				    cstr_decoded = pg_any_to_server(cstr_in, cstr_len, PG_UTF8);
				else
				    cstr_decoded = pstrdup(cstr_in);
				nulls[i] = false;
				values[i] = pgDatumFromCString(cstr_decoded, pgtype, pgtypmod, pginputfunc);
			    }
			    else
			    {
				ogrNullSlot(values, nulls, i);
			    }
			    break;
			}
		    case OFTDate:
		    case OFTTime:
		    case OFTDateTime:
			{
			    /*
			     * OGR date/times have a weird access method, so we use that to pull
			     * out the raw data and turn it into a string for PgSQL's (very
			     * sophisticated) date/time parsing routines to handle.
			     */
			    int year, month, day, hour, minute, second, tz;
			    char cstr[256];

			    OGR_F_GetFieldAsDateTime(feat, ogrfldnum,
				    &year, &month, &day,
				    &hour, &minute, &second, &tz);

			    if ( ogrfldtype == OFTDate )
			    {
				snprintf(cstr, 256, "%d-%02d-%02d", year, month, day);
			    }
			    else if ( ogrfldtype == OFTTime )
			    {
				snprintf(cstr, 256, "%02d:%02d:%02d", hour, minute, second);
			    }
			    else
			    {
				snprintf(cstr, 256, "%d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
			    }
			    nulls[i] = false;
			    values[i] = pgDatumFromCString(cstr, pgtype, pgtypmod, pginputfunc);
			    break;

			}
		    case OFTIntegerList:
		    case OFTRealList:
		    case OFTStringList:
			{
			    /* TODO, map these OGR array types into PgSQL arrays (fun!) */
			    elog(ERROR, "unsupported OGR array type \"%s\"", OGR_GetFieldTypeName(ogrfldtype));
			    break;
			}
		    default:
			{
			    elog(ERROR, "unsupported OGR type \"%s\"", OGR_GetFieldTypeName(ogrfldtype));
			    break;
			}

		}
	    }
	    else
	    {
		ogrNullSlot(values, nulls, i);
	    }
	}
	/* Fill in unmatched columns with NULL */
	else if ( ogrvariant == OGR_UNMATCHED )
	{
	    ogrNullSlot(values, nulls, i);
	}
	else
	{
	    elog(ERROR, "OGR FDW unsupported column variant in \"%s\", %d", pgname, ogrvariant);
	    return OGRERR_FAILURE;
	}

    }

    /* done! */
    return OGRERR_NONE;
}

GDALDatasetH ogrGetDataset(char *source)
{
    GDALDatasetH ogr_ds = NULL;

//    GDALAllRegister();

#if GDAL_VERSION_MAJOR < 2
    ogr_ds = OGROpen(source, FALSE, NULL);
#else
    elog(DEBUG1, "getenv=%s # %s", getenv("AWS_SECRET_ACCESS_KEY"), getenv("AWS_ACCESS_KEY_ID"));
    ogr_ds = GDALOpenEx(source,
	    GDAL_OF_READONLY,
	    NULL, NULL, NULL);
#endif


    if ( ! ogr_ds )
    {
	elog(ERROR, "Could not connect to source '%s'", source);
	return NULL;
    }

    return ogr_ds;
}

OGRLayerH ogrGetLayer(GDALDatasetH ogr_ds, char *layer_name)
{
    OGRLayerH ogr_lyr = NULL;

    ogr_lyr = GDALDatasetGetLayerByName(ogr_ds, layer_name);
    if ( ! ogr_lyr )
    {
	CPLError(CE_Failure, CPLE_AppDefined, "Could not find layer '%s' ", layer_name);
	return NULL;
    }

    return ogr_lyr;
}

HeapTuple oss_shape_read_tuple(OssShapeReader * reader)
{
    HeapTuple tuple = NULL;
    OGRFeatureH feat;
    TupleDesc tupdesc = reader->tupdesc;
    Datum *values = (Datum *)palloc0(sizeof(Datum) * reader->ncolumns);
    bool *nulls = (bool *)palloc0(sizeof(bool) * reader->ncolumns);

    elog(DEBUG1, "--->oss_shape_read_tuple");
    if ( reader->tupleIndex == 0 )
    {
	OGR_L_ResetReading(reader->ogr_lyr);
    }

    /* If we rectreive a feature from OGR, copy it over into the slot */
    feat = OGR_L_GetNextFeature(reader->ogr_lyr);
    if ( feat )
    {
	/* convert result to arrays of values and null indicators */
	if ( OGRERR_NONE != ogrFeatureToSlot(feat, values, nulls, reader) )
	    elog(ERROR, "failure reading OGR data source");

	/* store the virtual tuple */
	PG_TRY();
	{
	    tuple = heap_form_tuple(tupdesc, values, nulls);
	}
	PG_CATCH();
	{
	    EmitErrorReport();
	    FlushErrorState();
	    tuple = NULL;
	}
	PG_END_TRY();

	if (tuple == NULL) {
	    elog(ERROR, "cannot form tuple");
	}

	Assert(checkTuple(tuple, tupdesc));

	elog(DEBUG4, "--->return tuple");

	/* increment row count */
	reader->tupleIndex++;

	/* Release OGR feature object */
	OGR_F_Destroy(feat);
    }
    elog(DEBUG1, "<----oss_shape_read_tuple");

    return tuple;
}

static Oid
ogrLookupGeometryFunctionOid(const char *proname)
{
    List *names;
    FuncCandidateList clist;

    /* This only works if PostGIS is installed */
    if ( GEOMETRYOID == InvalidOid || GEOMETRYOID == BYTEAOID )
	return InvalidOid;

    names = stringToQualifiedNameList(proname);
#if PG_VERSION_NUM < 90400
    clist = FuncnameGetCandidates(names, -1, NIL, false, false);
#else
    clist = FuncnameGetCandidates(names, -1, NIL, false, false, false);
#endif
    if ( streq(proname, "st_setsrid") )
    {
	do
	{
	    int i;
	    for ( i = 0; i < clist->nargs; i++ )
	    {
		if ( clist->args[i] == GEOMETRYOID )
		    return clist->oid;
	    }
	}
	while( (clist = clist->next) );
    }
    else if ( streq(proname, "postgis_typmod_srid") )
    {
	return clist->oid;
    }

    return InvalidOid;
}

OssShapeReader *create_shape_reader(FileScanDesc scan,  GDALDatasetH ogr_ds, OGRLayerH ogr_lyr)
{
    OssShapeReader *reader = NULL;
//    if (GpIdentity.segindex == 0)
//	return NULL;

    reader = palloc0(sizeof(OssShapeReader));
    if (reader == NULL)
	elog(ERROR, "malloc OssShapeReader failed");

    reader->scan = scan;
    reader->tupdesc = scan->fs_tupDesc;
    reader->ncolumns = scan->fs_tupDesc->natts;
    reader->ogr_ds = ogr_ds;
    reader->ogr_lyr = ogr_lyr;
    reader->tupleIndex = 0;
    reader->setsridfunc = ogrLookupGeometryFunctionOid("st_setsrid");
    reader->typmodsridfunc = ogrLookupGeometryFunctionOid("postgis_typmod_srid");
    reader->lyr_utf8 =false;// OGR_L_TestCapability(ogr_lyr, OLCStringsAsUTF8);
    ogrReadColumnData(reader);
    elog(DEBUG1, "<---ogrReadColumnData");

    return reader;
}

void init_geomtype()
{
    Oid typoid = TypenameGetTypid("geometry");
    if (OidIsValid(typoid) && get_typisdefined(typoid))
    {
	GEOMETRYOID = typoid;
    }
    else
    {
	GEOMETRYOID = BYTEAOID;
    }
    if(putenv("POSTGIS_GDAL_ENABLED_DRIVERS=ENABLE_ALL"))
	elog(ERROR, "putenv failed.");
}

void destroy_shape_reader(OssShapeReader *reader)
{
    GDALClose(reader->ogr_ds);
    OGRCleanupAll();
    pfree(reader);
}
