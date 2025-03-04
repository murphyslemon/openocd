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


int cmsis_dap_tcp_open(struct cmsis_dap *dap, uint16_t vids[], uint16_t pids[], const char *serial)
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

    // send a message to the server
    const char *message = "Hello, server!";
    send(dap->bdata->socket_fd, message, strlen(message), 0);
    // TO-DO: verify the initialisation of in and out endpoints
    LOG_INFO("CMSIS-DAP: Interface Initialised (TCP)");
    // TO-DO

    return ERROR_OK;
}

void cmsis_dap_tcp_close(struct cmsis_dap *dap)
{
    close(dap->bdata->socket_fd);
    free(dap->bdata);
}

int cmsis_dap_tcp_read(struct cmsis_dap *dap, int transfer_timeout_ms, enum cmsis_dap_blocking blocking)
{
    int transferred = -4;
    LOG_INFO("Reading from CMSIS-DAP debugger over TCP");
    return transferred;
}

int cmsis_dap_tcp_write(struct cmsis_dap *dap, int len, int timeout_ms)
{
    LOG_INFO("Writing to CMSIS-DAP debugger over TCP");
    return ERROR_FAIL;
}

int cmsis_dap_tcp_packet_alloc(struct cmsis_dap *dap, unsigned int pkt_sz)
{
    LOG_INFO("Allocating packet buffer for CMSIS-DAP debugger over TCP");
    return ERROR_FAIL;
}

void cmsis_dap_tcp_packet_free(struct cmsis_dap *dap)
{
    LOG_INFO("Freeing packet buffer for CMSIS-DAP debugger over TCP");
}

void cmsis_dap_tcp_cancel_all(struct cmsis_dap *dap)
{
    LOG_INFO("Cancelling all pending requests for CMSIS-DAP debugger over TCP");
}


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

