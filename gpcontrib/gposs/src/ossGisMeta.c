#include "ossGisMeta.h"
static int 
GDALInfoReportCorner( char *buffer, int size,
		      GDALDatasetH hDataset,
                      OGRCoordinateTransformationH hTransform,
                      const char * corner_name,
                      double x, double y );

/*
int GetGisMeta2(char *meta) 
{
    strcpy(meta,"test\nline2222222222222222222222\n    33fff");
    return 0;
}
*/

int GetGisMeta(GDALDatasetH hDataset, char* buffer) 

{
    GDALRasterBandH	hBand = NULL;
    int			i, iBand, size = 0;
    double		adfGeoTransform[6];
    GDALDriverH		hDriver;
    char		**papszMetadata;
    int                 bComputeMinMax = FALSE, bSample = FALSE;
    int                 bShowGCPs = TRUE, bShowMetadata = TRUE, bShowRAT=TRUE;
    int                 bStats = FALSE, bApproxStats = TRUE, iMDD;
    int                 bShowColorTable = TRUE;
    int                 bReportProj4 = FALSE;
    char              **papszExtraMDDomains = NULL;
    int                 bListMDD = FALSE;
    const char  *pszProjection = NULL;
    OGRCoordinateTransformationH hTransform = NULL;

/* -------------------------------------------------------------------- */
/*      Report general info.                                            */
/* -------------------------------------------------------------------- */
    hDriver = GDALGetDatasetDriver( hDataset );
    size += sprintf( buffer + size, "Driver: %s/%s\n",
            GDALGetDriverShortName( hDriver ),
            GDALGetDriverLongName( hDriver ) );

    size += sprintf( buffer + size, "Size is %d, %d\n",
            GDALGetRasterXSize( hDataset ), 
            GDALGetRasterYSize( hDataset ) );

/* -------------------------------------------------------------------- */
/*      Report projection.                                              */
/* -------------------------------------------------------------------- */
    if( GDALGetProjectionRef( hDataset ) != NULL )
    {
        OGRSpatialReferenceH  hSRS;
        char		      *pszProjection;

        pszProjection = (char *) GDALGetProjectionRef( hDataset );

        hSRS = OSRNewSpatialReference(NULL);
        if( OSRImportFromWkt( hSRS, &pszProjection ) == CE_None )
        {
            char	*pszPrettyWkt = NULL;

            OSRExportToPrettyWkt( hSRS, &pszPrettyWkt, FALSE );
            size += sprintf( buffer + size, "Coordinate System is:\n%s\n", pszPrettyWkt );
            CPLFree( pszPrettyWkt );
        }
        else
            size += sprintf( buffer + size, "Coordinate System is `%s'\n",
                    GDALGetProjectionRef( hDataset ) );

        if ( bReportProj4 ) 
        {
            char *pszProj4 = NULL;
            OSRExportToProj4( hSRS, &pszProj4 );
            size += sprintf( buffer + size,"PROJ.4 string is:\n\'%s\'\n",pszProj4);
            CPLFree( pszProj4 ); 
        }

        OSRDestroySpatialReference( hSRS );
    }

/* -------------------------------------------------------------------- */
/*      Report Geotransform.                                            */
/* -------------------------------------------------------------------- */
    if( GDALGetGeoTransform( hDataset, adfGeoTransform ) == CE_None )
    {
        if( adfGeoTransform[2] == 0.0 && adfGeoTransform[4] == 0.0 )
        {
            size += sprintf( buffer + size, "Origin = (%.15f,%.15f)\n",
                    adfGeoTransform[0], adfGeoTransform[3] );

            size += sprintf( buffer + size, "Pixel Size = (%.15f,%.15f)\n",
                    adfGeoTransform[1], adfGeoTransform[5] );
        }
        else
            size += sprintf( buffer + size, "GeoTransform =\n"
                    "  %.16g, %.16g, %.16g\n"
                    "  %.16g, %.16g, %.16g\n", 
                    adfGeoTransform[0],
                    adfGeoTransform[1],
                    adfGeoTransform[2],
                    adfGeoTransform[3],
                    adfGeoTransform[4],
                    adfGeoTransform[5] );
    }

/* -------------------------------------------------------------------- */
/*      Report GCPs.                                                    */
/* -------------------------------------------------------------------- */
    if( bShowGCPs && GDALGetGCPCount( hDataset ) > 0 )
    {
        if (GDALGetGCPProjection(hDataset) != NULL)
        {
            OGRSpatialReferenceH  hSRS;
            char		      *pszProjection;

            pszProjection = (char *) GDALGetGCPProjection( hDataset );

            hSRS = OSRNewSpatialReference(NULL);
            if( OSRImportFromWkt( hSRS, &pszProjection ) == CE_None )
            {
                char	*pszPrettyWkt = NULL;

                OSRExportToPrettyWkt( hSRS, &pszPrettyWkt, FALSE );
                size += sprintf( buffer + size, "GCP Projection = \n%s\n", pszPrettyWkt );
                CPLFree( pszPrettyWkt );
            }
            else
                size += sprintf( buffer + size, "GCP Projection = %s\n",
                        GDALGetGCPProjection( hDataset ) );

            OSRDestroySpatialReference( hSRS );
        }

        for( i = 0; i < GDALGetGCPCount(hDataset); i++ )
        {
            const GDAL_GCP	*psGCP;
            
            psGCP = GDALGetGCPs( hDataset ) + i;

            size += sprintf( buffer + size, "GCP[%3d]: Id=%s, Info=%s\n"
                    "          (%.15g,%.15g) -> (%.15g,%.15g,%.15g)\n", 
                    i, psGCP->pszId, psGCP->pszInfo, 
                    psGCP->dfGCPPixel, psGCP->dfGCPLine, 
                    psGCP->dfGCPX, psGCP->dfGCPY, psGCP->dfGCPZ );
        }
    }

/* -------------------------------------------------------------------- */
/*      Report metadata.                                                */
/* -------------------------------------------------------------------- */

    if( bListMDD )
    {
        char** papszMDDList = GDALGetMetadataDomainList( hDataset );
        char** papszIter = papszMDDList;

        if( papszMDDList != NULL )
            size += sprintf( buffer + size, "Metadata domains:\n" );
        while( papszIter != NULL && *papszIter != NULL )
        {
            if( EQUAL(*papszIter, "") )
                size += sprintf( buffer + size, "  (default)\n");
            else
                size += sprintf( buffer + size, "  %s\n", *papszIter );
            papszIter ++;
        }
        CSLDestroy(papszMDDList);
    }

    papszMetadata = (bShowMetadata) ? GDALGetMetadata( hDataset, NULL ) : NULL;
    if( bShowMetadata && CSLCount(papszMetadata) > 0 )
    {
        size += sprintf( buffer + size, "Metadata:\n" );
        for( i = 0; papszMetadata[i] != NULL; i++ )
        {
            size += sprintf( buffer + size, "  %s\n", papszMetadata[i] );
        }
    }

    if( papszExtraMDDomains != NULL && EQUAL(papszExtraMDDomains[0], "all") &&
        papszExtraMDDomains[1] == NULL )
    {
        char** papszMDDList = GDALGetMetadataDomainList( hDataset );
        char** papszIter = papszMDDList;

        CSLDestroy(papszExtraMDDomains);
        papszExtraMDDomains = NULL;

        while( papszIter != NULL && *papszIter != NULL )
        {
            if( !EQUAL(*papszIter, "") &&
                !EQUAL(*papszIter, "IMAGE_STRUCTURE") &&
                !EQUAL(*papszIter, "SUBDATASETS") &&
                !EQUAL(*papszIter, "GEOLOCATION") &&
                !EQUAL(*papszIter, "RPC") )
            {
                papszExtraMDDomains = CSLAddString(papszExtraMDDomains, *papszIter);
            }
            papszIter ++;
        }
        CSLDestroy(papszMDDList);
    }

    for( iMDD = 0; bShowMetadata && iMDD < CSLCount(papszExtraMDDomains); iMDD++ )
    {
        papszMetadata = GDALGetMetadata( hDataset, papszExtraMDDomains[iMDD] );
        if( CSLCount(papszMetadata) > 0 )
        {
            size += sprintf( buffer + size, "Metadata (%s):\n", papszExtraMDDomains[iMDD]);
            for( i = 0; papszMetadata[i] != NULL; i++ )
            {
                if (EQUALN(papszExtraMDDomains[iMDD], "xml:", 4))
                    size += sprintf( buffer + size, "%s\n", papszMetadata[i] );
                else
                    size += sprintf( buffer + size, "  %s\n", papszMetadata[i] );
            }
        }
    }

/* -------------------------------------------------------------------- */
/*      Report "IMAGE_STRUCTURE" metadata.                              */
/* -------------------------------------------------------------------- */
    papszMetadata = (bShowMetadata) ? GDALGetMetadata( hDataset, "IMAGE_STRUCTURE" ) : NULL;
    if( bShowMetadata && CSLCount(papszMetadata) > 0 )
    {
        size += sprintf( buffer + size, "Image Structure Metadata:\n" );
        for( i = 0; papszMetadata[i] != NULL; i++ )
        {
            size += sprintf( buffer + size, "  %s\n", papszMetadata[i] );
        }
    }

/* -------------------------------------------------------------------- */
/*      Report subdatasets.                                             */
/* -------------------------------------------------------------------- */
    papszMetadata = GDALGetMetadata( hDataset, "SUBDATASETS" );
    if( CSLCount(papszMetadata) > 0 )
    {
        size += sprintf( buffer + size, "Subdatasets:\n" );
        for( i = 0; papszMetadata[i] != NULL; i++ )
        {
            size += sprintf( buffer + size, "  %s\n", papszMetadata[i] );
        }
    }

/* -------------------------------------------------------------------- */
/*      Report geolocation.                                             */
/* -------------------------------------------------------------------- */
    papszMetadata = (bShowMetadata) ? GDALGetMetadata( hDataset, "GEOLOCATION" ) : NULL;
    if( bShowMetadata && CSLCount(papszMetadata) > 0 )
    {
        size += sprintf( buffer + size, "Geolocation:\n" );
        for( i = 0; papszMetadata[i] != NULL; i++ )
        {
            size += sprintf( buffer + size, "  %s\n", papszMetadata[i] );
        }
    }

/* -------------------------------------------------------------------- */
/*      Report RPCs                                                     */
/* -------------------------------------------------------------------- */
    papszMetadata = (bShowMetadata) ? GDALGetMetadata( hDataset, "RPC" ) : NULL;
    if( bShowMetadata && CSLCount(papszMetadata) > 0 )
    {
        size += sprintf( buffer + size, "RPC Metadata:\n" );
        for( i = 0; papszMetadata[i] != NULL; i++ )
        {
            size += sprintf( buffer + size, "  %s\n", papszMetadata[i] );
        }
    }

/* -------------------------------------------------------------------- */
/*      Setup projected to lat/long transform if appropriate.           */
/* -------------------------------------------------------------------- */
    if( GDALGetGeoTransform( hDataset, adfGeoTransform ) == CE_None )
        pszProjection = GDALGetProjectionRef(hDataset);

    if( pszProjection != NULL && strlen(pszProjection) > 0 )
    {
        OGRSpatialReferenceH hProj, hLatLong = NULL;

        hProj = OSRNewSpatialReference( pszProjection );
        if( hProj != NULL )
            hLatLong = OSRCloneGeogCS( hProj );

        if( hLatLong != NULL )
        {
            CPLPushErrorHandler( CPLQuietErrorHandler );
            hTransform = OCTNewCoordinateTransformation( hProj, hLatLong );
            CPLPopErrorHandler();
            
            OSRDestroySpatialReference( hLatLong );
        }

        if( hProj != NULL )
            OSRDestroySpatialReference( hProj );
    }

/* -------------------------------------------------------------------- */
/*      Report corners.                                                 */
/* -------------------------------------------------------------------- */
    size += sprintf( buffer + size, "Corner Coordinates:\n" );
    size = GDALInfoReportCorner( buffer, size, hDataset, hTransform, "Upper Left", 
                          0.0, 0.0 );
    size = GDALInfoReportCorner( buffer, size, hDataset, hTransform, "Lower Left", 
                          0.0, GDALGetRasterYSize(hDataset));
    size = GDALInfoReportCorner( buffer, size, hDataset, hTransform, "Upper Right", 
                          GDALGetRasterXSize(hDataset), 0.0 );
    size = GDALInfoReportCorner( buffer, size, hDataset, hTransform, "Lower Right", 
                          GDALGetRasterXSize(hDataset), 
                          GDALGetRasterYSize(hDataset) );
    size = GDALInfoReportCorner( buffer, size, hDataset, hTransform, "Center", 
                          GDALGetRasterXSize(hDataset)/2.0, 
                          GDALGetRasterYSize(hDataset)/2.0 );

    if( hTransform != NULL )
    {
        OCTDestroyCoordinateTransformation( hTransform );
        hTransform = NULL;
    }
    
/* ==================================================================== */
/*      Loop over bands.                                                */
/* ==================================================================== */
    for( iBand = 0; iBand < GDALGetRasterCount( hDataset ); iBand++ )
    {
        double      dfMin, dfMax, adfCMinMax[2], dfNoData;
        int         bGotMin, bGotMax, bGotNodata, bSuccess;
        int         nBlockXSize, nBlockYSize, nMaskFlags;
        double      dfMean, dfStdDev;
        GDALColorTableH	hTable;
        CPLErr      eErr;

        hBand = GDALGetRasterBand( hDataset, iBand+1 );

        if( bSample )
        {
            float afSample[10000];
            int   nCount;

            nCount = GDALGetRandomRasterSample( hBand, 10000, afSample );
            size += sprintf( buffer + size, "Got %d samples.\n", nCount );
        }
        
        GDALGetBlockSize( hBand, &nBlockXSize, &nBlockYSize );
        size += sprintf( buffer + size, "Band %d Block=%dx%d Type=%s, ColorInterp=%s\n", iBand+1,
                nBlockXSize, nBlockYSize,
                GDALGetDataTypeName(
                    GDALGetRasterDataType(hBand)),
                GDALGetColorInterpretationName(
                    GDALGetRasterColorInterpretation(hBand)) );

        if( GDALGetDescription( hBand ) != NULL 
            && strlen(GDALGetDescription( hBand )) > 0 )
            size += sprintf( buffer + size, "  Description = %s\n", GDALGetDescription(hBand) );

        dfMin = GDALGetRasterMinimum( hBand, &bGotMin );
        dfMax = GDALGetRasterMaximum( hBand, &bGotMax );
        if( bGotMin || bGotMax || bComputeMinMax )
        {
            size += sprintf( buffer + size, "  " );
            if( bGotMin )
                size += sprintf( buffer + size, "Min=%.3f ", dfMin );
            if( bGotMax )
                size += sprintf( buffer + size, "Max=%.3f ", dfMax );
        
            if( bComputeMinMax )
            {
                CPLErrorReset();
                GDALComputeRasterMinMax( hBand, FALSE, adfCMinMax );
                if (CPLGetLastErrorType() == CE_None)
                {
                  size += sprintf( buffer + size, "  Computed Min/Max=%.3f,%.3f", 
                          adfCMinMax[0], adfCMinMax[1] );
                }
            }

            size += sprintf( buffer + size, "\n" );
        }

        eErr = GDALGetRasterStatistics( hBand, bApproxStats, bStats, 
                                        &dfMin, &dfMax, &dfMean, &dfStdDev );
        if( eErr == CE_None )
        {
            size += sprintf( buffer + size, "  Minimum=%.3f, Maximum=%.3f, Mean=%.3f, StdDev=%.3f\n",
                    dfMin, dfMax, dfMean, dfStdDev );
        }

        dfNoData = GDALGetRasterNoDataValue( hBand, &bGotNodata );
        if( bGotNodata )
        {
            if (CPLIsNan(dfNoData))
                size += sprintf( buffer + size, "  NoData Value=nan\n" );
            else
                size += sprintf( buffer + size, "  NoData Value=%.18g\n", dfNoData );
        }

        if( GDALGetOverviewCount(hBand) > 0 )
        {
            int		iOverview;

            size += sprintf( buffer + size, "  Overviews: " );
            for( iOverview = 0; 
                 iOverview < GDALGetOverviewCount(hBand);
                 iOverview++ )
            {
                GDALRasterBandH	hOverview;
                const char *pszResampling = NULL;

                if( iOverview != 0 )
                    size += sprintf( buffer + size, ", " );

                hOverview = GDALGetOverview( hBand, iOverview );
                if (hOverview != NULL)
                {
                    size += sprintf( buffer + size, "%dx%d", 
                            GDALGetRasterBandXSize( hOverview ),
                            GDALGetRasterBandYSize( hOverview ) );

                    pszResampling = 
                        GDALGetMetadataItem( hOverview, "RESAMPLING", "" );

                    if( pszResampling != NULL 
                        && EQUALN(pszResampling,"AVERAGE_BIT2",12) )
                        size += sprintf( buffer + size, "*" );
                }
                else
                    size += sprintf( buffer + size, "(null)" );
            }
            size += sprintf( buffer + size, "\n" );
        }

        if( GDALHasArbitraryOverviews( hBand ) )
        {
            size += sprintf( buffer + size, "  Overviews: arbitrary\n" );
        }
        
        nMaskFlags = GDALGetMaskFlags( hBand );
        if( (nMaskFlags & (GMF_NODATA|GMF_ALL_VALID)) == 0 )
        {
            GDALRasterBandH hMaskBand = GDALGetMaskBand(hBand) ;

            size += sprintf( buffer + size, "  Mask Flags: " );
            if( nMaskFlags & GMF_PER_DATASET )
                size += sprintf( buffer + size, "PER_DATASET " );
            if( nMaskFlags & GMF_ALPHA )
                size += sprintf( buffer + size, "ALPHA " );
            if( nMaskFlags & GMF_NODATA )
                size += sprintf( buffer + size, "NODATA " );
            if( nMaskFlags & GMF_ALL_VALID )
                size += sprintf( buffer + size, "ALL_VALID " );
            size += sprintf( buffer + size, "\n" );

            if( hMaskBand != NULL &&
                GDALGetOverviewCount(hMaskBand) > 0 )
            {
                int		iOverview;

                size += sprintf( buffer + size, "  Overviews of mask band: " );
                for( iOverview = 0; 
                     iOverview < GDALGetOverviewCount(hMaskBand);
                     iOverview++ )
                {
                    GDALRasterBandH	hOverview;

                    if( iOverview != 0 )
                        size += sprintf( buffer + size, ", " );

                    hOverview = GDALGetOverview( hMaskBand, iOverview );
                    size += sprintf( buffer + size, "%dx%d", 
                            GDALGetRasterBandXSize( hOverview ),
                            GDALGetRasterBandYSize( hOverview ) );
                }
                size += sprintf( buffer + size, "\n" );
            }
        }

        if( strlen(GDALGetRasterUnitType(hBand)) > 0 )
        {
            size += sprintf( buffer + size, "  Unit Type: %s\n", GDALGetRasterUnitType(hBand) );
        }

        if( GDALGetRasterCategoryNames(hBand) != NULL )
        {
            char **papszCategories = GDALGetRasterCategoryNames(hBand);
            int i;

            size += sprintf( buffer + size, "  Categories:\n" );
            for( i = 0; papszCategories[i] != NULL; i++ )
                size += sprintf( buffer + size, "    %3d: %s\n", i, papszCategories[i] );
        }

        if( GDALGetRasterScale( hBand, &bSuccess ) != 1.0 
            || GDALGetRasterOffset( hBand, &bSuccess ) != 0.0 )
            size += sprintf( buffer + size, "  Offset: %.15g,   Scale:%.15g\n",
                    GDALGetRasterOffset( hBand, &bSuccess ),
                    GDALGetRasterScale( hBand, &bSuccess ) );

        if( bListMDD )
        {
            char** papszMDDList = GDALGetMetadataDomainList( hBand );
            char** papszIter = papszMDDList;
            if( papszMDDList != NULL )
                size += sprintf( buffer + size, "  Metadata domains:\n" );
            while( papszIter != NULL && *papszIter != NULL )
            {
                if( EQUAL(*papszIter, "") )
                    size += sprintf( buffer + size, "    (default)\n");
                else
                    size += sprintf( buffer + size, "    %s\n", *papszIter );
                papszIter ++;
            }
            CSLDestroy(papszMDDList);
        }

        papszMetadata = (bShowMetadata) ? GDALGetMetadata( hBand, NULL ) : NULL;
        if( bShowMetadata && CSLCount(papszMetadata) > 0 )
        {
            size += sprintf( buffer + size, "  Metadata:\n" );
            for( i = 0; papszMetadata[i] != NULL; i++ )
            {
                size += sprintf( buffer + size, "    %s\n", papszMetadata[i] );
            }
        }

        papszMetadata = (bShowMetadata) ? GDALGetMetadata( hBand, "IMAGE_STRUCTURE" ) : NULL;
        if( bShowMetadata && CSLCount(papszMetadata) > 0 )
        {
            size += sprintf( buffer + size, "  Image Structure Metadata:\n" );
            for( i = 0; papszMetadata[i] != NULL; i++ )
            {
                size += sprintf( buffer + size, "    %s\n", papszMetadata[i] );
            }
        }

        if( GDALGetRasterColorInterpretation(hBand) == GCI_PaletteIndex 
            && (hTable = GDALGetRasterColorTable( hBand )) != NULL )
        {
            int			i;

            size += sprintf( buffer + size, "  Color Table (%s with %d entries)\n", 
                    GDALGetPaletteInterpretationName(
                        GDALGetPaletteInterpretation( hTable )), 
                    GDALGetColorEntryCount( hTable ) );

            if (bShowColorTable)
            {
                for( i = 0; i < GDALGetColorEntryCount( hTable ); i++ )
                {
                    GDALColorEntry	sEntry;
    
                    GDALGetColorEntryAsRGB( hTable, i, &sEntry );
                    size += sprintf( buffer + size, "  %3d: %d,%d,%d,%d\n", 
                            i, 
                            sEntry.c1,
                            sEntry.c2,
                            sEntry.c3,
                            sEntry.c4 );
                }
            }
        }

        if( bShowRAT && GDALGetDefaultRAT( hBand ) != NULL )
        {
            GDALRasterAttributeTableH hRAT = GDALGetDefaultRAT( hBand );
            
            GDALRATDumpReadable( hRAT, NULL );
        }
    }

    
    CSLDestroy( papszExtraMDDomains );
    
    GDALDumpOpenDatasets( stderr );
    GDALClose(hDataset);

    GDALDestroyDriverManager();

    CPLDumpSharedList( NULL );
    CPLCleanupTLS();
    return size;
}

/************************************************************************/
/*                        GDALInfoReportCorner()                        */
/************************************************************************/

static int 
GDALInfoReportCorner( char *buffer, int size, 
		      GDALDatasetH hDataset,
                      OGRCoordinateTransformationH hTransform,
                      const char * corner_name,
                      double x, double y )

{
    double	dfGeoX, dfGeoY;
    double	adfGeoTransform[6];
        
    size += sprintf( buffer + size, "%-11s ", corner_name );
    
/* -------------------------------------------------------------------- */
/*      Transform the point into georeferenced coordinates.             */
/* -------------------------------------------------------------------- */
    if( GDALGetGeoTransform( hDataset, adfGeoTransform ) == CE_None )
    {
        dfGeoX = adfGeoTransform[0] + adfGeoTransform[1] * x
            + adfGeoTransform[2] * y;
        dfGeoY = adfGeoTransform[3] + adfGeoTransform[4] * x
            + adfGeoTransform[5] * y;
    }

    else
    {
        size += sprintf( buffer + size, "(%7.1f,%7.1f)\n", x, y );
        return -1;
    }

/* -------------------------------------------------------------------- */
/*      Report the georeferenced coordinates.                           */
/* -------------------------------------------------------------------- */
    if( ABS(dfGeoX) < 181 && ABS(dfGeoY) < 91 )
    {
        size += sprintf( buffer + size, "(%12.7f,%12.7f) ", dfGeoX, dfGeoY );
    }
    else
    {
        size += sprintf( buffer + size, "(%12.3f,%12.3f) ", dfGeoX, dfGeoY );
    }

/* -------------------------------------------------------------------- */
/*      Transform to latlong and report.                                */
/* -------------------------------------------------------------------- */
    if( hTransform != NULL 
        && OCTTransform(hTransform,1,&dfGeoX,&dfGeoY,NULL) )
    {
        
        size += sprintf( buffer + size, "(%s,", GDALDecToDMS( dfGeoX, "Long", 2 ) );
        size += sprintf( buffer + size, "%s)", GDALDecToDMS( dfGeoY, "Lat", 2 ) );
    }

    size += sprintf( buffer + size, "\n" );

    return size;
}

char *getSubDataset(GDALMajorObjectH hObject)
{
    char **papszMetadata = GDALGetMetadata( (GDALMajorObjectH)hObject, "SUBDATASETS");
    if( papszMetadata != NULL && *papszMetadata != NULL )
	for( int i = 0; papszMetadata[i] != NULL; i++ )
	    elog(DEBUG4, "%s", papszMetadata[i]);
    return papszMetadata == NULL ? NULL : papszMetadata[0];
}
