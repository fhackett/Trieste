#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <stack>
#include <string_view>
#include <utility>
#include <variant>

namespace bfs
{
  // --- this is copy-pasted from
  // https://en.cppreference.com/w/cpp/utility/variant/visit
  template<class... Ts>
  struct overloaded : Ts...
  {
    using Ts::operator()...;
  };

  template<class... Ts>
  overloaded(Ts...) -> overloaded<Ts...>;
  // --- end of copy-paste

  template<typename T>
  struct Result
  {
  private:
    Result(
      std::shared_ptr<T> value,
      std::shared_ptr<std::function<Result<T>()>> next)
    : cell{{value, next}}
    {}

  public:
    struct Cell
    {
      std::shared_ptr<T> value;
      std::shared_ptr<std::function<Result<T>()>> next;
    };

    std::optional<Cell> cell;

    Result() : cell{} {}

    Result(T value)
    : Result{
        value,
        []() { return Result{}; },
      }
    {}

    Result(T value, std::function<Result<T>()> next)
    : cell{{
        std::make_shared<T>(value),
        std::make_shared<std::function<Result<T>()>>(next),
      }}
    {}

    inline explicit operator bool() const
    {
      return bool(cell);
    }

    struct sentinel_t
    {};

    struct iterator
    {
      Result<T> result;

      inline T operator*() const
      {
        return *result.cell->value;
      }

      inline iterator& operator++()
      {
        result = (*result.cell->next)();
        return *this;
      }

      inline bool operator!=(sentinel_t) const
      {
        return bool(result);
      }
    };

    iterator begin() const
    {
      return iterator{*this};
    }

    sentinel_t end() const
    {
      return {};
    }

    sentinel_t cend() const
    {
      return {};
    }

    template<typename U>
    Result<U> map(std::function<U(T)> fn) const
    {
      if (cell)
      {
        return {
          fn(*cell->value),
          [=, *this]() { return (*cell->next)().map(fn); },
        };
      }
      else
      {
        return {};
      }
    }

    inline Result<T> concat(Result<T> rhs) const
    {
      return concat([=]() { return rhs; });
    }

    inline Result<T> concat(std::function<Result<T>()> rhs_fn) const
    {
      if (!cell)
      {
        return rhs_fn();
      }
      return {
        cell->value,
        std::make_shared<std::function<Result<T>()>>(
          [=, *this]() { return (*cell->next)().concat(rhs_fn); }),
      };
    }

    template<typename U>
    Result<U> flat_map(std::function<Result<U>(T)> fn) const
    {
      if (!cell)
      {
        return {};
      }
      Result<T> current = *this;
      Result<U> res;
      while (!res && bool(current))
      {
        res = fn(*current.cell->value);
        current = (*current.cell->next)();
      }

      if (res)
      {
        // we have one head element; defer everything else
        return res.concat([=]() { return current.flat_map(fn); });
      }
      else
      {
        // we exhausted the entire sequence trying to find a head
        return {};
      }
    }
  };

  struct CatString
  {
    using Enum = std::variant<
      std::string_view,
      std::shared_ptr<std::string>,
      std::shared_ptr<std::pair<CatString, CatString>>>;
    Enum self;

    CatString(std::string_view str) : self{str} {}
    CatString(std::string str) : self{std::make_shared<std::string>(str)} {}
    CatString(CatString lhs, CatString rhs)
    : self{std::make_shared<std::pair<CatString, CatString>>(lhs, rhs)}
    {}

    inline CatString concat(CatString rhs) const
    {
      return CatString{*this, rhs};
    }

    inline std::string str() const
    {
      std::ostringstream out;
      out << *this;
      return out.str();
    }

    friend std::ostream& operator<<(std::ostream&, const CatString&);
  };

  inline std::ostream& operator<<(std::ostream& out, const CatString& cat_str)
  {
    using Stack = std::stack<std::reference_wrapper<const CatString>>;
    Stack stack;
    stack.push(cat_str);

    while (!stack.empty())
    {
      auto str = stack.top();
      stack.pop();

      std::visit(
        overloaded{
          [&](std::string_view strv) { out << strv; },
          [&](const std::shared_ptr<std::string>& strp) { out << *strp; },
          [&](const std::shared_ptr<std::pair<CatString, CatString>>& p) {
            stack.push(p->second);
            stack.push(p->first);
          }},
        str.get().self);
    }

    return out;
  }
}
