MRuby::Gem::Specification.new('mruby-mustache') do |spec|
  spec.license = 'Apache-2'
  spec.author  = 'Hendrik'
  spec.summary = 'Mustache templates for mruby'
  spec.add_dependency 'mruby-c-ext-helpers', github: 'Asmod4n/mruby-c-ext-helpers'
  spec.cxx.include_paths << "#{spec.dir}/include"
  # A dependent that renders templates itself needs this face too, not
  # only the mruby binding.
  spec.export_include_paths << "#{spec.dir}/include"
  # A build that already asks for C++20 or later keeps its -std: the
  # last -std on the line wins, and a later one here would take away
  # what the build chose, -freflection's C++26 among it.
  cxx20_or_later = spec.cxx.flags.flatten.any? do |flag|
    version = flag.to_s[%r{\A[-/]std[:=](?:c|gnu)\+\+(\w+)\z}, 1]
    version == 'latest' || version.to_s.match?(/\A2[0-9a-z]\z/)
  end
  spec.cxx.flags << (spec.for_windows? ? '/std:c++20' : '-std=c++20') unless cxx20_or_later
  %w[mruby-array-ext mruby-enum-ext mruby-hash-ext mruby-object-ext mruby-range-ext mruby-string-ext].each do |gem|
    spec.add_test_dependency gem
  end
end
