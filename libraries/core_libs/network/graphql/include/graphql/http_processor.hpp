#pragma once

#include "dag/dag.hpp"
#include "final_chain/final_chain.hpp"
#include "mutation.hpp"
#include "network/http_server.hpp"
#include "network/network.hpp"
#include "query.hpp"
#include "subscription.hpp"
#include "transaction/gas_pricer.hpp"
namespace ebla::net {
class GraphQlHttpProcessor final : public HttpProcessor {
 public:
  GraphQlHttpProcessor(std::shared_ptr<::ebla::final_chain::FinalChain> final_chain,
                       std::shared_ptr<::ebla::DagManager> dag_manager,
                       std::shared_ptr<::ebla::PbftManager> pbft_manager,
                       std::shared_ptr<::ebla::TransactionManager> transaction_manager,
                       std::shared_ptr<::ebla::DbStorage> db, std::shared_ptr<::ebla::GasPricer> gas_pricer,
                       std::weak_ptr<::ebla::Network> network, uint64_t chain_id);
  Response process(const Request& request) override;

 private:
  Response createErrResponse(std::string&& = "");
  Response createErrResponse(graphql::response::Value&& error_value);
  Response createOkResponse(std::string&& response_body);

 private:
  std::shared_ptr<graphql::ebla::Query> query_;
  std::shared_ptr<graphql::ebla::Mutation> mutation_;
  std::shared_ptr<graphql::ebla::Subscription> subscription_;
  graphql::ebla::Operations operations_;
};

}  // namespace ebla::net