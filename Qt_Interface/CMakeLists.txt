cmake_minimum_required(VERSION 3.19)
project(Rpi_Interface VERSION 0.1 LANGUAGES CXX)

# ✅ Ajouter le chemin de Qt6
set(CMAKE_PREFIX_PATH "C:/Qt/6.8.2/mingw_64/lib/cmake")

# ✅ Trouver Qt6 avec QtCharts et QtMqtt
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Mqtt Charts)

qt_standard_project_setup()

qt_add_executable(Rpi_Interface
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
)

# ✅ Lier les bibliothèques Qt nécessaires
target_link_libraries(Rpi_Interface
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Mqtt
        Qt6::Charts
)

include(GNUInstallDirs)

install(TARGETS Rpi_Interface
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET Rpi_Interface
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
