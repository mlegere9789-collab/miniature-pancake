cmake_minimum_required(VERSION 3.16)
project({{cookiecutter.plugin_slug}} VERSION {{cookiecutter.version}} LANGUAGES C)

# The ONLY dependency is the Dino 8 plug-in SDK. Point CMake at it with
#   -DCMAKE_PREFIX_PATH=<sdk prefix>      (the prefix you installed the SDK to)
find_package(Dino8PluginSdk 2 REQUIRED)

dino8_add_plugin({{cookiecutter.plugin_slug}} SOURCES src/plugin.c)
