cmake_minimum_required(VERSION 3.10)

add_library(ehip-tools OBJECT)
target_sources(ehip-tools PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/src/cmd.c"
)

if(CONFIG_PACKAGE_EHIP_TOOLS_PING)
    target_sources(ehip-tools PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/cmd_ping.c"
    )
endif()



target_link_libraries(ehip-tools PRIVATE eventhub ehip ehshell argparse)
