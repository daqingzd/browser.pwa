cmake_minimum_required(VERSION 3.5...3.28)
project(browser_c C)

set(PWAOS_SDK_DIR "$ENV{PWAOS_SDK_DIR}" CACHE STRING "Path to pwaos_sdk runtime directory")
if(NOT PWAOS_SDK_DIR)
    message(FATAL_ERROR "PWAOS_SDK_DIR environment variable is not set")
endif()

# 关键：把 Windows 反斜杠统一转换成正斜杠
file(TO_CMAKE_PATH "${PWAOS_SDK_DIR}" PWAOS_SDK_DIR)
add_definitions(-DPWAOS_SDK_DIR="${PWAOS_SDK_DIR}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
include_directories(${PWAOS_SDK_DIR}/include)

if(WIN32)
    set(PWAOS_LIB_DIR "${PWAOS_SDK_DIR}/lib/win")
    set(PWAOS_PLATFORM_LIBS "")
    file(GLOB PWAOS_RUNTIME_FILES "${PWAOS_LIB_DIR}/*.dll")

    # 诊断：如果没找到 DLL，配置阶段直接报错，而不是静默跳过
    if(NOT PWAOS_RUNTIME_FILES)
        message(FATAL_ERROR "在 ${PWAOS_LIB_DIR} 下没有找到任何 DLL，请检查路径和文件是否存在")
    endif()
    message(STATUS "将复制以下 DLL 到输出目录: ${PWAOS_RUNTIME_FILES}")
else()
    set(PWAOS_LIB_DIR "${PWAOS_SDK_DIR}/lib/linux")
    set(PWAOS_PLATFORM_LIBS pthread dl)
    set(PWAOS_RUNTIME_FILES "${PWAOS_LIB_DIR}/libpwaos_runtime_api.so")
endif()

link_directories(${PWAOS_LIB_DIR})

add_executable(browser browser_app.c)
target_link_libraries(browser PRIVATE pwaos_runtime_api ${PWAOS_PLATFORM_LIBS})

foreach(_f ${PWAOS_RUNTIME_FILES})
    add_custom_command(TARGET browser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_f}"
            "$<TARGET_FILE_DIR:browser>"
        COMMENT "Copy ${_f} to output directory"
    )
endforeach()

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.6)
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT browser)
endif()
