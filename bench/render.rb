# bench/render.rb

TEMPLATE = Mustache::Template.compile(<<~'TMPL')
  <h1>{{{title}}}</h1>
  {{#posts}}
  <article>
    <h2>{{{title}}}</h2>
    <p>{{{teaser}}}</p>
  </article>
  {{/posts}}
  {{^posts}}
  <p>No posts yet.</p>
  {{/posts}}
TMPL

DATA = {
  'title' => 'My Blog',
  'posts' => [
    { 'title' => 'First post',  'teaser' => 'Hello world' },
    { 'title' => 'Second post', 'teaser' => 'More content here' },
    { 'title' => 'Third post',  'teaser' => 'Even more' },
  ],
}

ITERS = 1_000_000
ITERS.times { TEMPLATE.render(DATA) }
