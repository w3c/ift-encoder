#ifndef COMMON_TRY_
#define COMMON_TRY_

#define TRYV(...)              \
  do {                         \
    auto res = (__VA_ARGS__);  \
    if (!res.ok()) return res; \
  } while (false)

#define TRY(...)                                   \
  ({                                               \
    auto res = (__VA_ARGS__);                      \
    if (!res.ok()) return std::move(res).status(); \
    std::move(*res);                               \
  })

#endif  // COMMON_TRY_