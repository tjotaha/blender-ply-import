/*
TODO:
- correctly ignore polygons with n > 4
- we assume property order in the file is always x,y,z. Need good way to handle other orders
- double-check that we are really ignoring faces with >4 vertices. 
  It seems indices of such a face *are* added to the index list...
- need to handle faces with more than 4 vertices
- add parameter to specify if returned vertex color array is
  blender-style (color per vertex per face) or plain per-vertex
- provide option to return 2-dim arrays, e.g. shape (V,3) vertices instead 
  of 1-dim V*3
*/

#define NPY_NO_DEPRECATED_API NPY_1_9_API_VERSION

#include <Python.h>
#include <numpy/arrayobject.h>
#include <rply.h>
#include <cstdio>
#include <cassert>

#ifdef _MSC_VER
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#else
#include <cstdint>
#endif

//#define DEBUG

//
// Custom object type to handle correct deallocation
// After http://blog.enthought.com/general/numpy-arrays-with-pre-allocated-memory/
//

typedef struct
{
    PyObject_HEAD
    void *memory;
#ifdef DEBUG
    const char *name;
#endif
}
_MyDeallocObject;

static void
_mydealloc_dealloc(_MyDeallocObject *self)
{
#ifdef DEBUG
    fprintf(stderr, "_mydealloc_dealloc() on '%s'\n", self->name);
    if (self->memory == NULL)
        fprintf(stderr, "self->memory == NULL!\n");
#endif
    
    free(self->memory);

#ifdef DEBUG
    fprintf(stderr, "Calling python type free()\n");
#endif    
    
#if PY_MAJOR_VERSION == 2
    self->ob_type->tp_free((PyObject*)self);
#elif PY_MAJOR_VERSION == 3
    Py_TYPE(self)->tp_free((PyObject*)self);
#endif
}

static PyTypeObject _MyDeallocType =
{
#if PY_MAJOR_VERSION == 2
    PyObject_HEAD_INIT(NULL)
    0,                          /*ob_size*/
#elif PY_MAJOR_VERSION == 3
    PyVarObject_HEAD_INIT(NULL, 0)
#endif
    "mydeallocator",            /*tp_name*/
    sizeof(_MyDeallocObject),   /*tp_basicsize*/
    0,                          /*tp_itemsize*/
    (destructor)_mydealloc_dealloc,         /*tp_dealloc*/
    0,                          /*tp_print*/
    0,                          /*tp_getattr*/
    0,                          /*tp_setattr*/
    0,                          /*tp_compare*/
    0,                          /*tp_repr*/
    0,                          /*tp_as_number*/
    0,                          /*tp_as_sequence*/
    0,                          /*tp_as_mapping*/
    0,                          /*tp_hash */
    0,                          /*tp_call*/
    0,                          /*tp_str*/
    0,                          /*tp_getattro*/
    0,                          /*tp_setattro*/
    0,                          /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,             /*tp_flags*/
    "Internal deallocator object",  /* tp_doc */
};

static void 
_set_base_object(PyArrayObject *arrobj, void *memory, const char *name)
{
    // XXX check for NULL in return of PyObject_New()
    PyObject *newobj = (PyObject*)PyObject_New(_MyDeallocObject, &_MyDeallocType);
    
    ((_MyDeallocObject *)newobj)->memory = memory;
#ifdef DEBUG
    ((_MyDeallocObject *)newobj)->name = strdup(name); 
#endif
    
#if NPY_API_VERSION >= 0x00000007
    PyArray_SetBaseObject(arrobj, newobj);
#else
    PyArray_BASE(arrobj) = newobj;
#endif
}

//
// rply stuff (arrays and callbacks)
//
// We don't use std::vector's for this as the deallocation will be
// done from _MyDeallocObject. It might be possible to place a 
// std::vector in _MyDeallocObject, but for now we're using malloc()
// and friends.
//

static float    *vertices = NULL;
static int      next_vertex_element_offset;

static uint32_t *faces = NULL;          // XXX vertex_indices
static uint32_t *loop_start = NULL;
static uint32_t *loop_total = NULL;
static int      face_indices_size;
static int      next_face_offset;
static int      next_face_element_offset;
static int      num_triangles, num_quads;

static float    *vertex_normals = NULL;
static int      next_vertex_normal_element_offset;

static float    *vertex_colors = NULL;
static int      next_vertex_color_element_offset;
static float    vertex_color_scale_factor;

static float    *vertex_texcoords = NULL;
static int      next_vertex_texcoord_element_offset;

// Vertex callbacks

static int
vertex_cb(p_ply_argument argument)
{
    vertices[next_vertex_element_offset] = ply_get_argument_value(argument);
    next_vertex_element_offset++;

    return 1;
}

static int
vertex_color_cb(p_ply_argument argument)
{
    vertex_colors[next_vertex_color_element_offset] = ply_get_argument_value(argument) * vertex_color_scale_factor;
    next_vertex_color_element_offset++;

    return 1;
}

static int
vertex_normal_cb(p_ply_argument argument)
{
    vertex_normals[next_vertex_normal_element_offset] = ply_get_argument_value(argument);
    next_vertex_normal_element_offset++;

    return 1;
}

static int
vertex_texcoord_cb(p_ply_argument argument)
{
    vertex_texcoords[next_vertex_texcoord_element_offset] = ply_get_argument_value(argument);
    next_vertex_texcoord_element_offset++;

    return 1;
}

// Face callback

static int
face_cb(p_ply_argument argument)
{
    long    length, value_index;
    int     vertex_index;

    ply_get_argument_property(argument, NULL, &length, &value_index);

    if (value_index == -1)
    {
        // First value of a list property, the one that gives the 
        // number of entries, i.e. start of new face
        loop_start[next_face_offset] = next_face_element_offset;
        loop_total[next_face_offset] = length;
        next_face_offset++;
        
        return 1;
    }
    
    if (next_face_element_offset == face_indices_size)
    {
        face_indices_size = int(face_indices_size * 1.1);
        faces = (uint32_t*) realloc(faces, face_indices_size*sizeof(uint32_t));
        // XXX check faces != NULL
    }
    
    vertex_index = ply_get_argument_value(argument);
    faces[next_face_element_offset++] = vertex_index;

    return 1;
}

// Main Python function

static PyObject*
readply(PyObject* self, PyObject* args, PyObject *kwds)
{
    char    *fname;
    int     blender_face_indices = 1;
    int     blender_vertex_colors_per_face = 1;
    
    static char *kwlist[] = {"plyfile", "blender_face_indices", "blender_vertex_colors_per_face", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|ii", kwlist, &fname, &blender_face_indices, &blender_vertex_colors_per_face))
        return NULL;
    
    // Open PLY file

    p_ply ply = ply_open(fname, NULL, 0, NULL);
    if (!ply)
    {
        char s[1024];
        sprintf(s, "Could not open PLY file %s", fname);
        PyErr_SetString(PyExc_IOError, s);
        return NULL;
    }

    if (!ply_read_header(ply))
    {
        PyErr_SetString(PyExc_IOError, "Could not read PLY header");
        ply_close(ply);
        return NULL;
    }

    // Check elements

    p_ply_element   vertex_element=NULL, face_element=NULL;
    const char      *name;

    p_ply_element element = ply_get_next_element(ply, NULL);
    while (element)
    {
        ply_get_element_info(element, &name, NULL);

        if (strcmp(name, "vertex") == 0)
            vertex_element = element;
        else if (strcmp(name, "face") == 0)
            face_element = element;

        element = ply_get_next_element(ply, element);
    }

    // XXX turn into actual checks
    assert(vertex_element && "Don't have a vertex element");
    assert(face_element && "Don't have a face element");

    // Set vertex and face property callbacks
    
    long nvertices, nfaces;

    nvertices = ply_set_read_cb(ply, "vertex", "x", vertex_cb, NULL, 0);
    ply_set_read_cb(ply, "vertex", "y", vertex_cb, NULL, 0);
    ply_set_read_cb(ply, "vertex", "z", vertex_cb, NULL, 1);

    nfaces = ply_set_read_cb(ply, "face", "vertex_indices", face_cb, NULL, 0);

    //printf("%ld vertices\n%ld faces\n", nvertices, nfaces);

    // Set optional per-vertex callbacks

    bool            have_vertex_colors = false;
    bool            have_vertex_normals = false;
    bool            have_vertex_texcoords = false;      // Either s,t or u,v sets will be used, but not both

    p_ply_property  prop;
    e_ply_type      ptype, plength_type, pvalue_type;
    
    // XXX check ply_set_read_cb() return values below

    prop = ply_get_next_property(vertex_element, NULL);
    while (prop)
    {
        ply_get_property_info(prop, &name, &ptype, &plength_type, &pvalue_type);

        //printf("property '%s'\n", name);

        if (strcmp(name, "red") == 0)
        {
            // Assumes green and blue properties are also available
            // XXX is there ever an alpha value?
            have_vertex_colors = true;

            if (ptype == PLY_UCHAR)
                vertex_color_scale_factor = 1.0f / 255;
            else if (ptype == PLY_FLOAT)
                vertex_color_scale_factor = 1.0f;
            else
                fprintf(stderr, "Warning: vertex color value type is %d, don't know how to handle!\n", ptype);

            ply_set_read_cb(ply, "vertex", "red", vertex_color_cb, NULL, 0);
            ply_set_read_cb(ply, "vertex", "green", vertex_color_cb, NULL, 0);
            ply_set_read_cb(ply, "vertex", "blue", vertex_color_cb, NULL, 1);
        }
        else if (strcmp(name, "nx") == 0)
        {
            // Assumes ny and nz properties are also available
            have_vertex_normals = true;

            ply_set_read_cb(ply, "vertex", "nx", vertex_normal_cb, NULL, 0);
            ply_set_read_cb(ply, "vertex", "ny", vertex_normal_cb, NULL, 0);
            ply_set_read_cb(ply, "vertex", "nz", vertex_normal_cb, NULL, 1);
        }
        else if (strcmp(name, "s") == 0 && !have_vertex_texcoords)
        {
            // Assumes t property is also available
            have_vertex_texcoords = true;

            ply_set_read_cb(ply, "vertex", "s", vertex_texcoord_cb, NULL, 0);
            ply_set_read_cb(ply, "vertex", "t", vertex_texcoord_cb, NULL, 1);
        }
        else if (strcmp(name, "u") == 0 && !have_vertex_texcoords)
        {
            // Assumes v property is also available
            have_vertex_texcoords = true;

            ply_set_read_cb(ply, "vertex", "u", vertex_texcoord_cb, NULL, 0);
            ply_set_read_cb(ply, "vertex", "v", vertex_texcoord_cb, NULL, 1);
        }

        prop = ply_get_next_property(vertex_element, prop);
    }

    // Allocate memory and initialize some values

    vertices = (float*) malloc(sizeof(float)*nvertices*3);
    next_vertex_element_offset = 0;

    // As we don't know the number of indices in advance we assume
    // quads. For a pure-triangle mesh this will overallocate by 1/4th,
    // but for a mesh with general n-gons we might have to reallocate
    // later on.
    
    next_face_offset = 0;
    next_face_element_offset = 0;
    
    face_indices_size = nfaces*4;
    faces = (uint32_t*) malloc(sizeof(uint32_t)*face_indices_size);
    
    loop_start = (uint32_t*) malloc(sizeof(uint32_t)*nfaces);
    loop_total = (uint32_t*) malloc(sizeof(uint32_t)*nfaces);

    if (have_vertex_normals)
    {
        vertex_normals = (float*) malloc(sizeof(float)*nvertices*3);
        next_vertex_normal_element_offset = 0;
    }

    if (have_vertex_colors)
    {
        vertex_colors = (float*) malloc(sizeof(float)*nvertices*3);
        next_vertex_color_element_offset = 0;
    }

    if (have_vertex_texcoords)
    {
        vertex_texcoords = (float*) malloc(sizeof(float)*nvertices*2);
        next_vertex_texcoord_element_offset = 0;
    }

    // Let rply process the file using the callbacks we set above

    num_triangles = num_quads = 0;

    if (!ply_read(ply))
    {
        // Failed!
        
        PyErr_SetString(PyExc_IOError, "Could not read PLY data");

        ply_close(ply);

        free(vertices);
        free(faces);

        if (have_vertex_normals)
            free(vertex_normals);
        if (have_vertex_colors)
            free(vertex_colors);
        if (have_vertex_texcoords)
            free(vertex_texcoords);

        return NULL;
    }

    //printf("%d triangles, %d quads\n", num_triangles, num_quads);

    // Clean up PLY reader

    ply_close(ply);

    //
    // Create return value
    //
    
    PyObject    *result = PyDict_New();
    

#if PY_MAJOR_VERSION == 2    
    PyDict_SetItemString(result, "num_vertices", PyInt_FromLong(nvertices));
    PyDict_SetItemString(result, "num_faces", PyInt_FromLong(nfaces));        
#else
    PyDict_SetItemString(result, "num_vertices", PyLong_FromLong(nvertices));
    PyDict_SetItemString(result, "num_faces", PyLong_FromLong(nfaces));        
#endif
    
    // Vertices
    
    npy_intp np_vertices_dims[1] = { nvertices*3 };
    // XXX check for NULL in return of PyArray_SimpleNewFromData()
    PyObject *np_vertices = PyArray_SimpleNewFromData(1, np_vertices_dims, NPY_FLOAT, vertices);    
    _set_base_object((PyArrayObject*)np_vertices, vertices, "vertices");
    
    PyDict_SetItemString(result, "vertices", (PyObject*)np_vertices);    

    // Faces
    
    // Vertex indices
    npy_intp np_faces_dims[1] = { next_face_element_offset };
    PyObject *np_faces = PyArray_SimpleNewFromData(1, np_faces_dims, NPY_UINT32, faces);
    _set_base_object((PyArrayObject*)np_faces, faces, "faces");
    PyDict_SetItemString(result, "faces", np_faces);
    
    // Loop starts
    npy_intp np_loop_dims[1] = { nfaces };
    PyObject *np_loop_start = PyArray_SimpleNewFromData(1, np_loop_dims, NPY_UINT32, loop_start);
    _set_base_object((PyArrayObject*)np_loop_start, loop_start, "loop_start");    
    PyDict_SetItemString(result, "loop_start", np_loop_start);    

    // Loop lengths
    PyObject *np_loop_total = PyArray_SimpleNewFromData(1, np_loop_dims, NPY_UINT32, loop_total);
    _set_base_object((PyArrayObject*)np_loop_total, loop_total, "loop_total");    
    PyDict_SetItemString(result, "loop_total", np_loop_total);    

    // Optional per-vertex arrays

    if (have_vertex_normals)
    {
        PyArrayObject *arr = (PyArrayObject*) PyArray_SimpleNewFromData(1, np_vertices_dims, NPY_FLOAT, vertex_normals);
        _set_base_object(arr, vertex_normals, "vertex_normals");
        PyObject *np_vnormals = (PyObject*) arr;
        
        PyDict_SetItemString(result, "normals", np_vnormals);
    }

    if (have_vertex_colors)
    {
        PyObject *np_vcolors;
        
        /*
        if (blender_vertex_colors_per_face)
        {
            // Convert list of per-vertex colors 
            // to per-vertex colors per face

            const int n = 4*((num_triangles*3)+(num_quads*4));

            float   *vcol2 = (float*) malloc (n*sizeof(float));
            float   *vcol2color = vcol2;
            float   *col;
            int     vi;

            // XXX only works for the blender 4-indices per face mode! not for the separate triangle and quad mode!
            for (int fi = 0; fi < nfaces; fi++)
            {
                const uint32_t *face = faces + 4*fi;

                for (int i = 0; i < 4; i++)
                {
                    vi = face[i];

                    if (i == 3 && vi == 0)
                    {
                        // Triangle
                        break;
                    }

                    col = vertex_colors + 3*vi;

                    vcol2color[0] = col[0];
                    vcol2color[1] = col[1];
                    vcol2color[2] = col[2];
                    vcol2color[3] = 1.0f;
                    vcol2color += 4;
                }
            }

            free(vertex_colors);
            
            npy_intp    dims[1] = { n };
            PyArrayObject *arr = (PyArrayObject*) PyArray_SimpleNewFromData(1, dims, NPY_FLOAT, vcol2);        
            _set_base_object(arr, vcol2, "vertex_colors");
            np_vcolors = (PyObject*) arr;            
        }
        else
        */
        {
            // Per-vertex colors
            PyArrayObject *arr = (PyArrayObject*) PyArray_SimpleNewFromData(1, np_vertices_dims, NPY_FLOAT, vertex_colors);
            _set_base_object(arr, vertex_colors, "vertex_colors");
            np_vcolors = (PyObject*) arr;                        
        }
        
        PyDict_SetItemString(result, "vertex_colors", np_vcolors);
    }

    if (have_vertex_texcoords)
    {
        npy_intp    np_vertex_texcoords_dims[1] = { nvertices*2 };

        PyArrayObject *arr = (PyArrayObject*) PyArray_SimpleNewFromData(1, np_vertex_texcoords_dims, NPY_FLOAT, vertex_texcoords);
        _set_base_object(arr, vertex_texcoords, "vertex_texcoords");
        PyObject *np_vtexcoords = (PyObject*) arr;    
        
        PyDict_SetItemString(result, "texcoords", np_vtexcoords);
    }

    // Return the stuff! 
    
    return result;
}

// Python module stuff

static char readply_func_doc[] = 
"readply(plyfile, blender_face_indices=True, blender_vertex_colors_per_face=True)\n\
\n\
Reads a 3D model from a PLY file.\n\
\n\
Returns a dictionary.\n\
\n\
The result will always contain keys \"num_vertices\" and \"num_faces\" holding\n\
integers. The values for other keys will be 1-dimensional Numpy arrays.\n\
\n\
Key \"vertices\" will contain vertex positions. Keys \"normals\", \"texcoords\"\n\
and \"vertex_colors\" are only present if their respective model element is present\n\
in the PLY file being read.\n\
\n\
If blender_face_indices is True (the default), there will be a key \"faces\" that holds\n\
an array following the Blender vertices_raw convention of using *four indices per face*,\n\
regardless of whether the face is a triangle or quad. Triangles can be recognized by their\n\
last index having a value of 0.\n\
\n\
If blender_face_indices is False, there will be keys \"triangles\" and \"quads\", holding\n\
3 indices per triangle and 4 indices per quad, respectively.\n\
\n\
If blender_vertex_colors_per_face is True (the default), the \"vertex_colors\" key holds\n\
a per-vertex color value for each face (provided that vertex colors are present in the PLY file).\n\
If the variable is False a single color per vertex is returned.\n\
\n\
BUGS:\n\
- Faces with more than 4 vertices are currently not supported and are not ignored correctly.\n\
- The vertex-colors per face return values (blender_vertex_colors_per_face=True)\n\
  are incorrect when blender_face_indices=False.\n\
";

static PyMethodDef ModuleMethods[] =
{    
     {"readply", (PyCFunction)readply, METH_VARARGS|METH_KEYWORDS, readply_func_doc},
     {NULL, NULL, 0, NULL}
};

/* module initialization */

#if PY_MAJOR_VERSION == 2
PyMODINIT_FUNC
initreadply(void)
{
    if (PyType_Ready(&_MyDeallocType) < 0)
        return;
    
    (void) Py_InitModule("readply", ModuleMethods);
    import_array();
}
#elif PY_MAJOR_VERSION == 3
static struct PyModuleDef module =
{
   PyModuleDef_HEAD_INIT,
   "readply",               /* name of module */
   NULL,                    /* module documentation, may be NULL */
   -1,                      /* size of per-interpreter state of the module,
                            or -1 if the module keeps state in global variables. */
   ModuleMethods
};

PyMODINIT_FUNC
PyInit_readply(void)
{
    PyObject *m;

    _MyDeallocType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&_MyDeallocType) < 0)
        return NULL;

    m = PyModule_Create(&module);
    if (m == NULL)
        return NULL;

    import_array();

    return m;
}
#endif
