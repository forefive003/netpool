#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#else
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "commtype.h"
#include "utilcommon.h"

DLL_API int util_get_cpu_core_num()
{
#ifdef _WIN32
	SYSTEM_INFO siSysInfo;
	GetSystemInfo(&siSysInfo);
	return siSysInfo.dwNumberOfProcessors;
#else
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

DLL_API int util_creatdir(char *pDir)
{
    int i = 0;
    int iRet;
    int iLen;
    char* pszDir;

    if(NULL == pDir)
    {
        return 0;
    }

    pszDir = STRDUP(pDir);
    iLen = (int)strlen(pszDir);

    if (pszDir[0] == '/')
    {
		/*linux�µ�һ��Ϊ/����*/
        i = 1;
    }
    else
    {
        i = 0;
    }

	// �����м�Ŀ¼
    for (;i < iLen;i ++)
    {
        if (pszDir[i] == '\\' || pszDir[i] == '/')
        {
            pszDir[i] = '\0';

			//���������,����
            iRet = ACCESS(pszDir,0);
            if (iRet != 0)
            {
                iRet = MKDIR(pszDir);
                if (iRet != 0)
                {
                    char errBuf[64] = {0};
                    printf("mkdir %s failed, error %s.\n", pszDir,
                                str_error_s(errBuf, 64, errno));
                    return -1;
                }
            }
			//֧��linux,������\����/
            pszDir[i] = '/';
        }
    }

    iRet = MKDIR(pszDir);
    free(pszDir);
    return iRet;
}
