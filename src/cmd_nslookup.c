/**
 * @file cmd_nslookup.c
 * @brief Nslookup command.
 * @author simon.xiaoapeng (simon.xiaoapeng@gmail.com)
 * @date 2025-12-05
 * 
 * @copyright Copyright (c) 2025  simon.xiaoapeng@gmail.com
 * 
 */

#include <string.h>

#include <eh.h>
#include <eh_swab.h>
#include <eh_debug.h>
#include <eh_module.h>
#include <eh_error.h>
#include <eh_formatio.h>
#include <eh_debug.h>
#include <eh_mem.h>
#include <ehshell.h>
#include <ehip-protocol/dns.h>

struct nslookup_context{
    char       domain[EHIP_DNS_CNAME_RR_DOMAIN_LEN_MAX + 1];
    eh_signal_slot_t slot_dns_table_changed;
    int              dns_desc;
    uint32_t         type;
};


static void nslookup_context_clean(ehshell_cmd_context_t *cmd_context){
    struct nslookup_context *ctx = (struct nslookup_context *)ehshell_command_get_user_data(cmd_context);
    eh_signal_slot_disconnect(&signal_dns_table_changed, &ctx->slot_dns_table_changed);
    eh_free(ctx);
}

static void print_dns_entry(struct stream_base *stream, struct dns_entry* entry, uint32_t type, const char *domain){
    eh_stream_printf(stream, "%s: \r\n", domain);
    if(type == EHIP_DNS_TYPE_A){
        for(int i=0; i < (int)EHIP_DNS_A_RR_IP_COUNT && entry->rr.a.ip[i] != 0; i++){
            eh_stream_printf(stream, "\t"IPV4_FORMATIO "\r\n", ipv4_formatio(entry->rr.a.ip[i]));
        }
    }else if(type == EHIP_DNS_TYPE_CNAME){
        eh_stream_printf(stream, "\t%s\r\n", entry->rr.cname.domain);
    }
}

static void slot_functhion_dns_table_changed(eh_event_t *e, void *slot_param){
    (void)  e;
    ehshell_cmd_context_t *cmd_context = (ehshell_cmd_context_t *)slot_param;
    struct nslookup_context *ctx = (struct nslookup_context *)ehshell_command_get_user_data(cmd_context);
    struct dns_entry* entry = ehip_dns_find_entry(ctx->dns_desc, ctx->domain, ctx->type);
    struct stream_base *stream = ehshell_command_stream(cmd_context);
    int ret;
    ret = eh_ptr_to_error(entry);
    switch (ret) {
        case 0:
            print_dns_entry(stream, entry, ctx->type, ctx->domain);
            break;
        case EH_RET_AGAIN:
            return ;
        case EH_RET_FAULT:
            eh_stream_printf(stream, "The dns query request failed.\r\n");
            break;
        default:
            eh_stream_printf(ehshell_command_stream(cmd_context), "The dns query request failed. Error code: %d\r\n", ret);
            break;
    }
    nslookup_context_clean(cmd_context);
    ehshell_command_finish(cmd_context);
}

void ehtools_nslookup(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]){
    struct nslookup_context *ctx = NULL;
    uint32_t type = EHIP_DNS_TYPE_A;
    struct dns_entry* entry = NULL;
    struct stream_base *stream = ehshell_command_stream(cmd_context);
    int dns_desc, ret;
    if(argc == 3){
        if(strcmp(argv[2], "A") == 0){
            type = EHIP_DNS_TYPE_A;
        }else if(strcmp(argv[2], "CNAME") == 0){
            type = EHIP_DNS_TYPE_CNAME;
        }else{
            goto help;
        }
    }else if(argc != 2){
        goto help;
    }

    dns_desc = ehip_dns_query_async(argv[1], -1, type);
    if(dns_desc < 0){
        eh_stream_printf(stream, "The dns query request failed. Error code: %d\r\n", dns_desc);
        goto error;
    }
    entry = ehip_dns_find_entry(dns_desc, argv[1], type);
    if(eh_ptr_to_error(entry) == 0){
        /* 说明缓存有效 */
        print_dns_entry(stream, entry, type, argv[1]);
        goto finish;
    }

    ctx = eh_malloc(sizeof(struct nslookup_context));
    if(ctx == NULL){
        eh_stream_printf(stream, "Memory allocation failed\r\n");
        goto finish;
    }
    strncpy(ctx->domain, argv[1], EHIP_DNS_CNAME_RR_DOMAIN_LEN_MAX + 1);
    ctx->type = type;
    ctx->dns_desc = dns_desc;
    ehshell_command_set_user_data(cmd_context, ctx);
    eh_signal_slot_init(&ctx->slot_dns_table_changed, slot_functhion_dns_table_changed, cmd_context);
    ret = eh_signal_slot_connect_to_main(&signal_dns_table_changed, &ctx->slot_dns_table_changed);
    if(ret != 0){
        eh_stream_printf(stream, "The dns table changed signal slot connect failed. Error code: %d\r\n", ret);
        goto error;
    }
    return ;
help:
    eh_stream_printf(stream, "Usage: %s\r\n", ehshell_command_usage(cmd_context));
error:
    if(ctx)
        eh_free(ctx);
finish:
    ehshell_command_finish(cmd_context);
    return;
}

void ehtools_nslookup_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event){
    if(ehshell_event & (EHSHELL_EVENT_SIGINT_REQUEST_QUIT | EHSHELL_EVENT_SHELL_EXIT)){
        nslookup_context_clean(cmd_context);
        ehshell_command_finish(cmd_context);
    }
}