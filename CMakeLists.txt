cmake_minimum_required(VERSION 3.10)
project(ArchiveFS)

# Установка стандарта C++ (C++17 или выше)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Включение поиска пакетов через pkg-config
find_package(PkgConfig REQUIRED)

# Поиск libarchive
pkg_check_modules(LIBARCHIVE REQUIRED libarchive)
if(NOT LIBARCHIVE_FOUND)
    message(FATAL_ERROR "libarchive not found!")
endif()

# Поиск FUSE (пробуем разные версии)
set(FUSE_NAMES fuse3 fuse)
foreach(fuse_name IN LISTS FUSE_NAMES)
    pkg_search_module(FUSE ${fuse_name})
    if(FUSE_FOUND)
        break()
    endif()
endforeach()

if(NOT FUSE_FOUND)
    # Если через pkg-config не нашли, пробуем ручную настройку
    find_package(FUSE REQUIRED)
    if(NOT FUSE_FOUND)
        message(FATAL_ERROR "FUSE library not found!")
    endif()
endif()

# Настройка include-директорий
include_directories(
    include
    ${LIBARCHIVE_INCLUDE_DIRS}
    ${FUSE_INCLUDE_DIRS}
)

# Сборка исполняемого файла
add_executable(ArchiveFS
    mainCode/main.cpp
    src/ArchiveFS.cpp
    src/FuseWrapper.cpp
)

# Настройка флагов компиляции
target_compile_options(ArchiveFS PRIVATE
    ${LIBARCHIVE_CFLAGS}
    ${FUSE_CFLAGS}
    -O3
    -g
    -Wall
    -Wextra
)

# Настройка линковки
target_link_libraries(ArchiveFS
    ${LIBARCHIVE_LIBRARIES}
    ${FUSE_LIBRARIES}
)

# Для совместимости с разными системами
if(APPLE)
    # macOS требует специальных флагов для FUSE
    target_link_options(ArchiveFS PRIVATE -Wl,-U,_fuse_exit)
endif()