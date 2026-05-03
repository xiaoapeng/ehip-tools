/**
 * @file cmd_route.c
 * @brief 路由命令
 * @author simon.xiaoapeng (simon.xiaoapeng@gmail.com)
 * @date 2026-02-09
 *
 * @copyright Copyright (c) 2026  simon.xiaoapeng@gmail.com
 *
 */
#include <string.h>
#include <stdlib.h>
#include <eh_error.h>
#include <eh_formatio.h>
#include <eh_mem.h>
#include <ehip_core.h>
#include <ehip_netdev_tool.h>
#include <ehip_netdev_trait.h>
#include <ehip-ipv4/ip.h>
#include <ehip-ipv4/route.h>
#include <ehshell.h>
#include <ehshell_config.h>

/* route add <dst> gw <gw> dev <dev> metric <m> = 9 args */
eh_static_assert(EHSHELL_CONFIG_ARGC_MAX >= 9,
    "EHSHELL_CONFIG_ARGC_MAX is too small for route command, "
    "please set CONFIG_PACKAGE_EHSHELL_ARGC_MAX >= 9 via menuconfig (ehshell package).");

static const char * route_type_to_string(enum route_table_type type){
    switch (type) {
        case ROUTE_TABLE_UNREACHABLE: return "unreachable";
        case ROUTE_TABLE_MULTICAST:   return "multicast";
        case ROUTE_TABLE_BROADCAST:   return "broadcast";
        case ROUTE_TABLE_UNICAST:     return "unicast";
        case ROUTE_TABLE_LOCAL:       return "local";
        case ROUTE_TABLE_LOCAL_SELF:  return "local-self";
        default:                      return "unknown";
    }
}

static void show_route_table(struct stream_base *stream){
    struct route_info *route_array = NULL;
    int count = ipv4_route_to_array(&route_array);
    if(count < 0){
        eh_stream_printf(stream, "Error: failed to get route table (code: %d)\n", count);
        return;
    }
    if(count == 0){
        eh_stream_printf(stream, "Route table is empty.\n");
        return;
    }

    eh_stream_printf(stream, "%-18s %-18s %-6s %-10s %s\n",
        "Destination", "Gateway", "Metric", "Mask", "Dev");
    eh_stream_printf(stream, "--------------------------------------------------------------\n");

    for(int i = 0; i < count; i++){
        struct route_info *r = &route_array[i];
        ehip_netdev_info_t info;
        const char *dev_name = "?";

        if(r->netdev && ehip_netdev_tool_get_info(r->netdev, &info) == EH_RET_OK){
            dev_name = info.name;
        }

        /* 格式化目标地址/掩码 */
        if(r->mask_len == 0 && r->dst_addr == IPV4_ADDR_ANY){
            eh_stream_printf(stream, "%-18s ", "default");
        } else {
            eh_stream_printf(stream, IPV4_FORMATIO "/%-10u ", ipv4_formatio(r->dst_addr), r->mask_len);
        }

        /* 格式化网关 */
        if(r->gateway == IPV4_ADDR_ANY){
            eh_stream_printf(stream, "%-18s ", "*");
        } else {
            eh_stream_printf(stream, IPV4_FORMATIO "          ", ipv4_formatio(r->gateway));
        }

        eh_stream_printf(stream, "%-6u %-10s %s\n", r->metric, route_type_to_string(ROUTE_TABLE_UNICAST), dev_name);
    }

    eh_free(route_array);
}

/**
 * @brief 解析 "<ip>/<mask_len>" 格式的目标地址
 * @return 0 成功, <0 失败
 */
static int parse_dst_prefix(const char *str, ipv4_addr_t *dst, uint8_t *mask_len){
    const char *slash = strchr(str, '/');
    if(slash == NULL){
        /* 无掩码，当作主机路由 /32 */
        if(ipv4_string_to_addr(str, dst) != EH_RET_OK)
            return -1;
        *mask_len = 32;
        return 0;
    }

    char ip_buf[16];
    size_t ip_len = (size_t)(slash - str);
    if(ip_len >= sizeof(ip_buf))
        return -1;

    strncpy(ip_buf, str, ip_len);
    ip_buf[ip_len] = '\0';

    if(ipv4_string_to_addr(ip_buf, dst) != EH_RET_OK)
        return -1;

    int len = atoi(slash + 1);
    if(len < 0 || len > 32)
        return -1;

    *mask_len = (uint8_t)len;
    return 0;
}

static int route_add(struct stream_base *stream, int argc, const char *argv[]){
    if(argc < 3){
        eh_stream_printf(stream, "Usage: route add <dst>/<mask> [gw <gateway>] [dev <netdev>] [metric <metric>]\n");
        return -1;
    }

    struct route_info route;
    memset(&route, 0, sizeof(route));
    route.gateway = IPV4_ADDR_ANY;
    route.src_addr = IPV4_ADDR_ANY;
    route.metric = 0;
    route.netdev = NULL;

    if(parse_dst_prefix(argv[2], &route.dst_addr, &route.mask_len) < 0){
        eh_stream_printf(stream, "Error: invalid destination format '%s'\n", argv[2]);
        return -1;
    }

    for(int i = 3; i < argc; i++){
        if(strcmp(argv[i], "gw") == 0){
            if(i + 1 >= argc){
                eh_stream_printf(stream, "Error: gw requires an argument\n");
                return -1;
            }
            if(ipv4_string_to_addr(argv[i + 1], &route.gateway) != EH_RET_OK){
                eh_stream_printf(stream, "Error: invalid gateway address '%s'\n", argv[i + 1]);
                return -1;
            }
            i++;
        } else if(strcmp(argv[i], "dev") == 0){
            if(i + 1 >= argc){
                eh_stream_printf(stream, "Error: dev requires an argument\n");
                return -1;
            }
            route.netdev = ehip_netdev_tool_find(argv[i + 1]);
            if(route.netdev == NULL){
                eh_stream_printf(stream, "Error: interface '%s' not found\n", argv[i + 1]);
                return -1;
            }
            i++;
        } else if(strcmp(argv[i], "metric") == 0){
            if(i + 1 >= argc){
                eh_stream_printf(stream, "Error: metric requires an argument\n");
                return -1;
            }
            int m = atoi(argv[i + 1]);
            if(m < 0 || m > UINT16_MAX){
                eh_stream_printf(stream, "Error: metric out of range (0-%d)\n", UINT16_MAX);
                return -1;
            }
            route.metric = (uint16_t)m;
            i++;
        } else {
            eh_stream_printf(stream, "Error: unknown option '%s'\n", argv[i]);
            return -1;
        }
    }

    /* 网关路由必须指定设备 */
    if(route.gateway != IPV4_ADDR_ANY && route.netdev == NULL){
        /* 尝试通过路由查找自动选择设备 */
        ipv4_addr_t best_src = IPV4_ADDR_ANY;
        struct route_info found;
        enum route_table_type type = ipv4_route_lookup(route.gateway, NULL, &found, &best_src);
        if(type == ROUTE_TABLE_UNICAST || type == ROUTE_TABLE_LOCAL || type == ROUTE_TABLE_LOCAL_SELF){
            route.netdev = found.netdev;
            route.src_addr = best_src;
        } else {
            eh_stream_printf(stream, "Error: cannot determine device for gateway " IPV4_FORMATIO "\n",
                ipv4_formatio(route.gateway));
            return -1;
        }
    }

    int ret = ipv4_route_add(&route);
    if(ret != EH_RET_OK){
        switch (ret) {
            case EH_RET_EXISTS:
                eh_stream_printf(stream, "Error: route already exists\n");
                break;
            case EH_RET_MALLOC_ERROR:
                eh_stream_printf(stream, "Error: out of memory\n");
                break;
            default:
                eh_stream_printf(stream, "Error: failed to add route (code: %d)\n", ret);
                break;
        }
        return -1;
    }

    return 0;
}

static int route_del(struct stream_base *stream, int argc, const char *argv[]){
    if(argc < 3){
        eh_stream_printf(stream, "Usage: route del <dst>/<mask> [gw <gateway>] [dev <netdev>] [metric <metric>]\n");
        return -1;
    }

    struct route_info route;
    memset(&route, 0, sizeof(route));
    route.gateway = IPV4_ADDR_ANY;
    route.src_addr = IPV4_ADDR_ANY;
    route.metric = 0;
    route.netdev = NULL;

    if(parse_dst_prefix(argv[2], &route.dst_addr, &route.mask_len) < 0){
        eh_stream_printf(stream, "Error: invalid destination format '%s'\n", argv[2]);
        return -1;
    }

    for(int i = 3; i < argc; i++){
        if(strcmp(argv[i], "gw") == 0){
            if(i + 1 >= argc){
                eh_stream_printf(stream, "Error: gw requires an argument\n");
                return -1;
            }
            if(ipv4_string_to_addr(argv[i + 1], &route.gateway) != EH_RET_OK){
                eh_stream_printf(stream, "Error: invalid gateway address '%s'\n", argv[i + 1]);
                return -1;
            }
            i++;
        } else if(strcmp(argv[i], "dev") == 0){
            if(i + 1 >= argc){
                eh_stream_printf(stream, "Error: dev requires an argument\n");
                return -1;
            }
            route.netdev = ehip_netdev_tool_find(argv[i + 1]);
            if(route.netdev == NULL){
                eh_stream_printf(stream, "Error: interface '%s' not found\n", argv[i + 1]);
                return -1;
            }
            i++;
        } else if(strcmp(argv[i], "metric") == 0){
            if(i + 1 >= argc){
                eh_stream_printf(stream, "Error: metric requires an argument\n");
                return -1;
            }
            int m = atoi(argv[i + 1]);
            if(m < 0 || m > UINT16_MAX){
                eh_stream_printf(stream, "Error: metric out of range (0-%d)\n", UINT16_MAX);
                return -1;
            }
            route.metric = (uint16_t)m;
            i++;
        } else {
            eh_stream_printf(stream, "Error: unknown option '%s'\n", argv[i]);
            return -1;
        }
    }

    int ret = ipv4_route_del(&route);
    if(ret != EH_RET_OK){
        eh_stream_printf(stream, "Error: route not found (code: %d)\n", ret);
        return -1;
    }

    return 0;
}

void ehtools_route(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]){
    struct stream_base *stream = ehshell_command_stream(cmd_context);

    if(argc == 1){
        show_route_table(stream);
        goto finish;
    }

    if(strcmp(argv[1], "add") == 0){
        if(route_add(stream, argc, argv) < 0)
            goto finish;
    } else if(strcmp(argv[1], "del") == 0 || strcmp(argv[1], "delete") == 0){
        if(route_del(stream, argc, argv) < 0)
            goto finish;
    } else {
        eh_stream_printf(stream, "Usage: route [add|del] [options]\n"
            "  route                                Show route table\n"
            "  route add <dst>/<mask> [gw <gw>] [dev <dev>] [metric <m>]\n"
            "  route del <dst>/<mask> [gw <gw>] [dev <dev>] [metric <m>]\n");
        goto finish;
    }

finish:
    eh_stream_finish(stream);
    ehshell_command_finish(cmd_context);
}

void ehtools_route_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event){
    if(ehshell_event & (EHSHELL_EVENT_SIGINT_REQUEST_QUIT | EHSHELL_EVENT_SHELL_EXIT)){
        ehshell_command_finish(cmd_context);
    }
}
