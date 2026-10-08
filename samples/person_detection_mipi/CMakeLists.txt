cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(person_detection_mipi)

set(NO_THREADSAFE_STATICS $<TARGET_PROPERTY:compiler-cpp,no_threadsafe_statics>)
zephyr_compile_options($<$<COMPILE_LANGUAGE:CXX>:${NO_THREADSAFE_STATICS}>)

target_sources(app PRIVATE
  src/main.c
  src/person_detection_mipi_capture.c
  src/person_detection_mipi_app.c
  src/person_detection_mipi_metadata.c
  src/person_detection_mipi_stream.c
  src/person_detection_mipi_inference.cpp
  src/person_detection_mipi_cmsis_shim.c
  src/infer_utils.c
  src/stream_cdc_transport.c
)
