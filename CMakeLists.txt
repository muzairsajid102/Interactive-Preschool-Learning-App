cmake_minimum_required(VERSION 3.16)

project(InteractiveAlphabetLearningSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets)

qt_add_executable(InteractiveAlphabetLearningSystem
    main.cpp
    MainWindow.h
    MainWindow.cpp
)

target_link_libraries(InteractiveAlphabetLearningSystem PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)

target_compile_definitions(InteractiveAlphabetLearningSystem PRIVATE
    PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)