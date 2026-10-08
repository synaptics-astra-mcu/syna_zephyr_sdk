/*
 * Copyright (c) 2025 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/posix/net/if_arp.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/net/capture.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/virtual_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(shell_iw, 0);

#pragma GCC diagnostic ignored "-Wimplicit-function-declaration"
#define SHELL_IW			"iw config for wifi."

static int cmd_iw(const struct shell *sh, size_t argc, char **argv)
{
	for (int i = 0; i < argc; i++)
		printk("args %d: %s, %p\n", i, argv[i], sh);

	uart_cmd_handler(argc, argv);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sample_commands,
	SHELL_CMD_ARG(iwconfig, NULL, SHELL_IW, cmd_iw, 0, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(syna, &sample_commands, "synaptics wifi commands", NULL);

extern void mhd_get_data_packet_number(int *length);
#define malloc rtos_malloc
#define free rtos_free

#define MHD_IPERF_ERROR_ENABLE
#define MHD_IPERF_INFO_ENABLE
//#define MHD_IPERF_DEBUG_ENABLE

#ifdef MHD_IPERF_ERROR_ENABLE
#define MHD_IPERF_ERROR(args) do { rtos_printf args; } while (0 == 1)
#else
#define MHD_IPERF_ERROR(args)
#endif

#ifdef MHD_IPERF_INFO_ENABLE
#define MHD_IPERF_INFO(args)  do { rtos_printf args; } while (0 == 1)
#else
#define MHD_IPERF_INFO(args)
#endif

#ifdef MHD_IPERF_DEBUG_ENABLE
#define MHD_IPERF_DEBUG(args) do { rtos_printf args; } while (0 == 1)
#else
#define MHD_IPERF_DEBUG(args)
#endif

#define RTOS_TIME_READY

#define PEER_PORT        5001
#define PEER_IP_ADDR     "192.168.1.110"

#define NO_OF_TIMES      5000
#define BACKLOG          10

#define IPERF_PKT_TIMEOUT 500

#ifdef RTOS_TIME_READY
typedef uint32_t         host_time_t;
#endif

uint32_t TX_BUF_SIZE = (1024*2);
uint32_t RX_BUF_SIZE = (1024*2);
int sfd = -1;

void mhd_iperf_close(void)
{
}

void mhd_iperf_tcptx(char *ip_addr, int time, int port, int wnd)
{
	int i;
	struct sockaddr_in srv_addr = {0};
	int ret = 0;
	char *buf;
	int loop = 0;

	if (ip_addr == NULL)
	{
		ip_addr = PEER_IP_ADDR;
	}
	if (port == 0)
	{
		port = PEER_PORT;
	}
	if (time == 0)
	{
		time = 30;
	}

	TX_BUF_SIZE = wnd;

	if (TX_BUF_SIZE > 8000)
		TX_BUF_SIZE = 3000;

	rtos_printf("TX_BUF_SIZE = %d\n", TX_BUF_SIZE);

	buf = (char *)malloc(TX_BUF_SIZE);
	if (buf == NULL)
	{
		rtos_printf("IPERF: buffer alloc failed\n");
		return;
	}

	rtos_printf("IPERF: ip=%s, time=%d, port=%d\n", ip_addr, time, port);
	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd == -1)
	{
		rtos_printf("IPERF: socket failed\n");
		free(buf);
		return;
	}

	srv_addr.sin_family = AF_INET;
	srv_addr.sin_addr.s_addr = inet_addr(ip_addr);
	srv_addr.sin_port = htons(port);

	ret = connect(sfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
	if (ret != 0)
	{
		rtos_printf("IPERF: connect failed. ret=%d\n", ret);
		goto FAILURE;
	}

	memset(buf, 'a', TX_BUF_SIZE);
#ifdef RTOS_TIME_READY
	host_time_t start_time = host_rtos_get_time();
	host_time_t diff_time = 0;

	host_time_t delta_start = start_time;
	host_time_t delta_time = 0;
	host_time_t delta_size = 0;
	rtos_printf("Interval           Transfer     Bandwidth \n");
	for (i = 0; diff_time < time*1000 + 200; i++)
	{
		//ret = send(sfd, buf, TX_BUF_SIZE, MSG_DONTWAIT);
		ret = send(sfd, buf, TX_BUF_SIZE, 0);

		if (ret <= 0)
		{
			rtos_printf("IPERF: send failed, ret=%d, i=%d\n", ret, i);
			goto FAILURE;
		}
		else if (ret != TX_BUF_SIZE)
		{
			//rtos_printf("IPERF: send failed. return=%d, buffer=%d\n", ret, TX_BUF_SIZE);
			//goto FAILURE;
		}
		diff_time = host_rtos_get_time() - start_time;
		delta_size += ret;
		delta_time = host_rtos_get_time();

		if (delta_time - delta_start > 1000) {
			rtos_printf("%2d.0 - %2d.0 sec: %6d Kbytes %6d Kbits/sec \n",
				loop, loop + 1, delta_size / 1000, delta_size * 8 / 1000);

			delta_start = delta_time;
			delta_size = 0;
			loop ++;
		}
	}
#endif /* RTOS_TIME_READY */

FAILURE:
	close(sfd);
	rtos_printf("IPERF: finished\n");
	free(buf);
	return;
}

void mhd_iperf_tcprx(int time, int port, int wnd)
{
	int i;
	struct sockaddr_in srv_addr = {0};
	int ret = 0;
	char *buf;
	int sfd_new = -1;
	socklen_t sin_size;
	int loop = 0;

	if (port == 0)
	{
		port = PEER_PORT;
	}
	if (time == 0)
	{
		time = 30;
	}

	RX_BUF_SIZE = wnd;

	if (RX_BUF_SIZE > 65000)
		RX_BUF_SIZE = 65000;

	if (RX_BUF_SIZE < 8000)
		RX_BUF_SIZE = 8000;

	rtos_printf("RX_BUF_SIZE = %d\n", RX_BUF_SIZE);
	buf = (char *)malloc(RX_BUF_SIZE);

	if (buf == NULL)
	{
		  rtos_printf("IPERF: buffer alloc failed\n");
		return;
	}

	rtos_printf("IPERF: time=%d, port=%d\n", time, port);
	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd == -1)
	{
		rtos_printf("IPERF: socket failed\n");
		free(buf);
		return;
	}

	srv_addr.sin_family = AF_INET;
	srv_addr.sin_addr.s_addr = INADDR_ANY;
	srv_addr.sin_port = htons(port);

	ret = bind(sfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
	if (ret != 0)
	{
		rtos_printf("IPERF: bind failed. ret=%d\n", ret);
		goto FAILURE;
	}
	else
	{
		rtos_printf("IPERF: bind success.\n");
	}
	ret = listen(sfd, BACKLOG);
	if (ret != 0)
	{
		MHD_IPERF_ERROR(("IPERF: listen failed. ret=%d\n", ret));
		goto FAILURE;
	}
	else
	{
		MHD_IPERF_INFO(("IPERF: listen success.\n"));
	}

	do
	{
		// Block and wait for an incoming connection
		sin_size = sizeof(struct sockaddr_in);
		sfd_new = accept(sfd, (struct sockaddr *)&srv_addr, &sin_size);
		if (sfd_new != -1)
		{
			MHD_IPERF_INFO(("IPERF: accept connection.\n"));
			// serve connection
#ifdef RTOS_TIME_READY
			host_time_t start_time = 0;//host_rtos_get_time();
			host_time_t diff_time = 0;

			host_time_t delta_start = start_time;
			host_time_t delta_time = 0;
			host_time_t delta_size = 0;
			rtos_printf("Interval           Transfer     Bandwidth \n");

			for (i = 0; diff_time < time*1000; i++)
			{
				ret = recv(sfd_new, buf, RX_BUF_SIZE, 0);
				if (ret <= 0)
				{
					MHD_IPERF_ERROR(("IPERF: receive failed, ret=%d, i=%d\n", ret, i));
					goto FAILURE;
				}
				if (start_time == 0) {
					start_time = host_rtos_get_time();
					delta_start = start_time;
				}
				diff_time = host_rtos_get_time() - start_time;
				delta_size += ret;
				delta_time = host_rtos_get_time();

				if (delta_time - delta_start > 1000) {
					rtos_printf("%2d.0 - %2d.0 sec: %6d Kbytes %6d Kbits/sec \n",
					    loop, loop + 1, delta_size / 1000, delta_size * 8 / 1000);

					delta_start = delta_time;
					delta_size = 0;
					loop ++;
				}

			}
#endif /* RTOS_TIME_READY */
		}
		else
		{
			MHD_IPERF_ERROR(("IPERF: accept failed. ret=%d\n", ret));
			goto FAILURE;
		}
	} while (0);

FAILURE:
	close(sfd);
	close(sfd_new);
	rtos_printf("IPERF: finished\n");
	free(buf);
	return;
}

void mhd_iperf_udptx(char *ip_addr, int time, int port, int wnd)
{
	int i;
	struct sockaddr_in srv_addr = {0};
	int ret = 0;
	char *buf;
	int pkts;
	int loop = 0;

	TX_BUF_SIZE = 1470;

	rtos_printf("TX_BUF_SIZE = %d\n", TX_BUF_SIZE);

	if (ip_addr == NULL)
	{
		ip_addr = PEER_IP_ADDR;
	}
	if (port == 0)
	{
		port = PEER_PORT;
	}
	if (time == 0)
	{
		time = 30;
	}

	buf = (char *)malloc(TX_BUF_SIZE);
	if (buf == NULL)
	{
		MHD_IPERF_ERROR(("IPERF: buffer alloc failed\n"));
		return;
	}

	MHD_IPERF_INFO(("IPERF: ip=%s, time=%d, port=%d\n", ip_addr, time, port));
	sfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sfd == -1)
	{
		MHD_IPERF_ERROR(("IPERF: socket failed\n"));
		free(buf);
		return;
	}

	srv_addr.sin_family = AF_INET;
	srv_addr.sin_addr.s_addr = inet_addr(ip_addr);
	srv_addr.sin_port = htons(port);

	ret = connect(sfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
	if (ret != 0)
	{
		MHD_IPERF_ERROR(("IPERF: connect failed. ret=%d\n", ret));
		goto FAILURE;
	}

	memset(buf, 'a', TX_BUF_SIZE);
#ifdef RTOS_TIME_READY
	host_time_t start_time = host_rtos_get_time();
	host_time_t diff_time = 0;

	host_time_t delta_start = start_time;
	host_time_t delta_time = 0;
	host_time_t delta_size = 0;

	rtos_printf("Interval           Transfer     Bandwidth \n");
	for (i = 0; diff_time < time*1000 + 200; i++)
	{
		ret = sendto(sfd, buf, TX_BUF_SIZE, 0, (struct sockaddr *)&srv_addr, sizeof(struct sockaddr_in));

		if (ret <= 0)
		{
			rtos_printf("IPERF: send failed, ret=%d, i=%d\n", ret, i);
			goto FAILURE;
		}
		else if (ret != TX_BUF_SIZE)
		{
			rtos_printf("IPERF: send failed. return=%d, buffer=%d\n", ret, TX_BUF_SIZE);
			//goto FAILURE;
		}
		diff_time = host_rtos_get_time() - start_time;
		delta_size += ret;
		delta_time = host_rtos_get_time();

		mhd_get_data_packet_number(&pkts);
		if (pkts > 32)
			host_rtos_delay_milliseconds(5);

		if (delta_time - delta_start > 1000) {
			rtos_printf("%2d.0 - %2d.0 sec: %6d Kbytes %6d Kbits/sec \n",
				loop, loop + 1, delta_size / 1000, delta_size * 8 / 1000);
			host_rtos_delay_milliseconds(1);

			delta_start = delta_time;
			delta_size = 0;
			loop ++;
			host_rtos_delay_milliseconds(1);
		}
	}
#endif /* RTOS_TIME_READY */

FAILURE:
	close(sfd);
	rtos_printf("IPERF: finished\n");
	free(buf);
	return;
}

void mhd_iperf_udprx(int time, int port, int wnd)
{
	int i;
	int ret = 0;
	char *buf;
	struct sockaddr_in from;
	int                fromlen = sizeof(from);
	struct sockaddr_in srv_addr = {0};
	int timeout = 1000;
	int loop = 0;

	RX_BUF_SIZE = wnd;

	if (RX_BUF_SIZE > 65000)
		RX_BUF_SIZE = 65000;

	if (RX_BUF_SIZE < 8000)
		RX_BUF_SIZE = 8000;

	if (port == 0)
	{
		port = PEER_PORT;
	}
	if (time == 0)
	{
		time = 30;
	}

	rtos_printf("RX_BUF_SIZE = %d\n", RX_BUF_SIZE);
	buf = (char *)malloc(RX_BUF_SIZE);
	if (buf == NULL)
	{
		MHD_IPERF_ERROR(("IPERF: buffer alloc failed\n"));
		return;
	}

	MHD_IPERF_INFO(("IPERF: time=%d, port=%d\n", time, port));
	sfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sfd == -1)
	{
		MHD_IPERF_ERROR(("IPERF: socket failed\n"));
		free(buf);
		return;
	}

	srv_addr.sin_family = AF_INET;
	srv_addr.sin_addr.s_addr = INADDR_ANY;
	srv_addr.sin_port = htons(port);

	ret = bind(sfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
	if (ret != 0)
	{
		MHD_IPERF_ERROR(("IPERF: bind failed. ret=%d\n", ret));
		goto FAILURE;
	}
	else
	{
		MHD_IPERF_INFO(("IPERF: bind success.\n"));
	}
	ret = setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	do
	{
#ifdef RTOS_TIME_READY
		host_time_t start_time = 0;//host_rtos_get_time();
		host_time_t diff_time = 0;

		host_time_t delta_start = start_time;
		host_time_t delta_time = 0;
		host_time_t delta_size = 0;
		rtos_printf("Interval           Transfer     Bandwidth \n");

		for (i = 0; diff_time < time*1000; i++)
		{
			ret = recvfrom(sfd, buf, RX_BUF_SIZE, 0, (struct sockaddr*) &from, (socklen_t*) &fromlen);
			if (ret <= 0)
			{
				MHD_IPERF_ERROR(("IPERF: receive failed, ret=%d, i=%d\n", ret, i));
				goto FAILURE;
			}

			if (start_time == 0) {
				start_time = host_rtos_get_time();
				delta_start = start_time;
			}
			diff_time = host_rtos_get_time() - start_time;
			delta_size += ret;
			delta_time = host_rtos_get_time();

			if (delta_time - delta_start > 1000) {
				rtos_printf("%2d.0 - %2d.0 sec: %6d Kbytes %6d Kbits/sec \n",
					loop, loop + 1, delta_size / 1000, delta_size * 8 / 1000);

				delta_start = delta_time;
				delta_size = 0;
				loop ++;
			}
		}
		rtos_printf("diff time %d, %d\n", start_time, host_rtos_get_time());
		break;
#endif /* RTOS_TIME_READY */
	} while (ret > 0);

FAILURE:
	close(sfd);
	rtos_printf("IPERF: finished, %d, %08x\n", htons(from.sin_port), from.sin_addr.s_addr);
	free(buf);
	return;
}
