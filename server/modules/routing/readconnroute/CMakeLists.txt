add_library(readconnroute SHARED readconnroute.cc)
target_link_libraries(readconnroute maxscale-common)
set_target_properties(readconnroute PROPERTIES VERSION "1.1.0"  LINK_FLAGS -Wl,-z,defs)
install_module(readconnroute core)
