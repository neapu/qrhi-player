set(EXE_NAME "qrhi_video_player")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

qt_add_executable(${EXE_NAME}
    main.cpp
)

target_link_libraries(${EXE_NAME} PRIVATE Qt6::Core Qt6::Widgets)

include(FetchContent)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(spdlog)

target_link_libraries(${EXE_NAME} PRIVATE spdlog::spdlog)

if (WIN32)
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        # 设置为控制台应用程序，方便调试
        set_target_properties(${EXE_NAME} PROPERTIES
            WIN32_EXECUTABLE FALSE
        )
    endif()

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