/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#pragma once

#include <graphene/app/plugin.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/transaction_history_object.hpp>
#include <fc/thread/future.hpp>

namespace graphene { namespace transaction_history {
      using namespace chain;


namespace detail
{
   class transaction_history_plugin_impl;
}

class transaction_history_plugin : public graphene::app::plugin
{
public:
   transaction_history_plugin(graphene::app::application* app);
   virtual ~transaction_history_plugin();

   static std::string plugin_name();
   static void plugin_set_program_options(
      boost::program_options::options_description& cli,
      boost::program_options::options_description& cfg);

   virtual void plugin_initialize(const boost::program_options::variables_map& options) override;
   virtual void plugin_startup() override;

   flat_set<account_id_type> tracked_accounts()const;

   friend class detail::transaction_history_plugin_impl;
   std::unique_ptr<detail::transaction_history_plugin_impl> my;
};

} } //graphene::transaction_history
