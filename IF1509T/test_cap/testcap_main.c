#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "omxcam/omxcam.h"

#include "rtpavcsend.h"

static int sock_createcl(const char* addr, int port);

struct sockaddr_in s_peer;
static RtpSend* rtpsock;
static int udpsock;

// エンコード結果を受け取るコールバック
static void video_encoded(omxcam_buffer_t buff)
{
	AvcAnalyzeAndSend(rtpsock, buff.data, buff.length);
}

static void video_encoded_toudp(omxcam_buffer_t buff)
{
	extern void udpsend(int, const unsigned char*, int);
	udpsend(udpsock, buff.data, buff.length);
}

int main (int argc, char **argv)
{
	int rtpmode=1;	// デフォルトはRTPモード
	omxcam_video_settings_t videoset = {};
	if (argc < 3) {
		printf("Usage: %s <peer_addr> <recv_port>\n", argv[0]);
		exit(1);
	}

	if (strstr(argv[0], "udp") != NULL)
		rtpmode = 0;		// コマンド名にudpが入っているときはUDPモード

	// ソケット初期化
	udpsock = sock_createcl(argv[1], atoi(argv[2]));
	if (udpsock < 0)
		return __LINE__;

	// キャプチャ初期化
	omxcam_video_init(&videoset);
	videoset.on_data = rtpmode ? video_encoded : video_encoded_toudp;
	// カメラ設定
	videoset.camera.width = 1920;
	videoset.camera.height = 1080;
	videoset.camera.framerate = 30;
	// エンコーダ設定
	videoset.h264.bitrate = 12*1000*1000; //12Mbps
	videoset.h264.idr_period = 30;	//30フレームごとにIDR
	videoset.h264.inline_headers = OMXCAM_TRUE; // SPS/PPSを挿入

	if (rtpmode) {
		// RTP初期化
		rtpopen(&rtpsock, 1/*SSRC*/, 96/*payload_type*/, udpsock, &s_peer);
	}

	// キャプチャ開始
	omxcam_video_start(&videoset, OMXCAM_CAPTURE_FOREVER);

	if (rtpmode)
		rtpclose(rtpsock);
	else
		close(udpsock);
	return 0;
}

//=========================== ソケット送信用
static int sock_createcl(const char* peer_addr, int port)
{
	int s;

	memset(&s_peer, 0, sizeof(s_peer));
	s_peer.sin_family = AF_INET;
	s_peer.sin_addr.s_addr = inet_addr(peer_addr);
	s_peer.sin_port = htons(port);

	// パラメータのチェック
	if (s_peer.sin_addr.s_addr == 0 || s_peer.sin_addr.s_addr == 0xffffffff) {
		fprintf(stderr, "Invalid address(%s)\n", peer_addr);
		return -1;
	}
	if (port <= 0 || port > 65535) {
		fprintf(stderr, "Invalid port(%d)\n", port);
		return -1;
	}

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) { perror("socket"); return -1; }

	return s;
}
