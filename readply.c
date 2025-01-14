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
#include <stdio.h>
#include <assert.h>

#ifdef _MSC_VER
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#else
#include <stdint.h>
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

/* 
// vjvalve comment:
    All these following values and variables are default to the original project file
    this means that several of the index elements and flags are hard coded and don't
    allow for much dynamicity. Luckily, most of these values frequently show up in a
    .ply and because of their typing require unique executions
*/ 
// Vertex coordinates (x,y,z) for all vertices in flat 1d array
static float    *vertices = NULL;
// Pointer index for location in vertex array
static int      next_vertex_element_index;

// list of vertices in forming a face (currently expecting triangulated faces)
static uint32_t *faces = NULL;          // XXX vertex_indices
static uint32_t *loop_start = NULL;
static uint32_t *loop_total = NULL;     // XXX rename loop_length
static int      face_indices_size;
static int      next_face_offset;
static int      next_face_element_index;
static int      num_triangles, num_quads;

static float    *vertex_normals = NULL;
static int      next_vertex_normal_element_index;

static float    *vertex_colors = NULL;
static int      next_vertex_color_element_index;
static float    vertex_color_scale_factor;

static float    *vertex_texcoords = NULL;
static int      next_vertex_texcoord_element_index;

// Vertex callbacks

static int
vertex_cb(p_ply_argument argument)
{
    vertices[next_vertex_element_index] = ply_get_argument_value(argument);
    next_vertex_element_index++;

    return 1;
}

static int
vertex_color_cb(p_ply_argument argument)
{
    // printf("index:%d\t vertex_color argument value: %lf\n", next_vertex_color_element_index, ply_get_argument_value(argument));
    // printf("vertex_scale_factor: %lf\n", vertex_color_scale_factor);
    vertex_colors[next_vertex_color_element_index] = ply_get_argument_value(argument) * vertex_color_scale_factor;
    next_vertex_color_element_index++;

    return 1;
}

static int
vertex_normal_cb(p_ply_argument argument)
{
    vertex_normals[next_vertex_normal_element_index] = ply_get_argument_value(argument);
    next_vertex_normal_element_index++;

    return 1;
}

static int
vertex_texcoord_cb(p_ply_argument argument)
{
    vertex_texcoords[next_vertex_texcoord_element_index] = ply_get_argument_value(argument);
    next_vertex_texcoord_element_index++;

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
        loop_start[next_face_offset] = next_face_element_index;
        loop_total[next_face_offset] = length;
        next_face_offset++;
        
        return 1;
    }
    
    if (next_face_element_index == face_indices_size)
    {
        face_indices_size = (int)(face_indices_size * 1.1);
        faces = (uint32_t*) realloc(faces, face_indices_size*sizeof(uint32_t));
        if (faces == NULL)
        {
            fprintf(stderr, "Failed to re-allocate faces array!\n");
            exit(-1);
        }
    }
    
    vertex_index = ply_get_argument_value(argument);
    faces[next_face_element_index++] = vertex_index;

    return 1;
}

// vjvalve: temporary variable and proto functions
#define VariableName(var) #var

/* -----------------------------------------------------------------------------
 * Struct for storing values of custom property for each vertex in a .ply
 *
 * name: property name 
 * values: full list of value associated for each vertex
 * next_value_index: index pointer to value of vertex
 * next: pointer to the next custom .ply property 
 *
 * -------------------------------------------------------------------------- */
typedef struct ply_property{
    char* name;
    double* values;
    unsigned int next_value_index;
    struct ply_property* next;
}ply_property;

/* -----------------------------------------------------------------------------
 * Array list shell for all the custom properties found in a .ply
 *
 * start: pointer to the first ply_property in list 
 * end: pointer to the last ply_property in list
 * num_vertices: keeps track of number of vertices the .ply file details
 * prop_count: number of ply_properties in list
 *
 * -------------------------------------------------------------------------- */
typedef struct ply_property_array{
    struct ply_property* start;
    struct ply_property* end;
    unsigned int num_vertices;
    unsigned int prop_count;
}ply_property_array;

// Due to how original code is set up, we keep track of the keys that are already being processed
// by default so that the code doesn't register them as custom properties
const char* key_lookup[] = {"x", "y", "z", "nx", "ny", "nz", "u", "v", "s", "t", "red", "green", "blue"};
struct ply_property_array ply_p_array;

/* -------------------------------------------------------------------------------------------
 * init_ply_property_array: initializes array
 * 
 * ply_p_array: array that is getting initialized
 * num_vertices: number of vertices that exist in .ply (which will allow for keeping track
 * of size of values array in the ply_property struct)
 * 
 * ---------------------------------------------------------------------------------------- */
static int
init_ply_property_array(struct ply_property_array* ply_p_array, unsigned int num_vertices){
    ply_p_array->start = NULL;
    ply_p_array->end = NULL;
    ply_p_array->num_vertices = num_vertices;
    ply_p_array->prop_count = 0;

    return 1;
}

/* ------------------------------------------------------------------------------------------
 * check_for_existing_property: check to see if the proposed key already has an existing property
 * of the same name in the list
 * 
 * ply_p_array: array checked to see for existing property
 * key: string name of the property
 * 
 * ----------------------------------------------------------------------------------------- */
static int
check_for_existing_property(struct ply_property_array* ply_p_array, const char* key){
    if(ply_p_array->start != NULL){
        struct ply_property* ply_p = ply_p_array->start;

        while(ply_p != NULL){
            if(!strcmp(ply_p->name, key)) return 1;
            ply_p = ply_p->next;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------------------------
 * append_ply_property: initializes and appends new ply_property struct to list  
 *
 * ply_p_array: array list
 * name: string for new ply_property 
*/
static int
append_ply_property(struct ply_property_array* ply_p_array, const char* name){
    
    if(check_for_existing_property(ply_p_array, name)) return 0;

    // when initializing the struct first allocate memory for the struct based off its size
    struct ply_property* ply_p = malloc(sizeof(ply_property));

    // then allocate memory for any other inner pointer type
    ply_p->name = malloc(strlen(name) + 1);
    strcpy(ply_p->name, name);

    ply_p->values = malloc(sizeof(double) * ply_p_array->num_vertices);
    ply_p->next_value_index = 0;
    ply_p->next = NULL;

    // if added ply_property is the first of the list
    if(ply_p_array->start == NULL){
        ply_p_array->start = ply_p;
        ply_p_array->end = ply_p;
    }
    else{
        ply_p_array->end->next = ply_p;
        ply_p_array->end = ply_p;
    }

    // increment the index of number of properties in array
    ply_p_array->prop_count++;

    return 1;
}

/* ----------------------------------------------------------------------
 * get_ply_property: returns the ply_property specified by the key passed
 * 
 * ply_p_array: property array list
 * key: name of property to be found
 * 
 * ------------------------------------------------------------------- */
static ply_property*
get_ply_property(struct ply_property_array* ply_p_array, char* key){
//    int size = ply_p_array->num_vertices;
    ply_property* ply_p = ply_p_array->start;
    while(ply_p != NULL){
        if(!strcmp(ply_p->name, key)) return ply_p;
        ply_p = ply_p->next;
    }
    return NULL;
}

/* ---------------------------------------------------------------------
 * free_ply_property_array: frees all allocated memory of the array
 * 
 * ply_p_array: Array list to be freed
 * ------------------------------------------------------------------ */
static int
free_ply_property_array(struct ply_property_array* ply_p_array){
    if(ply_p_array != NULL){
        struct ply_property* ply_p = ply_p_array-> start;
        struct ply_property* ply_p_temp = NULL;

        while(ply_p != NULL){
            // free(ply_p->values);
            ply_p_temp = ply_p;
            ply_p = ply_p->next;
            free(ply_p_temp);
        }
        ply_p_array->start = NULL;
        ply_p_array->end = NULL;

        free(ply_p_array);
    }
    return 1;
}

/* ----------------------------------------------------------------------------
 * print_ply_property: prints out the first "size" vertex values of the property
 * 
 * size: number of vertices to print up to (range(size))
 * 
 * ------------------------------------------------------------------------- */
static void
print_ply_property(struct ply_property* ply_p, unsigned int size){
    printf("name = %s\n", ply_p->name);
//    int size = ply_p_array->num_vertices;
//    int i;
    if(size){
        printf("values = ");
        for(int i=0; i<size;i++){
            printf("[%d]: %lf\n", i, ply_p->values[i]);
//            if(i < size-1) printf(", ");
//            else printf("\n");
        }
    }
}

/* -------------------------------------------------------------------------
 * print_ply_property_array: prints out all ply_property data in array
 * 
 * ply_p_array: array to be printed
 * 
 * ---------------------------------------------------------------------- */
static void
print_ply_property_array(struct ply_property_array* ply_p_array){
    struct ply_property* ply_p = ply_p_array->start;
    printf("\n**Starting print of ply_property_array**\n\n");
    printf("num_vertices = %d\n\n", ply_p_array->num_vertices);

    if(ply_p == NULL){
        printf("ply_property_array is empty \n\n");
        return;
    }

    while(ply_p != NULL){
       print_ply_property(ply_p, ply_p_array->num_vertices);
       printf("\n");

       ply_p = ply_p->next;
    }
    printf("**Ending print of ply_property_array**\n\n");
}

/* ------------------------------------------------------------------------------
 * fill_values: fills all ply_properties in array with default values for testing
 *
 * ply_p_array: array to be filled
 * 
 * --------------------------------------------------------------------------- */
static int
fill_values(struct ply_property_array* ply_p_array){
    ply_property* ply_p = ply_p_array->start;
    int size = ply_p_array->num_vertices;

    if(ply_p == NULL){
        printf("Empty ply_property_array\n");
        return 0;
    }

    while(ply_p != NULL){
        int i;
        for(i=0; i<size; i++){
            ply_p->values[i] = i;
        }
        ply_p = ply_p->next;
    }
    return 1;
}

/* ----------------------------------------------------------------------
 * To avoid interfering with the already established code and to avoid recounting 
 * properties that have already been accounted for (i.e: x, y, z, nx, ny, nz, u, v, s, t)
 * we loop through a list to confirm that the property is allowed to be processed
 * Returns 1 if successful, 0 otherwise
 * ---------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------------------
 * in_key_lookup: checks to see proposed name is an already accounted for property
 * 
 * name: string that is being checked
 * 
 * -------------------------------------------------------------------------------------*/
static int
in_key_lookup(const char* name){
    // return 1;
    int i;
    printf("prop found: %s\n", name);
    for(i=0; key_lookup[i]!= '\0'; i++){
        if(!strcmp(name, key_lookup[i])){
            // printf("found a string comparison with: %s\n", key_lookup[i]);
            printf("prop %s matches up with already accounted for %s\n", name, key_lookup[i]);
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------------------------------
 * property_double_cb: callback function where a value will be assigned to the vertex index 
 * of the property struct
 * 
 * argument: argument from .ply that contains the data associated with the property name
 * --------------------------------------------------------------------------------------- */
static int
property_double_cb(p_ply_argument argument)
{
    ply_property* ply_p;
//    ply_property** ply_p_star = &ply_p;
    int rc = ply_get_argument_user_data(argument, (void**) &ply_p, NULL);
//    int rc = ply_get_argument_user_data(argument, (void**) ply_p_star, NULL);
    // printf("index: %d\n", ply_p->next_value_index);
    // printf("output value: %lf\n", ply_get_argument_value(argument));
    ply_p->values[ply_p->next_value_index] = ply_get_argument_value(argument);
    // printf("value after assignment: %lf\n", ply_p->values[ply_p->next_value_index]);
//    printf("callback value %f\tindex: %d\n", ply_p->values[ply_p->next_value_index], ply_p->next_value_index);
    ply_p->next_value_index++;
    return rc;
}

// Main Python function

static PyObject*
readply(PyObject* self, PyObject* args, PyObject *kwds)
{
    char    *fname;
    int     vertex_values_per_loop = 1;
    
    static char *kwlist[] = {"plyfile", "vertex_values_per_loop", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|i", kwlist, &fname, &vertex_values_per_loop))
        return NULL;

//    printf(kwlist);
    
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

    // vertex_index used by Blender?
    nfaces = ply_set_read_cb(ply, "face", "vertex_indices", face_cb, NULL, 0);
    if (nfaces == 0)
    {
        printf("No property vertex_indices found for faces, trying vertex_index\n");
        nfaces = ply_set_read_cb(ply, "face", "vertex_index", face_cb, NULL, 0);
    }

    printf("%ld vertices\n%ld faces\n", nvertices, nfaces);

    // Set optional per-vertex callbacks

    int have_vertex_colors = 0;
    int have_vertex_normals = 0;
    int have_vertex_texcoords = 0;      // Either s,t or u,v sets will be used, but not both

    p_ply_property  prop;
    e_ply_type      ptype, plength_type, pvalue_type;

    // XXX check ply_set_read_cb() return values below

    prop = ply_get_next_property(vertex_element, NULL);
    int prop_count = 0;

    printf("number of vertices: %ld\n", nvertices);
    init_ply_property_array(&ply_p_array, nvertices);
//    fill_values(&ply_p_array);

//    print_ply_property_array(&ply_p_array);
    while (prop)
    {
        ply_get_property_info(prop, &name, &ptype, &plength_type, &pvalue_type);

        //printf("property '%s'\n", name);

        if (strcmp(name, "red") == 0)
        {
            // Assumes green and blue properties are also available
            // XXX is there ever an alpha value?
            have_vertex_colors = 1;

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
            have_vertex_normals = 1;

            ply_set_read_cb(ply, "vertex", "nx", vertex_normal_cb, NULL, 0);
            ply_set_read_cb(ply, "vertex", "ny", vertex_normal_cb, NULL, 0);
            ply_set_read_cb(ply, "vertex", "nz", vertex_normal_cb, NULL, 1);
        }
        else if (strcmp(name, "s") == 0 && !have_vertex_texcoords)
        {
            // Assumes t property is also available
            have_vertex_texcoords = 1;

            ply_set_read_cb(ply, "vertex", "s", vertex_texcoord_cb, NULL, 0);
            ply_set_read_cb(ply, "vertex", "t", vertex_texcoord_cb, NULL, 1);
        }
        else if (strcmp(name, "u") == 0 && !have_vertex_texcoords)
        {
            // Assumes v property is also available
            have_vertex_texcoords = 1;

            ply_set_read_cb(ply, "vertex", "u", vertex_texcoord_cb, NULL, 0);
            ply_set_read_cb(ply, "vertex", "v", vertex_texcoord_cb, NULL, 1);
        }
        else if(in_key_lookup(name)){
            // If a property came up that is unaccounted for then add it to the ply_property_array
            append_ply_property(&ply_p_array, name);
        //    printf("%s in key_lookup\n", name);
        //    printf("last value in array: %s\n", ply_p_array.end->name);

//            printf("type testing: %d\n", ply_p_array.end);
            ply_set_read_cb(ply, "vertex", name, property_double_cb, ply_p_array.end, 0);
        }

        prop = ply_get_next_property(vertex_element, prop);
    }

    /*****************testing******************/
    fill_values(&ply_p_array);
//    print_ply_property_array(&ply_p_array);
//    printf("************FLAG******************");
//    printf("In line %d\n", __LINE__);

    /******************************************/

    // Allocate memory and initialize some values

    vertices = (float*) malloc(sizeof(float)*nvertices*3);
    next_vertex_element_index = 0;

    // As we don't know the number of indices needed in advance we assume
    // quads. For a pure-triangle mesh this will overallocate by 1/4th,
    // but for a mesh with general n-gons we might have to reallocate
    // later on.

    next_face_offset = 0;
    next_face_element_index = 0;

    face_indices_size = nfaces > 128 ? nfaces*4 : 512;
    faces = (uint32_t*) malloc(sizeof(uint32_t)*face_indices_size);

    loop_start = (uint32_t*) malloc(sizeof(uint32_t)*nfaces);
    loop_total = (uint32_t*) malloc(sizeof(uint32_t)*nfaces);

    if (have_vertex_normals)
    {
        vertex_normals = (float*) malloc(sizeof(float)*nvertices*3);
        next_vertex_normal_element_index = 0;
    }

    if (have_vertex_colors)
    {
        vertex_colors = (float*) malloc(sizeof(float)*nvertices*3);
        next_vertex_color_element_index = 0;
    }

    if (have_vertex_texcoords)
    {
        vertex_texcoords = (float*) malloc(sizeof(float)*nvertices*2);
        next_vertex_texcoord_element_index = 0;
    }
    //if(have_other_properties) is not needed because we account for the creation of the property list and
    //callbacks when encountering and finding a property that is in our key_lookup.

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

        free_ply_property_array(&ply_p_array);

        return NULL;
    }

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
    npy_intp np_faces_dims[1] = { next_face_element_index };
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
    PyDict_SetItemString(result, "loop_length", np_loop_total);

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

//        for(int i = 0; i<nvertices * 3; i+=3){
//            printf("vertex_colors[%d, %d, %d]: (%d, %d, %d)\n", i, i+1, i+2, (int)(vertex_colors[i] * 255), (int)(vertex_colors[i+1] * 255), (int)(vertex_colors[i+2] * 255));
//        }
        if (vertex_values_per_loop)
        {
            // Convert list of per-vertex RGB colors to Blender-style
            // per-vertex-per-face-loop RGBA colors

            // printf("in vertex_values_per_loop, line %d\n", __LINE__);
            const int n = 4*next_face_element_index;
            float   *vcol2 = (float*) malloc(n*sizeof(float));
            float   *vcol2color = vcol2;
            float   *col;
            int     vi;

            for (int fi = 0; fi < next_face_element_index; fi++)
            {
                vi = faces[fi];


                col = vertex_colors + 3*vi;

                vcol2color[0] = col[0];
                vcol2color[1] = col[1];
                vcol2color[2] = col[2];
                vcol2color[3] = 1.0f;
                vcol2color += 4;
            }

            free(vertex_colors);

            npy_intp    dims[1] = { n };
            PyArrayObject *arr = (PyArrayObject*) PyArray_SimpleNewFromData(1, dims, NPY_FLOAT, vcol2);
            _set_base_object(arr, vcol2, "vertex_colors");
            np_vcolors = (PyObject*) arr;
        }
        else
        {
            // printf("vertex values per loop false\n");

            // Per-vertex RGB colors
            PyArrayObject *arr = (PyArrayObject*) PyArray_SimpleNewFromData(1, np_vertices_dims, NPY_FLOAT, (void*) vertex_colors);
            _set_base_object(arr, vertex_colors, "vertex_colors");
            np_vcolors = (PyObject*) arr;
        }

        PyDict_SetItemString(result, "vertex_colors", np_vcolors);
    }

    if (have_vertex_texcoords)
    {
        PyObject *np_vtexcoords;

        if (vertex_values_per_loop)
        {
            // Convert list of per-vertex UV coordinates to Blender-style
            // per-vertex-per-face-loop UV coordinates

            const int n = 2*next_face_element_index;

            float   *uv2 = (float*) malloc(n*sizeof(float));
            float   *uv2value = uv2;
            float   *uv;
            int     vi;

            for (int fi = 0; fi < next_face_element_index; fi++)
            {
                vi = faces[fi];

                uv = vertex_texcoords + 2*vi;

                uv2value[0] = uv[0];
                uv2value[1] = uv[1];
                uv2value += 2;
            }

            free(vertex_texcoords);

            npy_intp    dims[1] = { n };
            PyArrayObject *arr = (PyArrayObject*) PyArray_SimpleNewFromData(1, dims, NPY_FLOAT, uv2);
            _set_base_object(arr, uv2, "texture_coordinates");
            np_vtexcoords = (PyObject*) arr;
        }
        else
        {
            // Per-vertex UV coordinates
            npy_intp    np_vertex_texcoords_dims[1] = { nvertices*2 };

//            PyArrayObject *arr = (PyArrayObject*) PyArray_SimpleNewFromData(1, np_vertex_texcoords_dims, NPY_FLOAT, vertex_texcoords);
            PyArrayObject *arr = (PyArrayObject*) PyArray_SimpleNewFromData(1, np_vertex_texcoords_dims, NPY_FLOAT, vertex_texcoords);
            _set_base_object(arr, vertex_texcoords, "texture_coordinates");
            np_vtexcoords = (PyObject*) arr;
        }

        PyDict_SetItemString(result, "texture_coordinates", np_vtexcoords);
    }

    if (ply_p_array.start != NULL)
    {
        // printf("In line %d\n", __LINE__);
        ply_property* ply_p = ply_p_array.start;
//        print_ply_property_array(&ply_p_array);

        int testflag = 1;
        while(ply_p){

            PyObject *np_prop;
//            const int n = ply_p->next_value_index;
            const int n = ply_p_array.num_vertices;
            npy_intp    dims[1] = { n };
//            print_ply_property(ply_p, n);
            PyArrayObject* arr_temp = PyArray_SimpleNewFromData(1, dims, NPY_DOUBLE, ply_p->values);
            
            // printf("ply_p values: %lf\n", ply_p->values[0]);

//            if(testflag){
//                FILE *fp;
//                fp = fopen("text.txt", "w+");
//                PyArray_ToFile(arr_temp, fp, "\n", "");
//                fclose(fp);
//                testflag = 0;
//            }


            _set_base_object(arr_temp, ply_p->values, ply_p->name);
            // _MyDeallocObject * temp_arr_dealloc = ((_MyDeallocObject *)(PyArray_BASE(arr_temp)));
            // double * temp_double_arr = (double *)(temp_arr_dealloc->memory);
            // printf("[0]: %lf\n", temp_double_arr[0]);
            // printf("arr_temp->data[0]: %lf\n", (double)(temp_arr_dealloc->memory[0]);
            np_prop = (PyObject*) arr_temp;
            PyDict_SetItemString(result, ply_p->name, np_prop);

            ply_p = ply_p->next;

        }
        /* This code is uncommented because python references this memory location when accessing
        its dictionary values*/
        // free_ply_property_array(&ply_p_array);
    }

    // Return the stuff!


    return result;
}

// Python module stuff

static char readply_func_doc[] = 
"readply(plyfile, vertex_values_per_loop=False)\n\
\n\
Reads a 3D model from a PLY file.\n\
\n\
Returns a dictionary.\n\
\n\
The resulting dictionary will always contain keys \"num_vertices\" and \"num_faces\"\n\
holding integers. The values for other keys will be 1-dimensional Numpy arrays.\n\
\n\
Key \"vertices\" will contain vertex positions. Keys \"normals\", \"texture_coordinates\"\n\
and \"vertex_colors\" are only present if their respective model element is present\n\
in the PLY file being read.\n\
\n\
There will be three keys that describe faces:\n\
\n\
- \"faces\": vertex indices per face in a sequential list (i.e. one polygon's\n\
             indices directly follow the next)\n\
- \"loop_start\": the starting index of each face in the \"faces\" array\n\
- \"loop_length\": the length of each face in number of vertex indices\n\
\n\
If vertex_values_per_loop is True (the default) then the \"texture_coordinates\" and\n\
\"vertex_colors\" keys will hold arrays that specify per-vertex values *per polygon loop*\n\
(i.e. using the Blender convention). If vertex_values_per_loop is False the keys will hold\n\
per-vertex values (i.e. as specified in the PLY file).\n\
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
