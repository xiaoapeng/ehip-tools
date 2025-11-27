/**
 * @file cmd_ping.c
 * @brief Ping command.
 * @author simon.xiaoapeng (simon.xiaoapeng@gmail.com)
 * @date 2025-11-27
 * 
 * @copyright Copyright (c) 2025  simon.xiaoapeng@gmail.com
 * 
 */
#include <eh_error.h>
#include <eh_formatio.h>
#include <eh_debug.h>
#include <ehshell.h>
void ehtools_ping(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]){
    (void) cmd_context;
    (void) argc;
    (void) argv;
    /* TODO: implement ping command */
    eh_stream_printf(ehshell_command_stream(cmd_context), "ping command TODO...\r\n");
    ehshell_command_finish(cmd_context);
}
void ehtools_ping_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event){
    (void) cmd_context;
    (void) ehshell_event;
}