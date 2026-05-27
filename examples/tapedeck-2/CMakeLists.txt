add_executable(tapedeck-2 main.cpp)

target_link_libraries(tapedeck-2 PRIVATE omega::omega)

target_compile_features(tapedeck-2 PRIVATE cxx_std_17)
