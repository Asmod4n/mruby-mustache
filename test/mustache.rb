# mruby raises through a C++ throw here, never a longjmp: a gem with a
# .cpp in src/ turns on MRB_USE_CXX_EXCEPTION for the whole build
# (mruby lib/mruby/build/load_gems.rb). So a raise out of the middle of
# a render destroys what the render held, and the template renders again.
assert('a render that raises leaves the template whole') do
  t = Mustache::Template.new('{{#rows}}{{a}}{{/rows}}')
  assert_raise(TypeError) { t.render({ rows: [{ a: 'x' }, { a: 1 }] }) }
  assert_equal 'xy', t.render({ rows: [{ a: 'x' }, { a: 'y' }] })
end

# The second argument is the largest answer a render may give, not a
# buffer: a template holds its program and a size hint, and a render
# makes one String of the size the hint expects. So a large limit costs
# nothing until an answer is that large.
assert('an answer limit allocates nothing') do
  t = Mustache::Template.new('x{{v}}', 2**60)
  assert_equal 'xy', t.render({ v: 'y' })
end

# An escaped value is written into the room above the answer, less the 32
# bytes the wide escape may store past its end. A raw value that does not
# fit grows the answer to twice its size, or to exactly what it needs when
# that is more; a fresh template starts at 75 bytes, so a raw value over
# 150 bytes leaves the answer full to its last byte. The room above it is
# then none rather than a negative number, and the escape after it has to
# grow the answer, not write past it.
assert('an escaped value right after a raw write that filled the answer') do
  (151..200).each do |n|
    t = Mustache::Template.new('{{{a}}}{{b}}')
    assert_equal ('x' * n) + '&lt;&amp;' * 50, t.render({ a: 'x' * n, b: '<&' * 50 })
  end
end

# The answer starts at the size the hint learned from the renders before
# it and grows when a render outgrows it, raw text through mrb_str_cat
# and an escaped value through a resize that leaves room for the wide
# escape. What comes back is the same, whatever the hint said.
assert('an answer larger than the hint grows and stays right') do
  t = Mustache::Template.new('{{a}}{{{b}}}{{c}}')
  assert_equal 'x', t.render({ a: 'x' })
  big = '<&>' * 5000
  raw = 'r' * 10_000
  assert_equal '&lt;&amp;&gt;' * 5000 + raw + 'z', t.render({ a: big, b: raw, c: 'z' })
  assert_equal 'x', t.render({ a: 'x' })
  assert_equal raw, t.render({ b: raw })
end

assert('a size cannot be negative') do
  assert_raise(ArgumentError) { Mustache::Template.new('x', -1) }
  assert_raise(ArgumentError) { Mustache::Template.new('x', 16, -1) }
end

assert('an empty template renders nothing, text renders as it is') do
  assert_equal '', Mustache::Template.new('').render({})
  assert_equal 'static text', Mustache::Template.new('static text').render({})
end

# Class#allocate makes a Template that initialize never saw: it holds no
# program. mruby refuses to hand out its data, as for any data object
# that was never initialized.
assert('a template that was never initialized refuses to render') do
  assert_raise(TypeError) { Mustache::Template.allocate.render({}) }
end

# A key is a Symbol: the lookup is mrb_hash_fetch with a Symbol, which
# mruby answers without running Ruby. A String key is a different key.
assert('a key is a Symbol') do
  t = Mustache::Template.new('Hi {{name}}')
  assert_equal 'Hi alice', t.render({ name: 'alice' })
  assert_equal 'Hi ', t.render({ 'name' => 'alice' })
end

# A value is a String. Nothing is converted, because a conversion is a
# call into Ruby while the render holds pointers into the data.
assert('a value that is not a String is a TypeError') do
  t = Mustache::Template.new('{{v}}')
  assert_raise(TypeError) { t.render({ v: 42 }) }
  assert_raise(TypeError) { t.render({ v: true }) }
  assert_raise(TypeError) { t.render({ v: :sym }) }
  assert_raise(TypeError) { Mustache::Template.new('{{{v}}}').render({ v: 1.5 }) }
end

assert('a missing key, nil and false render nothing') do
  t = Mustache::Template.new('x={{v}}')
  assert_equal 'x=', t.render({})
  assert_equal 'x=', t.render({ v: nil })
  assert_equal 'x=', t.render({ v: false })
end

assert('sections: truthy, falsy, empty, lists, maps') do
  t = Mustache::Template.new('{{#s}}on{{/s}}')
  assert_equal 'on', t.render({ s: true })
  assert_equal '', t.render({ s: false })
  assert_equal '', t.render({ s: nil })
  assert_equal '', t.render({ s: [] })
  assert_equal '', t.render({ s: {} })
  assert_equal 'on', t.render({ s: { a: 'b' } })
  assert_equal 'onon', t.render({ s: [1, 2] })
  i = Mustache::Template.new('{{^s}}no{{/s}}')
  assert_equal 'no', i.render({ s: false })
  assert_equal 'no', i.render({ s: [] })
  assert_equal '', i.render({ s: true })
end

assert('a list section sees its element and the scopes around it') do
  t = Mustache::Template.new('{{#users}}{{greeting}} {{name}};{{/users}}')
  data = { greeting: 'hi', users: [{ name: 'alice' }, { name: 'bob' }] }
  assert_equal 'hi alice;hi bob;', t.render(data)
  assert_equal 'a,b,', Mustache::Template.new('{{#xs}}{{.}},{{/xs}}').render({ xs: %w[a b] })
end

assert('a dotted key walks into maps') do
  data = { a: { b: { c: 'deep' } } }
  assert_equal 'deep', Mustache::Template.new('{{a.b.c}}').render(data)
  assert_equal '', Mustache::Template.new('{{a.x.c}}').render(data)
end

# {{ }} escapes the five bytes HTML gives a meaning to. The escaper reads a
# value 32 bytes at a time on AVX2 and 16 on NEON, and byte by byte below
# that, so values of every length around those blocks are checked, with
# marks at every position of a block.
assert('{{ }} escapes & < > " and \', {{{ }}} and {{& }} do not') do
  t = Mustache::Template.new('{{v}}|{{{v}}}|{{&v}}')
  assert_equal %q(&amp;&lt;&gt;&quot;&#39;|&<>"'|&<>"'), t.render({ v: %q(&<>"') })
  e = Mustache::Template.new('{{v}}')
  (0..100).each do |len|
    (0...len).each do |at|
      next unless at % 7 == 0
      v = 'a' * len
      v[at] = '<'
      assert_equal v.gsub('<', '&lt;'), e.render({ v: v })
    end
  end
  dense = %q(&<>"') * 40
  want = dense.gsub('&', '&amp;').gsub('<', '&lt;').gsub('>', '&gt;').gsub('"', '&quot;').gsub("'", '&#39;')
  assert_equal want, e.render({ v: dense })
end

assert('comments are dropped') do
  assert_equal 'ab', Mustache::Template.new('a{{! ignored }}b').render({})
end

# A template that does not compile says what and where: the title of the
# problem and the byte its tag starts at.
assert('a template that does not compile is a ParseError that names the byte') do
  {
    'ab{{{x}}' => 'unclosed {{{ at byte 2',
    'ab{{x' => 'unclosed {{ at byte 2',
    'a{{ }}' => 'empty tag at byte 1',
    '{{#}}{{/}}' => 'empty key at byte 0',
    '{{a..b}}' => 'a key has an empty segment at byte 0',
    '{{> }}' => 'empty partial name at byte 0',
    'x{{/a}}' => 'a closing tag without an opening tag at byte 1',
    '{{#a}}{{/b}}' => 'a closing tag names another section than the one it closes at byte 6',
    'xy{{#a}}' => 'an unclosed section at byte 2',
    'a{{=<%=}}' => 'a Set Delimiter tag needs two delimiters at byte 1',
    'a{{= <% =}}' => 'a Set Delimiter tag needs two delimiters at byte 1',
    'a{{=<% %>}}' => 'unclosed {{ at byte 1',
    '{{$ }}{{/ }}' => 'empty block name at byte 0',
    '{{< }}{{/ }}' => 'empty parent name at byte 0',
    '{{$a}}{{/b}}' => 'a closing tag names another section than the one it closes at byte 6',
    '{{<a}}' => 'an unclosed section at byte 0',
  }.each do |src, message|
    e = assert_raise(Mustache::ParseError) { Mustache::Template.new(src) }
    assert_equal message, e.message
  end
end

# A standalone line gives its indent to every partial, parent and block
# tag on it. The compiler kept one copy of the indent per tag, so one line
# of N spaces and M tags compiled to N * M bytes: a 64 KiB source inside
# max_source_bytes grew to 176 MiB. The indent is now kept once per line,
# so the texts of a program are never larger than its source.
assert('a standalone line with many tags compiles to no more than its source') do
  line = (' ' * 4096) + ('{{>a}}' * 1000) + "\n"
  assert_true MustacheTest.compiled_texts_size(line) <= line.bytesize
  blocks = (' ' * 4096) + ('{{$b}}{{/b}}' * 500) + "\n"
  assert_true MustacheTest.compiled_texts_size(blocks) <= blocks.bytesize
  parents = (' ' * 4096) + ('{{<p}}{{/p}}' * 500) + "\n"
  assert_true MustacheTest.compiled_texts_size(parents) <= parents.bytesize
  t = Mustache::Template.new("  {{>a}}{{>a}}\n")
  assert_equal "  x\n  x\n", t.render({}, { a: Mustache::Template.new("x\n") })
end

# Tags on one closing line can sit at different depths of nested blocks,
# and each depth has removed a different part of the line's indent. The
# compiler once remembered only the last indent it had written, so a line
# that went back and forth between depths wrote the indent again at each
# turn: up to 64 copies of the line. Each part is now written once.
assert('a closing line at many block depths compiles to no more than its source') do
  opens = (0...8).map { |d| (' ' * d) + "{{$b#{d}}}\n" }.join
  closes = (0...8).to_a.reverse.map { |d| "{{>p}}{{/b#{d}}}" }.join
  src = opens + (' ' * 2000) + closes + "{{>p}}\n"
  assert_true MustacheTest.compiled_texts_size(src) <= src.bytesize
end

# A block finds the argument its caller gave it by name. The walk once
# compared the name with every argument of every caller in the chain, so
# a parent with N blocks called with N arguments cost N * N compares. The
# compiler now sorts the arguments of each call by name; the outermost
# caller still wins, and within one call the last argument of a name
# does, as before.
assert('a block finds its argument among many') do
  names = (0...300).map { |i| "b#{i}" }
  parent = Mustache::Template.new(names.map { |n| "{{$#{n}}}-{{/#{n}}}" }.join)
  child = Mustache::Template.new('{{<p}}' + names.reverse.map { |n| "{{$#{n}}}#{n[1..]};{{/#{n}}}" }.join + '{{/p}}')
  assert_equal (0...300).map { |i| "#{i};" }.join, child.render({}, { p: parent })
  twice = Mustache::Template.new('{{<p}}{{$b1}}first{{/b1}}{{$b1}}last{{/b1}}{{/p}}')
  assert_equal '-last' + '-' * 298, twice.render({}, { p: parent })
end

# A Set Delimiter tag may name any delimiter, and the compiler searches
# the rest of the source for it. A delimiter of 64 KiB made each search
# cost 64 KiB per byte of source: 2 s for 1 MiB. Delimiters are at most
# 16 bytes long.
assert('a delimiter is at most 16 bytes long') do
  sixteen = 'x' * 16
  assert_equal 'A', Mustache::Template.new("{{=#{sixteen} ]]=}}#{sixteen}a]]").render({ a: 'A' })
  e = assert_raise(Mustache::ParseError) { Mustache::Template.new("{{=#{'x' * 17} ]]=}}") }
  assert_equal 'a delimiter is longer than 16 bytes at byte 0', e.message
  e = assert_raise(Mustache::ParseError) { Mustache::Template.new("{{=[[ #{']' * 17}=}}") }
  assert_equal 'a delimiter is longer than 16 bytes at byte 0', e.message
end

# The compiler removes the indent of a block from every line inside it,
# and a block inside a block walks the same lines again. Nested without a
# bound that is quadratic: a 1 MiB source took 60 s to compile. Blocks and
# parents nest at most 64 deep when a template compiles, in every build,
# so the work stays linear in the source. A render stops them earlier
# where MAX_PARTIAL_DEPTH is below 64.
assert('blocks and parents nest at most 64 deep') do
  Mustache::Template.new(('{{$a}}' * 64) + 'x' + ('{{/a}}' * 64))
  Mustache::Template.new(('{{<a}}' * 64) + ('{{/a}}' * 64))
  e = assert_raise(Mustache::ParseError) { Mustache::Template.new(('{{$a}}' * 65) + ('{{/a}}' * 65)) }
  assert_equal 'blocks and parents nest deeper than 64 at byte 384', e.message
  e = assert_raise(Mustache::ParseError) { Mustache::Template.new(('{{<a}}' * 65) + ('{{/a}}' * 65)) }
  assert_equal 'blocks and parents nest deeper than 64 at byte 384', e.message
end

# The escaper writes whole 8-byte entity words, and its wide forms store
# 32-byte blocks, so it writes up to 32 bytes past the end of what it
# escapes. An Out made by out_over keeps those 32 bytes of the buffer out
# of its room: an escape either fits with them or says the Out is full.
# The buffers here are exactly as large as the size named, so the ASan
# build sees any write past them.
assert('an Out made over a buffer never writes past it') do
  ['a<b', ('a' * 31) + '<', '<' * 40, ('x' * 100) + '&'].each do |v|
    want = v.gsub('&', '&amp;').gsub('<', '&lt;')
    assert_equal want, MustacheTest.escaped_through_out_over(v, want.bytesize + 32)
    assert_nil MustacheTest.escaped_through_out_over(v, want.bytesize + 31)
    assert_nil MustacheTest.escaped_through_out_over(v, want.bytesize)
  end
end

# The context stack holds MAX_DEPTH frames, the data the render starts
# with being the first, and partials, parents and blocks nest at most
# MAX_PARTIAL_DEPTH deep. Past that the render stops with RenderError:
# nesting in the data or in the templates cannot grow the C stack without
# end. Both limits follow the mruby profile the build uses, the way
# mruby-cbor chooses its CBOR_MAX_DEPTH: 128 for MRB_HIGH_PROFILE, 64 for
# MRB_MAIN_PROFILE, 32 for MRB_BASELINE_PROFILE and 16 otherwise, and a
# build overrides them with -DMUSTACHE_MAX_DEPTH=n and
# -DMUSTACHE_MAX_PARTIAL_DEPTH=n.
assert('nesting up to the limits renders, and one past them is a RenderError') do
  sections = ->(n) { Mustache::Template.new(('{{^a}}' * n) + 'x' + ('{{/a}}' * n)) }
  assert_equal 'x', sections.(Mustache::MAX_DEPTH - 1).render({})
  assert_raise(Mustache::RenderError) { sections.(Mustache::MAX_DEPTH).render({}) }
  chain = ->(n) { (1..n).each_with_object({}) { |i, h| h[:"p#{i}"] = Mustache::Template.new(i == n ? 'y' : "{{>p#{i + 1}}}") } }
  top = Mustache::Template.new('{{>p1}}')
  assert_equal 'y', top.render({}, chain.(Mustache::MAX_PARTIAL_DEPTH))
  assert_raise(Mustache::RenderError) { top.render({}, chain.(Mustache::MAX_PARTIAL_DEPTH + 1)) }
end

assert('nesting deeper than the limits is a RenderError') do
  deep = Mustache::Template.new(('{{^a}}' * 5000) + ('{{/a}}' * 5000))
  assert_raise(Mustache::RenderError) { deep.render({}) }
  own = Mustache::Template.new('x{{>p}}')
  assert_raise(Mustache::RenderError) { own.render({}, { p: own }) }
  parent = Mustache::Template.new('x{{<p}}{{/p}}')
  assert_raise(Mustache::RenderError) { parent.render({}, { p: parent }) }
end

# A block that names itself inside its own argument finds that argument
# again each time it renders, so only the partial depth stops it.
assert('a block that fills itself is a RenderError') do
  filled = Mustache::Template.new('{{<p}}{{$b}}{{$b}}{{/b}}{{/b}}{{/p}}')
  assert_raise(Mustache::RenderError) { filled.render({}, { p: Mustache::Template.new('{{$b}}{{/b}}') }) }
end

# The old gem kept its compiled program in instance variables, and
# inspect showed all of it: the source of a template could end up in any
# error message that named the object.
assert('inspect shows no part of the template') do
  said = Mustache::Template.new('{{secret}}{{#s}}{{.}}{{/s}}').inspect
  assert_false said.include?('secret'), said
  assert_false said.include?('{{'), said
end

# The old gem built one op per tag inside a single C frame and ran out of
# GC arena from 40 tags on. Here compile makes a Symbol per key and no
# other object, and this checks that 2000 keys stay that way.
assert('a template with 2000 tags compiles and renders') do
  src = (0...2000).map { |i| "{{v#{i}}}" }.join
  data = {}
  2000.times { |i| data[:"v#{i}"] = 'x' }
  assert_equal 'x' * 2000, Mustache::Template.new(src).render(data)
end

# The answer is written into one buffer of the size the template was
# given, and never past it.
assert('an answer larger than the buffer is a RenderError') do
  t = Mustache::Template.new('{{v}}', 4)
  assert_equal 'abcd', t.render({ v: 'abcd' })
  assert_raise(Mustache::RenderError) { t.render({ v: 'abcde' }) }
  assert_raise(Mustache::RenderError) { t.render({ v: '"' }) }
  assert_equal '&lt;', t.render({ v: '<' })
  assert_equal 'ab', t.render({ v: 'ab' })
end

assert('partials: found, missing, not a Template') do
  t = Mustache::Template.new('[{{>p}}]')
  assert_equal '[x]', t.render({ v: 'x' }, { p: Mustache::Template.new('{{v}}') })
  assert_equal '[]', t.render({}, {})
  assert_equal '[]', t.render({}, { p: 'not a template' })
  assert_equal '[]', t.render({}, { p: Mustache::Template.allocate })
  assert_raise(ArgumentError) { t.render({}, [1]) }
end

# A partial named on a line of its own is indented as far as its tag. A
# partial inside it adds its own indent to that one (mustache spec,
# partials: Standalone Indentation, applied twice).
assert('a partial inside an indented partial adds its indent') do
  inner = Mustache::Template.new("1\n2\n")
  outer = Mustache::Template.new("a\n  {{>inner}}\nb\n")
  t = Mustache::Template.new("    {{>outer}}\n")
  assert_equal "    a\n      1\n      2\n    b\n", t.render({}, { outer: outer, inner: inner })
end

# The render asks mruby for nothing that runs Ruby. A Symbol key is
# hashed and compared by hash.c without a funcall, and mrb_hash_fetch
# never calls a default proc; so a Hash whose default proc raises, a Hash
# subclass that overrides [] and fetch, and a Hash large enough for hash.c
# to index it are read without calling anything.
class MustacheOverride < Hash
  def [](k); raise '[] called'; end
  def fetch(*a); raise 'fetch called'; end
end

assert('a render runs no Ruby') do
  t = Mustache::Template.new('{{a}}{{#l}}{{.}}{{/l}}')
  proc_hash = Hash.new { |_, _| raise 'default proc called' }
  proc_hash[:a] = 'A'
  proc_hash[:l] = %w[x y]
  assert_equal 'Axy', t.render(proc_hash)
  over = MustacheOverride.new
  over.store(:a, 'A')
  assert_equal 'A', t.render(over)
  big = {}
  600.times { |i| big["k#{i}"] = 'v' }
  big[:a] = 'A'
  assert_equal 'A', t.render(big)
end

# The mustache spec, https://github.com/mustache/spec at e8ec001,
# every case of every module but two. ~lambdas give the template code of
# the caller to run, and ~dynamic-names let the data pick the template:
# in both, something other than the templates decides what the output is.
SPEC = []

# ==========================================================================
# Comments
# ==========================================================================

SPEC << ["comments: Inline", {
  template: "12345{{! Comment Block! }}67890",
  data:     {},
  expected: "1234567890",
}]

SPEC << ["comments: Multiline", {
  template: "12345{{!\n  This is a\n  multi-line comment...\n}}67890\n",
  data:     {},
  expected: "1234567890\n",
}]

SPEC << ["comments: Standalone", {
  template: "Begin.\n{{! Comment Block! }}\nEnd.\n",
  data:     {},
  expected: "Begin.\nEnd.\n",
}]

SPEC << ["comments: Indented Standalone", {
  template: "Begin.\n  {{! Indented Comment Block! }}\nEnd.\n",
  data:     {},
  expected: "Begin.\nEnd.\n",
}]

SPEC << ["comments: Standalone Line Endings", {
  template: "|\r\n{{! Standalone Comment }}\r\n|",
  data:     {},
  expected: "|\r\n|",
}]

SPEC << ["comments: Standalone Without Previous Line", {
  template: "  {{! I'm Still Standalone }}\n!",
  data:     {},
  expected: "!",
}]

SPEC << ["comments: Standalone Without Newline", {
  template: "!\n  {{! I'm Still Standalone }}",
  data:     {},
  expected: "!\n",
}]

SPEC << ["comments: Multiline Standalone", {
  template: "Begin.\n{{!\nSomething's going on here...\n}}\nEnd.\n",
  data:     {},
  expected: "Begin.\nEnd.\n",
}]

SPEC << ["comments: Indented Multiline Standalone", {
  template: "Begin.\n  {{!\n    Something's going on here...\n  }}\nEnd.\n",
  data:     {},
  expected: "Begin.\nEnd.\n",
}]

SPEC << ["comments: Indented Inline", {
  template: "  12 {{! 34 }}\n",
  data:     {},
  expected: "  12 \n",
}]

SPEC << ["comments: Surrounding Whitespace", {
  template: "12345 {{! Comment Block! }} 67890",
  data:     {},
  expected: "12345  67890",
}]

SPEC << ["comments: Variable Name Collision", {
  template: "comments never show: >{{! comment }}<",
  data:     { "! comment" => 1, "! comment " => 2, "!comment" => 3, "comment" => 4 },
  expected: "comments never show: ><",
}]

# ==========================================================================
# Interpolation
# ==========================================================================

SPEC << ["interpolation: No Interpolation", {
  template: "Hello from {Mustache}!\n",
  data:     {},
  expected: "Hello from {Mustache}!\n",
}]

SPEC << ["interpolation: Basic Interpolation", {
  template: "Hello, {{subject}}!\n",
  data:     { "subject" => "world" },
  expected: "Hello, world!\n",
}]

SPEC << ["interpolation: No Re-interpolation", {
  template: "{{template}}: {{planet}}",
  data:     { "template" => "{{planet}}", "planet" => "Earth" },
  expected: "{{planet}}: Earth",
}]

SPEC << ["interpolation: HTML Escaping", {
  template: "These characters should be HTML escaped: {{forbidden}}\n",
  data:     { "forbidden" => "& \" < >" },
  expected: "These characters should be HTML escaped: &amp; &quot; &lt; &gt;\n",
}]

SPEC << ["interpolation: Triple Mustache", {
  template: "These characters should not be HTML escaped: {{{forbidden}}}\n",
  data:     { "forbidden" => "& \" < >" },
  expected: "These characters should not be HTML escaped: & \" < >\n",
}]

SPEC << ["interpolation: Ampersand", {
  template: "These characters should not be HTML escaped: {{&forbidden}}\n",
  data:     { "forbidden" => "& \" < >" },
  expected: "These characters should not be HTML escaped: & \" < >\n",
}]

SPEC << ["interpolation: Basic Integer Interpolation", {
  template: "\"{{mph}} miles an hour!\"",
  data:     { "mph" => 85 },
  expected: "\"85 miles an hour!\"",
}]

SPEC << ["interpolation: Triple Mustache Integer Interpolation", {
  template: "\"{{{mph}}} miles an hour!\"",
  data:     { "mph" => 85 },
  expected: "\"85 miles an hour!\"",
}]

SPEC << ["interpolation: Ampersand Integer Interpolation", {
  template: "\"{{&mph}} miles an hour!\"",
  data:     { "mph" => 85 },
  expected: "\"85 miles an hour!\"",
}]

SPEC << ["interpolation: Basic Decimal Interpolation", {
  template: "\"{{power}} jiggawatts!\"",
  data:     { "power" => 1.21 },
  expected: "\"1.21 jiggawatts!\"",
}]

SPEC << ["interpolation: Triple Mustache Decimal Interpolation", {
  template: "\"{{{power}}} jiggawatts!\"",
  data:     { "power" => 1.21 },
  expected: "\"1.21 jiggawatts!\"",
}]

SPEC << ["interpolation: Ampersand Decimal Interpolation", {
  template: "\"{{&power}} jiggawatts!\"",
  data:     { "power" => 1.21 },
  expected: "\"1.21 jiggawatts!\"",
}]

SPEC << ["interpolation: Basic Null Interpolation", {
  template: "I ({{cannot}}) be seen!",
  data:     { "cannot" => nil },
  expected: "I () be seen!",
}]

SPEC << ["interpolation: Triple Mustache Null Interpolation", {
  template: "I ({{{cannot}}}) be seen!",
  data:     { "cannot" => nil },
  expected: "I () be seen!",
}]

SPEC << ["interpolation: Ampersand Null Interpolation", {
  template: "I ({{&cannot}}) be seen!",
  data:     { "cannot" => nil },
  expected: "I () be seen!",
}]

SPEC << ["interpolation: Basic Context Miss Interpolation", {
  template: "I ({{cannot}}) be seen!",
  data:     {},
  expected: "I () be seen!",
}]

SPEC << ["interpolation: Triple Mustache Context Miss Interpolation", {
  template: "I ({{{cannot}}}) be seen!",
  data:     {},
  expected: "I () be seen!",
}]

SPEC << ["interpolation: Ampersand Context Miss Interpolation", {
  template: "I ({{&cannot}}) be seen!",
  data:     {},
  expected: "I () be seen!",
}]

SPEC << ["interpolation: Dotted Names - Basic Interpolation", {
  template: "\"{{person.name}}\" == \"{{\#person}}{{name}}{{/person}}\"",
  data:     { "person" => { "name" => "Joe" } },
  expected: "\"Joe\" == \"Joe\"",
}]

SPEC << ["interpolation: Dotted Names - Triple Mustache Interpolation", {
  template: "\"{{{person.name}}}\" == \"{{\#person}}{{{name}}}{{/person}}\"",
  data:     { "person" => { "name" => "Joe" } },
  expected: "\"Joe\" == \"Joe\"",
}]

SPEC << ["interpolation: Dotted Names - Ampersand Interpolation", {
  template: "\"{{&person.name}}\" == \"{{\#person}}{{&name}}{{/person}}\"",
  data:     { "person" => { "name" => "Joe" } },
  expected: "\"Joe\" == \"Joe\"",
}]

SPEC << ["interpolation: Dotted Names - Arbitrary Depth", {
  template: "\"{{a.b.c.d.e.name}}\" == \"Phil\"",
  data:     { "a" => { "b" => { "c" => { "d" => { "e" => { "name" => "Phil" } } } } } },
  expected: "\"Phil\" == \"Phil\"",
}]

SPEC << ["interpolation: Dotted Names - Broken Chains", {
  template: "\"{{a.b.c}}\" == \"\"",
  data:     { "a" => {} },
  expected: "\"\" == \"\"",
}]

SPEC << ["interpolation: Dotted Names - Broken Chain Resolution", {
  template: "\"{{a.b.c.name}}\" == \"\"",
  data:     { "a" => { "b" => {} }, "c" => { "name" => "Jim" } },
  expected: "\"\" == \"\"",
}]

SPEC << ["interpolation: Dotted Names - Initial Resolution", {
  template: "\"{{\#a}}{{b.c.d.e.name}}{{/a}}\" == \"Phil\"",
  data:     { "a" => { "b" => { "c" => { "d" => { "e" => { "name" => "Phil" } } } } }, "b" => { "c" => { "d" => { "e" => { "name" => "Wrong" } } } } },
  expected: "\"Phil\" == \"Phil\"",
}]

SPEC << ["interpolation: Dotted Names - Context Precedence", {
  template: "{{\#a}}{{b.c}}{{/a}}",
  data:     { "a" => { "b" => {} }, "b" => { "c" => "ERROR" } },
  expected: "",
}]

SPEC << ["interpolation: Dotted Names are never single keys", {
  template: "{{a.b}}",
  data:     { "a.b" => "c" },
  expected: "",
}]

SPEC << ["interpolation: Dotted Names - No Masking", {
  template: "{{a.b}}",
  data:     { "a.b" => "c", "a" => { "b" => "d" } },
  expected: "d",
}]

SPEC << ["interpolation: Implicit Iterators - Basic Interpolation", {
  template: "Hello, {{.}}!\n",
  data:     "world",
  expected: "Hello, world!\n",
}]

SPEC << ["interpolation: Implicit Iterators - HTML Escaping", {
  template: "These characters should be HTML escaped: {{.}}\n",
  data:     "& \" < >",
  expected: "These characters should be HTML escaped: &amp; &quot; &lt; &gt;\n",
}]

SPEC << ["interpolation: Implicit Iterators - Triple Mustache", {
  template: "These characters should not be HTML escaped: {{{.}}}\n",
  data:     "& \" < >",
  expected: "These characters should not be HTML escaped: & \" < >\n",
}]

SPEC << ["interpolation: Implicit Iterators - Ampersand", {
  template: "These characters should not be HTML escaped: {{&.}}\n",
  data:     "& \" < >",
  expected: "These characters should not be HTML escaped: & \" < >\n",
}]

SPEC << ["interpolation: Implicit Iterators - Basic Integer Interpolation", {
  template: "\"{{.}} miles an hour!\"",
  data:     85,
  expected: "\"85 miles an hour!\"",
}]

SPEC << ["interpolation: Interpolation - Surrounding Whitespace", {
  template: "| {{string}} |",
  data:     { "string" => "---" },
  expected: "| --- |",
}]

SPEC << ["interpolation: Triple Mustache - Surrounding Whitespace", {
  template: "| {{{string}}} |",
  data:     { "string" => "---" },
  expected: "| --- |",
}]

SPEC << ["interpolation: Ampersand - Surrounding Whitespace", {
  template: "| {{&string}} |",
  data:     { "string" => "---" },
  expected: "| --- |",
}]

SPEC << ["interpolation: Interpolation - Standalone", {
  template: "  {{string}}\n",
  data:     { "string" => "---" },
  expected: "  ---\n",
}]

SPEC << ["interpolation: Triple Mustache - Standalone", {
  template: "  {{{string}}}\n",
  data:     { "string" => "---" },
  expected: "  ---\n",
}]

SPEC << ["interpolation: Ampersand - Standalone", {
  template: "  {{&string}}\n",
  data:     { "string" => "---" },
  expected: "  ---\n",
}]

SPEC << ["interpolation: Interpolation With Padding", {
  template: "|{{ string }}|",
  data:     { "string" => "---" },
  expected: "|---|",
}]

SPEC << ["interpolation: Triple Mustache With Padding", {
  template: "|{{{ string }}}|",
  data:     { "string" => "---" },
  expected: "|---|",
}]

SPEC << ["interpolation: Ampersand With Padding", {
  template: "|{{& string }}|",
  data:     { "string" => "---" },
  expected: "|---|",
}]

# ==========================================================================
# Sections
# ==========================================================================

SPEC << ["sections: Truthy", {
  template: "\"{{\#boolean}}This should be rendered.{{/boolean}}\"",
  data:     { "boolean" => true },
  expected: "\"This should be rendered.\"",
}]

SPEC << ["sections: Falsey", {
  template: "\"{{\#boolean}}This should not be rendered.{{/boolean}}\"",
  data:     { "boolean" => false },
  expected: "\"\"",
}]

SPEC << ["sections: Null is falsey", {
  template: "\"{{\#null}}This should not be rendered.{{/null}}\"",
  data:     { "null" => nil },
  expected: "\"\"",
}]

SPEC << ["sections: Context", {
  template: "\"{{\#context}}Hi {{name}}.{{/context}}\"",
  data:     { "context" => { "name" => "Joe" } },
  expected: "\"Hi Joe.\"",
}]

SPEC << ["sections: Parent contexts", {
  template: "\"{{\#sec}}{{a}}, {{b}}, {{c.d}}{{/sec}}\"",
  data:     { "a" => "foo", "b" => "wrong", "sec" => { "b" => "bar" }, "c" => { "d" => "baz" } },
  expected: "\"foo, bar, baz\"",
}]

SPEC << ["sections: Variable test", {
  template: "\"{{\#foo}}{{.}} is {{foo}}{{/foo}}\"",
  data:     { "foo" => "bar" },
  expected: "\"bar is bar\"",
}]

SPEC << ["sections: List Contexts", {
  template: "{{\#tops}}{{\#middles}}{{tname.lower}}{{mname}}.{{\#bottoms}}{{tname.upper}}{{mname}}{{bname}}.{{/bottoms}}{{/middles}}{{/tops}}",
  data:     { "tops" => [{ "tname" => { "upper" => "A", "lower" => "a" }, "middles" => [{ "mname" => "1", "bottoms" => [{ "bname" => "x" }, { "bname" => "y" }] }] }] },
  expected: "a1.A1x.A1y.",
}]

SPEC << ["sections: Deeply Nested Contexts", {
  template: "{{\#a}}\n{{one}}\n{{\#b}}\n{{one}}{{two}}{{one}}\n{{\#c}}\n{{one}}{{two}}{{three}}{{two}}{{one}}\n{{\#d}}\n{{one}}{{two}}{{three}}{{four}}{{three}}{{two}}{{one}}\n{{\#five}}\n{{one}}{{two}}{{three}}{{four}}{{five}}{{four}}{{three}}{{two}}{{one}}\n{{one}}{{two}}{{three}}{{four}}{{.}}6{{.}}{{four}}{{three}}{{two}}{{one}}\n{{one}}{{two}}{{three}}{{four}}{{five}}{{four}}{{three}}{{two}}{{one}}\n{{/five}}\n{{one}}{{two}}{{three}}{{four}}{{three}}{{two}}{{one}}\n{{/d}}\n{{one}}{{two}}{{three}}{{two}}{{one}}\n{{/c}}\n{{one}}{{two}}{{one}}\n{{/b}}\n{{one}}\n{{/a}}\n",
  data:     { "a" => { "one" => 1 }, "b" => { "two" => 2 }, "c" => { "three" => 3, "d" => { "four" => 4, "five" => 5 } } },
  expected: "1\n121\n12321\n1234321\n123454321\n12345654321\n123454321\n1234321\n12321\n121\n1\n",
}]

SPEC << ["sections: List", {
  template: "\"{{\#list}}{{item}}{{/list}}\"",
  data:     { "list" => [{ "item" => 1 }, { "item" => 2 }, { "item" => 3 }] },
  expected: "\"123\"",
}]

SPEC << ["sections: Empty List", {
  template: "\"{{\#list}}Yay lists!{{/list}}\"",
  data:     { "list" => [] },
  expected: "\"\"",
}]

SPEC << ["sections: Doubled", {
  template: "{{\#bool}}\n* first\n{{/bool}}\n* {{two}}\n{{\#bool}}\n* third\n{{/bool}}\n",
  data:     { "bool" => true, "two" => "second" },
  expected: "* first\n* second\n* third\n",
}]

SPEC << ["sections: Nested (Truthy)", {
  template: "| A {{\#bool}}B {{\#bool}}C{{/bool}} D{{/bool}} E |",
  data:     { "bool" => true },
  expected: "| A B C D E |",
}]

SPEC << ["sections: Nested (Falsey)", {
  template: "| A {{\#bool}}B {{\#bool}}C{{/bool}} D{{/bool}} E |",
  data:     { "bool" => false },
  expected: "| A  E |",
}]

SPEC << ["sections: Context Misses", {
  template: "[{{\#missing}}Found key 'missing'!{{/missing}}]",
  data:     {},
  expected: "[]",
}]

SPEC << ["sections: Implicit Iterator - String", {
  template: "\"{{\#list}}({{.}}){{/list}}\"",
  data:     { "list" => ["a", "b", "c", "d", "e"] },
  expected: "\"(a)(b)(c)(d)(e)\"",
}]

SPEC << ["sections: Implicit Iterator - Integer", {
  template: "\"{{\#list}}({{.}}){{/list}}\"",
  data:     { "list" => [1, 2, 3, 4, 5] },
  expected: "\"(1)(2)(3)(4)(5)\"",
}]

SPEC << ["sections: Implicit Iterator - Decimal", {
  template: "\"{{\#list}}({{.}}){{/list}}\"",
  data:     { "list" => [1.1, 2.2, 3.3, 4.4, 5.5] },
  expected: "\"(1.1)(2.2)(3.3)(4.4)(5.5)\"",
}]

SPEC << ["sections: Implicit Iterator - Array", {
  template: "\"{{\#list}}({{\#.}}{{.}}{{/.}}){{/list}}\"",
  data:     { "list" => [[1, 2, 3], ["a", "b", "c"]] },
  expected: "\"(123)(abc)\"",
}]

SPEC << ["sections: Implicit Iterator - HTML Escaping", {
  template: "\"{{\#list}}({{.}}){{/list}}\"",
  data:     { "list" => ["&", "\"", "<", ">"] },
  expected: "\"(&amp;)(&quot;)(&lt;)(&gt;)\"",
}]

SPEC << ["sections: Implicit Iterator - Triple mustache", {
  template: "\"{{\#list}}({{{.}}}){{/list}}\"",
  data:     { "list" => ["&", "\"", "<", ">"] },
  expected: "\"(&)(\")(<)(>)\"",
}]

SPEC << ["sections: Implicit Iterator - Ampersand", {
  template: "\"{{\#list}}({{&.}}){{/list}}\"",
  data:     { "list" => ["&", "\"", "<", ">"] },
  expected: "\"(&)(\")(<)(>)\"",
}]

SPEC << ["sections: Implicit Iterator - Root-level", {
  template: "\"{{\#.}}({{value}}){{/.}}\"",
  data:     [{ "value" => "a" }, { "value" => "b" }],
  expected: "\"(a)(b)\"",
}]

SPEC << ["sections: Dotted Names - Truthy", {
  template: "\"{{\#a.b.c}}Here{{/a.b.c}}\" == \"Here\"",
  data:     { "a" => { "b" => { "c" => true } } },
  expected: "\"Here\" == \"Here\"",
}]

SPEC << ["sections: Dotted Names - Falsey", {
  template: "\"{{\#a.b.c}}Here{{/a.b.c}}\" == \"\"",
  data:     { "a" => { "b" => { "c" => false } } },
  expected: "\"\" == \"\"",
}]

SPEC << ["sections: Dotted Names - Broken Chains", {
  template: "\"{{\#a.b.c}}Here{{/a.b.c}}\" == \"\"",
  data:     { "a" => {} },
  expected: "\"\" == \"\"",
}]

SPEC << ["sections: Surrounding Whitespace", {
  template: " | {{\#boolean}}\t|\t{{/boolean}} | \n",
  data:     { "boolean" => true },
  expected: " | \t|\t | \n",
}]

SPEC << ["sections: Internal Whitespace", {
  template: " | {{\#boolean}} {{! Important Whitespace }}\n {{/boolean}} | \n",
  data:     { "boolean" => true },
  expected: " |  \n  | \n",
}]

SPEC << ["sections: Indented Inline Sections", {
  template: " {{\#boolean}}YES{{/boolean}}\n {{\#boolean}}GOOD{{/boolean}}\n",
  data:     { "boolean" => true },
  expected: " YES\n GOOD\n",
}]

SPEC << ["sections: Standalone Lines", {
  template: "| This Is\n{{\#boolean}}\n|\n{{/boolean}}\n| A Line\n",
  data:     { "boolean" => true },
  expected: "| This Is\n|\n| A Line\n",
}]

SPEC << ["sections: Indented Standalone Lines", {
  template: "| This Is\n  {{\#boolean}}\n|\n  {{/boolean}}\n| A Line\n",
  data:     { "boolean" => true },
  expected: "| This Is\n|\n| A Line\n",
}]

SPEC << ["sections: Standalone Line Endings", {
  template: "|\r\n{{\#boolean}}\r\n{{/boolean}}\r\n|",
  data:     { "boolean" => true },
  expected: "|\r\n|",
}]

SPEC << ["sections: Standalone Without Previous Line", {
  template: "  {{\#boolean}}\n\#{{/boolean}}\n/",
  data:     { "boolean" => true },
  expected: "\#\n/",
}]

SPEC << ["sections: Standalone Without Newline", {
  template: "\#{{\#boolean}}\n/\n  {{/boolean}}",
  data:     { "boolean" => true },
  expected: "\#\n/\n",
}]

SPEC << ["sections: Padding", {
  template: "|{{\# boolean }}={{/ boolean }}|",
  data:     { "boolean" => true },
  expected: "|=|",
}]

# ==========================================================================
# Inverted
# ==========================================================================

SPEC << ["inverted: Falsey", {
  template: "\"{{^boolean}}This should be rendered.{{/boolean}}\"",
  data:     { "boolean" => false },
  expected: "\"This should be rendered.\"",
}]

SPEC << ["inverted: Truthy", {
  template: "\"{{^boolean}}This should not be rendered.{{/boolean}}\"",
  data:     { "boolean" => true },
  expected: "\"\"",
}]

SPEC << ["inverted: Null is falsey", {
  template: "\"{{^null}}This should be rendered.{{/null}}\"",
  data:     { "null" => nil },
  expected: "\"This should be rendered.\"",
}]

SPEC << ["inverted: Context", {
  template: "\"{{^context}}Hi {{name}}.{{/context}}\"",
  data:     { "context" => { "name" => "Joe" } },
  expected: "\"\"",
}]

SPEC << ["inverted: List", {
  template: "\"{{^list}}{{n}}{{/list}}\"",
  data:     { "list" => [{ "n" => 1 }, { "n" => 2 }, { "n" => 3 }] },
  expected: "\"\"",
}]

SPEC << ["inverted: Empty List", {
  template: "\"{{^list}}Yay lists!{{/list}}\"",
  data:     { "list" => [] },
  expected: "\"Yay lists!\"",
}]

SPEC << ["inverted: Doubled", {
  template: "{{^bool}}\n* first\n{{/bool}}\n* {{two}}\n{{^bool}}\n* third\n{{/bool}}\n",
  data:     { "bool" => false, "two" => "second" },
  expected: "* first\n* second\n* third\n",
}]

SPEC << ["inverted: Nested (Falsey)", {
  template: "| A {{^bool}}B {{^bool}}C{{/bool}} D{{/bool}} E |",
  data:     { "bool" => false },
  expected: "| A B C D E |",
}]

SPEC << ["inverted: Nested (Truthy)", {
  template: "| A {{^bool}}B {{^bool}}C{{/bool}} D{{/bool}} E |",
  data:     { "bool" => true },
  expected: "| A  E |",
}]

SPEC << ["inverted: Context Misses", {
  template: "[{{^missing}}Cannot find key 'missing'!{{/missing}}]",
  data:     {},
  expected: "[Cannot find key 'missing'!]",
}]

SPEC << ["inverted: Dotted Names - Truthy", {
  template: "\"{{^a.b.c}}Not Here{{/a.b.c}}\" == \"\"",
  data:     { "a" => { "b" => { "c" => true } } },
  expected: "\"\" == \"\"",
}]

SPEC << ["inverted: Dotted Names - Falsey", {
  template: "\"{{^a.b.c}}Not Here{{/a.b.c}}\" == \"Not Here\"",
  data:     { "a" => { "b" => { "c" => false } } },
  expected: "\"Not Here\" == \"Not Here\"",
}]

SPEC << ["inverted: Dotted Names - Broken Chains", {
  template: "\"{{^a.b.c}}Not Here{{/a.b.c}}\" == \"Not Here\"",
  data:     { "a" => {} },
  expected: "\"Not Here\" == \"Not Here\"",
}]

SPEC << ["inverted: Surrounding Whitespace", {
  template: " | {{^boolean}}\t|\t{{/boolean}} | \n",
  data:     { "boolean" => false },
  expected: " | \t|\t | \n",
}]

SPEC << ["inverted: Internal Whitespace", {
  template: " | {{^boolean}} {{! Important Whitespace }}\n {{/boolean}} | \n",
  data:     { "boolean" => false },
  expected: " |  \n  | \n",
}]

SPEC << ["inverted: Indented Inline Sections", {
  template: " {{^boolean}}NO{{/boolean}}\n {{^boolean}}WAY{{/boolean}}\n",
  data:     { "boolean" => false },
  expected: " NO\n WAY\n",
}]

SPEC << ["inverted: Standalone Lines", {
  template: "| This Is\n{{^boolean}}\n|\n{{/boolean}}\n| A Line\n",
  data:     { "boolean" => false },
  expected: "| This Is\n|\n| A Line\n",
}]

SPEC << ["inverted: Standalone Indented Lines", {
  template: "| This Is\n  {{^boolean}}\n|\n  {{/boolean}}\n| A Line\n",
  data:     { "boolean" => false },
  expected: "| This Is\n|\n| A Line\n",
}]

SPEC << ["inverted: Standalone Line Endings", {
  template: "|\r\n{{^boolean}}\r\n{{/boolean}}\r\n|",
  data:     { "boolean" => false },
  expected: "|\r\n|",
}]

SPEC << ["inverted: Standalone Without Previous Line", {
  template: "  {{^boolean}}\n^{{/boolean}}\n/",
  data:     { "boolean" => false },
  expected: "^\n/",
}]

SPEC << ["inverted: Standalone Without Newline", {
  template: "^{{^boolean}}\n/\n  {{/boolean}}",
  data:     { "boolean" => false },
  expected: "^\n/\n",
}]

SPEC << ["inverted: Padding", {
  template: "|{{^ boolean }}={{/ boolean }}|",
  data:     { "boolean" => false },
  expected: "|=|",
}]

# ==========================================================================
# Partials
# ==========================================================================

SPEC << ["partials: Basic Behavior", {
  template: "\"{{>text}}\"",
  data:     {},
  partials: { "text" => "from partial" },
  expected: "\"from partial\"",
}]

SPEC << ["partials: Failed Lookup", {
  template: "\"{{>text}}\"",
  data:     {},
  partials: {},
  expected: "\"\"",
}]

SPEC << ["partials: Context", {
  template: "\"{{>partial}}\"",
  data:     { "text" => "content" },
  partials: { "partial" => "*{{text}}*" },
  expected: "\"*content*\"",
}]

SPEC << ["partials: Recursion", {
  template: "{{>node}}",
  data:     { "content" => "X", "nodes" => [{ "content" => "Y", "nodes" => [] }] },
  partials: { "node" => "{{content}}<{{\#nodes}}{{>node}}{{/nodes}}>" },
  expected: "X<Y<>>",
}]

SPEC << ["partials: Nested", {
  template: "{{>outer}}",
  data:     { "a" => "hello", "b" => "world" },
  partials: { "outer" => "*{{a}} {{>inner}}*", "inner" => "{{b}}!" },
  expected: "*hello world!*",
}]

SPEC << ["partials: Surrounding Whitespace", {
  template: "| {{>partial}} |",
  data:     {},
  partials: { "partial" => "\t|\t" },
  expected: "| \t|\t |",
}]

SPEC << ["partials: Inline Indentation", {
  template: "  {{data}}  {{> partial}}\n",
  data:     { "data" => "|" },
  partials: { "partial" => ">\n>" },
  expected: "  |  >\n>\n",
}]

SPEC << ["partials: Standalone Line Endings", {
  template: "|\r\n{{>partial}}\r\n|",
  data:     {},
  partials: { "partial" => ">" },
  expected: "|\r\n>|",
}]

SPEC << ["partials: Standalone Without Previous Line", {
  template: "  {{>partial}}\n>",
  data:     {},
  partials: { "partial" => ">\n>" },
  expected: "  >\n  >>",
}]

SPEC << ["partials: Standalone Without Newline", {
  template: ">\n  {{>partial}}",
  data:     {},
  partials: { "partial" => ">\n>" },
  expected: ">\n  >\n  >",
}]

SPEC << ["partials: Standalone Indentation", {
  template: "\\\n {{>partial}}\n/\n",
  data:     { "content" => "<\n->" },
  partials: { "partial" => "|\n{{{content}}}\n|\n" },
  expected: "\\\n |\n <\n->\n |\n/\n",
}]

SPEC << ["partials: Padding Whitespace", {
  template: "|{{> partial }}|",
  data:     { "boolean" => true },
  partials: { "partial" => "[]" },
  expected: "|[]|",
}]

# ==========================================================================
# Delimiters
# ==========================================================================

SPEC << ["delimiters: Pair Behavior", {
  template: "{{=<% %>=}}(<%text%>)",
  data:     { "text" => "Hey!" },
  expected: "(Hey!)",
}]

SPEC << ["delimiters: Special Characters", {
  template: "({{=[ ]=}}[text])",
  data:     { "text" => "It worked!" },
  expected: "(It worked!)",
}]

SPEC << ["delimiters: Sections", {
  template: "[\n{{\#section}}\n  {{data}}\n  |data|\n{{/section}}\n\n{{= | | =}}\n|\#section|\n  {{data}}\n  |data|\n|/section|\n]\n",
  data:     { "section" => true, "data" => "I got interpolated." },
  expected: "[\n  I got interpolated.\n  |data|\n\n  {{data}}\n  I got interpolated.\n]\n",
}]

SPEC << ["delimiters: Inverted Sections", {
  template: "[\n{{^section}}\n  {{data}}\n  |data|\n{{/section}}\n\n{{= | | =}}\n|^section|\n  {{data}}\n  |data|\n|/section|\n]\n",
  data:     { "section" => false, "data" => "I got interpolated." },
  expected: "[\n  I got interpolated.\n  |data|\n\n  {{data}}\n  I got interpolated.\n]\n",
}]

SPEC << ["delimiters: Partial Inheritence", {
  template: "[ {{>include}} ]\n{{= | | =}}\n[ |>include| ]\n",
  data:     { "value" => "yes" },
  partials: { "include" => ".{{value}}." },
  expected: "[ .yes. ]\n[ .yes. ]\n",
}]

SPEC << ["delimiters: Post-Partial Behavior", {
  template: "[ {{>include}} ]\n[ .{{value}}.  .|value|. ]\n",
  data:     { "value" => "yes" },
  partials: { "include" => ".{{value}}. {{= | | =}} .|value|." },
  expected: "[ .yes.  .yes. ]\n[ .yes.  .|value|. ]\n",
}]

SPEC << ["delimiters: Surrounding Whitespace", {
  template: "| {{=@ @=}} |",
  data:     {},
  expected: "|  |",
}]

SPEC << ["delimiters: Outlying Whitespace (Inline)", {
  template: " | {{=@ @=}}\n",
  data:     {},
  expected: " | \n",
}]

SPEC << ["delimiters: Standalone Tag", {
  template: "Begin.\n{{=@ @=}}\nEnd.\n",
  data:     {},
  expected: "Begin.\nEnd.\n",
}]

SPEC << ["delimiters: Indented Standalone Tag", {
  template: "Begin.\n  {{=@ @=}}\nEnd.\n",
  data:     {},
  expected: "Begin.\nEnd.\n",
}]

SPEC << ["delimiters: Standalone Line Endings", {
  template: "|\r\n{{= @ @ =}}\r\n|",
  data:     {},
  expected: "|\r\n|",
}]

SPEC << ["delimiters: Standalone Without Previous Line", {
  template: "  {{=@ @=}}\n=",
  data:     {},
  expected: "=",
}]

SPEC << ["delimiters: Standalone Without Newline", {
  template: "=\n  {{=@ @=}}",
  data:     {},
  expected: "=\n",
}]

SPEC << ["delimiters: Pair with Padding", {
  template: "|{{= @   @ =}}|",
  data:     {},
  expected: "||",
}]

# ==========================================================================
# Inheritance
# ==========================================================================

SPEC << ["inheritance: Default", {
  template: "{{$title}}Default title{{/title}}\n",
  data:     {},
  expected: "Default title\n",
}]

SPEC << ["inheritance: Variable", {
  template: "{{$foo}}default {{bar}} content{{/foo}}\n",
  data:     { "bar" => "baz" },
  expected: "default baz content\n",
}]

SPEC << ["inheritance: Triple Mustache", {
  template: "{{$foo}}default {{{bar}}} content{{/foo}}\n",
  data:     { "bar" => "<baz>" },
  expected: "default <baz> content\n",
}]

SPEC << ["inheritance: Sections", {
  template: "{{$foo}}default {{\#bar}}{{baz}}{{/bar}} content{{/foo}}\n",
  data:     { "bar" => { "baz" => "qux" } },
  expected: "default qux content\n",
}]

SPEC << ["inheritance: Negative Sections", {
  template: "{{$foo}}default {{^bar}}{{baz}}{{/bar}} content{{/foo}}\n",
  data:     { "baz" => "three" },
  expected: "default three content\n",
}]

SPEC << ["inheritance: Mustache Injection", {
  template: "{{$foo}}default {{\#bar}}{{baz}}{{/bar}} content{{/foo}}\n",
  data:     { "bar" => { "baz" => "{{qux}}" } },
  expected: "default {{qux}} content\n",
}]

SPEC << ["inheritance: Inherit", {
  template: "{{<include}}{{/include}}\n",
  data:     {},
  partials: { "include" => "{{$foo}}default content{{/foo}}" },
  expected: "default content",
}]

SPEC << ["inheritance: Overridden content", {
  template: "{{<super}}{{$title}}sub template title{{/title}}{{/super}}",
  data:     {},
  partials: { "super" => "...{{$title}}Default title{{/title}}..." },
  expected: "...sub template title...",
}]

SPEC << ["inheritance: Data does not override block", {
  template: "{{<include}}{{$var}}var in template{{/var}}{{/include}}",
  data:     { "var" => "var in data" },
  partials: { "include" => "{{$var}}var in include{{/var}}" },
  expected: "var in template",
}]

SPEC << ["inheritance: Data does not override block default", {
  template: "{{<include}}{{/include}}",
  data:     { "var" => "var in data" },
  partials: { "include" => "{{$var}}var in include{{/var}}" },
  expected: "var in include",
}]

SPEC << ["inheritance: Overridden parent", {
  template: "test {{<parent}}{{$stuff}}override{{/stuff}}{{/parent}}",
  data:     {},
  partials: { "parent" => "{{$stuff}}...{{/stuff}}" },
  expected: "test override",
}]

SPEC << ["inheritance: Two overridden parents", {
  template: "test {{<parent}}{{$stuff}}override1{{/stuff}}{{/parent}} {{<parent}}{{$stuff}}override2{{/stuff}}{{/parent}}\n",
  data:     {},
  partials: { "parent" => "|{{$stuff}}...{{/stuff}}{{$default}} default{{/default}}|" },
  expected: "test |override1 default| |override2 default|\n",
}]

SPEC << ["inheritance: Override parent with newlines", {
  template: "{{<parent}}{{$ballmer}}\npeaked\n\n:(\n{{/ballmer}}{{/parent}}",
  data:     {},
  partials: { "parent" => "{{$ballmer}}peaking{{/ballmer}}" },
  expected: "peaked\n\n:(\n",
}]

SPEC << ["inheritance: Inherit indentation", {
  template: "{{<parent}}{{$nineties}}hammer time{{/nineties}}{{/parent}}",
  data:     {},
  partials: { "parent" => "stop:\n  {{$nineties}}collaborate and listen{{/nineties}}\n" },
  expected: "stop:\n  hammer time\n",
}]

SPEC << ["inheritance: Only one override", {
  template: "{{<parent}}{{$stuff2}}override two{{/stuff2}}{{/parent}}",
  data:     {},
  partials: { "parent" => "{{$stuff}}new default one{{/stuff}}, {{$stuff2}}new default two{{/stuff2}}" },
  expected: "new default one, override two",
}]

SPEC << ["inheritance: Parent template", {
  template: "{{>parent}}|{{<parent}}{{/parent}}",
  data:     {},
  partials: { "parent" => "{{$foo}}default content{{/foo}}" },
  expected: "default content|default content",
}]

SPEC << ["inheritance: Recursion", {
  template: "{{<parent}}{{$foo}}override{{/foo}}{{/parent}}",
  data:     {},
  partials: { "parent" => "{{$foo}}default content{{/foo}} {{$bar}}{{<parent2}}{{/parent2}}{{/bar}}", "parent2" => "{{$foo}}parent2 default content{{/foo}} {{<parent}}{{$bar}}don't recurse{{/bar}}{{/parent}}" },
  expected: "override override override don't recurse",
}]

SPEC << ["inheritance: Multi-level inheritance", {
  template: "{{<parent}}{{$a}}c{{/a}}{{/parent}}",
  data:     {},
  partials: { "parent" => "{{<older}}{{$a}}p{{/a}}{{/older}}", "older" => "{{<grandParent}}{{$a}}o{{/a}}{{/grandParent}}", "grandParent" => "{{$a}}g{{/a}}" },
  expected: "c",
}]

SPEC << ["inheritance: Multi-level inheritance, no sub child", {
  template: "{{<parent}}{{/parent}}",
  data:     {},
  partials: { "parent" => "{{<older}}{{$a}}p{{/a}}{{/older}}", "older" => "{{<grandParent}}{{$a}}o{{/a}}{{/grandParent}}", "grandParent" => "{{$a}}g{{/a}}" },
  expected: "p",
}]

SPEC << ["inheritance: Text inside parent", {
  template: "{{<parent}} asdfasd {{$foo}}hmm{{/foo}} asdfasdfasdf {{/parent}}",
  data:     {},
  partials: { "parent" => "{{$foo}}default content{{/foo}}" },
  expected: "hmm",
}]

SPEC << ["inheritance: Text inside parent", {
  template: "{{<parent}} asdfasd asdfasdfasdf {{/parent}}",
  data:     {},
  partials: { "parent" => "{{$foo}}default content{{/foo}}" },
  expected: "default content",
}]

SPEC << ["inheritance: Block scope", {
  template: "{{<parent}}{{$block}}I say {{fruit}}.{{/block}}{{/parent}}",
  data:     { "fruit" => "apples", "nested" => { "fruit" => "bananas" } },
  partials: { "parent" => "{{\#nested}}{{$block}}You say {{fruit}}.{{/block}}{{/nested}}" },
  expected: "I say bananas.",
}]

SPEC << ["inheritance: Standalone parent", {
  template: "Hi,\n  {{<parent}}{{/parent}}\n",
  data:     {},
  partials: { "parent" => "one\ntwo\n" },
  expected: "Hi,\n  one\n  two\n",
}]

SPEC << ["inheritance: Standalone block", {
  template: "{{<parent}}{{$block}}\none\ntwo{{/block}}\n{{/parent}}\n",
  data:     {},
  partials: { "parent" => "Hi,\n  {{$block}}{{/block}}\n" },
  expected: "Hi,\n  one\n  two\n",
}]

SPEC << ["inheritance: Block reindentation", {
  template: "{{<parent}}{{$block}}\n    one\n    two\n{{/block}}{{/parent}}\n",
  data:     {},
  partials: { "parent" => "Hi,\n  {{$block}}\n  {{/block}}\n" },
  expected: "Hi,\n  one\n  two\n",
}]

SPEC << ["inheritance: Intrinsic indentation", {
  template: "{{<parent}}{{$block}}\none\ntwo\n{{/block}}{{/parent}}\n",
  data:     {},
  partials: { "parent" => "Hi,\n{{$block}}\n  default\n{{/block}}\n" },
  expected: "Hi,\n  one\n  two\n",
}]

SPEC << ["inheritance: Nested block reindentation", {
  template: "{{<parent}}{{$nested}}\nthree\n{{/nested}}{{/parent}}\n",
  data:     {},
  partials: { "parent" => "{{<grandparent}}{{$block}}\n  one\n  {{$nested}}\n    two\n  {{/nested}}\n{{/block}}{{/grandparent}}\n", "grandparent" => "{{$block}}default{{/block}}" },
  expected: "one\n  three\n",
}]


# ==========================================================================
# Run them
# ==========================================================================

# The spec's data carries String keys and some Integer values. Here a key
# is a Symbol and a value is a String, so the data is converted on the way
# in; what the spec expects is unchanged.
def spec_data_of(v)
  case v
  when Hash    then v.each_with_object({}) { |(k, x), h| h[k.to_sym] = spec_data_of(x) }
  when Array   then v.map { |x| spec_data_of(x) }
  when Integer, Float then v.to_s
  else v
  end
end

SPEC.each do |label, t|
  assert("spec: #{label}") do
    partials = (t[:partials] || {}).each_with_object({}) { |(k, v), h| h[k.to_sym] = Mustache::Template.new(v) }
    assert_equal t[:expected], Mustache::Template.new(t[:template]).render(spec_data_of(t[:data]), partials)
  end
end

# Each run of a template body adds its op count plus one to the render
# score, the way Liquid adds a block's node count per render. A list of n
# rows over a one-op body costs 3 for the outer run and 2 per row.
assert('the render score counts every run of a body') do
  data = { l: %w[a b c d e f g h i j] }
  assert_equal 'x' * 10, Mustache::Template.new('{{#l}}x{{/l}}', nil, nil, 23).render(data)
  e = assert_raise(Mustache::RenderError) { Mustache::Template.new('{{#l}}x{{/l}}', nil, nil, 22).render(data) }
  assert_equal 'the render does more work than max_render_score', e.message
end

# A review found that 360 bytes of partials, each calling the next four
# times, rendered nothing for 33 seconds: the answer limit never trips on
# empty output and the depth limits allow 4**15 calls. The score stops it
# after the build default, whatever the partials write.
assert('partials that fan out stop at the render score') do
  levels = [Mustache::MAX_PARTIAL_DEPTH - 1, 12].min
  partials = {}
  (1..levels).each { |i| partials[:"p#{i}"] = Mustache::Template.new("{{>p#{i + 1}}}" * 8) }
  partials[:"p#{levels + 1}"] = Mustache::Template.new('')
  top = Mustache::Template.new('{{>p1}}', nil, nil, [Mustache.max_render_score, 2**24].min)
  e = assert_raise(Mustache::RenderError) { top.render({}, partials) }
  assert_equal 'the render does more work than max_render_score', e.message
end

# The answer limit may not exceed what one mruby String can hold, less the
# slack the escaper writes past the end.
assert('the answer limit stays below the longest mruby String') do
  most = 0x7fffffffffffffff
  assert_raise(ArgumentError) { Mustache::Template.new('x', most) } if most.is_a?(Integer)
  assert_raise(ArgumentError) { Mustache::Template.new('x', -1) }
end

# This test lowers the limit of the VM, and nothing can raise it again, so
# it runs last: every test of this gem shares one VM (gem_test.c).
# max_source_bytes bounds how large a template's source may be. The build
# sets the ceiling, 2 GiB, because every offset in a compiled template is
# 32 bits wide. A VM may lower it, a Template class may lower it further
# for itself and its subclasses, and one template may lower it further
# still; no level may raise what the level around it allows.
assert('max_source_bytes: each level can only lower the one around it') do
  build = 2**31 - 1
  assert_equal build, Mustache.max_source_bytes
  assert_raise(ArgumentError) { Mustache.max_source_bytes = build + 1 }
  sub = Class.new(Mustache::Template)
  begin
    Mustache.max_source_bytes = 64
    assert_equal 64, Mustache.max_source_bytes
    assert_equal 64, Mustache::Template.max_source_bytes
    e = assert_raise(ArgumentError) { Mustache::Template.max_source_bytes = 65 }
    assert_equal 'max_source_bytes too long (len=65 max=64)', e.message
    sub.max_source_bytes = 16
    assert_equal 16, sub.max_source_bytes
    assert_equal 64, Mustache::Template.max_source_bytes
    assert_raise(ArgumentError) { sub.new('x', 16, 17) }
    assert_equal 'x', sub.new('x', 16, 8).render({})
    e = assert_raise(ArgumentError) { sub.new('x' * 17) }
    assert_equal 'template too long (len=17 max=16)', e.message
    assert_equal 'x' * 17, Mustache::Template.new('x' * 17).render({})
    assert_raise(ArgumentError) { Mustache::Template.new('x' * 65) }
    assert_raise(ArgumentError) { Mustache::Template.new('x' * 9, 64, 8) }
  end
end


# max_render_score follows the same rule as max_source_bytes. It also runs
# after every other test for the same reason.
assert('max_render_score: each level can only lower the one around it') do
  build = Mustache.max_render_score
  assert_true build >= 2**24
  assert_raise(ArgumentError) { Mustache.max_render_score = build + 1 }
  sub = Class.new(Mustache::Template)
  Mustache.max_render_score = 64
  assert_equal 64, Mustache::Template.max_render_score
  e = assert_raise(ArgumentError) { Mustache::Template.max_render_score = 65 }
  assert_equal 'max_render_score too long (len=65 max=64)', e.message
  sub.max_render_score = 2
  assert_equal 2, sub.max_render_score
  assert_raise(ArgumentError) { sub.new('x', nil, nil, 3) }
  assert_equal 'x', sub.new('x').render({})
  assert_raise(Mustache::RenderError) { sub.new('x{{y}}').render({}) }
  assert_equal 'x', Mustache::Template.new('x{{y}}').render({})
end
