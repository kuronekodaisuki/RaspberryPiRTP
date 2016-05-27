#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int sock_write(int sock, const unsigned char* buff, unsigned int bufflen);
static void sock_close(int sock);

extern struct sockaddr_in s_peer;

#define SOCKHEADER_SIZE		2		// UDPパケットの先頭につけるヘッダのサイズ。シーケンス番号だけ。

static int sock_write(int sock, const unsigned char* buff, unsigned int bufflen)
{
	static unsigned short seqno = 0;
	unsigned char buff2[1500];
	// 先頭2バイトにシーケンス番号
	((unsigned short*)buff2)[0] = seqno++;
	memcpy(buff2+SOCKHEADER_SIZE, buff, bufflen);
	return sendto(sock, buff2, bufflen+SOCKHEADER_SIZE, 0, (struct sockaddr*)&s_peer, sizeof(s_peer));
}

void udpsend(int sock, const unsigned char* buff, int bufflen)
{
	// 1300バイトずつ送信
	while (bufflen > 0) {
		int l = bufflen > 1300 ? 1300 : bufflen;
		sock_write(sock, buff, l);
		buff += l;
		bufflen -= l;
	}
}

