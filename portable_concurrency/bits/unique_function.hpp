#pragma once

#include <functional>
#include <memory>
#include <type_traits>

#include "unique_function.h"
#include "invoke.h"

namespace portable_concurrency {
inline namespace cxx14_v1 {

namespace detail {

template<typename F>
using is_storable_t = std::integral_constant<bool,
  alignof(F) <= small_buffer_align &&
  sizeof(F) <= small_buffer_size &&
  std::is_nothrow_move_constructible<F>::value
>;

template<typename R, typename... A>
using func_ptr_t = R (*) (A...);

template<typename R, typename... A>
struct callable_vtbl {
  func_ptr_t<void, small_buffer&> destroy;
  func_ptr_t<void, small_buffer&, small_buffer&> move;
  func_ptr_t<R, small_buffer&, A...> call;
};

template<typename F, typename R, typename... A>
const callable_vtbl<R, A...>& get_callable_vtbl() {
  static_assert (is_storable_t<F>::value, "Can't embed object into small buffer");
  static const callable_vtbl<R, A...> res = {
    [](small_buffer& buf) {reinterpret_cast<F&>(buf).~F();},
    [](small_buffer& src, small_buffer& dst) {new(&dst) F{std::move(reinterpret_cast<F&>(src))};},
    [](small_buffer& buf, A... a) -> R {
      return portable_concurrency::cxx14_v1::detail::invoke(reinterpret_cast<F&>(buf), a...);
    }
  };
  return res;
}

template<typename T>
auto is_null(const T&) noexcept
  -> std::enable_if_t<!std::is_convertible<std::nullptr_t, T>::value, bool>
{
  return false;
}

template<typename T>
auto is_null(const T& val) noexcept
  -> std::enable_if_t<std::is_convertible<std::nullptr_t, T>::value, bool>
{
  return val == nullptr;
}

} // namespace detail

template<typename R, typename... A>
unique_function<R(A...)>::unique_function() noexcept = default;

template<typename R, typename... A>
unique_function<R(A...)>::unique_function(std::nullptr_t) noexcept {}

template<typename R, typename... A>
template<typename F>
unique_function<R(A...)>::unique_function(F&& f):
  unique_function(std::forward<F>(f), detail::is_storable_t<std::decay_t<F>>{})
{}

template<typename R, typename... A>
template<typename F>
unique_function<R(A...)>::unique_function(F&& f, std::true_type) {
  if (detail::is_null(f))
    return;
  new(&buffer_) std::decay_t<F>{std::forward<F>(f)};
  vtbl_ = &detail::get_callable_vtbl<std::decay_t<F>, R, A...>();
}

template<typename R, typename... A>
template<typename F>
unique_function<R(A...)>::unique_function(F&& f, std::false_type) {
  if (detail::is_null(f))
    return;
  auto wrapped_func = [func = std::make_unique<std::decay_t<F>>(std::forward<F>(f))](A... a) {
    return detail::invoke(*func, a...);
  };
  using wrapped_func_t = decltype(wrapped_func);
  new(&buffer_) wrapped_func_t{std::move(wrapped_func)};
  vtbl_ = &detail::get_callable_vtbl<wrapped_func_t, R, A...>();
}

template<typename R, typename... A>
unique_function<R(A...)>::~unique_function() {
  if (vtbl_)
    vtbl_->destroy(buffer_);
}

template<typename R, typename... A>
unique_function<R(A...)>::unique_function(unique_function<R(A...)>&& rhs) noexcept {
  if (rhs.vtbl_)
    rhs.vtbl_->move(rhs.buffer_, buffer_);
  vtbl_ = rhs.vtbl_;
}

template<typename R, typename... A>
unique_function<R(A...)>& unique_function<R(A...)>::operator= (unique_function<R(A...)>&& rhs) noexcept {
  if (vtbl_)
    vtbl_->destroy(buffer_);
  if (rhs.vtbl_)
    rhs.vtbl_->move(rhs.buffer_, buffer_);
  vtbl_ = rhs.vtbl_;
  return *this;
}

template<typename R, typename... A>
R unique_function<R(A...)>::operator() (A... args)  {
  if (!vtbl_)
    throw std::bad_function_call{};
  return vtbl_->call(buffer_, args...);
}

} // inline namespace cxx14_v1
} // namespace portable_concurrency
