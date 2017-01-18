#include <graphene/seeding/seeding.hpp>
#include <graphene/chain/operation_history_object.hpp>
#include <graphene/chain/database.hpp>
#include <decent/encrypt/encryptionutils.hpp>
#include <graphene/chain/content_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include <graphene/package/package.hpp>
#include <algorithm>

namespace graphene { namespace seeding {
namespace bpo = boost::program_options;
namespace detail {


seeding_plugin_impl::~seeding_plugin_impl() {
   return;
}

graphene::chain::database &seeding_plugin_impl::database() {
   return _self.database();
}

void seeding_plugin_impl::on_operation(const operation_history_object &op_obj) {
   graphene::chain::database &db = database();

   if( op_obj.op.which() == operation::tag<request_to_buy_operation>::value ) {
      const request_to_buy_operation &rtb_op = op_obj.op.get<request_to_buy_operation>();
      const auto &idx = db.get_index_type<my_seeding_index>().indices().get<by_URI>();
      const auto &sitr = idx.find(rtb_op.URI);
      if( sitr == idx.end())
         return;
      const auto &sidx = db.get_index_type<my_seeder_index>().indices().get<by_seeder>();
      const auto &sritr = sidx.find(sitr->seeder);
      FC_ASSERT(sritr != sidx.end());

      //ok, this is our seeding... we shall generate deliver keys out of it...
      account_object seeder_account = db.get<account_object>(sitr->seeder);
      //const buying_object bo = get_object<buying_object>(buying);
      const auto &cidx = db.get_index_type<content_index>().indices().get<graphene::chain::by_URI>();
      const auto &citr = cidx.find(rtb_op.URI);
      if( citr == cidx.end())
         FC_THROW("cannot find content by URI");
      const content_object &co = *citr;
      d_integer destPubKey = rtb_op.pubKey;
      decent::crypto::ciphertext orig = co.key_parts.at(seeder_account.id);
      decent::crypto::point message;
      auto result = decent::crypto::el_gamal_decrypt(orig, sritr->content_privKey, message);
      FC_ASSERT(result == decent::crypto::ok);
      deliver_keys_operation op;
      result = decent::crypto::encrypt_with_proof(message, sritr->content_privKey, destPubKey, orig, op.key, op.proof);

      op.seeder = seeder_account.id;

      signed_transaction tx;
      tx.operations.push_back(op);

      auto dyn_props = db.get_dynamic_global_properties();
      tx.set_reference_block(dyn_props.head_block_id);
      tx.set_expiration(dyn_props.time + fc::seconds(30));
      tx.validate();

      chain_id_type _chain_id = db.get_chain_id();

      tx.sign(sritr->privKey, _chain_id);
      fc::async([this, tx]() { _self.p2p_node().broadcast_transaction(tx); });
   }

   if( op_obj.op.which() == operation::tag<content_submit_operation>::value ) {
      ilog("seeding plugin:  on_operation() handling new content");
      const content_submit_operation &cs_op = op_obj.op.get<content_submit_operation>();
      const auto &idx = db.get_index_type<my_seeder_index>().indices().get<by_seeder>();
      auto seeder_itr = idx.begin();
      while( seeder_itr != idx.end()) {
         if( std::find(cs_op.seeders.begin(), cs_op.seeders.end(), (seeder_itr->seeder)) != cs_op.seeders.end()) {
            auto s = cs_op.seeders.begin();
            auto k = cs_op.key_parts.begin();
            while( *s != seeder_itr->seeder && s != cs_op.seeders.end()) {
               ++s;
               ++k;
            }

            const auto &sidx = db.get_index_type<my_seeding_index>().indices().get<by_URI>();
            const auto &sitr = sidx.find(cs_op.URI);
            if( sitr != sidx.end()) {
               //TODO_DECENT - resubmit is not handled yet properly, planned in drop 2
               uint32_t new_space = (cs_op.size + 1048575) / 1048576; //we allocate the whole megabytes per content
               uint32_t diff_space = sitr->space - new_space;
               db.modify<my_seeding_object>(*sitr, [&](my_seeding_object &mso) {
                    mso.space -= diff_space;
               });
               db.modify<my_seeder_object>(*seeder_itr, [&](my_seeder_object &mso) {
                    mso.free_space -= diff_space;
               });
            } else {

               db.create<my_seeding_object>([&](my_seeding_object &so) {
                    so.URI = cs_op.URI;
                    so.seeder = seeder_itr->seeder;
                    so.space = (cs_op.size + 1048575) / 1048576; //we allocate the whole megabytes per content
                    if( k != cs_op.key_parts.end())
                       so.key = *k;
                    so.expiration = cs_op.expiration;
                    so.cd = cs_op.cd;
               });
               db.modify<my_seeder_object>(*seeder_itr, [&](my_seeder_object &mso) {
                    mso.free_space -= (cs_op.size + 1048575) / 1048576; //we allocate the whole megabytes per content
               });
            }
         }
         ++seeder_itr;
      }

      //TODO_DECENT download the content and initialize callback
   }
}

void
seeding_plugin_impl::generate_por(my_seeding_id_type so_id, graphene::package::package_object downloaded_package) {
   graphene::chain::database &db = database();
   const my_seeding_object &mso = db.get<my_seeding_object>(so_id);
   const auto &sidx = db.get_index_type<my_seeder_index>().indices().get<by_seeder>();
   const auto &sritr = sidx.find(mso.seeder);
   FC_ASSERT(sritr != sidx.end());

   if( db.head_block_time() > mso.expiration ) {
      //TODO_DECENT: graphene::package::package_manager::instance().delete_package();
      db.remove( mso );
      return;
   }
   decent::crypto::custody_proof proof;
   fc::ripemd160 b_id = db.head_block_id();
   uint32_t b_num = db.head_block_num();
   proof.reference_block = b_num;
   for(int i =0; i<5; i++)
      proof.seed.data[i] = b_id._hash[i];

   downloaded_package.create_proof_of_custody(mso.cd, proof);
   //TODO_DECENT - issue PoR and start periodic PoR generation
   proof_of_custody_operation op;

   op.seeder = mso.seeder;
   op.proof = proof;
   op.URI = mso.URI;

   signed_transaction tx;
   tx.operations.push_back(op);

   auto dyn_props = db.get_dynamic_global_properties();
   tx.set_reference_block(dyn_props.head_block_id);
   tx.set_expiration(dyn_props.time + fc::seconds(30));
   tx.validate();

   chain_id_type _chain_id = db.get_chain_id();

   tx.sign(sritr->privKey, _chain_id);
   fc::async([this, tx]() { _self.p2p_node().broadcast_transaction(tx); });
   fc::time_point fc_now = fc::time_point::now();
   fc::time_point next_wakeup( fc_now + fc::microseconds( 1000000*(24*60*60 - 10 ) ) );
   if( next_wakeup > fc::time_point(mso.expiration) )
      next_wakeup = fc::time_point(mso.expiration) - fc::microseconds( 30 * 1000000 );

   fc::schedule([this, so_id, downloaded_package](){ generate_por(so_id, downloaded_package); }, next_wakeup, "Seeding plugin PoR generate");
}

}// end namespace detail


seeding_plugin::seeding_plugin() : my( new detail::seeding_plugin_impl( *this )){}
void seeding_plugin::plugin_startup()
{
   //we shall send ready_to_publish here....
}

void seeding_plugin::plugin_initialize( const boost::program_options::variables_map& options )
{try{
   ilog("seeding plugin:  plugin_initialize() start");
   database().on_applied_operation.connect( [&]( const operation_history_object& b ){ my->on_operation(b); } );
   database().add_index< primary_index < my_seeding_index > >();
   database().add_index< primary_index < my_seeder_index > >();

   fc::optional<fc::ecc::private_key> private_key;
   d_integer content_key;
   account_id_type seeder;
   uint32_t free_space;
   if( options.count("seeder-private-key") || options.count("content-private-key") || options.count("seeder") || options.count("free-space") ) {
      if( options.count("seeder-private-key")) {
         private_key = graphene::utilities::wif_to_key(options["seeder-private-key"].as<std::string>());
         if( !private_key )
            try {
               private_key = fc::variant(options["seeder-private-key"].as<string>()).as<fc::ecc::private_key>();
            }
            catch( const fc::exception & ) {
               FC_THROW("Invalid WIF-format seeder private key ${key_string}",
                        ("key_string", options["seeder-private-key"].as<string>()));
            }
      } else {
         FC_THROW("missing seeder-private-key parameter");
      }


      if( options.count("content-private-key")) {
         try {
            content_key = decent::crypto::d_integer::from_string(options["content-private-key"].as<string>());
         } catch( ... ) {
            FC_THROW("Invalid content private key ${key_string}",
                     ("key_string", options["content-private-key"].as<string>()));
         }
      } else {
         FC_THROW("missing content-private-key parameter");
      }

      if( options.count("seeder"))
         seeder = fc::variant(options["seeder"].as<string>()).as<account_id_type>();
      else
         FC_THROW("missing seeder parameter");

      if( options.count("free-space"))
         free_space = options["free-space"].as<int>();
      else
         FC_THROW("missing free-space parameter");

      database().create<my_seeder_object>([&](my_seeder_object &mso) {
           mso.seeder = seeder;
           mso.free_space = free_space;
           mso.content_privKey = content_key;
           mso.privKey = *private_key;
      });
   }
   ilog("seeding plugin:  plugin_initialize() end");
}FC_LOG_AND_RETHROW() }

std::string seeding_plugin::plugin_name()const
{
   return "seeding";
}

void seeding_plugin::plugin_set_program_options(
        boost::program_options::options_description& cli,
        boost::program_options::options_description& cfg)
{
   cli.add_options()
         ("seeder", bpo::value<string>(), "ID of account controlling this seeder, quotes are required, may specify multiple times)")
         ("content-private-key", bpo::value<string>(), "El Gamal content private key")
         ("seeder-private-key", bpo::value<string>(), "Private key of the account controlling this seeder")
         ("free-space", bpo::value<int>(), "Allocated disk space, in MegaBytes")
         ;
}

}}
