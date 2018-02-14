MRuby::Gem::Specification.new('mruby-getpass') do |spec|
  spec.license = 'Apache-2'
  spec.author  = 'Hendrik Beskow'
  spec.summary = "Read passwords from the command prompt"

  add_dependency 'mruby-error'
end
