#include <stdio.h>
#include <ctype.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <memory.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <net/if.h>
#include <sys/ioctl.h>

void* accept_request(void* arg);
void bad_request(int);
void cat(int, FILE*);
void cannot_execute(int);
void error_die(const char*);
void execute_cgi(int, const char*, const char*, const char*);
int get_line(int, char*, int);
void headers(int, const char*);
void not_found(int);
void serve_file(int, const char*);
int startup(u_short*);
void unimplemented(int);
int get_local_ip(struct in_addr*);

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server:jdbhttpd/0.1.0\r\n"

void* accept_request(void* arg) {
    int client = *((int*)arg);
    free(arg);
	char buf[1024];
	int numchars;
	char method[255];
	char url[255];
	char path[512];
	size_t i, j;
	struct stat st;
	int cgi = 0;
	char* query_string = NULL;

	numchars = get_line(client, buf, sizeof(buf));
	i = 0, j = 0;
	while (!ISspace(buf[j]) && (i < sizeof(method) - 1)) {
		method[i] = buf[j];
		i++, j++;
	}
	method[i] = '\0';

	//该服务器只能处理GET和POST请求
	if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
		unimplemented(client);
		return NULL;
	}

	/*post开启cgi*/
	if (strcasecmp(method, "POST") == 0) {
		cgi = 1;
	}

	//读取url地址
	i = 0;
	while (ISspace(buf[j]) && (j < sizeof(buf)))
		j++;
	while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))) {
		url[i] = buf[j];
		i++, j++;
	}
	url[i] = '\0';

	//如果是get方法
	if (strcasecmp(method, "GET") == 0) {
		query_string = url;
		while ((*query_string != '?') && (*query_string != '\0')) {
			query_string++;
		}
		if (*query_string == '?') {
			cgi = 1;
			*query_string = '\0';
			query_string++;
		}
	}

	/*格式化url到path数组，html文件都在htdocs中*/
	sprintf(path, "htdocs%s", url);
	//若是目录，则加上index.html
	if (path[strlen(path) - 1] == '/') {
		strcat(path, "index.html");
	}
	//寻找对应路径下的文件
	if (stat(path, &st) == -1) {
		//没找到文件,将请求报文中的请求头全部丢弃
		while ((numchars > 0) && strcmp("\n",buf)) {
			numchars = get_line(client, buf, sizeof(buf));
		}
		not_found(client);
	}
	else {
		if ((st.st_mode & S_IFMT) == S_IFDIR) {
			strcat(path, "/index.html");
		}
		if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH)) {
			cgi = 1;
		}
		if (!cgi) {
			serve_file(client, path);
		}
		else {
			execute_cgi(client, path, method, query_string);
		}

	}
	//由于HTTP是无连接的，所以断开连接
	close(client);

    return NULL;
}

void bad_request(int client) {
	char buf[1024];
	sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-type:text/html\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf,"\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<P>YOUR browser sent a bad request,");
	send(client,buf, strlen(buf), 0);
	sprintf(buf, "such as a POST without a Content-Length.\r\n");
	send(client, buf, strlen(buf), 0);

}

void cat(int client, FILE* resource) {
	char buf[1024];
	fgets(buf, sizeof(buf), resource);
	while (!feof(resource)) {
		send(client, buf, strlen(buf), 0);
		fgets(buf, sizeof(buf), resource);
	}
}

void cannot_execute(int client) {
	char buf[1024];
	sprintf(buf, "HTTP/1.0 500 Internal ServerError\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-type:text/html\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
	send(client, buf, strlen(buf), 0);
}

void error_die(const char* sc)
{
	perror(sc);
	exit(1);
}

void execute_cgi(int client, const char* path, const char* method, const char* query_string) {
	char buf[1024];
	int cgi_output[2];
	int cgi_input[2];
	pid_t pid;
	int status;
	int i;
	char c;
	int numchars = 1;
	int content_length = -1;

	buf[0] = 'A'; buf[1] = '\0';
	if (strcasecmp(method, "GET") == 0) {
		while ((numchars > 0) && strcmp("\n", buf)) {
			numchars = get_line(client, buf, sizeof(buf));
		}
	}
	else {//post
		//在post类型报文中的请求头中的Content_Length，得到请求体（body）的长度
		numchars = get_line(client, buf, sizeof(buf));
		//仅把Content_Length提取出来，请求头中的其他字段就忽略掉
		while ((numchars > 0) && strcmp("\n", buf)) {
			//Content-Length: 15个字节
			buf[15] = '\0';
			if (strcasecmp(buf, "Content-Length:") == 0) {
				content_length = atoi(&buf[16]);
			}
			numchars = get_line(client, buf, sizeof(buf));
		}
		if (content_length == -1) {
			bad_request(client);
			return;
		}
	}

	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	send(client, buf, strlen(buf), 0);

	//建立管道
	if (pipe(cgi_output) < 0) {
		cannot_execute(client);
		return;
	}
	if (pipe(cgi_input) < 0) {
		cannot_execute(client);
		return;
	}

	//创建新进程处理cgi
	if ((pid = fork()) < 0) {
		cannot_execute(client);
		return;
	}

	if (pid == 0) {//子进程
		char meth_env[255];
		char query_env[255];
		char length_env[255];

		dup2(cgi_output[1], 1);
		dup2(cgi_input[0], 0);
		close(cgi_output[0]);
		close(cgi_input[1]);

		//设置request_method的环境变量
		sprintf(meth_env, "REQUEST_METHOD=%s", method);
		putenv(meth_env);
		if (strcasecmp(method, "GET") == 0) {
			sprintf(query_env, "QUERY_STRING=%s", query_string);
			putenv(query_env);
		}
		else {
			sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
			putenv(length_env);
		}
		//使用execl运行cgi程序
		execl(path, path, NULL);
		exit(0);
	}
	else {//parent
		close(cgi_output[1]);
		close(cgi_input[0]);
		if (strcasecmp(method, "POST") == 0) {
			for (i = 0; i < content_length; i++) {
				recv(client, &c, 1, 0);
				write(cgi_input[1], &c, 1);
			}
		}

		while (read(cgi_output[0], &c, 1) > 0)
			send(client, &c, 1, 0);

		close(cgi_output[0]);
		close(cgi_input[1]);
		waitpid(pid, &status, 0);
	}
}

//读取一行，同时统一不同系统的换行符'\r\n','\n','\r'为'\n'
int get_line(int sock, char* buf, int size) {
	char ch = '\0';
	int i = 0;
	int n;

	while ((i < size - 1) && (ch != '\n')) {
		n = recv(sock, &ch, 1, 0);
		if (n > 0) {
			if (ch == '\r') {
				n = recv(sock, &ch, 1, MSG_PEEK);
				if ((n > 0) && (ch == '\n')) {
					recv(sock, &ch, 1, 0);
				}
				else {
					ch = '\n';
				}
			}
			buf[i] = ch;
			i++;
		}
		else {
			ch = '\n';
		}

	}
	buf[i] = '\0';
	return i;
}

void headers(int client,const char* filename) {
	char buf[1024];
	(void)filename;

	strcpy(buf, "HTTP/1.0 200 OK\r\n");
	send(client, buf, strlen(buf), 0);
	strcpy(buf, SERVER_STRING);
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type:text/html\r\n");
	send(client, buf, strlen(buf), 0);
	strcpy(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
}

void not_found(int client) {
	char buf[1024];

	sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
	send(client, buf, strlen(buf), 0);

	/*服务器信息*/
	sprintf(buf, SERVER_STRING);
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type:text/html\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<HTML><TITLE>NOT FOUND</TITLE>\r\n");
	send(client, buf, sizeof(buf), 0);
	sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "your request because the resource specified\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "is unavailable or nonexistent.\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "</BODY></HTML>\r\n");
	send(client, buf, strlen(buf), 0);
}

void serve_file(int client, const char* filename) {
	FILE* resource = NULL;
	int numchars = 1;
	char buf[1024];

	buf[0] = 'A'; buf[1] = '\0';
	while ((numchars > 0) && strcmp("\n", buf))
		numchars = get_line(client, buf, sizeof(buf));

	resource = fopen(filename, "r");
	if (resource == NULL)
		not_found(client);
	else {
		//写Http头部，请求行和请求头
		headers(client, filename);
		//写请求体body
		cat(client, resource);
	}
	fclose(resource);
}

int startup(u_short* port) {
	int httpd = 0;
	struct sockaddr_in name;

	httpd = socket(PF_INET, SOCK_STREAM, 0);
	if (httpd == -1) {
		error_die("socket");
	}
	memset(&name, 0, sizeof(name));
	name.sin_family = AF_INET;
	name.sin_port = htons(*port);
	//name.sin_addr.s_addr = htonl(INADDR_ANY);
	struct in_addr ipaddr;
    get_local_ip(&ipaddr);
    name.sin_addr.s_addr = ipaddr.s_addr;
	if (bind(httpd, (struct sockaddr*)&name, sizeof(name)) < 0) {
		error_die("bind");
	}
	if (*port == 0) {
		socklen_t namelen = sizeof(name);
		if (getsockname(httpd, (struct sockaddr*)&name, &namelen) == -1) {
			error_die("getsockname");
		}
		*port = ntohs(name.sin_port);
	}

	if (listen(httpd, 5) < 0) {
		error_die("listen");
	}
	return httpd;

}

void unimplemented(int client) {
	char buf[1024];

	sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
	send(client, buf, strlen(buf), 0);

	/*服务器信息*/
	sprintf(buf, SERVER_STRING);
	send(client,buf,strlen(buf),0);
	sprintf(buf, "Content-Type:text/html\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
	send(client, buf, sizeof(buf), 0);
	sprintf(buf, "</TITLE></HEAD>\r\n");
	send(client, buf, sizeof(buf), 0);
	sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "</BODY></HTML>\r\n");
	send(client, buf, strlen(buf), 0);
}

int get_local_ip(struct in_addr* ipaddr){
    printf("sfsdfds\n");
    int sock_get_ip;
    struct sockaddr_in* sin;
    struct ifreq ifr_ip;

    if((sock_get_ip=socket(AF_INET,SOCK_STREAM,0)) == -1){
        printf("socket create failse\n");
        return 0;
    }
    memset(&ifr_ip,0,sizeof(ifr_ip));
    strncpy(ifr_ip.ifr_name,"ens33",sizeof(ifr_ip.ifr_name)-1);
    if(ioctl(sock_get_ip,SIOCGIFADDR,&ifr_ip) < 0){
        printf("ioctl error\n");
        return 0;
    }
    sin = (struct sockaddr_in*)&ifr_ip.ifr_addr;
    *ipaddr = sin->sin_addr;

    char ip[50];
    strcpy(ip,inet_ntoa(sin->sin_addr));
    printf("local ip:%s\n",ip);
    fflush(stdout);
    close(sock_get_ip);
    return 1;
}


int main(void) {
	int server_sock = -1;
	u_short port = 0;
	int client_sock = -1;
	struct sockaddr_in client_name;
	socklen_t client_name_len = sizeof(client_name);
	pthread_t newthread;

	/*在对应端口建立httpd服务*/
	server_sock = startup(&port);
	printf("httpd running on port %d\n", port);

    int* iptr = NULL;
	while (1) {
		client_sock = accept(server_sock, (struct sockaddr*)&client_name, &client_name_len);
		if (client_sock == -1) {
			error_die("accept");
		}
        iptr = (int*)malloc(sizeof(int));
        *iptr = client_sock;
		if (pthread_create(&newthread, NULL, accept_request, (void*)iptr) != 0) {
			perror("pthread_create");
		}
	}

	close(server_sock);
	return 0;
}
