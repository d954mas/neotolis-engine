nt_add_module(nt_blob LOG_DOMAIN "blob" nt_blob.c nt_blob.h)
target_link_libraries(nt_blob PUBLIC nt_core PRIVATE nt_pool nt_shared)
