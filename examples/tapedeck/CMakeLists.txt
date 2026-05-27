add_executable(tapedeck main.cpp)

target_link_libraries(tapedeck PRIVATE omega::core)

target_compile_features(tapedeck PRIVATE cxx_std_17)
