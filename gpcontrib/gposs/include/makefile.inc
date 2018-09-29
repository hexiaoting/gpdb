COMMON_OBJS =  ossGisExt.o ossExtWrapper.o ossGisReader.o ossCommon.o ossDownloader.o ossGisRaster.o ossGisMeta.o ossShapeReader.o 

COMMON_LINK_OPTIONS = -lstdc++ -lxml2 -lpthread -lcrypto -lcurl -lz -loss -lgdal -lrtpostgis -L/home/gpadmin/repo/gdal/gdal/build/lib

COMMON_CPP_FLAGS = -fPIC -I/usr/include/libxml2 -I/usr/local/opt/openssl/include

TEST_OBJS = $(patsubst %.o,%_test.o,$(COMMON_OBJS))
