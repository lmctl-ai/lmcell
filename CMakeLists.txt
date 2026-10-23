cmake_minimum_required(VERSION 3.24)
project(lmcell VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
option(LMCELL_FULL_STATIC "Link the complete executable statically" ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/physicell")
  message(FATAL_ERROR "third_party/physicell missing: run scripts/sync-deps.sh")
endif()

function(embed_file var file name)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
  file(READ "${file}" hex HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
  set(${var} "inline constexpr unsigned char ${name}[] = {${hex}0}; inline constexpr std::size_t ${name}_size = sizeof(${name}) - 1;\n" PARENT_SCOPE)
endfunction()

set(GEN "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GEN}")
embed_file(SCHEMA_SRC "${CMAKE_CURRENT_SOURCE_DIR}/schema/lmcell.dsl.json" embedded_schema)
file(WRITE "${GEN}/schema_embedded.hpp" "#pragma once\n#include <cstddef>\nnamespace lmcell { ${SCHEMA_SRC} }\n")
embed_file(NOTICES_SRC "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES" embedded_notices)
file(WRITE "${GEN}/notices_embedded.hpp" "#pragma once\n#include <cstddef>\nnamespace lmcell { ${NOTICES_SRC} }\n")

set(PHYS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/physicell")
# BioFVM's global Microenvironment constructor writes to 13 std::vectors in
# default_microenvironment_options before that object is constructed. Those
# writes leak and use objects before lifetime begins. Its own later constructor
# initializes the same options. Omit only those premature writes in a build copy;
# the pinned checkout remains clean and all other source is verbatim.
set(BIOFVM_ORIGINAL "${PHYS}/BioFVM/BioFVM_microenvironment.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BIOFVM_ORIGINAL}")
file(READ "${BIOFVM_ORIGINAL}" BIOFVM_SOURCE)
set(EARLY_OPTIONS "[\t ]*default_microenvironment_options\\.Dirichlet_[a-z_]+\\.assign\\( 1 ,[^\n]*\n")
string(REGEX MATCHALL "default_microenvironment_options\\.Dirichlet_[a-z_]+\\.assign\\( 1 ," EARLY_MATCHES "${BIOFVM_SOURCE}")
list(LENGTH EARLY_MATCHES EARLY_COUNT)
if(NOT EARLY_COUNT EQUAL 13)
  message(FATAL_ERROR "Pinned BioFVM constructor changed (${EARLY_COUNT}): review the initialization repair")
endif()
string(REGEX REPLACE "${EARLY_OPTIONS}" "" BIOFVM_SOURCE "${BIOFVM_SOURCE}")
file(WRITE "${GEN}/BioFVM_microenvironment.cpp" "${BIOFVM_SOURCE}")
set(PHYS_SOURCES
  ${PHYS}/BioFVM/BioFVM_MultiCellDS.cpp
  ${PHYS}/BioFVM/BioFVM_agent_container.cpp
  ${PHYS}/BioFVM/BioFVM_basic_agent.cpp
  ${PHYS}/BioFVM/BioFVM_matlab.cpp
  ${PHYS}/BioFVM/BioFVM_mesh.cpp
  ${GEN}/BioFVM_microenvironment.cpp
  ${PHYS}/BioFVM/BioFVM_solvers.cpp
  ${PHYS}/BioFVM/BioFVM_utilities.cpp
  ${PHYS}/BioFVM/BioFVM_vector.cpp
  ${PHYS}/BioFVM/pugixml.cpp
  ${PHYS}/core/PhysiCell_basic_signaling.cpp
  ${PHYS}/core/PhysiCell_cell.cpp
  ${PHYS}/core/PhysiCell_cell_container.cpp
  ${PHYS}/core/PhysiCell_constants.cpp
  ${PHYS}/core/PhysiCell_custom.cpp
  ${PHYS}/core/PhysiCell_phenotype.cpp
  ${PHYS}/core/PhysiCell_rules.cpp
  ${PHYS}/core/PhysiCell_signal_behavior.cpp
  ${PHYS}/core/PhysiCell_standard_models.cpp
  ${PHYS}/core/PhysiCell_utilities.cpp
  ${PHYS}/modules/PhysiCell_MultiCellDS.cpp
  ${PHYS}/modules/PhysiCell_POV.cpp
  ${PHYS}/modules/PhysiCell_SVG.cpp
  ${PHYS}/modules/PhysiCell_geometry.cpp
  ${PHYS}/modules/PhysiCell_pathology.cpp
  ${PHYS}/modules/PhysiCell_pugixml.cpp
  ${PHYS}/modules/PhysiCell_settings.cpp
  ${PHYS}/modules/PhysiCell_various_outputs.cpp)

add_library(physicell_core STATIC ${PHYS_SOURCES})
target_include_directories(physicell_core PUBLIC ${PHYS}/core ${PHYS}/modules ${PHYS}/BioFVM)
target_include_directories(physicell_core SYSTEM PUBLIC cpp/serial)
# Upstream warnings are noisy; sanitizer flags still instrument this target.
target_compile_options(physicell_core PRIVATE -w)

add_library(cell_core STATIC cpp/core.cpp)
target_include_directories(cell_core PUBLIC cpp ${PHYS}/core ${PHYS}/modules ${PHYS}/BioFVM)
target_include_directories(cell_core SYSTEM PUBLIC cpp/serial)
target_link_libraries(cell_core PUBLIC physicell_core)
target_compile_options(cell_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(lmcell cpp/main.cpp)
target_include_directories(lmcell PRIVATE cpp "${GEN}" ${PHYS}/core ${PHYS}/modules ${PHYS}/BioFVM)
target_compile_definitions(lmcell PRIVATE LMCELL_VERSION="${PROJECT_VERSION}")
target_link_libraries(lmcell PRIVATE cell_core)
target_compile_options(lmcell PRIVATE -Wall -Wextra -Wpedantic)

function(static_executable target)
  if((CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang") AND NOT APPLE)
    target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc)
    if(LMCELL_FULL_STATIC)
      target_link_options(${target} PRIVATE -static)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
      target_link_options(${target} PRIVATE -s)
    endif()
  endif()
endfunction()
static_executable(lmcell)

include(CTest)
if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/scientific.cpp")
  add_executable(scientific_tests tests/scientific.cpp)
  target_link_libraries(scientific_tests PRIVATE cell_core)
  target_compile_options(scientific_tests PRIVATE -Wall -Wextra -Wpedantic)
  static_executable(scientific_tests)
  foreach(mode baseline growth death rules walk diffusion exchange validation partial)
    add_test(NAME ${mode} COMMAND scientific_tests ${mode})
    set_tests_properties(${mode} PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  endforeach()
endif()
