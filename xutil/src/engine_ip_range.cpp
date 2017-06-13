#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <Ws2tcpip.h> /*must be here, otherwise conflict with ws2def.h*/
#endif


#include "commtype.h"
#include "utilstr.h"
#include "engine_ip_range.h"

#if 0
char* ipv4_bits_to_netmask(unsigned int mask, char *mask_buf)
{
    uint32_t ip = htonl(0xffffffff << ( 32- mask));

    if (inet_ntop(AF_INET, (void*)&ip, mask_buf, HOST_IP_LEN) == NULL)
    {
        fprintf(stderr, "inet_ntop failed.\n");
        return NULL;
    }

    return mask_buf;
}

void engine_net_to_ip(engine_ip_t *ip, unsigned int mask)
{
    if (ADDRTYPE_IPV4 == ip->type)
    {
        //转换ipv4
        uint32_t t = htonl(0xffffffff << ( 32- mask));
        ip->ip_union._v4.addr &= t;
        return;
    }

///IPV6的掩码限制在32～128之间
    if (mask >= 32 && mask <= 128)
    {
        if(mask > 96)
        {
            ip->ip_union._v6.addr[3] &= htonl(0xffffffff<<(128 - mask));
        }
        else if(mask == 96)
        {
            ip->ip_union._v6.addr[3] &= htonl(0x0);
        }
        else if(mask > 64)
        {
            ip->ip_union._v6.addr[3] &= htonl(0x0);
            ip->ip_union._v6.addr[2] &= htonl(0xffffffff<<(128 - mask-32));
        }
        else if(mask == 64)
        {
            ip->ip_union._v6.addr[3] &= htonl(0x0);
            ip->ip_union._v6.addr[2] &= htonl(0x0);
        }
        else if(mask > 32)
        {
            ip->ip_union._v6.addr[3] &= htonl(0x0);
            ip->ip_union._v6.addr[2] &= htonl(0x0);
            ip->ip_union._v6.addr[1] &= htonl(0xffffffff<<(128 - mask- 64));
        }
        else if(mask == 32)
        {
            ip->ip_union._v6.addr[3] &= htonl(0x0);
            ip->ip_union._v6.addr[2] &= htonl(0x0);
            ip->ip_union._v6.addr[1] &= htonl(0x0);
        }
    }
    else
    {
        fprintf(stderr, "error mask value %d when ipv6\n", mask);
    }
    return;
}
#endif

static int _load_ipv4_subnet(const char* ipstr, const char *mask,
                engine_ip_range_t *ipRange)
{
    uint32_t minIP, maxIP;
    uint32_t ipval;

    uint32_t i_mask = strtol(mask, (char **) NULL, 10);
    if (i_mask <= 32)
    {
        if (0 != engine_str_to_ipv4(ipstr, &ipval))
        {
            return -1;
        }

        minIP = ntohl(ipval);
        CONVERT_TO_BEGIN_END(minIP, maxIP, i_mask);

        ipRange->start_addr.type = ADDRTYPE_IPV4;
        ipRange->start_addr.ip_union._v4 = htonl(minIP);
        ipRange->end_addr.type = ADDRTYPE_IPV4;
        ipRange->end_addr.ip_union._v4 = htonl(maxIP);
        return 0;
    }

    fprintf(stderr, "error mask value %d when ipv4\n", i_mask);
    return -1;
}

static int engine_ip_parse_range_ipv4(char *ipstr, engine_ip_range_t *ipRange)
{
    char *p, *pch;
    int len;
    char str[INET_ADDRSTRLEN] = {0};
    uint32_t ipval = 0;

    if ((p = strpbrk(ipstr, "/")) != NULL)
    {
        /*1.1.1.1/23*/
        *p = '\0';
        p++;
        return _load_ipv4_subnet(ipstr, p, ipRange);
    }
    else if ((p = strpbrk(ipstr, "-")) != NULL)
    {
        *p = '\0';
        p++;

        /*找到最后一个. 如192.168.1.60, 则在192.168.1位置*/
        if ((pch = strrchr(ipstr, '.')) == NULL)
        {
            fprintf(stderr, "error ipstr %s when ipv4\n", ipstr);
            return -1;
        }

        /*解析start地址*/
        if (engine_str_to_ipv4(ipstr, &ipval) != 0)
        {
            return -1;
        }
        ipRange->start_addr.type = ADDRTYPE_IPV4;
        ipRange->start_addr.ip_union._v4 = ipval;

        /*解析end地址*/
        if ((strchr(p, '.') != NULL))
        {
            /*192.168.1.1-192.168.1.60*/
            if (engine_str_to_ipv4(p, &ipval) != 0)
            {
                return -1;
            }
            ipRange->end_addr.type = ADDRTYPE_IPV4;
            ipRange->end_addr.ip_union._v4 = ipval;
        }
        else
        {
            /*192.168.1.1-60, 取192.168.1. 加上60*/
            len = (int)(pch - ipstr + 1);
            util_strncpy(str, ipstr, len);
			util_strncpy(str + len, p, (int)strlen(p));
            p = str;

            if (engine_str_to_ipv4(p, &ipval) != 0)
            {
                return -1;
            }
            ipRange->end_addr.type = ADDRTYPE_IPV4;
            ipRange->end_addr.ip_union._v4 = ipval;
        }
    }
    else
    {
        if (engine_str_to_ipv4(ipstr, &ipval) != 0)
        {
            return -1;
        }
        ipRange->start_addr.type = ADDRTYPE_IPV4;
        ipRange->start_addr.ip_union._v4 = ipval;
        ipRange->end_addr.type = ADDRTYPE_IPV4;
        ipRange->end_addr.ip_union._v4 = ipval;
    }

    return 0;
}


#define INT_LEN 32
#define IPV6_INT_LEN 4

static int _load_ipv6_subnet(const char* ipstr, const char *mask,
        engine_ip_range_t *ipRange)
{
    uint32_t i_mask = strtol(mask, (char **) NULL, 10);

    struct in6_addr ipval;
    struct in6_addr minipval, maxipval;
    if (0 != engine_str_to_ipv6(ipstr, &ipval))
    {
        return -1;
    }
    engine_ipv6_to_host(&ipval);

    memcpy(&minipval, &ipval, sizeof(struct in6_addr));
    memcpy(&maxipval, &ipval, sizeof(struct in6_addr));

#ifdef _WIN32
	uint32_t *min_addr32 = (uint32_t*)&minipval.u.Byte[0];
	uint32_t *max_addr32 = (uint32_t*)&maxipval.u.Byte[0];
#endif

    if (i_mask > INT_LEN * 3 && i_mask <= INT_LEN * 4)
    {
#ifdef _WIN32		
		CONVERT_TO_BEGIN_END(min_addr32[3], max_addr32[3], i_mask - 96);
#else
        CONVERT_TO_BEGIN_END(minipval.s6_addr32[3], maxipval.s6_addr32[3], i_mask - 96);
#endif
    }
    else if (i_mask > INT_LEN * 2 && i_mask <= INT_LEN * 3)
    {
#ifdef _WIN32		
		CONVERT_TO_BEGIN_END(min_addr32[2], max_addr32[2], i_mask - 64);
		min_addr32[3] = 0;
		max_addr32[3] = 0xffffffff;
#else
        CONVERT_TO_BEGIN_END(minipval.s6_addr32[2], maxipval.s6_addr32[2], i_mask - 64);
        minipval.s6_addr32[3] = 0;
        maxipval.s6_addr32[3] = 0xffffffff;
#endif
    }
    else if (i_mask > INT_LEN && i_mask <= INT_LEN * 2)
    {
#ifdef _WIN32
		CONVERT_TO_BEGIN_END(min_addr32[1], max_addr32[1], i_mask - 32);

		min_addr32[2] = 0;
		min_addr32[3] = 0;

		max_addr32[2] = 0xffffffff;
		max_addr32[3] = 0xffffffff;
#else
        CONVERT_TO_BEGIN_END(minipval.s6_addr32[1], maxipval.s6_addr32[1], i_mask - 32);

        minipval.s6_addr32[2] = 0;
        minipval.s6_addr32[3] = 0;

        maxipval.s6_addr32[2] = 0xffffffff;
        maxipval.s6_addr32[3] = 0xffffffff;
#endif
    }
    else
    {
#ifdef _WIN32
		CONVERT_TO_BEGIN_END(min_addr32[0], max_addr32[0], i_mask);
		min_addr32[1] = 0;
		min_addr32[2] = 0;
		min_addr32[3] = 0;

		max_addr32[1] = 0xffffffff;
		max_addr32[2] = 0xffffffff;
		max_addr32[3] = 0xffffffff;
#else
        CONVERT_TO_BEGIN_END(minipval.s6_addr32[0], maxipval.s6_addr32[0], i_mask);
        minipval.s6_addr32[1] = 0;
        minipval.s6_addr32[2] = 0;
        minipval.s6_addr32[3] = 0;

        maxipval.s6_addr32[1] = 0xffffffff;
        maxipval.s6_addr32[2] = 0xffffffff;
        maxipval.s6_addr32[3] = 0xffffffff;
#endif
    }

    /*转换为网络字节序*/
    engine_ipv6_to_net(&minipval);
    engine_ipv6_to_net(&maxipval);

    ipRange->start_addr.type = ADDRTYPE_IPV6;
    memcpy(&ipRange->start_addr.ip_union._v6, &minipval, sizeof(struct in6_addr));
    ipRange->end_addr.type = ADDRTYPE_IPV6;
    memcpy(&ipRange->end_addr.ip_union._v6, &maxipval, sizeof(struct in6_addr));

    return 0;
}

static int engine_ip_parse_range_ipv6(char *ipstr, engine_ip_range_t *ipRange)
{
    char *p, *pch;
    int len;
    char str[INET_ADDRSTRLEN] = {0};

    struct in6_addr ipval;

    if ((p = strpbrk(ipstr, "/")) != NULL)
    {
        /*2001::23/96*/
        *p = '\0';
        p++;
        return _load_ipv6_subnet(ipstr, p, ipRange);
    }
    else if ((p = strpbrk(ipstr, "-")) != NULL)
    {
        *p = '\0';
        p++;

        /*找到最后一个. 如2001:1001::23, 则在2001:1001:位置*/
        if ((pch = strrchr(ipstr, ':')) == NULL)
        {
            fprintf(stderr, "error ipstr %s when ipv6\n", ipstr);
            return -1;
        }

        /*解析start地址*/
        if (engine_str_to_ipv6(ipstr, &ipval) != 0)
        {
            return -1;
        }
        ipRange->start_addr.type = ADDRTYPE_IPV6;
        memcpy(&ipRange->start_addr.ip_union._v6, &ipval, sizeof(struct in6_addr));

        /*解析end地址*/
        if ((strchr(p, ':') != NULL))
        {
            /*2001::23-2001::ff*/
            if (engine_str_to_ipv6(p, &ipval) != 0)
            {
                return -1;
            }
            ipRange->end_addr.type = ADDRTYPE_IPV6;
            memcpy(&ipRange->end_addr.ip_union._v6, &ipval, sizeof(struct in6_addr));
        }
        else
        {
            /*2001:1001::23-ff, 取2001:1001:: 加上ff*/
            len = (int)(pch - ipstr + 1);
			util_strncpy(str, ipstr, len);
			util_strncpy(str + len, p, (int)strlen(p));
            p = str;

            if (engine_str_to_ipv6(p, &ipval) != 0)
            {
                return -1;
            }
            ipRange->end_addr.type = ADDRTYPE_IPV6;
            memcpy(&ipRange->end_addr.ip_union._v6, &ipval, sizeof(struct in6_addr));
        }
    }
    else
    {
        if (engine_str_to_ipv6(ipstr, &ipval) != 0)
        {
            return -1;
        }

        ipRange->start_addr.type = ADDRTYPE_IPV6;
        memcpy(&ipRange->start_addr.ip_union._v6, &ipval, sizeof(struct in6_addr));
        ipRange->end_addr.type = ADDRTYPE_IPV6;
        memcpy(&ipRange->end_addr.ip_union._v6, &ipval, sizeof(struct in6_addr));
    }

    return 0;
}

DLL_API int engine_ip_parse_range(char *ipstr, engine_ip_range_t *ipRange)
{
    if (strchr(ipstr, ':') != NULL)  /// IPv6
    {
		return engine_ip_parse_range_ipv6(ipstr, ipRange);
    }
    else if (strchr(ipstr, '.') != NULL) ///IPv4
    {
        return engine_ip_parse_range_ipv4(ipstr, ipRange);
    }
    else
    {
        return -1;
    }
}

DLL_API int engine_ip_parse_ranges(char* ipRangeStr,
                engine_ip_range_t* ipRanges, int *ipRangesCnt,
                int maxCnt)
{
    char *tmpstr = NULL;

    /*先去前后空格*/
    ipRangeStr = util_str_trim(ipRangeStr, ' ');

    tmpstr = strtok(ipRangeStr, ",");
    if (NULL == tmpstr)
    {
        fprintf(stderr, "no ipnet in params.\n");
        return -1;
    }

    uint16_t ipIndex = 0;
    while(tmpstr != NULL)
    {
        if (ipIndex >= maxCnt)
        {
            fprintf(stderr, "too many ipnet in params.\n");
            return -1;
        }

        if (engine_ip_parse_range(tmpstr, &ipRanges[ipIndex]))
        {
            fprintf(stderr, "parse ipnet failed, %s.\n", tmpstr);
            return -1;
        }

        ipIndex++;
        tmpstr = strtok(NULL, ",");
    }

    *ipRangesCnt = ipIndex;
    return 0;
}

DLL_API int port_parse_range(char* portstr, port_range_t* portRange)
{
    char *p;

    /*2-1000*/
    if ((p = strpbrk(portstr, "-")) != NULL)
    {
        *p = '\0';
        p++;

        portRange->start_port = atoi(portstr);;
        portRange->end_port = atoi(p);
    }
    else
    {
        portRange->start_port = atoi(portstr);
        portRange->end_port = portRange->start_port;
    }

    if (portRange->start_port < SCAN_PORT_MIN
            || portRange->start_port > SCAN_PORT_MAX)
    {
        fprintf(stderr, "port %d invalid, must between 16-65535.\n",
                portRange->start_port);
        return -1;
    }

    if (portRange->end_port < SCAN_PORT_MIN
                || portRange->end_port > SCAN_PORT_MAX)
    {
        fprintf(stderr, "port %d invalid, must between 16-65535.\n",
                    portRange->end_port);
        return -1;
    }

    if (portRange->end_port < portRange->start_port)
    {
        fprintf(stderr, "port %d-%d not invalid.\n", portRange->start_port,
                    portRange->end_port);
        return -1;
    }

    return 0;
}

DLL_API int port_parse_ranges(char* portRangeStr,
                        port_range_t* portRanges, 
                        int *portRangesCnt,
                        int maxCnt)
{
    char *tmpstr = NULL;

    /*先去前后空格*/
    portRangeStr = util_str_trim(portRangeStr, ' ');

    tmpstr = strtok(portRangeStr, ",");
    if (NULL == tmpstr)
    {
        fprintf(stderr, "no port in params.\n");
        return -1;
    }

    uint16_t portIndex = 0;
    while(tmpstr != NULL)
    {
        if (portIndex >= maxCnt)
        {
            fprintf(stderr, "too many ports in params.\n");
            return -1;
        }

        if (port_parse_range(tmpstr, &portRanges[portIndex]))
        {
            fprintf(stderr, "parse ports failed, %s.\n", tmpstr);
            return -1;
        }

        portIndex++;
        tmpstr = strtok(NULL, ",");
    }

    *portRangesCnt = portIndex;
    return 0;
}

