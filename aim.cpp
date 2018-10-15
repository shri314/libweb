int main() {

   struct abcd {
      abcd(int x, int y)
         : x(x)
         , y(y)
      {
      }

      int x;
      int y;
   };

   struct pqrs {
      pqrs(int x, int y)
         : x(x)
         , y(y)
      {
      }

      int x;
      int y;

      static pqrs parse_to(std::string);
   };

   api_list api_handlers(
      post<pqrs> / "activate" / arg<int>("edgeid") / "foo" / arg<float>() / => [](auto data, auto x, auto y) {
         std::cout << "x = " << x << ", y = " << y << "\n";
         return abcd{data.x + x + 10, data.y + y + 10};
      },
      get / "baz" ^ param<int>("x") = param<int> => [](auto x) {
         std::fstream f;

         return f;
      },
      def / "" => []() {
         return 500_reply;
      }
   );

   auto ws = make_webserver(
         []() { },
         api_handlers,
         [](wspp::req) { },
         [](wspp::resp) { }
   );

   ws();
}

