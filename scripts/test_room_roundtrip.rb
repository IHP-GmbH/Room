repo = ENV["COMMONDB_ROOT"]
repo = File.expand_path("..", __dir__) if repo.nil? || repo.empty?

src = ENV["CORE_PATH"] || File.join(repo, "examples/gds_to_room/output/sample.room")
out = ENV["CORE_OUT"] || File.join(repo, "examples/gds_to_room/output/sample_roundtrip.room")

ly = RBA::Layout.new
ly.read(src)
puts "read cells=#{ly.cells}"

opt = RBA::SaveLayoutOptions.new
opt.format = "CORE"
ly.write(out, opt)
puts "wrote #{out}"

ly2 = RBA::Layout.new
ly2.read(out)
puts "reload cells=#{ly2.cells} top=#{ly2.top_cell.name}"
puts "OK"
