#!/usr/bin/env python
# Blender 2.8x version
#
# blender -P mesh_readply.py -- file.ply
#
import sys, os, time
try:
    import bpy, bmesh
    in_blender = True
except ImportError:
    print("Script being called outside of Blender")
    in_blender = False


sys.path.insert(0, '.')
try:
    import readply
except ImportError:
    scriptdir = os.path.split(os.path.abspath(__file__))[0]    
    sys.path.insert(0, scriptdir)
    import readply
    
def testing_function():
    print("testing_function called")
    


def print_vert_attr(p):
    for i in range(p['num_vertices']):
        print("vertex %d" % i)
        print('vertex coord: %d, %d, %d' % (p['vertices'][i*3], p['vertices'][i*3+1], p['vertices'][i*3+2]))
        # print('rgb: %d, %d, %d' % (p['vertex_colors'][i*3] * 255, p['vertex_colors'][i*3+1] * 255, p['vertex_colors'][i*3+2] * 255))
        # try:
        #     print('density, temperature, pressure: %.1f, %.1f, %.1f' % (p['density'][i], p['temperature'][i], p['pressure'][i]))
        # except:
        #     print('file doesn\'t have density, temperature, or pressure parameters')
        
        print("\n")

def print_vertex_colors(p, num: int = -1):
    i = 0
    while i < len(p['vertex_colors']):
        print("rgba: p['vertex_colors'] vertex %d: %d, %d, %d, %d\n" % (p['faces'][i//4],
                                                                              p['vertex_colors'][i] * 255,
                                                                              p['vertex_colors'][i+1] * 255,
                                                                              p['vertex_colors'][i+2] * 255,
                                                                              p['vertex_colors'][i+3]))
        i += 4
        
def print_col_attribute(attr, num: int = -1):
    i = 0
    
    if num > 0:
        while i < num:
            print("[%d] rgba: %f %f %f %f" % (i, attr[i].color[0], attr[i].color[1], attr[i].color[2], attr[i].color[3]))
            i +=1
    else:
        for i in range(len(attr)):
            print("[%d] rgba: %f %f %f %f" % (i, attr[i].color[0], attr[i].color[1], attr[i].color[2], attr[i].color[3]))
            

def print_loop_start(p):
    for i in range(len(p['loop_start'])):
        print("loop_start[%d]: %d\n" % (i, p['loop_start'][i]))

def print_loop_length(p):
    for i in range(len(p['loop_length'])):
        print("loop_length[%d]: %d\n" % (i, p['loop_length'][i]))

def print_faces(p):
    for i in range(len(p['faces'])):
        print("faces[%d]: %d\n" % (i, p['faces'][i]))

def print_property(p, prop: str, num: int = -1, first_last: bool = False):
    
    if num > 0:
        for i in range(num):
            print("p[%s][%d]: %.5f" % (prop, i, p[prop][i]))

        prop_len = len(p[prop])

        if first_last and prop_len > num*2:
            for i in range(prop_len-num, prop_len):
                print("p[%s][%d]: %.5f" % (prop, i, p[prop][i]))
    else:
        print("p[%s]: " % (prop))
        print(p[prop])
# def print_property(p, prop: str, num: int):
#     print_property(p, prop, num, False)

# def print_property(p, prop:str):
#     print_property(p, prop, len(p[prop]))

def unlerp(min, max, val):
    range = max - min
    diff = val - min
    return diff/range


print('Using readply module: %s' % readply.__file__)

# # Option parsing
#
args = []
try:
    idx = sys.argv.index('--')
    args = sys.argv[idx+1:]
except ValueError as e:
    args = []

# print("current directory: %r\n" % os.getcwd())
# fname = 'colored_monkey.ply'
# path = 'C:\\Users\\vjvalve\\Documents\\blender-ply-import\\test\\'
#fname = 'small_wavelet_1_extra_properties.ply'
#path = 'C:\\Users\\vjvalve\\Documents\\09326\\blender-ply-import\\'

#fname = 'wavelet_for_import_with_added_properties.ply'
#path = 'C:\\Users\\vjvalve\\Documents\\09326\\ParaViewImportTesting\\ExtractorOutput\\'
#fpath = fpath.replace("\\", "\\\\")

#fname = 'xyslice_minz_1_0028_0_0_with_added_properties.ply'
fname= 'sparc-wall_with_added_properties.ply'
#fname='foo_additional_prop.ply'
#path = 'C:\\Users\\vjvalve\\Documents\\09326\\'
path = 'C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\ExtractorOutput\\'

fpath = path+fname

if len(args) > 0:
     fname = args[0]

print("file %s exists: %r\n" % (fname, os.path.exists(fpath)))

# Start parsing the PLY file

t0 = time.time()

p = readply.readply(fpath)
# print(p.keys())

t1 = time.time()
print(p.keys())
#print('PLY file read by readply() in %.3fs' % (t1-t0))
print('%d vertices, %d faces' % (p['num_vertices'], p['loop_start'].shape[0]))
# print('vertices: {}\n'.format(p['vertices']))
# print('vertex_colors.shape: {0}\n'.format(p['vertex_colors'].shape))
#print("vertex_colors length: %d" % (len(p['vertex_colors'])))
# print_vertex_colors(p)

# print_loop_start(p)
#
# print_loop_length(p)

#print_faces(p)

# print('density: {}\n'.format(p['density']))

#print_vert_attr(p)

normal_props = ["num_vertices", "num_faces", "vertices", "faces", "loop_start", "loop_length", "vertex_colors"]

# for i in range(10):
for key, value in p.items():
    if key not in normal_props:
        print("prop: " + key )
#        print_property(p, key, 10, True)
        # print(p[key])


if in_blender:
    # Create a mesh + object using the vertex and face data in the numpy arrays

    mesh = bpy.data.meshes.new(name='imported mesh')

    mesh.vertices.add(p['num_vertices'])
    mesh.vertices.foreach_set('co', p['vertices'])

    mesh.loops.add(len(p['faces']))
    mesh.loops.foreach_set('vertex_index', p['faces'])

    mesh.polygons.add(p['num_faces'])
    mesh.polygons.foreach_set('loop_start', p['loop_start'])
    mesh.polygons.foreach_set('loop_total', p['loop_length'])
    
    
    for key, value in p.items():
        
        if key not in normal_props:
            
            print("prop: " + key)
#            print(p[key])
            prop_layer = mesh.vertex_layers_float.new(name=key)
            prop_layer.data.foreach_set('value', p[key])
#            
            # test_co_w = [1.0, 1.0, 1.0, 1.0]
            # test_co_b = [0.0, 0.0, 0.0, 1.0]
            
            co_layer_prop = []
            max_d = max(p[key])
            min_d = min(p[key])
            # prop_threshold = (max_d+min_d) / 2
            
            # prop_avg = sum(p[key]) / len(p[key])
            
#            print("max_d: %f" % (max_d))
#            print("min_d: %f" % (min_d))
            # print("prop_threshold: %f" % (prop_threshold))
            # print("prop_avg: %f" %(prop_avg))
            
            i = 0
            j = 0
            k = 0
            
            if len(p[key]) < 10:
                print("key: " + key)
                print(*p[key])
            for vertex in p['faces']:
                
                prop_val = prop_layer.data[vertex].value
                
                unlerp_val = unlerp(min=min_d, max=max_d, val=prop_val)
                
                
                unlerp_color = [unlerp_val, unlerp_val, unlerp_val, 1.0]
                
                co_layer_prop += unlerp_color
                
                
#                print("vertex: %d" % vertex)
#                
#                print("unlerp_val: %f" % unlerp_val)
#                
#                print(unlerp_color)
                
#                print(co_layer_prop)
                
                    

            
#            print("co_layer_prop: %d" % (len(co_layer_prop)))
#            print(co_layer_prop)
            
#            dcol_layer = mesh.vertex_colors.new()
#            dcol_data = dcol_layer.data
#            dcol_data.foreach_set('color', co_layer_prop)
#            pcol_layer = mesh.color_attributes.new(name=key, type="BYTE_COLOR", domain="POINT")
            pcol_layer = mesh.color_attributes.new(name=key, type="BYTE_COLOR", domain="CORNER")
            pcol_data = pcol_layer.data
            pcol_data.foreach_set("color", co_layer_prop)
            
#            seq_ret = pcol
#            print_col_attribute(pcol_data)
        
        

    if 'vertex_normals' in p:
        mesh.vertices.foreach_set('normal', p['vertex_normals'])

    if 'vertex_colors' in p:
        vcol_layer = mesh.vertex_colors.new()
        vcol_data = vcol_layer.data
        vcol_data.foreach_set('color', p['vertex_colors'])

    if 'texture_coordinates' in p:
        uv_layer = mesh.uv_layers.new(name='default')
        uv_layer.data.foreach_set('uv', p['texture_coordinates'])
        



        

    mesh.validate()
    mesh.update()

    # Create object to link to mesh

    obj = bpy.data.objects.new('imported object', mesh)

    # Add object to the scene
    scene = bpy.context.scene
    scene.collection.children[0].objects.link(obj)

    # Select the new object and make it active
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj

    t2 = time.time()
    print('Blender object+mesh created in %.3fs' % (t2 - t1))

    del p

    print('Total import time %.3fs' % (t2 - t0))



