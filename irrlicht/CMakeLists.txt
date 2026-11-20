cmake_minimum_required(VERSION 3.10)
project(Irrlicht C CXX)

set(CMAKE_CXX_STANDARD 11)

set(SDL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib/SDL-1.2.15")

include_directories(
	include
	source/Irrlicht
	source/Irrlicht/zlib
	source/Irrlicht/jpeglib
	source/Irrlicht/libpng
	source/Irrlicht/bzip2
	source/Irrlicht/lzma
	source/Irrlicht/aesGladman
	"${SDL_DIR}/include"
)

add_definitions(-DIRRLICHT_EXPORTS=1 -D_CRT_SECURE_NO_WARNINGS -DWINDOWS_IGNORE_PACKING_MISMATCH)

file(GLOB IRRLICHT_CORE_SOURCES
	"source/Irrlicht/*.cpp"
	"source/Irrlicht/*.c"
)

set(ZLIB_SOURCES
	source/Irrlicht/zlib/adler32.c
	source/Irrlicht/zlib/compress.c
	source/Irrlicht/zlib/crc32.c
	source/Irrlicht/zlib/deflate.c
	source/Irrlicht/zlib/inffast.c
	source/Irrlicht/zlib/inflate.c
	source/Irrlicht/zlib/inftrees.c
	source/Irrlicht/zlib/trees.c
	source/Irrlicht/zlib/uncompr.c
	source/Irrlicht/zlib/zutil.c
)

set(JPEGLIB_SOURCES
	source/Irrlicht/jpeglib/jaricom.c
	source/Irrlicht/jpeglib/jcapimin.c
	source/Irrlicht/jpeglib/jcapistd.c
	source/Irrlicht/jpeglib/jcarith.c
	source/Irrlicht/jpeglib/jccoefct.c
	source/Irrlicht/jpeglib/jccolor.c
	source/Irrlicht/jpeglib/jcdctmgr.c
	source/Irrlicht/jpeglib/jchuff.c
	source/Irrlicht/jpeglib/jcinit.c
	source/Irrlicht/jpeglib/jcmainct.c
	source/Irrlicht/jpeglib/jcmarker.c
	source/Irrlicht/jpeglib/jcmaster.c
	source/Irrlicht/jpeglib/jcomapi.c
	source/Irrlicht/jpeglib/jcparam.c
	source/Irrlicht/jpeglib/jcprepct.c
	source/Irrlicht/jpeglib/jcsample.c
	source/Irrlicht/jpeglib/jctrans.c
	source/Irrlicht/jpeglib/jdapimin.c
	source/Irrlicht/jpeglib/jdapistd.c
	source/Irrlicht/jpeglib/jdarith.c
	source/Irrlicht/jpeglib/jdatadst.c
	source/Irrlicht/jpeglib/jdatasrc.c
	source/Irrlicht/jpeglib/jdcoefct.c
	source/Irrlicht/jpeglib/jdcolor.c
	source/Irrlicht/jpeglib/jddctmgr.c
	source/Irrlicht/jpeglib/jdhuff.c
	source/Irrlicht/jpeglib/jdinput.c
	source/Irrlicht/jpeglib/jdmainct.c
	source/Irrlicht/jpeglib/jdmarker.c
	source/Irrlicht/jpeglib/jdmaster.c
	source/Irrlicht/jpeglib/jdmerge.c
	source/Irrlicht/jpeglib/jdpostct.c
	source/Irrlicht/jpeglib/jdsample.c
	source/Irrlicht/jpeglib/jdtrans.c
	source/Irrlicht/jpeglib/jerror.c
	source/Irrlicht/jpeglib/jfdctflt.c
	source/Irrlicht/jpeglib/jfdctfst.c
	source/Irrlicht/jpeglib/jfdctint.c
	source/Irrlicht/jpeglib/jidctflt.c
	source/Irrlicht/jpeglib/jidctfst.c
	source/Irrlicht/jpeglib/jidctint.c
	source/Irrlicht/jpeglib/jmemmgr.c
	source/Irrlicht/jpeglib/jmemnobs.c
	source/Irrlicht/jpeglib/jquant1.c
	source/Irrlicht/jpeglib/jquant2.c
	source/Irrlicht/jpeglib/jutils.c
)

set(LIBPNG_SOURCES
	source/Irrlicht/libpng/png.c
	source/Irrlicht/libpng/pngerror.c
	source/Irrlicht/libpng/pngget.c
	source/Irrlicht/libpng/pngmem.c
	source/Irrlicht/libpng/pngpread.c
	source/Irrlicht/libpng/pngread.c
	source/Irrlicht/libpng/pngrio.c
	source/Irrlicht/libpng/pngrtran.c
	source/Irrlicht/libpng/pngrutil.c
	source/Irrlicht/libpng/pngset.c
	source/Irrlicht/libpng/pngtrans.c
	source/Irrlicht/libpng/pngwio.c
	source/Irrlicht/libpng/pngwrite.c
	source/Irrlicht/libpng/pngwtran.c
	source/Irrlicht/libpng/pngwutil.c
)

set(BZIP2_SOURCES
	source/Irrlicht/bzip2/blocksort.c
	source/Irrlicht/bzip2/bzcompress.c
	source/Irrlicht/bzip2/bzlib.c
	source/Irrlicht/bzip2/crctable.c
	source/Irrlicht/bzip2/decompress.c
	source/Irrlicht/bzip2/huffman.c
	source/Irrlicht/bzip2/randtable.c
)

set(LZMA_SOURCES
	source/Irrlicht/lzma/LzmaDec.c
)

set(AES_SOURCES
	source/Irrlicht/aesGladman/aescrypt.cpp
	source/Irrlicht/aesGladman/aeskey.cpp
	source/Irrlicht/aesGladman/aestab.cpp
	source/Irrlicht/aesGladman/fileenc.cpp
	source/Irrlicht/aesGladman/hmac.cpp
	source/Irrlicht/aesGladman/prng.cpp
	source/Irrlicht/aesGladman/pwd2key.cpp
	source/Irrlicht/aesGladman/sha1.cpp
	source/Irrlicht/aesGladman/sha2.cpp
)

set(ALL_SOURCES
	${IRRLICHT_CORE_SOURCES}
	${ZLIB_SOURCES}
	${JPEGLIB_SOURCES}
	${LIBPNG_SOURCES}
	${BZIP2_SOURCES}
	${LZMA_SOURCES}
	${AES_SOURCES}
)

add_library(Irrlicht SHARED ${ALL_SOURCES})

if(WIN32)
	target_link_libraries(Irrlicht
		opengl32 glu32 user32 gdi32 advapi32 shell32
		"${SDL_DIR}/lib/x86/SDL.lib"
	)
endif()

# Output targets
set_target_properties(Irrlicht PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/Win32-VisualStudio"
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib/Win32-visualstudio"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/Win32-VisualStudio"
	OUTPUT_NAME "Irrlicht"
)

add_custom_command(TARGET Irrlicht POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
	"${SDL_DIR}/lib/x86/SDL.dll"
	"${CMAKE_CURRENT_SOURCE_DIR}/bin/Win32-VisualStudio/SDL.dll"
)
