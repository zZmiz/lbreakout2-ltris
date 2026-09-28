
# v5's measured linker heap span was 47672 bytes. Keep at least 47616,
# allowing only 56 bytes for pointers/telemetry, NOT multi-KiB new buffers.
# This is a floor, not proof of runtime safety; the constrained fragment
# test and hardware mallinfo checkpoints are also required.
file(READ "${MAP}" map_text)
string(REGEX MATCH "0x([0-9a-fA-F]+)[ \t]+__end__ =" end_match "${map_text}")
if(NOT end_match)
    message(FATAL_ERROR "Missing __end__ in ${MAP}")
endif()
set(heap_start "0x${CMAKE_MATCH_1}")
string(REGEX MATCH "0x([0-9a-fA-F]+)[ \t]+__HeapLimit =" limit_match "${map_text}")
if(NOT limit_match)
    message(FATAL_ERROR "Missing __HeapLimit in ${MAP}")
endif()
math(EXPR heap_bytes "0x${CMAKE_MATCH_1} - ${heap_start}")
if(heap_bytes LESS 47616)
    message(FATAL_ERROR "LTris SRAM heap ${heap_bytes} bytes < 47616-byte floor; runtime HDMI queue alone needs 36864 bytes")
endif()
message(STATUS "LTris SRAM guard: heap=${heap_bytes} bytes, runtime HDMI queue=36864 bytes (other dynamic allocations still required)")
