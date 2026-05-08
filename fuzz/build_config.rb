MRuby::Build.new('fuzz') do |conf|
  toolchain :clang
  conf.gembox 'default'
  conf.gem '/root/mruby-mustache'
  conf.cc.flags << ENV['CFLAGS'].to_s.split(' ')
  conf.linker.flags << ENV['LDFLAGS'].to_s.split(' ')
  conf.enable_debug
end
