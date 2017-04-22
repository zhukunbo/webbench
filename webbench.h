#ifndef _WEBBENCH_H_
#define _WEBBENCH_H_

#define PRG_VERSION  	"version 1.0"
#define REQUEST_SIZE 	2048
#define METHOD_GET 	 	0
#define METHOD_HEAD  	1
#define METHOD_OPTIONS  2
#define METHOD_TRACE 	3
#define HTTP_DEF_PORT   80

extern int debug_open;

#define PRINT_DEG(fmt, args...) do { \
    if (debug_open) { \
        printf("(%s: %s-%d) \033[0;33m"fmt"\033[0m\n", "DEBUG", __func__, __LINE__, ## args); \
    } \
} while (0)

#endif 	/* end _WEBBENCH_H_*/