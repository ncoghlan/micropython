/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <string.h>
#include <assert.h>

#include "py/nlr.h"
#include "py/obj.h"
#include "py/runtime0.h"
#include "py/runtime.h"
#include "py/builtin.h"
#include "py/objtype.h"

#define MP_OBJ_IS_DICT_TYPE(o) (MP_OBJ_IS_OBJ(o) && ((mp_obj_base_t*)o)->type->make_new == dict_make_new)

STATIC mp_obj_t dict_update(mp_uint_t n_args, const mp_obj_t *args, mp_map_t *kwargs);

// This is a helper function to iterate through a dictionary.  The state of
// the iteration is held in *cur and should be initialised with zero for the
// first call.  Will return NULL when no more elements are available.
STATIC mp_map_elem_t *dict_iter_next(mp_obj_dict_t *dict, mp_uint_t *cur) {
    mp_uint_t max = dict->map.alloc;
    mp_map_t *map = &dict->map;

    for (mp_uint_t i = *cur; i < max; i++) {
        if (MP_MAP_SLOT_IS_FILLED(map, i)) {
            *cur = i + 1;
            return &(map->table[i]);
        }
    }

    return NULL;
}

STATIC void dict_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mp_obj_dict_t *self = MP_OBJ_CAST(self_in);
    bool first = true;
    if (!(MICROPY_PY_UJSON && kind == PRINT_JSON)) {
        kind = PRINT_REPR;
    }
    if (MICROPY_PY_COLLECTIONS_ORDEREDDICT && self->base.type != &mp_type_dict) {
        mp_printf(print, "%q(", self->base.type->name);
    }
    mp_print_str(print, "{");
    mp_uint_t cur = 0;
    mp_map_elem_t *next = NULL;
    while ((next = dict_iter_next(self, &cur)) != NULL) {
        if (!first) {
            mp_print_str(print, ", ");
        }
        first = false;
        mp_obj_print_helper(print, next->key, kind);
        mp_print_str(print, ": ");
        mp_obj_print_helper(print, next->value, kind);
    }
    mp_print_str(print, "}");
    if (MICROPY_PY_COLLECTIONS_ORDEREDDICT && self->base.type != &mp_type_dict) {
        mp_print_str(print, ")");
    }
}

STATIC mp_obj_t dict_make_new(mp_obj_t type_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    mp_obj_t dict_out = mp_obj_new_dict(0);
    mp_obj_dict_t *dict = MP_OBJ_CAST(dict_out);
    dict->base.type = MP_OBJ_CAST(type_in);
    #if MICROPY_PY_COLLECTIONS_ORDEREDDICT
    if (MP_OBJ_CAST(type_in) == &mp_type_ordereddict) {
        dict->map.is_ordered = 1;
    }
    #endif
    if (n_args > 0 || n_kw > 0) {
        mp_obj_t args2[2] = {dict_out, args[0]}; // args[0] is always valid, even if it's not a positional arg
        mp_map_t kwargs;
        mp_map_init_fixed_table(&kwargs, n_kw, args + n_args);
        dict_update(n_args + 1, args2, &kwargs); // dict_update will check that n_args + 1 == 1 or 2
    }
    return dict_out;
}

STATIC mp_obj_t dict_unary_op(mp_uint_t op, mp_obj_t self_in) {
    mp_obj_dict_t *self = MP_OBJ_CAST(self_in);
    switch (op) {
        case MP_UNARY_OP_BOOL: return mp_obj_new_bool(self->map.used != 0);
        case MP_UNARY_OP_LEN: return MP_OBJ_NEW_SMALL_INT(self->map.used);
        default: return MP_OBJ_NULL; // op not supported
    }
}

STATIC mp_obj_t dict_binary_op(mp_uint_t op, mp_obj_t lhs_in, mp_obj_t rhs_in) {
    mp_obj_dict_t *o = MP_OBJ_CAST(lhs_in);
    switch (op) {
        case MP_BINARY_OP_IN: {
            mp_map_elem_t *elem = mp_map_lookup(&o->map, rhs_in, MP_MAP_LOOKUP);
            return mp_obj_new_bool(elem != NULL);
        }
        case MP_BINARY_OP_EQUAL: {
            #if MICROPY_PY_COLLECTIONS_ORDEREDDICT
            if (MP_UNLIKELY(MP_OBJ_IS_TYPE(lhs_in, &mp_type_ordereddict) && MP_OBJ_IS_TYPE(rhs_in, &mp_type_ordereddict))) {
                //TODO: implement
                return MP_OBJ_NULL;
            } else
            #endif
            if (MP_OBJ_IS_TYPE(rhs_in, &mp_type_dict)) {
                mp_obj_dict_t *rhs = MP_OBJ_CAST(rhs_in);
                if (o->map.used != rhs->map.used) {
                    return mp_const_false;
                }

                mp_uint_t cur = 0;
                mp_map_elem_t *next = NULL;
                while ((next = dict_iter_next(o, &cur)) != NULL) {
                    mp_map_elem_t *elem = mp_map_lookup(&rhs->map, next->key, MP_MAP_LOOKUP);
                    if (elem == NULL || !mp_obj_equal(next->value, elem->value)) {
                        return mp_const_false;
                    }
                }
                return mp_const_true;
            } else {
                // dict is not equal to instance of any other type
                return mp_const_false;
            }
        }
        default:
            // op not supported
            return MP_OBJ_NULL;
    }
}

// TODO: Make sure this is inlined in dict_subscr() below.
mp_obj_t mp_obj_dict_get(mp_obj_t self_in, mp_obj_t index) {
    mp_obj_dict_t *self = MP_OBJ_CAST(self_in);
    mp_map_elem_t *elem = mp_map_lookup(&self->map, index, MP_MAP_LOOKUP);
    if (elem == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_KeyError, "<value>"));
    } else {
        return elem->value;
    }
}

STATIC mp_obj_t dict_subscr(mp_obj_t self_in, mp_obj_t index, mp_obj_t value) {
    if (value == MP_OBJ_NULL) {
        // delete
        mp_obj_dict_delete(self_in, index);
        return mp_const_none;
    } else if (value == MP_OBJ_SENTINEL) {
        // load
        mp_obj_dict_t *self = MP_OBJ_CAST(self_in);
        mp_map_elem_t *elem = mp_map_lookup(&self->map, index, MP_MAP_LOOKUP);
        if (elem == NULL) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_KeyError, "<value>"));
        } else {
            return elem->value;
        }
    } else {
        // store
        mp_obj_dict_store(self_in, index, value);
        return mp_const_none;
    }
}

/******************************************************************************/
/* dict iterator                                                              */

typedef struct _mp_obj_dict_it_t {
    mp_obj_base_t base;
    mp_obj_t dict;
    mp_uint_t cur;
} mp_obj_dict_it_t;

STATIC mp_obj_t dict_it_iternext(mp_obj_t self_in) {
    mp_obj_dict_it_t *self = MP_OBJ_CAST(self_in);
    mp_map_elem_t *next = dict_iter_next(MP_OBJ_CAST(self->dict), &self->cur);

    if (next == NULL) {
        return MP_OBJ_STOP_ITERATION;
    } else {
        return next->key;
    }
}

STATIC const mp_obj_type_t mp_type_dict_it = {
    { &mp_type_type },
    .name = MP_QSTR_iterator,
    .getiter = mp_identity,
    .iternext = dict_it_iternext,
};

STATIC mp_obj_t dict_getiter(mp_obj_t self_in) {
    mp_obj_t o_out = m_new_obj(mp_obj_dict_it_t);
    mp_obj_dict_it_t *o = MP_OBJ_CAST(o_out);
    o->base.type = &mp_type_dict_it;
    o->dict = self_in;
    o->cur = 0;
    return o_out;
}

/******************************************************************************/
/* dict methods                                                               */

STATIC mp_obj_t dict_clear(mp_obj_t self_in) {
    assert(MP_OBJ_IS_DICT_TYPE(self_in));
    mp_obj_dict_t *self = MP_OBJ_CAST(self_in);

    mp_map_clear(&self->map);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(dict_clear_obj, dict_clear);

STATIC mp_obj_t dict_copy(mp_obj_t self_in) {
    assert(MP_OBJ_IS_DICT_TYPE(self_in));
    mp_obj_dict_t *self = MP_OBJ_CAST(self_in);
    mp_obj_t other_out = mp_obj_new_dict(self->map.alloc);
    mp_obj_dict_t *other = MP_OBJ_CAST(other_out);
    other->base.type = self->base.type;
    other->map.used = self->map.used;
    other->map.all_keys_are_qstrs = self->map.all_keys_are_qstrs;
    other->map.is_fixed = 0;
    other->map.is_ordered = self->map.is_ordered;
    memcpy(other->map.table, self->map.table, self->map.alloc * sizeof(mp_map_elem_t));
    return other_out;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(dict_copy_obj, dict_copy);

// this is a classmethod
STATIC mp_obj_t dict_fromkeys(mp_uint_t n_args, const mp_obj_t *args) {
    assert(2 <= n_args && n_args <= 3);
    mp_obj_t iter = mp_getiter(args[1]);
    mp_obj_t len = mp_obj_len_maybe(iter);
    mp_obj_t value = mp_const_none;
    mp_obj_t next = NULL;
    mp_obj_t self_out;

    if (n_args > 2) {
        value = args[2];
    }

    if (len == MP_OBJ_NULL) {
        /* object's type doesn't have a __len__ slot */
        self_out = mp_obj_new_dict(0);
    } else {
        self_out = mp_obj_new_dict(MP_OBJ_SMALL_INT_VALUE(len));
    }

    mp_obj_dict_t *self = MP_OBJ_CAST(self_out);
    while ((next = mp_iternext(iter)) != MP_OBJ_STOP_ITERATION) {
        mp_map_lookup(&self->map, next, MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = value;
    }

    return self_out;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(dict_fromkeys_fun_obj, 2, 3, dict_fromkeys);
STATIC MP_DEFINE_CONST_CLASSMETHOD_OBJ(dict_fromkeys_obj, (const mp_obj_t)&dict_fromkeys_fun_obj);

STATIC mp_obj_t dict_get_helper(mp_map_t *self, mp_obj_t key, mp_obj_t deflt, mp_map_lookup_kind_t lookup_kind) {
    mp_map_elem_t *elem = mp_map_lookup(self, key, lookup_kind);
    mp_obj_t value;
    if (elem == NULL || elem->value == MP_OBJ_NULL) {
        if (deflt == MP_OBJ_NULL) {
            if (lookup_kind == MP_MAP_LOOKUP_REMOVE_IF_FOUND) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_KeyError, "<value>"));
            } else {
                value = mp_const_none;
            }
        } else {
            value = deflt;
        }
        if (lookup_kind == MP_MAP_LOOKUP_ADD_IF_NOT_FOUND) {
            elem->value = value;
        }
    } else {
        value = elem->value;
        if (lookup_kind == MP_MAP_LOOKUP_REMOVE_IF_FOUND) {
            elem->value = MP_OBJ_NULL; // so that GC can collect the deleted value
        }
    }
    return value;
}

STATIC mp_obj_t dict_get(mp_uint_t n_args, const mp_obj_t *args) {
    assert(2 <= n_args && n_args <= 3);
    assert(MP_OBJ_IS_DICT_TYPE(args[0]));

    return dict_get_helper(&((mp_obj_dict_t *)args[0])->map,
                           args[1],
                           n_args == 3 ? args[2] : MP_OBJ_NULL,
                           MP_MAP_LOOKUP);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(dict_get_obj, 2, 3, dict_get);

STATIC mp_obj_t dict_pop(mp_uint_t n_args, const mp_obj_t *args) {
    assert(2 <= n_args && n_args <= 3);
    assert(MP_OBJ_IS_DICT_TYPE(args[0]));

    return dict_get_helper(&((mp_obj_dict_t *)args[0])->map,
                           args[1],
                           n_args == 3 ? args[2] : MP_OBJ_NULL,
                           MP_MAP_LOOKUP_REMOVE_IF_FOUND);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(dict_pop_obj, 2, 3, dict_pop);


STATIC mp_obj_t dict_setdefault(mp_uint_t n_args, const mp_obj_t *args) {
    assert(2 <= n_args && n_args <= 3);
    assert(MP_OBJ_IS_DICT_TYPE(args[0]));

    return dict_get_helper(&((mp_obj_dict_t *)args[0])->map,
                           args[1],
                           n_args == 3 ? args[2] : MP_OBJ_NULL,
                           MP_MAP_LOOKUP_ADD_IF_NOT_FOUND);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(dict_setdefault_obj, 2, 3, dict_setdefault);


STATIC mp_obj_t dict_popitem(mp_obj_t self_in) {
    assert(MP_OBJ_IS_DICT_TYPE(self_in));
    mp_obj_dict_t *self = MP_OBJ_CAST(self_in);
    mp_uint_t cur = 0;
    mp_map_elem_t *next = dict_iter_next(self, &cur);
    if (next == NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_KeyError, "popitem(): dictionary is empty"));
    }
    self->map.used--;
    mp_obj_t items[] = {next->key, next->value};
    next->key = MP_OBJ_SENTINEL; // must mark key as sentinel to indicate that it was deleted
    next->value = MP_OBJ_NULL;
    mp_obj_t tuple = mp_obj_new_tuple(2, items);

    return tuple;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(dict_popitem_obj, dict_popitem);

STATIC mp_obj_t dict_update(mp_uint_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    assert(MP_OBJ_IS_DICT_TYPE(args[0]));
    mp_obj_dict_t *self = MP_OBJ_CAST(args[0]);

    mp_arg_check_num(n_args, kwargs->used, 1, 2, true);

    if (n_args == 2) {
        // given a positional argument

        if (MP_OBJ_IS_DICT_TYPE(args[1])) {
            // update from other dictionary (make sure other is not self)
            if (args[1] != args[0]) {
                mp_uint_t cur = 0;
                mp_map_elem_t *elem = NULL;
                while ((elem = dict_iter_next((mp_obj_dict_t*)args[1], &cur)) != NULL) {
                    mp_map_lookup(&self->map, elem->key, MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = elem->value;
                }
            }
        } else {
            // update from a generic iterable of pairs
            mp_obj_t iter = mp_getiter(args[1]);
            mp_obj_t next = NULL;
            while ((next = mp_iternext(iter)) != MP_OBJ_STOP_ITERATION) {
                mp_obj_t inneriter = mp_getiter(next);
                mp_obj_t key = mp_iternext(inneriter);
                mp_obj_t value = mp_iternext(inneriter);
                mp_obj_t stop = mp_iternext(inneriter);
                if (key == MP_OBJ_STOP_ITERATION
                    || value == MP_OBJ_STOP_ITERATION
                    || stop != MP_OBJ_STOP_ITERATION) {
                    nlr_raise(mp_obj_new_exception_msg(
                                 &mp_type_ValueError,
                                 "dictionary update sequence has the wrong length"));
                } else {
                    mp_map_lookup(&self->map, key, MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = value;
                }
            }
        }
    }

    // update the dict with any keyword args
    for (mp_uint_t i = 0; i < kwargs->alloc; i++) {
        if (MP_MAP_SLOT_IS_FILLED(kwargs, i)) {
            mp_map_lookup(&self->map, kwargs->table[i].key, MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = kwargs->table[i].value;
        }
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(dict_update_obj, 1, dict_update);


/******************************************************************************/
/* dict views                                                                 */

STATIC const mp_obj_type_t dict_view_type;
STATIC const mp_obj_type_t dict_view_it_type;

typedef enum _mp_dict_view_kind_t {
    MP_DICT_VIEW_ITEMS,
    MP_DICT_VIEW_KEYS,
    MP_DICT_VIEW_VALUES,
} mp_dict_view_kind_t;

STATIC char *mp_dict_view_names[] = {"dict_items", "dict_keys", "dict_values"};

typedef struct _mp_obj_dict_view_it_t {
    mp_obj_base_t base;
    mp_dict_view_kind_t kind;
    mp_obj_t dict;
    mp_uint_t cur;
} mp_obj_dict_view_it_t;

typedef struct _mp_obj_dict_view_t {
    mp_obj_base_t base;
    mp_obj_t dict;
    mp_dict_view_kind_t kind;
} mp_obj_dict_view_t;

STATIC mp_obj_t dict_view_it_iternext(mp_obj_t self_in) {
    assert(MP_OBJ_IS_TYPE(self_in, &dict_view_it_type));
    mp_obj_dict_view_it_t *self = MP_OBJ_CAST(self_in);
    mp_map_elem_t *next = dict_iter_next(MP_OBJ_CAST(self->dict), &self->cur);

    if (next == NULL) {
        return MP_OBJ_STOP_ITERATION;
    } else {
        switch (self->kind) {
            case MP_DICT_VIEW_ITEMS:
            default: {
                mp_obj_t items[] = {next->key, next->value};
                return mp_obj_new_tuple(2, items);
            }
            case MP_DICT_VIEW_KEYS:
                return next->key;
            case MP_DICT_VIEW_VALUES:
                return next->value;
        }
    }
}

STATIC const mp_obj_type_t dict_view_it_type = {
    { &mp_type_type },
    .name = MP_QSTR_iterator,
    .getiter = mp_identity,
    .iternext = dict_view_it_iternext,
};

STATIC mp_obj_t dict_view_getiter(mp_obj_t view_in) {
    assert(MP_OBJ_IS_TYPE(view_in, &dict_view_type));
    mp_obj_dict_view_t *view = MP_OBJ_CAST(view_in);
    mp_obj_t o_out = m_new_obj(mp_obj_dict_view_it_t);
    mp_obj_dict_view_it_t *o = MP_OBJ_CAST(o_out);
    o->base.type = &dict_view_it_type;
    o->kind = view->kind;
    o->dict = view->dict;
    o->cur = 0;
    return o_out;
}

STATIC void dict_view_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    assert(MP_OBJ_IS_TYPE(self_in, &dict_view_type));
    mp_obj_dict_view_t *self = MP_OBJ_CAST(self_in);
    bool first = true;
    mp_print_str(print, mp_dict_view_names[self->kind]);
    mp_print_str(print, "([");
    mp_obj_t self_iter = dict_view_getiter(self_in);
    mp_obj_t next = MP_OBJ_NULL;
    while ((next = dict_view_it_iternext(self_iter)) != MP_OBJ_STOP_ITERATION) {
        if (!first) {
            mp_print_str(print, ", ");
        }
        first = false;
        mp_obj_print_helper(print, next, PRINT_REPR);
    }
    mp_print_str(print, "])");
}

STATIC mp_obj_t dict_view_binary_op(mp_uint_t op, mp_obj_t lhs_in, mp_obj_t rhs_in) {
    // only supported for the 'keys' kind until sets and dicts are refactored
    mp_obj_dict_view_t *o = MP_OBJ_CAST(lhs_in);
    if (o->kind != MP_DICT_VIEW_KEYS) {
        return MP_OBJ_NULL; // op not supported
    }
    if (op != MP_BINARY_OP_IN) {
        return MP_OBJ_NULL; // op not supported
    }
    return dict_binary_op(op, o->dict, rhs_in);
}

STATIC const mp_obj_type_t dict_view_type = {
    { &mp_type_type },
    .name = MP_QSTR_dict_view,
    .print = dict_view_print,
    .binary_op = dict_view_binary_op,
    .getiter = dict_view_getiter,
};

STATIC mp_obj_t mp_obj_new_dict_view(mp_obj_t dict, mp_dict_view_kind_t kind) {
    mp_obj_t o_out = m_new_obj(mp_obj_dict_view_t);
    mp_obj_dict_view_t *o = MP_OBJ_CAST(o_out);
    o->base.type = &dict_view_type;
    o->dict = dict;
    o->kind = kind;
    return o_out;
}

STATIC mp_obj_t dict_view(mp_obj_t self_in, mp_dict_view_kind_t kind) {
    assert(MP_OBJ_IS_DICT_TYPE(self_in));
    return mp_obj_new_dict_view(self_in, kind);
}

STATIC mp_obj_t dict_items(mp_obj_t self_in) {
    return dict_view(self_in, MP_DICT_VIEW_ITEMS);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(dict_items_obj, dict_items);

STATIC mp_obj_t dict_keys(mp_obj_t self_in) {
    return dict_view(self_in, MP_DICT_VIEW_KEYS);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(dict_keys_obj, dict_keys);

STATIC mp_obj_t dict_values(mp_obj_t self_in) {
    return dict_view(self_in, MP_DICT_VIEW_VALUES);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(dict_values_obj, dict_values);

/******************************************************************************/
/* dict constructors & public C API                                           */

STATIC const mp_map_elem_t dict_locals_dict_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR_clear), (mp_obj_t)&dict_clear_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_copy), (mp_obj_t)&dict_copy_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_fromkeys), (mp_obj_t)&dict_fromkeys_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get), (mp_obj_t)&dict_get_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_items), (mp_obj_t)&dict_items_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_keys), (mp_obj_t)&dict_keys_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_pop), (mp_obj_t)&dict_pop_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_popitem), (mp_obj_t)&dict_popitem_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_setdefault), (mp_obj_t)&dict_setdefault_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_update), (mp_obj_t)&dict_update_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_values), (mp_obj_t)&dict_values_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR___getitem__), (mp_obj_t)&mp_op_getitem_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR___setitem__), (mp_obj_t)&mp_op_setitem_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR___delitem__), (mp_obj_t)&mp_op_delitem_obj },
};

STATIC MP_DEFINE_CONST_DICT(dict_locals_dict, dict_locals_dict_table);

const mp_obj_type_t mp_type_dict = {
    { &mp_type_type },
    .name = MP_QSTR_dict,
    .print = dict_print,
    .make_new = dict_make_new,
    .unary_op = dict_unary_op,
    .binary_op = dict_binary_op,
    .subscr = dict_subscr,
    .getiter = dict_getiter,
    .locals_dict = (mp_obj_t)&dict_locals_dict,
};

#if MICROPY_PY_COLLECTIONS_ORDEREDDICT
STATIC const mp_obj_tuple_t ordereddict_base_tuple = {{&mp_type_tuple}, 1, {(mp_obj_t)&mp_type_dict}};

const mp_obj_type_t mp_type_ordereddict = {
    { &mp_type_type },
    .name = MP_QSTR_OrderedDict,
    .print = dict_print,
    .make_new = dict_make_new,
    .unary_op = dict_unary_op,
    .binary_op = dict_binary_op,
    .subscr = dict_subscr,
    .getiter = dict_getiter,
    .bases_tuple = (mp_obj_t)&ordereddict_base_tuple,
    .locals_dict = (mp_obj_t)&dict_locals_dict,
};
#endif

void mp_obj_dict_init(mp_obj_dict_t *dict, mp_uint_t n_args) {
    dict->base.type = &mp_type_dict;
    mp_map_init(&dict->map, n_args);
}

mp_obj_t mp_obj_new_dict(mp_uint_t n_args) {
    mp_obj_t o_out = m_new_obj(mp_obj_dict_t);
    mp_obj_dict_t *o = MP_OBJ_CAST(o_out);
    mp_obj_dict_init(o, n_args);
    return o_out;
}

mp_uint_t mp_obj_dict_len(mp_obj_t self_in) {
    mp_obj_dict_t *self = MP_OBJ_CAST(self_in);
    return self->map.used;
}

mp_obj_t mp_obj_dict_store(mp_obj_t self_in, mp_obj_t key, mp_obj_t value) {
    assert(MP_OBJ_IS_DICT_TYPE(self_in));
    mp_obj_dict_t *self = MP_OBJ_CAST(self_in);
    mp_map_lookup(&self->map, key, MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = value;
    return self_in;
}

mp_obj_t mp_obj_dict_delete(mp_obj_t self_in, mp_obj_t key) {
    assert(MP_OBJ_IS_DICT_TYPE(self_in));
    mp_obj_dict_t *self = MP_OBJ_CAST(self_in);
    dict_get_helper(&self->map, key, MP_OBJ_NULL, MP_MAP_LOOKUP_REMOVE_IF_FOUND);
    return self_in;
}

mp_map_t *mp_obj_dict_get_map(mp_obj_t self_in) {
    assert(MP_OBJ_IS_DICT_TYPE(self_in));
    mp_obj_dict_t *self = MP_OBJ_CAST(self_in);
    return &self->map;
}
