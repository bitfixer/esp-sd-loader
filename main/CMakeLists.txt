FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.cpp)

idf_component_register(SRCS ${app_sources}
                    INCLUDE_DIRS "."
                    REQUIRES ${components}
)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wunused-function
    -Wunused-macros
    -Wmissing-prototypes
)

