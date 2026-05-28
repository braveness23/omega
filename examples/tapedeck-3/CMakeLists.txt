add_executable(tapedeck-3 main.cpp)

target_link_libraries(tapedeck-3 PRIVATE omega::omega)

target_compile_features(tapedeck-3 PRIVATE cxx_std_17)
