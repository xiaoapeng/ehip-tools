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

if(CONFIG_PACKAGE_EHIP_TOOLS_NSLOOKUP)
    target_sources(ehip-tools PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/cmd_nslookup.c"
    )
endif()

if(CONFIG_PACKAGE_EHIP_TOOLS_TELNET)
    target_sources(ehip-tools PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/cmd_telnet.c"
    )
endif()

if(CONFIG_PACKAGE_EHIP_TOOLS_IFCONFIG)
    target_sources(ehip-tools PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/cmd_ifconfig.c"
    )
endif()

if(CONFIG_PACKAGE_EHIP_TOOLS_ROUTE)
    target_sources(ehip-tools PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/cmd_route.c"
    )
endif()

if(CONFIG_PACKAGE_EHIP_TOOLS_TRACEROUTE)
    target_sources(ehip-tools PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/cmd_traceroute.c"
    )
endif()

if(CONFIG_PACKAGE_EHIP_TOOLS_UDP_ECHO)
    target_sources(ehip-tools PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/cmd_udp_echo.c"
    )
endif()

if(CONFIG_PACKAGE_EHIP_TOOLS_TCP_ECHO)
    target_sources(ehip-tools PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/cmd_tcp_echo.c"
    )
endif()

target_link_libraries(ehip-tools PRIVATE eventhub ehip ehshell argparse)
