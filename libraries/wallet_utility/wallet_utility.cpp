/* (c) 2016, 2017 DECENT Services. For details refers to LICENSE.txt */
#include <decent/wallet_utility/wallet_utility.hpp>
#include <graphene/utilities/dirhelper.hpp>
#include <fc/rpc/websocket_api.hpp>
#include <fc/network/http/websocket.hpp>
#include <fc/filesystem.hpp>
#include <fc/rpc/api_connection.hpp>
#include <fc/api.hpp>
#include <decent/package/package.hpp>
#include <iostream>

namespace decent
{
namespace wallet_utility
{
   using wallet_api = graphene::wallet::wallet_api;
   using wallet_api_ptr = std::shared_ptr<wallet_api>;
   using wallet_data = graphene::wallet::wallet_data;
   using websocket_client = fc::http::websocket_client;
   using websocket_client_ptr = std::shared_ptr<websocket_client>;
   using websocket_connection_ptr = fc::http::websocket_connection_ptr;
   using websocket_api_connection = fc::rpc::websocket_api_connection;
   using websocket_api_connection_ptr = std::shared_ptr<websocket_api_connection>;
   using fc_api = fc::api<wallet_api>;
   using fc_api_ptr = std::shared_ptr<fc_api>;
   using fc_remote_api = fc::api<graphene::app::login_api>;
   using fc_remote_api_ptr = std::shared_ptr<fc_remote_api>;

   namespace
   {
      class wallet_exception : public std::exception
      {
      public:
         wallet_exception(std::string const& str_info) noexcept : m_str_info(str_info) {}
         virtual ~wallet_exception() {}

         char const* what() const noexcept override
         {
            return m_str_info.c_str();
         }
      private:
         std::string m_str_info;
      };

   }

   namespace detail
   {
      class WalletAPIConnection : public fc::api_connection
      {  // no idea yet why deriving
      public:
         WalletAPIConnection() {}
         virtual ~WalletAPIConnection() {}

         virtual fc::variant send_call(fc::api_id_type api_id, string method_name, fc::variants args = fc::variants() ) {FC_ASSERT(false);}
         virtual fc::variant send_callback(uint64_t callback_id, fc::variants args = fc::variants() ) {FC_ASSERT(false);}
         virtual void send_notice(uint64_t callback_id, fc::variants args = fc::variants() ) {FC_ASSERT(false);}
      };

      using WalletAPIConnectionPtr = std::shared_ptr<WalletAPIConnection>;

      class WalletAPIHelper
      {
      public:
         WalletAPIHelper(const fc::path &wallet_file, graphene::wallet::server_data ws)
         : m_ptr_wallet_api(nullptr)
         , m_ptr_fc_api_connection(nullptr)
         {
            wallet_data wdata;
            bool has_wallet_file = fc::exists(wallet_file);
            if (has_wallet_file)
               wdata = fc::json::from_file(wallet_file).as<wallet_data>();

            if (ws.server.empty())
               ws.server = wdata.ws_server;
            if (ws.user.empty())
               ws.user = wdata.ws_user;
            if (ws.password.empty())
               ws.password = wdata.ws_password;

            websocket_client_ptr ptr_ws_client(new websocket_client());
               websocket_connection_ptr ptr_ws_connection = ptr_ws_client->connect(ws.server);

            //  capture ptr_ws_connection and ptr_ws_client own the lifetime
            websocket_api_connection_ptr ptr_api_connection =
               websocket_api_connection_ptr(new websocket_api_connection(*ptr_ws_connection),
                                         [ptr_ws_connection, ptr_ws_client](websocket_api_connection* &p_api_connection) mutable
                                         {
                                            delete p_api_connection;
                                            p_api_connection = nullptr;
                                            ptr_ws_connection.reset();
                                            ptr_ws_client.reset();
                                         });

            fc_remote_api_ptr ptr_remote_api =
               fc_remote_api_ptr(new fc_remote_api(ptr_api_connection->get_remote_api<graphene::app::login_api>(1)));
            if (false == (*ptr_remote_api)->login(ws.user, ws.password))
               throw wallet_exception("fc::api<graphene::app::login_api>::login");

            if (!has_wallet_file)
               wdata.chain_id = (*ptr_remote_api)->database()->get_chain_id();

            //  capture ptr_api_connection and ptr_remote_api too. encapsulate all inside wallet_api
            m_ptr_wallet_api.reset(new wallet_api(*ptr_remote_api, wdata.chain_id, ws),
                                   [ptr_api_connection, ptr_remote_api](wallet_api* &p_wallet_api) mutable
                                   {
                                      delete p_wallet_api;
                                      p_wallet_api = nullptr;
                                      ptr_api_connection.reset();
                                      ptr_remote_api.reset();
                                   });

            if (has_wallet_file)
               m_ptr_wallet_api->load_wallet_file(wallet_file.generic_string());
            m_ptr_wallet_api->set_wallet_filename( wallet_file.generic_string() );

            fc_api_ptr ptr_fc_api = fc_api_ptr(new fc_api(m_ptr_wallet_api.get()));

            for (auto& name_formatter : m_ptr_wallet_api->get_result_formatters())
               m_result_formatters[name_formatter.first] = name_formatter.second;

            m_ptr_fc_api_connection = WalletAPIConnectionPtr(new WalletAPIConnection(),
                                                             [ptr_fc_api](WalletAPIConnection* &pWAPICon) mutable
                                                             {
                                                                delete pWAPICon;
                                                                pWAPICon = nullptr;
                                                                ptr_fc_api.reset();
                                                             });
            m_ptr_fc_api_connection->register_api(*ptr_fc_api);
         }

         wallet_api_ptr m_ptr_wallet_api;
         WalletAPIConnectionPtr m_ptr_fc_api_connection;
         std::map<string, std::function<string(fc::variant,const fc::variants&)> > m_result_formatters;
         std::map<graphene::chain::asset_id_type, std::pair<std::string, uint8_t>> m_asset_symbols;
      };
   }
   //
   //  WalletAPI
   //
   WalletAPI::WalletAPI(const fc::path &wallet_file, const graphene::wallet::server_data &ws)
   : m_wallet_file(wallet_file)
   , m_ws(ws)
   , m_pthread(nullptr)
   , m_pimpl(nullptr)
   , m_mutex()
   {
   }

   WalletAPI::~WalletAPI()
   {
   }

   void WalletAPI::Connect(std::atomic_bool& cancellation_token)
   {
      if (IsConnected())
         throw wallet_exception("already connected");

      std::lock_guard<std::mutex> lock(m_mutex);

      m_pthread.reset(new fc::thread("wallet_api_service"));

      fc::future<string> future_connect =
      m_pthread->async([this, &cancellation_token] () -> string
                       {
                          std::string error;
                          while (! cancellation_token)
                          {
                             try
                             {
                                m_pimpl.reset(new detail::WalletAPIHelper(m_wallet_file, m_ws));
                                break;
                             }
                             catch(wallet_exception const& ex)
                             {
                                return ex.what();
                             }
                             catch(fc::exception const& ex)
                             {
                                error = ex.what();
                             }
                             catch(std::exception const& ex)
                             {
                                error = ex.what();
                             }

                             if (!error.empty()) {
                                //log error ??
                             }
                          }

//                          if (cancellation_token) {
//                             std::cout << "cancelation_token" << std::endl;
//                          }

                          return string();
                       });
      string str_result = future_connect.wait();
      if (!str_result.empty())
         throw wallet_exception(str_result);
   }

   bool WalletAPI::IsConnected()
   {
      std::lock_guard<std::mutex> lock(m_mutex);
      return nullptr != m_pimpl;
   }

   bool WalletAPI::IsNew()
   {
      if (!IsConnected())
         throw wallet_exception("not yet connected");

      std::lock_guard<std::mutex> lock(m_mutex);

      auto& pimpl = m_pimpl->m_ptr_wallet_api;
      fc::future<bool> future_is_new =
      m_pthread->async([&pimpl] () -> bool
                       {
                          return pimpl->is_new();
                       });
      return future_is_new.wait();
   }
   bool WalletAPI::IsLocked()
   {
      if (!IsConnected())
         throw wallet_exception("not yet connected");

      std::lock_guard<std::mutex> lock(m_mutex);

      auto& pimpl = m_pimpl->m_ptr_wallet_api;
      fc::future<bool> future_is_locked =
      m_pthread->async([&pimpl] () -> bool
                       {
                          return pimpl->is_locked();
                       });
      return future_is_locked.wait();
   }
   std::chrono::system_clock::time_point WalletAPI::HeadBlockTime()
   {
      if (!IsConnected())
         throw wallet_exception("not yet connected");

      std::lock_guard<std::mutex> lock(m_mutex);

      auto& pimpl = m_pimpl->m_ptr_wallet_api;
      fc::future<fc::time_point_sec> future_head_block_time =
      m_pthread->async([&pimpl] ()
                       {
                          return pimpl->head_block_time();
                       });
      fc::time_point_sec fctp = future_head_block_time.wait();
      return std::chrono::system_clock::from_time_t(fctp.sec_since_epoch());
   }
   void WalletAPI::SetPassword(string const& str_password)
   {
      if (!IsConnected())
         throw wallet_exception("not yet connected");

      std::lock_guard<std::mutex> lock(m_mutex);

      auto& pimpl = m_pimpl->m_ptr_wallet_api;
      fc::future<void> future_set_password =
      m_pthread->async([&pimpl, &str_password] ()
                       {
                          return pimpl->set_password(str_password);
                       });
      return future_set_password.wait();
   }
   void WalletAPI::Unlock(string const& str_password)
   {
      if (!IsConnected())
         throw wallet_exception("not yet connected");

      std::lock_guard<std::mutex> lock(m_mutex);

      auto& pimpl = m_pimpl->m_ptr_wallet_api;
      fc::future<void> future_unlock =
      m_pthread->async([&pimpl, &str_password] ()
                       {
                          return pimpl->unlock(str_password);
                       });
      return future_unlock.wait();
   }

   void WalletAPI::SaveWalletFile()
   {
      if (!IsConnected())
         throw wallet_exception("not yet connected");

      string str_file = m_wallet_file.to_native_ansi_path();

      std::lock_guard<std::mutex> lock(m_mutex);

      auto& pimpl = m_pimpl->m_ptr_wallet_api;
      fc::future<void> future_save_wallet_file =
      m_pthread->async([&pimpl, &str_file] ()
                       {
                          return pimpl->save_wallet_file(str_file);
                       });
      return future_save_wallet_file.wait();
   }
   /*
   std::vector<graphene::chain::content_summary> WalletAPI::SearchContent(string const& str_term, uint32_t iCount)
   {
      if (false == IsConnected())
         throw wallet_exception("not yet connected");

      std::lock_guard<std::mutex> lock(m_mutex);

      auto& pimpl = m_pimpl->m_ptr_wallet_api;
      fc::future<vector<graphene::chain::content_summary>> future_search_content =
      m_pthread->async([&pimpl, &str_term, iCount] ()
                       {
                          return pimpl->search_content(str_term, iCount);
                       });
      return future_search_content.wait();
   }
    */

   string WalletAPI::RunTask(string const& str_command)
   {
      if (false == IsConnected())
         throw wallet_exception("not yet connected");

      std::lock_guard<std::mutex> lock(m_mutex);
      auto& pimpl = m_pimpl;

      fc::future<string> future_run =
      m_pthread->async([&pimpl, &str_command] () -> string
                       {
                          std::string line = str_command;
                          line += char(EOF);
                          fc::variants args = fc::json::variants_from_string(line);

                          if (false == args.empty())
                          {
                             const string& method = args.front().get_string();

                             variant result;
                             try {
                                result = pimpl->m_ptr_fc_api_connection->receive_call(0, method, fc::variants(args.begin()+1, args.end()));
                             } catch (fc::exception const& ex) {
                                throw std::runtime_error(ex.to_detail_string());
                             }
                             auto it = pimpl->m_result_formatters.find(method);

                             string str_result;
                             if (it == pimpl->m_result_formatters.end())
                                str_result = fc::json::to_pretty_string(result);
                             else
                                str_result = it->second(result, args);
                             
                             return str_result;
                          }
                          // may as well throw an exception?
                          
                          return string();
                       });
      string str_result = future_run.wait();
      return str_result;
   }
}
}
