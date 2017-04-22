#include "socket.c"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <rpc/types.h>
#include <getopt.h>
#include <strings.h>
#include <time.h>
#include <signal.h>

#include "webbench.h"

static int mypipe[2];
static int force;
static int force_reload;
static int http10 = 1;
static int bench_time;
static int proxy_port = HTTP_DEF_PORT;
static int clients;
static int speed;
static int failed;
static int bytes;
static int time_out;
static char *proxy_host;
static int method = METHOD_GET;
static char host[MAXHOSTNAMELEN]; 
static char request[REQUEST_SIZE];
int debug_open = 1;

static const struct option long_options[] = {
    {"force", no_argument, &force, 1},
    {"reload", no_argument, &force_reload, 1},
    {"time", required_argument, NULL, 't'},
    {"help", no_argument,  NULL, '?'},
    {"http09", no_argument, NULL, '9'},
    {"http10", no_argument, NULL, '1'},
    {"http11", no_argument, NULL, '2'},
    {"get", no_argument, &method, METHOD_GET},
    {"head", no_argument, &method, METHOD_HEAD},
    {"options", no_argument, &method, METHOD_OPTIONS},
    {"trace", no_argument, &method, METHOD_TRACE},
    {"version", no_argument, NULL, 'V'},
    {"proxy", required_argument, NULL, 'p'},
    {"clients", required_argument, NULL, 'c'},
	{NULL, 0, NULL, 0}
};

static void usage(void)
{
    fprintf(stderr, 
        "webbench [option]... URL\n"
        "  -f|--force               Don't wait for reply from server.\n"
        "  -r|--reload              Send reload request - Pragma: no-cache.\n"
        "  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
        "  -p|--proxy <server:port> Use proxy server for request.\n"
        "  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
        "  -9|--http09              Use HTTP/0.9 style requests.\n"
        "  -1|--http10              Use HTTP/1.0 protocol.\n"
        "  -2|--http11              Use HTTP/1.1 protocol.\n"
        "  --get                    Use GET request method.\n"
        "  --head                   Use HEAD request method.\n"
        "  --options                Use OPTIONS request method.\n"
        "  --trace                  Use TRACE request method.\n"
        "  -?|-h|--help             This information.\n"
        "  -V|--version             Display program version.\n"
    );
}

static void build_requst(const char *url)
{
	int i;
	char tmp[16];

	bzero(host, MAXHOSTNAMELEN);
    bzero(request, REQUEST_SIZE);

	if (force_reload && (proxy_host != NULL) && (http10 < 1)) {
		http10 = 1;
	}
	if ((method == METHOD_HEAD) && (http10 < 1)) {
		http10 = 1;
	}
	if ((method == METHOD_OPTIONS) && (http10 < 2)) {
		http10 = 2;
	}
	if((method == METHOD_TRACE) && (http10 < 2)) {		
		http10 = 2;
	}

	switch (method){
		case METHOD_GET: 
			strcpy(request, "GET");
			break;
		case METHOD_HEAD: 
			strcpy(request, "HEAD");
			break;
		case METHOD_OPTIONS: 
			strcpy(request,"OPTIONS");
			break;
		case METHOD_TRACE: 
			strcpy(request,"TRACE");
			break;	
	}

	strcat(request," ");
	if (strstr(url, "://") == NULL) {
		fprintf(stderr, "\n %s is no a vaild url. \n",url);
		exit(EXIT_FAILURE);
	}
	
	if (strlen(url) > 1500) {
		fprintf(stderr, "\n URL is too long \n");
		exit(EXIT_FAILURE);		
	}

	if (proxy_host == NULL) {
		if (strncasecmp("http://", url, 7) != 0) {
			fprintf(stderr, "\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
			exit(EXIT_FAILURE);
		}
	}

	i = strstr(url, "://") - url + 3;
	if (strstr(url + i, "/") == NULL) {
		fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
		exit(EXIT_FAILURE);
	}

	if (proxy_host == NULL) {
		if ((index(url + i, ':') != NULL) && 
			(index(url + i, ':') < index(url + i, '/'))) {
			strncpy(host, url + i, strchr(url + i, ':') - url - i);	
			bzero(tmp, sizeof(tmp));
			strncpy(tmp, index(url + i, ':') + 1, 
			strchr(url + i, '/') - index(url + i, ':') - 1);
			proxy_port = atoi(tmp);
			if (proxy_port == 0) {
				proxy_port = HTTP_DEF_PORT;
			}
	   } else {
			strncpy(host, url + i, strcspn(url + i, "/"));
	   }
	   PRINT_DEG("host is %s, proxy_port = %d", host, proxy_port);
	   strcat(request + strlen(request), url + i + strcspn(url + i, "/"));	   	   
	} else {
       strcat(request, url);
	}
	PRINT_DEG("the requst is %s \n", request);

	if (http10 == 1) {
		strcat(request, " HTTP/1.0");
	} else if(http10 == 2) {
		strcat(request, " HTTP/1.1");
	}
	strcat(request, "\r\n");

	if (http10 > 0) {
		strcat(request, "User-Agent: WebBench "PRG_VERSION"\r\n");
	}
	if ((proxy_host == NULL) && (http10 > 0)) {		
		strcat(request,"Host: ");
		strcat(request,host);
		strcat(request,"\r\n");
	}
	
	if (force_reload && (proxy_host != NULL)) {
		strcat(request,"Pragma: no-cache\r\n");
	}
	if (http10 > 1) {
		strcat(request,"Connection: close\r\n");
	}
	/* add empty line in the end */
	if (http10 > 0) {
		strcat(request,"\r\n"); 
	}   
}

static void alarm_handler(int signal)
{
	PRINT_DEG("it is time out");
	time_out = 1;
}

static void bench_calc(const char *host,const int port,const char *req)
{
	int len, n;
	int sock_id;
	char buff[1500];
	struct sigaction sa;

	/* setup alarm signal handler */
	sa.sa_handler = alarm_handler;
	sa.sa_flags=0;

	if (sigaction(SIGALRM, &sa, NULL)) {
		exit(3);
	}

	alarm(bench_time);

	len = strlen(req);

re_try:
	while (1) {
		if (time_out) {
			if (failed > 0) {
				--failed;				
			}
			return;
		}

		sock_id = create_socket_info(host, port);
		if (sock_id < 0) {
			failed++;
			continue;
		}		
		if (len != write(sock_id, req, len)) {
			failed++;
			close(sock_id);
			continue;
		}
		if (http10 == 0) {
			if (shutdown(sock_id, 1)) {
				failed++;
				close(sock_id);
				continue;
			}
		}
			
		if (force == 0) {
			while (1) {
				if (time_out) {
					break;
				}
				n = read(sock_id, buff, 1500);
				if (n < 0) {
					failed++;
					close(sock_id);
					goto re_try;
				} else if (n == 0) {
					break;
				} else {
					bytes += n;
				}
			}			
		}

		if (close(sock_id)) {
			failed++;
			continue;
		}
		speed++;
	}
}

static int core_process(void)
{
	int i;
	int n, m, k;
	pid_t pid;
	FILE *pipe_fd;

	if (pipe(mypipe)) {
		perror("pipe failed.");
		return -1;
	}

	/* fork process number of clients */
	for (i = 0; i < clients; i++) {
		pid = fork();
		if (pid <= (pid_t)0) {
			/* make sure childs faster */
			sleep(1);
			break;
		}
	}

	if (pid < (pid_t)0) {
		fprintf(stderr,"problems forking worker no. %d\n",i);
		perror("fork failed.");
		return -1;
	}

	if (pid == 0) {		
		/* this is a child process */
		bench_calc(((proxy_host == NULL) ? host: proxy_host), proxy_port, request);

		pipe_fd = fdopen(mypipe[1], "w");
		if (pipe_fd == NULL) {
			perror("open pipe for writing failed.");
		 	return -1;
		}
		
		PRINT_DEG("speed, failed, bytes= %d %d %d", speed, failed, bytes);
		fprintf(pipe_fd, "%d %d %d\n", speed, failed, bytes);
		fclose(pipe_fd);

		return 0;
	} else {
		PRINT_DEG("this is father");
		pipe_fd = fdopen(mypipe[0], "r");
		if (pipe_fd == NULL) {
			perror("open pipe for reading failed.");
			return -1;
		}		
		setvbuf(pipe_fd, NULL, _IONBF, 0);
		speed  = 0;
      	failed = 0;
     	bytes  = 0;

		PRINT_DEG("speed, failed, bytes= %d %d %d", speed, failed, bytes);

		while (1) {
			pid = fscanf(pipe_fd, "%d %d %d", &m, &n, &k);
			if (pid < 2) {
				fprintf(stderr,"Some of our childrens died.\n");
				break;
			}
			speed  += m;
			failed += n;
			bytes  += k;

			if (--clients == 0) {
				break;
			}			
			PRINT_DEG("clients = %d", clients);	
		}
		fclose(pipe_fd);

		printf("\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
	  		(int)((speed + failed) / (bench_time / 60.0f)),
	  		(int)(bytes / (float)bench_time),
	  		speed,
	  		failed);
	}

	return speed;
}

int main(int argc, char *argv[])
{
    int opt;
    int options_index=0;
	int socket_fd;
    char *tmp=NULL;
    
    /*1、参数检查 */
    if (argc == 1) {
        usage();
        exit(EXIT_FAILURE);
    }
    /*2、参数解析 */
    while ((opt = getopt_long(argc, argv, "912Vfrt:p:c:?h", long_options, &options_index)) != EOF) {
        switch (opt) {
            case  0 : break;
            case 'f': 
                force = 1;
                break;
            case 'r':
                force_reload=1;
                break; 
            case '9':
                http10=0;
                break;
            case '1': 
                http10=1;
                break;
            case '2': 
                http10=2;
                break;
            case 'V':
                printf(PRG_VERSION);
                exit(EXIT_SUCCESS);
            case 't':
                bench_time = atoi(optarg);
                break;
            case 'p':
                /* 获取代理服务器端口号，strrchr,返回从左边开始最后一个比配的字符以后的字符串 */
                tmp = strrchr(optarg, ':');
                proxy_host = optarg;
                if (tmp == NULL) {
                    break;
                }
                if (tmp == optarg) {
                    fprintf(stderr, "Error in option --proxy %s: Missing hostname.\n", optarg);
                    exit(EXIT_FAILURE);
                }
                if (tmp == (optarg + strlen(optarg) - 1)) {
                    fprintf(stderr, "Error in option --proxy %s Port number is missing.\n", optarg);
                    exit(EXIT_FAILURE);
                }
                proxy_port = atoi(tmp + 1);
                break;
            case ' ' :
            case '?':
            case 'h':
                usage();
                exit(EXIT_FAILURE);
            case 'c':
                clients = atoi(optarg);
                break;
            default :
                usage();
                exit(EXIT_FAILURE);
        }
}
    if (optind == argc) {
        fprintf(stderr, "webbench: Missing URL!\n");
        usage();
        exit(EXIT_FAILURE);
    }
    /* 设置默认的参数 */
    if (clients == 0) {
        clients = 1;
    } 
    if (bench_time == 0) {
        bench_time = 30;
    }

	/********************** info print *************************/
    fprintf(stderr,
		"Webbench - Simple Web Benchmark "PRG_VERSION"\n"
		"Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n"
	);
	build_requst(argv[optind]);
	
	printf("\nBenchmarking: ");
	switch (method) {
		case METHOD_GET:
		default:
			printf("GET");
		 	break;
		case METHOD_OPTIONS:
			printf("OPTIONS");
			break;
		case METHOD_HEAD:
			printf("HEAD");
			break;
		case METHOD_TRACE:
			printf("TRACE");
			break;
	}
	printf(" %s",argv[optind]);
	switch (http10) {
		case 0: 
			printf(" (using HTTP/0.9)");
			break;
		case 2: 
			printf(" (using HTTP/1.1)");
			break;
	}
	printf("\n");

	printf("%d clients \n", clients);
	printf("running %d sec", bench_time);	
	if (force) {
		printf(", early socket close");
	}
	if (proxy_host != NULL) {
		printf(", via proxy server %s:%d",proxy_host,proxy_port);
	}
	if (force_reload) {
		printf(", forcing reload");
	}
 	printf(".\n");
	/********************** info print end*************************/
	/*3、check avaibility of target server  */
	socket_fd = create_socket_info((proxy_host == NULL) ? host : proxy_host, proxy_port);
	if (socket_fd < 0) {
		fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
        exit(EXIT_FAILURE);
	}
	close(socket_fd);
	
    /*4、核心处理 */
	return core_process();
}

