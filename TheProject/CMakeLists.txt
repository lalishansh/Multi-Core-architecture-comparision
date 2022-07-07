# CMakeList.txt : CMake project for FaceCaputureAPP, include source and define
# project specific logic here.
cmake_minimum_required (VERSION 3.8)

find_package(fmt CONFIG REQUIRED)

set (TARGET_EXEC "test")
file(GLOB HEADER_COLLECTION "src/**.h" "src/**.hpp")

add_executable(${TARGET_EXEC} 
    # Headers
        ${HEADER_COLLECTION}
    # Source
        #"src/"
        "src/main.cpp"
        "src/models/source.cpp"
    )
target_include_directories (${TARGET_EXEC}
    PRIVATE 
        "src/"
    )

target_link_libraries (${TARGET_EXEC}
    PRIVATE 
       fmt::fmt
    )

set_target_properties(${TARGET_EXEC} PROPERTIES LINK_FLAGS "/PROFILE")