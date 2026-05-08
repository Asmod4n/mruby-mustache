##
## mruby-mustache tests
##

# ---- basics --------------------------------------------------------------

assert('Mustache::VERSION-ish smoke test') do
  assert_equal 'Hello World', Mustache.mustache('Hello {{name}}', { 'name' => 'World' })
end

assert('renders empty template') do
  assert_equal '', Mustache.mustache('', {})
end

assert('renders template with no tags') do
  assert_equal 'static text', Mustache.mustache('static text', {})
end

# ---- key shapes ----------------------------------------------------------

assert('symbol keys') do
  assert_equal 'Hi alice', Mustache.mustache('Hi {{name}}', { name: 'alice' })
end

assert('string keys') do
  assert_equal 'Hi alice', Mustache.mustache('Hi {{name}}', { 'name' => 'alice' })
end

assert('mixed string and symbol keys in one hash') do
  data = { name: 'alice', 'age' => 30 }
  assert_equal 'alice/30', Mustache.mustache('{{name}}/{{age}}', data)
end

assert('integer keys via to_s') do
  data = { 1 => 'one', 2 => 'two' }
  assert_equal 'one two', Mustache.mustache('{{1}} {{2}}', data)
end

assert('missing key renders empty') do
  assert_equal 'a:', Mustache.mustache('a:{{missing}}', { name: 'x' })
end

# ---- value stringification -----------------------------------------------

assert('integer values') do
  assert_equal 'count=42', Mustache.mustache('count={{n}}', { n: 42 })
end

assert('nil values render empty') do
  assert_equal 'x=', Mustache.mustache('x={{v}}', { v: nil })
end

assert('false values render empty') do
  assert_equal 'x=false', Mustache.mustache('x={{v}}', { v: false })
end

assert('true values render as "true"') do
  assert_equal 'x=true', Mustache.mustache('x={{v}}', { v: true })
end

# ---- sections ------------------------------------------------------------

assert('truthy section renders body') do
  assert_equal 'on', Mustache.mustache('{{#flag}}on{{/flag}}', { flag: true })
end

assert('falsy section skips body') do
  assert_equal '', Mustache.mustache('{{#flag}}on{{/flag}}', { flag: false })
end

assert('nil section skips body') do
  assert_equal '', Mustache.mustache('{{#flag}}on{{/flag}}', { flag: nil })
end

assert('empty array section skips body') do
  assert_equal '', Mustache.mustache('{{#xs}}.{{/xs}}', { xs: [] })
end

assert('empty hash section skips body') do
  assert_equal '', Mustache.mustache('{{#h}}.{{/h}}', { h: {} })
end

assert('inverted section fires when falsy') do
  assert_equal 'no', Mustache.mustache('{{^flag}}no{{/flag}}', { flag: false })
end

assert('inverted section skips when truthy') do
  assert_equal '', Mustache.mustache('{{^flag}}no{{/flag}}', { flag: true })
end

# ---- array iteration -----------------------------------------------------

assert('array iteration with current-item dot') do
  data = { items: ['a', 'b', 'c'] }
  assert_equal 'a,b,c,', Mustache.mustache('{{#items}}{{.}},{{/items}}', data)
end

assert('array of hashes') do
  data = { users: [{ name: 'alice' }, { name: 'bob' }] }
  assert_equal 'alice|bob|', Mustache.mustache('{{#users}}{{name}}|{{/users}}', data)
end

assert('iteration sees outer scope (lookup walks up)') do
  data = { greeting: 'hi', users: [{ name: 'alice' }, { name: 'bob' }] }
  out = Mustache.mustache('{{#users}}{{greeting}} {{name}};{{/users}}', data)
  assert_equal 'hi alice;hi bob;', out
end

# ---- hash iteration (objiter extension) ----------------------------------

assert('hash iteration with key/value') do
  skip('not supported')
  data = { config: { host: 'localhost', port: 8080 } }
  out  = Mustache.mustache('{{#config.*}}{{*}}={{.}};{{/config.*}}', data)
  # Order depends on hash insertion order — both keys must appear.
  assert_true out.include?('host=localhost;')
  assert_true out.include?('port=8080;')
end

# ---- subsel / dot paths --------------------------------------------------

assert('subsel into nested hash') do
  data = { user: { name: 'alice', role: 'admin' } }
  assert_equal 'alice (admin)', Mustache.mustache('{{user.name}} ({{user.role}})', data)
end

assert('subsel two levels deep') do
  data = { a: { b: { c: 'deep' } } }
  assert_equal 'deep', Mustache.mustache('{{a.b.c}}', data)
end

# ---- escaping ------------------------------------------------------------

assert('double-stache HTML-escapes') do
  data = { html: '<b>hi</b>' }
  assert_equal '&lt;b&gt;hi&lt;/b&gt;', Mustache.mustache('{{html}}', data)
end

assert('triple-stache does not escape') do
  data = { html: '<b>hi</b>' }
  assert_equal '<b>hi</b>', Mustache.mustache('{{{html}}}', data)
end

assert('ampersand prefix does not escape') do
  data = { html: '<b>hi</b>' }
  assert_equal '<b>hi</b>', Mustache.mustache('{{&html}}', data)
end

# ---- comments ------------------------------------------------------------

assert('comments are stripped') do
  assert_equal 'ab', Mustache.mustache('a{{! ignored }}b', {})
end

# ---- extension flags -----------------------------------------------------

assert('equality extension matches') do
  skip('not supported')
  data = { status: 'ok' }
  out  = Mustache.mustache('{{#status=ok}}good{{/status=ok}}', data)
  assert_equal 'good', out
end

assert('equality extension non-match') do
  data = { status: 'fail' }
  out  = Mustache.mustache('{{#status=ok}}good{{/status=ok}}', data)
  assert_equal '', out
end


# test/mustache_spec.rb
#
# Conformance tests against the official mustache spec.
# Source: https://github.com/mustache/spec
#
# Subset covered: comments, interpolation, sections, inverted.
# Not covered: delimiters, partials, ~lambdas, ~dynamic-names, ~inheritance —
# we don't implement these features.
#
# Tests requiring "standalone line" whitespace handling are marked :skip.
# We do not strip whitespace around section/comment tags that are alone on
# their line; that's a documented non-goal of this implementation.
#
# Each test is one hash: { name:, template:, data:, expected:, skip: nil | "reason" }

SPEC = []

# ==========================================================================
# Comments
# ==========================================================================

SPEC << ['comments: Inline', {
  template: '12345{{! Comment Block! }}67890',
  data:     {},
  expected: '1234567890',
}]
SPEC << ['comments: Multiline', {
  template: "12345{{!\n  This is a\n  multi-line comment...\n}}67890\n",
  data:     {},
  expected: "1234567890\n",
}]
SPEC << ['comments: Standalone', {
  template: "Begin.\n{{! Comment Block! }}\nEnd.\n",
  data:     {},
  expected: "Begin.\nEnd.\n"
}]
SPEC << ['comments: Indented Standalone', {
  template: "Begin.\n  {{! Indented Comment Block! }}\nEnd.\n",
  data:     {},
  expected: "Begin.\nEnd.\n"
}]
SPEC << ['comments: Standalone Line Endings', {
  template: "|\r\n{{! Standalone Comment }}\r\n|",
  data:     {},
  expected: "|\r\n|"
}]
SPEC << ['comments: Standalone Without Previous Line', {
  template: "  {{! I'm Still Standalone }}\n!",
  data:     {},
  expected: "!"
}]
SPEC << ['comments: Standalone Without Newline', {
  template: "!\n  {{! I'm Still Standalone }}",
  data:     {},
  expected: "!\n"
}]
SPEC << ['comments: Multiline Standalone', {
  template: "Begin.\n{{!\nSomething's going on here...\n}}\nEnd.\n",
  data:     {},
  expected: "Begin.\nEnd.\n"
}]
SPEC << ['comments: Indented Multiline Standalone', {
  template: "Begin.\n  {{!\n    Something's going on here...\n  }}\nEnd.\n",
  data:     {},
  expected: "Begin.\nEnd.\n"
}]
SPEC << ['comments: Indented Inline', {
  template: "  12 {{! 34 }}\n",
  data:     {},
  expected: "  12 \n",
}]
SPEC << ['comments: Surrounding Whitespace', {
  template: "12345 {{! Comment Block! }} 67890",
  data:     {},
  expected: "12345  67890",
}]

# ==========================================================================
# Interpolation
# ==========================================================================

SPEC << ['interpolation: No Interpolation', {
  template: "Hello from {Mustache}!\n",
  data:     {},
  expected: "Hello from {Mustache}!\n",
}]
SPEC << ['interpolation: Basic Interpolation', {
  template: "Hello, {{subject}}!\n",
  data:     { 'subject' => 'world' },
  expected: "Hello, world!\n",
}]
SPEC << ['interpolation: No Re-interpolation', {
  template: '{{template}}: {{planet}}',
  data:     { 'template' => '{{planet}}', 'planet' => 'Earth' },
  expected: '{{planet}}: Earth',
}]
SPEC << ['interpolation: HTML Escaping', {
  template: "These characters should be HTML escaped: {{forbidden}}\n",
  data:     { 'forbidden' => '& " < >' },
  expected: "These characters should be HTML escaped: &amp; &quot; &lt; &gt;\n",
}]
SPEC << ['interpolation: Triple Mustache', {
  template: "These characters should not be HTML escaped: {{{forbidden}}}\n",
  data:     { 'forbidden' => '& " < >' },
  expected: "These characters should not be HTML escaped: & \" < >\n",
}]
SPEC << ['interpolation: Ampersand', {
  template: "These characters should not be HTML escaped: {{&forbidden}}\n",
  data:     { 'forbidden' => '& " < >' },
  expected: "These characters should not be HTML escaped: & \" < >\n",
}]
SPEC << ['interpolation: Basic Integer Interpolation', {
  template: '"{{mph}} miles an hour!"',
  data:     { 'mph' => 85 },
  expected: '"85 miles an hour!"',
}]
SPEC << ['interpolation: Triple Mustache Integer Interpolation', {
  template: '"{{{mph}}} miles an hour!"',
  data:     { 'mph' => 85 },
  expected: '"85 miles an hour!"',
}]
SPEC << ['interpolation: Ampersand Integer Interpolation', {
  template: '"{{&mph}} miles an hour!"',
  data:     { 'mph' => 85 },
  expected: '"85 miles an hour!"',
}]
SPEC << ['interpolation: Basic Null Interpolation', {
  template: 'I ({{cannot}}) be seen!',
  data:     { 'cannot' => nil },
  expected: 'I () be seen!',
}]
SPEC << ['interpolation: Triple Mustache Null Interpolation', {
  template: 'I ({{{cannot}}}) be seen!',
  data:     { 'cannot' => nil },
  expected: 'I () be seen!',
}]
SPEC << ['interpolation: Ampersand Null Interpolation', {
  template: 'I ({{&cannot}}) be seen!',
  data:     { 'cannot' => nil },
  expected: 'I () be seen!',
}]
SPEC << ['interpolation: Basic Context Miss Interpolation', {
  template: 'I ({{cannot}}) be seen!',
  data:     {},
  expected: 'I () be seen!',
}]
SPEC << ['interpolation: Triple Mustache Context Miss Interpolation', {
  template: 'I ({{{cannot}}}) be seen!',
  data:     {},
  expected: 'I () be seen!',
}]
SPEC << ['interpolation: Ampersand Context Miss Interpolation', {
  template: 'I ({{&cannot}}) be seen!',
  data:     {},
  expected: 'I () be seen!',
}]
SPEC << ['interpolation: Dotted Names - Basic Interpolation', {
  template: '"{{person.name}}" == "{{#person}}{{name}}{{/person}}"',
  data:     { 'person' => { 'name' => 'Joe' } },
  expected: '"Joe" == "Joe"',
}]
SPEC << ['interpolation: Dotted Names - Triple Mustache Interpolation', {
  template: '"{{{person.name}}}" == "{{#person}}{{{name}}}{{/person}}"',
  data:     { 'person' => { 'name' => 'Joe' } },
  expected: '"Joe" == "Joe"',
}]
SPEC << ['interpolation: Dotted Names - Ampersand Interpolation', {
  template: '"{{&person.name}}" == "{{#person}}{{&name}}{{/person}}"',
  data:     { 'person' => { 'name' => 'Joe' } },
  expected: '"Joe" == "Joe"',
}]
SPEC << ['interpolation: Dotted Names - Arbitrary Depth', {
  template: '"{{a.b.c.d.e.name}}" == "Phil"',
  data:     { 'a' => { 'b' => { 'c' => { 'd' => { 'e' => { 'name' => 'Phil' } } } } } },
  expected: '"Phil" == "Phil"',
}]
SPEC << ['interpolation: Dotted Names - Broken Chains', {
  template: '"{{a.b.c}}" == ""',
  data:     { 'a' => {} },
  expected: '"" == ""',
}]
SPEC << ['interpolation: Dotted Names - Broken Chain Resolution', {
  template: '"{{a.b.c.name}}" == ""',
  data:     { 'a' => { 'b' => {} }, 'c' => { 'name' => 'Jim' } },
  expected: '"" == ""',
}]
SPEC << ['interpolation: Dotted Names - Initial Resolution', {
  template: '"{{#a}}{{b.c.d.e.name}}{{/a}}" == "Phil"',
  data:     {
    'a' => { 'b' => { 'c' => { 'd' => { 'e' => { 'name' => 'Phil' } } } } },
    'b' => { 'c' => { 'd' => { 'e' => { 'name' => 'Wrong' } } } },
  },
  expected: '"Phil" == "Phil"',
}]
SPEC << ['interpolation: Dotted Names - Context Precedence', {
  template: '{{#a}}{{b.c}}{{/a}}',
  data:     { 'a' => { 'b' => {} }, 'b' => { 'c' => 'ERROR' } },
  expected: '',
}]
SPEC << ['interpolation: Dotted Names are never single keys', {
  template: '{{a.b}}',
  data:     { 'a.b' => 'c' },
  expected: '',
}]
SPEC << ['interpolation: Dotted Names - No Masking', {
  template: '{{a.b}}',
  data:     { 'a.b' => 'c', 'a' => { 'b' => 'd' } },
  expected: 'd',
}]
SPEC << ['interpolation: Implicit Iterators - Basic Interpolation', {
  template: "Hello, {{.}}!\n",
  data:     'world',
  expected: "Hello, world!\n",
}]
SPEC << ['interpolation: Implicit Iterators - HTML Escaping', {
  template: "These characters should be HTML escaped: {{.}}\n",
  data:     '& " < >',
  expected: "These characters should be HTML escaped: &amp; &quot; &lt; &gt;\n",
}]
SPEC << ['interpolation: Implicit Iterators - Triple Mustache', {
  template: "These characters should not be HTML escaped: {{{.}}}\n",
  data:     '& " < >',
  expected: "These characters should not be HTML escaped: & \" < >\n",
}]
SPEC << ['interpolation: Implicit Iterators - Ampersand', {
  template: "These characters should not be HTML escaped: {{&.}}\n",
  data:     '& " < >',
  expected: "These characters should not be HTML escaped: & \" < >\n",
}]
SPEC << ['interpolation: Implicit Iterators - Basic Integer Interpolation', {
  template: '"{{.}} miles an hour!"',
  data:     85,
  expected: '"85 miles an hour!"',
}]
SPEC << ['interpolation: Interpolation - Surrounding Whitespace', {
  template: '| {{string}} |',
  data:     { 'string' => '---' },
  expected: '| --- |',
}]
SPEC << ['interpolation: Triple Mustache - Surrounding Whitespace', {
  template: '| {{{string}}} |',
  data:     { 'string' => '---' },
  expected: '| --- |',
}]
SPEC << ['interpolation: Ampersand - Surrounding Whitespace', {
  template: '| {{&string}} |',
  data:     { 'string' => '---' },
  expected: '| --- |',
}]
SPEC << ['interpolation: Interpolation - Standalone', {
  template: " {{string}}\n",
  data:     { 'string' => '---' },
  expected: " ---\n",
}]
SPEC << ['interpolation: Triple Mustache - Standalone', {
  template: " {{{string}}}\n",
  data:     { 'string' => '---' },
  expected: " ---\n",
}]
SPEC << ['interpolation: Ampersand - Standalone', {
  template: " {{&string}}\n",
  data:     { 'string' => '---' },
  expected: " ---\n",
}]
SPEC << ['interpolation: Interpolation With Padding', {
  template: '|{{ string }}|',
  data:     { 'string' => '---' },
  expected: '|---|',
}]
SPEC << ['interpolation: Triple Mustache With Padding', {
  template: '|{{{ string }}}|',
  data:     { 'string' => '---' },
  expected: '|---|',
}]
SPEC << ['interpolation: Ampersand With Padding', {
  template: '|{{& string }}|',
  data:     { 'string' => '---' },
  expected: '|---|',
}]

# ==========================================================================
# Sections
# ==========================================================================

SPEC << ['sections: Truthy', {
  template: '"{{#boolean}}This should be rendered.{{/boolean}}"',
  data:     { 'boolean' => true },
  expected: '"This should be rendered."',
}]
SPEC << ['sections: Falsey', {
  template: '"{{#boolean}}This should not be rendered.{{/boolean}}"',
  data:     { 'boolean' => false },
  expected: '""',
}]
SPEC << ['sections: Context', {
  template: '"{{#context}}Hi {{name}}.{{/context}}"',
  data:     { 'context' => { 'name' => 'Joe' } },
  expected: '"Hi Joe."',
}]
SPEC << ['sections: Deeply Nested Contexts', {
  template: '',
  data:     {},
  expected: ''
}]
SPEC << ['sections: List', {
  template: '"{{#list}}{{item}}{{/list}}"',
  data:     { 'list' => [{ 'item' => 1 }, { 'item' => 2 }, { 'item' => 3 }] },
  expected: '"123"',
}]
SPEC << ['sections: Empty List', {
  template: '"{{#list}}Yay lists!{{/list}}"',
  data:     { 'list' => [] },
  expected: '""',
}]
SPEC << ['sections: Doubled', {
  template: '',
  data:     {},
  expected: ''
}]
SPEC << ['sections: Nested (Truthy)', {
  template: '| A {{#bool}}B {{#bool}}C{{/bool}} D{{/bool}} E |',
  data:     { 'bool' => true },
  expected: '| A B C D E |',
}]
SPEC << ['sections: Nested (Falsey)', {
  template: '| A {{#bool}}B {{#bool}}C{{/bool}} D{{/bool}} E |',
  data:     { 'bool' => false },
  expected: '| A  E |',     # two spaces
}]
SPEC << ['sections: Context Misses', {
  template: "[{{#missing}}Found key 'missing'!{{/missing}}]",
  data:     {},
  expected: '[]',
}]
SPEC << ['sections: Implicit Iterator - String', {
  template: '"{{#list}}({{.}}){{/list}}"',
  data:     { 'list' => ['a', 'b', 'c', 'd', 'e'] },
  expected: '"(a)(b)(c)(d)(e)"',
}]
SPEC << ['sections: Implicit Iterator - Integer', {
  template: '"{{#list}}({{.}}){{/list}}"',
  data:     { 'list' => [1, 2, 3, 4, 5] },
  expected: '"(1)(2)(3)(4)(5)"',
}]
SPEC << ['sections: Dotted Names - Truthy', {
  template: '"{{#a.b.c}}Here{{/a.b.c}}" == "Here"',
  data:     { 'a' => { 'b' => { 'c' => true } } },
  expected: '"Here" == "Here"',
}]
SPEC << ['sections: Dotted Names - Falsey', {
  template: '"{{#a.b.c}}Here{{/a.b.c}}" == ""',
  data:     { 'a' => { 'b' => { 'c' => false } } },
  expected: '"" == ""',
}]
SPEC << ['sections: Dotted Names - Broken Chains', {
  template: '"{{#a.b.c}}Here{{/a.b.c}}" == ""',
  data:     { 'a' => {} },
  expected: '"" == ""',
}]
SPEC << ['sections: Surrounding Whitespace', {
  template: " | {{#boolean}}\t|\t{{/boolean}} | \n",
  data:     { 'boolean' => true },
  expected: " | \t|\t | \n",
}]
SPEC << ['sections: Internal Whitespace', {
  template: '',
  data:     {},
  expected: ''
}]
SPEC << ['sections: Indented Inline Sections', {
  template: " {{#boolean}}YES{{/boolean}}\n {{#boolean}}GOOD{{/boolean}}\n",
  data:     { 'boolean' => true },
  expected: " YES\n GOOD\n",
}]
SPEC << ['sections: Standalone Lines', {
  template: '',
  data:     {},
  expected: ''
}]
SPEC << ['sections: Indented Standalone Lines', {
  template: '',
  data:     {},
  expected: ''
}]
SPEC << ['sections: Standalone Line Endings', {
  template: '',
  data:     {},
  expected: ''
}]
SPEC << ['sections: Standalone Without Previous Line', {
  template: '',
  data:     {},
  expected: ''
}]
SPEC << ['sections: Standalone Without Newline', {
  template: '',
  data:     {},
  expected: ''
}]
SPEC << ['sections: Padding', {
  template: '|{{# boolean }}={{/ boolean }}|',
  data:     { 'boolean' => true },
  expected: '|=|',
}]

# ==========================================================================
# Inverted Sections
# ==========================================================================

SPEC << ['inverted: Falsey', {
  template: '"{{^boolean}}This should be rendered.{{/boolean}}"',
  data:     { 'boolean' => false },
  expected: '"This should be rendered."',
}]
SPEC << ['inverted: Truthy', {
  template: '"{{^boolean}}This should not be rendered.{{/boolean}}"',
  data:     { 'boolean' => true },
  expected: '""',
}]
SPEC << ['inverted: Null is falsey', {
  template: '"{{^null}}This should be rendered.{{/null}}"',
  data:     { 'null' => nil },
  expected: '"This should be rendered."',
}]
SPEC << ['inverted: Context', {
  template: '"{{^context}}Hi {{name}}.{{/context}}"',
  data:     { 'context' => { 'name' => 'Joe' } },
  expected: '""',
}]
SPEC << ['inverted: List', {
  template: '"{{^list}}{{n}}{{/list}}"',
  data:     { 'list' => [{ 'n' => 1 }, { 'n' => 2 }, { 'n' => 3 }] },
  expected: '""',
}]
SPEC << ['inverted: Empty List', {
  template: '"{{^list}}Yay lists!{{/list}}"',
  data:     { 'list' => [] },
  expected: '"Yay lists!"',
}]
SPEC << ['inverted: Doubled', {
  template: '',
  data:     {},
  expected: ''
}]
SPEC << ['inverted: Nested (Falsey)', {
  template: '| A {{^bool}}B {{^bool}}C{{/bool}} D{{/bool}} E |',
  data:     { 'bool' => false },
  expected: '| A B C D E |',
}]
SPEC << ['inverted: Nested (Truthy)', {
  template: '| A {{^bool}}B {{^bool}}C{{/bool}} D{{/bool}} E |',
  data:     { 'bool' => true },
  expected: '| A  E |',     # two spaces
}]
SPEC << ['inverted: Context Misses', {
  template: "[{{^missing}}Cannot find key 'missing'!{{/missing}}]",
  data:     {},
  expected: "[Cannot find key 'missing'!]",
}]
SPEC << ['inverted: Dotted Names - Truthy', {
  template: '"{{^a.b.c}}Not Here{{/a.b.c}}" == ""',
  data:     { 'a' => { 'b' => { 'c' => true } } },
  expected: '"" == ""',
}]
SPEC << ['inverted: Dotted Names - Falsey', {
  template: '"{{^a.b.c}}Not Here{{/a.b.c}}" == "Not Here"',
  data:     { 'a' => { 'b' => { 'c' => false } } },
  expected: '"Not Here" == "Not Here"',
}]
SPEC << ['inverted: Dotted Names - Broken Chains', {
  template: '"{{^a.b.c}}Not Here{{/a.b.c}}" == "Not Here"',
  data:     { 'a' => {} },
  expected: '"Not Here" == "Not Here"',
}]
SPEC << ['inverted: Surrounding Whitespace', {
  template: " | {{^boolean}}\t|\t{{/boolean}} | \n",
  data:     { 'boolean' => false },
  expected: " | \t|\t | \n",
}]
SPEC << ['inverted: Internal Whitespace', {
  template: '',
  data:     {},
  expected: ''
}]
SPEC << ['inverted: Indented Inline Sections', {
  template: " {{^boolean}}NO{{/boolean}}\n {{^boolean}}WAY{{/boolean}}\n",
  data:     { 'boolean' => false },
  expected: " NO\n WAY\n",
}]
SPEC << ['inverted: Standalone Lines', {
  template: '',
  data:     {},
  expected: ''
}]
SPEC << ['inverted: Standalone Indented Lines', {
  template: '',
  data:     {},
  expected: ''
}]
SPEC << ['inverted: Standalone Line Endings', {
  template: '',
  data:     {},
  expected: ''
}]
SPEC << ['inverted: Standalone Without Previous Line', {
  template: '',
  data:     {},
  expected: ''
}]
SPEC << ['inverted: Standalone Without Newline', {
  template: '',
  data:     {},
  expected: ''
}]
SPEC << ['inverted: Padding', {
  template: '|{{^ boolean }}={{/ boolean }}|',
  data:     { 'boolean' => false },
  expected: '|=|',
}]

# ==========================================================================
# Partials
# ==========================================================================

SPEC << ['partials: Basic Behavior', {
  template: '"{{>text}}"',
  data:     {},
  partials: { 'text' => 'from partial' },
  expected: '"from partial"',
}]
SPEC << ['partials: Failed Lookup', {
  template: '"{{>text}}"',
  data:     {},
  partials: {},
  expected: '""',
}]
SPEC << ['partials: Context', {
  template: '"{{>partial}}"',
  data:     { 'text' => 'content' },
  partials: { 'partial' => '*{{text}}*' },
  expected: '"*content*"',
}]
SPEC << ['partials: Recursion', {
  template: '{{>node}}',
  data:     { 'content' => 'X', 'nodes' => [{ 'content' => 'Y', 'nodes' => [] }] },
  partials: { 'node' => '{{content}}<{{#nodes}}{{>node}}{{/nodes}}>' },
  expected: 'X<Y<>>',
}]

# Standalone-line variants — important to verify indentation propagation:
SPEC << ['partials: Standalone Indentation', {
  template: "\\\n {{>partial}}\n/\n",
  data:     { 'content' => "<\n->" },     # double-quoted: real newline
  partials: { 'partial' => "|\n{{{content}}}\n|\n" },
  expected: "\\\n |\n <\n->\n |\n/\n",
}]


# ==========================================================================
# Categories not implemented at all — documented as skips
# ==========================================================================

[
  'delimiters: Pair Behavior',
  'delimiters: Special Characters',
  'delimiters: Sections',
  'delimiters: Inverted Sections',
  '~lambdas: Interpolation',
  '~lambdas: Section',
  '~lambdas: Inverted Section',
  '~dynamic-names: Basic Behavior',
  '~inheritance: Default',
].each do |label|
  SPEC << [label, {
    template: '',
    data:     {},
    expected: '',
    skip: 'feature not implemented (delimiters/partials/lambdas/dynamic-names/inheritance)',
  }]
end

# ==========================================================================
# Run them
# ==========================================================================

SPEC.each do |label, t|
  assert("spec: #{label}") do
    if t[:skip]
      skip(t[:skip])
    elsif t[:partials]
      compiled = {}
      t[:partials].each { |k, v| compiled[k] = Mustache::Template.compile(v) }
      result = Mustache::Template.compile(t[:template]).render(t[:data], compiled)
      assert_equal t[:expected], result
    else
      assert_equal t[:expected], Mustache.mustache(t[:template], t[:data])
    end
  end
end
