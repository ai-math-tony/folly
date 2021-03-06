/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <experimental/coroutine>
#include <type_traits>

#include <folly/Optional.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/Traits.h>
#include <folly/experimental/coro/detail/Helpers.h>
#include <folly/futures/Future.h>

namespace folly {
namespace coro {
template <typename Awaitable>
Task<Optional<lift_unit_t<detail::decay_rvalue_reference_t<
    detail::lift_lvalue_reference_t<semi_await_result_t<Awaitable>>>>>>
timed_wait(Awaitable awaitable, Duration duration) {
  auto posted = std::make_shared<std::atomic<bool>>(false);
  Baton baton;
  Try<lift_unit_t<detail::decay_rvalue_reference_t<
      detail::lift_lvalue_reference_t<semi_await_result_t<Awaitable>>>>>
      result;

  futures::sleepUnsafe(duration).setCallback_(
      [posted, &baton, executor = co_await co_current_executor](
          auto&&, auto&&) {
        if (!posted->exchange(true, std::memory_order_relaxed)) {
          executor->add([&baton] { baton.post(); });
        }
      });

  co_invoke(
      [awaitable = std::move(
           awaitable)]() mutable -> Task<semi_await_result_t<Awaitable>> {
        co_return co_await std::move(awaitable);
      })
      .scheduleOn(co_await co_current_executor)
      .start([posted, &baton, &result](auto&& r) {
        if (!posted->exchange(true, std::memory_order_relaxed)) {
          result = std::move(r);
          baton.post();
        }
      });

  co_await detail::UnsafeResumeInlineSemiAwaitable{get_awaiter(baton)};

  if (!result.hasValue() && !result.hasException()) {
    co_return folly::none;
  }
  co_return *result;
}

} // namespace coro
} // namespace folly
