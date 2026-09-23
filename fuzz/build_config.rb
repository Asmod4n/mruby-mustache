MRuby::Build.new do |conf|
  conf.toolchain :clang

  conf.enable_sanitizer 'address,undefined'

  conf.enable_debug
  conf.enable_bintest
  conf.cc.defines << 'MRB_GC_STRESS'
  conf.cxx.defines << 'MRB_GC_STRESS'
  conf.cc.flags << '-O1'
  conf.cxx.flags << '-O1'
  conf.gem File.expand_path('..', __dir__)
  conf.gem File.expand_path(__dir__)
end
