#include "ossGisRaster.h"

void raster_destroy(rt_raster raster) {
    uint16_t i;
    uint16_t nbands = rt_raster_get_num_bands(raster);
    for (i = 0; i < nbands; i++) {
	rt_band band = rt_raster_get_band(raster, i);
	if (band == NULL) continue;

	if (!rt_band_is_offline(band) && !rt_band_get_ownsdata_flag(band)) {
	    void* mem = rt_band_get_data(band);
	    if (mem) rtdealloc(mem);
	}
	rt_band_destroy(band);
    }
    rt_raster_destroy(raster);
}

rt_raster
rt_raster_from_gdal_dataset(GDALDatasetH ds) {
    rt_raster rast = NULL;
    double gt[6] = {0};
    CPLErr cplerr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t numBands = 0;
    int i = 0;
    char *authname = NULL;
    char *authcode = NULL;

    GDALRasterBandH gdband = NULL;
    GDALDataType gdpixtype = GDT_Unknown;
    rt_band band;
    int32_t idx;
    rt_pixtype pt = PT_END;
    uint32_t ptlen = 0;
    int hasnodata = 0;
    double nodataval;

    int x;
    int y;

    int nXBlocks, nYBlocks;
    int nXBlockSize, nYBlockSize;
    int iXBlock, iYBlock;
    int nXValid, nYValid;
    int iY;

    uint8_t *values = NULL;
    uint32_t valueslen = 0;
    uint8_t *ptr = NULL;


    /* raster size */
    width = GDALGetRasterXSize(ds);
    height = GDALGetRasterYSize(ds);

    /* create new raster */
    rast = rt_raster_new(width, height);
    if (NULL == rast) {
	rterror("rt_raster_from_gdal_dataset: Out of memory allocating new raster");
	return NULL;
    }
    /* get raster attributes */
    cplerr = GDALGetGeoTransform(ds, gt);
    if (cplerr != CE_None) {
	//        RASTER_DEBUG(4, "Using default geotransform matrix (0, 1, 0, 0, 0, -1)");
	gt[0] = 0;
	gt[1] = 1;
	gt[2] = 0;
	gt[3] = 0;
	gt[4] = 0;
	gt[5] = -1;
    }

    /* apply raster attributes */
    rast->ipX = gt[0];
    rast->scaleX = gt[1];
    rast->skewX = gt[2];
    rast->ipY = gt[3];
    rast->skewY = gt[4];
    rast->scaleY = gt[5];

    /* srid */
    if (rt_util_gdal_sr_auth_info(ds, &authname, &authcode) == ES_NONE) {
	if (
		authname != NULL &&
		strcmp(authname, "EPSG") == 0 &&
		authcode != NULL
	   ) {
	    rt_raster_set_srid(rast, atoi(authcode));
	}

	if (authname != NULL)
	    rtdealloc(authname);
	if (authcode != NULL)
	    rtdealloc(authcode);
    }

    numBands = GDALGetRasterCount(ds);


    /* copy bands */
    for (i = 1; i <= numBands; i++) {
	gdband = NULL;
	gdband = GDALGetRasterBand(ds, i);

	/* pixtype */
	gdpixtype = GDALGetRasterDataType(gdband);
	pt = rt_util_gdal_datatype_to_pixtype(gdpixtype);
	if (pt == PT_END) {
	    rterror("rt_raster_from_gdal_dataset: Unknown pixel type for GDAL band");
	    rt_raster_destroy(rast);
	    return NULL;
	}
	ptlen = rt_pixtype_size(pt);

	/* size: width and height */
	width = GDALGetRasterBandXSize(gdband);
	height = GDALGetRasterBandYSize(gdband);

	/* nodata */
	nodataval = GDALGetRasterNoDataValue(gdband, &hasnodata);

	/* create band object */
	idx = rt_raster_generate_new_band(
		rast, pt,
		(hasnodata ? nodataval : 0),
		hasnodata, nodataval, rt_raster_get_num_bands(rast)
		);
	if (idx < 0) {
	    rterror("rt_raster_from_gdal_dataset: Could not allocate memory for raster band");
	    rt_raster_destroy(rast);
	    return NULL;
	}
	band = rt_raster_get_band(rast, idx);

	/* this makes use of GDAL's "natural" blocks */
	GDALGetBlockSize(gdband, &nXBlockSize, &nYBlockSize);
	nXBlocks = (width + nXBlockSize - 1) / nXBlockSize;
	nYBlocks = (height + nYBlockSize - 1) / nYBlockSize;

	/* allocate memory for values */
	valueslen = ptlen * nXBlockSize * nYBlockSize;
	values = rtalloc(valueslen);
	if (values == NULL) {
	    rterror("rt_raster_from_gdal_dataset: Could not allocate memory for GDAL band pixel values");
	    rt_raster_destroy(rast);
	    return NULL;
	}

	for (iYBlock = 0; iYBlock < nYBlocks; iYBlock++) {
	    for (iXBlock = 0; iXBlock < nXBlocks; iXBlock++) {
		x = iXBlock * nXBlockSize;
		y = iYBlock * nYBlockSize;

		memset(values, 0, valueslen);

		/* valid block width */
		if ((iXBlock + 1) * nXBlockSize > width)
		    nXValid = width - (iXBlock * nXBlockSize);
		else
		    nXValid = nXBlockSize;

		/* valid block height */
		if ((iYBlock + 1) * nYBlockSize > height)
		    nYValid = height - (iYBlock * nYBlockSize);
		else
		    nYValid = nYBlockSize;

		cplerr = GDALRasterIO(
			gdband, GF_Read,
			x, y,
			nXValid, nYValid,
			values, nXValid, nYValid,
			gdpixtype,
			0, 0
			);
		if (cplerr != CE_None) {
		    rterror("rt_raster_from_gdal_dataset: Could not get data from GDAL raster");
		    rtdealloc(values);
		    rt_raster_destroy(rast);
		    return NULL;
		}

		/* if block width is same as raster width, shortcut */
		if (nXBlocks == 1 && nYBlockSize > 1 && nXValid == width) {
		    x = 0;
		    y = nYBlockSize * iYBlock;

		    rt_band_set_pixel_line(band, x, y, values, nXValid * nYValid);
		}
		else {
		    ptr = values;
		    x = nXBlockSize * iXBlock;
		    for (iY = 0; iY < nYValid; iY++) {
			y = iY + (nYBlockSize * iYBlock);

			rt_band_set_pixel_line(band, x, y, ptr, nXValid);
			ptr += (nXValid * ptlen);
		    }
		}
	    }
	}

	/* free memory */
	rtdealloc(values);
    }

    return rast;
}

rt_pixtype
rt_util_gdal_datatype_to_pixtype(GDALDataType gdt) {
    switch (gdt) {
	case GDT_Byte:
	    return PT_8BUI;
	case GDT_UInt16:
	    return PT_16BUI;
	case GDT_Int16:
	    return PT_16BSI;
	case GDT_UInt32:
	    return PT_32BUI;
	case GDT_Int32:
	    return PT_32BSI;
	case GDT_Float32:
	    return PT_32BF;
	case GDT_Float64:
	    return PT_64BF;
	default:
	    return PT_END;
    }

    return PT_END;
}

