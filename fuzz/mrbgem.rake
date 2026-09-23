MRuby::Gem::Specification.new('mruby-mustache-fuzzer') do |spec|
  spec.license = 'Apache-2'
  spec.author  = 'Hendrik'
  spec.summary = 'libFuzzer harnesses for mruby-mustache'

  spec.bins = %w[mruby-mustache-fuzzer mustache-core-fuzzer]

  spec.add_dependency 'mruby-mustache'

  fuzz_flags = %w[-fsanitize=fuzzer]

  spec.cxx.include_paths << File.expand_path('../include', spec.dir)
  spec.cxx.flags << '-std=c++20' << fuzz_flags
  spec.linker.flags << fuzz_flags

  spec.build.gems['mruby-mustache'].cxx.flags << fuzz_flags
end
