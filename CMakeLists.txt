cmake_minimum_required(VERSION 3.10)
project(gimgtools C)

set(CMAKE_C_STANDARD 99)

# --- gimglib (static library) ---
add_library(gimglib STATIC
    gimglib.c util.c sf_typ.c sf_mps.c sf_tre.c sf_rgn.c sf_lbl.c
    sf_net.c sf_nod.c sf_dem.c sf_mar.c sf_gmp.c
)
target_compile_definitions(gimglib PRIVATE _FILE_OFFSET_BITS=64)

if(NOT WIN32)
    find_package(Iconv REQUIRED)
    target_link_libraries(gimglib PRIVATE Iconv::Iconv)
endif()

# --- util_indep (reused by 4 tools) ---
set(UTIL_INDEP util_indep.c)

# --- executables ---
add_executable(gimginfo gimginfo.c)
target_link_libraries(gimginfo PRIVATE gimglib)

add_executable(gimgfixcmd gimgfixcmd.c cmdlib.c)
target_link_libraries(gimgfixcmd PRIVATE gimglib $<$<NOT:$<C_COMPILER_ID:MSVC>>:m>)

add_executable(gimgxor gimgxor.c)

add_executable(gimgunlock gimgunlock.c ${UTIL_INDEP})
add_executable(gimgch gimgch.c ${UTIL_INDEP})
add_executable(gimgextract gimgextract.c ${UTIL_INDEP})
add_executable(gimgchcodepage gimgchcodepage.c ${UTIL_INDEP})

add_executable(cmdc cmdc.c)
target_link_libraries(cmdc PRIVATE $<$<NOT:$<C_COMPILER_ID:MSVC>>:m>)

