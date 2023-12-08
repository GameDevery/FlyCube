set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options("/MP")
    add_definitions(-DUNICODE -D_UNICODE)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-DNOMINMAX)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_definitions(-DTARGET_IOS=1)
elseif (CMAKE_SYSTEM_NAME STREQUAL "tvOS")
    add_definitions(-DTARGET_TVOS=1)
else()
    add_definitions(-DTARGET_MACOS=1)
endif()
