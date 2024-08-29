#include <cassert>
#include <goto-symex/dynamic_allocation.h>
#include <goto-symex/goto_symex.h>
#include <util/c_types.h>
#include <util/cprover_prefix.h>
#include <util/expr_util.h>
#include <irep2/irep2.h>
#include <util/std_expr.h>

void goto_symext::default_replace_dynamic_allocation(expr2tc &expr)
{
  expr->Foreach_operand([this](expr2tc &e) {
    if (!is_nil_expr(e))
      default_replace_dynamic_allocation(e);
  });

  bool use_old_encoding = !options.get_bool_option("z3-slhv");
  if (is_valid_object2t(expr))
  {
    /* alloc */
    // replace with CPROVER_alloc[POINTER_OBJECT(...)]
    if(use_old_encoding) {
      const valid_object2t &obj = to_valid_object2t(expr);

      expr2tc obj_expr = pointer_object2tc(pointer_type2(), obj.value);

      expr2tc alloc_arr_2;
      migrate_expr(symbol_expr(*ns.lookup(valid_ptr_arr_name)), alloc_arr_2);

      expr2tc index_expr = index2tc(get_bool_type(), alloc_arr_2, obj_expr);
      expr = index_expr;
    } else {
      // Checking that the object is valid by heap_alloc_size
      log_status(" --- generate cond for checking heap region --- ");
      const valid_object2t &obj = to_valid_object2t(expr);
      obj.dump();
      const heap_region2t &valid_inner = to_heap_region2t(obj.value);
      const intheap_type2t &_type = to_intheap_type(valid_inner.type);
      // get alloc size heap
      expr2tc alloc_size_heap;
      migrate_expr(symbol_expr(*ns.lookup(alloc_size_heap_name)), alloc_size_heap);

      expr2tc size = gen_ulong(_type.total_bytes);
      expr2tc heap_contain =
        heap_contain2tc(
          alloc_size_heap,
          points_to2tc(valid_inner.source_location, size)
        );
      expr = heap_contain;
      log_status(" --- generate cond for checking heap region --- ");
    }
  }
  else if (is_invalid_pointer2t(expr))
  {
    /* (!valid /\ dynamic) \/ invalid */
    const invalid_pointer2t &ptr = to_invalid_pointer2t(expr);

    expr2tc obj_expr = pointer_object2tc(pointer_type2(), ptr.ptr_obj);

    if (use_old_encoding)
    {
      expr2tc alloc_arr_2;
      migrate_expr(symbol_expr(*ns.lookup(valid_ptr_arr_name)), alloc_arr_2);

      expr2tc index_expr = index2tc(get_bool_type(), alloc_arr_2, obj_expr);
      expr2tc notindex = not2tc(index_expr);

      // XXXjmorse - currently we don't correctly track the fact that stack
      // objects change validity as the program progresses, and the solver is
      // free to guess that a stack ptr is invalid, as we never update
      // __ESBMC_alloc for stack ptrs.
      // So, add the precondition that invalid_ptr only ever applies to dynamic
      // objects.

      expr2tc sym_2;
      migrate_expr(symbol_expr(*ns.lookup(dyn_info_arr_name)), sym_2);

      expr2tc ptr_obj = pointer_object2tc(pointer_type2(), ptr.ptr_obj);
      expr2tc is_dyn = index2tc(get_bool_type(), sym_2, ptr_obj);

      // Catch free pointers: don't allow anything to be pointer object 1, the
      // invalid pointer.
      type2tc ptr_type = pointer_type2tc(get_empty_type());
      expr2tc invalid_object = symbol2tc(ptr_type, "INVALID");
      expr2tc isinvalid = equality2tc(ptr.ptr_obj, invalid_object);

      expr2tc is_not_bad_ptr = and2tc(notindex, is_dyn);
      expr2tc is_valid_ptr = or2tc(is_not_bad_ptr, isinvalid);

      expr = is_valid_ptr;
    }
    else
    {
      // TODO : support
      log_error("Dot not support invalid pointer yet");
      abort();

      expr2tc alloc_size_heap;
      migrate_expr(symbol_expr(*ns.lookup(alloc_size_heap_name)), alloc_size_heap);
    }
  }
  else if (is_deallocated_obj2t(expr))
  {
    /* !alloc */
    // replace with CPROVER_alloc[POINTER_OBJECT(...)]
    const deallocated_obj2t &obj = to_deallocated_obj2t(expr);

    expr2tc obj_expr = pointer_object2tc(pointer_type2(), obj.value);

    expr2tc alloc_arr_2;
    migrate_expr(symbol_expr(*ns.lookup(valid_ptr_arr_name)), alloc_arr_2);

    if (is_symbol2t(obj.value))
      expr = index2tc(get_bool_type(), alloc_arr_2, obj_expr);
    else
    {
      expr2tc index_expr = index2tc(get_bool_type(), alloc_arr_2, obj_expr);
      expr = not2tc(index_expr);
    }
  }
  else if (is_dynamic_size2t(expr))
  {
    // replace with CPROVER_alloc_size[POINTER_OBJECT(...)]
    //nec: ex37.c
    const dynamic_size2t &size = to_dynamic_size2t(expr);

    expr2tc obj_expr = pointer_object2tc(pointer_type2(), size.value);

    expr2tc alloc_arr_2;
    migrate_expr(symbol_expr(*ns.lookup(alloc_size_arr_name)), alloc_arr_2);

    expr2tc index_expr = index2tc(size_type2(), alloc_arr_2, obj_expr);
    expr = index_expr;
  }
}
