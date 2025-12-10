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


extern void ehtools_ping(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]);
extern void ehtools_ping_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event);
extern void ehtools_nslookup(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]);
extern void ehtools_nslookup_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event);
extern void ehtools_telnet(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]);
extern void ehtools_telnet_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event);

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
};

static int __init cmd_init(void){
    int ret;
    if(!ehshell_default()){
        eh_mwarnfl(TOOLS_CMD, "ehshell_default is NULL");
        return 0;
    }
    ret = ehshell_register_commands(ehshell_default(), ehtools_command_info_tbl, EH_ARRAY_SIZE(ehtools_command_info_tbl));
    if(ret < 0){
        eh_errfl("ehshell_register_commands failed, ret = %d", ret);
        return ret;
    }
    return 0;
}


ehshell_module_default_command_export(cmd_init, NULL);