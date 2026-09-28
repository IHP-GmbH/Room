repo = ENV["COMMONDB_ROOT"]
repo = File.expand_path("..", __dir__) if repo.nil? || repo.empty?

src = ENV["CORE_PATH"] || File.join(repo, "examples/gds_to_room/output/sample.room")
out = ENV["CORE_OUT"] || File.join(repo, "examples/gds_to_room/output/sample_props_roundtrip.room")

def shape_prop_sets(ly, ci)
  sets = []
  ly.each_layer do |li|
    ly.cell(ci).shapes(li).each(RBA::Shapes::SAll) do |shape|
      props = {}
      shape.properties.each { |key, value| props[key.to_s] = value.to_s }
      sets << props unless props.empty?
    end
  end
  sets.sort_by(&:to_s)
end

def cell_prop_hash(ly, ci)
  props = {}
  ly.cell(ci).properties.each { |key, value| props[key.to_s] = value.to_s }
  props
end

ly = RBA::Layout.new
ly.read(src)
ci = ly.top_cell.cell_index
before_cell = cell_prop_hash(ly, ci)
before_shapes = shape_prop_sets(ly, ci)

opt = RBA::SaveLayoutOptions.new
opt.format = "CORE"
ly.write(out, opt)

ly2 = RBA::Layout.new
ly2.read(out)
ci2 = ly2.top_cell.cell_index
after_cell = cell_prop_hash(ly2, ci2)
after_shapes = shape_prop_sets(ly2, ci2)

puts "cell props before=#{before_cell.inspect}"
puts "cell props after =#{after_cell.inspect}"
puts "shape prop sets before=#{before_shapes.inspect}"
puts "shape prop sets after =#{after_shapes.inspect}"

if before_cell != after_cell || before_shapes != after_shapes
  raise "property round-trip mismatch"
end

puts "OK"
