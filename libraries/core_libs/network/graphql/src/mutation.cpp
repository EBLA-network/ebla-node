#include "graphql/mutation.hpp"

#include <stdexcept>

#include "common/util.hpp"
#include "libdevcore/CommonJS.h"

using namespace std::literals;

namespace graphql::ebla {

Mutation::Mutation(std::shared_ptr<::ebla::TransactionManager> trx_manager) noexcept
    : trx_manager_(std::move(trx_manager)) {}

response::Value Mutation::applySendRawTransaction(response::Value&& dataArg) const {
  const auto trx =
      std::make_shared<::ebla::Transaction>(jsToBytes(dataArg.get<std::string>(), dev::OnFailed::Throw), true);
  if (auto [ok, err_msg] = trx_manager_->insertTransaction(trx); !ok) {
    throw(
        std::runtime_error(::ebla::fmt("Transaction is rejected.\n"
                                         "RLP: %s\n"
                                         "Reason: %s",
                                         dev::toJS(trx->rlp()), err_msg)));
  }
  return response::Value(dev::toJS(trx->getHash()));
}

}  // namespace graphql::ebla