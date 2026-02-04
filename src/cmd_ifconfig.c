/**
 * @file cmd_ifconfig.c
 * @brief Network Interface Configuration Command Tool
 * @author simon.xiaoapeng (simon.xiaoapeng@gmail.com)
 * @date 2026-02-04
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

/**
 * @brief 显示网络接口信息
 * @param stream 输出流
 * @param netdev 网络设备
 */
static void show_interface_info(struct stream_base *stream, ehip_netdev_t *netdev) {
    ehip_netdev_info_t info;
    int ret = ehip_netdev_tool_get_info(netdev, &info);
    
    if (ret != EH_RET_OK) {
        eh_stream_printf(stream, "Error: failed to get interface info (code: %d)\n", ret);
        return;
    }
    
    // 显示接口基本信息
    eh_stream_printf(stream, "%s: ", info.name);
    
    // 显示状态标志
    if (info.status & EHIP_NETDEV_STATUS_UP) {
        eh_stream_printf(stream, "UP ");
    } else {
        eh_stream_printf(stream, "DOWN ");
    }
    
    if (info.status & EHIP_NETDEV_STATUS_LINK) {
        eh_stream_printf(stream, "LINK ");
    }
    
    eh_stream_printf(stream, "\n");
    
    // 显示MTU
    eh_stream_printf(stream, "        MTU: %d\n", info.mtu);
    
    // 根据设备类型显示特定信息
    switch (info.type) {
        case EHIP_NETDEV_TYPE_ETHERNET:
            // 显示MAC地址
            eh_stream_printf(stream, "        ether ");
            for (int i = 0; i < 6; i++) {
                eh_stream_printf(stream, "%02x", info.ethernet.hw_addr.addr[i]);
                if (i < 5) eh_stream_printf(stream, ":");
            }
            eh_stream_printf(stream, "\n");
            
            // 显示广播地址
            eh_stream_printf(stream, "        broadcast ");
            for (int i = 0; i < 6; i++) {
                eh_stream_printf(stream, "%02x", info.ethernet.broadcast_hw_addr.addr[i]);
                if (i < 5) eh_stream_printf(stream, ":");
            }
            eh_stream_printf(stream, "\n");
            break;
            
        case EHIP_NETDEV_TYPE_LOOPBACK:
            eh_stream_printf(stream,        "        loopback\n");
            break;
            
        case EHIP_NETDEV_TYPE_TUN:
            eh_stream_printf(stream,        "        tun\n");
            break;
            
        default:
            eh_stream_printf(stream,        "        unknown type: %d\n", info.type);
            break;
    }
    
    // 显示IPv4信息
    ipv4_netdev_info_t *ipv4_info = NULL;
    switch (info.type) {
        case EHIP_NETDEV_TYPE_ETHERNET:
            ipv4_info = &info.ethernet.ipv4_info;
            break;
        case EHIP_NETDEV_TYPE_LOOPBACK:
            ipv4_info = &info.loopback.ipv4_info;
            break;
        case EHIP_NETDEV_TYPE_TUN:
            ipv4_info = &info.tun.ipv4_info;
            break;
        default:
            break;
    }
    
    if (ipv4_info && ipv4_info->ipv4_addr_num > 0) {
        // 显示属性标志
        eh_stream_printf(stream, "        IPv4 attributes: ");
        if (ipv4_info->attr_flags & IPV4_ATTR_FLAG_ARP_SUPPORT) {
            eh_stream_printf(stream, "ARP ");
        }
        if (ipv4_info->attr_flags & IPV4_ATTR_FLAG_FORWARD_SUPPORT) {
            eh_stream_printf(stream, "FORWARD ");
        }
        if (ipv4_info->attr_flags & IPV4_ATTR_FLAG_LOOPBACK_SUPPORT) {
            eh_stream_printf(stream, "LOOPBACK ");
        }
        eh_stream_printf(stream, "\n");
        
        // 显示所有IP地址
        for (size_t i = 0; i < ipv4_info->ipv4_addr_num && i < EHIP_NETDEV_MAX_IP_NUM; i++) {
            ipv4_addr_t ip_addr = ipv4_info->ipv4_addr[i];
            ipv4_addr_t netmask = ipv4_info->ipv4_mask[i];
            
            // 计算广播地址
            ipv4_addr_t broadcast_addr = ip_addr | ~netmask;
            
            eh_stream_printf(stream, "        inet " IPV4_FORMATIO, ipv4_formatio(ip_addr));
            eh_stream_printf(stream, "  netmask " IPV4_FORMATIO, ipv4_formatio(netmask));
            eh_stream_printf(stream, "  broadcast " IPV4_FORMATIO, ipv4_formatio(broadcast_addr));
            eh_stream_printf(stream, "\n");
        }
    } else {
        eh_stream_printf(stream, "        No IPv4 address configured\n");
    }
    
    eh_stream_printf(stream, "\n");
}
/**
 * @brief 配置网络接口
 * @param stream 输出流
 * @param netdev 网络设备
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 执行结果
 */
static int configure_interface(struct stream_base *stream, ehip_netdev_t *netdev, int argc, const char *argv[]) {
    // 支持的命令格式：
    // ifconfig <interface> up/down
    // ifconfig <interface> <ip>/<prefix>

    if (argc < 3) {
        eh_stream_printf(stream, "Error: insufficient parameters for configuration\n");
        return -1;
    }
    const char *action = argv[2];
    if (strcmp(action, "up") == 0) {
        return ehip_netdev_tool_up(netdev);
    } else if (strcmp(action, "down") == 0){
        ehip_netdev_tool_down(netdev);
        return 0;
    } else {
        ipv4_addr_t ip_addr;
        uint8_t prefix_len = 24;

        struct ipv4_netdev *ipv4_dev = ehip_netdev_trait_ipv4_dev(netdev);
        if (ipv4_dev == NULL) {
            eh_stream_printf(stream, "Error: interface %s does not support IPv4\n", argv[1]);
            return -1;
        }
        
        // 先尝试直接解析IP地址
        int ret = ipv4_string_to_addr(action, &ip_addr);
        
        // 如果解析失败，检查是否有/字符
        if (ret != EH_RET_OK) {
            const char *slash = strchr(action, '/');
            // 复制IP部分到临时缓冲区
            char ip_part[16]; // 足够存储IP地址
            if(slash == NULL){
                eh_stream_printf(stream, "Error: invalid IP address format\n");
                return -1;
            }
            size_t ip_len = (size_t)(slash - action);
            if (ip_len >= sizeof(ip_part)) {
                eh_stream_printf(stream, "Error: IP address too long\n");
                return -1;
            }
            strncpy(ip_part, action, ip_len);
            ip_part[ip_len] = '\0';

            ret = ipv4_string_to_addr(ip_part, &ip_addr);
            if (ret != EH_RET_OK) {
                eh_stream_printf(stream, "Error: invalid IP address format\n");
                return -1;
            }

            prefix_len = (uint8_t)atoi(slash + 1);
            if (prefix_len == 0 || prefix_len > 32) {
                eh_stream_printf(stream, "Error: invalid prefix length (must be 1-32)\n");
                return -1;
            }
        }
        
        // 使用ipv4_netdev_set_main_addr配置IP地址
        ret = ipv4_netdev_set_main_addr(ipv4_dev, ip_addr, prefix_len);
        if (ret != EH_RET_OK) {
            switch (ret) {
                case EH_RET_EXISTS:
                    eh_stream_printf(stream, "Error: IP address already exists on interface\n");
                    break;
                case EH_RET_INVALID_PARAM:
                    eh_stream_printf(stream, "Error: invalid IP address (zero, multicast, or broadcast)\n");
                    break;
                default:
                    eh_stream_printf(stream, "Error: failed to set IP address (code: %d)\n", ret);
                    break;
            }
            return -1;
        }
    }
    
    return 0;
}

void ehtools_ifconfig(ehshell_cmd_context_t *cmd_context, int argc, const char *argv[]) {
    /* ifconfig [interface] [action] [parameters] */
    struct stream_base *stream = ehshell_command_stream(cmd_context);
    
    if (argc == 1) {
        eh_stream_printf(stream, "Network Interfaces:\n\n");
        ehip_netdev_t *netdev = NULL;
        while ((netdev = ehip_netdev_tool_iterate(netdev)) != NULL) {
            show_interface_info(stream, netdev);
        }
        goto finish;
    }
    if (argc >= 2) {
        ehip_netdev_t *netdev = ehip_netdev_tool_find(argv[1]);
        if (netdev == NULL) {
            eh_stream_printf(stream, "ifconfig: interface %s not found.\n", argv[1]);
            goto finish;
        }
        
        if (argc == 2) {
            show_interface_info(stream, netdev);
        } else {
            if (configure_interface(stream, netdev, argc, argv) < 0) {
                eh_stream_printf(stream, "ifconfig: configuration failed.\n");
                goto finish;
            }
        }
    }

finish:
    eh_stream_finish(stream);
    ehshell_command_finish(cmd_context);
}

void ehtools_ifconfig_event(ehshell_cmd_context_t *cmd_context, enum ehshell_event ehshell_event) {
    // ifconfig命令无需事件处理，直接完成命令
    if (ehshell_event & (EHSHELL_EVENT_SIGINT_REQUEST_QUIT | EHSHELL_EVENT_SHELL_EXIT)) {
        ehshell_command_finish(cmd_context);
    }
}