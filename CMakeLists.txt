cmake_minimum_required(VERSION 3.8)
project(nina_ui)

##################################
#  Perform Cross Compile setup   #
##################################

set(XENOMAI_BASE_DIR "/usr/xenomai" CACHE STRING "xenomai base dir path")

# Check if a cross-compile build
message("CMAKE_SYSROOT is " ${CMAKE_SYSROOT})
if(NOT "${CMAKE_SYSROOT}" STREQUAL "")
    set(CMAKE_SYSROOT "${CMAKE_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(XENOMAI_BASE_DIR "${CMAKE_SYSROOT}/usr/xenomai")
endif()
message("XENOMAI_BASE_DIR is " ${XENOMAI_BASE_DIR})

option(WITH_XENOMAI "Enable Xenomai support" ON)

function(add_xenomai_to_target target)
    # from `xeno-config --skin=posix --cflags`
    set(XENOMAI_C_FLAGS "-D_REENTRANT -D__COBALT__ -D__COBALT_WRAP__")

    set(XENOMAI_INCLUDE_DIRS
        ${XENOMAI_BASE_DIR}/include
        ${XENOMAI_BASE_DIR}/include/cobalt
    )

    find_library(COBALT_LIB cobalt HINTS ${XENOMAI_BASE_DIR}/lib)
    target_compile_options(${target} PRIVATE ${XENOMAI_C_FLAGS})
    target_include_directories(${target} PRIVATE ${XENOMAI_INCLUDE_DIRS})
    target_link_libraries(${target} PRIVATE ${COBALT_LIB} rt m)
endfunction()

##################################
#  Generate build information    #
##################################

# Get the latest commit hash of the working branch
execute_process(
    COMMAND git log -1 --format=%H
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M")

configure_file(
    ${CMAKE_SOURCE_DIR}/include/version.h.in
    ${CMAKE_BINARY_DIR}/version.h
)

####################
#  Main Target     #
####################

set(COMPILATION_UNITS src/main.cpp
                      src/logger.cpp
                      src/engine/managers/base_manager.cpp
                      src/engine/managers/daw_manager.cpp
                      src/engine/managers/midi_device_manager.cpp
                      src/engine/managers/sequencer_manager.cpp
                      src/engine/managers/arpeggiator_manager.cpp
                      src/engine/managers/osc_manager.cpp
                      src/engine/managers/surface_control_manager.cpp
                      src/engine/managers/analog_input_control_manager.cpp
                      src/engine/managers/file_manager.cpp
                      src/engine/managers/sw_manager.cpp
                      src/engine/managers/keyboard_manager.cpp
                      src/engine/managers/gui/gui_manager.cpp
                      src/engine/event_router.cpp
                      src/engine/event.cpp
                      src/engine/layer_info.cpp
                      src/engine/param.cpp
                      src/engine/timer.cpp
                      src/engine/system_config.cpp
                      src/engine/system_func.cpp
                      src/engine/tempo.cpp
                      src/engine/utils.cpp
                      src/drivers/surface_control.cpp
                      src/drivers/analog_input_control.cpp)

# Enumerate all the headers separately so that CLion can index them
set(EXTRA_CLION_SOURCES )

set(SOURCE_FILES "${COMPILATION_UNITS}" "${EXTRA_CLION_SOURCES}")

add_executable(nina_ui "${COMPILATION_UNITS}")
if (${WITH_XENOMAI})
    message("Building with Xenomai support")
    add_xenomai_to_target(nina_ui)
else()
    message("Building WITHOUT Xenomai support")
    target_compile_definitions(nina_ui PRIVATE NO_XENOMAI)
endif()
target_link_libraries(nina_ui PRIVATE pthread)

#########################
#  Include Directories  #
#########################

set(INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/src"
                 "${PROJECT_SOURCE_DIR}/src/engine"
                 "${PROJECT_SOURCE_DIR}/src/engine/managers"
                 "${PROJECT_SOURCE_DIR}/src/engine/managers/gui"
                 "${PROJECT_SOURCE_DIR}/src/drivers"
                 "${PROJECT_SOURCE_DIR}/include"
                 "${PROJECT_SOURCE_DIR}/thirdparty/rapidjson/include"
                 "${PROJECT_SOURCE_DIR}/thirdparty/spdlog/include"
                 "${PROJECT_SOURCE_DIR}/thirdparty/rpi-rtdm-audio-driver"
                 "${CMAKE_BINARY_DIR}")

# /usr/local doesn't get added by default in Mac OSX
if (APPLE)
    set(INCLUDE_DIRS "${INCLUDE_DIRS}" /usr/local/include)
endif()

################################
#  Subdirectory configuration  #
################################

option(RAPIDJSON_BUILD_EXAMPLES OFF)
add_subdirectory(thirdparty)

#################################
#  Linked libraries             #
#################################

# Common libraries between all builds, and safe to be included in test targets
set(COMMON_LIBRARIES pthread)

set(EXTRA_BUILD_LIBRARIES ${EXTRA_BUILD_LIBRARIES} elkcpp protobuf grpc++ grpc gpr asound lo systemd blkid)
target_include_directories(nina_ui PRIVATE ${INCLUDE_DIRS})
target_link_libraries(nina_ui PRIVATE ${EXTRA_BUILD_LIBRARIES} ${COMMON_LIBRARIES})

####################################
#  Compiler Flags and definitions  #
####################################

target_compile_features(nina_ui PRIVATE cxx_std_17)
target_compile_options(nina_ui PRIVATE -Wall -Wextra -Wno-psabi -fno-rtti -ffast-math -Wno-type-limits)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "7.0"))
        target_compile_options(nina_ui PRIVATE -faligned-new)
    endif()
endif()

######################
#  Tests subproject  #
######################

option(WITH_TESTS "Enable testing" OFF)

if (${WITH_TESTS})
    enable_testing()
    add_subdirectory(test)
endif()


####################
#  Install         #
####################

install(TARGETS nina_ui DESTINATION bin)
