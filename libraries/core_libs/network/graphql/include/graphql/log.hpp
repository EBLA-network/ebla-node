#pragma once

#include <memory>
#include <string>

#include "LogObject.h"
#include "final_chain/final_chain.hpp"
#include "graphql/transaction.hpp"
#include "transaction/transaction_manager.hpp"

namespace graphql::ebla {

class Log {
 public:
  explicit Log(std::shared_ptr<::ebla::final_chain::FinalChain> final_chain,
               std::shared_ptr<::ebla::TransactionManager> trx_manager, std::shared_ptr<const Transaction> transaction,
               ::ebla::LogEntry log, int index) noexcept;

  int getIndex() const noexcept;
  std::shared_ptr<object::Account> getAccount(std::optional<response::Value>&& blockArg) const noexcept;
  std::vector<response::Value> getTopics() const noexcept;
  response::Value getData() const noexcept;
  std::shared_ptr<object::Transaction> getTransaction() const noexcept;

 private:
  std::shared_ptr<::ebla::final_chain::FinalChain> final_chain_;
  std::shared_ptr<::ebla::TransactionManager> trx_manager_;
  std::shared_ptr<const Transaction> kTransaction;
  const ::ebla::LogEntry kLog;
  const int kIndex;
};

}  // namespace graphql::ebla