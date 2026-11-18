#ifndef CMAKE_CONFIG_H
#define CMAKE_CONFIG_H

#define PROJECT_NAME "minetest"
#define INSTALL_PREFIX "/usr/local"
#define VERSION_STRING "0.3.0"
#define USE_GETTEXT 0
#ifdef NDEBUG
	#define BUILD_TYPE "Release"
#else
	#define BUILD_TYPE "Debug"
#endif
#define BUILD_INFO "VER=" VERSION_STRING " RUN_IN_PLACE=1 USE_GETTEXT=0 INSTALL_PREFIX=" INSTALL_PREFIX " BUILD_TYPE=" BUILD_TYPE

#endif
