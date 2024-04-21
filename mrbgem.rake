MRuby::Gem::Specification.new('mruby-mustache') do |spec|
  spec.license = 'Apache-2'
  spec.author  = 'Hendrik'
  spec.summary = 'mustache templates for mruby'

  json_implementation = nil
  if spec.search_package('libcjson')
    json_implementation = 'cjson'
    spec.cc.defines << 'HAVE_CJSON'
  elsif spec.search_package('jansson')
    json_implementation = 'jansson'
    spec.cc.defines << 'HAVE_JANSSON'
  elsif spec.search_package('json-c')
    json_implementation = 'json-c'
    spec.cc.defines << 'HAVE_JSON_C'
  else
    raise "no mustach compatible JSON parser found, please install cJSON, jansson or json-c with development headers"
  end

  if spec.build.toolchains.include? 'visualcpp'
    spec.cc.defines << 'NO_OPEN_MEMSTREAM'
  end

  spec.cc.defines << 'MUSTACH_SAFE' << '_GNU_SOURCE'

  mustach_src = "#{spec.dir}/deps/mustach"
  spec.cc.include_paths << "#{mustach_src}"
  source_files = %W(
    #{mustach_src}/mustach.c
    #{mustach_src}/mustach-wrap.c
    #{mustach_src}/mustach-#{json_implementation}.c
  )
  spec.objs += source_files.map { |f| f.relative_path_from(dir).pathmap("#{build_dir}/%X#{spec.exts.object}" ) }
end
