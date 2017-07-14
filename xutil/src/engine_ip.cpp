#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <Ws2tcpip.h> /*must be here, otherwise conflict with ws2def.h*/
#endif

#include "commtype.h"
#include "engine_ip.h"
#include "win_inet_ntop.h"
#include "win_inet_pton.h"

const char * util_inet_ntop(int af, const void *src, char *dst, size_t size)
{
#ifdef _WIN32
	return win_inet_ntop(af, src,dst,size);
#else
	return inet_ntop(af, src,dst,size);
#endif
}

int util_inet_pton(int af, const char *src, void *dst)
{
#ifdef _WIN32
	return win_inet_pton(af, src, dst);
#else
	return inet_pton(af, src, dst);
#endif
}


DLL_API bool host_addr_check(char *hostname)
{
	int count = 0;
	char* p = hostname;

	while(0 != *p)
	{
	   if(*p =='.')
		if((p+1) != NULL && *(p+1)!= '.')
			count++;
	   p++;
	}

	if(count != 3)
	{
		return false;
	}

	return true;
}

DLL_API char* engine_ipv4_to_str(uint32_t ip, char *ip_buf)
{
	if (util_inet_ntop(AF_INET, (void*)&ip, ip_buf, INET_ADDRSTRLEN) == NULL)
	{
		fprintf(stderr, "inet_ntop failed.\n");
	}
	return ip_buf;
}

DLL_API char* engine_ipv6_to_str(struct in6_addr *ipv6, char *ip_buf)
{
	if (util_inet_ntop(AF_INET6, (void*)ipv6, ip_buf, INET6_ADDRSTRLEN) == NULL)
	{
		fprintf(stderr, "inet_ntop failed.\n");
	}
	return ip_buf;
}

DLL_API char* engine_ip_to_str(engine_ip_t *ip, char *ip_buf)
{
	if (ADDRTYPE_IPV4 == ip->type)
	{
		return engine_ipv4_to_str(ip->ip_union._v4, ip_buf);
	}

	return engine_ipv6_to_str(&ip->ip_union._v6, ip_buf);
}

DLL_API int engine_str_to_ipv4(const char* ipstr, uint32_t *ip)
{
	struct in_addr tempaddr;
	if (util_inet_pton(AF_INET, ipstr, &tempaddr) != 0)
	{
		*ip = tempaddr.s_addr;
		return 0;
	}
	return -1;
}

DLL_API int engine_str_to_ipv6(const char* ipstr, struct in6_addr *ipv6)
{
	struct in6_addr tempaddr;
	if (util_inet_pton(AF_INET6, ipstr, &tempaddr) == 1)
	{
		IPV6_ADDR_COPY(ipv6, &tempaddr);
		return 0;
	}
	return -1;
}

DLL_API int engine_str_to_ip(const char* ipstr, engine_ip_t *engine_ip)
{
	if (strchr(ipstr, ':') == NULL)
	{
		/*ipv4*/
		struct in_addr tempaddr;
		if (util_inet_pton(AF_INET, ipstr, &tempaddr) != 0)
		{
			engine_ip->type = ADDRTYPE_IPV4;
			engine_ip->ip_union._v4 = tempaddr.s_addr;
			return 0;
		}
		return -1;
	}

	/*ipv6*/
	struct in6_addr tempaddr;
	if (util_inet_pton(AF_INET6, ipstr, &tempaddr) == 1)
	{
		engine_ip->type = ADDRTYPE_IPV6;
		IPV6_ADDR_COPY(&engine_ip->ip_union._v6, &tempaddr);
		return 0;
	}
	return -1;
}
