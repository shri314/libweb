#include <boost/asio/buffer.hpp>
#include <boost/asio/ssl/context.hpp>
#include <cstddef>
#include <memory>


#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

#include <boost/config.hpp>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using tcp           = boost::asio::ip::tcp;      // from <boost/asio/ip/tcp.hpp>
namespace ssl       = boost::asio::ssl;          // from <boost/asio/ssl.hpp>
namespace http      = boost::beast::http;        // from <boost/beast/http.hpp>
namespace websocket = boost::beast::websocket;   // from <boost/beast/websocket.hpp>

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template <class Body, class Allocator, class Sender>
void handle_request(http::request<Body, http::basic_fields<Allocator>>&& req, Sender& sender)
{
   // Build the path to the requested file
   auto&& tgt = req.target();
   std::string path{tgt.begin(), tgt.end()};

   std::ostringstream oss;
   oss << "{\"method\":\"GET\", \"data\":\"Hello! World\", \"path\": " << std::quoted(path) << "}\r\n";

   http::response<http::string_body> res{http::status::ok, req.version()};
   res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
   res.set(http::field::content_type, "application/json");
   res.keep_alive(req.keep_alive());
   res.body() = oss.str();
   res.prepare_payload();

   sender(std::move(res));
}

//------------------------------------------------------------------------------
inline void load_server_certificate(boost::asio::ssl::context& ctx)
{
   /*
       The certificate was generated from CMD.EXE on Windows 10 using:

       winpty openssl dhparam -out dh.pem 2048
       winpty openssl req -newkey rsa:2048 -nodes -keyout key.pem -x509 -days 10000 -out cert.pem -subj "//C=US\ST=CA\L=Los Angeles\O=Beast\CN=www.example.com"
   */

   std::string const cert = R"(-----BEGIN CERTIFICATE-----
MIIDizCCAnOgAwIBAgIJAMD97Z/i0vy6MA0GCSqGSIb3DQEBCwUAMFwxCzAJBgNV
BAYTAklOMQ0wCwYDVQQIDARNYWhhMQ0wCwYDVQQHDARQdW5lMQswCQYDVQQKDAJG
SDELMAkGA1UECwwCRkgxFTATBgNVBAMMDGVsZWN0cm9uLXUxNjAeFw0xODEwMTUw
ODIyNTNaFw00NjAzMDIwODIyNTNaMFwxCzAJBgNVBAYTAklOMQ0wCwYDVQQIDARN
YWhhMQ0wCwYDVQQHDARQdW5lMQswCQYDVQQKDAJGSDELMAkGA1UECwwCRkgxFTAT
BgNVBAMMDGVsZWN0cm9uLXUxNjCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoC
ggEBAOmSQrIhxv7uqmGnejbQsqaaktsPr6iBxjtojm9ta9Voz5+LSflx8xJr6jTA
sTWHAriBIXKZ1KOmXsbn7ct5dw5KJjb6+b6K28+dMAdwbYM0J49rEWz7xCY3M8oJ
clkKBsTRjUDkrpoHwrewUSSClQTVWqPHSc3EfyyXtQadZFhGgQx6fuDB6vEDnGS3
p1LvMTDAR+Hx9Rrrdjh4dzUjQjLEk6j4cTRy10AR675JPGDVW4Xfb15tcGuczUhq
Jx1GZRwMCK9e52TB+JZsu2n450fFSJ28dSroHRjuh+z83IU9MOzrJcGgP8A6v//R
dzfHj8SZv/cldPSDH94Xdgu3+c8CAwEAAaNQME4wHQYDVR0OBBYEFJZpwPCzmO4h
hW+aFoQQc5E2TzaUMB8GA1UdIwQYMBaAFJZpwPCzmO4hhW+aFoQQc5E2TzaUMAwG
A1UdEwQFMAMBAf8wDQYJKoZIhvcNAQELBQADggEBAJmiQ4DlC3j7EQUnzXAaW+x7
CGgt8uyodpHUCSHmHXqBr605Q0LMQthe0vro/8zXlhOngvbNvV4QhIXcm8DBTlGr
ldKQrjy7nv9iPbbmTpqINjYMPx+WQDQRPbgRdmm5hbA56bWXaQtnJ3Ffe6pukOSl
0kW+r/yrqjbKsGvSFX+pSMqFBX68HleYtEfDovIIUUmIlvtRz3oWPD3f227x9p9D
o10+dGeeWNBjfQbw12C5iQvesc+9sfBQfqDnCMvsrx8edp9nUu8EhGnZC9PbJyTx
Vqo6EpFifoaABa222iuyNr1IeI0JxTISNpRMgAqJ93t3SJuuii+SiF08iryKJJ0=
-----END CERTIFICATE-----)";

   std::string const key = R"(-----BEGIN PRIVATE KEY-----
MIIEvwIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQDpkkKyIcb+7qph
p3o20LKmmpLbD6+ogcY7aI5vbWvVaM+fi0n5cfMSa+o0wLE1hwK4gSFymdSjpl7G
5+3LeXcOSiY2+vm+itvPnTAHcG2DNCePaxFs+8QmNzPKCXJZCgbE0Y1A5K6aB8K3
sFEkgpUE1Vqjx0nNxH8sl7UGnWRYRoEMen7gwerxA5xkt6dS7zEwwEfh8fUa63Y4
eHc1I0IyxJOo+HE0ctdAEeu+STxg1VuF329ebXBrnM1IaicdRmUcDAivXudkwfiW
bLtp+OdHxUidvHUq6B0Y7ofs/NyFPTDs6yXBoD/AOr//0Xc3x4/Emb/3JXT0gx/e
F3YLt/nPAgMBAAECggEBAIKInQGUYvQCSfI9eM29oLn/VOdcQt03eaOl8URQsJ0Z
8D7Hx1XC2weGi+NjdSXnCBw4/opfcjjrN7iN0ZWPknvH8Md1R7G3RNUokbj2mh1j
PmCmg5yWr65z11uoH+Gm+tVwDzMfGn02rRmfThnLaKDE49z3+UUn9bLkLeU+fN/f
pabEgQVWm+gR+Nqwu0lnP4hIuu4EPqFMPhuoeFJL3tF6jUj6TpymU0FLbcN68dTn
3/aRDTzkURNjVltGL6FkPAIOC/c9amcFixjIP9R0WKAoRzq9vKWAxsfCFY2PW4Sn
L6WkBQiu3IUF7B2az7EtpmqEQzKDFDkFs7kiEFG1fSECgYEA/gyuSAQCyeiwU/UB
5pHphUnWsYLr7gtKfkpWUrfyrASfkmk35GolA5DVTydn1DS+HSXHLZoygYzHAeip
zw26w/r5SNYc2zQdCVqrX3yImCbP32YRq7cTrmbJz+av9K/o6t3jkuCN9bk3Ka/a
4XomclZ4Or5NJ1UcGq/EUfx2xOcCgYEA611UwANC/+IbcZ1BxKc6FjPhOCkZCOGs
WgXEn0pYqDQzsQ3STmwwnHShpUAF7Nz/te+VYrOncHyy3MKCmxRlqpAmrvsXgve8
Top59p2hbJtzl0yvXMUbyz9+lOIvmQhnb91SdI2zWTIREa570zUEDFUhqYU9L381
gDvGMWlIHtkCgYEAtu68pEC3HVq/dYUsHDIfPAoiXWoPpzZcKroulJu+U1105gF7
7K79/iZRYxDV8/kSmHx2gwDQs3aCan51oBGb1bknvennl3RcIDs4waYz2ZPBBXVC
7ioDMwby9RCAzSnKBy1FzKKDtlfrP8pQSyIhAx3+rxYPCm5/sTu1mu+3OisCgYEA
wD1uEJtZaRksY0MXyeDw39umFDH1QbzaAnl9nMGj4lco9W+dm/JgThoxQYEmlc34
iojS0lh85bLkgM3a0YH6fDLbdcae8JkaVGhkr9sWU1gxNJ4tCa/xQLRu1npceDlW
CxeR1qM6nxr2uCN0/hDqaoxKP2UVkarsbv3jps4gWxkCgYASczmjbDCeeMKIqL2x
yao50zC44fgic56zD48S0vGVJVqD2JeWUu/CZ4tL7G4fjeouwX+HXpQSZJfanGfB
bhJdC2Uiwm4UUA3/jni0GGfeJSX3TSFiTaEGF+dZFOWPCmzefZwf5nGQKRIkPJCF
H3+0GypQ8k/OggReJ8OY2BRZHA==
-----END PRIVATE KEY-----)";

   std::string const dh
      = "-----BEGIN DH PARAMETERS-----\n"
        "MIIBCAKCAQEArzQc5mpm0Fs8yahDeySj31JZlwEphUdZ9StM2D8+Fo7TMduGtSi+\n"
        "/HRWVwHcTFAgrxVdm+dl474mOUqqaz4MpzIb6+6OVfWHbQJmXPepZKyu4LgUPvY/\n"
        "4q3/iDMjIS0fLOu/bLuObwU5ccZmDgfhmz1GanRlTQOiYRty3FiOATWZBRh6uv4u\n"
        "tff4A9Bm3V9tLx9S6djq31w31Gl7OQhryodW28kc16t9TvO1BzcV3HjRPwpe701X\n"
        "oEEZdnZWANkkpR/m/pfgdmGPU66S2sXMHgsliViQWpDCYeehrvFRHEdR9NV+XJfC\n"
        "QMUk26jPTIVTLfXmmwU0u8vUkpR7LQKkwwIBAg==\n"
        "-----END DH PARAMETERS-----\n";

   ctx.set_password_callback([](std::size_t, boost::asio::ssl::context_base::password_purpose) { return "test"; });

   ctx.set_options(boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::single_dh_use);

   ctx.use_certificate_chain(boost::asio::buffer(cert.data(), cert.size()));

   ctx.use_private_key(boost::asio::buffer(key.data(), key.size()), boost::asio::ssl::context::file_format::pem);

   ctx.use_tmp_dh(boost::asio::buffer(dh.data(), dh.size()));
}

// Report a failure
void fail(boost::system::error_code ec, char const* what)
{
   std::cerr << what << ": " << ec.message() << "\n";
}


class http_session : public std::enable_shared_from_this<http_session>
{
   // This queue is used for HTTP pipelining.
   class queue
   {
      http_session* owner_;
      int limit_;
      std::vector<std::function<void()>> items_;

   public:
      explicit queue(http_session* owner, int limit)
         : owner_(owner)
         , limit_(limit)
      {
         items_.reserve(limit_);
      }

      // Returns `true` if we have reached the queue limit
      bool is_full() const
      {
         return items_.size() >= limit_;
      }

      // Called when a message finishes sending
      // Returns `true` if the caller should initiate a read
      bool next_task()
      {
         assert(!items_.empty());

         auto const was_full = is_full();
         items_.erase(items_.begin());
         if (!items_.empty())
            items_.front()();
         return was_full;
      }

      // Called by the HTTP handler to send a response.
      template <class M>
      void operator()(M&& msg)
      {
         // Allocate and store the work
         items_.push_back([ msg = std::move(msg), this ]() mutable { owner_->schedule_write(msg); });

         // If there was no previous work, start this one
         if (items_.size() == 1)
            items_.front()();
      }
   };

   tcp::socket socket_;
   ssl::stream<tcp::socket&> stream_;
   boost::asio::strand<boost::asio::io_context::executor_type> strand_;
   boost::asio::steady_timer timer_;
   boost::beast::flat_buffer buffer_;
   http::request<http::string_body> req_;
   queue queue_;
   std::chrono::seconds timeout_;

public:
   // Take ownership of the socket
   explicit http_session(tcp::socket&& socket, ssl::context& ctx)
      : socket_(std::move(socket))
      , stream_(socket_, ctx)
      , strand_(socket_.get_executor())
      , timer_(socket_.get_executor().context(), std::chrono::steady_clock::time_point::max())
      , queue_(this, 10)
      , timeout_(15)
   {
   }

   // Start the asynchronous operation
   void run()
   {
      // Make sure we run on the strand
      if (!strand_.running_in_this_thread())
      {
         return boost::asio::post(boost::asio::bind_executor(strand_, [self = shared_from_this()]() { self->run(); }));
      }

      this->arm_timer();

      auto&& on_handshake = [self = shared_from_this()](auto ec)
      {
         if (ec)
            return fail(ec, "handshake");

         self->schedule_read();
      };

      // Perform the SSL handshake
      stream_.async_handshake(ssl::stream_base::server, boost::asio::bind_executor(strand_, std::move(on_handshake)));
   }

   void arm_timer()
   {
      auto&& on_timeout = [self = shared_from_this()](auto ec)
      {
         if (ec && ec != boost::asio::error::operation_aborted)
            return fail(ec, "timer");

         // Verify that the timer really expired since the deadline may have moved.
         if (self->timer_.expiry() <= std::chrono::steady_clock::now())
         {
            // self->do_full_close();
            self->do_close();
            return;
         }

         self->arm_timer();
      };

      timer_.async_wait(boost::asio::bind_executor(strand_, std::move(on_timeout)));
   }

   void schedule_read()
   {
      // Set the timer
      timer_.expires_after(timeout_);

      // Make the request empty before reading,
      // otherwise the operation behavior is undefined.
      req_ = {};

      auto&& on_read = [self = shared_from_this()](auto ec, std::size_t)
      {
         // Happens when the timer closes the socket
         if (ec == boost::asio::error::operation_aborted)
            return;

         // This means they closed the connection
         if (ec == http::error::end_of_stream)
            return self->do_close();
         if (ec)
            return fail(ec, "read");

         // See if it is a WebSocket Upgrade
         if (websocket::is_upgrade(self->req_))
         {
            // ignore websockets
            self->do_close();
            return;
         }

         // Send the response
         handle_request(std::move(self->req_), self->queue_);

         // If we aren't at the queue limit, try to process another request
         if (!self->queue_.is_full())
            self->schedule_read();
      };

      // Read a request
      http::async_read(stream_, buffer_, req_, boost::asio::bind_executor(strand_, std::move(on_read)));
   }


   template <bool isRequest, class Body, class Fields>
   void schedule_write(http::message<isRequest, Body, Fields>& msg)
   {
      auto&& on_write = [ self = shared_from_this(), close = msg.need_eof() ](auto ec, auto sz)
      {
         // Happens when the timer closes the socket
         if (ec == boost::asio::error::operation_aborted)
            return;

         if (ec)
            return fail(ec, "write");

         if (close)
         {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            return self->do_close();
         }

         // Inform the queue that a write completed
         if (self->queue_.next_task())
         {
            // Read another request
            self->schedule_read();
         }
      };

      http::async_write(stream_, msg, boost::asio::bind_executor(strand_, std::move(on_write)));
   }

   void do_close()
   {
      auto&& on_shutdown = [self = shared_from_this()](auto ec)
      {
         if (ec && ec != boost::asio::error::eof)
            return fail(ec, "shutdown");
      };

      // Perform the SSL shutdown
      stream_.async_shutdown(boost::asio::bind_executor(strand_, std::move(on_shutdown)));
   }

   void do_full_close()
   {
      // Send a TCP shutdown
      boost::system::error_code ec;
      socket_.shutdown(tcp::socket::shutdown_both, ec);

      // Closing the socket cancels all outstanding operations. They
      // will complete with boost::asio::error::operation_aborted
      socket_.close(ec);
   }
};

//------------------------------------------------------------------------------

// Accepts incoming connections and launches the sessions
template <class SessionRunner>
class listener : public std::enable_shared_from_this<listener<SessionRunner>>
{
   ssl::context& ctx_;
   tcp::acceptor acceptor_;
   tcp::socket accepted_socket_;

public:
   listener(boost::asio::io_context& ioc, ssl::context& ctx, tcp::endpoint endpoint)
      : ctx_(ctx)
      , acceptor_(ioc)
      , accepted_socket_(ioc)
   {
      // Open the acceptor
      acceptor_.open(endpoint.protocol());

      // Allow address reuse
      acceptor_.set_option(boost::asio::socket_base::reuse_address(true));

      // Bind to the server address
      acceptor_.bind(endpoint);

      // Start listening for connections
      acceptor_.listen(boost::asio::socket_base::max_listen_connections);
   }

   // Start accepting incoming connections
   void run()
   {
      acceptor_.async_accept(accepted_socket_, [self = this->shared_from_this()](auto ec) {
         if (ec)
         {
            fail(ec, "accept");
         }
         else
         {
            std::make_shared<SessionRunner>(std::move(self->accepted_socket_), self->ctx_)->run();
         }

         // Accept another connection
         self->run();
      });
   }
};

//------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
   // Check command line arguments.
   if (argc != 4)
   {
      std::cerr << "Usage: sample_two <address> <port> <threads>\n"
                << "Example:\n"
                << "    sample_two 0.0.0.0 8080 1\n";
      return EXIT_FAILURE;
   }

   auto const address = boost::asio::ip::make_address(argv[1]);
   auto const port    = static_cast<unsigned short>(std::atoi(argv[2]));
   auto const threads = std::max<int>(1, std::atoi(argv[3]));

   // The io_context is required for all I/O
   boost::asio::io_context ioc{threads};

   // The SSL context is required, and holds certificates
   ssl::context ctx{ssl::context::sslv23};

   // This holds the self-signed certificate used by the server
   load_server_certificate(ctx);

   // Create and launch a listening port
   std::make_shared<listener<http_session>>(ioc, ctx, tcp::endpoint{address, port})->run();

   // Capture SIGINT and SIGTERM to perform a clean shutdown
   boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
   signals.async_wait([&](boost::system::error_code const&, int) { ioc.stop(); });

   // Run the I/O service on the requested number of threads
   std::vector<std::thread> v;
   v.reserve(threads - 1);
   for (auto i = threads - 1; i > 0; --i)
      v.emplace_back([&ioc] { ioc.run(); });
   ioc.run();

   // Block until all the threads exit
   for (auto& t : v)
      t.join();

   return EXIT_SUCCESS;
}
