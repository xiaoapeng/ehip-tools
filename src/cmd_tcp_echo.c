/**
 * @file cmd_tcp_echo.c
 * @author simon.xiaoapeng@gmail.com
 * @brief TCP Echo Server Command Tool
 * @version 0.1
 * @date 2024-03-25
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#include <string.h>
#include <stdlib.h>
#include <eh_error.h>
#include <eh_formatio.h>
#include <eh_mem.h>
#include <eh_comp_timer.h>
#include <eh_swab.h>
#include <ehip_core.h>
#include <ehip_buffer.h>
#include <ehip_error.h>
#include <ehip-ipv4/ip.h>
#include <ehip-ipv4/tcp.h>
#include <ehip-ipv4/ip_message.h>
#include <ehshell.h>

struct tcp_echo_context {
    tcp_server_pcb_t tcp_server;
    ehshell_cmd_context_t *cmd_context;
};

static void tcp_echo_context_clean(ehshell_cmd_context_t *cmd_context) {
    struct tcp_echo_context *ctx = ehshell_command_get_userdata(cmd_context);
    if (ctx != NULL) {
        if (ctx->tcp_server != NULL) {
            ehip_tcp_server_delete(ctx->tcp_server);
        }
        eh_free(ctx);
    }
}

static void tcp_echo_recv_callback(tcp_pcb_t pcb, enum tcp_event state) {
    struct tcp_echo_context *ctx = ehip_tcp_client_get_userdata(pcb);
    struct stream_base *stream = ehshell_command_stream(ctx->cmd_context);
    tcp_client_info_t info;
    uint8_t *data_ptr;
    int32_t len;
    int32_t offset;
    int32_t wl = 0;
    eh_ringbuf_t *tx_ringbuf = ehip_tcp_client_get_send_ringbuf(pcb);
    eh_ringbuf_t *rx_ringbuf = ehip_tcp_client_get_recv_ringbuf(pcb);
    
    ehip_tcp_client_get_info(pcb, &info);

    eh_stream_printf(stream, "TCP Echo: ("IPV4_FORMATIO":%d)->("IPV4_FORMATIO":%d) size:%d\r\n", 
        ipv4_formatio(info.remote_addr),
        info.remote_port,
        ipv4_formatio(info.local_addr), 
        info.local_port,
        eh_ringbuf_size(rx_ringbuf));
    
    // 回显数据
    len = 0;
    data_ptr = (uint8_t *)eh_ringbuf_peek(rx_ringbuf, 0, NULL, &len);
    wl += eh_ringbuf_write(tx_ringbuf, data_ptr, len);
    offset = len;
    len = 0;
    data_ptr = (uint8_t *)eh_ringbuf_peek(rx_ringbuf, offset, NULL, &len);
    wl += eh_ringbuf_write(tx_ringbuf, data_ptr, len);
    eh_ringbuf_read_skip(rx_ringbuf, wl);
    
    if (state == TCP_RECV_DATA || eh_ringbuf_free_size(tx_ringbuf) == 0 || eh_ringbuf_size(rx_ringbuf) == 0) {
        ehip_tcp_client_request_send(pcb);
    }
}

static void tcp_echo_connect_change_callback(tcp_pcb_t pcb, enum tcp_event state) {
    struct tcp_echo_context *ctx = ehip_tcp_client_get_userdata(pcb);
    struct stream_base *stream = ehshell_command_stream(ctx->cmd_context);
    
    switch (state) {
        case TCP_CONNECT_TIMEOUT:
        case TCP_ERROR:
        case TCP_RECV_FIN:
        case TCP_RECV_RST:
        case TCP_SEND_TIMEOUT:
        case TCP_KEEPALIVE_TIMEOUT:
        case TCP_DISCONNECTED:
            eh_stream_printf(stream, "TCP Echo: Connection closed - %s\r\n", 
                state == TCP_CONNECT_TIMEOUT ? "TCP_CONNECT_TIMEOUT" :
                state == TCP_ERROR ? "TCP_ERROR" :
                state == TCP_RECV_FIN ? "TCP_RECV_FIN" :
                state == TCP_RECV_RST ? "TCP_RECV_RST" : 
                state == TCP_SEND_TIMEOUT ? "TCP_SEND_TIMEOUT" :
                state == TCP_KEEPALIVE_TIMEOUT ? "TCP_KEEPALIVE_TIMEOUT" :
                state == TCP_DISCONNECTED ? "TCP_DISCONNECTED" : "UNKNOWN"
            );
            ehip_tcp_client_delete(pcb);
            break;
        case TCP_CONNECTED:
            eh_stream_printf(stream, "TCP Echo: New connection established\r\n");
            break;
        case TCP_RECV_DATA:
            tcp_echo_recv_callback(pcb, state);
            break;
        case TCP_RECV_ACK:
            tcp_echo_recv_callback(pcb, state);
            break;
    }
}

static void tcp_new_connect(tcp_server_pcb_t server_pcb, tcp_pcb_t new_client) {
    struct tcp_echo_context *ctx = ehip_tcp_server_get_userdata(server_pcb);
    struct stream_base *stream = ehshell_command_stream(ctx->cmd_context);
    
    eh_stream_printf(stream, "TCP Echo: New client connected\r\n");
    ehip_tcp_set_events_callback(new_client, tcp_echo_connect_change_callback);
    ehip_tcp_client_set_userdata(new_client, ctx);
}

void ehtools_tcp_echo(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]) {
    /* tcp_echo <port> [bind_addr] [netdev] */
    struct stream_base *stream = ehshell_command_stream(cmd_context);
    struct tcp_echo_context *ctx = NULL;
    int port = -1;
    ipv4_addr_t bind_addr = IPV4_ADDR_ANY;
    ehip_netdev_t *netdev = NULL;
    uint16_t rx_buf_size = 1460 * 2;
    uint16_t tx_buf_size = 1460 * 2;
    int ret;
    
    if (argc < 2 || argc > 4)
        goto help;
    
    ctx = eh_malloc(sizeof(struct tcp_echo_context));
    if (ctx == NULL) {
        eh_stream_printf(stream, "TCP Echo: malloc tcp echo context failed.\r\n");
        goto error;
    }
    ctx->cmd_context = cmd_context;
    ctx->tcp_server = NULL;
    
    port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        eh_stream_printf(stream, "TCP Echo: port %s is invalid.\r\n", argv[1]);
        goto error;
    }
    
    // 检查是否需要使用any模式
    int use_any_mode = 0;
    
    if (argc == 2) {
        // 只有端口参数：使用 any_new
        use_any_mode = 1;
    } else if (argc >= 3) {
        // 检查第二个参数是否为"any"
        if (strcmp(argv[2], "any") == 0) {
            use_any_mode = 1;
        }
    }
    
    if (argc == 4) {
        // 检查第三个参数是否为"any"
        if (strcmp(argv[3], "any") == 0) {
            use_any_mode = 1;
        } else {
            // 解析网络设备
            netdev = ehip_netdev_find(argv[3]);
            if (netdev == NULL) {
                eh_stream_printf(stream, "TCP Echo: netdev %s not found.\r\n", argv[3]);
                goto error;
            }
        }
    }
    
    if (argc >= 3 && !use_any_mode) {
        // 解析绑定地址
        if (ipv4_string_to_addr(argv[2], &bind_addr) != EH_RET_OK) {
            eh_stream_printf(stream, "TCP Echo: bind address %s is invalid.\r\n", argv[2]);
            goto error;
        }
    }
    
    // 根据模式选择服务器创建函数
    if (use_any_mode) {
        ctx->tcp_server = ehip_tcp_server_any_new(eh_hton16((uint16_t)port), rx_buf_size, tx_buf_size);
        eh_stream_printf(stream, "TCP Echo Server (any) started on port %d\r\n", port);
    } else {
        ctx->tcp_server = ehip_tcp_server_new(bind_addr, eh_hton16((uint16_t)port), netdev, rx_buf_size, tx_buf_size);
        eh_stream_printf(stream, "TCP Echo Server started on "IPV4_FORMATIO":%d", 
            ipv4_formatio(bind_addr), port);
        if (netdev != NULL) {
            eh_stream_printf(stream, " (netdev: %s)", argv[3]);
        }
        eh_stream_printf(stream, "\r\n");
    }
    
    if (eh_ptr_to_error(ctx->tcp_server) < 0) {
        eh_stream_printf(stream, "TCP Echo: create tcp server failed. Error code: %d\r\n", eh_ptr_to_error(ctx->tcp_server));
        goto error;
    }
    
    // 设置回调函数
    ehip_tcp_server_set_new_connect_callback(ctx->tcp_server, tcp_new_connect);
    ehip_tcp_server_set_userdata(ctx->tcp_server, ctx);
    
    // 开始监听
    ret = ehip_tcp_server_listen(ctx->tcp_server);
    if (ret < 0) {
        eh_stream_printf(stream, "TCP Echo: server listen failed. Error code: %d\r\n", ret);
        goto error;
    }
    
    ehshell_command_set_userdata(cmd_context, ctx);
    
    return ;
    
help:
    {
        const struct ehshell_command_info* command_info = ehshell_command_getcommand_info(cmd_context);
        eh_stream_printf(stream, "%s:\t%s\r\n", command_info->command, command_info->description);
        eh_stream_printf(stream, "\t%s\r\n", command_info->usage);
        eh_stream_printf(stream, "Usage:\r\n");
        eh_stream_printf(stream, "  %s <port>                    - Listen on any address\r\n", command_info->command);
        eh_stream_printf(stream, "  %s <port> <bind_addr>        - Listen on specific address\r\n", command_info->command);
        eh_stream_printf(stream, "  %s <port> <bind_addr> <netdev> - Listen on specific address and netdev\r\n", command_info->command);
        eh_stream_printf(stream, "  Use 'any' for bind_addr or netdev to use default behavior\r\n");
    }
    
error:
    if (ctx != NULL) {
        tcp_echo_context_clean(cmd_context);
    }
    eh_stream_finish(stream);
    ehshell_command_finish(cmd_context);
}

void ehtools_tcp_echo_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event) {
    if (ehshell_event & (EHSHELL_EVENT_SIGINT_REQUEST_QUIT | EHSHELL_EVENT_SHELL_EXIT)) {
        tcp_echo_context_clean(cmd_context);
        ehshell_command_finish(cmd_context);
    }
}