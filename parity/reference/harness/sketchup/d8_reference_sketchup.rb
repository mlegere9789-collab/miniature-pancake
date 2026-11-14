# Dino 8 reference-output harness for SketchUp (Ruby API).
# STATUS: UNTESTED against a real SketchUp installation. Written from the public SketchUp Ruby API docs.
# SAFETY: read-only with respect to the owner's work.
#  - REFUSES to run unless the active model is empty (use File > New first).
#  - All modelling is inside one operation that is aborted at the end (nothing is saved, Undo is clean).
#  - Writes only inside the output folder (safe_path). No network calls. No settings changes.
# RUN: Window > Ruby Console, then:   load '/full/path/to/d8_reference_sketchup.rb'
#      Output folder: ENV['D8_REF_OUT'] or a folder dialog, else ~/dino8_reference/sketchup_<timestamp>.
# EULA: counsel must confirm that scripted observation is permitted before this is run.
require 'json'
require 'fileutils'
require 'digest'
require 'time'

module D8Ref
  VERSION = '0.1.0'
  MM = 1.0.mm   # inches per millimetre (SketchUp length units are inches internally)
  IN3_TO_MM3 = 25.4**3

  def self.choose_out
    out = ENV['D8_REF_OUT']
    if out.nil? || out.empty?
      out = UI.select_directory(title: 'Choose the Dino 8 reference output folder') if UI.respond_to?(:select_directory)
    end
    if out.nil? || out.empty?
      out = File.join(Dir.home, 'dino8_reference', 'sketchup_' + Time.now.utc.strftime('%Y%m%dT%H%M%SZ'))
    end
    FileUtils.mkdir_p(out)
    File.realpath(out)
  end

  def self.safe_path(out, name)
    p = File.expand_path(File.join(out, name))
    raise "refusing to write outside the output folder: #{p}" unless p == out || p.start_with?(out + File::SEPARATOR)
    p
  end

  def self.write_json(out, name, obj)
    File.write(safe_path(out, name), JSON.pretty_generate(obj))
  end

  def self.mm3(v) v.nil? ? nil : v * IN3_TO_MM3 end
  def self.mm(v) v.nil? ? nil : v / MM end
  def self.bb(e)
    b = e.bounds
    { min: [mm(b.min.x), mm(b.min.y), mm(b.min.z)], max: [mm(b.max.x), mm(b.max.y), mm(b.max.z)] }
  end

  def self.stats(g)
    ents = g.respond_to?(:definition) ? g.definition.entities : g.entities
    {
      faces: ents.grep(Sketchup::Face).size, edges: ents.grep(Sketchup::Edge).size,
      volume_mm3: (g.respond_to?(:volume) ? mm3(g.volume) : nil),
      manifold: (g.respond_to?(:manifold?) ? g.manifold? : nil),
      surface_area_mm2: ents.grep(Sketchup::Face).sum { |f| f.area } / (MM * MM),
      bbox_mm: bb(g)
    }
  end

  # Build a solid box as a group (push/pull of a rectangle).
  def self.box(ents, x, y, z, ox = 0, oy = 0, oz = 0)
    g = ents.add_group
    f = g.entities.add_face([ox * MM, oy * MM, oz * MM], [(ox + x) * MM, oy * MM, oz * MM], [(ox + x) * MM, (oy + y) * MM, oz * MM], [ox * MM, (oy + y) * MM, oz * MM])
    f.reverse! if f.normal.z < 0
    f.pushpull(z * MM)
    g
  end

  def self.cylinder(ents, cx, cy, z0, r, h, segs = 96)
    g = ents.add_group
    c = g.entities.add_circle([cx * MM, cy * MM, z0 * MM], [0, 0, 1], r * MM, segs)
    f = g.entities.add_face(c)
    f.reverse! if f.normal.z < 0
    f.pushpull(h * MM)
    g
  end

  def self.scenarios
    [
      [:s01_box, lambda { |m, e, out| g = box(e, 100, 60, 40); stats(g).merge(expected_volume_mm3: 240000.0) }],
      [:s02_cylinder, lambda { |m, e, out| g = cylinder(e, 0, 0, 0, 15, 40, 96); stats(g).merge(expected_volume_true_circle: Math::PI * 225 * 40, expected_volume_polygon: 0.5 * 96 * 225 * Math.sin(2 * Math::PI / 96) * 40) }],
      [:s03_sphere_followme, lambda { |m, e, out|
        g = e.add_group; ge = g.entities
        path = ge.add_circle([0, 0, 0], [0, 0, 1], 25 * MM, 48)
        prof = ge.add_face(ge.add_circle([0, 0, 0], [0, 1, 0], 25 * MM, 48))
        prof.followme(path) rescue (return { unsupported: 'followme failed: ' + $!.message })
        stats(g).merge(expected_volume_true_sphere: 4.0 / 3 * Math::PI * 25**3)
      }],
      [:s04_fillet_one_edge, lambda { |m, e, out| { unsupported: 'no native fillet in the Ruby API or the standard tool set; recorded as a gap, not emulated' } }],
      [:s05_chamfer_one_edge, lambda { |m, e, out|
        g = box(e, 100, 60, 40); ge = g.entities
        v = ge.grep(Sketchup::Edge).find { |ed| ed.vertices.all? { |vx| vx.position.x.abs < 1e-6 && vx.position.y.abs < 1e-6 } }
        return { error: 'edge not found' } unless v
        # Emulation by cutting a prism with Solid Tools: subtract a triangular prism from the corner.
        prism = e.add_group; pf = prism.entities.add_face([-1 * MM, -1 * MM, -1 * MM], [5 * MM, -1 * MM, -1 * MM], [-1 * MM, 5 * MM, -1 * MM]) ; pf.reverse! if pf.normal.z < 0; pf.pushpull(42 * MM)
        r = g.subtract(prism)
        { note: 'chamfer emulated with Group#subtract of a prism (approximate; expected volume change -12.5*40 only if prism cut passes through the corner exactly)', result_is_group: !r.nil? }.merge(r ? stats(r) : {})
      }],
      [:s06_boolean_difference, lambda { |m, e, out|
        res = { expected_volume_if_box_minus_cylinder_mm3: 240000.0 - 0.5 * 96 * 100 * Math.sin(2 * Math::PI / 96) * 40,
                note: 'the API argument order is itself observed: both orders are recorded' }
        [[:a_subtract_b, true], [:b_subtract_a, false]].each do |tag, a_first|
          e.to_a.each { |x| x.erase! if x.valid? }
          a = box(e, 100, 60, 40); b = cylinder(e, 50, 30, -5, 10, 50, 96)
          r = a_first ? a.subtract(b) : b.subtract(a)
          res[tag] = r ? stats(r) : { error: 'subtract returned nil' }
        end
        res
      }],
      [:s07_boolean_union, lambda { |m, e, out| a = box(e, 100, 60, 40); b = cylinder(e, 100, 30, 0, 10, 60, 96); r = a.union(b); r ? stats(r) : { error: 'union returned nil' } }],
      [:s08_boolean_intersect, lambda { |m, e, out| a = box(e, 100, 60, 40); b = cylinder(e, 50, 30, -5, 10, 50, 96); r = a.intersect(b); r ? stats(r).merge(expected_volume_polygon: 0.5 * 96 * 100 * Math.sin(2 * Math::PI / 96) * 40) : { error: 'intersect returned nil' } }],
      [:s09_offset_curve_face, lambda { |m, e, out|
        g = e.add_group; f = g.entities.add_face([0, 0, 0], [100 * MM, 0, 0], [100 * MM, 60 * MM, 0], [0, 60 * MM, 0])
        return { unsupported: 'Face#offset is not in the Ruby API of this build' } unless f.respond_to?(:offset)
        r = f.offset(-5 * MM); { result: r.to_s }
      }],
      [:s10_circle_properties, lambda { |m, e, out| g = e.add_group; c = g.entities.add_circle([0, 0, 0], [0, 0, 1], 20 * MM, 96); f = g.entities.add_face(c); { segments: c.size, face_area_mm2: f.area / (MM * MM), expected_true_area: Math::PI * 400, perimeter_mm: mm(c.sum { |ed| ed.length }), expected_true_perimeter: 2 * Math::PI * 20 } }],
      [:s11_pushpull_profile, lambda { |m, e, out| g = e.add_group; f = g.entities.add_face([0, 0, 0], [50 * MM, 0, 0], [50 * MM, 30 * MM, 0], [0, 30 * MM, 0]); f.reverse! if f.normal.z < 0; f.pushpull(20 * MM); stats(g).merge(expected_volume_mm3: 30000.0) }],
      [:s12_followme_revolve, lambda { |m, e, out|
        g = e.add_group; ge = g.entities
        path = ge.add_circle([0, 0, 0], [0, 0, 1], 20 * MM, 64)
        prof = ge.add_face([20 * MM, 0, -5 * MM], [30 * MM, 0, -5 * MM], [30 * MM, 0, 5 * MM], [20 * MM, 0, 5 * MM])
        prof.followme(path)
        stats(g).merge(expected_volume_pappus_mm3: 2 * Math::PI * 25 * 100)
      }],
      [:s13_loft_unavailable, lambda { |m, e, out| { unsupported: 'no loft in the standard tool set; extension tools only' } }],
      [:s14_transform_rotate_scale, lambda { |m, e, out|
        g = box(e, 100, 60, 40); g.transform!(Geom::Transformation.rotation([0, 0, 0], [0, 0, 1], 45.degrees)); r = stats(g)
        s = box(e, 100, 60, 40, 300, 0, 0); s.transform!(Geom::Transformation.scaling(s.bounds.min, 2.0, 1.0, 0.5))
        { rotated: r, scaled: stats(s), expected_scaled_volume_mm3: 240000.0 }
      }],
      [:s15_linear_array, lambda { |m, e, out| gs = (0...5).map { |i| g = box(e, 100, 60, 40); g.transform!(Geom::Transformation.translation([120 * i * MM, 0, 0])); g }; bounds = gs.map(&:bounds).inject { |a, b| a.add(b); a }; { count: 5, union_bbox_mm: bb(Struct.new(:bounds).new(bounds)) , total_volume_mm3: gs.sum { |x| mm3(x.volume) } } }],
      [:s16_triangulated_mesh, lambda { |m, e, out| g = cylinder(e, 0, 0, 0, 15, 40, 48); pm = Geom::PolygonMesh.new; g.entities.grep(Sketchup::Face).each { |f| pm.add_polygon(f.vertices.map(&:position)) rescue nil }; { polygon_mesh_points: pm.points.size, polygons: pm.polygons.size, count_faces: g.entities.grep(Sketchup::Face).size } }],
      [:s17_intersect_faces, lambda { |m, e, out| g = e.add_group; a = g.entities.add_face([0, 0, 0], [100 * MM, 0, 0], [100 * MM, 100 * MM, 0], [0, 100 * MM, 0]); b = g.entities.add_face([50 * MM, 50 * MM, -10 * MM], [150 * MM, 50 * MM, -10 * MM], [150 * MM, 50 * MM, 10 * MM], [50 * MM, 50 * MM, 10 * MM]); g.entities.intersect_with(false, g.transformation, g.entities, g.transformation, true, [a, b]); { edges_after: g.entities.grep(Sketchup::Edge).size, faces_after: g.entities.grep(Sketchup::Face).size } }],
      [:s18_solid_trim_split_shell, lambda { |m, e, out| a = box(e, 100, 60, 40); b = box(e, 100, 60, 40, 50, 0, 0); r = a.outer_shell(b); r ? stats(r) : { error: 'outer_shell returned nil' } }],
      [:s19_measure_tools_api, lambda { |m, e, out| g = box(e, 100, 60, 40); f = g.entities.grep(Sketchup::Face).max_by(&:area); { largest_face_area_mm2: f.area / (MM * MM), expected: 6000.0, normal: f.normal.to_a, edge_lengths_mm: g.entities.grep(Sketchup::Edge).map { |ed| mm(ed.length) }.sort.uniq } }],
      [:s20_export_formats, lambda { |m, e, out|
        a = box(e, 100, 60, 40); b = cylinder(e, 50, 30, -5, 10, 50, 96); r = a.subtract(b) || a
        res = {}
        %w[dae kmz obj stl dxf dwg 3ds fbx ifc wrl xsi].each do |ext|
          path = safe_path(out, "scenario20_result.#{ext}")
          begin
            ok = m.export(path, selectionset_only: false)
            ex = File.exist?(path)
            res[ext] = { ok: ok, exists: ex, bytes: ex ? File.size(path) : 0, sha256: ex ? Digest::SHA256.file(path).hexdigest : nil }
          rescue => err
            res[ext] = { ok: false, error: err.message }
          end
        end
        res
      }]
    ]
  end

  def self.enumerate_api
    r = { sketchup_constants: (Sketchup.constants.map(&:to_s).sort rescue []), geom_constants: (Geom.constants.map(&:to_s).sort rescue []) }
    classes = %w[Sketchup::Model Sketchup::Entities Sketchup::Face Sketchup::Edge Sketchup::Group Sketchup::ComponentInstance Sketchup::ComponentDefinition Sketchup::Material Sketchup::Layer Sketchup::Camera Sketchup::RenderingOptions Sketchup::Page Sketchup::Selection Geom::Transformation Geom::PolygonMesh]
    r[:class_methods] = {}
    classes.each do |cn|
      begin
        k = Object.const_get(cn)
        r[:class_methods][cn] = { instance: k.public_instance_methods(false).map(&:to_s).sort, singleton: k.singleton_methods(false).map(&:to_s).sort }
      rescue StandardError => err
        r[:class_methods][cn] = { error: err.message }
      end
    end
    r[:extensions] = (Sketchup.extensions.map { |x| { name: x.name, version: x.version, loaded: x.loaded?, creator: x.creator } } rescue [])
    r[:note] = 'Menu items and tools are not enumerable from the Ruby API; the owner records the toolbar and menu tree manually (see README).'
    r
  end

  def self.run
    out = choose_out
    model = Sketchup.active_model
    if model.entities.length != 0 || model.definitions.count { |d| !d.internal? && !d.image? } > 0
      UI.messagebox('Dino 8 reference harness: the active model is not empty. Use File > New and run again. Nothing was done.')
      return
    end
    t0 = Time.now
    meas = {}
    model.start_operation('Dino 8 reference run', true)
    begin
      scenarios.each do |id, fn|
        t = Time.now
        # each scenario starts from a clean slate inside the same operation
        model.entities.to_a.each { |x| x.erase! if x.valid? }
        begin
          v = fn.call(model, model.entities, out)
          meas[id] = { status: 'ok', values: v, seconds: (Time.now - t).round(3) }
        rescue Exception => err
          meas[id] = { status: 'error', error: err.message, trace: err.backtrace.first(8), seconds: (Time.now - t).round(3) }
        end
      end
    ensure
      model.abort_operation
    end
    write_json(out, 'measurements.json', meas)
    write_json(out, 'commands.json', enumerate_api)
    man = {
      harness: 'dino8-reference-sketchup', harness_version: VERSION, tested_against_real_application: false,
      utc: Time.now.utc.iso8601, program: 'SketchUp', sketchup_version: Sketchup.version, sketchup_version_number: Sketchup.version_number,
      pro: (Sketchup.is_pro? rescue nil), platform: (Sketchup.platform rescue nil), os_language: (Sketchup.os_language rescue nil),
      ruby: RUBY_VERSION, ruby_platform: RUBY_PLATFORM, output_dir: out, units: 'model built in millimetres; internal inches converted',
      model_units_option: (model.options['UnitsOptions'].to_a.map { |k, v| [k, v] }.to_h rescue nil),
      scenario_count: meas.size, scenarios_ok: meas.values.count { |v| v[:status] == 'ok' }, seconds: (Time.now - t0).round(2)
    }
    man[:files] = Dir.children(out).sort
    write_json(out, 'manifest.json', man)
    UI.messagebox("Dino 8 reference run finished: #{man[:scenarios_ok]}/#{meas.size} scenarios ok.\nOutput: #{out}")
  end
end

D8Ref.run
