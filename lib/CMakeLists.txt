aux_source_directory(./ DIR_LIB_SRCS)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/include)

#SET(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

add_library (http_conn STATIC ${DIR_LIB_SRCS})