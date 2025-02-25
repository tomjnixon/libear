cmake_minimum_required(VERSION 3.5)

############################################################
# set global paths
############################################################
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include_directories(include)

############################################################
# init project
############################################################
project(libear VERSION 0.9.0 LANGUAGES CXX)

if(${PROJECT_SOURCE_DIR} STREQUAL ${CMAKE_SOURCE_DIR})
    set(IS_ROOT_PROJECT TRUE)
else()
    set(IS_ROOT_PROJECT FALSE)
endif()

string(TIMESTAMP PROJECT_BUILD_DATE)
set(PROJECT_DESCRIPTION "EBU Tech 3388 core library")

############################################################
# add new CMAKE_BUILD_TYPE coverage
############################################################
SET(CMAKE_CXX_FLAGS_COVERAGE "${CMAKE_CXX_FLAGS_DEBUG} --coverage")
SET(CMAKE_EXE_LINKER_FLAGS_COVERAGE "${CMAKE_EXE_LINKER_FLAGS_DEBUG} --coverage")
SET(CMAKE_SHARED_LINKER_FLAGS_COVERAGE "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} --coverage")

############################################################
# user config options
############################################################
include(FeatureSummary)
option(EAR_HIDE_INTERNAL_SYMBOLS "Hide symbols by default" TRUE)
option(EAR_UNIT_TESTS "Build units tests" $<AND:${IS_ROOT_PROJECT},$<NOT:${BUILD_SHARED_LIBS}>>)
option(EAR_EXAMPLES "Build examples" ${IS_ROOT_PROJECT})
option(EAR_PACKAGE_AND_INSTALL "Package and install libear" ${IS_ROOT_PROJECT})
set(INSTALL_LIB_DIR lib CACHE PATH "Installation directory for libraries")
set(INSTALL_BIN_DIR bin CACHE PATH "Installation directory for executables")
set(INSTALL_INCLUDE_DIR include CACHE PATH "Installation directory for header files")
if(WIN32 AND NOT CYGWIN)
  set(INSTALL_CMAKE_DIR CMake CACHE PATH "Installation directory for CMake files")
else()
  set(INSTALL_CMAKE_DIR share/cmake/ear CACHE PATH "Installation directory for CMake files")
endif()

if(${BUILD_SHARED_LIBS} AND ${EAR_UNIT_TESTS} AND ${EAR_HIDE_INTERNAL_SYMBOLS})
  message(FATAL_ERROR "\
    Unit tests need access to internal functions which are not exported by \
    default for a shared library. Consider disabling EAR_HIDE_INTERNAL_SYMBOLS \
    to run the unit tests with the shared library of libear.")
endif()

############################################################
# find libraries
############################################################
find_package(Boost 1.57 REQUIRED)
find_package(yaml-cpp 0.5.2 CONFIG REQUIRED)
include(${PROJECT_SOURCE_DIR}/submodules/eigen3.cmake)

############################################################
# configure files
############################################################
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/config/version.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/ear/version.hpp")

############################################################
# add targets
############################################################
add_subdirectory(submodules)
add_subdirectory(src)
add_subdirectory(tools)
if(EAR_EXAMPLES)
  add_subdirectory(examples)
endif()

if(EAR_UNIT_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()

############################################################
# FeatureSummary
############################################################
add_feature_info(EAR_HIDE_INTERNAL_SYMBOLS ${EAR_HIDE_INTERNAL_SYMBOLS} "Hide symbols by default")
add_feature_info(EAR_UNIT_TESTS ${EAR_UNIT_TESTS} "Build units tests")
add_feature_info(EAR_EXAMPLES ${EAR_EXAMPLES} "Build examples")
add_feature_info(EAR_PACKAGE_AND_INSTALL ${EAR_PACKAGE_AND_INSTALL} "Package and install libear")
feature_summary(WHAT ALL)

if(EAR_PACKAGE_AND_INSTALL)
  #########################################################
  # very minimal cpack configuration
  #########################################################
  include(InstallRequiredSystemLibraries)
  set (CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
  set (CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
  set (CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
  set (CPACK_GENERATOR "TGZ;ZIP")
  include (CPack)

  #########################################################
  # custom uninstall target
  #########################################################
  configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/uninstall.cmake.in"
      "${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake"
      IMMEDIATE @ONLY
  )
  add_custom_target(uninstall COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake)
endif()
