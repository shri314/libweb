#pragma once

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
   template<class... Args>
   void run(Args... args)
   {
      acceptor_.async_accept(accepted_socket_, [self = this->shared_from_this(), args...](auto ec) {
         if (ec)
         {
            fail(ec, "accept");
         }
         else
         {
            std::make_shared<SessionRunner>(std::move(self->accepted_socket_), args...)->run();
         }

         // Accept another connection
         self->run(args...);
      });
   }
};

