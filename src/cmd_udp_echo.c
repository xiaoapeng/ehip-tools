/**
 * @file cmd_udp_echo.c
 * @author simon.xiaoapeng@gmail.com
 * @brief 
 * @version 0.1
 * @date 2024-03-25
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#include "eh_swab.h"
#include "ehip-ipv4/ip_message.h"
#include "ehip_buffer.h"
#include <string.h>
#include <stdlib.h>
#include <eh_error.h>
#include <eh_formatio.h>
#include <eh_mem.h>
#include <eh_comp_timer.h>
#include <ehip_core.h>
#include <ehip-ipv4/ip.h>
#include <ehip-ipv4/udp.h>
#include <ehip_error.h>
#include <ehshell.h>

struct udp_echo_context{
    udp_pcb_t udp_pcb;
    ehshell_cmd_context_t *cmd_context;
};

static void udp_echo_context_clean(ehshell_cmd_context_t *cmd_context){
    struct udp_echo_context *ctx = ehshell_command_get_userdata(cmd_context);
    if(ctx != NULL){
        ehip_udp_delete(ctx->udp_pcb);
        eh_free(ctx);
    }
}
static void udp_echo_recv_callback(udp_pcb_t pcb, ipv4_addr_t addr, uint16_be_t port, struct ip_message *udp_rx_meg){
    struct udp_echo_context *ctx = ehip_udp_get_userdata(pcb);
    struct stream_base *stream = ehshell_command_stream(ctx->cmd_context);
    struct udp_sender udp_sender;
    int ret;
    ehip_buffer_size_t payload_size;
    ehip_buffer_size_t udp_size;
    /* 单次读取大小 */
    ehip_buffer_size_t single_read_size;
    uint8_t *write_ptr = NULL;
    ehip_buffer_t *send_buffer = NULL;
    udp_size = (ehip_buffer_size_t)ip_message_rx_data_size(udp_rx_meg);
    // eh_stream_printf(stream, "udp recv callback ip:" IPV4_FORMATIO ":%d size:%d \r\n", 
    //     ipv4_formatio(addr), eh_ntoh16(port), udp_size);
    ehip_udp_sender_init(pcb, &udp_sender, addr, port);
    ret = ehip_udp_sender_route_ready(&udp_sender);
    if(ret != 0){
        eh_stream_printf(stream, "ehip_udp_sender_route_ready failed. Error code: %d\r\n", ret);
        return ;
    }
    while(udp_size){
        ret = ehip_udp_sender_add_buffer(&udp_sender, &send_buffer);
        if(ret != 0){
            eh_stream_printf(stream, "ehip_udp_sender_add_buffer failed. Error code: %d\r\n", ret);
            goto clean;
        }
        payload_size = ehip_buffer_get_free_capacity(send_buffer);
        single_read_size = udp_size > payload_size ? payload_size : udp_size;
        write_ptr = ehip_buffer_payload_tail_append(send_buffer, single_read_size);
        ip_message_rx_real_read(udp_rx_meg, write_ptr, single_read_size);
        udp_size -= single_read_size;
    }
    ret = ehip_udp_send(pcb, &udp_sender);
    if(ret != 0){
        eh_stream_printf(stream, "ehip_udp_sender_send failed. Error code: %d\r\n", ret);
        goto clean;
    }
clean:
    eh_stream_finish(stream);
    ehip_udp_sender_deinit(&udp_sender);
}
void ehtools_udp_echo(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]){
    /* udp_echo <netdev|any> <ip|any> <port> */
    struct stream_base *stream = ehshell_command_stream(cmd_context);
    struct udp_echo_context *ctx;
    static ehip_netdev_t *netdev = NULL;
    ipv4_addr_t ipv4_addr = IPV4_ADDR_ANY;
    int port = -1;
    if(argc != 4)
        goto help;
    ctx = eh_malloc(sizeof(struct udp_echo_context));
    if(ctx == NULL){
        eh_stream_printf(stream, "ehtools_udp_echo malloc udp echo context failed. \r\n");
        goto error;
    }
    ctx->cmd_context = cmd_context;
    if(strcmp(argv[1], "any") != 0){
        netdev = ehip_netdev_find(argv[1]);
        if(netdev == NULL){
            eh_stream_printf(stream, "ehtools_udp_echo find netdev %s failed. \r\n", argv[1]);
            goto error;
        }
    }

    if(strcmp(argv[2], "any") != 0){
        if(ipv4_string_to_addr(argv[2], &ipv4_addr) != EH_RET_OK){
            eh_stream_printf(stream, "ehtools_udp_echo ipv4 string to addr %s failed. \r\n", argv[2]);
            goto error;
        }
    }
    port = atoi(argv[3]);
    if(port <= 0 || port > 65535){
        eh_stream_printf(stream, "ehtools_udp_echo port %s is invalid. \r\n", argv[3]);
        goto error;
    }
    if(netdev == NULL && ipv4_addr == IPV4_ADDR_ANY){
        ctx->udp_pcb = ehip_udp_any_new(eh_hton16((uint16_t)port));
    }else{
        ctx->udp_pcb = ehip_udp_new(ipv4_addr, eh_hton16((uint16_t)port), netdev);
    }
    if(eh_ptr_to_error(ctx->udp_pcb) < 0){
        eh_stream_printf(stream, "ehtools_udp_echo create udp pcb failed. Error code: %d\r\n", eh_ptr_to_error(ctx->udp_pcb));
        goto error;
    }
    ehip_udp_set_recv_callback(ctx->udp_pcb, udp_echo_recv_callback);
    ehip_udp_set_userdata(ctx->udp_pcb, ctx);
    ehshell_command_set_userdata(cmd_context, ctx);
    return ;
help:
    {
        const struct ehshell_command_info* command_info = ehshell_command_getcommand_info(cmd_context);
        eh_stream_printf(stream, "%s:\t%s\r\n", command_info->command, command_info->description);
        eh_stream_printf(stream, "\t%s\r\n", command_info->usage);
    }
error:
    eh_stream_finish(stream);
    ehshell_command_finish(cmd_context);
}

void ehtools_udp_echo_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event){
    if(ehshell_event & (EHSHELL_EVENT_SIGINT_REQUEST_QUIT | EHSHELL_EVENT_SHELL_EXIT)){
        udp_echo_context_clean(cmd_context);
        ehshell_command_finish(cmd_context);
    }
}