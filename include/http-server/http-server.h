#if !defined(HTTP_SERVER_HTTP_SERVER_INCLUDED_H_)
#define HTTP_SERVER_HTTP_SERVER_INCLUDED_H_

#include <stddef.h>
#include <sys/queue.h>
#include "http_parser.h"

/**
 * Platform dependent socket type.
 */
typedef int http_server_socket_t;
#define HTTP_SERVER_INVALID_SOCKET -1

#define HTTP_SERVER_POINTER_POINT 100000
#define HTTP_SERVER_FUNCTION_POINT 200000

#if defined(HTTP_SERVER_CINIT)
#undef HTTP_SERVER_CINIT
#endif

#define HTTP_SERVER_CINIT(name, type, value) \
    HTTP_SERVER_OPT_ ## name = HTTP_SERVER_ ## type ## _POINT + value

typedef enum {
    HTTP_SERVER_CINIT(OPEN_SOCKET_FUNCTION, FUNCTION, 1),
    HTTP_SERVER_CINIT(OPEN_SOCKET_DATA, POINTER, 2),
    HTTP_SERVER_CINIT(SOCKET_FUNCTION, FUNCTION, 3),
    HTTP_SERVER_CINIT(SOCKET_DATA, POINTER, 4),
    HTTP_SERVER_CINIT(CLOSE_SOCKET_FUNCTION, FUNCTION, 5),
    HTTP_SERVER_CINIT(CLOSE_SOCKET_DATA, POINTER, 6),
    HTTP_SERVER_CINIT(HANDLER, POINTER, 7),
    HTTP_SERVER_CINIT(HANDLER_DATA, POINTER, 8),
} http_server_option;

/**
 * Callback that will be called whenever http-server requests
 * new socket. At this point it is up to user to set socket's
 * purpse. It could be UDP or TCP (or some abstract type)
 * @param clientp User defined pointer
 */
typedef http_server_socket_t (*http_server_opensocket_callback)(void * clientp);

/**
 * Callback that will be called whenever http-server stops using
 * a socket. Typically this should call close(2).
 */
typedef int (*http_server_closesocket_callback)(http_server_socket_t sock, void * clientp);

/**
 * Called when http-server requests some poll.
 * @param clientp Client pointer
 * @param sock Socket
 * @param flags Some mix of flags {HTTP_SERVER_POLL_IN,HTTP_SERVER_POLL_OUT}
 * @param socketp Some custom data assigned to socket (could be NULL)
 */
typedef int (*http_server_socket_callback)(void * clientp, http_server_socket_t sock, int flags, void * socketp);

// Request handler

struct http_server_client;

/** Received a chunk of URL */
typedef int (*http_server_handler_data_cb)(struct http_server_client * client, void * data, const char * buf, size_t size);

/** Callback without args */
typedef int (*http_server_handler_cb)(struct http_server_client * client, void * data);


typedef struct http_server_handler
{
    void * data;
    http_server_handler_data_cb on_url;
    void * on_url_data;
    http_server_handler_cb on_message_complete;
    void * on_message_complete_data;
} http_server_handler;

typedef struct http_server_buf
{
    char * mem; // Memory
    char * data; // Actual data (mem > data is possible)
    int size;
    TAILQ_ENTRY(http_server_buf) bufs;
} http_server_buf;

struct http_server_header
{
    TAILQ_ENTRY(http_server_header) headers;
    char * key;
    int key_size;
    char * value;
    int value_size;
};

/**
 * HTTP rresponse object
 */
typedef struct http_server_response
{
    // Owner. Could be NULL in case the response is not tied to
    // a particular client.
    struct http_server_client * client;
    TAILQ_HEAD(slisthead2, http_server_buf) buffer;
    int headers_sent; // are headers sent yet?
    TAILQ_HEAD(http_server_headers, http_server_header) headers;
    int is_chunked;
    int is_done; // is response done?
} http_server_response;

struct http_server;

/**
 * Represents single HTTP client connection.
 */
typedef struct http_server_client
{
    http_server_socket_t sock;
    void * data;
    SLIST_ENTRY(http_server_client) next;
    // private:
    http_parser_settings parser_settings_;
    http_parser parser_; // private
    http_server_handler * handler;
    http_server_response * current_response_;
    struct http_server * server_;
    int current_flags; // current I/O poll flags
    int is_complete; // request is complete
} http_server_client;

typedef struct http_server
{
    /**
     * Socket listener. This is the socket that listens for connections.
     */
    http_server_socket_t sock_listen;
    /**
     * This is what user assigns
     */
    void * sock_listen_data;
    /**
     * Callback that user provides and should return new low-level
     * socket.
     */
    http_server_opensocket_callback opensocket_func;
    /**
     * Custom data pointer that user provides.
     */
    void * opensocket_data;
    /**
     * Callback that will be called whenever http-server decides
     * to close a socket.
     */
    http_server_closesocket_callback closesocket_func;
    /**
     * Custom data pointer that user provides for close socket callback.
     */
    void * closesocket_data;
    /**
     * Called when http-server internals wants some async action
     */
    http_server_socket_callback socket_func;
    /**
     * Custom data pointer for `socket_func` that user provides
     */
    void * socket_data;
    /**
     * All connected clients
     */
    SLIST_HEAD(slisthead, http_server_client) clients;
    /**
     * Handler for all connections
     */
    http_server_handler * handler_;
    /**
     * Current HTTP response
     */
    http_server_response * response_;
    /**
     * Private holder for holding current event loop pointer
     */
    void * event_loop_;
    /**
     * Private custom data for the event loop
     */
    void * event_loop_data_;
} http_server;

#define HTTP_SERVER_ENUM_ERROR_CODES(XX) \
    XX(OK, 0, "Success") \
    XX(SOCKET_ERROR, 1, "Invalid socket") \
    XX(NOTIMPL, 2, "Not implemented error") \
    XX(SOCKET_EXISTS, 3, "Socket is already managed") \
    XX(INVALID_PARAM, 4, "Invalid parameter") \
    XX(CLIENT_EOF, 5, "End of file") \
    XX(PARSER_ERROR, 6, "Unable to parse HTTP request")

#define HTTP_SERVER_ENUM_ERRNO(name, val, descr) \
    HTTP_SERVER_ ## name = val,

// Error codes
typedef enum {
    HTTP_SERVER_ENUM_ERROR_CODES(HTTP_SERVER_ENUM_ERRNO)
} http_server_errno;

#undef HTTP_SERVER_ENUM_ERRNO

// Poll for reading
#define HTTP_SERVER_POLL_IN 1<<1
// Poll for writing
#define HTTP_SERVER_POLL_OUT 1<<2
// Poll to be removed for further polling
#define HTTP_SERVER_POLL_REMOVE 1<<3

/**
 * Sets up HTTP server instance private fields.
 */
int http_server_init(http_server * srv);

/**
 * Cleans up HTTP server instance private fields.
 */
void http_server_free(http_server * srv);

/**
 * String description of error.
 * @param e Error code
 * @return Description as string
 */
char * http_server_errstr(http_server_errno e);

/**
 * Sets options for http_server instance
 */
int http_server_setopt(http_server * srv, http_server_option opt, ...);

/**
 * Starts http server. Assumes all options are configured. This will
 * create sockets.
 */
int http_server_start(http_server * srv);

/**
 * Cancels the http server and no new connections will be accepted.
 * @param srv Server
 */
int http_server_cancel(http_server * srv);

/**
 * Blocks and serves connections
 */
int http_server_run(http_server * srv);

/**
 * Assigns pointer to a socket
 * @param sock Socket
 * @param data Data
 */
int http_server_assign(http_server * srv, http_server_socket_t sock, void * data);

/**
 * Adds new client to manage.
 * @param srv Server
 * @param sock Socket
 */
int http_server_add_client(http_server * srv, http_server_socket_t sock);

/**
 * Remove client from the list
 * @param srv Server
 * @param sock Socket
 */
int http_server_pop_client(http_server * srv, http_server_socket_t sock);

/**
 * Perform action on a socket. Usually called after receiving some async event.
 * @param srv Server instance
 * @param socket Socket object
 * @param flags Flags (mix of flags: HTTP_SERVER_POLL_IN|HTTP_SERVER_POLL_OUT)
 */
int http_server_socket_action(http_server * srv, http_server_socket_t socket, int flags);

/**
 * Create new HTTP client instance
 */
http_server_client * http_server_new_client(http_server * server, http_server_socket_t sock, http_server_handler * handler);

/**
 * Feeds client using chunk of datad    
 */
int http_server_perform_client(http_server_client * client, const char * at, size_t size);

/**
 * Calls for I/O poll on a client
 * @private
 */
int http_server_poll_client(http_server_client * client, int flags);

// Response API
#define HTTP_SERVER_ENUM_STATUS_CODES(XX) \
    XX(100, CONTINUE, "Continue") \
    XX(101, SWITCHING_PROTOCOLS, "Switching Protocols") \
    XX(200, OK, "OK") \
    XX(201, CREATED, "Created") \
    XX(202, ACCEPTED, "Accepted") \
    XX(203, NON_AUTHORITATIVE_INFORMATION,"Non-Authoritative Information") \
    XX(204, NO_CONTENT, "No Content") \
    XX(205, RESET_CONTENT, "Reset Content") \
    XX(206, PARTIAL_CONTENT, "Partial Content") \
    XX(300, MULTIPLE_CHOICES, "Multiple Choices") \
    XX(301, MOVED_PERMANENTLY, "Moved Permanently") \
    XX(302, FOUND, "Found") \
    XX(303, SEE_OTHER, "See Other") \
    XX(304, NOT_MODIFIED, "Not Modified") \
    XX(305, USE_PROXY, "Use Proxy") \
    XX(307, TEMPORARY_REDIRECT, "Temporary Redirect") \
    XX(400, BAD_REQUEST, "Bad Request") \
    XX(401, UNAUTHORIZED, "Unauthorized") \
    XX(402, PAYMENT_REQUIRED, "Payment Required") \
    XX(403, FORBIDDEN, "Forbidden") \
    XX(404, NOT_FOUND, "Not Found") \
    XX(405, METHOD_NOT_ALLOWED, "Method Not Allowed") \
    XX(406, NOT_ACCEPTABLE, "Not Acceptable") \
    XX(407, PROXY_AUTHENTICATION_REQUIRED, "Proxy Authentication Required") \
    XX(408, REQUEST_TIMEOUT, "Request Timeout") \
    XX(409, CONFLICT, "Conflict") \
    XX(410, GONE, "Gone") \
    XX(411, LENGTH_REQUIRED, "Length Required") \
    XX(412, PRECONDITION_FAILED, "Precondition Failed") \
    XX(413, REQUEST_ENTITY_TOO_LARGE, "Request Entity Too Large") \
    XX(414, REQUEST_URI_TOO_LONG, "Request-URI Too Long") \
    XX(415, UNSUPPORTED_MEDIA_TYPE, "Unsupported Media Type") \
    XX(416, REQUESTED_RANGE_NOT_SATISFIABLE, "Requested Range Not Satisfiable") \
    XX(417, EXPECTATION_FAILED, "Expectation Failed") \
    XX(500, INTERNAL_SERVER_ERROR, "Internal Server Error") \
    XX(501, NOT_IMPLEMENTED, "Not Implemented") \
    XX(502, BAD_GATEWAY, "Bad Gateway") \
    XX(503, SERVICE_UNAVAILABLE, "Service Unavailable") \
    XX(504, GATEWAY_TIMEOUT, "Gateway Timeout") \
    XX(505, HTTP_VERSION_NOT_SUPPORTED, "HTTP Version Not Supported")

typedef enum
{
#define XX(status_code, name, description) HTTP_ ## status_code ## _ ## name = status_code,
    HTTP_SERVER_ENUM_STATUS_CODES(XX)
#undef XX
} http_server_status_code;

/**
 * Creates new response object
 */
http_server_response * http_server_response_new();

/**
 * Free response and all associated
 */
void http_server_response_free(http_server_response * res);

/**
 * Queues response to the client.
 */
int http_server_response_begin(http_server_client * client, http_server_response * res);

/**
 * Response is done.
 */
int http_server_response_end(http_server_response * client);

/**
 * Start sending the response.
 * User should not call this directly.
 */
int http_server_response__flush(http_server_response * res);

/**
 * Write response header
 */
int http_server_response_write_head(http_server_response * res, int status_code);

/**
 * Sets header in response
 */
int http_server_response_set_header(http_server_response * res, char * name, int namelen, char * value, int valuelen);

/**
 * Write some data to the responses
 * @param res Response
 * @param data Data
 * @param size Size
 */
int http_server_response_write(http_server_response * res, char * data, int size);

/**
 * Write some data to the response similiar to a printf(3) call.
 */
int http_server_response_printf(http_server_response * res, const char * format, ...);


#endif
