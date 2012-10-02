#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "periph.h"
#include "uart.h"

#include "memmap.h"
#include "core.h"

#include "../state_async.h"

#define INVALID_CLIENT (UINT_MAX-1)

pthread_t poll_uart_pthread;
volatile bool poll_uart_enabled = false;


static void *poll_uart_thread(void *);
#ifdef DEBUG1
static pthread_mutex_t poll_uart_mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
#else
static pthread_mutex_t poll_uart_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
static pthread_cond_t  poll_uart_cond  = PTHREAD_COND_INITIALIZER;
static uint32_t poll_uart_client = INVALID_CLIENT;

// circular buffer
// head is location to read valid data from
// tail is location to write data to
// characters are lost if not read fast enough
// head == tail --> buffer is full (not that it matters)
// head == NULL --> buffer if empty

// these should be treated as char's, but are stored as uint32_t
// to unify state tracking code
static uint32_t poll_uart_buffer[POLL_UART_BUFSIZE];
static uint32_t *poll_uart_head = NULL;
static uint32_t *poll_uart_tail = poll_uart_buffer;

static const struct timespec poll_uart_baud_sleep =\
		{0, (NSECS_PER_SEC/POLL_UART_BAUD)*8};	// *8 bytes vs bits


////////////////////////////////////////////////////////////////////////////////
// UART THREAD(S)
////////////////////////////////////////////////////////////////////////////////

static void *poll_uart_thread(void *unused __attribute__ ((unused))) {
	uint16_t port = POLL_UART_PORT;

	int sock;
	struct sockaddr_in server;

	assert(0 == prctl(PR_SET_NAME, "poll_uart_thread", 0, 0, 0));

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (-1 == sock) {
		ERR(E_UNKNOWN, "Creating UART device: %s\n", strerror(errno));
	}

	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons(port);
	int on = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	if (-1 == bind(sock, (struct sockaddr *) &server, sizeof(server))) {
		ERR(E_UNKNOWN, "Creating UART device: %s\n", strerror(errno));
	}

	if (-1 == listen(sock, 1)) {
		ERR(E_UNKNOWN, "Creating UART device: %s\n", strerror(errno));
	}

	INFO("UART listening on port %d (use `nc -4 localhost %d` to communicate)\n",
			port, port);

	pthread_cond_signal(&poll_uart_cond);

	while (1) {
		int client;
		while (1) {
			fd_set set;
			struct timeval timeout;

			FD_ZERO(&set);
			FD_SET(sock, &set);

			timeout.tv_sec = 0;
			timeout.tv_usec = 100000;

			if (select(FD_SETSIZE, &set, NULL, NULL, &timeout)) {
				client = accept(sock, NULL, 0);
				break;
			} else {
				if (!poll_uart_enabled) {
					INFO("Polling UART device shut down\n");
					pthread_exit(NULL);
				}
			}
		}

		if (-1 == client) {
			ERR(E_UNKNOWN, "UART device failure: %s\n", strerror(errno));
		} else {
			struct sockaddr_in sa;
			socklen_t l = sizeof(sa);

			if (getsockname(client, (struct sockaddr*) &sa, &l)) {
				ERR(E_UNKNOWN, "%d UART: %s\n", __LINE__, strerror(errno));
			}
			assert(l == sizeof(sa));

			char buf[INET_ADDRSTRLEN];
			if (NULL == inet_ntop(AF_INET, &sa.sin_addr, buf, INET_ADDRSTRLEN)) {
				ERR(E_UNKNOWN, "%d UART: %s\n", __LINE__, strerror(errno));
			}

			INFO("UART connected. Client at %s\n", buf);
		}

		static const char *welcome = "\
>>MSG<< You are now connected to the UART polling device\n\
>>MSG<< Lines prefixed with '>>MSG<<' are sent from this\n\
>>MSG<< UART <--> network bridge, not the connected device\n\
>>MSG<< This device has a "VAL2STR(POLL_UART_BUFSIZE)" byte buffer\n\
>>MSG<< This device operates at "VAL2STR(POLL_UART_BAUD)" baud\n\
>>MSG<< To send a message, simply type and press the return key\n\
>>MSG<< All characters, up to and including the \\n will be sent\n";

		if (-1 == send(client, welcome, strlen(welcome), 0)) {
			ERR(E_UNKNOWN, "%d UART: %s\n", __LINE__, strerror(errno));
		}

		SW_A(&poll_uart_client, client);

		static uint8_t c;
		static int ret;
		while (1) {
			// n.b. If the baud rate is set to a speed s.t. polling
			// becomes CPU intensive (not likely..), this could be
			// replaced with select + self-pipe
			nanosleep(&poll_uart_baud_sleep, NULL);

			if (!poll_uart_enabled) {
				SW_A(&poll_uart_client, INVALID_CLIENT);
				static const char *goodbye = "\
\n\
>>MSG<< An extra newline has been printed before this line\n\
>>MSG<< The polling UART device has shut down. Good bye.\n";
				send(client, goodbye, strlen(goodbye), 0);
				close(client);
				INFO("Polling UART disconnected from client\n");
				INFO("Polling UART device shut down\n");
				pthread_exit(NULL);
			}

			ret = recv(client, &c, 1, MSG_DONTWAIT);

			if (ret != 1) {
				// Common case: poll
				if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
					continue;
				}

				break;
			}

			state_async_block_start();

			uint32_t* head = SRP_AB(&poll_uart_head);
			uint32_t* tail = SRP_AB(&poll_uart_tail);

			DBG1("recv start\thead: %td, tail: %td\n",
					(head)?head - poll_uart_buffer:-1,
					tail - poll_uart_buffer);

			SW_AB(tail, c);
			if (NULL == head) {
				head = tail;
				SWP_AB(&poll_uart_head, head);
			}
			tail++;
			if (tail == (poll_uart_buffer + POLL_UART_BUFSIZE))
				tail = poll_uart_buffer;
			SWP_AB(&poll_uart_tail, tail);

			DBG1("recv end\thead: %td, tail: %td\t[%td=%c]\n",
					(head)?head - poll_uart_buffer:-1,
					tail - poll_uart_buffer,
					tail-poll_uart_buffer-1, *(tail-1));

			state_async_block_end();
		}

		if (ret == 0) {
			INFO("UART client has closed connection"\
				"(no more data in but you can still send)\n");
		} else {
			WARN("Lost connection to UART client (%s)\n", strerror(errno));
			SW_A(&poll_uart_client, INVALID_CLIENT);
		}
	}
}

static uint8_t poll_uart_status_read(void) {
	uint8_t ret = 0;

	state_async_block_start();
	ret |= (SRP_AB(&poll_uart_head) != NULL) << POLL_UART_RXBIT; // data avail?
	ret |= (SR_AB(&poll_uart_client) == INVALID_CLIENT) << POLL_UART_TXBIT; // tx busy?
	state_async_block_end();

	// For lock contention
	nanosleep(&poll_uart_baud_sleep, NULL);

	return ret;
}

static void poll_uart_status_write(uint8_t val) {
	if (val & POLL_UART_RSTBIT) {
		state_async_block_start();
		SWP_AB(&poll_uart_head, NULL);
		SWP_AB(&poll_uart_tail, poll_uart_buffer);
		state_async_block_end();
	}
}

static uint8_t poll_uart_rxdata_read(void) {
	uint8_t ret;

#ifdef DEBUG1
	int idx;
#endif

	state_async_block_start();
	if (NULL == SRP_AB(&poll_uart_head)) {
		DBG1("Poll UART RX attempt when RX Pending was false\n");
		ret = SR_AB(&poll_uart_buffer[3]); // eh... rand? 3, why not?
	} else {
#ifdef DEBUG1
		idx = SRP_AB(&poll_uart_head) - poll_uart_buffer;
#endif
		uint32_t* head = SRP_AB(&poll_uart_head);
		uint32_t* tail = SRP_AB(&poll_uart_tail);

		ret = *head;

		head++;
		if (head == (poll_uart_buffer + POLL_UART_BUFSIZE))
			head = poll_uart_buffer;
		if (head == tail)
			head = NULL;

		DBG1("rxdata end\thead: %td, tail: %td\t[%td=%c]\n",
				(head)?head - poll_uart_buffer:-1,
				tail - poll_uart_buffer,
				(head)?head-poll_uart_buffer:-1,(head)?*head:'\0');

		SWP_AB(&poll_uart_head, head);
	}
	state_async_block_end();

	DBG1("UART read byte: %c %x\tidx: %d\n", ret, ret, idx);

	return ret;
}

static void poll_uart_txdata_write(uint8_t val) {
	DBG1("UART write byte: %c %x\n", val, val);

	static int ret;

	uint32_t client = SR_A(&poll_uart_client);
	if (INVALID_CLIENT == client) {
		DBG1("Poll UART TX ignored as client is busy\n");
		// no connected client (TX is busy...) so drop
	}
	else if (-1 ==  ( ret = send(client, &val, 1, 0))  ) {
		WARN("%d UART: %s\n", __LINE__, strerror(errno));
		SW_A(&poll_uart_client, INVALID_CLIENT);
	}

	DBG2("UART byte sent %c\n", val);
}

//////////////////////////////////////////////////////////////////

// Wrappers to match function signature
static bool uart_read_status(uint32_t addr __attribute__ ((unused)), uint8_t *val) {
	*val = poll_uart_status_read();
	return true;
}

static void uart_write_status(uint32_t addr __attribute__ ((unused)), uint8_t val) {
	poll_uart_status_write(val);
}

static bool uart_read_data(uint32_t addr __attribute__ ((unused)), uint8_t *val) {
	*val = poll_uart_rxdata_read();
	return true;
}

static void uart_write_data(uint32_t addr __attribute__ ((unused)), uint8_t val) {
	poll_uart_txdata_write(val);
}

static void print_poll_uart(void) {
}

static pthread_t start_poll_uart(void *unused __attribute__ ((unused))) {
	// Spawn uart thread, waits until spawned to return
	pthread_mutex_lock(&poll_uart_mutex);
	pthread_create(&poll_uart_pthread, NULL, poll_uart_thread, NULL);
	pthread_cond_wait(&poll_uart_cond, &poll_uart_mutex);
	pthread_mutex_unlock(&poll_uart_mutex);
	return poll_uart_pthread;
}

__attribute__ ((constructor))
void register_memmap_uart(void) {
	union memmap_fn mem_fn;

	mem_fn.R_fn8 = uart_read_status;
	register_memmap("Poll UART", false, 1, mem_fn,
			POLL_UART_STATUS, POLL_UART_STATUS+1);
	mem_fn.W_fn8 = uart_write_status;
	register_memmap("Poll UART", true, 1, mem_fn,
			POLL_UART_STATUS, POLL_UART_STATUS+1);

	mem_fn.R_fn8 = uart_read_data;
	register_memmap("Poll UART", false, 1, mem_fn,
			POLL_UART_RXDATA, POLL_UART_RXDATA+1);

	mem_fn.W_fn8 = uart_write_data;
	register_memmap("Poll UART", true, 1, mem_fn,
			POLL_UART_TXDATA, POLL_UART_TXDATA+1);

	register_periph_printer(print_poll_uart);
	register_periph_thread(start_poll_uart, &poll_uart_enabled);
}
