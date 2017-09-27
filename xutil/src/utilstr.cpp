#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#else
#include <windows.h>
//#include <minwindef.h>
#endif

#include "utilstr.h"


DLL_API char *util_str_trim(char *src, char find)
{
	int i = 0;
	char *begin = src;
	while (src[i] != '\0')
	{
		if (src[i] != find)
		{
			break;
		}
		else
		{
			begin++;
		}
		i++;
	}

	for (i = (int)strlen(src) - 1; i >= 0; i--)
	{
		if (src[i] != find)
		{
			break;
		}
		else
		{
			src[i] = '\0';
		}
	}

	return begin;
}

DLL_API int util_strlen(char *str)
{
    int c = 0;

    while (*str++ != 0)
        c++;
    return c;
}


DLL_API int util_strncmp(char *s1, char *s2, int n)
{
    //assert((s1!=NULL)&&(s2!=NULL));  
  
    while(*s1!='\0'&&*s2!='\0'&&n)
    {  
        if(*s1-*s2>0)  
            return 1;  
        if(*s1-*s2<0)  
            return -1;  
        s1++;  
        s2++;  
        n--;  
    }  
    if(*s1=='\0'&&*s2!='\0')
        return -1;  
    if(*s2=='\0'&&*s1!='\0')  
        return 1;  
    return 0; 
}

DLL_API int util_strcmp(char *s1, char *s2)
{
    //assert((s1!=NULL)&&(s2!=NULL));  
  
    while(*s1!='\0'&&*s2!='\0')
    {  
        if(*s1-*s2>0)  
            return 1;  
        if(*s1-*s2<0)  
            return -1;  
        s1++;  
        s2++;  
    }  
    if(*s1=='\0'&&*s2!='\0')
        return -1;  
    if(*s2=='\0'&&*s1!='\0')  
        return 1;  
    return 0; 
}

DLL_API int util_strncpy(char *dst, char *src, int len)
{
    int l = util_strlen(src) + 1;
    if (l > len) l = len;

    util_memcpy(dst, src, l);

    return l;
}

DLL_API int util_strcpy(char *dst, char *src)
{
    int l = util_strlen(src) + 1;

    util_memcpy(dst, src, l);

    return l;
}

DLL_API void util_memcpy(void *dst, void *src, int len)
{
    char *r_dst = (char *)dst;
    char *r_src = (char *)src;
    while (len--)
        *r_dst++ = *r_src++;
}

DLL_API void util_zero(void *buf, int len)
{
    char *zero = (char*)buf;
    while (len--)
        *zero++ = 0;
}

DLL_API int util_memsearch(char *buf, int buf_len, char *mem, int mem_len)
{
    int i, matched = 0;

    if (mem_len > buf_len)
        return -1;

    for (i = 0; i < buf_len; i++)
    {
        if (buf[i] == mem[matched])
        {
            if (++matched == mem_len)
                return i + 1;
        }
        else
            matched = 0;
    }

    return -1;
}

DLL_API int util_stristr(char *haystack, int haystack_len, char *str)
{
    char *ptr = haystack;
    int str_len = util_strlen(str);
    int match_count = 0;

    while (haystack_len-- > 0)
    {
        char a = *ptr++;
        char b = str[match_count];
        a = a >= 'A' && a <= 'Z' ? a | 0x60 : a;
        b = b >= 'A' && b <= 'Z' ? b | 0x60 : b;

        if (a == b)
        {
            if (++match_count == str_len)
                return int(ptr - haystack);
        }
        else
            match_count = 0;
    }

    return -1;
}

