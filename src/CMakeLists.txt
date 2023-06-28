add_library(gcpp SHARED gc_scan.cpp copy_collector.cpp 
                        safe_alloc.cpp mem_prot.cpp
                        concurrent_gc.cpp)
target_include_directories(gcpp PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_compile_options(gcpp PRIVATE ${COMPILE_FLAGS})