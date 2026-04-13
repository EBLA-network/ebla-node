#pragma once

#include <memory>
#include <string>

#include "AccountObject.h"
#include "final_chain/final_chain.hpp"
#include "final_chain/state_api.hpp"

namespace graphql::ebla {

class Account {
 public:
  explicit Account(std::shared_ptr<::ebla::final_chain::FinalChain> final_chain, dev::Address address,
                   ::ebla::EthBlockNumber blk_n);
  explicit Account(std::shared_ptr<::ebla::final_chain::FinalChain> final_chain, dev::Address address);

  response::Value getAddress() const noexcept;
  response::Value getBalance() const noexcept;
  response::Value getTransactionCount() const noexcept;
  response::Value getCode() const noexcept;
  response::Value getStorage(response::Value&& slotArg) const;

 private:
  const dev::Address kAddress;
  std::optional<::ebla::state_api::Account> account_;
  std::shared_ptr<::ebla::final_chain::FinalChain> final_chain_;
};
}  // namespace graphql::ebla