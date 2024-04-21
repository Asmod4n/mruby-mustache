# mruby-mustache
Mustache templates for mruby as a mruby wrapper around the c library https://gitlab.com/jobol/mustach
mustach is a C implementation of the mustache template specification, version 1.4.1.

Requirements
============
You need to have cJSON, jansson or json-c with developerment headers installed

Installation
============
Add the following to your build_config.rb
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
refer to https://mustache.github.io/mustache.5.html for more information about the Mustache language.

Notes
=====
This mruby gem doesn't aim to be compatible with the API of the official Ruby Mustache gem nor support all its features, especially not the unsafe ones like File reading or running of ruby code inside a template.
The c library mustach ships with JSON as the data exchange format by default, if you use Mustache.mustache the data object is automatically converted to a JSON string and passed to mustach.
Once mustach ships with a Object oriented compatible API this gem will be updated to support that too.

Errorhandling
=============
When a error occurs exceptions of the type Mustache::Error are raised.

API
==============
```ruby
Mustache.mustache(template, data, data_type = :ruby, flags = Mustache::With_AllExtensions)
```
template is a Mustache template.
data is the data to apply to the template.
data_type can be :ruby or :json, when using :json you must pass a JSON conforming string as data.

flags is a bitmask

Here is a flags summary.

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

you can use multiple flags like this Mustache::With_Equal | Mustache::With_Compare

For details about the flags, see below.

### Explicit Tag Substitution With Colon (Mustache::With_Colon)

In somecases the name of the key used for substitution begins with a
character reserved for mustach: one of `#`, `^`, `/`, `&`, `{`, `>` and `=`.

This extension introduces the special character `:` to explicitly
tell mustach to just substitute the value. So `:` becomes a new special
character.

### Empty Tag Allowed (Mustache::With_EmptyTag)

When an empty tag is found, instead of automatically raising the error
MUSTACH\_ERROR\_EMPTY\_TAG pass it.

### Value Testing Equality (Mustache::With_Equal)

This extension allows you to test the value of the selected key.
It allows to write `key=value` (matching test) or `key=!value`
(not matching test) in any query.

### Value Comparing (Mustache::With_Compare)

These extension extends the extension for testing equality to also
compare values if greater or lesser.
Its allows to write `key>value` (greater), `key>=value` (greater or equal),
`key<value` (lesser) and `key<=value` (lesser or equal).

It the comparator sign appears in the first column it is ignored
as if it was escaped.

### Interpret JSON Pointers (Mustache::With_JsonPointer)

This extension allows to use JSON pointers as defined in IETF RFC 6901.
If active, any key starting with "/" is a JSON pointer.
This implies to use the colon to introduce JSON keys.

A special escaping is used for `=`, `<`, `>` signs when
values comparisons are enabled: `~=` gives `=` in the key.

### Iteration On Objects (Mustache::With_ObjectIter)

With this extension, using the pattern `{{#X.*}}...{{/X.*}}`
allows to iterate on fields of `X`.

Example:

- `{{s.*}} {{*}}:{{.}}{{/s.*}}` applied on `{"s":{"a":1,"b":true}}` produces ` a:1 b:true`

Here the single star `{{*}}` is replaced by the iterated key
and the single dot `{{.}}` is replaced by its value.

### Error when a requested tag is undefined (Mustache::With_ErrorUndefined)

Report the error MUSTACH_ERROR_UNDEFINED_TAG when a requested tag
is not defined.

### Access To Current Value

*this was an extension but is now always enforced*

The value of the current field can be accessed using single dot.

Examples:

- `{{#key}}{{.}}{{/key}}` applied to `{"key":3.14}` produces `3.14`
- `{{#array}} {{.}}{{/array}}` applied to `{"array":[1,2]}` produces ` 1 2`.

### Partial Data First

*this was an extension but is now always enforced*

The default resolution for partial pattern like `{{> name}}`
is to search for `name` in the current json context or if not found `name.mustache`.

By default, the order of the search is (1) as a file,
and if not found, (2) in the current json context.

When this option is set, the order is reverted and content
of partial is search (1) in the current json context,
and if not found, (2) as a file.

Note: Reading files is disabled in this mruby gem.

### Escape First Compare

This extension automatically escapes comparisons appears as
first characters.
