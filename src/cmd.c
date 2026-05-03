/**
 * @file cmd.c
 * @brief Command line tools for ehip.
 * @author simon.xiaoapeng (simon.xiaoapeng@gmail.com)
 * @date 2025-11-27
 * 
 * @copyright Copyright (c) 2025  simon.xiaoapeng@gmail.com
 * 
 */

#include <eh_error.h>
#include <eh_module.h>
#include <eh_debug.h>
#include <ehshell.h>
#include <ehshell_module.h>
#include <autoconf.h>

#ifdef CONFIG_PACKAGE_EHIP_TOOLS_PING
extern void ehtools_ping(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]);
extern void ehtools_ping_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event);
#endif

#ifdef CONFIG_PACKAGE_EHIP_TOOLS_NSLOOKUP
extern void ehtools_nslookup(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]);
extern void ehtools_nslookup_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event);
#endif
#ifdef CONFIG_PACKAGE_EHIP_TOOLS_IFCONFIG
extern void ehtools_ifconfig(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]);
#endif

#ifdef CONFIG_PACKAGE_EHIP_TOOLS_ROUTE
extern void ehtools_route(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]);
extern void ehtools_route_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event);
#endif

#ifdef CONFIG_PACKAGE_EHIP_TOOLS_TRACEROUTE
extern void ehtools_traceroute(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]);
extern void ehtools_traceroute_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event);
#endif

#ifdef CONFIG_PACKAGE_EHIP_TOOLS_TELNET
extern void ehtools_telnet(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]);
extern void ehtools_telnet_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event);
#endif

#ifdef CONFIG_PACKAGE_EHIP_TOOLS_UDP_ECHO
extern void ehtools_udp_echo(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]);
extern void ehtools_udp_echo_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event);
#endif


#ifdef CONFIG_PACKAGE_EHIP_TOOLS_TCP_ECHO
extern void ehtools_tcp_echo(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]);
extern void ehtools_tcp_echo_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event);
#endif



static struct ehshell_command_info ehtools_command_info_tbl[] = {
#ifdef CONFIG_PACKAGE_EHIP_TOOLS_PING
    {
        .command = "ping",
        .description = "Ping command.",
        .usage = "ping <ip|domain>",
        .flags = 0,
        .do_function = ehtools_ping,
        .do_event_function = ehtools_ping_event
    },
#endif
#ifdef CONFIG_PACKAGE_EHIP_TOOLS_NSLOOKUP
    {
        .command = "nslookup",
        .description = "Nslookup command.",
        .usage = "nslookup <domain> [A|CNAME]",
        .flags = 0,
        .do_function = ehtools_nslookup,
        .do_event_function = ehtools_nslookup_event
    },
#endif
#ifdef CONFIG_PACKAGE_EHIP_TOOLS_IFCONFIG
    {
        .command = "ifconfig",
        .description = "Network interface configuration command.",
        .usage = "ifconfig [interface] [up|down] [ip] [netmask mask]",
        .flags = 0,
        .do_function = ehtools_ifconfig,
        .do_event_function = NULL
    },
#endif
#ifdef CONFIG_PACKAGE_EHIP_TOOLS_ROUTE
    {
        .command = "route",
        .description = "Route table configuration command.",
        .usage = "route [add|del <dst>/<mask> [gw <gw>] [dev <dev>] [metric <m>]]",
        .flags = 0,
        .do_function = ehtools_route,
        .do_event_function = ehtools_route_event
    },
#endif
#ifdef CONFIG_PACKAGE_EHIP_TOOLS_TRACEROUTE
    {
        .command = "traceroute",
        .description = "Traceroute command.",
        .usage = "traceroute <ip|domain> [max_ttl] [max_retry]",
        .flags = 0,
        .do_function = ehtools_traceroute,
        .do_event_function = ehtools_traceroute_event
    },
#endif
#ifdef CONFIG_PACKAGE_EHIP_TOOLS_TELNET
    {
        .command = "telnet",
        .description = "Telnet command.",
        .usage = "telnet <ip|domain> <port>",
        .flags = EHSHELL_COMMAND_REDIRECT_INPUT,
        .do_function = ehtools_telnet,
        .do_event_function = ehtools_telnet_event
    },
#endif
#ifdef CONFIG_PACKAGE_EHIP_TOOLS_UDP_ECHO
    {
        .command = "udp_echo",
        .description = "Udp echo command.",
        .usage = "udp_echo <netdev|any> <ip|any> <port>",
        .flags = 0,
        .do_function = ehtools_udp_echo,
        .do_event_function = ehtools_udp_echo_event
    },
#endif
#ifdef CONFIG_PACKAGE_EHIP_TOOLS_TCP_ECHO
    {
        .command = "tcp_echo",
        .description = "Tcp echo server command.",
        .usage = "tcp_echo <port> [bind_addr] [netdev]",
        .flags = 0,
        .do_function = ehtools_tcp_echo,
        .do_event_function = ehtools_tcp_echo_event
    },
#endif
};

static int __init cmd_init(void){
    int ret;
    ret = ehshell_register_commands(ehtools_command_info_tbl, EH_ARRAY_SIZE(ehtools_command_info_tbl));
    if(ret < 0){
        eh_errfl("ehshell_register_commands failed, ret = %d", ret);
        return ret;
    }
    return 0;
}

ehshell_module_command_export(cmd_init, NULL);