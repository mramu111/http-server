#include <cassert>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

extern "C" {
#include "http-server/http-server.h"
}

struct http_error_category
    : boost::system::error_category
{
    const char * name() const
    {
        return "http_server";
    }
    std::string message(int rc) const
    {
        char * str = http_server_errstr(static_cast<http_server_errno>(rc));
        assert(str);
        return str;
    }
};

inline boost::system::error_category & get_http_error_category()
{
    static http_error_category category;
    return category;
}

class http_server_service
    : public boost::asio::io_service::service
{
public:
    static boost::asio::io_service::id id;
    http_server_service(boost::asio::io_service & io_service)
        : boost::asio::io_service::service(io_service)
        , srv_()
        , handler_()
    {
        http_server_init(&srv_);
        // srv_.sock_listen_data = NULL; // should be null by default
        int result;
        // Set callbacks
        result = http_server_setopt(&srv_, HTTP_SERVER_OPT_OPEN_SOCKET_DATA, this);
        if (result != HTTP_SERVER_OK)
        {
            throw boost::system::system_error(result, get_http_error_category(), "http_server_setopt");
        }
        result = http_server_setopt(&srv_, HTTP_SERVER_OPT_OPEN_SOCKET_FUNCTION, &opensocket_function);
        if (result != HTTP_SERVER_OK)
        {
            throw boost::system::system_error(result, get_http_error_category(), "http_server_setopt");
        }
        result = http_server_setopt(&srv_, HTTP_SERVER_OPT_CLOSE_SOCKET_DATA, this);
        if (result != HTTP_SERVER_OK)
        {
            throw boost::system::system_error(result, get_http_error_category(), "http_server_setopt");
        }
        result = http_server_setopt(&srv_, HTTP_SERVER_OPT_CLOSE_SOCKET_FUNCTION, &closesocket_function);
        if (result != HTTP_SERVER_OK)
        {
            throw boost::system::system_error(result, get_http_error_category(), "http_server_setopt");
        }

        result = http_server_setopt(&srv_, HTTP_SERVER_OPT_SOCKET_DATA, this);
        if (result != HTTP_SERVER_OK)
        {
            throw boost::system::system_error(result, get_http_error_category(), "http_server_setopt");
        }
        result = http_server_setopt(&srv_, HTTP_SERVER_OPT_SOCKET_FUNCTION, &socket_function);
        if (result != HTTP_SERVER_OK)
        {
            throw boost::system::system_error(result, get_http_error_category(), "http_server_setopt");
        }
        // Prepare handler object
        handler_.on_message_complete = &on_message_complete;
        handler_.on_message_complete_data = this;

        result = http_server_setopt(&srv_, HTTP_SERVER_OPT_HANDLER, &handler_);
        if (result != HTTP_SERVER_OK)
        {
            throw boost::system::system_error(result, get_http_error_category(), "http_server_setopt");
        }
        result = http_server_setopt(&srv_, HTTP_SERVER_OPT_HANDLER_DATA, this);
        if (result != HTTP_SERVER_OK)
        {
            throw boost::system::system_error(result, get_http_error_category(), "http_server_setopt");   
        }
    }
    void shutdown_service()
    {
    }
    void start()
    {
        int result = http_server_start(&srv_);
        if (result != HTTP_SERVER_OK)
        {
            throw boost::system::system_error(result, get_http_error_category(), "http_server_start");   
        }
    }
private:
    static http_server_socket_t opensocket_function(void * clientp)
    {
        http_server_service * svc = static_cast<http_server_service *>(clientp);
        boost::asio::ip::tcp::acceptor * acceptor = new boost::asio::ip::tcp::acceptor(svc->get_io_service());
        boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), 5000);
        acceptor->open(endpoint.protocol());
        acceptor->set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        acceptor->bind(endpoint);
        acceptor->listen();

        fprintf(stderr, "new acceptor: %d\n", acceptor->native_handle());
        svc->acceptors_.insert(std::make_pair(static_cast<http_server_socket_t>(acceptor->native_handle()), acceptor));
        return acceptor->native_handle();
    }
    static int closesocket_function(http_server_socket_t socket, void * clientp)
    {
        http_server_service * svc = static_cast<http_server_service *>(clientp);
        http_server_service::acceptors_t::iterator it = svc->acceptors_.find(socket);
        if (it != svc->acceptors_.end())
        {
            fprintf(stderr, "erase asio acceptor socket %d\n", it->first);
            it->second->cancel();
            delete it->second;
            svc->acceptors_.erase(it);
            return HTTP_SERVER_OK;
        }
        http_server_service::sockets_t::iterator it2 = svc->sockets_.find(socket);
        if (it2 != svc->sockets_.end())
        {
            fprintf(stderr, "erase asio client socket %d\n", it2->first);
            it2->second->cancel();
            delete it2->second;
            svc->sockets_.erase(it2);
            return HTTP_SERVER_OK;
        }
        fprintf(stderr, "close socket invalid socket: %d\n", socket);
        return HTTP_SERVER_INVALID_SOCKET;
    }
    static int socket_function(void * clientp, http_server_socket_t sock, int flags, void * socketp)
    {
        http_server_service * svc = static_cast<http_server_service *>(clientp);
        http_server_service::acceptors_t::iterator it = svc->acceptors_.find(sock);
        if (it != svc->acceptors_.end())
        {
            // Call acceptor
            fprintf(stderr, "async_accept on acceptor %d\n", it->second->native_handle());
            // Prepare new socket
            boost::asio::ip::tcp::socket * new_socket = new boost::asio::ip::tcp::socket(svc->get_io_service());
            it->second->async_accept(*new_socket,
                boost::bind(&http_server_service::handle_accept, svc,
                    boost::asio::placeholders::error,
                    new_socket));
            return 0;
        }
        fprintf(stderr, "clientp=%p sock=%d flags=%d socketp=%p\n", clientp, sock, flags, socketp);
        http_server_service::sockets_t::iterator it2 = svc->sockets_.find(sock);
        if (it2 == svc->sockets_.end())
        {
            fprintf(stderr, "socket %d not found!\n", sock);
            //abort();
            return 0;
        }
        if (flags & HTTP_SERVER_POLL_REMOVE)
        {
            fprintf(stderr, "cancel asio socket %d\n", sock);
            it2->second->cancel();
        }
        if (flags & HTTP_SERVER_POLL_IN)
        {
            it2->second->async_read_some(boost::asio::null_buffers(),
                boost::bind(&http_server_service::handle_read, svc,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred,
                    it2->second));
        }
        if (flags & HTTP_SERVER_POLL_OUT)
        {
            it2->second->async_write_some(boost::asio::null_buffers(),
                boost::bind(&http_server_service::handle_write, svc,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred,
                    it2->second));
        }
        return 0;
    }
    void handle_accept(const boost::system::error_code & error,
        boost::asio::ip::tcp::socket * socket)
    {
        if (error)
        {
            fprintf(stderr, "unable to accept new connection: %s\n", error.message().c_str());
            // TODO: Remove socket?
            return;
        }
        fprintf(stderr, "handle new conn %d\n", socket->native_handle());
        
        sockets_.insert(std::make_pair(
            static_cast<http_server_socket_t>(socket->native_handle()),
            socket));

        int result = http_server_socket_action(&srv_, socket->native_handle(), 0);
        if (result != HTTP_SERVER_OK)
        {
            sockets_.erase(socket->native_handle());
            throw boost::system::system_error(result, get_http_error_category(), "http_server_socket_action");   
        }
    }
    void handle_read(const boost::system::error_code & error,
        std::size_t bytes_transferred,
        boost::asio::ip::tcp::socket * socket)
    {
        if (error)
        {
            fprintf(stderr, "Unable to read data from socket: %s", error.message().c_str());
            return;
        }
        fprintf(stderr, "handle read %d\n", socket->native_handle());
        int result = http_server_socket_action(&srv_, socket->native_handle(), HTTP_SERVER_POLL_IN);
        if (result != HTTP_SERVER_OK)
        {
            if (result != HTTP_SERVER_CLIENT_EOF)
            {
                throw boost::system::system_error(result, get_http_error_category(), "http_server_socket_action");
            }
        }
    }
    void handle_write(const boost::system::error_code & error,
        std::size_t bytes_transferred,
        boost::asio::ip::tcp::socket * socket)
    {
        if (error)
        {
            fprintf(stderr, "Unable to write data to socket: %s", error.message().c_str());
            return;
        }
        fprintf(stderr, "handle write %d\n", socket->native_handle());
        int result = http_server_socket_action(&srv_, socket->native_handle(), HTTP_SERVER_POLL_OUT);
        if (result != HTTP_SERVER_OK)
        {
            throw boost::system::system_error(result, get_http_error_category(), "http_server_socket_action");
        }
    }
    static int on_message_complete(http_server_client * client, void * data)
    {
        return static_cast<http_server_service *>(client->handler->data)->handle_message(client, data);
    }
    int handle_message(http_server_client * client, void * data)
    {
        // Write response
        http_server_response * res = http_server_response_new();
        

        int result = http_server_response_begin(client, res);
        if (result != HTTP_SERVER_OK)
        {
            throw boost::system::system_error(result, get_http_error_category(), "http_server_response_begin");   
        }
        // Start response by writing header
        result = http_server_response_write_head(res, 200);
        if (result != HTTP_SERVER_OK)
        {
            throw boost::system::system_error(result, get_http_error_category(), "http_server_response_write_head");   
        }
        char * info_url;
        result = http_server_client_getinfo(client, HTTP_SERVER_CLIENTINFO_URL, &info_url);
        if (result != HTTP_SERVER_OK)
        {
            throw boost::system::system_error(result, get_http_error_category(), "http_server_client_getinfo");   
        }
        assert(info_url);
        std::string url = info_url;
        // Send streaming response
        if (url == "/stream/")
        {
            boost::shared_ptr<boost::asio::deadline_timer> timer =
                boost::make_shared<boost::asio::deadline_timer>(boost::ref(get_io_service()));
            timer->expires_from_now(boost::posix_time::seconds(1));
            timer->async_wait(
                boost::bind(&http_server_service::stream_response, this,
                    boost::asio::placeholders::error,
                    client,
                    data,
                    res,
                    0,
                    timer));
            return 0;
        }
        // Send message
        char chunk[1024];
        int length = sprintf(chunk, "Hello world!\n");
        result = http_server_response_write(res, chunk, length);
        if (result != HTTP_SERVER_OK)
        {
            throw boost::system::system_error(result, get_http_error_category(), "http_server_response_write");   
        }
        length = sprintf(chunk, "URL: %s!\n", url.c_str());
        result = http_server_response_write(res, chunk, length);
        if (result != HTTP_SERVER_OK)
        {
            throw boost::system::system_error(result, get_http_error_category(), "http_server_response_write");   
        }
        // Finish response
        result = http_server_response_end(res);
        if (result != HTTP_SERVER_OK)
        {
            throw boost::system::system_error(result, get_http_error_category(), "http_server_response_end");   
        }
        return 0;
    }
    void stream_response(const boost::system::error_code & ec,
        http_server_client * client,
        void * data,
        http_server_response * res,
        int counter,
        boost::shared_ptr<boost::asio::deadline_timer> timer)
    {
        if (ec)
        {
            fprintf(stderr, "stream response fail %s\n", ec.message().c_str());
            return;
        }
        ++counter;
        if (counter > 10)
        {
            http_server_response_end(res);
            //delete req;
            client->data = NULL;
            return;
        }
        else
        {
            http_server_response_printf(res, "Hello world %d!\n", counter);
        }
        timer->expires_from_now(boost::posix_time::seconds(1));
        timer->async_wait(
            boost::bind(&http_server_service::stream_response, this,
                boost::asio::placeholders::error,
                client,
                data,
                res,
                counter,
                timer));
    }
    http_server srv_;
    http_server_handler handler_;
    // Map of open sockets
    typedef std::map<http_server_socket_t, boost::asio::ip::tcp::acceptor *> acceptors_t;
    acceptors_t acceptors_;
    typedef std::map<http_server_socket_t, boost::asio::ip::tcp::socket *> sockets_t;
    sockets_t sockets_;
};

boost::asio::io_service::id http_server_service::id;

int
main(int argc, char * argv[])
{
    int port;
    boost::asio::io_service io_service;
    boost::asio::use_service<http_server_service>(io_service)
        .start();
    io_service.run();
}