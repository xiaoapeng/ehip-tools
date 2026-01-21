/**
 * @file cmd_ping.c
 * @brief Ping command.
 * @author simon.xiaoapeng (simon.xiaoapeng@gmail.com)
 * @date 2025-11-27
 * 
 * @copyright Copyright (c) 2025  simon.xiaoapeng@gmail.com
 * 
 */
#include <string.h>
#include <eh_error.h>
#include <eh_formatio.h>
#include <eh_mem.h>
#include <eh_comp_timer.h>
#include <ehip_core.h>
#include <ehip-ipv4/ip.h>
#include <ehip-ipv4/ping.h>
#include <ehip_error.h>
#include <ehip-protocol/dns.h>
#include <ehshell.h>

#define PING_DEFAULT_DATA_LEN 64

struct ping_context{
    uint32_t  flags;            /* 必须放在第一个字段 */
    ping_pcb_t ping_pcb;
    eh_signal_slot_t slot_1s_timer;
};

struct ping_dns_context{
    uint32_t    flags;          /* 必须放在第一个字段 */
    int         dns_desc;
    eh_signal_slot_t slot_dns_table_changed;
};

#define  ping_dns_context_get_domain(ctx) ((char*)((struct ping_dns_context *)(ctx)+1))

static void ping_context_clean(ehshell_cmd_context_t *cmd_context){
    uint32_t *flags = (uint32_t *)ehshell_command_get_user_data(cmd_context);
    if(*flags == 0){
        /* ping阶段 */
        struct ping_context *ctx = (struct ping_context *)ehshell_command_get_user_data(cmd_context);
        ping_pcb_t ping_pcb = ctx->ping_pcb;
        eh_signal_slot_disconnect(&signal_eh_comp_timer_1s, &ctx->slot_1s_timer);
        eh_free(ctx);
        ehip_ping_delete(ping_pcb);
    }else{
        /* DNS阶段 */
        struct ping_dns_context *ctx = (struct ping_dns_context *)ehshell_command_get_user_data(cmd_context);
        eh_signal_slot_disconnect(&signal_dns_table_changed, &ctx->slot_dns_table_changed);
        eh_free(ctx);
    }

}


static void ping_error_callback(ping_pcb_t pcb, ipv4_addr_t addr, uint16_t seq, int erron){
    ehshell_cmd_context_t *cmd_context = (ehshell_cmd_context_t *)ehip_ping_get_userdata(pcb);
    // struct ping_context *ctx = (struct ping_context *)ehshell_command_get_user_data(cmd_context);
    struct stream_base *stream = ehshell_command_stream(cmd_context);
    if(erron == EHIP_RET_REDIRECTED) 
        return ;
    if(erron == EH_RET_TIMEOUT){
        /* 超时 */
        eh_stream_printf(stream, "ping timeout. addr: " IPV4_FORMATIO ", seq: %d\r\n", 
            ipv4_formatio(addr), seq);
        return ;
    }
    eh_stream_printf(stream, "ping error. addr: " IPV4_FORMATIO ", seq: %d, erron: %d\r\n", 
        ipv4_formatio(addr), seq, erron);
    ping_context_clean(cmd_context);
    ehshell_command_finish(cmd_context);
}
static void ping_response_callback(ping_pcb_t pcb, ipv4_addr_t addr, uint16_t seq, uint8_t ttl, eh_clock_t time){
    ehshell_cmd_context_t *cmd_context = (ehshell_cmd_context_t *)ehip_ping_get_userdata(pcb);
    struct stream_base *stream = ehshell_command_stream(cmd_context);
    eh_stream_printf(stream, "ping response. addr: " IPV4_FORMATIO ", seq: %d, ttl: %d, time: %luus\r\n", 
        ipv4_formatio(addr), seq, ttl, eh_clock_to_usec(time));
}


static void slot_functhion_1s_timer(eh_event_t *e, void *slot_param){
    (void) e;
    int ret;
    ehshell_cmd_context_t *cmd_context = (ehshell_cmd_context_t *)slot_param;
    struct ping_context *ctx = (struct ping_context *)ehshell_command_get_user_data(cmd_context);
    struct stream_base *stream = ehshell_command_stream(cmd_context);
    if(ehip_ping_has_active_request(ctx->ping_pcb)){
        return ;
    }
    ret = ehip_ping_request(ctx->ping_pcb, PING_DEFAULT_DATA_LEN);
    if(ret < 0){
        eh_stream_printf(stream, "ehip_ping_request failed. Error code: %d\r\n", ret);
        ping_context_clean(cmd_context);
        ehshell_command_finish(cmd_context);
        return ;
    }
}

static int ehtools_ping_start(ehshell_cmd_context_t *cmd_context, ipv4_addr_t ipv4_addr){
    struct ping_context *ctx;
    ping_pcb_t ping_pcb;
    int ret;
    struct stream_base *stream = ehshell_command_stream(cmd_context);
    ping_pcb = ehip_ping_any_new(ipv4_addr);
    if(eh_ptr_to_error(ping_pcb) < 0){
        ret = eh_ptr_to_error(ping_pcb);
        eh_stream_printf(stream, "ehip_ping_any_new failed. Error code: %d\r\n", ret);
        goto ehip_ping_any_new_error;
    }
    ctx = eh_malloc(sizeof(struct ping_context));
    if(ctx == NULL){
        ret = EH_RET_MALLOC_ERROR;
        eh_stream_printf(stream, "ehtools_ping_start malloc ctx failed. \r\n");
        goto eh_malloc_error;
    }
    ctx->ping_pcb = ping_pcb;
    ehshell_command_set_user_data(cmd_context, ctx);
    ehip_ping_set_userdata(ping_pcb, cmd_context);
    ehip_ping_set_error_callback(ping_pcb, ping_error_callback);
    ehip_ping_set_response_callback(ping_pcb, ping_response_callback);
    ehip_ping_set_timeout(ping_pcb, 100); // 100 * 100ms = 10s
    ctx->flags = 0;
    eh_signal_slot_init(&ctx->slot_1s_timer, slot_functhion_1s_timer, cmd_context);
    ret = eh_signal_slot_connect_to_main(&signal_eh_comp_timer_1s, &ctx->slot_1s_timer);
    if(ret != 0){
        eh_stream_printf(stream, "The 1s timer signal slot connect failed. Error code: %d\r\n", ret);
        goto eh_signal_slot_connect_to_main_error;
    }
    return 0;
eh_signal_slot_connect_to_main_error:
    eh_free(ctx);
eh_malloc_error:
    ehip_ping_delete(ping_pcb);
ehip_ping_any_new_error:
    eh_stream_finish(stream);
    return ret;
}

static void slot_functhion_dns_table_changed(eh_event_t *e, void *slot_param){
    (void)  e;
    ehshell_cmd_context_t *cmd_context = (ehshell_cmd_context_t *)slot_param;
    struct ping_dns_context *ctx = (struct ping_dns_context *)ehshell_command_get_user_data(cmd_context);
    struct dns_entry* entry = ehip_dns_find_entry(ctx->dns_desc, ping_dns_context_get_domain(ctx), EHIP_DNS_TYPE_A);
    struct stream_base *stream = ehshell_command_stream(cmd_context);
    int ret;
    ret = eh_ptr_to_error(entry);
    switch (ret) {
        case 0:{
            /* 清理旧的DNS查询ping上下文 */
            ping_context_clean(cmd_context);
            ret = ehtools_ping_start(cmd_context, entry->rr.a.ip[0]);
            if(ret == 0)
                return ;
            eh_stream_printf(stream, "ehtools_ping_start failed. Error code: %d\r\n", ret);
            goto finish;
        }
        case EH_RET_AGAIN:
            return ;
        case EH_RET_FAULT:
            eh_stream_printf(stream, "The dns query request failed.\r\n");
            break;
        default:
            eh_stream_printf(ehshell_command_stream(cmd_context), "The dns query request failed. Error code: %d\r\n", ret);
            break;
    }
    ping_context_clean(cmd_context);
finish:
    ehshell_command_finish(cmd_context);
}

static int ehtools_ping_dns_start(ehshell_cmd_context_t *cmd_context, const char *domain){
    int ret, dns_desc;
    struct stream_base *stream = ehshell_command_stream(cmd_context);
    struct dns_entry* entry = NULL;
    size_t domain_len;
    struct ping_dns_context *ctx = NULL;
    dns_desc = ehip_dns_query_async(domain, -1, EHIP_DNS_TYPE_A);
    if(dns_desc < 0){
        eh_stream_printf(stream, "ehip_dns_query_async failed. Error code: %d\r\n", dns_desc);
        return dns_desc;
    }
    entry = ehip_dns_find_entry(dns_desc, domain, EHIP_DNS_TYPE_A);
    if(eh_ptr_to_error(entry) == 0){
        /* 有效条目 */
        eh_stream_printf(stream, "dns entry found. addr: " IPV4_FORMATIO "\r\n", 
            ipv4_formatio(entry->rr.a.ip[0]));
        return ehtools_ping_start(cmd_context, entry->rr.a.ip[0]);
    }
    /* 未找到有效条目，等待dns查询 */
    domain_len = strlen(domain);
    ctx = eh_malloc(sizeof(struct ping_dns_context) + domain_len + 1);
    if(ctx == NULL){
        eh_stream_printf(stream, "ehtools_ping_dns_start malloc ping dns context failed. \r\n");
        return EH_RET_MALLOC_ERROR;
    }
    ctx->flags = 1;
    ctx->dns_desc = dns_desc;
    strncpy(ping_dns_context_get_domain(ctx), domain, domain_len);
    ping_dns_context_get_domain(ctx)[domain_len] = '\0';
    ehshell_command_set_user_data(cmd_context, ctx);
    eh_signal_slot_init(&ctx->slot_dns_table_changed, slot_functhion_dns_table_changed, cmd_context);
    ret = eh_signal_slot_connect_to_main(&signal_dns_table_changed, &ctx->slot_dns_table_changed);
    if(ret != 0){
        eh_stream_printf(stream, "The dns table signal slot connect failed. Error code: %d\r\n", ret);
        goto error;
    }
    return 0;
error:
    eh_free(ctx);
    return ret;
}


void ehtools_ping(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]){
    struct stream_base *stream = ehshell_command_stream(cmd_context);
    int ret;
    if(argc != 2)
        goto help;
    ipv4_addr_t ipv4_addr;
    
    if(ipv4_string_to_addr(argv[1], &ipv4_addr) != EH_RET_OK){
        /* TODO: 尝试dns解析 */
        ret = ehtools_ping_dns_start(cmd_context, argv[1]);
        if(ret == 0)
            return ;
        eh_stream_printf(stream, "ehtools_ping_dns_start failed. Error code: %d\r\n", ret);
        goto finish;
    }

    ret = ehtools_ping_start(cmd_context, ipv4_addr);
    if(ret != 0){
        eh_stream_printf(stream, "ehtools_ping_start failed. Error code: %d\r\n", ret);
        goto finish;
    }
    return ;
help:
    eh_stream_printf(stream, "Usage: %s\r\n", ehshell_command_usage(cmd_context));
finish:
    ehshell_command_finish(cmd_context);
}
void ehtools_ping_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event){
    if(ehshell_event & (EHSHELL_EVENT_SIGINT_REQUEST_QUIT | EHSHELL_EVENT_SHELL_EXIT)){
        ping_context_clean(cmd_context);
        ehshell_command_finish(cmd_context);
    }
}