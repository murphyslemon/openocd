//
// Created by murph on 03/03/2025.
//
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <helper/system.h>
#include <helper/log.h>
#include <helper/replacements.h>
#include <jtag/jtag.h>	/* ERROR_JTAG_DEVICE_ERROR only */

#include <winsock2.h>    // For Windows socket functions
#include <ws2tcpip.h>    // For getaddrinfo and getnameinfo
#include <sys/types.h>   // For socket types


#include "cmsis_dap.h"

enum {
        CMSIS_DAP_TRANSFER_PENDING = 0,	/* must be 0, used in libusb_handle_events_completed */
        CMSIS_DAP_TRANSFER_IDLE,
        CMSIS_DAP_TRANSFER_COMPLETED
};

struct cmsis_dap_backend_data {
    int socket_fd;  // TCP socket descriptor
    struct sockaddr_in server_addr;  // Server (debugger) address

    uint8_t command_buffer[MAX_PENDING_REQUESTS][64];  // Command buffer
    uint8_t response_buffer[MAX_PENDING_REQUESTS][64]; // Response buffer

    unsigned int pending_requests;  // Track pending requests
};
/*
static char *cmsis_dap_tcp_host = NULL;  // NULL means no host is set initially
static int cmsis_dap_tcp_port = 0;       // 0 means no port is set initially
*/

static void cmsis_dap_tcp_close(struct cmsis_dap *dap);
int cmsis_dap_tcp_packet_alloc(struct cmsis_dap *dap, unsigned int pkt_sz);
void cmsis_dap_tcp_packet_free(struct cmsis_dap *dap);

static int cmsis_dap_tcp_open(struct cmsis_dap *dap, uint16_t vids[], uint16_t pids[], const char *serial)
{
    struct sockaddr_in server_addr;

    dap->bdata = calloc(1, sizeof(struct cmsis_dap_backend_data));
    if (!dap->bdata)
        return ERROR_FAIL;

    dap->bdata->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (dap->bdata->socket_fd < 0) {
        LOG_ERROR("Failed to create socket");
        return ERROR_FAIL;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(50372);  // Change port as needed
    inet_pton(AF_INET, "192.168.85.191", &server_addr.sin_addr.s_addr);  // Change to debugger's IP

    if (connect(dap->bdata->socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("Failed to connect to CMSIS-DAP debugger over TCP");
        close(dap->bdata->socket_fd);
        return ERROR_FAIL;
    }

    LOG_INFO("CMSIS-DAP: Interface Initialised (TCP)");
    unsigned int packet_size = 512U;
    int err = cmsis_dap_tcp_packet_alloc(dap, packet_size);
    if (err != ERROR_OK)
        cmsis_dap_tcp_close(dap);
    return ERROR_OK;
}

static void cmsis_dap_tcp_close(struct cmsis_dap *dap)
{
    if (!dap || !dap->bdata)
        return;

    // Close the socket connection
    if (dap->bdata->socket_fd >= 0) {
#ifdef _WIN32
        closesocket(dap->bdata->socket_fd);
#else
        close(dap->bdata->socket_fd);
#endif
        dap->bdata->socket_fd = -1;
    }

    // Free the allocated memory
    free(dap->bdata);
    dap->bdata = NULL;
}

static int cmsis_dap_tcp_read(struct cmsis_dap *dap, int transfer_timeout_ms, enum cmsis_dap_blocking blocking)
{
    fd_set read_fds;
    struct timeval timeout;
    int ret = 0;
    transfer_timeout_ms *= 4;  // Increase timeout for TCP
    FD_ZERO(&read_fds);
    FD_SET(dap->bdata->socket_fd, &read_fds);

    timeout.tv_sec = transfer_timeout_ms / 1000;
    timeout.tv_usec = (transfer_timeout_ms % 1000) * 1000;

    if (blocking == CMSIS_DAP_BLOCKING) {
        ret = select(dap->bdata->socket_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ret <= 0) {
            LOG_ERROR("Timeout or error while waiting for CMSIS-DAP over TCP");
            return ERROR_FAIL;
        }
    }

    uint8_t buffer[dap->packet_buffer_size];
    int received = recv(dap->bdata->socket_fd, (char *)buffer, dap->packet_size, 0);
    memcpy(dap->packet_buffer, buffer, received);
    memset(&dap->packet_buffer[received], 0, (dap->packet_buffer_size - received));
    LOG_DEBUG_IO("Read %d bytes from CMSIS-DAP over TCP", received);
    return received;
}

static int cmsis_dap_tcp_write(struct cmsis_dap *dap, int txlen, int timeout_ms)
{
    //LOG_ERROR("tcp_write: packet size: %u, txlen: %d, timeout: %d", dap->packet_size, txlen, timeout_ms);
    if (!dap || !dap->bdata)
        return ERROR_FAIL;
    timeout_ms *= 4;  // Increase timeout for TCP
    int total_bytes_sent = 0;
    int bytes_sent;
    struct timeval timeout;
    fd_set write_fds;

    FD_ZERO(&write_fds);
    FD_SET(dap->bdata->socket_fd, &write_fds);

    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    // Check if the socket is writable within the timeout
    int ret = select(dap->bdata->socket_fd + 1, NULL, &write_fds, NULL, &timeout);
    if (ret <= 0) {
        LOG_ERROR("Timeout or error while writing to CMSIS-DAP over TCP");
        return ERROR_FAIL;
    }

    // Send data over TCP
    while (total_bytes_sent < txlen) {
        bytes_sent = send(dap->bdata->socket_fd,
                          (const char *)(dap->packet_buffer + total_bytes_sent),
                          txlen - total_bytes_sent, 0);

        if (bytes_sent <= 0) {
            LOG_ERROR("Failed to send data to CMSIS-DAP over TCP");
            return ERROR_FAIL;
        }

        total_bytes_sent += bytes_sent;
    }

    return ERROR_OK;
}

int cmsis_dap_tcp_packet_alloc(struct cmsis_dap *dap, unsigned int pkt_sz)
{
    // Allocate the main packet buffer
    dap->packet_buffer = malloc(pkt_sz);
    if (!dap->packet_buffer) {
        LOG_ERROR("unable to allocate CMSIS-DAP packet buffer");
        return ERROR_FAIL;
    }

    dap->packet_size = pkt_sz;
    dap->packet_buffer_size = pkt_sz;
    dap->packet_usable_size = pkt_sz - 1; // Prevent sending zero-size packets

    dap->command = dap->packet_buffer;
    dap->response = dap->packet_buffer;

    return ERROR_OK;
}

void cmsis_dap_tcp_packet_free(struct cmsis_dap *dap)
{
    // Free the allocated packet buffer
    free(dap->packet_buffer);
    dap->packet_buffer = NULL;
    dap->command = NULL;
    dap->response = NULL;
}

void cmsis_dap_tcp_cancel_all(struct cmsis_dap *dap)
{
    LOG_INFO("Cancelling all pending requests for CMSIS-DAP debugger over TCP");

    if (dap->bdata->socket_fd >= 0) {
        shutdown(dap->bdata->socket_fd, SD_BOTH); // Stop any ongoing transmission on Windows
        close(dap->bdata->socket_fd);
        dap->bdata->socket_fd = -1; // Mark as closed
    }
}

/*
COMMAND_HANDLER(cmsis_dap_handle_tcp_config_command)
        {
                if (CMD_ARGC == 2) {
                    COMMAND_PARSE_NUMBER(int, CMD_ARGV[1], cmsis_dap_tcp_port);
                    cmsis_dap_tcp_host = strdup(CMD_ARGV[0]);
                } else {
                    LOG_ERROR("expected exactly two arguments: cmsis_dap_tcp_config <host> <port>");
                }

                return ERROR_OK;
        }

const struct command_registration cmsis_dap_tcp_subcommand_handlers[] = {
        {
                .name = "config",
                .handler = &cmsis_dap_handle_tcp_config_command,
                .mode = COMMAND_CONFIG,
                .help = "set the TCP host and port to use",
                .usage = "<host> <port>",
        },
        COMMAND_REGISTRATION_DONE
};
*/

const struct cmsis_dap_backend cmsis_dap_tcp_backend = {
        .name = "tcp",
        .open = cmsis_dap_tcp_open,
        .close = cmsis_dap_tcp_close,
        .read = cmsis_dap_tcp_read,
        .write = cmsis_dap_tcp_write,
        .packet_buffer_alloc = cmsis_dap_tcp_packet_alloc,
        .packet_buffer_free = cmsis_dap_tcp_packet_free,
        .cancel_all = cmsis_dap_tcp_cancel_all
};

