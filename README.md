````markdown
# mruby-mustache

Mustache templates for mruby — a wrapper around the C library <https://gitlab.com/jobol/mustach>.

Mustach is a C implementation of the Mustache template specification, version 1.4.1.

Installation
============
Add the following to your `build_config.rb`:
```ruby
conf.gem github: 'Asmod4n/mruby-mustache'
```
For more information about mruby gems: https://github.com/mruby/mruby/blob/master/doc/guides/mrbgems.md

Usage Examples
==============
```ruby
template = 'Hello {{name}}'
data = {name: "World"}
puts Mustache.mustache(template, data)
```
Refer to https://mustache.github.io/mustache.5.html for the Mustache language reference.

Notes
=====
This mruby gem doesn't aim to be compatible with the API of the official Ruby Mustache gem nor support all its features, especially not the unsafe ones like File reading or running of ruby code inside a template.

Data is walked directly as mruby `Hash` and `Array` values; no JSON parsing happens at render time. Hash keys are matched by their stringified form, so `String`, `Symbol`, `Integer`, or any `to_s`-able key works uniformly. Values are rendered via `to_s`.

Error handling
==============
When an error occurs, an exception of type `Mustache::Error` (or one of its subclasses) is raised. Subclasses correspond to the underlying mustach error codes, so you can rescue at any granularity:
```ruby
begin
  Mustache.mustache(tmpl, data)
rescue Mustache::Error::UndefinedTag
  # only raised with With_ErrorUndefined
rescue Mustache::Error::Closing
  # template has mismatched {{#x}}...{{/y}}
rescue Mustache::Error => e
  # any mustache problem
end
```
Available subclasses: `System`, `UnexpectedEnd`, `EmptyTag`, `TagTooLong`, `BadSeparators`, `TooDeep`, `Closing`, `BadUnescapeTag`, `InvalidItf`, `ItemNotFound`, `PartialNotFound`, `UndefinedTag`, `TooMuchNesting`, `Unknown`.

API
===
```ruby
Mustache.mustache(template, data, flags = Mustache::With_AllExtensions)
```
`template` is a Mustache template.

`data` is the data to apply to the template.

`flags` is a bitmask.

Here is a flags summary.

```pre
Flag name                      | Description
-------------------------------+------------------------------------------------
Mustache::With_Colon           | Explicit tag substitution with colon
Mustache::With_EmptyTag        | Empty Tag Allowed
-------------------------------+------------------------------------------------
Mustache::With_Equal           | Value Testing Equality
Mustache::With_Compare         | Value Comparing
Mustache::With_JsonPointer     | Interpret JSON Pointers
Mustache::With_ObjectIter      | Iteration On Objects
Mustache::With_EscFirstCmp     | Escape First Compare
Mustache::With_ErrorUndefined  | Error when a requested tag is undefined
-------------------------------+------------------------------------------------
Mustache::With_AllExtensions   | Activate all known extensions
Mustache::With_NoExtensions    | Disable any extension
```
You can combine multiple flags like `Mustache::With_Equal | Mustache::With_Compare`.

For details about the flags, see below.

### Explicit Tag Substitution With Colon (Mustache::With_Colon)

In some cases the name of the key used for substitution begins with a character reserved for mustach: one of `#`, `^`, `/`, `&`, `{`, `>` and `=`.

This extension introduces the special character `:` to explicitly tell mustach to just substitute the value. So `:` becomes a new special character.

### Empty Tag Allowed (Mustache::With_EmptyTag)

When an empty tag is found, instead of automatically raising `Mustache::Error::EmptyTag`, pass it.

### Value Testing Equality (Mustache::With_Equal)

This extension allows you to test the value of the selected key. It allows to write `key=value` (matching test) or `key=!value` (not matching test) in any query.

### Value Comparing (Mustache::With_Compare)

This extension extends the equality test to also compare values. It allows to write `key>value` (greater), `key>=value` (greater or equal), `key<value` (lesser) and `key<=value` (lesser or equal).

If the comparator sign appears in the first column it is ignored as if it was escaped.

Note: comparison is done lexicographically against the stringified value, not numerically. `{{#count>=10}}` against `count: 9` compares `"9"` against `"10"` and treats `"9"` as greater.

### Interpret JSON Pointers (Mustache::With_JsonPointer)

This extension allows the use of JSON pointers as defined in IETF RFC 6901. If active, any key starting with `/` is a JSON pointer. This implies using the colon to introduce keys.

A special escaping is used for `=`, `<`, `>` signs when value comparisons are enabled: `~=` gives `=` in the key.

Note: array indexing inside JSON pointers (e.g. `/users/0/name`) is not currently supported — only hash key segments are walked.

### Iteration On Objects (Mustache::With_ObjectIter)

With this extension, the pattern `{{#X.*}}...{{/X.*}}` allows iteration over the fields of `X`.

Example:

- `{{#s.*}} {{*}}:{{.}}{{/s.*}}` applied to `{"s" => {"a" => 1, "b" => true}}` produces ` a:1 b:true`.

Here the single star `{{*}}` is replaced by the iterated key, and the single dot `{{.}}` by its value.

### Error when a requested tag is undefined (Mustache::With_ErrorUndefined)

Raise `Mustache::Error::UndefinedTag` when a requested tag is not defined.

### Access To Current Value

*This was an extension but is now always enforced.*

The value of the current field can be accessed using a single dot.

Examples:

- `{{#key}}{{.}}{{/key}}` applied to `{key: 3.14}` produces `3.14`.
- `{{#array}} {{.}}{{/array}}` applied to `{array: [1, 2]}` produces ` 1 2`.

### Partial Data First

*This was an extension but is now always enforced.*

Reading partials from files is disabled in this mruby gem, so this flag has no effect — partials are always resolved against the data context (or fail).

### Escape First Compare

This extension automatically escapes comparison signs that appear as the first character of a tag.