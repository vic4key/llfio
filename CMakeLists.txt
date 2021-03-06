cmake_minimum_required(VERSION 3.1 FATAL_ERROR)
include(cmake/QuickCppLibBootstrap.cmake)
include(QuickCppLibRequireOutOfSourceBuild)
include(QuickCppLibUtils)
include(QuickCppLibPolicies)

option(LLFIO_USE_EXPERIMENTAL_SG14_STATUS_CODE "Whether to use SG14 status_code for failure handling" OFF)

# Parse the version we tell cmake directly from the version header file
ParseProjectVersionFromHpp("${CMAKE_CURRENT_SOURCE_DIR}/include/llfio/version.hpp" VERSIONSTRING)
# Sets the usual PROJECT_NAME etc
project(llfio VERSION ${VERSIONSTRING} LANGUAGES C CXX)
# Also set a *cmake* namespace for this project
set(PROJECT_NAMESPACE)
# Setup this cmake environment for this project
include(QuickCppLibSetupProject)
if(NOT PROJECT_IS_DEPENDENCY)
  # This file should be updated with the last git SHA next commit
  UpdateRevisionHppFromGit("${CMAKE_CURRENT_SOURCE_DIR}/include/llfio/revision.hpp")
endif()
# Find my library dependencies
find_quickcpplib_library(quickcpplib 1.0 REQUIRED)
find_quickcpplib_library(outcome 2.0 REQUIRED)
find_quickcpplib_library(kerneltest 1.0 REQUIRED)
if(WIN32)
  add_subdirectory("include/llfio/ntkernel-error-category" EXCLUDE_FROM_ALL)
endif()

# Make the standard static and shared libraries, and if supported by this compiler, C++ modules
# for both static and shared libraries as well. For the non-C++ module variants, makes the
# interface headers into precompiled headers. Only builds all of them if this is the topmost
# CMakeLists, else built only if something upstream is dependent on one of them.
include(QuickCppLibMakeLibrary)
# Make an interface only library so dependent CMakeLists can bring in this header-only library
include(QuickCppLibMakeHeaderOnlyLibrary)

# Make preprocessed edition of this library target
if(NOT PROJECT_IS_DEPENDENCY)
  if(NOT PYTHONINTERP_FOUND)
    indented_message(WARNING "NOT rebuilding preprocessed edition of library due to python not being installed")
  elseif(FALSE)
    # See if the ply package is installed so pcpp can run
    execute_process(COMMAND python -c "import ply" RESULT_VARIABLE python_has_ply)
    if(NOT python_has_ply EQUAL 0)
      indented_message(WARNING "NOT rebuilding preprocessed edition of library due to installed python not having the ply package installed. "
        "Do 'pip install ply' to fix. NOTE that doxygen docs will NOT build without the precompiled edition.")
    else()
      function(make_single_header target name)
        add_partial_preprocess(${target}
                              "${name}"
                              ${ARGN}
                              -I ..
                              --passthru-defines --passthru-unfound-includes --passthru-unknown-exprs
                              --line-directive #--passthru-comments --debug
                              -U QUICKCPPLIB_ENABLE_VALGRIND
                              -U DOXYGEN_SHOULD_SKIP_THIS -U DOXYGEN_IS_IN_THE_HOUSE
                              -U STANDARDESE_IS_IN_THE_HOUSE
                              -U __has_include -U __has_feature -U __has_cpp_attribute
                              -U __cpp_modules
                             -U gsl_FEATURE_WITH_CONTAINER_TO_STD -U gsl_FEATURE_MAKE_SPAN_TO_STD -U gsl_FEATURE_BYTE_SPAN_TO_STD
                             -U gsl_FEATURE_HAVE_IMPLICIT_MACRO -U gsl_FEATURE_HAVE_OWNER_MACRO -U gsl_FEATURE_EXPERIMENTAL_RETURN_GUARD
                              WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                              )
        if(NOT CMAKE_VERSION VERSION_LESS 3.3)
          add_dependencies(outcome_hl ${target})
        endif()
      endfunction()
      make_single_header(llfio_hl-pp-posix
                         "${CMAKE_CURRENT_SOURCE_DIR}/single-header/llfio-posix.hpp"
                         "${CMAKE_CURRENT_SOURCE_DIR}/include/llfio/v2.0/llfio.hpp"
                         #-D QUICKCPPLIB_USE_STD_BYTE -D QUICKCPPLIB_USE_STD_OPTIONAL -D QUICKCPPLIB_USE_STD_SPAN
                         -U gsl_COMPILER_MSVC_VERSION -U gsl_HAS_CPP0X -D gsl_CPLUSPLUS=201703 -D __cplusplus=201703
                         -D LLFIO_LEAN_AND_MEAN -U _WIN32
                         -D LLFIO_EXPERIMENTAL_STATUS_CODE=1)
      make_single_header(llfio_hl-pp-win
                         "${CMAKE_CURRENT_SOURCE_DIR}/single-header/llfio-win.hpp"
                         "${CMAKE_CURRENT_SOURCE_DIR}/include/llfio/v2.0/llfio.hpp"
                         #-D QUICKCPPLIB_USE_STD_BYTE -D QUICKCPPLIB_USE_STD_OPTIONAL -D QUICKCPPLIB_USE_STD_SPAN
                         -D gsl_CPLUSPLUS=201703 -D __cplusplus=201703
                         -D LLFIO_LEAN_AND_MEAN -D _WIN32
                         -D LLFIO_EXPERIMENTAL_STATUS_CODE=1)
      make_single_header(llfio_hl-pp-abi
                         "${CMAKE_CURRENT_SOURCE_DIR}/single-header/abi.hpp"
                         "${CMAKE_CURRENT_SOURCE_DIR}/include/llfio/v2.0/llfio.hpp"
                         -D LLFIO_EXPERIMENTAL_STATUS_CODE=1
                         -D LLFIO_DISABLE_ABI_PERMUTATION=1
                         -D OUTCOME_DISABLE_ABI_PERMUTATION=1
                         -D QUICKCPPLIB_DISABLE_ABI_PERMUTATION=1
                         -U LLFIO_UNSTABLE_VERSION
                         -U OUTCOME_UNSTABLE_VERSION)
    endif()
  endif()
endif()

# Create a custom doxygen generation target
include(QuickCppLibMakeDoxygen)
# Set the standard definitions for these libraries and bring in the all_* helper functions
include(QuickCppLibApplyDefaultDefinitions)
# Do we have the Coroutines TS?
function(CheckCXXHasCoroutines iter flags)
  set(CMAKE_REQUIRED_FLAGS "${flags}")
  check_cxx_source_compiles("
#include <future>
std::future<int> g() { co_return 0; }
int main() { return g().get(); }
" CXX_HAS_COROUTINES${iter})
  set(CXX_HAS_COROUTINES${iter} ${CXX_HAS_COROUTINES${iter}} PARENT_SCOPE)
endfunction()
include(CheckCXXSourceCompiles)
if(MSVC)
  CheckCXXHasCoroutines(_MSVC "/await")
  if(CXX_HAS_COROUTINES_MSVC)
    all_compile_options(PUBLIC "/await")
    all_compile_definitions(PUBLIC "__cpp_coroutines")
  endif()
endif()
if(CLANG OR GCC)
  CheckCXXHasCoroutines(_CLANG_GCC "-std=c++14 -fcoroutines-ts")
  if(CXX_HAS_COROUTINES_CLANG_GCC)
    all_compile_options(PUBLIC "-fcoroutines-ts")
    all_compile_definitions(PUBLIC "__cpp_coroutines")
  endif()
endif()

# Set the C++ features this library requires
all_compile_features(PUBLIC
  # cxx_exceptions                        ## Annoyingly not supported by cmake 3.6
  cxx_alias_templates
  cxx_variadic_templates
  cxx_noexcept
  cxx_constexpr
  cxx_lambda_init_captures
  cxx_attributes
  cxx_generic_lambdas
)
if(NOT MSVC OR CMAKE_VERSION VERSION_GREATER 3.59)
  all_compile_features(PUBLIC
    cxx_variable_templates
  )
endif()
# Set the library dependencies this library has
all_link_libraries(PUBLIC quickcpplib::hl outcome::hl Threads::Threads)
# Set the system dependencies this library has
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
  find_library(libstdcxx_stdcxxfs stdc++fs)
  if(libstdcxx_stdcxxfs MATCHES "NOTFOUND")
    set(libstdcxx_stdcxxfs -lstdc++fs)
  endif()
  all_link_libraries(PUBLIC ${libstdcxx_stdcxxfs} rt)
endif()
if(CMAKE_SYSTEM_NAME MATCHES "FreeBSD" OR APPLE)
  find_library(libcxx_cxxexperimental c++experimental)
  all_link_libraries(PUBLIC ${libcxx_cxxexperimental})
endif()
# Set any macros this library requires
if(LLFIO_USE_EXPERIMENTAL_SG14_STATUS_CODE)
  all_compile_definitions(PUBLIC LLFIO_EXPERIMENTAL_STATUS_CODE=1)
endif()
if(WIN32)
  all_compile_definitions(PRIVATE _WIN32_WINNT=0x600)  ## Target WinVista
  if(NOT LLFIO_USE_EXPERIMENTAL_SG14_STATUS_CODE)
    target_link_libraries(llfio_dl PUBLIC ntkernel-error-category::dl)
    target_link_libraries(llfio_sl PUBLIC ntkernel-error-category::sl)
  endif()
endif()
# Anyone using the static or dynamic libraries is not using the header only variant
foreach(lib llfio_sl llfio_dl)
  target_compile_definitions(${lib} INTERFACE LLFIO_HEADERS_ONLY=0) 
  target_compile_definitions(${lib} PRIVATE LLFIO_SOURCE=1) 
endforeach()
if(TARGET llfio-example_single-header)
  set(compiler_has_cxx_17 0)
  foreach(feature ${CMAKE_CXX_COMPILE_FEATURES})
    if(feature STREQUAL "cxx_std_17")
      set(compiler_has_cxx_17 1)
    endif()
  endforeach()
  # The single header test requires C++ 17
  if(compiler_has_cxx_17)
    target_compile_features(llfio-example_single-header PRIVATE cxx_std_17)
  else()
    set_target_properties(llfio-example_single-header PROPERTIES EXCLUDE_FROM_ALL ON EXCLUDE_FROM_DEFAULT_BUILD ON)
  endif()
endif()

# For all possible configurations of this library, add each test
include(QuickCppLibMakeStandardTests)
# For each test target, link to kerneltest
foreach(test_target ${llfio_TEST_TARGETS})
  target_link_libraries(${test_target} PRIVATE kerneltest::hl)
endforeach()
if(MSVC)
  foreach(test_target ${llfio_TEST_TARGETS})
    target_compile_options(${test_target} PRIVATE /wd4503)       ## decorated name length exceeded
    #target_compile_options(${test_target} PRIVATE /permissive-)  ## future parsing
  endforeach()
endif()

# Cache this library's auto scanned sources for later reuse
include(QuickCppLibCacheLibrarySources)

# Make available this library for install and export
include(QuickCppLibMakeInstall)
include(QuickCppLibMakeExport)
