cmake_minimum_required(VERSION 3.20)

project(HolyBloodPrototype LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "Disable GLFW documentation targets" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Disable GLFW example targets" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Disable GLFW test targets" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build bundled dependencies statically" FORCE)

option(HB_GLFW_BUILD_X11 "Build GLFW's X11 backend on Linux" ON)
option(HB_GLFW_BUILD_WAYLAND "Build GLFW's Wayland backend on Linux" ON)
if(UNIX AND NOT APPLE)
    set(GLFW_BUILD_X11 ${HB_GLFW_BUILD_X11} CACHE BOOL "Build GLFW's X11 backend" FORCE)
    set(GLFW_BUILD_WAYLAND ${HB_GLFW_BUILD_WAYLAND} CACHE BOOL "Build GLFW's Wayland backend" FORCE)
endif()

add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)

find_package(OpenGL REQUIRED)

add_library(glew STATIC third_party/glew/src/glew.c)
target_include_directories(glew PUBLIC third_party/glew/include)
target_compile_definitions(glew PUBLIC GLEW_STATIC)
target_link_libraries(glew PUBLIC OpenGL::GL ${CMAKE_DL_LIBS})

file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/SimpleGame/*.cpp")

add_executable(HolyBloodPrototype ${GAME_SOURCES})
target_include_directories(HolyBloodPrototype PRIVATE
    SimpleGame
    third_party/glew/include
    third_party/stb
)
target_compile_definitions(HolyBloodPrototype PRIVATE NOMINMAX GLFW_INCLUDE_NONE)
target_link_libraries(HolyBloodPrototype PRIVATE glfw glew OpenGL::GL)
if(WIN32)
    target_link_libraries(HolyBloodPrototype PRIVATE psapi)
endif()

if(MSVC)
    target_compile_options(HolyBloodPrototype PRIVATE /utf-8 /W3)
elseif(APPLE)
    target_compile_options(HolyBloodPrototype PRIVATE -Wall -Wextra -Wno-unused-parameter)
else()
    target_compile_options(HolyBloodPrototype PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_custom_command(TARGET HolyBloodPrototype POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/SimpleGame/Data"
            "$<TARGET_FILE_DIR:HolyBloodPrototype>/Data"
    COMMENT "Copying game data and bundled fonts beside the executable"
)

install(TARGETS HolyBloodPrototype RUNTIME DESTINATION .)
install(DIRECTORY SimpleGame/Data/ DESTINATION Data PATTERN "Cache" EXCLUDE)
