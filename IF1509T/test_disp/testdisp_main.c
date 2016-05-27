#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "bcm_host.h"
#include "ilclient.h"
#define SOCKHEADER_SIZE		2		// UDPパケットの先頭につけるヘッダのサイズ。シーケンス番号だけ。

static int sock_createsv(int port);
static int sock_read(int sock, unsigned char* buff, unsigned int bufflen);
static void sock_close(int sock);

int main (int argc, char **argv)
{
	if (argc < 2) {
		printf("Usage: %s <recv_port>\n", argv[0]);
		exit(1);
	}
	bcm_host_init();	// OpenMAXを使う前に必要

	OMX_VIDEO_PARAM_PORTFORMATTYPE format;
	OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
	enum Component_E { COMP_DEC=0, COMP_SCHE, COMP_REND, COMP_CLOCK };
	COMPONENT_T *clist[5];	// コンポーネントのインスタンス配列（＋NULL終端）
	enum Tunnel_E { TUN_DECOUT=0, TUN_RENDIN, TUN_CLOCKOUT };
	TUNNEL_T tunnel[4];		// トンネルのインスタンス配列（＋終端）
	ILCLIENT_T *client;
	unsigned int data_len = 0;
	int err = 0;
	int sock;

	memset(clist, 0, sizeof(clist));
	memset(tunnel, 0, sizeof(tunnel));

	// 1: 初期化処理
	if ((sock = sock_createsv(atoi(argv[1]))) < 0) // 受信用ソケット作成
		exit(1);
	if ((client = ilclient_init()) == NULL)
		exit(1);
	if (OMX_Init() != OMX_ErrorNone) {
		ilclient_destroy(client);
		exit(1);
	}

	// 2: コンポーネント作成
	if (ilclient_create_component(client, &clist[COMP_DEC], "video_decode",
			ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS) != 0)
		err = __LINE__;
	if (!err && ilclient_create_component(client, &clist[COMP_REND], "video_render",
			ILCLIENT_DISABLE_ALL_PORTS) != 0)
		err = __LINE__;
	if (!err && ilclient_create_component(client, &clist[COMP_CLOCK], "clock",
			ILCLIENT_DISABLE_ALL_PORTS) != 0)
		err = __LINE__;
	if (!err && ilclient_create_component(client, &clist[COMP_SCHE], "video_scheduler",
			ILCLIENT_DISABLE_ALL_PORTS) != 0)
		err = __LINE__;

	// 3:クロックの設定
	memset(&cstate, 0, sizeof(cstate));
	cstate.nSize = sizeof(cstate);
	cstate.nVersion.nVersion = OMX_VERSION;
	cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
	cstate.nWaitMask = 1;
	if (!err && OMX_SetParameter(ILC_GET_HANDLE(clist[COMP_CLOCK]), OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
		err = __LINE__;

	// 4: トンネルの設定
	set_tunnel(&tunnel[TUN_DECOUT],   clist[COMP_DEC], 131, clist[COMP_SCHE], 10);
	set_tunnel(&tunnel[TUN_RENDIN],   clist[COMP_SCHE], 11, clist[COMP_REND], 90);
	set_tunnel(&tunnel[TUN_CLOCKOUT], clist[COMP_CLOCK],80, clist[COMP_SCHE], 12);

	if (!err && ilclient_setup_tunnel(&tunnel[TUN_CLOCKOUT], 0, 0) != 0)
		err = __LINE__;

	if (!err) {
		ilclient_change_component_state(clist[COMP_CLOCK], OMX_StateExecuting);
		ilclient_change_component_state(clist[COMP_DEC],   OMX_StateIdle);
	}

	// 5: デコーダの設定
	memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
	format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
	format.nVersion.nVersion = OMX_VERSION;
	format.nPortIndex = 130;
	format.eCompressionFormat = OMX_VIDEO_CodingAVC;

	if (!err &&
		OMX_SetParameter(ILC_GET_HANDLE(clist[COMP_DEC]), OMX_IndexParamVideoPortFormat, &format) == OMX_ErrorNone &&
		ilclient_enable_port_buffers(clist[COMP_DEC], 130, NULL, NULL, NULL) == 0)
	{
		OMX_BUFFERHEADERTYPE *buf;
		int port_settings_changed = 0;
		int first_packet = 1;

		// 6: デコーダの開始
		ilclient_change_component_state(clist[COMP_DEC], OMX_StateExecuting);

		while ((buf=ilclient_get_input_buffer(clist[COMP_DEC], 130, 1)) != NULL) {
			// デコーダの入力ポートからバッファをもらって、ビデオデータをセットする
			unsigned char *dest = buf->pBuffer;
			data_len += sock_read(sock, dest, buf->nAllocLen-data_len);

			if (port_settings_changed == 0 &&
				((data_len > 0 && ilclient_remove_event(clist[COMP_DEC], OMX_EventPortSettingsChanged, 131, 0, 0, 1) == 0) ||
				 (data_len == 0 && ilclient_wait_for_event(clist[COMP_DEC], OMX_EventPortSettingsChanged, 131, 0, 0, 1,
                                                       ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 10000) == 0)))
			{ //初回
				port_settings_changed = 1;
				// DECODER -> SCHEDULER トンネル
				if (ilclient_setup_tunnel(&tunnel[TUN_DECOUT], 0, 0) != 0) {
					err = __LINE__; break;
				}
				ilclient_change_component_state(clist[COMP_SCHE], OMX_StateExecuting);

				// SCHEDULER -> RENDER トンネル
				if (ilclient_setup_tunnel(&tunnel[TUN_RENDIN], 0, 1000) != 0) {
					err = __LINE__; break;
				}
				ilclient_change_component_state(clist[COMP_REND], OMX_StateExecuting);
			}
			if (!data_len)
				break;

			buf->nFilledLen = data_len;
			data_len = 0;

			buf->nOffset = 0;
			if (first_packet) {
				buf->nFlags = OMX_BUFFERFLAG_STARTTIME;
				first_packet = 0;
			}
			else
				buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;

			if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(clist[COMP_DEC]), buf) != OMX_ErrorNone) {
				err = __LINE__; break;
			}
		}

		buf->nFilledLen = 0;
		buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;

		if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(clist[COMP_DEC]), buf) != OMX_ErrorNone)
			err = __LINE__;

		// wait for EOS from render
		ilclient_wait_for_event(clist[COMP_REND], OMX_EventBufferFlag, 90, 0, OMX_BUFFERFLAG_EOS, 0,
									ILCLIENT_BUFFER_FLAG_EOS, 10000);

		// need to flush the renderer to allow clist[COMP_DEC] to disable its input port
		ilclient_flush_tunnels(tunnel, 0);

		ilclient_disable_port_buffers(clist[COMP_DEC], 130, NULL, NULL, NULL);
	}

	sock_close(sock);

	ilclient_disable_tunnel(&tunnel[0]);
	ilclient_disable_tunnel(&tunnel[1]);
	ilclient_disable_tunnel(&tunnel[2]);
	ilclient_teardown_tunnels(tunnel);

	ilclient_state_transition(clist, OMX_StateIdle);
	ilclient_state_transition(clist, OMX_StateLoaded);

	ilclient_cleanup_components(clist);

	OMX_Deinit();

	ilclient_destroy(client);

	if (err != 0)
		fprintf(stderr, "Error occurred (%d)\n", err);
	return err;
}

//=========================== ソケット受信用
static int sock_createsv(int port)
{
	int v;
	struct sockaddr_in sin = {AF_INET};
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) { perror("socket"); return -1; }

	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(port);
	if (bind(sock, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
		perror("bind"); return -1;
	}
	v = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&v, sizeof(v));
	v = 1024000;	//受信バッファを1MByteに
	setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&v, sizeof(v));

	return sock;
}

static int sock_read(int sock, unsigned char* buff, unsigned int bufflen)
{
	// パケットのヘッダからシーケンス番号をチェックする
	static unsigned long long total_packets = 0;
	static unsigned short prev_seqno = 0;
	static int first = 1;
	unsigned short seq;
	unsigned char tmp[1500];
	int recved = recvfrom(sock, tmp, sizeof(tmp), 0, NULL, NULL);
	if (recved < 0)
		return -1;
	total_packets ++;

	// 先頭2バイトにシーケンス番号
	seq = ((unsigned short*)tmp)[0];
	if (!first) {
		if ((unsigned short)(seq - prev_seqno) != 1)
			fprintf(stderr, "seqno [%d]->[%d] : total[%llu]\n", (int)prev_seqno, (int)seq, total_packets);
	}
	else
		first = 0;
	prev_seqno = seq;
	recved -= SOCKHEADER_SIZE;	// ヘッダの分スキップ

	if (recved > bufflen)
		recved = bufflen;
	memcpy(buff, tmp+SOCKHEADER_SIZE, recved);
	return recved;
}

static void sock_close(int sock)
{
	close(sock);
}
