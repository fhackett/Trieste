# Beyond Infix

The Infix language is a useful introduction to Trieste, but by its very nature as an introductory tutorial, it cannot cover more advanced concepts.
This extended tutorial covers those less-documented Trieste features, while chronicling key aspects of editing an existing Trieste-based language implementation.

Our task at hand is to extend Infix with some more advanced concepts: multiple language versions, tuples, functions (both user-defined and built-in), and destructuring assignments.
Each of these topics explores a potential difficulty and how Trieste helps resolve it:
- Multiple language versions: given time, any programming language will change, and with multiple versions "in the wild", implementations may have to accept more than one of them.
  Correctly implementing something like this also helps us manage the collection of language extensions we explore in this document, which is why we look into this first.
- Tuples: a simple form of compoud data, which has interesting syntactic interactions with the existing Infix features.
- Functions: a source of diverse scoping problems we can resolve in different ways using the tools provided by Trieste.
- Destructuring assignments: introducing a new kind of definition with complex scoping rules might seems like it requires a refactor.
  We show how such problems can be avoided in Trieste.

## Multiple Language Versions

Because a core goal of this project is to handle multiple divergent extensions of Infix, we should explain the options we have available to do this.
Most commonly, multiple versions of a language follow an evolution, where one version is a superset of the other, or a specific behavior is changed but all other features stay the same.
In Trieste, this means that we can write mostly-shared code between language versions, and when building our pass structure we can accept parameters that indicate whether a given feature should be enabled or disabled.
Ideally, this choice can be restricted to a small number, or perhaps even just one, pass that translates the possible variations into a common form that can pass through the rest of the compiler without further special cases.

The Infix tooling implements the remainder of this document using the technique above, by defining the complete resulting language in its WF definitions, then selectively disabling and altering passes in order to add and remove language features.
This means that by default a given pass either supports or ignores every Infix extension.
Feature-specific changes are annotated, and we will walk you through them as you read on.

To help identify and possibly ignore features you don't want to look at yet, the Well-Formedness (WF) definitions are written in a deliberately redundant style that looks like this:
```cpp
const auto wf =
  (Top <<= Calculation)
  | (Calculation <<= (Assign | Output)++)
  | (Assign <<= Ident * Expression)[Ident]
  | (Output <<= String * Expression)
  | (Expression <<= (Add | Subtract | Multiply | Divide | Ref | Float | Int))
  | (Ref <<= Ident)
  | (Add <<= Expression * Expression)
  | (Subtract <<= Expression * Expression)
  | (Multiply <<= Expression * Expression)
  | (Divide <<= Expression * Expression)
  // --- tuples extension ---
  | (Expression <<= (Tuple | TupleIdx | TupleAppend | Add | Subtract | Multiply | Divide | Ref | Float | Int))
  | (Tuple <<= Expression++)
  | (TupleIdx <<= Expression * Expression)
  | (TupleAppend <<= Expression++)
  // ...
  ;
```

At the top, you can see the original Infix definitions untouched.
Below, marked by `--- tuples extension ---`, you can see repeated definitions that include the added feature, tuples in this case.
In this case, the tuples feature leaves many things alone (assignments, output, etc are not touched), but overrides the `Expression` well-formedness so that it includes the added tuple expressions.
Other definitions do similar things, partially overriding the definitions above it.

A reader focusing on earlier parts of this document can safely ignore the definitions below, and read the simpler language as-is.
Similarly, rules mentioning the ommitted constructs can be ignored.
If you write programs that use only features you know, rules regarding unknown features are unreachable.
The implementation will also reject inputs that use features they are not supposed to, and defaults to the original Infix language.

Note that not all language designs support this kind of layering.
Some design decisions went into ensuring the implementation was easy to follow as you read the tutorial, as opposed to supporting all variations of a feature, and we leave exploring those possibilities as an exercise for the reader.
One such feature is removing the requirement that functions are defined before they are used - many popular programming languages allow definitions in any order, and so can we with a little effort.
For more of a challenge, consider that with this feature it becomes possible to write recursive, or even mutually recursive, functions.
Either detect and reject that case as an error, or try implementing recursive function evaluation.

TODO: config arguments, and the risk of keeping references to temporaries.
Copying the config object is often fine, especially if it's just a few boolean flags.
Same goes for other things that go inside the pass objects - there can be subtle differences between correct and incorrect code due to taking references to locals without realizing.
From personal experience, a pattern constructor silently taking a reference to a local pointer that referred to a static storage function, rather than copying the pointer, caused problems at least once.
If in doubt, remember to enable AddressSanitizer or equivalent to check that your pass hasn't been constructed with some bad pointers somewhere.

## Tuples in Infix

To start with, what counts as a tuple?
It's an immutable data structure that holds 0 or more ordered elements of any type (for some notion of type).
There are a few ways to write them, but they usually involve separating the elements with commas (`,`).
They are often written surrounded by parentheses, as in `(a, b, c)`, but some languages (like Python) make the parentheses optional, meaning that `a, b, c` on its own is a valid tuple.
Some languages allow trailing commas at the end of a tuple, as in `(a, b, c,)`, which can be helpful when writing multiline literals.
The empty tuple and one-element tuple are almost universally edge cases, where you need to write something like `(,)` for an empty tuple.
A one-element tuple must then be written `(x,)`, with a mandatory trailing comma to disambiguate from `(x)`, which just means `x` instead with no tuple.

This section documents how Trieste can be used to implement tuples that have this syntax, with both mandatory and optional parentheses.
Two of our methods make minimal additions to the parser and express the majority of their logic as additional rewrite rules.
We also discuss a third method, which front-loads much of the parsing workload onto the parser, trading in some language design flexibility for simpler reader rules.

### Extending the AST

A neat thing about Trieste is that, regardless of how tuples are lexically written, all versions of tuples we discuss have the same AST.
To start with, we only introduce tuple literals -- we will consider how to extract elements from a tuple and append tuples to one another later.

For just tuple literals, the AST needs two key extensions: a new token type, and extending the set of possible expressions.

The token definition is like any other, with no special flags.
```cpp
inline const auto Tuple = TokenDef("infix-tuple");
```

The AST can be extended in 2 lines, adding tuples as a type of expression, and specifying that tuple literals can have 0 or more expressions as children:
```cpp
const auto wf =
  // ...
  | (Expression <<= (Tuple | Add | Subtract | Multiply | Divide | Ref | Float | Int))
  | (Tuple <<= Expression++)
```

We will come back to this definition when adding the other two tuple primitives, but all similar changes will follow the same formula.
More importantly, this shows how little difference many lexical-level or syntax sugar-level changes can make to a Trieste based language's main AST, since a lot of nuance can be removed when reading the language, before reaching the main AST.

### The Simplest Parser Modification

Lexically speaking, the simplest way to extend the parser for reading tuples is to add a comma token that directly corresponds to the `,` character.

The token definition is standard for Trieste:
```cpp
inline const auto Comma = TokenDef("infix-comma");
```

Unlike tokens used in the public interface for Infix, however, this token can be kept as internal since it will be eliminated and replaced by either `Error` or `Tuple` expressions by the reader.

Then, we can allow the parser to generate `Comma` tokens when it sees a lexical comma:
```cpp
R"(,)" >> [](auto& m) {
  m.add(Comma);
},
```

This action follows the same template as the arithmetic operators already in the language.
No context or complex logic is used here; subequent passes will make sense of what these commas mean.

As an aside on adding more token types, we find that defining more token types with specific syntactic meaning leads to clearer programs.
In principle, it would be possible to re-use the `Tuple` token and not introduce an additional definition, but this would make the implementation's behavior less clear.
Not only might commas be used outside of tuples (imagine adding C-style function calls with comma-separated arguments to Infix!), but re-using a token weakens Trieste's patterns, fuzzing, and well-formedness facilities.

Changing something from one token type to another is easily monitored using well-formedness definitions, and can be easily detected using patterns.
Using the same token type to mean multiple things, especially when it means one thing in the input of a pass and another in the pass's output, opens us up to confusing situations.
It might hide a rule not applying at all when it should (unchanged AST satisfies output WF), and it can make it easier for a rule to apply to its own output in an infinite loop (pattern does not exclude rule output).
While not always possible, making the intended difference clear by changing a token type can help avoid these cases.

### Tuples as Low-precedence N-ary Operators

Compared to how Infix already works, the simplest way of parsing tuples, assuming we do not want to require parentheses around a tuple expression, is to treat `,` as a lowest-precedence infix operator.
That is, `x + y, y * z` becomes grouped as `(x + y) , (y * z)`.
Because of the meaning of parentheses as a grouping mechanism, `(x, y)` will always work if `x, y` works, meaning that this technique parses a strict superset of tuples that require parentheses.

This technique is not without its share of interesting semantics and edge cases however, the most prominent being the meaning of `x, y, z`: the expression should parse as a 3-element tuple, but if we just recognize `,` as a binary operator like `+` or `-`, we might parse `((x, y), z)` instead.
That is why this section title mentions "N-ary Operators".
Instead of just parsing `(expr) , (expr)` in one step, we must recognize all commas in the same expression as describing the same tuple, one which becomes larger and larger the more commas our expression contains.
We also need to deal with the special exception, the unary tuple, where a trailing comma is mandatory because only `x,` is a tuple, not `x`.

To do this, write a collection of recursive rules that leverage the fact that nested sub-expressions must have already been parsed into single nodes of type `Expression`.
This includes parenthesized groups, meaning that if the programmer actually writes `(x, y), z`, then the outermost expression will contain `(expr x , y) , z`, which "hides" the comma in the parentheses from consideration.
By adding parentheses, it remains possible to write nested tuples, rather than having all tuples unconditionally flatten themselves into one large tuple literal.

Here are the cases we use to parse tuples in this style, with rules numbered 1-5:
```cpp
// (1) nullary tuple, just a `,` on its own
T(Expression) << (T(Comma)[Comma] * End) >>
  [](Match& _) { return Expression << (Tuple ^ _(Comma)); },
// (2) two expressions comma-separated makes a tuple
In(Expression) *
    (T(Expression)[Lhs] * T(Comma) * T(Expression)[Rhs]) >>
  [](Match& _) { return (Tuple << _(Lhs) << _(Rhs)); },
// (3) a tuple, a comma, and an expression makes a longer tuple
In(Expression) *
    (T(Tuple)[Lhs] * T(Comma) * T(Expression)[Rhs]) >>
  [](Match& _) { return _(Lhs) << _(Rhs); },
// (4) the one-element tuple uses , as a postfix operator
T(Expression) <<
    (T(Expression)[Expression] * T(Comma) * End) >>
  [](Match& _) { return Expression << (Tuple << _(Expression)); },
// (5) one trailing comma at the end of a tuple is allowed (but not more)
T(Expression) << (T(Tuple)[Tuple] * T(Comma) * End) >>
  [](Match& _) { return Expression << _(Tuple); },
```

Crucially, note that several rules apply to the results of other rules, especially rule 3.
This pass makes use of Trieste's rewrite-until-done semantics.

To give an example of how these rules apply, consider how our first example, `x, y, z`, is parsed:
- Start: `(expr x) , (expr y) , (expr z)`
- Rule 2 --> `(tuple (expr x) (expr y)) , (expr z)`
- Rule 3 --> `(tuple (expr x) (expr y) (expr z))`

For the same example with a trailing comma, we need rule 5 to account for that:
- Same as above --> `(tuple (expr x) (expr y) (expr z)) ,`
- Rule 5 --> `(tuple (expr x) (expr y) (expr z))`

Note that rule 5 can only apply once, ensuring that we do not accept nonsense expressions like `x, y,,,,,` with too many trailing commas.
We use the `End` pattern to indicate that we only accept the extra comma if it is the last token in the expression; if there was another comma after it, the pattern will not match and the extra comma should be caught by an error handling catch-all pattern.

Rules 1 and 4 handle cases where rule 2 cannot match: since rule 2 catches tuples of length 2 and above by separating 2 expressions with a comma, rule 1 catches tuples of length 0, and rule 4 catches tuples of length 1.
Rule 1 uses `End` again, and avoids `In` to ensure we catch only expressions with a single `,`.
Remember that the key difference between `In` and `<<` is that `In` (a) does not become part of the pattern and (b) does not require the pattern match to start at the beginning of the specified node.
Using `<<` lets us specifically talk about the entire contents of a node, starting at the beginning.
Similarly, rule 4 deals with unary tuples where, unlike rule 5, there cannot be an under-construction tuple to match because that requires at least 2 elements.

To show rule 1 in action, consider the expression `,`, to which no rules except 1 apply:
- Start: `,`
- Rule 1 --> `(tuple)`

To show rule 4 in action, consider the expression `x ,`, where `x` is not a tuple so rule 5 does not apply:
- Start `x ,`
- Rule 4 --> `(tuple x)`

Remember also that, because this pass goes after all the arithmetic operator passes, we can deal with nested arithmetic as well.
For the expression `x + y * 2 , x - y`, we get:
- Start: `(expr (x + (y * 2))) , (expr x - y)` (already parsed `+`, `-`, `*`, etc... `,` is treated as an unknown operator by those passes)
- Rule 2 --> `(tuple (expr (x + (y * 2))) (expr x - y))`

**Exercise to the reader:**
can you rewrite this pass with fewer rules?
If you could find a way to consolidate rule 2 into rule 4, then things might be simpler... but you might run into trouble dealing with trailing commas in the end.
Remember that you can always add new token types, like `UnderConstructionTuple`, to disambiguate cases like this where multiple otherwise-identical situations end up with an over-general name like `Tuple`.
You can always use the testing infrastructure in this repository to validate your work.

### Tuples as a Special Kind of Group

TODO: problem is that it might not be easy to tell if a group has commas in it or not.
TODO: we can mark all groups with a "tuple candidate" node, do our operator parsing, and then group things up by shifting commas into tuples

TODO: we could actually hand-write a rule that looks for commas directly

### Parser Tuples

TODO: how to make the parser read the tuples with .seq

TODO: notice that .seq always allows a trailing instance of the separator. This surprisingly allows our .seq based assignment to accept `x = y =` with no way to work around it.

### Evaluating a Tuple Expression

TODO: use Expression --> Literal as "pseudo-types" to track whether something has evaluated

TODO: a previous version of this tutorial used int and float nodes directly, rather than using a Literal prefix.
This is more compact for a very simple language, but it doesn't scale since it essentially makes you write out all possible nodes a value can be.
It is more scalable to describe a broad category, like our "literal" token, which allows us to generically identify evaluated and unevaluated terms even if the language changes to some extent.

TODO: how to represent (as WF) the intermediate state where we can have "stuck expressions", so we can catch errors. Yes, errors may well end up in a second pass if you're relying on fixpoints.

### Appending and Indexing

TODO: appending is a pseudo-function; our lang doesn't have functions so we special-case it with a keyword, like language builtings often are
TODO: indexing is a highest-precedence binop (excercise to reader, highlight gotchas)

TODO: for evaluation, these operations do not add any new literal types; they should all disappear in a fully evaluated program

### Error Discipline, Fuzzing, and Test Cases

TODO: fuzz testing is useful to check that your program doesn't outright crash, but it is easy to pass WF if your code emits `Error` or something valid-looking, even if it doesn't make sense.

TODO: explain the principles behind the `.expected` tests, why they matter, and some general guidance on how to write/maintain them. Consider also storing test collections as data, like JSON or YAML (which has good multiline tring embeds).

TODO: what do I even do with BFS?

--- older notes

Notes:
- it's possible to get lost combining tuple and call parsing
- "stuck terms" and the original version of the code; explain that you can do sometimes error handling in 1 pass but it doesn't really scale once you have many ways to fail (TODO: edits to the original tutorial, where there is only one way to have an evaluation succeed and cause an error (e.g. `1 / (1-1)`))
- p.s. and splitting one pass into 2 can cause `String * (Foo >>= Literal | Expression)` because one term becomes a `|`. Warn people for now, plot design improvements later.

Ideas:
- `(a, b, ...)` (note: do in parser, or have a `,` token and resolve with a pass)
- `a, b, ...` (note: interpret `,` as a binop? trouble with distinguishing tuples of tuples vs wider tuples?)
- consider more exotic delimiters? (`<<>>` from TLA+, or something)
- append, fst, snd type builtins (without explicit fns?)
- how might one implement a "prelude"? (omni-present collection of definitions that are implicitly available everywhere)

## n Ways to Design Functions

Ideas:
- call as name + syntactic tuple? e.g. `fn(a, b, ...)`
- what if the parameter position of a function call is special, e.g., named parameters or special markers?
- only currying? `fn a b ...` (function application becomes an invisible operator, which is interesting)
- first or second-class? How much does it cost to switch? (first class trivially subsumes second class, but we have to reject more programs... going second class to first class requires a fun desugaring, which you can just about do with tuples)
- lazy params (don't start with this one, but it's a nasty stress test to add it as a feature... rip off Scala's trick if we do, probably)

## Pattern Languages Lite: Destructuring Assignments

Ideas:
- Pattern in assignment, and possibly function argument position, to match and extract values from tuples, however we did tuple syntax.
- Discuss the magic `_` everyone uses.
- Can we use values (literals at least) in pattern position, as a kind of assertion? How expensive is that to change?
