cmake_minimum_required(VERSION 3.14)

set(PROJECT_NAME divinity_fling_visualizer)

project(${PROJECT_NAME})

set(CMAKE_C_STANDARD 23)

# Make sure all binaries are placed into the same build folder.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Add modules directory
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/modules")

# Optional: Enable Python support
option(ENABLE_PYTHON "Enable Python support" OFF)
if(ENABLE_PYTHON)
    include(FetchPython)
endif()

# Optional: Enable Vulkan support
option(ENABLE_VULKAN "Enable Vulkan rendering" OFF)
if(ENABLE_VULKAN)
    include(FetchVulkan)
    if(VULKAN_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan)
    endif()
endif()

# Link your project against CF statically, rather than as a shared library.
set(CUTE_FRAMEWORK_STATIC ON)
set(CF_FRAMEWORK_BUILD_SAMPLES OFF)

# This will download and build Cute Framework just once the first time you build your game.
include(FetchContent)
FetchContent_Declare(
	cute
	GIT_REPOSITORY https://github.com/RandyGaul/cute_framework
	GIT_TAG a13f4661f5ba27e8d45027b52f121c1704786425
)
FetchContent_MakeAvailable(cute)

# --- ZSTD Integration ---
# Disable programs, tests, and shared libraries to speed up the static build
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.6
)
FetchContent_GetProperties(zstd)
if(NOT zstd_POPULATED)
    FetchContent_Populate(zstd)
    # Zstd's CMakeLists.txt is not in the root directory, so we point directly to it
    add_subdirectory(${zstd_SOURCE_DIR}/build/cmake ${zstd_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
# ------------------------

# Create symbolic link to data directory for non-Windows platforms
if(NOT WIN32)
	set(DATA_LINK_PATH ${CMAKE_BINARY_DIR}/data)
	set(DATA_SOURCE_PATH ${CMAKE_SOURCE_DIR}/data)
	
	# Create symbolic link if it doesn't exist
	if(NOT EXISTS ${DATA_LINK_PATH})
		execute_process(
			COMMAND ${CMAKE_COMMAND} -E create_symlink ${DATA_SOURCE_PATH} ${DATA_LINK_PATH}
			RESULT_VARIABLE SYMLINK_RESULT
		)
		if(NOT SYMLINK_RESULT EQUAL 0)
			message(WARNING "Failed to create symbolic link from ${DATA_LINK_PATH} to ${DATA_SOURCE_PATH}")
		else()
			message(STATUS "Created symbolic link: ${DATA_LINK_PATH} -> ${DATA_SOURCE_PATH}")
		endif()
	endif()
endif()

# Any additional paths needed should either be under /src or add a new one for any additional paths that's not under /src
file(GLOB_RECURSE PROJECT_SOURCE_C
     "${CMAKE_CURRENT_SOURCE_DIR}/src/main.c"
)

# Source code for your game.
add_executable(
	${PROJECT_NAME}
	${PROJECT_SOURCE_C}
)

# Our source code will be in the `src` folder.
target_include_directories(${PROJECT_NAME} PUBLIC 
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libs/ogam>
)

# Some basic information needed for CMake to generate your Info.plist file.
# This is necessary for e.g. iOS builds.
if(APPLE)
	set_target_properties(${PROJECT_NAME} PROPERTIES
		MACOSX_BUNDLE_GUI_IDENTIFIER "com.${PROJECT_NAME}.${PROJECT_NAME}"
		MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
		MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
	)
endif()

# Make the game link against Cute Framework.
target_link_libraries(${PROJECT_NAME} PRIVATE cute)

# For convenience on Windows, set MSVC debugger's working directory in the build folder.
# Also ask MSVC to make the game the startup project.
if (MSVC)
	set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:${PROJECT_NAME}>)
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
endif()

add_link_options(
    /LARGEADDRESSAWARE
    /DEBUG
    $<$<NOT:$<CONFIG:DEBUG>>:/INCREMENTAL:NO> # Disable incremental linking.
)