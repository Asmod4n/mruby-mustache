require 'fileutils'

# libFuzzer runs each harness for FUZZ_SECONDS, 300 by default, with
# FUZZ_JOBS workers. A worker that finds a crash, a leak or undefined
# behaviour writes SUMMARY: into its log and the input into findings/.
GEM_ROOT = File.expand_path('..', __dir__)
SECONDS  = (ENV['FUZZ_SECONDS'] || 300).to_i
JOBS     = (ENV['FUZZ_JOBS'] || 3).to_i

{ 'mruby-mustache-fuzzer' => 'corpus', 'mustache-core-fuzzer' => 'corpus_core' }.each do |bin, corpus|
  assert("#{bin}: no findings in #{SECONDS} seconds") do
    findings = File.join(GEM_ROOT, 'findings', bin)
    FileUtils.mkdir_p(findings)
    Dir[File.join(findings, 'fuzz-*.log')].each { |f| File.delete(f) }
    cmd = cmd_list(bin) + [
      File.join(GEM_ROOT, corpus),
      "-dict=#{File.join(GEM_ROOT, 'mustache.dict')}",
      "-jobs=#{JOBS}", "-workers=#{JOBS}",
      "-max_total_time=#{SECONDS}",
      "-artifact_prefix=#{findings}/",
      '-max_len=65536', '-use_value_profile=1', '-rss_limit_mb=4096'
    ]
    Dir.chdir(findings) { system(*cmd, out: File::NULL, err: File::NULL) }
    logs = Dir[File.join(findings, 'fuzz-*.log')]
    crashed = logs.select { |log| File.read(log).include?('SUMMARY:') }
    assert_false logs.empty?, "#{bin} wrote no log"
    assert_true crashed.empty?, "findings: #{crashed.join(', ')}"
  end
end
