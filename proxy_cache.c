#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define LRU_MAGIC_NUMBER 9999
// Least Recently Used
// LRU: 가장 오랫동안 참조되지 않은 페이지를 교체하는 기법

#define CACHE_OBJS_COUNT 10

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *requestline_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";

static const char *host_key = "Host";
static const char *connection_key = "Connection";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *user_agent_key = "User-Agent";

void *thread(void *vargsp);
void doit(int connfd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio);
int connect_endServer(char *hostname, int port, char *http_header);

// cache function
void cache_init();
int cache_find(char *url);
void cache_uri(char *uri, char *buf);

void readerPre(int i);
void readerAfter(int i);

typedef struct 
{
  char cache_obj[MAX_OBJECT_SIZE];
  char cache_url[MAXLINE];
  int LRU; // least recently used 가장 덜 사용한 것 (캐시에서 삭제할 때)
  int isEmpty;

  int readCnt;  // count of readers
  sem_t wmutex;  // protects accesses to cache 세마포어 타입
  sem_t rdcntmutex;  // protects accesses to readcnt
}cache_block; // 캐쉬블럭 구조체로 선언


typedef struct
{
  cache_block cacheobjs[CACHE_OBJS_COUNT];  // ten cache blocks
  int cache_num;
}Cache;

Cache cache;


int main(int argc, char **argv) {
  int listenfd, connfd;
  socklen_t clientlen;
  char hostname[MAXLINE], port[MAXLINE];
  pthread_t tid;
  struct sockaddr_storage clientaddr;

  cache_init(); 

  if (argc != 2) {
    // fprintf: 출력을 파일에다 씀. strerr: 파일 포인터
    fprintf(stderr, "usage: %s <port> \n", argv[0]);
    exit(1);  // exit(1): 에러 시 강제 종료
  }
  Signal(SIGPIPE, SIG_IGN); // 특정 클라가 종료되어있다고 해서 남은 클라에 영향가지않게 그 한쪽 종료됐다는 시그널을 무시해라.
  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s %s).\n", hostname, port);

    // 첫 번째 인자 *thread: 쓰레드 식별자
    // 두 번째: 쓰레드 특성 지정 (기본: NULL)
    // 세 번째: 쓰레드 함수
    // 네 번째: 쓰레드 함수의 매개변수
    Pthread_create(&tid, NULL, thread, (void *)connfd);

    // doit(connfd);

    // Close(connfd);
  }
  return 0;
}

void *thread(void *vargsp) {
  int connfd = (int)vargsp;
  Pthread_detach(pthread_self());
  doit(connfd);
  Close(connfd);
}

void doit(int connfd) {
  int end_serverfd;

  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char endserver_http_header[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE];
  int port;
  
  // rio: client's rio / server_rio: endserver's rio
  rio_t rio, server_rio;

  Rio_readinitb(&rio, connfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  sscanf(buf, "%s %s %s", method, uri, version);  // read the client reqeust line

  if (strcasecmp(method, "GET")) {
    printf("Proxy does not implement the method");
    return;
  }
  
  char url_store[100];
  strcpy(url_store, uri);

  // the url is cached?
  int cache_index;
  // in cache then return the cache content
  if ((cache_index=cache_find(url_store)) != -1) {
    readerPre(cache_index); // 열어줌
    Rio_writen(connfd, cache.cacheobjs[cache_index].cache_obj, strlen(cache.cacheobjs[cache_index].cache_obj));
    readerAfter(cache_index); // 닫아줌
    return;
  }
  
  // parse the uri to get hostname, file path, port
  parse_uri(uri, hostname, path, &port);

  // build the http header which will send to the end server
  build_http_header(endserver_http_header, hostname, path, port, &rio);

  // connect to the end server
  end_serverfd = connect_endServer(hostname, port, endserver_http_header);
  if (end_serverfd < 0) {
    printf("connection failed\n");
    return;
  }

  Rio_readinitb(&server_rio, end_serverfd);

  // write the http header to endserver
  Rio_writen(end_serverfd, endserver_http_header, strlen(endserver_http_header));

  // recieve message from end server and send to the client
  char cachebuf[MAX_OBJECT_SIZE];
  int sizebuf = 0;
  size_t n;
  while ((n=Rio_readlineb(&server_rio, buf, MAXLINE)) != 0) {
    // printf("proxy received %ld bytes, then send\n", n);
    sizebuf += n;
    if (sizebuf < MAX_OBJECT_SIZE) // 작으면 response 내용을 적어놈
      strcat(cachebuf, buf);
    Rio_writen(connfd, buf, n);
  }
  Close(end_serverfd);

  // store it
  if (sizebuf < MAX_OBJECT_SIZE) {
    cache_uri(url_store, cachebuf);
  }
}

void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio) {
  char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];
  
  // request line
  sprintf(request_hdr, requestline_hdr_format, path);

  // get other request header for client rio and change it
  while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
    if (strcmp(buf, endof_hdr) == 0)
      break;  // EOF
    
    if (!strncasecmp(buf, host_key, strlen(host_key))) {
      strcpy(host_hdr, buf);
      continue;
    }

    if (!strncasecmp(buf, connection_key, strlen(connection_key))
      && !strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key))
      && !strncasecmp(buf, user_agent_key, strlen(user_agent_key))) {
        strcat(other_hdr, buf);
      }
  }
  if (strlen(host_hdr) == 0) {
    sprintf(host_hdr, host_hdr_format, hostname);
  }
  sprintf(http_header, "%s%s%s%s%s%s%s",
          request_hdr,
          host_hdr,
          conn_hdr,
          prox_hdr,
          user_agent_hdr,
          other_hdr,
          endof_hdr);
  return;
}

// Connect to the end server
inline int connect_endServer(char *hostname, int port, char *http_header) {
  char portStr[100];
  sprintf(portStr, "%d", port);
  return Open_clientfd(hostname, portStr);
}

// parse the uri to get hostname, file path, port
void parse_uri(char *uri, char *hostname, char *path, int *port) {
  *port = 80;
  char *pos = strstr(uri, "//");

  pos = pos!=NULL? pos+2:uri;

  char *pos2 = strstr(pos, ":");
  // sscanf(pos, "%s", hostname);
  if (pos2 != NULL) {
    *pos2 = '\0';
    sscanf(pos, "%s", hostname);
    sscanf(pos2+1, "%d%s", port, path);
  } else {
    pos2 = strstr(pos, "/");
    if (pos2 != NULL) {
      *pos2 = '\0';  // 중간에 끊으려고
      sscanf(pos, "%s", hostname);
      *pos2 = '/';
      sscanf(pos2, "%s", path);
    } else {
      scanf(pos, "%s", hostname);
    }
  }
  return;
}

void cache_init() {
  cache.cache_num = 0;
  int i;
  for (i=0; i<CACHE_OBJS_COUNT; i++) {
    cache.cacheobjs[i].LRU = 0; // 처음이니까
    cache.cacheobjs[i].isEmpty = 1; // 1, 0으로 캐시 엠티인지 구분?

    // Sem_init 첫 번째 인자: 초기화할 세마포어의 포인터
    // 두 번째: 0 - 쓰레드들끼리 세마포어 공유, 그 외 - 프로세스 간 공유
    // 세 번째: 초기 값
    Sem_init(&cache.cacheobjs[i].wmutex, 0, 1); // write mutex
    Sem_init(&cache.cacheobjs[i].rdcntmutex, 0, 1); // read count mutex
    // ㄴ flag 지정
    cache.cacheobjs[i].readCnt = 0; // init이니까 0
  }
}

void readerPre(int i) {
  P(&cache.cacheobjs[i].rdcntmutex);
  cache.cacheobjs[i].readCnt++;
  if (cache.cacheobjs[i].readCnt == 1)
    P(&cache.cacheobjs[i].wmutex);
  V(&cache.cacheobjs[i].rdcntmutex);
}

void readerAfter(int i) {
  P(&cache.cacheobjs[i].rdcntmutex);
  cache.cacheobjs[i].readCnt--;
  if (cache.cacheobjs[i].readCnt == 0)
    V(&cache.cacheobjs[i].wmutex);
  V(&cache.cacheobjs[i].rdcntmutex);
}

int cache_find(char *url) {
  int i;
  for (i=0; i<CACHE_OBJS_COUNT; i++) {
    readerPre(i);
    if ((cache.cacheobjs[i].isEmpty == 0) && (strcmp(url, cache.cacheobjs[i].cache_url) == 0))
      break;
    readerAfter(i);
  }
  if (i >= CACHE_OBJS_COUNT)
    return -1;
  return i;
}

int cache_eviction() {
  int min = LRU_MAGIC_NUMBER;
  int minindex = 0;
  int i;
  for (i=0; i<CACHE_OBJS_COUNT; i++) {
    readerPre(i);
    if (cache.cacheobjs[i].isEmpty == 1) {
      minindex = i;
      readerAfter(i);
      break;
    }
    if (cache.cacheobjs[i].LRU < min) {
      minindex = i;
      min = cache.cacheobjs[i].LRU;
      readerAfter(i);
      continue;
    }
    readerAfter(i);
  }
  return minindex;
}

void writePre(int i) {
  P(&cache.cacheobjs[i].wmutex);
}

void writeAfter(int i) {
  V(&cache.cacheobjs[i].wmutex);
}

// update the LRU number except the new cache one
void cache_LRU(int index) {
  int i;
  for (i=0; i<index; i++) { // ex) 5 일때 5 이하 다 내려줌
    writePre(i);
    if (cache.cacheobjs[i].isEmpty == 0 && i != index)
      cache.cacheobjs[i].LRU--;
    writeAfter(i);
  }
  i++;
  for (i; i<CACHE_OBJS_COUNT; i++) { // 5부터 10 사이 수도 내려줌
    writePre(i);
    if (cache.cacheobjs[i].isEmpty == 0 && i != index) {
      cache.cacheobjs[i].LRU--; // 이미 찾은 애는 9999로 보냈으니 그 앞에있는 애들 인덱스 -1씩 내려준다
    }
    writeAfter(i);
  }
}

// cache the uri and content in cache
void cache_uri(char *uri, char *buf) {
  int i = cache_eviction(); // 빈 캐시 블럭을 찾는 첫번째 index
  
  writePre(i);

  strcpy(cache.cacheobjs[i].cache_obj, buf);
  strcpy(cache.cacheobjs[i].cache_url, uri);
  cache.cacheobjs[i].isEmpty = 0;
  cache.cacheobjs[i].LRU = LRU_MAGIC_NUMBER; // 가장 최근에 했으니 우선순위 9999로 보내줌
  cache_LRU(i); // 나 빼고 LRU 다 내려.. 난 9999니까

  writeAfter(i);
}