#include "CLI/App.hpp"
#include "CLI/Error.hpp"
#include "progspace.h"
#include "test_util.h"

#include <CLI/CLI.hpp>
#include <ios>
#include <sstream>

int main(int argc, char** argv)
{
  CLI::App app;
  try
  {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError& e)
  {
    app.exit(e);
  }

  struct StringTestExpected
  {
    bool tuple_parens_omitted;
    std::string str;

    bool operator==(const StringTestExpected&) const = default;

    inline explicit operator std::string() const
    {
      std::ostringstream out;
      out << "{" << std::endl
          << "  .tuple_parens_omitted = " << std::boolalpha
          << tuple_parens_omitted << ";" << std::endl
          << "  .str = \"" << str << "\";" << std::endl
          << "}";
      return out.str();
    }
  };

  auto expecteds_to_str = [](const std::vector<StringTestExpected>& vec) {
    std::ostringstream out;
    out << vec;
    return out.str();
  };

  struct StringTest
  {
    trieste::Node input;
    std::vector<StringTestExpected> expected;
  };

  using namespace infix;

  std::vector<StringTest> string_tests =
    {
      {
        .input = Calculation
          << (Assign << (Ident ^ "foo")
                     << (Expression
                         << ((Add ^ "+")
                             << (Expression << (Int ^ "0"))
                             << (Expression
                                 << ((Add ^ "+")
                                     << (Expression << (Int ^ "1"))
                                     << (Expression << (Int ^ "2"))))))),
        .expected =
          {
            // {
            //   .tuple_parens_omitted = false,
            //   .str = "foo = 0 + 1 + 2;",
            // },
            {
              .tuple_parens_omitted = false,
              .str = "foo = 0 + (1 + 2);",
            },
            // {
            //   .tuple_parens_omitted = false,
            //   .str = "foo = (0 + 1 + 2);",
            // },
            {
              .tuple_parens_omitted = false,
              .str = "foo = (0 + (1 + 2));",
            },
          },
      },
      {
        .input = Calculation
          << (Assign << (Ident ^ "foo")
                     << (Expression
                         << ((Add ^ "+")
                             << (Expression
                                 << ((Add ^ "+")
                                     << (Expression << (Int ^ "0"))
                                     << (Expression << (Int ^ "1"))))
                             << (Expression << (Int ^ "2"))))),
        .expected =
          {
            {
              .tuple_parens_omitted = false,
              .str = "foo = 0 + 1 + 2;",
            },
            {
              .tuple_parens_omitted = false,
              .str = "foo = (0 + 1) + 2;",
            },
            {
              .tuple_parens_omitted = false,
              .str = "foo = (0 + 1 + 2);",
            },
            {
              .tuple_parens_omitted = false,
              .str = "foo = ((0 + 1) + 2);",
            },
          },
      },
      {
        .input = Calculation
          << (Assign << (Ident ^ "foo")
                     << (Expression
                         << (Tuple << (Expression << (Int ^ "1"))
                                   << (Expression << (Int ^ "2"))
                                   << (Expression << (Int ^ "3"))))),
        .expected =
          {
            {
              .tuple_parens_omitted = true,
              .str = "foo = 1, 2, 3;",
            },
            {
              .tuple_parens_omitted = true,
              .str = "foo = 1, 2, 3,;",
            },
            {
              .tuple_parens_omitted = false,
              .str = "foo = (1, 2, 3);",
            },
            {
              .tuple_parens_omitted = false,
              .str = "foo = (1, 2, 3,);",
            },
          },
      },
    };

  for (const auto& test : string_tests)
  {
    std::vector<StringTestExpected> actual;

    for (auto render : progspace::calculation_strings(test.input))
    {
      actual.push_back({
        .tuple_parens_omitted = render.tuple_parens_omitted,
        .str = render.str.str(),
      });
    }

    if (test.expected != actual)
    {
      std::cout << "Unexpected stringification for:" << std::endl
                << test.input << std::endl
                << "Expected:" << std::endl
                << test.expected << std::endl
                << "Actual (diffy print):" << std::endl;
      diffy_print(
        expecteds_to_str(test.expected), expecteds_to_str(actual), std::cout);
      return 1;
    }
  }
  std::cout << "All ok." << std::endl;

  return 0;
}
