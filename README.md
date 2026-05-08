# mruby-mustache

A small, fast, logic-less template engine for mruby. Written entirely in C
against mruby core; no Ruby code, no extension gems, no external
dependencies.

## Migration from v1 — breaking changes

**v2 is a full rewrite.** The implementation is now C; the public Ruby API
is unchanged in shape but stricter in semantics.

What changed:

- **Extension flags are gone.** `Mustache::With_*` constants no longer
  exist. The legacy `Mustache.mustache(template, data, flags)` call
  still accepts a flags argument, but it is silently ignored.
- **Equality / comparison / hash-iteration / JSON-pointer extensions
  are gone.** Templates using `{{#status=ok}}…{{/status=ok}}`,
  `{{#x>5}}`, `{{#hash.*}}`, or `/`-prefixed keys parse those characters
  as literal key names rather than as operators.
- **Partials are supported, but only as pre-compiled `Template` objects.**
  No file loading, no string-source partials. Caller compiles partials
  themselves and passes a hash to `render`.
- **Error class hierarchy simplified.** v1's many subclasses are gone.
  Errors now inherit from `Mustache::Error` and are exactly:
  `Mustache::ParseError` and `Mustache::RenderError`.
- **Context lookup is to_s-based.** Hash keys are matched by their
  `to_s` form against the parsed tag name. String keys, symbol keys,
  integer keys, anything that stringifies — all work uniformly. No
  silent string↔symbol crossover, no integer special-casing; just
  one rule.

What's the same:

- `{{var}}`, `{{{var}}}`, `{{&var}}`, `{{#sec}}`, `{{^sec}}`,
  `{{!comment}}`, `{{.}}`, and dotted paths all behave per spec.
- HTML escaping in `{{var}}`.
- Standalone-line whitespace handling, including indent propagation
  for partials.
- Hash, array, scalar truthiness rules for sections.

If you relied on extension flags or extension syntax, this version is not
a drop-in upgrade. Pin to v1 or migrate by computing extension behavior
in your data preparation step (e.g. compute `is_ok` upstream and use
`{{#is_ok}}…{{/is_ok}}` instead of `{{#status=ok}}…{{/status=ok}}`).

## Install

Add to your `build_config.rb`:

    conf.gem github: 'Asmod4n/mruby-mustache'

## Quick start

    template = Mustache::Template.compile(<<~TMPL)
      Hello, {{name}}!
      {{#items}}
        - {{.}}
      {{/items}}
      {{^items}}
      No items yet.
      {{/items}}
    TMPL

    template.render({
      'name'  => 'world',
      'items' => ['a', 'b', 'c'],
    })
    # => "Hello, world!\n  - a\n  - b\n  - c\n"

## API

### `Mustache::Template.compile(source) → Template`

Parse a template once. Raises `Mustache::ParseError` on unclosed or
mismatched tags. The returned `Template` is reusable and safe to share
across many renders.

    template = Mustache::Template.compile("Hi {{name}}")

### `Template#render(context = nil, partials = nil) → String`

Render the template against a context. The optional second argument is
a Hash mapping partial names to pre-compiled `Template` objects.

    template.render({ 'name' => 'Alice' })
    # => "Hi Alice"

    page = Mustache::Template.compile("{{>header}}\n{{body}}")
    head = Mustache::Template.compile("=== {{title}} ===")
    page.render({ 'title' => 'Home', 'body' => 'hi' }, { 'header' => head })
    # => "=== Home ===\nhi"

Missing keys render as empty strings, per the mustache spec. Missing
partials, or partials whose value is not a `Template` instance, also
render as empty.

### `Template#tags → Array<String>`

Returns a deduplicated, first-appearance-ordered list of names referenced
in the template. Useful for tooling and validation.

    template = Mustache::Template.compile("{{a}} {{#b}}{{c.d}}{{/b}}")
    template.tags  # => ["a", "b", "c.d"]

To validate that a data hash covers all template tags:

    missing = template.tags.reject do |t|
      t == '.' || data.dig(*t.split('.'))
    end
    raise "missing keys: #{missing}" unless missing.empty?

### `Mustache.mustache(template, context = nil) → String`

One-shot convenience. Compiles and renders in one call. For repeated
rendering, use `Template.compile` and reuse the template object. The
legacy third `flags` argument is accepted and ignored for compatibility.

## Supported syntax

| Syntax | Meaning |
|---|---|
| `{{var}}` | Interpolate, HTML-escaped |
| `{{{var}}}` | Interpolate, raw (no escape) |
| `{{&var}}` | Interpolate, raw (alias for `{{{}}}`) |
| `{{#sec}}…{{/sec}}` | Section: render block if truthy, iterate if array |
| `{{^sec}}…{{/sec}}` | Inverted section: render block if falsy/empty |
| `{{>name}}` | Partial: render the named pre-compiled `Template` |
| `{{!comment}}` | Comment, stripped from output |
| `{{.}}` | Current iteration value |
| `{{a.b.c}}` | Dotted path |

### Sections

A section behaves as follows depending on the value of its key:

- **Array** → iterate, render the block once per element with that element pushed onto the context stack
- **Hash** → render once with the hash pushed onto the stack
- **truthy scalar** (string, number, `true`) → render once with the scalar pushed onto the stack
- **`false`, `nil`, empty array, empty hash** → skip the block

Inverted sections render the block exactly when a regular section would skip it.

### Context lookup

The library looks up keys in this order:

1. Walk the context stack from innermost to outermost frame.
2. For each frame that's a `Hash`, find the first key whose `to_s`
   matches the parsed tag name. String keys match directly; symbols
   match via `Symbol#to_s`; integers via `Integer#to_s`; anything
   else via whatever `to_s` returns.
3. Once the first segment is found, drill into remaining dotted parts
   on the resolved value (no further upward walking).

This means `{{user.email}}` finds `user` in the nearest scope that has
it, then takes `email` from that user. If any segment fails, the whole
expression renders empty.

### Partials

Partials are looked up in the hash passed as the second argument to
`render`. Values must be `Mustache::Template` instances. The library
never compiles partial source at render time and never touches the
filesystem.

    header = Mustache::Template.compile("=== {{title}} ===\n")
    page   = Mustache::Template.compile("{{>header}}body\n")

    page.render({ 'title' => 'Hi' }, { 'header' => header })
    # => "=== Hi ===\nbody\n"

Recursion is bounded by `MUSTACHE_MAX_PARTIAL_DEPTH` (64 by default);
exceeding it raises `Mustache::RenderError`.

Indentation propagation is implemented per spec: a partial preceded by
whitespace on a standalone line gets that whitespace prepended to every
line of its output (but not to lines that come from interpolated values).

### HTML escaping

`{{var}}` HTML-escapes its value (`&`, `<`, `>`, `"`, `'`). `{{{var}}}`
and `{{&var}}` render raw. Use raw forms for values you've already
escaped or that contain trusted markup; use the default escaped form
for anything originating from untrusted input.

## Standalone-line whitespace

Per spec, lines containing only whitespace and standalone-eligible tags
(sections, inverted sections, closing tags, comments, partials) are
stripped entirely including the trailing newline:

    Hello!
    {{#items}}
      - {{.}}
    {{/items}}
    Goodbye.

renders as `Hello!\n  - a\n  - b\nGoodbye.\n`, not with extra blank
lines where the section markers were.

Interpolation tags (`{{var}}`, `{{{var}}}`, `{{&var}}`) are never
standalone. Indentation around them is preserved.

## Spec conformance

mruby-mustache passes every test it claims to support from the
[mustache spec v1.4.3](https://github.com/mustache/spec):

- `comments` ✓
- `interpolation` ✓
- `sections` ✓
- `inverted` ✓
- `partials` ✓ (basic, failed lookup, context, recursion, standalone indentation)

The implementation **deliberately omits** these spec sections:

- `delimiters` — set-delimiter tags (`{{=<% %>=}}`)
- `~lambdas` — code as data
- `~dynamic-names` — `{{>*name}}`
- `~inheritance` — `{{<parent}}{{$block}}…{{/parent}}`

These omissions are by design. Set delimiters mutate parser state
mid-parse; lambdas turn the template language into an executable one;
the optional modules add substantial parser surface for niche use cases.

## Performance

Benchmarked rendering a small blog-post template (9 interpolations,
3-element iteration). Wall-clock per render, including process startup
and template compile, divided by iteration count:

| Implementation | Time per render | vs Rust |
|---|---|---|
| **mruby (this gem, v2)** | **~350 ns** | **0.6×** |
| Rust `mustache` crate (runtime API) | ~560 ns | 1.0× |

For pure mruby with no JIT and no external dependencies, this puts the
gem in the same performance class as compiled-Rust template libraries
operating on dynamic data.

## Configuration

Three compile-time `#define`s control resource limits:

- `MUSTACHE_OUTBUF_STACK` (default `64 * 1024`) — size of the C stack
  buffer used for render output. Renders larger than this promote to
  a heap-backed mruby String. Useful to lower on embedded targets with
  small thread stacks.
- `MUSTACHE_MAX_DEPTH` (default `32`) — maximum context-stack nesting
  depth (sections within sections within sections...). Exceeding raises
  `Mustache::RenderError`.
- `MUSTACHE_MAX_PARTIAL_DEPTH` (default `64`) — maximum recursive
  partial-call depth. Exceeding raises `Mustache::RenderError`.

Override via your `build_config.rb`:

    conf.gem github: 'Asmod4n/mruby-mustache' do |g|
      g.cc.defines << 'MUSTACHE_OUTBUF_STACK=8192'
    end

## Design notes

- **Single C file (`src/mustache.c`).** No Ruby code beyond tests.
- **Uses only mruby's native data types** — `Array`, `Hash`, `String`,
  `Integer`. No custom `MRB_TT_DATA` types, no GC mark or free hooks.
  Compiled ops are stored as `Array<Array>`, GC'd naturally.
- **Stack buffers for transient data** — render output up to 64 KB and
  the context stack live on the C stack; only the final result allocates
  a Ruby `String`. Renders that exceed the stack budget transparently
  promote to a heap-backed String.
- **`mrb_str_byte_subseq` for source slicing** — text segments and key
  parts in compiled ops share storage with the source string instead
  of copying.
- **Hash lookup via `mrb_hash_foreach` + `mrb_obj_as_string`** —
  one C-level pass per frame, no allocation per lookup attempt.

## Errors

All errors inherit from `Mustache::Error`:

- `Mustache::ParseError` — malformed templates (unclosed tags,
  mismatched closing tags, empty keys, empty partial names)
- `Mustache::RenderError` — runtime limits exceeded (max nesting depth,
  max partial recursion); should not appear in normal use

## Testing & fuzzing

The gem ships with:

- A full unit test suite in `test/`, covering API surface and
  behavioral edges
- The official mustache spec v1.4.3 conformance tests for the four
  sections this implementation covers (plus the partials section)
- A libFuzzer harness in `fuzz/`, with build script and seed corpus

Run tests via `rake test` from the mruby build directory. To fuzz:

    ./fuzz/run.sh build       # build harness with libFuzzer + ASan + UBSan
    ./fuzz/run.sh 300         # fuzz for 300 seconds
    ./fuzz/run.sh             # fuzz forever

The current release passes the test suite cleanly under ASan and has
been run through libFuzzer for tens of thousands of iterations without
findings.

## License

Apache-2.0
```