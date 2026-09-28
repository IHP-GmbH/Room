repo = ENV["COMMONDB_ROOT"]
repo = File.expand_path("..", __dir__) if repo.nil? || repo.empty?

path = ENV["CORE_PATH"] || File.join(repo, "examples/gds_to_room/output/sample.room")
ly = RBA::Layout.new
ly.read(path)

total = 0
ly.each_cell do |ci|
  ly.each_layer do |li|
    total += ly.cell(ci).shapes(li).size
  end
end

puts "file=#{path}"
puts "cells=#{ly.cells}"
puts "shapes=#{total}"
