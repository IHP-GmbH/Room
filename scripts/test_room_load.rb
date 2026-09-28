repo = ENV["COMMONDB_ROOT"]
repo = File.expand_path("..", __dir__) if repo.nil? || repo.empty?

path = ENV["CORE_PATH"] || File.join(repo, "examples/gds_to_room/output/sample.room")
ly = RBA::Layout.new
ly.read(path)
puts "file=#{path}"
puts "cells=#{ly.cells}"
puts "top=#{ly.top_cell.name}"
puts "layers=#{ly.layers}"
puts "shapes=#{ly.top_cell.shapes(0).size}" if ly.layers > 0
puts "OK"
