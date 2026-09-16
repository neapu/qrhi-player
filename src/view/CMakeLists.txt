set(EXE_NAME "qrhi_player")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Svg)

qt_standard_project_setup()

qt_add_executable(${EXE_NAME}
    main.cpp
    LogManager.h
    LogManager.cpp
    MainWindow.h
    MainWindow.cpp
)

target_link_libraries(${EXE_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Svg Controller)

include(FetchContent)

# spdlog 默认用窄字符 fopen 打开日志文件，无法处理含非 ASCII 字符（如中文用户名）的
# %APPDATA% 路径。必须在 FetchContent_MakeAvailable 之前定义，
# 保证 spdlog 库本身与本项目使用一致的 filename_t，否则会因 ODR 冲突崩溃
if (WIN32)
    add_compile_definitions(SPDLOG_WCHAR_FILENAMES)
endif()

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(spdlog)

target_link_libraries(${EXE_NAME} PRIVATE spdlog::spdlog)

if (WIN32)
    # Debug 配置为控制台应用程序，方便调试；Release 保持 GUI 程序。
    # 用生成器表达式而非 CMAKE_BUILD_TYPE 判断，兼容多配置生成器（VS / Ninja Multi-Config）
    set_target_properties(${EXE_NAME} PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Debug>
    )

    # 配置Windows平台下，自动拷贝Qt依赖库到bin目录
    find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if (WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${EXE_NAME} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" 
                --$<LOWER_CASE:$<CONFIG>>
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                $<TARGET_FILE:${EXE_NAME}>
            COMMENT "Running windeployqt to deploy Qt dependencies..."
            VERBATIM
        )
    else()
        message(WARNING "windeployqt not found. Please ensure it is installed and available in the PATH.")
    endif()
endif()