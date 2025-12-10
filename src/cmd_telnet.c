/**
 * @file cmd_telnet.c
 * @brief Telnet command.
 * @author simon.xiaoapeng (simon.xiaoapeng@gmail.com)
 * @date 2025-12-07
 * 
 * @copyright Copyright (c) 2025  simon.xiaoapeng@gmail.com
 * 
 */

#include <eh_error.h>
#include <eh_formatio.h>
#include <eh_ringbuf.h>
#include <ehip_core.h>
#include <ehip-ipv4/ip.h>
#include <ehip-ipv4/ping.h>
#include <ehshell.h>

void ehtools_telnet(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]){
    (void)cmd_context;
    (void)argc;
    (void)argv;
    /* TODO */
}

void ehtools_telnet_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event){
    if(ehshell_event & EHSHELL_EVENT_RECEIVE_INPUT_DATA){
        int32_t readable_size;
        int32_t rl = 0;
        const char *input_buf[2] = {NULL, NULL};
        size_t input_buf_len[2] = {0, 0};
        eh_ringbuf_t *ringbuf = ehshell_command_input_ringbuf(cmd_context, &readable_size);
        if(!ringbuf){
            return;
        }
        rl = 0;
        input_buf[0] = (const char *)eh_ringbuf_peek(ringbuf, 0, NULL, &rl);
        input_buf_len[0] = (size_t)rl;
        rl = 0;
        input_buf[1] = (const char *)eh_ringbuf_peek(ringbuf, (int32_t)input_buf_len[0], NULL, &rl);
        input_buf_len[1] = (size_t)rl;
        if(input_buf_len[0] > (size_t)readable_size){
            input_buf_len[0] = (size_t)readable_size;
            input_buf_len[1] = 0;
        }else if(input_buf_len[1] > (size_t)readable_size - input_buf_len[0]){
            input_buf_len[1] = (size_t)readable_size - input_buf_len[0];
        }
        eh_stream_printf(ehshell_command_stream(cmd_context), "receive:|%.*hhq %.*hhq|\r\n", 
            (int)input_buf_len[0], input_buf[0], (int)input_buf_len[1], input_buf[1]);
        eh_ringbuf_read_skip(ringbuf, (int32_t)(input_buf_len[0] + input_buf_len[1]));
    }
    if(ehshell_event & EHSHELL_EVENT_SIGINT_REQUEST_QUIT){
        ehshell_command_finish(cmd_context);
    }
}