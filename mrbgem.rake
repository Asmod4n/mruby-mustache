MRuby::Gem::Specification.new('mruby-mustache') do |spec|
  spec.license = 'Apache-2'
  spec.author  = 'Hendrik'
  spec.summary = 'Mustache templates for mruby (native Hash/Array, no JSON layer)'

if spec.build.toolchains.include? 'visualcpp'
  spec.cc.defines << 'NO_OPEN_MEMSTREAM'
  spec.cc.defines << '_CRT_NONSTDC_NO_DEPRECATE'    # POSIX names like fdopen
  spec.cc.defines << '_CRT_SECURE_NO_WARNINGS'      # belt-and-braces
  spec.cc.defines << 'ssize_t=ptrdiff_t'
end
  spec.cc.defines << 'MUSTACH_SAFE' << '_GNU_SOURCE'

  mustach_src = "#{spec.dir}/deps/mustach"
  spec.cc.include_paths << mustach_src
  source_files = %W(
    #{mustach_src}/mustach.c
    #{mustach_src}/mustach-wrap.c
  )
  spec.objs += source_files.map { |f|
    f.relative_path_from(dir).pathmap("#{build_dir}/%X#{spec.exts.object}")
  }
end
