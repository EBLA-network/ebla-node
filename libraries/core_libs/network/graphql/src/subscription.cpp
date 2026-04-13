#include "graphql/subscription.hpp"

#include <iostream>

namespace graphql::ebla {

response::Value Subscription::getTestSubscription() const noexcept {
  std::cout << "Subscription::getTestSubscription" << std::endl;
  return response::Value(123456789);
}

}  // namespace graphql::ebla