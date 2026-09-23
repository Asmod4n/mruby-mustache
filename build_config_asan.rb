MRuby::Build.new('asan') do |conf|
  conf.toolchain :clang
  conf.enable_debug
  conf.enable_sanitizer 'address,undefined,leak'
  conf.cc.flags << '-Og' << '-g' << '-fno-omit-frame-pointer' << '-march=x86-64-v3'
  conf.cxx.flags << '-Og' << '-g' << '-fno-omit-frame-pointer' << '-march=x86-64-v3'
  conf.enable_test
  conf.gem core: 'mruby-bin-mrbc'
  conf.gem File.expand_path(File.dirname(__FILE__))
end
