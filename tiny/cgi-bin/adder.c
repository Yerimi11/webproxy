/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */

// tiny서버 실행시킨 후 http://15.164.94.35:8000/cgi-bin/adder?1&7 접속

#include "csapp.h"

int main(void) {
  char *buf, *p;
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
  int n1=0, n2=0;

  /* Extract the two arguments */
if ((buf = getenv("QUERY_STRING")) != NULL) {
  p = strchr(buf, '&');
  *p = '\0';
  // strcpy(arg1, buf); // url에 우리가 직접 쳤을 때 적용되는 함수
  // strcpy(arg2, p+1);
  // n1 = atoi(arg1);
  // n2 = atoi(arg2);
  sscanf(buf, "Num1 = %d", &n1);
  sscanf(p+1, "Num2 = %d", &n2);
}

  /* Make the response body */
  // content 인자에 html body를 담는다
  sprintf(content, "QUERY_STRING=%s", buf);
  sprintf(content, "Welcome to add.com: ");
  sprintf(content, "%sTHE Internet addition portal. \r\n<p>", content);
  sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);
  sprintf(content, "%sThanks for visiting!\r\n", content);

  /* Generate the HTTP response */
  printf("Connection: close\r\n");
  printf("Content-length: %d\r\n", (int)strlen(content));
  printf("Content-type: text/html\r\n\r\n");
  // 여기까지가 헤더

  // 여기까지 바디 출력
  printf("%s", content);
  fflush(stdout);
  
  exit(0);
}
/* $end adder */
