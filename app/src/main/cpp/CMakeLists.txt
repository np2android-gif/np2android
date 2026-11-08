cmake_minimum_required(VERSION 3.22)
project(np2android C CXX)

# ---- SDL2 (libSDL2.so) -------------------------------------------------------
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(SDL2)

# ---- Neko Project II 0.86 (libmain.so) ---------------------------------------
set(NP2 ${CMAKE_CURRENT_SOURCE_DIR}/np2)
set(PORT ${CMAKE_CURRENT_SOURCE_DIR}/port)

# CPU コア: OFF = i286c (0.86 の Android テンプレートと同じ), ON = IA-32
option(NP2_IA32 "Use IA-32 CPU core" OFF)

file(GLOB NP2_SOURCES CONFIGURE_DEPENDS
    ${NP2}/calendar.c
    ${NP2}/keystat.c
    ${NP2}/nevent.c
    ${NP2}/pccore.c
    ${NP2}/statsave.c
    ${NP2}/timing.c
    ${NP2}/bios/*.c
    ${NP2}/cbus/*.c
    ${NP2}/codecnv/*.c
    ${NP2}/common/*.c
    ${NP2}/embed/*.c
    ${NP2}/embed/menu/*.c
    ${NP2}/embed/menubase/*.c
    ${NP2}/fdd/*.c
    ${NP2}/font/*.c
    ${NP2}/generic/*.c
    ${NP2}/io/*.c
    ${NP2}/lio/*.c
    ${NP2}/mem/*.c
    ${NP2}/sdl2/*.c
    ${NP2}/sdl2/*.cpp
    ${NP2}/sound/*.c
    ${NP2}/sound/getsnd/*.c
    ${NP2}/sound/vermouth/*.c
    ${NP2}/vram/*.c
    ${PORT}/*.c
)
# port/ に Android 用の実装があるものは上流の sdl2 版を外す
list(REMOVE_ITEM NP2_SOURCES
    ${NP2}/sdl2/taskmng.c
    ${NP2}/sdl2/mousemng.c
)

if(NP2_IA32)
    file(GLOB NP2_CPU_SOURCES CONFIGURE_DEPENDS
        ${NP2}/debugsub386.c
        ${NP2}/i386c/*.c
        ${NP2}/i386c/ia32/*.c
        ${NP2}/i386c/ia32/instructions/*.c
        # x11 版と同じく FPU はダミー実装 (fpemul.c は USE_FPU 前提で未検証)
        ${NP2}/i386c/ia32/instructions/fpu/fpdummy.c
    )
    set(NP2_CPU_INCLUDES
        ${NP2}/i386c
        ${NP2}/i386c/ia32
        ${NP2}/i386c/ia32/instructions
        ${NP2}/i386c/ia32/instructions/fpu
    )
    set(NP2_CPU_DEFINES CPUCORE_IA32)
else()
    file(GLOB NP2_CPU_SOURCES CONFIGURE_DEPENDS
        ${NP2}/debugsub.c
        ${NP2}/i286c/*.c
    )
    set(NP2_CPU_INCLUDES ${NP2}/i286c)
    set(NP2_CPU_DEFINES)
endif()

add_library(main SHARED ${NP2_SOURCES} ${NP2_CPU_SOURCES})

target_include_directories(main PRIVATE
    ${PORT}
    ${NP2}
    ${NP2}/cbus
    ${NP2}/common
    ${NP2}/embed
    ${NP2}/embed/menu
    ${NP2}/embed/menubase
    ${NP2}/generic
    ${NP2}/io
    ${NP2}/mem
    ${NP2}/sdl2
    ${NP2}/sound
    ${NP2}/vram
    ${NP2_CPU_INCLUDES}
)

target_compile_definitions(main PRIVATE ${NP2_CPU_DEFINES})

# 2012〜2016年頃のコードを現行 clang で通すための緩和
target_compile_options(main PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-std=gnu99>
    -Wno-error=implicit-function-declaration
    -Wno-error=int-conversion
    -Wno-error=incompatible-pointer-types
    -Wno-error=implicit-int
    -Wno-deprecated-declarations
    -Wno-parentheses
    -Wno-dangling-else
    -fno-strict-aliasing
)

target_link_libraries(main PRIVATE SDL2 log android)
