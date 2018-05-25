/*****************************************************************************/
/*  LibreDWG - free implementation of the DWG file format                    */
/*                                                                           */
/*  Copyright (C) 2018 Free Software Foundation, Inc.                        */
/*                                                                           */
/*  This library is free software, licensed under the terms of the GNU       */
/*  General Public License as published by the Free Software Foundation,     */
/*  either version 3 of the License, or (at your option) any later version.  */
/*  You should have received a copy of the GNU General Public License        */
/*  along with this program.  If not, see <http://www.gnu.org/licenses/>.    */
/*****************************************************************************/

/*
 * out_dxf.c: write as Ascii DXF
 * written by Reini Urban
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "common.h"
#include "bits.h"
#include "dwg.h"
#include "out_dxf.h"

#define DWG_LOGLEVEL DWG_LOGLEVEL_NONE
#include "logging.h"

/* the current version per spec block */
static unsigned int cur_ver = 0;
static char buf[4096];

// exported
const char* dxf_codepage (int code, Dwg_Data* dwg);

// imported
extern void
obj_string_stream(Bit_Chain *dat, BITCODE_RL bitsize, Bit_Chain *str);

// private
static void
dxf_common_entity_handle_data(Bit_Chain *dat, Dwg_Object* obj);

/*--------------------------------------------------------------------------------
 * MACROS
 */

#define IS_PRINT
#define IS_DXF

#define FIELD(name,type,dxf) VALUE(_obj->name,type,dxf)
#define FIELD_CAST(name,type,cast,dxf) FIELD(name,cast,dxf)
#define FIELD_TRACE(name,type)

#define VALUE_TV(value,dxf) \
  { GROUP(dxf); \
    fprintf(dat->fh, "%s\r\n", value); }
#ifdef HAVE_NATIVE_WCHAR2
# define VALUE_TU(value,dxf)\
  { GROUP(dxf); \
    fprintf(dat->fh, "%ls\r\n", (wchar_t*)value); }
#else
# define VALUE_TU(wstr,dxf) \
  { \
    BITCODE_TU ws = (BITCODE_TU)wstr; \
    uint16_t _c; \
    GROUP(dxf);\
    while ((_c = *ws++)) { \
      fprintf(dat->fh, "%c", (char)(_c & 0xff)); \
    } \
    fprintf(dat->fh, "\r\n"); \
  }
#endif

#define FIELD_VALUE(name) _obj->name
#define ANYCODE -1
#define FIELD_HANDLE(name, handle_code, dxf) \
  if (dxf && _obj->name) { \
    fprintf(dat->fh, "%d\r\n%lX\r\n", dxf, _obj->name->absolute_ref); \
  }
#define HEADER_9(name) \
    GROUP(9);\
    fprintf (dat->fh, "$%s\r\n", #name)
#define VALUE_H(value,dxf) \
  {\
    Dwg_Object_Ref *ref = value;\
    if (ref && ref->obj) { VALUE_RS(ref->absolute_ref, dxf); }\
    else { VALUE_RS(0, dxf); } \
  }
#define HEADER_H(name,dxf) \
    HEADER_9(name);\
    VALUE_H(dwg->header_vars.name, dxf)

#define HEADER_VALUE(name, type, dxf, value) \
  if (dxf) {\
    GROUP(9);\
    fprintf (dat->fh, "$" #name "\r\n");\
    VALUE (value, type, dxf);\
  }
#define HEADER_VAR(name, type, dxf) \
  HEADER_VALUE(name, type, dxf, dwg->header_vars.name)

#define HEADER_3D(name)\
  HEADER_9(name);\
  POINT_3D (name, header_vars.name, 10, 20, 30);
#define HEADER_2D(name)\
  HEADER_9(name);\
  POINT_2D (name, header_vars.name, 10, 20);
#define HEADER_BLL(name, dxf) \
  HEADER_9(name);\
  VALUE_BLL(dwg->header_vars.name, dxf);

#define SECTION(section) fprintf(dat->fh, "  0\r\nSECTION\r\n  2\r\n" #section "\r\n")
#define ENDSEC()         fprintf(dat->fh, "  0\r\nENDSEC\r\n")
#define TABLE(table)     fprintf(dat->fh, "  0\r\nTABLE\r\n  2\r\n" #table "\r\n")
#define ENDTAB()         fprintf(dat->fh, "  0\r\nENDTAB\r\n")
#define RECORD(record)   fprintf(dat->fh, "  0\r\n" #record "\r\n")

#define GROUP(dxf) \
    fprintf (dat->fh, "%3i\r\n", dxf)
#define VALUE(value, type, dxf) \
  if (dxf) { \
    GROUP(dxf);\
    snprintf (buf, 4096, "%s\r\n", dxf_format (dxf));\
    GCC_DIAG_IGNORE(-Wformat-nonliteral) \
    fprintf(dat->fh, buf, value);\
    GCC_DIAG_RESTORE \
  }

#define HEADER_HANDLE_NAME(name, dxf, section) \
  HEADER_9(name);\
  HANDLE_NAME(name, dxf, section)
#define HANDLE_NAME(name, dxf, section) \
  {\
    Dwg_Object_Ref *ref = dwg->header_vars.name;\
    /*if (ref && !ref->obj) ref->obj = dwg_resolve_handle(dwg, ref->absolute_ref); */ \
    if (ref && ref->obj) \
      { \
        if (ref->obj->tio.object->tio.section->entry_name && \
            !strcmp(ref->obj->tio.object->tio.section->entry_name, "STANDARD")) \
            fprintf(dat->fh, "%3i\r\nStandard\r\n", dxf); \
        else \
            fprintf(dat->fh, "%3i\r\n%s\r\n", dxf, \
                    ref->obj->tio.object->tio.section->entry_name); \
      } \
    else { \
      fprintf(dat->fh, "%3i\r\n\r\n", dxf); \
    } \
  }

#define FIELD_DATAHANDLE(name, code, dxf) FIELD_HANDLE(name, code, dxf)
#define FIELD_HANDLE_N(name, vcount, handle_code, dxf) FIELD_HANDLE(name, handle_code, dxf)

#define HEADER_RC(name,dxf)  HEADER_9(name); FIELD(name, RC, dxf)
#define HEADER_RS(name,dxf)  HEADER_9(name); FIELD(name, RS, dxf)
#define HEADER_RD(name,dxf)  HEADER_9(name); FIELD(name, RD, dxf)
#define HEADER_RL(name,dxf)  HEADER_9(name); FIELD(name, RL, dxf)
#define HEADER_RLL(name,dxf) HEADER_9(name); FIELD(name, RLL, dxf)
#define HEADER_TV(name,dxf)  HEADER_9(name); VALUE_TV(_obj->name,dxf)
#define HEADER_T(name,dxf)   HEADER_9(name); VALUE_T(_obj->name, dxf)

#define VALUE_B(value,dxf)   VALUE(value, RC, dxf)
#define VALUE_BB(value,dxf)  VALUE(value, RC, dxf)
#define VALUE_3B(value,dxf)  VALUE(value, RC, dxf)
#define VALUE_BS(value,dxf)  VALUE(value, RS, dxf)
#define VALUE_BL(value,dxf)  VALUE(value, BL, dxf)
#define VALUE_BLL(value,dxf) VALUE(value, RLL, dxf)
#define VALUE_BD(value,dxf)  VALUE(value, RD, dxf)
#define VALUE_RC(value,dxf)  VALUE(value, RC, dxf)
#define VALUE_RS(value,dxf)  VALUE(value, RS, dxf)
#define VALUE_RD(value,dxf)  VALUE(value, RD, dxf)
#define VALUE_RL(value,dxf)  VALUE(value, RL, dxf)
#define VALUE_RLL(value,dxf) VALUE(value, RLL, dxf)
#define VALUE_MC(value,dxf)  VALUE(value, MC, dxf)
#define VALUE_MS(value,dxf)  VALUE(value, MS, dxf)
#define FIELD_B(name,dxf)   FIELD(name, B, dxf)
#define FIELD_BB(name,dxf)  FIELD(name, BB, dxf)
#define FIELD_3B(name,dxf)  FIELD(name, 3B, dxf)
#define FIELD_BS(name,dxf)  FIELD(name, BS, dxf)
#define FIELD_BL(name,dxf)  FIELD(name, BL, dxf)
#define FIELD_BLL(name,dxf) FIELD(name, BLL, dxf)
#define FIELD_BD(name,dxf)  FIELD(name, BD, dxf)
#define FIELD_RC(name,dxf)  FIELD(name, RC, dxf)
#define FIELD_RS(name,dxf)  FIELD(name, RS, dxf)
#define FIELD_RD(name,dxf)  FIELD(name, RD, dxf)
#define FIELD_RL(name,dxf)  FIELD(name, RL, dxf)
#define FIELD_RLL(name,dxf) FIELD(name, RLL, dxf)
#define FIELD_MC(name,dxf)  FIELD(name, MC, dxf)
#define FIELD_MS(name,dxf)  FIELD(name, MS, dxf)
#define FIELD_TF(name,len,dxf)  VALUE_TV(_obj->name, dxf)
#define FIELD_TFF(name,len,dxf) VALUE_TV(_obj->name, dxf)
#define FIELD_TV(name,dxf) \
  if (_obj->name != NULL && dxf != 0) { VALUE_TV(_obj->name,dxf); }
#define FIELD_TU(name,dxf) \
  if (_obj->name != NULL && dxf != 0) { VALUE_TU((BITCODE_TU)_obj->name, dxf); }
#define FIELD_T(name,dxf) \
  { if (dat->from_version >= R_2007) { FIELD_TU(name, dxf); } \
    else                             { FIELD_TV(name, dxf); } }
#define VALUE_T(value,dxf) \
  { if (dat->from_version >= R_2007) { VALUE_TU(value, dxf); } \
    else                             { VALUE_TV(value, dxf); } }
#define FIELD_BT(name,dxf)     FIELD(name, BT, dxf);
#define FIELD_4BITS(name,dxf)  FIELD(name,4BITS,dxf)
#define FIELD_BE(name,dxf)     FIELD_3RD(name,dxf)
#define FIELD_DD(name, _default, dxf) FIELD_BD(name, dxf)
#define FIELD_2DD(name, d1, d2, dxf) { FIELD_DD(name.x, d1, dxf); FIELD_DD(name.y, d2, dxf+10); }
#define FIELD_3DD(name, def, dxf) { \
    FIELD_DD(name.x, FIELD_VALUE(def.x), dxf); \
    FIELD_DD(name.y, FIELD_VALUE(def.y), dxf+10); \
    FIELD_DD(name.z, FIELD_VALUE(def.z), dxf+20); }
#define FIELD_2RD(name,dxf) {FIELD(name.x, RD, dxf); FIELD(name.y, RD, dxf+10);}
#define FIELD_2BD(name,dxf) {FIELD(name.x, BD, dxf); FIELD(name.y, BD, dxf+10);}
#define FIELD_2BD_1(name,dxf) {FIELD(name.x, BD, dxf); FIELD(name.y, BD, dxf+1);}
#define FIELD_3RD(name,dxf) {FIELD(name.x, RD, dxf); FIELD(name.y, RD, dxf+10); FIELD(name.z, RD, dxf+20);}
#define FIELD_3BD(name,dxf) {FIELD(name.x, BD, dxf); FIELD(name.y, BD, dxf+10); FIELD(name.z, BD, dxf+20);}
#define FIELD_3BD_1(name,dxf) {FIELD(name.x, BD, dxf); FIELD(name.y, BD, dxf+1); FIELD(name.z, BD, dxf+2);}
#define FIELD_3DPOINT(name,dxf) FIELD_3BD(name,dxf)
#define FIELD_CMC(name,dxf)\
  VALUE_RS(_obj->name.index, dxf)
#define FIELD_TIMEBLL(name,dxf) \
  GROUP(dxf);\
  fprintf(dat->fh, FORMAT_BL "." FORMAT_BL "\r\n", _obj->name.days, _obj->name.ms)
#define HEADER_CMC(name,dxf) \
    HEADER_9(name);\
    VALUE_RS(dwg->header_vars.name.index, dxf)

#define POINT_3D(name, var, c1, c2, c3)\
  {\
    fprintf (dat->fh, "%3i\r\n%-16.14f\r\n", c1, dwg->var.x);\
    fprintf (dat->fh, "%3i\r\n%-16.14f\r\n", c2, dwg->var.y);\
    fprintf (dat->fh, "%3i\r\n%-16.14f\r\n", c3, dwg->var.z);\
  }
#define POINT_2D(name, var, c1, c2) \
  {\
    fprintf (dat->fh, "%3i\r\n%-16.14f\r\n", c1, dwg->var.x);\
    fprintf (dat->fh, "%3i\r\n%-16.14f\r\n", c2, dwg->var.y);\
  }

//FIELD_VECTOR_N(name, type, size):
// reads data of the type indicated by 'type' 'size' times and stores
// it all in the vector called 'name'.
#define FIELD_VECTOR_N(name, type, size, dxf)\
  if (dxf)\
    {\
      for (vcount=0; vcount < (int)size; vcount++)\
        {\
          fprintf(dat->fh, #name ": " FORMAT_##type ",\r\n", _obj->name[vcount]);\
        }\
    }
#define FIELD_VECTOR_T(name, size, dxf)\
  if (dxf) {\
    PRE (R_2007) {                                                   \
      for (vcount=0; vcount < (int)_obj->size; vcount++)             \
        VALUE_TV(_obj->name[vcount], dxf);                           \
    } else {                                                         \
      for (vcount=0; vcount < (int)_obj->size; vcount++)             \
        VALUE_TU(_obj->name[vcount], dxf);                           \
    }                                                                \
  }

#define FIELD_VECTOR(name, type, size, dxf) FIELD_VECTOR_N(name, type, _obj->size, dxf)

#define FIELD_2RD_VECTOR(name, size, dxf)\
  if (dxf) {\
    for (vcount=0; vcount < (int)_obj->size; vcount++)    \
      {\
        FIELD_2RD(name[vcount], dxf);\
      }\
  }

#define FIELD_2DD_VECTOR(name, size, dxf)\
  FIELD_2RD(name[0], dxf);\
  for (vcount = 1; vcount < (int)_obj->size; vcount++)\
    {\
      FIELD_2DD(name[vcount], FIELD_VALUE(name[vcount - 1].x), FIELD_VALUE(name[vcount - 1].y), dxf);\
    }\

#define FIELD_3DPOINT_VECTOR(name, size, dxf)\
  if (dxf) {\
    for (vcount=0; vcount < (int)_obj->size; vcount++)\
      {\
        FIELD_3DPOINT(name[vcount], dxf);\
      }\
    }

#define HANDLE_VECTOR_N(name, size, code, dxf) \
  if (dxf) {\
    for (vcount=0; vcount < (int)size; vcount++)\
      {\
        FIELD_HANDLE_N(name[vcount], vcount, code, dxf);\
      }\
    }

#define HANDLE_VECTOR(name, sizefield, code, dxf) \
  HANDLE_VECTOR_N(name, FIELD_VALUE(sizefield), code, dxf)

#define FIELD_INSERT_COUNT(insert_count, type, dxf) \
  FIELD(insert_count, type, dxf)

#define FIELD_XDATA(name, size)

#define REACTORS(code)\
  if (obj->tio.object->num_reactors) {\
    fprintf(dat->fh, "102\r\n{ACAD_REACTORS\r\n");\
    for (vcount=0; vcount < (int)obj->tio.object->num_reactors; vcount++)\
      { /* soft ptr */ \
        fprintf(dat->fh, "330\r\n"); \
        FIELD_HANDLE_N(reactors[vcount], vcount, code, -5);\
      }\
    fprintf(dat->fh, "102\r\n}\r\n");\
  }
#define ENT_REACTORS(code)\
  if (_obj->num_reactors) {\
    fprintf(dat->fh, "102\r\n{ACAD_REACTORS\r\n");\
    for (vcount=0; vcount < _obj->num_reactors; vcount++)\
      {\
        fprintf(dat->fh, "330\r\n"); \
        FIELD_HANDLE_N(reactors[vcount], vcount, code, -5);\
      }\
    fprintf(dat->fh, "102\r\n}\r\n");\
  }

#define XDICOBJHANDLE(code)
#define ENT_XDICOBJHANDLE(code)

#define REPEAT_N(times, name, type) \
  for (rcount=0; rcount<(int)times; rcount++)

#define REPEAT(times, name, type) \
  for (rcount=0; rcount<(int)_obj->times; rcount++)

#define REPEAT2(times, name, type) \
  for (rcount2=0; rcount2<(int)_obj->times; rcount2++)

#define REPEAT3(times, name, type) \
  for (rcount3=0; rcount3<(int)_obj->times; rcount3++)

#define REPEAT4(times, name, type) \
  for (rcount4=0; rcount4<(int)_obj->times; rcount4++)

#define COMMON_ENTITY_HANDLE_DATA \
  SINCE(R_13) { \
    dxf_common_entity_handle_data(dat, obj); \
  }
#define SECTION_STRING_STREAM
#define START_STRING_STREAM
#define END_STRING_STREAM
#define START_HANDLE_STREAM

#define DWG_ENTITY(token) \
static void \
dwg_dxf_##token (Bit_Chain *dat, Dwg_Object * obj) \
{\
  int vcount, rcount, rcount2, rcount3, rcount4; \
  Dwg_Entity_##token *ent, *_obj;\
  Dwg_Object_Entity *_ent;\
  RECORD(token);\
  LOG_INFO("Entity " #token ":\n")\
  _ent = obj->tio.entity;\
  _obj = ent = _ent->tio.token;\
  LOG_TRACE("Entity handle: %d.%d.%lu\n",\
    obj->handle.code,\
    obj->handle.size,\
    obj->handle.value)

#define DWG_ENTITY_END }

#define DWG_OBJECT(token) \
static void \
dwg_dxf_ ##token (Bit_Chain *dat, Dwg_Object * obj) \
{ \
  int vcount, rcount, rcount2, rcount3, rcount4;\
  Bit_Chain *hdl_dat = dat;\
  Dwg_Object_##token *_obj;\
  /* if not a _CONTROL object: */ \
  /* RECORD(token); */ \
  LOG_INFO("Object " #token ":\n")\
  _obj = obj->tio.object->tio.token;\
  LOG_TRACE("Object handle: %d.%d.%lu\n",\
    obj->handle.code,\
    obj->handle.size,\
    obj->handle.value)

#define DWG_OBJECT_END }

//TODO
#define COMMON_TABLE_CONTROL_FLAGS(owner) \
  VALUE_BS (ctrl->handle.value, 5); \
  VALUE_H  (_ctrl->null_handle, 330); \
  VALUE_TV ("AcDbSymbolTable", 100); \
  RESET_VER

#define COMMON_TABLE_FLAGS(owner, acdbname) \
  VALUE_BS (obj->handle.value, 5); \
  FIELD_HANDLE (owner, 4, 330); \
  VALUE_TV ("AcDbSymbolTableRecord", 100); \
  VALUE_TV ("AcDb" #acdbname "TableRecord", 100); \
  FIELD_T (entry_name, 2); \
  VALUE_RC (0, 70); \
  RESET_VER

#include "dwg.spec"

/* returns 1 if object could be printd and 0 otherwise
 */
static int
dwg_dxf_variable_type(Dwg_Data * dwg, Bit_Chain *dat, Dwg_Object* obj)
{
  int i;
  char *dxfname;
  Dwg_Class *klass;
  int is_entity;

  if ((obj->type - 500) > dwg->num_classes)
    return 0;

  i = obj->type - 500;
  klass = &dwg->dwg_class[i];
  dxfname = klass->dxfname;
  // almost always false
  is_entity = dwg_class_is_entity(klass);

#define UNHANDLED_CLASS \
      LOG_WARN("Unhandled Class %s %d %s (0x%x%s)", is_entity ? "entity" : "object",\
               klass->number, dxfname, klass->proxyflag,\
               klass->wasazombie ? " was proxy" : "")
#define UNTESTED_CLASS \
      LOG_WARN("Untested Class %s %d %s (0x%x%s)", is_entity ? "entity" : "object",\
               klass->number, dxfname, klass->proxyflag,\
               klass->wasazombie ? " was proxy" : "")

  if (!is_entity)
    fprintf(dat->fh, "  0\r\n%s\r\n", dxfname);
  if (!strcmp(dxfname, "ACDBDICTIONARYWDFLT"))
    {
      assert(!is_entity);
      dwg_dxf_DICTIONARYWDLFT(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "DICTIONARYVAR"))
    {
      assert(!is_entity);
      dwg_dxf_DICTIONARYVAR(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "HATCH"))
    {
      assert(!is_entity);
      dwg_dxf_HATCH(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "FIELDLIST"))
    {
      UNTESTED_CLASS;
      assert(!is_entity);
      dwg_dxf_FIELDLIST(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "GROUP"))
    {
      UNTESTED_CLASS;
      assert(!is_entity);
      dwg_dxf_GROUP(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "IDBUFFER"))
    {
      dwg_dxf_IDBUFFER(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "IMAGE"))
    {
      dwg_dxf_IMAGE(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "IMAGEDEF"))
    {
      dwg_dxf_IMAGEDEF(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "IMAGEDEF_REACTOR"))
    {
      dwg_dxf_IMAGEDEF_REACTOR(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "LAYER_INDEX"))
    {
      dwg_dxf_LAYER_INDEX(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "LAYOUT"))
    {
      dwg_dxf_LAYOUT(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "LWPLINE"))
    {
      dwg_dxf_LWPLINE(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "MULTILEADER"))
    {
#ifdef DEBUG_MULTILEADER
      UNTESTED_CLASS; //broken Leader_Line's/Points
      dwg_dxf_MULTILEADER(dat, obj);
      return 1;
#else
      UNHANDLED_CLASS;
      return 0;
#endif
    }
  if (!strcmp(dxfname, "MLEADERSTYLE"))
    {
      dwg_dxf_MLEADERSTYLE(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "OLE2FRAME"))
    {
      dwg_dxf_OLE2FRAME(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "OBJECTCONTEXTDATA")
      || strcmp(klass->cppname, "AcDbObjectContextData"))
    {
      dwg_dxf_OBJECTCONTEXTDATA(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "ACDBPLACEHOLDER"))
    {
      dwg_dxf_PLACEHOLDER(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "PROXY"))
    {
      dwg_dxf_PROXY(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "RASTERVARIABLES"))
    {
      dwg_dxf_RASTERVARIABLES(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "SCALE"))
    {
      dwg_dxf_SCALE(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "SORTENTSTABLE"))
    {
      dwg_dxf_SORTENTSTABLE(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "SPATIAL_FILTER"))
    {
      dwg_dxf_SPATIAL_FILTER(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "SPATIAL_INDEX"))
    {
      dwg_dxf_SPATIAL_INDEX(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "TABLE"))
    {
      UNTESTED_CLASS;
      dwg_dxf_TABLE(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "WIPEOUTVARIABLE"))
    {
      UNTESTED_CLASS;
      dwg_dxf_WIPEOUTVARIABLE(dat, obj);
      return 0;
    }
  if (!strcmp(dxfname, "WIPEOUT"))
    {
      dwg_dxf_WIPEOUT(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "FIELDLIST"))
    {
      UNTESTED_CLASS;
      dwg_dxf_FIELDLIST(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "VBA_PROJECT"))
    {
#ifdef DEBUG_VBA_PROJECT
      UNTESTED_CLASS;
      dwg_dxf_VBA_PROJECT(dat, obj);
      return 1;
#else
      UNHANDLED_CLASS;
      return 0;
#endif
    }
  if (!strcmp(dxfname, "CELLSTYLEMAP"))
    {
#ifdef DEBUG_CELLSTYLEMAP
      UNTESTED_CLASS;
      dwg_dxf_CELLSTYLEMAP(dat, obj);
      return 1;
#else
      UNHANDLED_CLASS;
      return 0;
#endif
    }
  if (!strcmp(dxfname, "VISUALSTYLE"))
    {
      dwg_dxf_VISUALSTYLE(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "AcDbField")) //?
    {
      UNTESTED_CLASS;
      dwg_dxf_FIELD(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "TABLECONTENT"))
    {
      UNTESTED_CLASS;
      dwg_dxf_TABLECONTENT(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "TABLEGEOMETRY"))
    {
      UNTESTED_CLASS;
      dwg_dxf_TABLEGEOMETRY(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "GEODATA"))
    {
      UNTESTED_CLASS;
      dwg_dxf_GEODATA(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "XRECORD"))
    {
      dwg_dxf_XRECORD(dat, obj);
      return 1;
    }
  if (!strcmp(dxfname, "ARCALIGNEDTEXT"))
    {
      UNHANDLED_CLASS;
      //assert(!is_entity);
      //dwg_dxf_ARCALIGNEDTEXT(dat, obj);
      return 0;
    }
  if (!strcmp(dxfname, "DIMASSOC"))
    {
      UNHANDLED_CLASS;
      assert(!is_entity);
      //dwg_dxf_DIMASSOC(dat, obj);
      return 0;
    }
  if (!strcmp(dxfname, "MATERIAL"))
    {
      UNHANDLED_CLASS;
      assert(!is_entity);
      //dwg_dxf_MATERIAL(dat, obj);
      return 0;
    }
  if (!strcmp(dxfname, "TABLESTYLE"))
    {
      UNHANDLED_CLASS;
      assert(!is_entity);
      //dwg_dxf_TABLESTYLE(dat, obj);
      return 0;
    }
  if (!strcmp(dxfname, "DBCOLOR"))
    {
      UNHANDLED_CLASS;
      assert(!is_entity);
      //dwg_dxf_DBCOLOR(dat, obj);
      return 0;
    }
  if (!strcmp(dxfname, "ACDBSECTIONVIEWSTYLE"))
    {
      UNHANDLED_CLASS;
      assert(!is_entity);
      //dwg_dxf_SECTIONVIEWSTYLE(dat, obj);
      return 0;
    }
  if (!strcmp(dxfname, "ACDBDETAILVIEWSTYLE"))
    {
      UNHANDLED_CLASS;
      assert(!is_entity);
      //dwg_dxf_DETAILVIEWSTYLE(dat, obj);
      return 0;
    }
  if (!strcmp(dxfname, "ACDBASSOCNETWORK"))
    {
      UNHANDLED_CLASS;
      assert(!is_entity);
      //dwg_dxf_ASSOCNETWORK(dat, obj);
      return 0;
    }
  if (!strcmp(dxfname, "ACDBASSOC2DCONSTRAINTGROUP"))
    {
      UNHANDLED_CLASS;
      assert(!is_entity);
      //dwg_dxf_ASSOC2DCONSTRAINTGROUP(dat, obj);
      return 0;
    }
  if (!strcmp(dxfname, "ACDBASSOCGEOMDEPENDENCY"))
    {
      UNHANDLED_CLASS;
      assert(!is_entity);
      //dwg_dxf_ASSOCGEOMDEPENDENCY(dat, obj);
      return 0;
    }
  if (!strcmp(dxfname, "ACDB_LEADEROBJECTCONTEXTDATA_CLASS"))
    {
      //UNHANDLED_CLASS;
      //dwg_dxf_LEADEROBJECTCONTEXTDATA(dat, obj);
      return 0;
    }

  return 0;
}

void
dwg_dxf_object(Bit_Chain *dat, Dwg_Object *obj)
{

  switch (obj->type)
    {
    case DWG_TYPE_TEXT:
      RECORD(TEXT);
      dwg_dxf_TEXT(dat, obj);
      break;
    case DWG_TYPE_ATTRIB:
      RECORD(ATTRIB);
      dwg_dxf_ATTRIB(dat, obj);
      break;
    case DWG_TYPE_ATTDEF:
      RECORD(ATTDEF);
      dwg_dxf_ATTDEF(dat, obj);
      break;
    case DWG_TYPE_BLOCK:
      RECORD(BLOCK);
      dwg_dxf_BLOCK(dat, obj);
      break;
    case DWG_TYPE_ENDBLK:
      RECORD(ENDBLK);
      dwg_dxf_ENDBLK(dat, obj);
      break;
    case DWG_TYPE_SEQEND:
      RECORD(SEQEND);
      dwg_dxf_SEQEND(dat, obj);
      break;
    case DWG_TYPE_INSERT:
      RECORD(INSERT);
      dwg_dxf_INSERT(dat, obj);
      break;
    case DWG_TYPE_MINSERT:
      RECORD(MINSERT);
      dwg_dxf_MINSERT(dat, obj);
      break;
    case DWG_TYPE_VERTEX_2D:
      RECORD(VERTEX_2D);
      dwg_dxf_VERTEX_2D(dat, obj);
      break;
    case DWG_TYPE_VERTEX_3D:
      RECORD(VERTEX_3D);
      dwg_dxf_VERTEX_3D(dat, obj);
      break;
    case DWG_TYPE_VERTEX_MESH:
      RECORD(VERTEX_MESH);
      dwg_dxf_VERTEX_MESH(dat, obj);
      break;
    case DWG_TYPE_VERTEX_PFACE:
      RECORD(VERTEX_PFACE);
      dwg_dxf_VERTEX_PFACE(dat, obj);
      break;
    case DWG_TYPE_VERTEX_PFACE_FACE:
      RECORD(VERTEX_PFACE_FACE);
      dwg_dxf_VERTEX_PFACE_FACE(dat, obj);
      break;
    case DWG_TYPE_POLYLINE_2D:
      RECORD(POLYLINE_2D);
      dwg_dxf_POLYLINE_2D(dat, obj);
      break;
    case DWG_TYPE_POLYLINE_3D:
      RECORD(POLYLINE_3D);
      dwg_dxf_POLYLINE_3D(dat, obj);
      break;
    case DWG_TYPE_ARC:
      RECORD(ARC);
      dwg_dxf_ARC(dat, obj);
      break;
    case DWG_TYPE_CIRCLE:
      RECORD(CIRCLE);
      dwg_dxf_CIRCLE(dat, obj);
      break;
    case DWG_TYPE_LINE:
      RECORD(LINE);
      dwg_dxf_LINE(dat, obj);
      break;
    case DWG_TYPE_DIMENSION_ORDINATE:
      RECORD(DIMENSION_ORDINATE);
      dwg_dxf_DIMENSION_ORDINATE(dat, obj);
      break;
    case DWG_TYPE_DIMENSION_LINEAR:
      RECORD(DIMENSION_LINEAR);
      dwg_dxf_DIMENSION_LINEAR(dat, obj);
      break;
    case DWG_TYPE_DIMENSION_ALIGNED:
      RECORD(DIMENSION_ALIGNED);
      dwg_dxf_DIMENSION_ALIGNED(dat, obj);
      break;
    case DWG_TYPE_DIMENSION_ANG3PT:
      RECORD(DIMENSION_ANG3PT);
      dwg_dxf_DIMENSION_ANG3PT(dat, obj);
      break;
    case DWG_TYPE_DIMENSION_ANG2LN:
      RECORD(DIMENSION_ANG2LN);
      dwg_dxf_DIMENSION_ANG2LN(dat, obj);
      break;
    case DWG_TYPE_DIMENSION_RADIUS:
      RECORD(DIMENSION_RADIUS);
      dwg_dxf_DIMENSION_RADIUS(dat, obj);
      break;
    case DWG_TYPE_DIMENSION_DIAMETER:
      RECORD(DIMENSION_DIAMETER);
      dwg_dxf_DIMENSION_DIAMETER(dat, obj);
      break;
    case DWG_TYPE_POINT:
      RECORD(POINT);
      dwg_dxf_POINT(dat, obj);
      break;
    case DWG_TYPE__3DFACE:
      RECORD(_3DFACE);
      dwg_dxf__3DFACE(dat, obj);
      break;
    case DWG_TYPE_POLYLINE_PFACE:
      RECORD(POLYLINE_PFACE);
      dwg_dxf_POLYLINE_PFACE(dat, obj);
      break;
    case DWG_TYPE_POLYLINE_MESH:
      RECORD(POLYLINE_MESH);
      dwg_dxf_POLYLINE_MESH(dat, obj);
      break;
    case DWG_TYPE_SOLID:
      RECORD(SOLID);
      dwg_dxf_SOLID(dat, obj);
      break;
    case DWG_TYPE_TRACE:
      RECORD(TRACE);
      dwg_dxf_TRACE(dat, obj);
      break;
    case DWG_TYPE_SHAPE:
      RECORD(SHAPE);
      dwg_dxf_SHAPE(dat, obj);
      break;
    case DWG_TYPE_VIEWPORT:
      RECORD(VIEWPORT);
      dwg_dxf_VIEWPORT(dat, obj);
      break;
    case DWG_TYPE_ELLIPSE:
      RECORD(ELLIPSE);
      dwg_dxf_ELLIPSE(dat, obj);
      break;
    case DWG_TYPE_SPLINE:
      RECORD(SPLINE);
      dwg_dxf_SPLINE(dat, obj);
      break;
    case DWG_TYPE_REGION:
      RECORD(REGION);
      dwg_dxf_REGION(dat, obj);
      break;
    case DWG_TYPE_3DSOLID:
      RECORD(3DSOLID);
      dwg_dxf__3DSOLID(dat, obj);
      break; /* Check the type of the object */
    case DWG_TYPE_BODY:
      RECORD(BODY);
      dwg_dxf_BODY(dat, obj);
      break;
    case DWG_TYPE_RAY:
      RECORD(RAY);
      dwg_dxf_RAY(dat, obj);
      break;
    case DWG_TYPE_XLINE:
      RECORD(XLINE);
      dwg_dxf_XLINE(dat, obj);
      break;
    case DWG_TYPE_DICTIONARY:
      RECORD(DICTIONARY);
      dwg_dxf_DICTIONARY(dat, obj);
      break;
    case DWG_TYPE_MTEXT:
      RECORD(MTEXT);
      dwg_dxf_MTEXT(dat, obj);
      break;
    case DWG_TYPE_LEADER:
      RECORD(LEADER);
      dwg_dxf_LEADER(dat, obj);
      break;
    case DWG_TYPE_TOLERANCE:
      RECORD(TOLERANCE);
      dwg_dxf_TOLERANCE(dat, obj);
      break;
    case DWG_TYPE_MLINE:
      RECORD(MLINE);
      dwg_dxf_MLINE(dat, obj);
      break;
    case DWG_TYPE_BLOCK_CONTROL:
      //RECORD(BLOCK);
      //dwg_dxf_BLOCK_CONTROL(dat, obj);
      break;
    case DWG_TYPE_BLOCK_HEADER:
      //RECORD(BLOCK_HEADER);
      //dwg_dxf_BLOCK_HEADER(dat, obj);
      break;
    case DWG_TYPE_LAYER_CONTROL:
      //RECORD(LAYER);
      //dwg_dxf_LAYER_CONTROL(dat, obj);
      break;
    case DWG_TYPE_LAYER:
      //RECORD(LAYER);
      //dwg_dxf_LAYER(dat, obj);
      break;
    case DWG_TYPE_STYLE_CONTROL:
      //RECORD(STYLE);
      //dwg_dxf_STYLE_CONTROL(dat, obj);
      break;
    case DWG_TYPE_STYLE:
      RECORD(STYLE);
      dwg_dxf_STYLE(dat, obj);
      break;
    case DWG_TYPE_LTYPE_CONTROL:
      //RECORD(LTYPE);
      //dwg_dxf_LTYPE_CONTROL(dat, obj);
      break;
    case DWG_TYPE_LTYPE:
      RECORD(LTYPE);
      dwg_dxf_LTYPE(dat, obj);
      break;
    case DWG_TYPE_VIEW_CONTROL:
      //RECORD(VIEW);
      //dwg_dxf_VIEW_CONTROL(dat, obj);
      break;
    case DWG_TYPE_VIEW:
      //RECORD(VIEW);
      //dwg_dxf_VIEW(dat, obj);
      break;
    case DWG_TYPE_UCS_CONTROL:
      //RECORD(UCS);
      //dwg_dxf_UCS_CONTROL(dat, obj);
      break;
    case DWG_TYPE_UCS:
      //RECORD(UCS);
      //dwg_dxf_UCS(dat, obj);
      break;
    case DWG_TYPE_VPORT_CONTROL:
      //RECORD(VPORT);
      //dwg_dxf_VPORT_CONTROL(dat, obj);
      break;
    case DWG_TYPE_VPORT:
      //RECORD(VPORT);
      //dwg_dxf_VPORT(dat, obj);
      break;
    case DWG_TYPE_APPID_CONTROL:
      //RECORD(APPID);
      //dwg_dxf_APPID_CONTROL(dat, obj);
      break;
    case DWG_TYPE_APPID:
      //RECORD(APPID);
      //dwg_dxf_APPID(dat, obj);
      break;
    case DWG_TYPE_DIMSTYLE_CONTROL:
      //RECORD(DIMSTYLE);
      //dwg_dxf_DIMSTYLE_CONTROL(dat, obj);
      break;
    case DWG_TYPE_DIMSTYLE:
      //RECORD(DIMSTYLE);
      //dwg_dxf_DIMSTYLE(dat, obj);
      break;
    case DWG_TYPE_VPORT_ENT_CONTROL:
      //RECORD(VPORT_ENT_HEADER);
      //dwg_dxf_VPORT_ENT_CONTROL(dat, obj);
      break;
    case DWG_TYPE_VPORT_ENT_HEADER:
      //RECORD(VPORT_ENT_HEADER);
      //dwg_dxf_VPORT_ENT_HEADER(dat, obj);
      break;
    case DWG_TYPE_GROUP:
      RECORD(GROUP);
      dwg_dxf_GROUP(dat, obj);
      break;
    case DWG_TYPE_MLINESTYLE:
      RECORD(MLINESTYLE);
      dwg_dxf_MLINESTYLE(dat, obj);
      break;
    case DWG_TYPE_OLE2FRAME:
      RECORD(OLE2FRAME);
      dwg_dxf_OLE2FRAME(dat, obj);
      break;
    case DWG_TYPE_DUMMY:
      RECORD(DUMMY);
      dwg_dxf_DUMMY(dat, obj);
      break;
    case DWG_TYPE_LONG_TRANSACTION:
      RECORD(LONG_TRANSACTION);
      dwg_dxf_LONG_TRANSACTION(dat, obj);
      break;
    case DWG_TYPE_LWPLINE:
      RECORD(LWPLINE);
      dwg_dxf_LWPLINE(dat, obj);
      break;
    case DWG_TYPE_HATCH:
      RECORD(HATCH);
      dwg_dxf_HATCH(dat, obj);
      break;
    case DWG_TYPE_XRECORD:
      RECORD(XRECORD);
      dwg_dxf_XRECORD(dat, obj);
      break;
    case DWG_TYPE_PLACEHOLDER:
      RECORD(PLACEHOLDER);
      dwg_dxf_PLACEHOLDER(dat, obj);
      break;
    case DWG_TYPE_PROXY_ENTITY:
      //RECORD(PROXY_ENTITY);
      //dwg_dxf_PROXY_ENTITY(dat, obj);
      break;
    case DWG_TYPE_OLEFRAME:
      RECORD(OLEFRAME);
      dwg_dxf_OLEFRAME(dat, obj);
      break;
    case DWG_TYPE_VBA_PROJECT:
      LOG_ERROR("Unhandled Object VBA_PROJECT. Has its own section\n");
      //RECORD(VBA_PROJECT);
      //dwg_dxf_VBA_PROJECT(dat, obj);
      break;
    case DWG_TYPE_LAYOUT:
      RECORD(LAYOUT);
      dwg_dxf_LAYOUT(dat, obj);
      break;
    default:
      if (obj->type == obj->parent->layout_number)
        {
          dwg_dxf_LAYOUT(dat, obj);
        }
      /* > 500:
         TABLE, DICTIONARYWDLFT, IDBUFFER, IMAGE, IMAGEDEF, IMAGEDEF_REACTOR,
         LAYER_INDEX, OLE2FRAME, PROXY, RASTERVARIABLES, SORTENTSTABLE, SPATIAL_FILTER,
         SPATIAL_INDEX
      */
      else if (!dwg_dxf_variable_type(obj->parent, dat, obj))
        {
          Dwg_Data *dwg = obj->parent;
          int is_entity;
          int i = obj->type - 500;
          Dwg_Class *klass = NULL;

          if (i <= (int)dwg->num_classes)
            {
              klass = &dwg->dwg_class[i];
              is_entity = dwg_class_is_entity(klass);
            }
          // properly dwg_decode_object/_entity for eed, reactors, xdic
          if (klass && !is_entity)
            {
              dwg_dxf_UNKNOWN_OBJ(dat, obj);
            }
          else if (klass)
            {
              dwg_dxf_UNKNOWN_ENT(dat, obj);
            }
          else // not a class
            {
              LOG_WARN("Unknown object, skipping eed/reactors/xdic");
              SINCE(R_2000)
                {
                  LOG_INFO("Object bitsize: %u\n", obj->bitsize)
                }
              LOG_INFO("Object handle: %d.%d.%lu\n",
                       obj->handle.code, obj->handle.size, obj->handle.value);
            }
        }
    }
}

static void
dxf_common_entity_handle_data(Bit_Chain *dat, Dwg_Object* obj)
{
  Dwg_Object_Entity *ent;
  //Dwg_Data *dwg = obj->parent;
  Dwg_Object_Entity *_obj;
  int i;
  long unsigned int vcount = 0;
  ent = obj->tio.entity;
  _obj = ent;

  #include "common_entity_handle_data.spec"
}

const char *
dxf_format (int code)
{
  if (0 <= code && code < 5)
    return "%s";
  if (code == 5 || code == -5)
    return "%X";
  if (5 < code && code < 10)
    return "%s";
  if (code < 60)
    return "%-16.14f";
  if (code < 80)
    return "%6i";
  if (90 <= code && code <= 99)
    return "%9li";
  if (code == 100)
    return "%s";
  if (code == 102)
    return "%s";
  if (code == 105)
    return "%X";
  if (110 <= code && code <= 149)
    return "%-16.14f";
  if (160 <= code && code <= 169)
    return "%12li";
  if (170 <= code && code <= 179)
    return "%6i";
  if (210 <= code && code <= 239)
    return "%-16.14f";
  if (270 <= code && code <= 289)
    return "%6i";
  if (290 <= code && code <= 299)
    return "%6i"; // boolean
  if (300 <= code && code <= 319)
    return "%s";
  if (320 <= code && code <= 369)
    return "%X";
  if (370 <= code && code <= 389)
    return "%6i";
  if (390 <= code && code <= 399)
    return "%X";
  if (400 <= code && code <= 409)
    return "%6i";
  if (410 <= code && code <= 419)
    return "%s";
  if (420 <= code && code <= 429)
    return "%9li"; //int32_t
  if (430 <= code && code <= 439)
    return "%s";
  if (440 <= code && code <= 449)
    return "%9li"; //int32_t
  if (450 <= code && code <= 459)
    return "%12li"; //long
  if (460 <= code && code <= 469)
    return "%-16.14f";
  if (470 <= code && code <= 479)
    return "%s";
  if (480 <= code && code <= 481)
    return "%X";
  if (code == 999)
    return "%s";
  if (1000 <= code && code <= 1009)
    return "%s";
  if (1010 <= code && code <= 1059)
    return "%-16.14f";
  if (1060 <= code && code <= 1070)
    return "%6i";
  if (code == 1071)
    return "%9li"; //int32_t

  return "(unknown code)";
}

const char* dxf_codepage (int code, Dwg_Data* dwg)
{
  if (code == 30 || code == 0)
    return "ANSI_1252";
  else if (code == 29)
    return "ANSI_1251";
  else if (dwg->header.version >= R_2007)
    return "UTF-8"; // dwg internally: UCS-16, for DXF: UTF-8
  else
    return "ANSI_1252";
}

// see https://www.autodesk.com/techpubs/autocad/acad2000/dxf/header_section_group_codes_dxf_02.htm
void
dxf_header_write(Bit_Chain *dat, Dwg_Data* dwg)
{
  Dwg_Header_Variables* _obj = &dwg->header_vars;
  Dwg_Object* obj = NULL;
  double ms;
  const int minimal = dwg->opts & 0x10;
  const char* codepage = dxf_codepage(dwg->header.codepage, dwg);

  if (dwg->header.codepage != 30 &&
      dwg->header.codepage != 29 &&
      dwg->header.codepage != 0 &&
      dwg->header.version < R_2007) {
    // some asian or eastern-european codepage
    // see https://github.com/mozman/ezdxf/blob/master/docs/source/dxfinternals/fileencoding.rst
    LOG_WARN("Unknown codepage %d, assuming ANSI_1252", dwg->header.codepage);
  }

  #include "header_variables_dxf.spec"

  return;
}

// only called since r2000. but not really needed, unless referenced
static int
dxf_classes_write (Bit_Chain *dat, Dwg_Data * dwg)
{
  unsigned int i;

  SECTION (CLASSES);
  LOG_TRACE("num_classes: %u\n", dwg->num_classes);
  for (i=0; i < dwg->num_classes; i++)
    {
      RECORD (CLASS);
      VALUE_TV (dwg->dwg_class[i].dxfname, 1);
      VALUE_T (dwg->dwg_class[i].cppname, 2);
      VALUE_T (dwg->dwg_class[i].appname, 3);
      VALUE_RS (dwg->dwg_class[i].proxyflag, 90);
      SINCE (R_2004) {
        VALUE_RC (dwg->dwg_class[i].instance_count, 91);
      }
      VALUE_RC (dwg->dwg_class[i].wasazombie, 280);
      // Is-an-entity. 1f2 for entities, 1f3 for objects
      VALUE_RC (dwg->dwg_class[i].item_class_id == 0x1F2 ? 1 : 0, 281);
    }
  ENDSEC();
  return 0;
}

static int
dxf_tables_write (Bit_Chain *dat, Dwg_Data * dwg)
{
  unsigned int i;

  SECTION(TABLES);
  if (dwg->vport_control.num_entries)
    {
      Dwg_Object_VPORT_CONTROL *_ctrl = &dwg->vport_control;
      Dwg_Object *ctrl = &dwg->object[_ctrl->objid];
      TABLE(VPORT);
      COMMON_TABLE_CONTROL_FLAGS(null_handle);
      dwg_dxf_VPORT_CONTROL(dat, ctrl);
      /* ??
      VALUE_TV ("ACAD", 1001);
      VALUE_TV ("DbSaveVer", 1000);
      VALUE_RS (30, 1071); */
      for (i=0; i<dwg->vport_control.num_entries; i++)
        {
          Dwg_Object *obj = dwg_ref_get_object(dwg, _ctrl->vports[i]);
          if (obj) {
            RECORD (VPORT);
            //reordered in the DXF: 2,70,10,11,12,13,14,15,16,...
            //special-cased in the spec
            dwg_dxf_VPORT(dat, obj);
          }
        }
      ENDTAB();
    }
  if (dwg->ltype_control.num_entries)
    {
      Dwg_Object_LTYPE_CONTROL *_ctrl = &dwg->ltype_control;
      Dwg_Object *ctrl = &dwg->object[_ctrl->objid];
      TABLE(LTYPE);
      COMMON_TABLE_CONTROL_FLAGS(null_handle);
      dwg_dxf_LTYPE_CONTROL(dat, ctrl);
      for (i=0; i<dwg->ltype_control.num_entries; i++)
        {
          Dwg_Object *obj = dwg_ref_get_object(dwg, _ctrl->linetypes[i]);
          if (obj) {
            RECORD (LTYPE);
            dwg_dxf_LTYPE(dat, obj);
          }
        }
      ENDTAB();
    }
  if (dwg->layer_control.num_entries)
    {
      Dwg_Object_LAYER_CONTROL *_ctrl = &dwg->layer_control;
      Dwg_Object *ctrl = &dwg->object[_ctrl->objid];
      TABLE(LAYER);
      COMMON_TABLE_CONTROL_FLAGS(null_handle);
      dwg_dxf_LAYER_CONTROL(dat, ctrl);
      for (i=0; i<dwg->layer_control.num_entries; i++)
        {
          Dwg_Object *obj = dwg_ref_get_object(dwg, _ctrl->layers[i]);
          if (obj) {
            RECORD (LAYER);
            dwg_dxf_LAYER(dat, obj);
          }
        }
      ENDTAB();
    }
  if (dwg->style_control.num_entries)
    {
      Dwg_Object_STYLE_CONTROL *_ctrl = &dwg->style_control;
      Dwg_Object *ctrl = &dwg->object[_ctrl->objid];
      TABLE(STYLE);
      COMMON_TABLE_CONTROL_FLAGS(null_handle);
      dwg_dxf_STYLE_CONTROL(dat, ctrl);
      for (i=0; i<dwg->style_control.num_entries; i++)
        {
          Dwg_Object *obj = dwg_ref_get_object(dwg, _ctrl->styles[i]);
          if (obj) {
            RECORD (STYLE);
            dwg_dxf_STYLE(dat, obj);
          }
        }
      ENDTAB();
    }
  if (dwg->view_control.num_entries)
    {
      Dwg_Object_VIEW_CONTROL *_ctrl = &dwg->view_control;
      Dwg_Object *ctrl = &dwg->object[_ctrl->objid];
      TABLE(VIEW);
      COMMON_TABLE_CONTROL_FLAGS(null_handle);
      dwg_dxf_VIEW_CONTROL(dat, ctrl);
      for (i=0; i<dwg->view_control.num_entries; i++)
        {
          Dwg_Object *obj = dwg_ref_get_object(dwg, _ctrl->views[i]);
          if (obj) {
            RECORD (VIEW);
            dwg_dxf_VIEW(dat, obj);
          }
        }
      ENDTAB();
    }
  if (dwg->ucs_control.num_entries)
    {
      Dwg_Object_UCS_CONTROL *_ctrl = &dwg->ucs_control;
      Dwg_Object *ctrl = &dwg->object[_ctrl->objid];
      TABLE(UCS);
      COMMON_TABLE_CONTROL_FLAGS(null_handle);
      dwg_dxf_UCS_CONTROL(dat, ctrl);
      for (i=0; i<dwg->ucs_control.num_entries; i++)
        {
          Dwg_Object *obj = dwg_ref_get_object(dwg, _ctrl->ucs[i]);
          if (obj) {
            RECORD (UCS);
            dwg_dxf_UCS(dat, obj);
          }
        }
      ENDTAB();
    }
  if (dwg->appid_control.num_entries)
    {
      Dwg_Object_APPID_CONTROL *_ctrl = &dwg->appid_control;
      Dwg_Object *ctrl = &dwg->object[_ctrl->objid];
      TABLE(APPID);
      COMMON_TABLE_CONTROL_FLAGS(null_handle);
      dwg_dxf_APPID_CONTROL(dat, ctrl);
      for (i=0; i<dwg->appid_control.num_entries; i++)
        {
          Dwg_Object *obj = dwg_ref_get_object(dwg, _ctrl->apps[i]);
          if (obj) {
            RECORD (APPID);
            dwg_dxf_APPID(dat, obj);
          }
        }
      ENDTAB();
    }
  if (dwg->dimstyle_control.num_entries)
    {
      Dwg_Object_DIMSTYLE_CONTROL *_ctrl = &dwg->dimstyle_control;
      Dwg_Object *ctrl = &dwg->object[_ctrl->objid];
      TABLE(DIMSTYLE);
      COMMON_TABLE_CONTROL_FLAGS(null_handle);
      dwg_dxf_DIMSTYLE_CONTROL(dat, ctrl);
      //ignoring morehandles
      for (i=0; i<dwg->dimstyle_control.num_entries; i++)
        {
          Dwg_Object *obj = dwg_ref_get_object(dwg, _ctrl->dimstyles[i]);
          if (obj) {
            RECORD (DIMSTYLE);
            dwg_dxf_DIMSTYLE(dat, obj);
          }
        }
      ENDTAB();
    }
  if (dwg->block_control.num_entries)
    {
      Dwg_Object_BLOCK_CONTROL *_ctrl = &dwg->block_control;
      Dwg_Object *ctrl = &dwg->object[_ctrl->objid];
      TABLE(BLOCK_RECORD);
      COMMON_TABLE_CONTROL_FLAGS(null_handle);
      dwg_dxf_BLOCK_CONTROL(dat, ctrl);
      /*
      for (i=0; i < _ctrl->num_entries; i++)
        {
          Dwg_Object *obj = dwg_ref_get_object(dwg, _ctrl->block_headers[i]);
          if (obj) {
            RECORD (BLOCK_RECORD);
            dwg_dxf_BLOCK_HEADER(dat, obj);
          }
        }
      */
      ENDTAB();
    }
  ENDSEC();
  return 0;
}

static int
dxf_blocks_write (Bit_Chain *dat, Dwg_Data * dwg)
{
  unsigned int i;
  Dwg_Object *mspace = NULL, *pspace = NULL;
  Dwg_Object_BLOCK_CONTROL *_ctrl = &dwg->block_control;
  Dwg_Object *ctrl = &dwg->object[_ctrl->objid];

  SECTION(BLOCKS);
  COMMON_TABLE_CONTROL_FLAGS(null_handle);
  dwg_dxf_BLOCK_CONTROL(dat, ctrl);
  if (_ctrl->model_space)
    {
      Dwg_Object *obj = dwg_ref_get_object(dwg, _ctrl->model_space);
      if (obj) {
        mspace = obj;
        assert(obj->type == DWG_TYPE_BLOCK_HEADER);
        RECORD (BLOCK);
        dwg_dxf_BLOCK_HEADER(dat, obj);
      }
    }
  if (dwg->block_control.paper_space)
    {
      Dwg_Object *obj = dwg_ref_get_object(dwg, dwg->block_control.paper_space);
      if (obj) {
        pspace = obj;
        assert(obj->type == DWG_TYPE_BLOCK_HEADER);
        RECORD (BLOCK);
        dwg_dxf_BLOCK_HEADER(dat, obj);
      }
    }
  for (i=0; i<dwg->block_control.num_entries; i++)
    {
      Dwg_Object *obj = dwg_ref_get_object(dwg, dwg->block_control.block_headers[i]);
      if (obj && obj != mspace && obj != pspace)
        {
          assert(obj->type == DWG_TYPE_BLOCK_HEADER);
          RECORD (BLOCK);
          dwg_dxf_BLOCK_HEADER(dat, obj);
        }
    }
  ENDSEC();
  return 0;
}

static int
dxf_entities_write (Bit_Chain *dat, Dwg_Data * dwg)
{
  long unsigned int i;

  SECTION(ENTITIES);
  for (i=0; i<dwg->num_objects; i++)
    {
      if (dwg->object[i].supertype == DWG_SUPERTYPE_ENTITY)
        dwg_dxf_object(dat, &dwg->object[i]);
    }
  ENDSEC();
  return 0;
}

static int
dxf_objects_write (Bit_Chain *dat, Dwg_Data * dwg)
{
  long unsigned int i;

  SECTION(OBJECTS);
  for (i=0; i<dwg->num_objects; i++)
    {
      if (dwg->object[i].supertype == DWG_SUPERTYPE_OBJECT)
        dwg_dxf_object(dat, &dwg->object[i]);
    }
  ENDSEC();
  return 0;
}

//TODO: Beware, there's also a new ACDSDATA section, with ACDSSCHEMA elements
// and the Thumbnail_Data

static int
dxf_preview_write (Bit_Chain *dat, Dwg_Data * dwg)
{
  (void)dat; (void)dwg;
  //...
  return 0;
}

int
dwg_write_dxf(Bit_Chain *dat, Dwg_Data * dwg)
{
  const int minimal = dwg->opts & 0x10;
  struct Dwg_Header *obj = &dwg->header;

  VALUE_TV(PACKAGE_STRING, 999);

  // A minimal header requires only $ACADVER, $HANDSEED, and then ENTITIES
  // see https://pythonhosted.org/ezdxf/dxfinternals/filestructure.html
  SINCE(R_13)
  {
    dxf_header_write (dat, dwg);

    SINCE(R_2000) {
      if (dxf_classes_write (dat, dwg))
        goto fail;
    }

    if (dxf_tables_write (dat, dwg))
      goto fail;

    if (dxf_blocks_write (dat, dwg))
      goto fail;
  }

  if (dxf_entities_write (dat, dwg))
    goto fail;

  SINCE(R_13) {
    if (dxf_objects_write (dat, dwg))
      goto fail;
  }

  if (dwg->header.version >= R_2000 && !minimal) {
    if (dxf_preview_write (dat, dwg))
      goto fail;
  }
  RECORD(EOF);

  return 0;
 fail:
  return 1;
}

#undef IS_PRINT
#undef IS_DXF
