/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  mini_rtsp_server.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/09/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/09/2026 03:32:10 PM"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdint.h>

#define SERVER_PORT 8554
#define BUF_SIZE 4096

static int        client_rtp_port = 0;
static int        client_rtcp_port = 0;
static char       client_ip[64] = {0};
static int        rtp_sock = -1;

static uint16_t   rtp_seq = 0;

static uint32_t   rtp_timestamp = 0;

static uint32_t   rtp_ssrc = 0x12345678;

typedef struct
{
	uint8_t vpxcc;
	uint8_t mpt;

	uint16_t seq;

	uint32_t timestamp;

	uint32_t ssrc;
} RTPHeader;

static int create_rtp_socket()
{
	rtp_sock = socket(AF_INET, SOCK_DGRAM, 0);

	if(rtp_sock < 0)
	{
		perror("rtp socket");
		return -1;
	}

	return 0;
}

static void send_rtp_packet()
{
	char packet[1500];

	RTPHeader header;

	header.vpxcc = 0x80;
	header.mpt = 96;
	header.seq = htons(rtp_seq++);
	header.timestamp = htonl(rtp_timestamp);
	header.ssrc = htonl(rtp_ssrc);

	memcpy(packet, &header, 12);

	const char *payload = "Hello RTP";

	memcpy(packet + 12, payload, strlen(payload));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(client_rtp_port);
	addr.sin_addr.s_addr = inet_addr(client_ip);

	sendto(rtp_sock, packet, 12 + strlen(payload), 0, 
			(struct sockaddr *)&addr, sizeof(addr));

	printf("RTP packet sent seq=%d timestamp=%u\n", rtp_seq-1, rtp_timestamp);

	rtp_timestamp += 3000;
}

static int get_cseq(const char *req)
{
	const char *p = strstr(req, "CSeq:");
	if (!p) return 1;
	return atoi(p + 5);
}

static int parse_client_ports(const char *req)
{
	const char *p = strstr(req, "client_port=");
	if (!p)
	{
		return -1;
	}

	p += strlen("client_port=");

	if (sscanf(p, "%d-%d", &client_rtp_port, &client_rtcp_port) != 2)
	{
		 return -1;
	}

	return 0;
}

static void send_options(int client, int cseq)
{
	char res[512];
	snprintf(res, sizeof(res),
		"RTSP/1.0 200 OK\r\n"
		"CSeq: %d\r\n"
		"Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN\r\n"
		"\r\n",
		cseq);
	send(client, res, strlen(res), 0);
}

static void send_describe(int client, int cseq)
{
	/* 
	 * SPS: 67 64 10 28 AC 1B 1A A0 A0 3D A1 00 00 03 00 01 00 00 03 00 3C 8F 08 84 6A
	 * 
	 * Z2QQKKwbGqCgPaEAAAMAAQAAAwA8jwiEag==
	 *
	 * PPS: 68 EF 3C B0
	 * aO88sA==
	 * */
	const char *sdp =
		"v=0\r\n"
		"o=- 0 0 IN IP4 0.0.0.0\r\n"
		"s=mini_rtsp_server\r\n"
		"t=0 0\r\n"
		"a=control:*\r\n"
		"a=range:npt=0-\r\n"
		"m=video 0 RTP/AVP 96\r\n"
		"c=IN IP4 0.0.0.0\r\n"
		"a=rtpmap:96 H264/90000\r\n"
		"a=fmtp:96 packetization-mode=1;"
		"sprop-parameter-sets="
		"Z2QQKKwbGqCgPaEAAAMAAQAAAwA8jwiEag==,"
		"aO88sA==\r\n"
		"a=control:track1\r\n";

	char res[2048];

	int response_len = snprintf(
			res,
			sizeof(res),
			"RTSP/1.0 200 OK\r\n"
			"CSeq: %d\r\n"
			"Content-Type: application/sdp\r\n"
			"Content-Length: %zu\r\n"
			"\r\n"
			"%s",
			cseq,
			strlen(sdp),
			sdp);

	if (response_len < 0 || response_len >= (int)sizeof(res))
	{
		fprintf(stderr, "DESCRIBE response is too large.\n");
		return;
	}

	printf("\n========== SDP ==========\n");
	printf("%s", sdp);
	printf("======= SDP END =========\n");

	printf("\n===== DESCRIBE RESPONSE =====\n");
	printf("%s", res);
	printf("===== RESPONSE END =========\n");

	size_t total_sent = 0;

	while (total_sent < (size_t)response_len)
	{
		ssize_t n = send(client, res + total_sent, (size_t)response_len - total_sent, 0);

		if (n < 0)
		{
			perror("send DESCRIBE response");
			return;
		}

		if (n == 0)
		{
			fprintf(stderr,  "Client disconnected while sending DESCRIBE response.\n");
			return;
		}

		total_sent += (size_t)n;
	}

	printf("Sent DESCRIBE response: %zu bytes.\n", total_sent);
}

static void send_setup(int client, int cseq)
{
	char res[512];
	snprintf(res, sizeof(res),
			"RTSP/1.0 200 OK\r\n"
			"CSeq: %d\r\n"
			"Session: 12345678\r\n"
			"Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=6000-6001\r\n"
			"\r\n",
			cseq,
			client_rtp_port,
			client_rtcp_port);

	send(client, res, strlen(res), 0);
}

static void send_play(int client, int cseq)
{
	char res[512];
	snprintf(res, sizeof(res),
			"RTSP/1.0 200 OK\r\n"
			"CSeq: %d\r\n"
			"Session: 12345678\r\n"
			"RTP-Info: url=rtsp://0.0.0.0:%d/live/track1;seq=0;rtptime=0\r\n"
			"\r\n",
			cseq, SERVER_PORT);
	send(client, res, strlen(res), 0);
}

int main(void)
{
	int server_fd, client_fd;
	struct sockaddr_in server_addr, client_addr;
	socklen_t client_len = sizeof(client_addr);
	char buf[BUF_SIZE];

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0)
	{
		perror("socket");
		return 1;
	}

	int opt = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	server_addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		perror("bind");
		close(server_fd);
		return 1;
	}

	if (listen(server_fd, 5) < 0)
	{
		perror("listen");
		close(server_fd);
		return 1;
	}

	printf("Mini RTSP server listening on port %d...\n", SERVER_PORT);

	client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
	if (client_fd < 0)
	{
		perror("accept");
		close(server_fd);
		return 1;
	}

	printf("Client connected: %s:%d\n",
			inet_ntoa(client_addr.sin_addr),
			ntohs(client_addr.sin_port));

	snprintf(client_ip, sizeof(client_ip), "%s", inet_ntoa(client_addr.sin_addr));
	printf("Client IP saved: %s\n", client_ip);

	while (1)
	{
		memset(buf, 0, sizeof(buf));
		int n = recv(client_fd, buf, sizeof(buf) - 1, 0);
		if(n<=0)
		{
			printf("Client disconnected.\n");
			break;
		}

		printf("\n===== RTSP Request =====\n%s\n", buf);

		int cseq = get_cseq(buf);

		if (strstr(buf, "OPTIONS"))
		{
			send_options(client_fd, cseq);
			printf("Sent OPTIONS response.\n");
		}
		else if (strstr(buf, "DESCRIBE"))
		{
			send_describe(client_fd, cseq);
			printf("Sent DESCRIBE response.\n");
		}
		else if (strstr(buf, "SETUP"))
		{
			if (parse_client_ports(buf) == 0)
			{
				printf("Parsed client RTP port : %d\n", client_rtp_port);
				printf("Parsed client RTCP port: %d\n", client_rtcp_port);
			}
			else
			{
				printf("Failed to parse client_port.\n");
			}
			send_setup(client_fd, cseq);
			printf("Sent SETUP response.\n");
		}
		else if (strstr(buf, "PLAY"))
		{
			send_play(client_fd, cseq);
			printf("Sent PLAY response.\n");
			if(rtp_sock < 0)
			{
				create_rtp_socket();
			}
			send_rtp_packet();
		}
		else if (strstr(buf, "TEARDOWN"))
		{
			printf("TEARDOWN received.\n");
			break;
		}
		else
		{
			printf("Unknown RTSP request.\n");
		}
	}

	close(client_fd);
	close(server_fd);
	return 0;
}
