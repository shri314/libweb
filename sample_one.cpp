#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
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
         items_.push_back(
            [ msg = std::move(msg), this ]() mutable {
               owner_->schedule_write( msg );
            });

         // If there was no previous work, start this one
         if (items_.size() == 1)
            items_.front()();
      }
   };

   tcp::socket socket_;
   boost::asio::strand<boost::asio::io_context::executor_type> strand_;
   boost::asio::steady_timer timer_;
   boost::beast::flat_buffer buffer_;
   http::request<http::string_body> req_;
   queue queue_;
   std::chrono::seconds timeout_;

public:
   // Take ownership of the socket
   explicit http_session(tcp::socket&& socket)
      : socket_(std::move(socket))
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


      arm_timer();

      schedule_read();
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
            self->do_full_close();
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
      http::async_read(socket_, buffer_, req_, boost::asio::bind_executor(strand_, std::move(on_read)));
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

      http::async_write(socket_, msg, boost::asio::bind_executor(strand_, std::move(on_write)));
   }

   void do_close()
   {
      // Send a TCP shutdown
      boost::system::error_code ec;
      socket_.shutdown(tcp::socket::shutdown_send, ec);
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
   tcp::acceptor acceptor_;
   tcp::socket accepted_socket_;

public:
   listener(boost::asio::io_context& ioc, tcp::endpoint endpoint)
      : acceptor_(ioc)
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
            std::make_shared<SessionRunner>(std::move(self->accepted_socket_))->run();
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
      std::cerr << "Usage: sample_one <address> <port> <threads>\n"
                << "Example:\n"
                << "    sample_one 0.0.0.0 8080 1\n";
      return EXIT_FAILURE;
   }

   auto const address = boost::asio::ip::make_address(argv[1]);
   auto const port    = static_cast<unsigned short>(std::atoi(argv[2]));
   auto const threads = std::max<int>(1, std::atoi(argv[3]));

   // The io_context is required for all I/O
   boost::asio::io_context ioc{threads};

   // Create and launch a listening port
   std::make_shared<listener<http_session>>(ioc, tcp::endpoint{address, port})->run();

   // Capture SIGINT and SIGTERM to perform a clean shutdown
   boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
   signals.async_wait([&](boost::system::error_code const&, int) { ioc.stop(); });

   // Run the I/O service on the requested number of threads
   std::vector<std::thread> v;
   v.reserve(threads - 1);
   for (auto i = threads - 1; i > 0; --i) v.emplace_back([&ioc] { ioc.run(); });
   ioc.run();

   // Block until all the threads exit
   for (auto& t : v) t.join();

   return EXIT_SUCCESS;
}
