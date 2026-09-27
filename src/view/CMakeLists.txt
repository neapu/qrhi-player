set(EXE_NAME "qrhi_player")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

qt_add_executable(${EXE_NAME}
    main.cpp
    Controller.h
    Controller.cpp
    MainWindow.h
    MainWindow.cpp
    LogManager.h
    LogManager.cpp

    components/VolumeButton.h
    components/VolumeButton.cpp
    components/PlayOrPauseButton.h
    components/PlayOrPauseButton.cpp
    components/StopButton.h
    components/StopButton.cpp
    components/PlaybackLabel.h
    components/PlaybackLabel.cpp

)

qt_add_resources(${EXE_NAME} "player_icons"
    PREFIX "/icons"
    BASE "images"
    FILES
        images/pause.svg
        images/play.svg
        images/stop.svg
        images/volume-mute.svg
        images/volume-down.svg
        images/volume-up.svg
)

add_subdirectory(video_renderer)
add_subdirectory(audio_renderer)

target_link_libraries(${EXE_NAME} PRIVATE Qt6::Core Qt6::Widgets Media spdlog video_renderer audio_renderer)


if (WIN32)
    # Debug 配置为控制台应用程序，方便调试；Release 保持 GUI 程序。
    # 用生成器表达式而非 CMAKE_BUILD_TYPE 判断，兼容多配置生成器（VS / Ninja Multi-Config）
    set_target_properties(${EXE_NAME} PROPERTIES
        WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>
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