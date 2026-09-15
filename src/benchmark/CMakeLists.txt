set(EXE_NAME "controller_benchmark")

# SDL2 不随系统安装，参照 spdlog 的方式以固定tag浅克隆引入
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
include(FetchContent)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.32.10
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL2)

add_executable(${EXE_NAME}
    main.cpp
    AudioPlayer.cpp
    Stats.cpp
)
target_link_libraries(${EXE_NAME} PRIVATE
    Controller
    SDL2::SDL2
)

# 控制台程序自带入口点：禁用SDL_main对main的宏替换
target_compile_definitions(${EXE_NAME} PRIVATE SDL_MAIN_HANDLED)

if (WIN32)
    # Controller.dll 运行期依赖av*.dll，拷到exe目录让demo免设PATH即可运行
    file(GLOB AV_RUNTIME_DLLS "${FFMPEG_DIR}/bin/av*.dll")
    if (AV_RUNTIME_DLLS)
        add_custom_command(TARGET ${EXE_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${AV_RUNTIME_DLLS} "$<TARGET_FILE_DIR:${EXE_NAME}>"
            VERBATIM)
    endif()
endif()
