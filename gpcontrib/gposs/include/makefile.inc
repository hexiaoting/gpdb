COMMON_OBJS =  ossGisExt.o ossExtWrapper.o ossGisReader.o ossCommon.o ossDownloader.o ossGisRaster.o ossGisMeta.o ossShapeReader.o 

GDAL_LIB = $(shell gdal-config --libs)

PG_LIB = $(shell pg_config --pkglibdir)

GPDB_LIB = $(shell pg_config --libdir)

COMMON_LINK_OPTIONS = -lstdc++ -lxml2 -lpthread -lcrypto -lcurl -lz -loss -L$(GPDB_LIB) -lrtpostgis -L$(PG_LIB) $(GDAL_LIB) 

COMMON_CPP_FLAGS = -fPIC -I/usr/include/libxml2 -I/usr/local/opt/openssl/include

TEST_OBJS = $(patsubst %.o,%_test.o,$(COMMON_OBJS))
