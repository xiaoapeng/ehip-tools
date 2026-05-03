/**
 * @file cmd_traceroute.c
 * @brief Traceroute command
 * @author simon.xiaoapeng (simon.xiaoapeng@gmail.com)
 * @date 2026-05-01
 *
 * @copyright Copyright (c) 2026  simon.xiaoapeng@gmail.com
 *
 */
#include <string.h>
#include <stdlib.h>
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

#define TRACEROUTE_DEFAULT_MAX_TTL   30
#define TRACEROUTE_DEFAULT_MAX_RETRY 3
#define TRACEROUTE_DEFAULT_TIMEOUT   30  /* 30 * 100ms = 3s */
#define TRACEROUTE_DATA_LEN          40

struct traceroute_context {
    uint32_t            flags;          /* 必须放在第一个字段 */
    ping_pcb_t          ping_pcb;
    eh_signal_slot_t    slot_1s_timer;
    struct stream_base  *stream;
    ehshell_cmd_context_t *cmd_context;
    ipv4_addr_t         hops[TRACEROUTE_DEFAULT_MAX_TTL];
    uint8_t             current_ttl;
    uint8_t             retry_count;
    uint8_t             max_ttl;
    uint8_t             max_retry;
    uint16_t            seq_base;
    bool                seq_base_set;
};

struct traceroute_dns_context {
    uint32_t    flags;          /* 必须放在第一个字段 */
    int         dns_desc;
    eh_signal_slot_t slot_dns_table_changed;
    uint8_t     max_ttl;
    uint8_t     max_retry;
};

#define traceroute_dns_context_get_domain(ctx) ((char*)((struct traceroute_dns_context *)(ctx)+1))

static void traceroute_cleanup(ehshell_cmd_context_t *cmd_context){
    uint32_t *flags = (uint32_t *)ehshell_command_get_userdata(cmd_context);
    if(*flags == 0){
        struct traceroute_context *ctx = (struct traceroute_context *)ehshell_command_get_userdata(cmd_context);
        eh_signal_slot_disconnect(&signal_eh_comp_timer_1s, &ctx->slot_1s_timer);
        ehip_ping_delete(ctx->ping_pcb);
        eh_free(ctx);
    } else {
        struct traceroute_dns_context *ctx = (struct traceroute_dns_context *)ehshell_command_get_userdata(cmd_context);
        eh_signal_slot_disconnect(&signal_dns_table_changed, &ctx->slot_dns_table_changed);
        eh_free(ctx);
    }
}

static void traceroute_error_callback(ping_pcb_t pcb, ipv4_addr_t addr, uint16_t seq, int erron){
    ehshell_cmd_context_t *cmd_context = (ehshell_cmd_context_t *)ehip_ping_get_userdata(pcb);
    struct traceroute_context *ctx = (struct traceroute_context *)ehshell_command_get_userdata(cmd_context);
    struct stream_base *stream = ctx->stream;

    if(erron != EHIP_RET_TTL_EXPIRED)
        return;
    if(ctx->current_ttl == 0 || ctx->current_ttl > ctx->max_ttl)
        return;

    /* 记录跳点 */
    uint8_t idx = ctx->current_ttl - 1;
    if(!ctx->hops[idx])
        ctx->hops[idx] = addr;

    /* 打印该跳点 */
    eh_stream_printf(stream, "%3u  " IPV4_FORMATIO "\r\n", ctx->current_ttl, ipv4_formatio(addr));

    /* 前进到下一跳 */
    ctx->current_ttl++;
    ctx->retry_count = 0;
    ctx->seq_base_set = false;

    if(ctx->current_ttl > ctx->max_ttl){
        eh_stream_printf(stream, "traceroute complete.\r\n");
        traceroute_cleanup(cmd_context);
        ehshell_command_finish(cmd_context);
    }
}

static void traceroute_response_callback(ping_pcb_t pcb, ipv4_addr_t addr, uint16_t seq, uint8_t ttl, eh_clock_t time_ms){
    ehshell_cmd_context_t *cmd_context = (ehshell_cmd_context_t *)ehip_ping_get_userdata(pcb);
    struct traceroute_context *ctx = (struct traceroute_context *)ehshell_command_get_userdata(cmd_context);
    struct stream_base *stream = ctx->stream;

    eh_stream_printf(stream, "%3u  " IPV4_FORMATIO "  reached! ttl=%u time=%luus\r\n",
        ctx->current_ttl, ipv4_formatio(addr), ttl, eh_clock_to_usec(time_ms));

    traceroute_cleanup(cmd_context);
    ehshell_command_finish(cmd_context);
}

static void slot_function_1s_timer(eh_event_t *e, void *slot_param){
    (void)e;
    int ret;
    ehshell_cmd_context_t *cmd_context = (ehshell_cmd_context_t *)slot_param;
    struct traceroute_context *ctx = (struct traceroute_context *)ehshell_command_get_userdata(cmd_context);

    if(ehip_ping_has_active_request(ctx->ping_pcb))
        return;

    /* 超过当前跳最大重试次数，前进到下一跳 */
    if(ctx->retry_count >= ctx->max_retry){
        struct stream_base *stream = ctx->stream;
        eh_stream_printf(stream, "%3u  *\r\n", ctx->current_ttl);
        ctx->current_ttl++;
        ctx->retry_count = 0;
        ctx->seq_base_set = false;

        if(ctx->current_ttl > ctx->max_ttl){
            eh_stream_printf(stream, "traceroute complete.\r\n");
            traceroute_cleanup(cmd_context);
            ehshell_command_finish(cmd_context);
            return;
        }
    }

    /* 设置 TTL */
    ehip_ping_set_ttl(ctx->ping_pcb, ctx->current_ttl);

    ret = ehip_ping_request(ctx->ping_pcb, TRACEROUTE_DATA_LEN);
    if(ret < 0){
        struct stream_base *stream = ctx->stream;
        eh_stream_printf(stream, "ehip_ping_request failed. Error code: %d\r\n", ret);
        traceroute_cleanup(cmd_context);
        ehshell_command_finish(cmd_context);
        return;
    }

    if(!ctx->seq_base_set){
        ctx->seq_base = (uint16_t)ret;
        ctx->seq_base_set = true;
    }
    ctx->retry_count++;
}

static int traceroute_start(ehshell_cmd_context_t *cmd_context, ipv4_addr_t dst, uint8_t max_ttl, uint8_t max_retry){
    int ret;
    struct stream_base *stream = ehshell_command_stream(cmd_context);
    struct traceroute_context *ctx;

    ping_pcb_t pcb = ehip_ping_any_new(dst);
    if(eh_ptr_to_error(pcb) < 0){
        ret = eh_ptr_to_error(pcb);
        eh_stream_printf(stream, "ehip_ping_any_new failed. Error code: %d\r\n", ret);
        return ret;
    }

    ctx = eh_malloc(sizeof(struct traceroute_context));
    if(ctx == NULL){
        ehip_ping_delete(pcb);
        eh_stream_printf(stream, "malloc failed.\r\n");
        return EH_RET_MALLOC_ERROR;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->flags = 0;
    ctx->ping_pcb = pcb;
    ctx->stream = stream;
    ctx->cmd_context = cmd_context;
    ctx->current_ttl = 1;
    ctx->retry_count = 0;
    ctx->max_ttl = max_ttl;
    ctx->max_retry = max_retry;
    ctx->seq_base_set = false;

    ehshell_command_set_userdata(cmd_context, ctx);
    ehip_ping_set_userdata(pcb, cmd_context);
    ehip_ping_set_error_callback(pcb, traceroute_error_callback);
    ehip_ping_set_response_callback(pcb, traceroute_response_callback);
    ehip_ping_set_timeout(pcb, TRACEROUTE_DEFAULT_TIMEOUT);

    eh_signal_slot_init(&ctx->slot_1s_timer, slot_function_1s_timer, cmd_context);
    ret = eh_signal_slot_connect_to_main(&signal_eh_comp_timer_1s, &ctx->slot_1s_timer);
    if(ret != 0){
        eh_stream_printf(stream, "timer signal slot connect failed. Error code: %d\r\n", ret);
        eh_free(ctx);
        ehip_ping_delete(pcb);
        return ret;
    }

    eh_stream_printf(stream, "traceroute to " IPV4_FORMATIO ", %u hops max\r\n",
        ipv4_formatio(dst), max_ttl);
    return 0;
}

static void slot_function_dns_table_changed(eh_event_t *e, void *slot_param){
    (void)e;
    ehshell_cmd_context_t *cmd_context = (ehshell_cmd_context_t *)slot_param;
    struct traceroute_dns_context *ctx = (struct traceroute_dns_context *)ehshell_command_get_userdata(cmd_context);
    struct dns_entry *entry = ehip_dns_find_entry(ctx->dns_desc, traceroute_dns_context_get_domain(ctx), EHIP_DNS_TYPE_A);
    struct stream_base *stream = ehshell_command_stream(cmd_context);
    int ret;

    ret = eh_ptr_to_error(entry);
    switch (ret) {
        case 0:
            traceroute_cleanup(cmd_context);
            ret = traceroute_start(cmd_context, entry->rr.a.ip[0], ctx->max_ttl, ctx->max_retry);
            if(ret == 0)
                return;
            eh_stream_printf(stream, "traceroute_start failed. Error code: %d\r\n", ret);
            goto finish;
        case EH_RET_AGAIN:
            return;
        case EH_RET_FAULT:
            eh_stream_printf(stream, "DNS query failed.\r\n");
            break;
        default:
            eh_stream_printf(stream, "DNS query failed. Error code: %d\r\n", ret);
            break;
    }
    traceroute_cleanup(cmd_context);
finish:
    ehshell_command_finish(cmd_context);
}

static int traceroute_dns_start(ehshell_cmd_context_t *cmd_context, const char *domain, uint8_t max_ttl, uint8_t max_retry){
    int ret, dns_desc;
    struct stream_base *stream = ehshell_command_stream(cmd_context);
    struct dns_entry *entry = NULL;
    size_t domain_len;
    struct traceroute_dns_context *ctx;

    dns_desc = ehip_dns_query_async(domain, -1, EHIP_DNS_TYPE_A);
    if(dns_desc < 0){
        eh_stream_printf(stream, "ehip_dns_query_async failed. Error code: %d\r\n", dns_desc);
        return dns_desc;
    }
    entry = ehip_dns_find_entry(dns_desc, domain, EHIP_DNS_TYPE_A);
    if(eh_ptr_to_error(entry) == 0){
        eh_stream_printf(stream, "DNS resolved: " IPV4_FORMATIO "\r\n", ipv4_formatio(entry->rr.a.ip[0]));
        return traceroute_start(cmd_context, entry->rr.a.ip[0], max_ttl, max_retry);
    }

    domain_len = strlen(domain);
    ctx = eh_malloc(sizeof(struct traceroute_dns_context) + domain_len + 1);
    if(ctx == NULL){
        eh_stream_printf(stream, "malloc failed.\r\n");
        return EH_RET_MALLOC_ERROR;
    }
    ctx->flags = 1;
    ctx->dns_desc = dns_desc;
    ctx->max_ttl = max_ttl;
    ctx->max_retry = max_retry;
    strncpy(traceroute_dns_context_get_domain(ctx), domain, domain_len);
    traceroute_dns_context_get_domain(ctx)[domain_len] = '\0';
    ehshell_command_set_userdata(cmd_context, ctx);
    eh_signal_slot_init(&ctx->slot_dns_table_changed, slot_function_dns_table_changed, cmd_context);
    ret = eh_signal_slot_connect_to_main(&signal_dns_table_changed, &ctx->slot_dns_table_changed);
    if(ret != 0){
        eh_stream_printf(stream, "dns signal slot connect failed. Error code: %d\r\n", ret);
        eh_free(ctx);
        return ret;
    }
    return 0;
}

void ehtools_traceroute(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]){
    struct stream_base *stream = ehshell_command_stream(cmd_context);
    int ret;

    if(argc < 2)
        goto help;

    ipv4_addr_t dst;
    uint8_t max_ttl = TRACEROUTE_DEFAULT_MAX_TTL;
    uint8_t max_retry = TRACEROUTE_DEFAULT_MAX_RETRY;

    if(argc >= 3){
        int m = atoi(argv[2]);
        if(m > 0 && m <= 255)
            max_ttl = (uint8_t)m;
    }
    if(argc >= 4){
        int r = atoi(argv[3]);
        if(r > 0 && r <= 255)
            max_retry = (uint8_t)r;
    }

    if(ipv4_string_to_addr(argv[1], &dst) != EH_RET_OK){
        ret = traceroute_dns_start(cmd_context, argv[1], max_ttl, max_retry);
        if(ret == 0)
            return;
        eh_stream_printf(stream, "traceroute_dns_start failed. Error code: %d\r\n", ret);
        goto finish;
    }

    ret = traceroute_start(cmd_context, dst, max_ttl, max_retry);
    if(ret == 0)
        return;
    eh_stream_printf(stream, "traceroute_start failed. Error code: %d\r\n", ret);
    goto finish;

help:
    eh_stream_printf(stream, "Usage: traceroute <ip|domain> [max_ttl] [max_retry]\r\n");
finish:
    ehshell_command_finish(cmd_context);
}

void ehtools_traceroute_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event){
    if(ehshell_event & (EHSHELL_EVENT_SIGINT_REQUEST_QUIT | EHSHELL_EVENT_SHELL_EXIT)){
        traceroute_cleanup(cmd_context);
        ehshell_command_finish(cmd_context);
    }
}
