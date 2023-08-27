FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.cpp)

set(components arduino-esp32 app_update)

idf_component_register(SRCS ${app_sources}
                    INCLUDE_DIRS "."
                    REQUIRES ${components}
)
