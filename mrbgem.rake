MRuby::Gem::Specification.new('mruby-mustache') do |spec|
  spec.license = 'Apache-2'
  spec.author  = 'Hendrik'
  spec.summary = 'Mustache templates for mruby'
  spec.add_dependency 'mruby-c-ext-helpers', github: 'Asmod4n/mruby-c-ext-helpers'
  spec.cxx.include_paths << "#{spec.dir}/include"
  spec.cxx.flags << (spec.for_windows? ? '/std:c++20' : '-std=c++20')
  %w[mruby-array-ext mruby-enum-ext mruby-hash-ext mruby-object-ext mruby-range-ext mruby-string-ext].each do |gem|
    spec.add_test_dependency gem
  end
end
