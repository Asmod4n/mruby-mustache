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
  data = { status: 'ok' }
  out  = Mustache.mustache('{{#status=ok}}good{{/status=ok}}', data)
  assert_equal 'good', out
end

assert('equality extension non-match') do
  data = { status: 'fail' }
  out  = Mustache.mustache('{{#status=ok}}good{{/status=ok}}', data)
  assert_equal '', out
end

assert('NoExtensions disables ObjectIter') do
  data = { config: { host: 'localhost' } }
  # With_NoExtensions should treat `config.*` as a literal key path,
  # which doesn't exist, so the section is skipped.
  out  = Mustache.mustache(
    '{{#config.*}}x{{/config.*}}', data, Mustache::With_NoExtensions
  )
  assert_equal '', out
end

# ---- error hierarchy -----------------------------------------------------

assert('Mustache::Error is a RuntimeError') do
  assert_true Mustache::Error.ancestors.include?(RuntimeError)
end

assert('every error subclass < Mustache::Error') do
  %i[
    System UnexpectedEnd EmptyTag TagTooLong BadSeparators
    TooDeep Closing BadUnescapeTag InvalidItf ItemNotFound
    PartialNotFound UndefinedTag TooMuchNesting Unknown
  ].each do |sym|
    cls = Mustache::Error.const_get(sym)
    assert_true cls.ancestors.include?(Mustache::Error),
                "#{cls} should inherit from Mustache::Error"
  end
end

assert('mismatched closing tag raises Closing') do
  assert_raise(Mustache::Error::Closing) do
    Mustache.mustache('{{#a}}x{{/b}}', { a: true })
  end
end

assert('unclosed section raises UnexpectedEnd') do
  assert_raise(Mustache::Error::UnexpectedEnd) do
    Mustache.mustache('{{#a}}x', { a: true })
  end
end

assert('With_ErrorUndefined raises UndefinedTag') do
  assert_raise(Mustache::Error::UndefinedTag) do
    Mustache.mustache('{{missing}}', {}, Mustache::With_ErrorUndefined)
  end
end

assert('all errors rescuable as Mustache::Error') do
  assert_raise(Mustache::Error) do
    Mustache.mustache('{{#a}}x{{/b}}', { a: true })
  end
end

# ---- arg validation ------------------------------------------------------

assert('rejects non-string template') do
  assert_raise(TypeError) { Mustache.mustache(123, {}) }
end

assert('flags arg accepted as integer') do
  out = Mustache.mustache('{{x}}', { x: 'y' }, Mustache::With_AllExtensions)
  assert_equal 'y', out
end
