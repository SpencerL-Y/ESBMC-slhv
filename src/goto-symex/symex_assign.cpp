/**
 * @author Term SELO
 * @brief Modified by from ESBMC
 */

#include <cassert>
#include <goto-symex/dynamic_allocation.h>
#include <goto-symex/execution_state.h>
#include <goto-symex/goto_symex.h>
#include <util/c_types.h>
#include <util/cprover_prefix.h>
#include <util/expr_util.h>
#include <util/i2string.h>
#include <irep2/irep2.h>
#include <util/migrate.h>
#include <util/simplify_expr.h>
#include <util/std_expr.h>

goto_symext::goto_symext(
  const namespacet &_ns,
  contextt &_new_context,
  const goto_functionst &_goto_functions,
  std::shared_ptr<symex_targett> _target,
  optionst &opts)
  : options(opts),
    guard_identifier_s("goto_symex::guard"),
    first_loop(0),
    total_claims(0),
    remaining_claims(0),
    max_unwind(options.get_option("unwind").c_str()),
    constant_propagation(!options.get_bool_option("no-propagation")),
    ns(_ns),   
    new_context(_new_context),
    goto_functions(_goto_functions),
    target(std::move(_target)),
    cur_state(nullptr),
    last_throw(nullptr),
    inside_unexpected(false),
    no_return_value_opt(options.get_bool_option("no-return-value-opt")),
    stack_limit(atol(options.get_option("stack-limit").c_str())),
    depth_limit(atol(options.get_option("depth").c_str())),
    break_insn(atol(options.get_option("break-at").c_str())),
    memory_leak_check(options.get_bool_option("memory-leak-check")),
    no_reachable_memleak(options.get_bool_option("no-reachable-memory-leak")),
    no_assertions(options.get_bool_option("no-assertions")),
    no_simplify(options.get_bool_option("no-simplify")),
    no_unwinding_assertions(options.get_bool_option("no-unwinding-assertions")),
    partial_loops(options.get_bool_option("partial-loops")),
    k_induction(options.is_kind()),
    base_case(options.get_bool_option("base-case")),
    forward_condition(options.get_bool_option("forward-condition")),
    inductive_step(options.get_bool_option("inductive-step"))
{
  const std::string &set = options.get_option("unwindset");
  unsigned int length = set.length();

  for (unsigned int idx = 0; idx < length; idx++)
  {
    std::string::size_type next = set.find(",", idx);
    std::string val = set.substr(idx, next - idx);
    unsigned long id = atoi(val.substr(0, val.find(":", 0)).c_str());
    BigInt uw(val.substr(val.find(":", 0) + 1).c_str());
    unwind_set[id] = uw;
    if (next == std::string::npos)
      break;
    idx = next;
  }

  art1 = nullptr;

  valid_ptr_arr_name = "c:@__ESBMC_alloc";
  alloc_size_arr_name = "c:@__ESBMC_alloc_size";
  dyn_info_arr_name = "c:@__ESBMC_is_dynamic";
  alloc_size_heap_name = "c:@__ESBMC_heap_alloc_size";

  symbolt sym;
  sym.id = "symex_throw::thrown_obj";
  sym.name = "thrown_obj";
  // Type left deliberately undefined. XXX, is this wise?
  new_context.move(sym);
}

goto_symext::goto_symext(const goto_symext &sym)
  : options(sym.options),
    ns(sym.ns),
    new_context(sym.new_context),
    goto_functions(sym.goto_functions),
    last_throw(nullptr),
    inside_unexpected(false)
{
  *this = sym;
}

goto_symext &goto_symext::operator=(const goto_symext &sym)
{
  unwind_set = sym.unwind_set;
  max_unwind = sym.max_unwind;
  constant_propagation = sym.constant_propagation;
  total_claims = sym.total_claims;
  remaining_claims = sym.remaining_claims;
  guard_identifier_s = sym.guard_identifier_s;
  depth_limit = sym.depth_limit;
  break_insn = sym.break_insn;
  memory_leak_check = sym.memory_leak_check;
  no_reachable_memleak = sym.no_reachable_memleak;
  no_assertions = sym.no_assertions;
  no_simplify = sym.no_simplify;
  no_unwinding_assertions = sym.no_unwinding_assertions;
  partial_loops = sym.partial_loops;
  k_induction = sym.k_induction;
  base_case = sym.base_case;
  forward_condition = sym.forward_condition;
  inductive_step = sym.inductive_step;
  first_loop = sym.first_loop;

  valid_ptr_arr_name = sym.valid_ptr_arr_name;
  alloc_size_arr_name = sym.alloc_size_arr_name;
  dyn_info_arr_name = sym.dyn_info_arr_name;
  alloc_size_heap_name = sym.alloc_size_heap_name;

  dynamic_memory = sym.dynamic_memory;

  // Art ptr is shared
  art1 = sym.art1;

  // Symex target is another matter; a higher up class needs to decide
  // whether we're duplicating it or using the same one.
  target = nullptr;

  return *this;
}

void goto_symext::do_simplify(expr2tc &expr)
{
  if (!no_simplify)
    simplify(expr);
}

void goto_symext::symex_assign(
  const expr2tc &code_assign,
  const bool hidden,
  const guardt &guard)
{
  log_debug("SLHV", "xxxxxxxxxxxxxxxxxxxxxx symex assign xxxxxxxxxxxxxxxxxxxxxx");
  if (messaget::state.modules.count("SLHV") > 0)
    code_assign->dump();

  const code_assign2t &code = to_code_assign2t(code_assign);
  expr2tc assign_target = code.target;
  expr2tc assign_source = code.source;

  bool use_old_encoding = !options.get_bool_option("z3-slhv");
  if(!use_old_encoding) {
    if(is_symbol2t(assign_target) &&
       is_array_type(assign_target->type) && 
       (to_symbol2t(assign_target).get_symbol_name().compare(valid_ptr_arr_name.as_string()) == 0 ||
        to_symbol2t(assign_target).get_symbol_name().compare(dyn_info_arr_name.as_string()) == 0 ||
        to_symbol2t(assign_target).get_symbol_name().compare(alloc_size_arr_name.as_string()) == 0)) {
          if(
              (to_symbol2t(assign_target).get_symbol_name().compare(alloc_size_arr_name.as_string()) == 0) && 
              is_constant_array_of2t(assign_source)) {
            // used to track valid pointer            
            symbolt allocsize_heap;
            allocsize_heap.name = alloc_size_heap_name;
            allocsize_heap.id = alloc_size_heap_name;
            allocsize_heap.lvalue = true;
            allocsize_heap.type = typet(typet::t_intheap);
            new_context.add(allocsize_heap);
            expr2tc new_lhs = symbol2tc(get_intheap_type(),  allocsize_heap.id);
            expr2tc new_rhs = gen_emp();
            symex_assign(code_assign2tc(new_lhs, new_rhs));
          } 
          return;
        }
  }
  // Sanity check: if the target has zero size, then we've ended up assigning
  // to/from either a C++ POD class with no fields or an empty C struct or
  // union. The rest of the model checker isn't rated for dealing with this
  // concept; perform a NOP.
  /* TODO: either we support empty classes/structs/unions, or we don't. */
  if (is_structure_type(code.target->type))
  {
    const struct_union_data &t2 =
      static_cast<const struct_union_data &>(*code.target->type);

    if (t2.members.empty())
      return;
  }

  expr2tc original_lhs = code.target;
  expr2tc lhs = code.target;
  expr2tc rhs = code.source;
  if (options.get_bool_option("z3-slhv"))
  {
    adapt_to_slhv(lhs);
    adapt_to_slhv(rhs);

    if (is_sideeffect2t(rhs) &&
        to_sideeffect2t(rhs).kind == sideeffect2t::nondet ||
        is_constant_struct2t(rhs))
    {
      symex_stack_sideeffect(lhs, rhs);
      return;
    }
  }

  replace_nondet(lhs);
  replace_nondet(rhs);

  log_debug("SLHV", "after replace : lhs = rhs");
  if (messaget::state.modules.count("SLHV") > 0)
  {
    lhs->dump();
    rhs->dump();
  }

  intrinsic_races_check_dereference(lhs);
  log_debug("SLHV", "dereference lhs write");
  dereference(lhs, dereferencet::WRITE);
  log_debug("SLHV", "dereference rhs read");
  dereference(rhs, dereferencet::READ);
  log_debug("SLHV", "replace lhs");
  replace_dynamic_allocation(lhs);
  log_debug("SLHV", "replace rhs");
  replace_dynamic_allocation(rhs);
  log_debug("SLHV", "replace done");

  log_debug("SLHV", "after deref : lhs = rhs");
  if (messaget::state.modules.count("SLHV") > 0)
  {
    lhs->dump();
    rhs->dump();
  }

  // printf expression that has lhs
  if (is_code_printf2t(rhs))
  {
    log_error("does not support code printf in assign");
    // symex_printf(lhs, rhs);
  }
  if (is_sideeffect2t(rhs))
  {
    // check what symex_mem represent
    const sideeffect2t &effect = to_sideeffect2t(rhs);
    
    if (options.get_bool_option("z3-slhv") &&
        effect.kind != sideeffect2t::malloc &&
        effect.kind != sideeffect2t::alloca)
    {
      log_error("Dot not support this type of sedeffect");
      abort();
    }
    
    switch (effect.kind)
    {
    case sideeffect2t::cpp_new:
    case sideeffect2t::cpp_new_arr:
      symex_cpp_new(lhs, effect);
      break;
    case sideeffect2t::realloc:
      symex_realloc(lhs, effect);
      break;
    case sideeffect2t::malloc:
      symex_malloc(lhs, effect);
      break;
    case sideeffect2t::alloca:
      symex_alloca(lhs, effect);
      break;
    case sideeffect2t::va_arg:
      symex_va_arg(lhs, effect);
      break;
    case sideeffect2t::printf2:
      // do nothing here
      break;
    default:
      assert(0 && "unexpected side effect");
    }

    return;
  }

  bool hidden_ssa = hidden || cur_state->top().hidden;
  if (!hidden_ssa)
  {
    auto const maybe_symbol = get_base_object(lhs);
    if (is_symbol2t(maybe_symbol))
    {
      auto const s = to_symbol2t(maybe_symbol).thename.as_string();
      hidden_ssa |= (s.find('$') != std::string::npos) ||
                    (s.find("__ESBMC_") != std::string::npos);
    }
  }

  guardt g(guard); // NOT the state guard!
  symex_assign_rec(lhs, original_lhs, rhs, expr2tc(), g, hidden_ssa);

  log_debug("SLHV", "xxxxxxxxxxxxxxxxxxxxxx symex assign xxxxxxxxxxxxxxxxxxxxxx");
}

void goto_symext::symex_assign_rec(
  const expr2tc &lhs,
  const expr2tc &full_lhs,
  expr2tc &rhs,
  expr2tc full_rhs,
  guardt &guard,
  const bool hidden)
{
  if (is_symbol2t(lhs))
  {
    if (is_intheap_type(lhs) && is_intheap_type(rhs) && is_symbol2t(rhs))
    {
      const intheap_type2t &lhs_ty = to_intheap_type(lhs->type);
      const intheap_type2t &rhs_ty = to_intheap_type(rhs->type);
      if (lhs_ty.is_alloced)
      {
        if (!rhs_ty.is_alloced ||
            lhs_ty.field_types.size() != rhs_ty.field_types.size())
        {
          log_error("Wrong assignment for heap varaible");
          rhs->dump();
          abort();
        }

        std::string lhs_loc = to_symbol2t(lhs_ty.location).get_symbol_name();
        std::string rhs_loc = to_symbol2t(rhs_ty.location).get_symbol_name();

        // Copy fields of struct/...
        if (lhs_loc != rhs_loc)
        {
          for (unsigned int i = 0; i < lhs_ty.field_types.size(); i++)
          {
            if (lhs_ty.field_types[i] != rhs_ty.field_types[i])
            {
              log_status("Dismatch type info");
              abort();
            }

            expr2tc lhs_field = field_of2tc(lhs_ty.field_types[i], lhs, gen_ulong(i));
            expr2tc rhs_field = field_of2tc(lhs_ty.field_types[i], rhs, gen_ulong(i));

            symex_assign_rec(lhs_field, lhs, rhs_field, rhs, guard, false);
          }
          return;
        }
      }
    }

    log_debug("SLHV", " xxxxxxxxx symex assign symbol xxxxxxxxx ");
    symex_assign_symbol(lhs, full_lhs, rhs, full_rhs, guard, hidden);
    log_debug("SLHV", " xxxxxxxxx symex assign symbol xxxxxxxxx ");
  }
  else if (is_index2t(lhs))
  {
    symex_assign_array(lhs, full_lhs, rhs, full_rhs, guard, hidden);
  }
  else if (is_member2t(lhs))
  {
    symex_assign_member(lhs, full_lhs, rhs, full_rhs, guard, hidden);
  }
  else if (is_if2t(lhs))
  {
    symex_assign_if(lhs, full_lhs, rhs, full_rhs, guard, hidden);
  }
  else if (is_typecast2t(lhs) || is_bitcast2t(lhs))
  {
    symex_assign_typecast(lhs, full_lhs, rhs, full_rhs, guard, hidden);
  }
  else if (is_constant_string2t(lhs) || is_null_object2t(lhs))
  {
    // ignore
  }
  else if (is_byte_extract2t(lhs))
  {
    symex_assign_byte_extract(lhs, full_lhs, rhs, full_rhs, guard, hidden);
  }
  else if (is_concat2t(lhs))
  {
    symex_assign_concat(lhs, full_lhs, rhs, full_rhs, guard, hidden);
  }
  else if (is_constant_struct2t(lhs))
  {
    symex_assign_structure(lhs, full_lhs, rhs, full_rhs, guard, hidden);
  }
  else if (is_extract2t(lhs))
  {
    symex_assign_extract(lhs, full_lhs, rhs, full_rhs, guard, hidden);
  }
  else if (is_bitand2t(lhs))
  {
    symex_assign_bitfield(lhs, full_lhs, rhs, full_rhs, guard, hidden);
  }
  else if (is_field_of2t(lhs))
  {
    symex_assign_fieldof(lhs, full_lhs, rhs, full_rhs, guard, hidden);
  }
  else
  {
    log_error("assignment to {} not handled", get_expr_id(lhs));
    abort();
  }
}

void goto_symext::symex_assign_symbol(
  const expr2tc &lhs,
  const expr2tc &full_lhs,
  expr2tc &rhs,
  expr2tc &full_rhs,
  guardt &guard,
  const bool hidden)
{
  // put assignment guard in rhs
  if (!guard.is_true())
    rhs = if2tc(rhs->type, guard.as_expr(), rhs, lhs);

  expr2tc org_rhs = rhs;

  cur_state->rename(rhs);
  do_simplify(rhs);

  log_debug("SLHV", "after rename rhs");
  if(messaget::state.modules.count("SLHV") > 0) rhs->dump();

  if (!is_nil_expr(full_rhs))
  {
    cur_state->rename(full_rhs);
    do_simplify(full_rhs);
  }

  expr2tc renamed_lhs = lhs;
  cur_state->rename_type(renamed_lhs);

  cur_state->assignment(renamed_lhs, rhs);

  // Special case when the lhs is an array access, we need to get the
  // right symbol for the index
  expr2tc new_lhs = full_lhs;
  if (is_index2t(new_lhs))
    cur_state->rename(to_index2t(new_lhs).index);

  guardt tmp_guard(cur_state->guard);
  tmp_guard.append(guard);

  // do the assignment
  target->assignment(
    tmp_guard.as_expr(),
    renamed_lhs,
    new_lhs,
    rhs,
    full_rhs,
    cur_state->source,
    cur_state->gen_stack_trace(),
    hidden,
    first_loop);
}

void goto_symext::symex_assign_structure(
  const expr2tc &lhs,
  const expr2tc &full_lhs,
  expr2tc &rhs,
  expr2tc &full_rhs,
  guardt &guard,
  const bool hidden)
{
  const struct_type2t &structtype = to_struct_type(lhs->type);
  const constant_struct2t &the_structure = to_constant_struct2t(lhs);

  // Explicitly project lhs fields out of structure, assignment will just undo
  // any member operations. If user is assigning to a structure literal, we
  // will croak after recursing. Otherwise, we are assigning to a re-constituted
  // structure, through dereferencing.
  unsigned int i = 0;
  for (auto const &it : structtype.members)
  {
    const expr2tc &lhs_memb = the_structure.datatype_members[i];
    expr2tc rhs_memb = member2tc(it, rhs, structtype.member_names[i]);
    symex_assign_rec(lhs_memb, full_lhs, rhs_memb, full_rhs, guard, hidden);
    i++;
  }
}

void goto_symext::symex_assign_typecast(
  const expr2tc &lhs,
  const expr2tc &full_lhs,
  expr2tc &rhs,
  expr2tc &full_rhs,
  guardt &guard,
  const bool hidden)
{
  // these may come from dereferencing on the lhs
  assert(lhs->type->type_id == rhs->type->type_id);

  expr2tc rhs_typecasted, from;
  if (is_typecast2t(lhs))
  {
    assert(!is_array_type(lhs));
    assert(!is_vector_type(lhs));

    from = to_typecast2t(lhs).from;
    if (is_struct_type(lhs) && lhs->type != from->type)
    {
      /* See dereference_type_compare() for the conditions allowed here. */

      /* cast only between structs */
      assert(is_struct_type(from));

      /* lhs->type must be a prefix of from->type; the prefix could be empty
       * when it is, e.g., an empty C++ base class of from's type. */
      assert(to_struct_type(migrate_type_back(lhs->type))
               .is_prefix_of(to_struct_type(migrate_type_back(from->type))));

      const struct_union_data &lhs_data = to_struct_type(lhs->type);
      const struct_union_data &from_data = to_struct_type(from->type);

      size_t n = lhs_data.members.size();
      assert(n <= from_data.members.size());

      /* Only the prefix changes, the members untouched by this assignment stay
       * the same. Turn
       *
       *   (struct To)from := rhs
       *
       * into
       *
       *   from := new_rhs
       *
       * The 'new_rhs' is a big nested with2t
       *
       *   WITH (WITH (... (WITH from [? := ?]) ...) [? := ?]) [? := ?]
       *
       * where each element i in [0,n) has the form
       *
       *   WITH src_i [.from_name[i] := (from_type[i])rhs.lhs_name[i]]
       *
       * and where
       * - 'src_i' is the inner element, the initial 'src_0' is 'from';
       * - 'from_type[i]' is the type of the member in the struct type of 'from'
       *   and 'from_name[i]' is its member_name;
       * - 'lhs_name[i]' is the name of the corresponding member in the prefix
       *   type of lhs.
       */
      expr2tc new_rhs = from;
      const std::vector<type2tc> &lhs_type = lhs_data.members;
      const std::vector<irep_idt> &lhs_name = lhs_data.member_names;
      const std::vector<type2tc> &from_type = from_data.members;
      const std::vector<irep_idt> &from_name = from_data.member_names;
      for (size_t i = 0; i < n; i++)
      {
        new_rhs = with2tc(
          from->type,
          new_rhs,
          constant_string2tc(
            array_type2tc(
              get_uint8_type(), gen_ulong(from_name[i].size() + 1), false),
            from_name[i],
            constant_string2t::DEFAULT),
          typecast2tc(from_type[i], member2tc(lhs_type[i], rhs, lhs_name[i])));
      }

      /* XXX fbrausse: do we need to assign from := from in case the lhs->type
       *               is empty? */
      rhs_typecasted = new_rhs;
    }
    else
    {
      /* XXX fbrausse: is this really the semantics?
       * What about
       *
       *   (int)f := 42
       *
       * where f is a symbol of float type? */
      rhs_typecasted = typecast2tc(from->type, rhs);
    }
  }
  else
  {
    from = to_bitcast2t(lhs).from;
    rhs_typecasted = bitcast2tc(from->type, rhs);
  }

  symex_assign_rec(from, full_lhs, rhs_typecasted, full_rhs, guard, hidden);
}

void goto_symext::symex_assign_array(
  const expr2tc &lhs,
  const expr2tc &full_lhs,
  expr2tc &rhs,
  expr2tc &full_rhs,
  guardt &guard,
  const bool hidden)
{
  // lhs must be index operand
  // that takes two operands: the first must be an array
  // the second is the index

  const index2t &index = to_index2t(lhs);

  assert(
    is_array_type(index.source_value) || is_vector_type(index.source_value));

  // turn
  //   a[i]=e
  // into
  //   a'==a WITH [i:=e]

  expr2tc new_rhs = rhs;
  if (new_rhs->type != index.type)
    new_rhs = typecast2tc(index.type, new_rhs);

  new_rhs =
    with2tc(index.source_value->type, index.source_value, index.index, new_rhs);

  symex_assign_rec(
    index.source_value, full_lhs, new_rhs, full_rhs, guard, hidden);
}

void goto_symext::symex_assign_member(
  const expr2tc &lhs,
  const expr2tc &full_lhs,
  expr2tc &rhs,
  expr2tc &full_rhs,
  guardt &guard,
  const bool hidden)
{
  // symbolic execution of a struct member assignment

  // lhs must be member operand
  // that takes one operands, which must be a structure

  const member2t &member = to_member2t(lhs);

  assert(
    is_struct_type(member.source_value) || is_union_type(member.source_value));

  const irep_idt &component_name = member.member;
  expr2tc real_lhs = member.source_value;

  // typecasts involved? C++ does that for inheritance.
  if (is_typecast2t(member.source_value))
  {
    const typecast2t &cast = to_typecast2t(member.source_value);
    if (is_null_object2t(cast.from))
    {
      // ignore
    }
    else
    {
      // remove the type cast, we assume that the member is there
      real_lhs = cast.from;
      assert(is_struct_type(real_lhs) || is_union_type(real_lhs));
    }
  }

  // turn
  //   a.c=e
  // into
  //   a'==a WITH [c:=e]

  type2tc str_type = array_type2tc(
    get_uint8_type(), gen_ulong(component_name.as_string().size() + 1), false);
  expr2tc new_rhs = with2tc(
    real_lhs->type,
    real_lhs,
    constant_string2tc(str_type, component_name, constant_string2t::DEFAULT),
    rhs);

  symex_assign_rec(
    member.source_value, full_lhs, new_rhs, full_rhs, guard, hidden);
}

void goto_symext::symex_assign_if(
  const expr2tc &lhs,
  const expr2tc &full_lhs,
  expr2tc &rhs,
  expr2tc &full_rhs,
  guardt &guard,
  const bool hidden)
{
  // we have (c?a:b)=e;

  // need to copy rhs -- it gets destroyed
  expr2tc rhs_copy = rhs;
  const if2t &ifval = to_if2t(lhs);

  expr2tc cond = ifval.cond;

  guardt old_guard(guard);

  guard.add(cond);
  symex_assign_rec(ifval.true_value, full_lhs, rhs, full_rhs, guard, hidden);
  guard = old_guard;

  expr2tc not_cond = not2tc(cond);
  guard.add(not_cond);
  symex_assign_rec(
    ifval.false_value, full_lhs, rhs_copy, full_rhs, guard, hidden);
  guard = old_guard;
}

void goto_symext::symex_assign_byte_extract(
  const expr2tc &lhs,
  const expr2tc &full_lhs,
  expr2tc &rhs,
  expr2tc &full_rhs,
  guardt &guard,
  const bool hidden)
{
  // we have byte_extract_X(l, b)=r
  // turn into l=byte_update_X(l, b, r)

  // Grief: multi dimensional arrays.
  const byte_extract2t &extract = to_byte_extract2t(lhs);

  if (is_multi_dimensional_array(extract.source_value))
  {
    const array_type2t &arr_type = to_array_type(extract.source_value->type);
    assert(
      !is_multi_dimensional_array(arr_type.subtype) &&
      "Can't currently byte extract through more than two dimensions of "
      "array right now, sorry");
    expr2tc subtype_sz =
      constant_int2tc(index_type2(), type_byte_size(arr_type.subtype));
    expr2tc div = div2tc(index_type2(), extract.source_offset, subtype_sz);
    expr2tc mod = modulus2tc(index_type2(), extract.source_offset, subtype_sz);
    do_simplify(div);
    do_simplify(mod);

    expr2tc idx = index2tc(arr_type.subtype, extract.source_value, div);
    expr2tc be2 =
      byte_update2tc(arr_type.subtype, idx, mod, rhs, extract.big_endian);
    expr2tc store =
      with2tc(extract.source_value->type, extract.source_value, div, be2);
    symex_assign_rec(
      extract.source_value, full_lhs, store, full_rhs, guard, hidden);
  }
  else
  {
    expr2tc new_rhs = byte_update2tc(
      extract.source_value->type,
      extract.source_value,
      extract.source_offset,
      rhs,
      extract.big_endian);

    symex_assign_rec(
      extract.source_value, full_lhs, new_rhs, full_rhs, guard, hidden);
  }
}

void goto_symext::symex_assign_concat(
  const expr2tc &lhs,
  const expr2tc &full_lhs,
  expr2tc &rhs,
  expr2tc &,
  guardt &guard,
  const bool hidden)
{
// Right: generate a series of symex assigns.
#ifndef NDEBUG
  const concat2t &cat = to_concat2t(lhs);
  assert(cat.type->get_width() > 8);
#endif
  assert(is_scalar_type(rhs));

  // Second attempt at this code: byte stitching guarantees that all the concats
  // occur in one large grouping. Produce a list of them.
  std::list<expr2tc> operand_list;
  expr2tc cur_concat = lhs;
  while (is_concat2t(cur_concat))
  {
    const concat2t &cat2 = to_concat2t(cur_concat);
    operand_list.push_back(cat2.side_2);
    cur_concat = cat2.side_1;
  }

  // Add final operand to list
  operand_list.push_back(cur_concat);

#ifndef NDEBUG
  for (auto const &foo : operand_list)
    assert(foo->type->get_width() == 8);
#endif
  assert((operand_list.size() * 8) == cat.type->get_width());

  bool is_big_endian =
    (config.ansi_c.endianess == configt::ansi_ct::IS_BIG_ENDIAN);

  // Pin one set of rhs version numbers: if we assign part of a value to itself,
  // it'll change during the assignment
  cur_state->rename(rhs);

  // Produce a corresponding set of byte extracts from the rhs value. Note that
  // the byte offset is always the same no matter endianness here, any byte
  // order flipping is handled at the smt layer.
  std::list<expr2tc> extracts;
  for (unsigned int i = 0; i < operand_list.size(); i++)
  {
    expr2tc byte =
      byte_extract2tc(get_uint_type(8), rhs, gen_ulong(i), is_big_endian);
    extracts.push_back(byte);
  }

  // Now proceed to pair them up
  assert(extracts.size() == operand_list.size());
  auto lhs_it = operand_list.begin();
  auto rhs_it = extracts.begin();
  while (lhs_it != operand_list.end())
  {
    expr2tc new_rhs = *rhs_it;
    const type2tc &type = (*lhs_it)->type;
    if (new_rhs->type != type)
      new_rhs = typecast2tc(type, new_rhs);
    symex_assign_rec(*lhs_it, full_lhs, new_rhs, rhs, guard, hidden);
    lhs_it++;
    rhs_it++;
  }
}

void goto_symext::symex_assign_extract(
  const expr2tc &lhs,
  const expr2tc &full_lhs,
  expr2tc &rhs,
  expr2tc &full_rhs,
  guardt &guard,
  const bool hidden)
{
  const extract2t &ex = to_extract2t(lhs);
  assert(is_bv_type(ex.from));
  assert(is_bv_type(rhs));
  assert(rhs->type->get_width() == lhs->type->get_width());

  // We need to: read the rest of the bitfield and reconstruct it. Extract
  // and concats are probably the best approach for the solver to optimise for.
  unsigned int bitblob_width = ex.from->type->get_width();
  expr2tc top_part;
  if (ex.upper != bitblob_width - 1)
  {
    // Extract from the top of the blob down to the bit above this extract
    type2tc thetype = get_uint_type(bitblob_width - ex.upper - 1);
    top_part = extract2tc(thetype, ex.from, bitblob_width - 1, ex.upper + 1);
  }

  expr2tc bottom_part;
  if (ex.lower != 0)
  {
    type2tc thetype = get_uint_type(ex.lower);
    bottom_part = extract2tc(thetype, ex.from, ex.lower - 1, 0);
  }

  // We now have two or three parts: accumulate them into a bitblob sized lump
  expr2tc accuml = bottom_part;
  if (!is_nil_expr(accuml))
  {
    type2tc thetype =
      get_uint_type(accuml->type->get_width() + rhs->type->get_width());
    accuml = concat2tc(thetype, rhs, bottom_part);
  }
  else
  {
    accuml = rhs;
  }

  if (!is_nil_expr(top_part))
  {
    assert(
      accuml->type->get_width() + top_part->type->get_width() == bitblob_width);
    type2tc thetype = get_uint_type(bitblob_width);
    accuml = concat2tc(thetype, top_part, accuml);
  }
  else
  {
    assert(accuml->type->get_width() == bitblob_width);
  }

  // OK: accuml now has a bitblob sized expression that can be assigned into
  // the relevant field.
  symex_assign_rec(ex.from, full_lhs, accuml, full_rhs, guard, hidden);
}

void goto_symext::symex_assign_bitfield(
  const expr2tc &lhs,
  const expr2tc &full_lhs,
  expr2tc &rhs,
  expr2tc &full_rhs,
  guardt &guard,
  const bool hidden)
{
  /* Expect to assign values to bitfields. Bitfield values are constructed
   * by dereferencet::extract_bits_from_byte_array(), thus we handle this case:
   *   lhs := rhs
   * where
   *   lhs = (rtype)(val >> shft) & mask
   *   rtype = rhs->type
   *
   * Translate to
   *   val := new_rhs
   * where
   *   new_rhs  = (vtype)val & neg_mask | rhs_shft
   *   neg_mask = ~((vtype)mask << shft)
   *   rhs_shft = (vtype)rhs << shft
   *   vtype    = (unsignedbv of width matching val->type)
   *
   * The new LHS is either an index, a byte_extract or a concat which can be
   * handled recursively
   */

  assert(is_bitand2t(lhs));
  const expr2tc &cast_expr = to_bitand2t(lhs).side_1;
  assert(is_typecast2t(cast_expr));
  const expr2tc &shft_expr = to_typecast2t(cast_expr).from;
  assert(is_lshr2t(shft_expr));
  const expr2tc &val = to_lshr2t(shft_expr).side_1;
  const expr2tc &shft = to_lshr2t(shft_expr).side_2;
  const expr2tc &mask = to_bitand2t(lhs).side_2;

  expr2tc neg_mask, rhs_shft, new_rhs;

  neg_mask = typecast2tc(get_uint_type(val->type->get_width()), mask);
  neg_mask = shl2tc(neg_mask->type, neg_mask, shft);
  neg_mask = bitnot2tc(neg_mask->type, neg_mask);

  rhs_shft = typecast2tc(neg_mask->type, rhs);
  rhs_shft = shl2tc(rhs_shft->type, rhs_shft, shft);

  new_rhs = typecast2tc(neg_mask->type, val);
  new_rhs = bitand2tc(new_rhs->type, new_rhs, neg_mask);
  new_rhs = bitor2tc(new_rhs->type, new_rhs, rhs_shft);
  new_rhs = typecast2tc(val->type, new_rhs);

  return symex_assign_rec(val, full_lhs, new_rhs, full_rhs, guard, hidden);
}

void goto_symext::symex_assign_fieldof(
  const expr2tc &lhs,
  const expr2tc &full_lhs,
  expr2tc &rhs,
  expr2tc &full_rhs,
  guardt &guard,
  const bool hidden)
{
  assert(is_scalar_type(rhs));

  const field_of2t& field_of = to_field_of2t(lhs);
  expr2tc heap_region = field_of.source_heap;
  // make sure it is l1 name
  cur_state->level2.get_original_name(heap_region);
  const expr2tc &field = field_of.operand;

  expr2tc update_heap =
    heap_update2tc(heap_region->type, heap_region, field, rhs);

  symex_assign_rec(heap_region, full_lhs, update_heap, full_rhs, guard, hidden);

  // symex_disj_heaps(heap_region);
}

void goto_symext::replace_nondet(expr2tc &expr)
{
  if (
    is_sideeffect2t(expr) && to_sideeffect2t(expr).kind == sideeffect2t::nondet)
  {

    unsigned int &nondet_count = get_dynamic_counter();
    expr =
      symbol2tc(expr->type, "nondet$symex::nondet" + i2string(nondet_count++));
  }
  else
  {
    expr->Foreach_operand([this](expr2tc &e) {
      if (!is_nil_expr(e))
        replace_nondet(e);
    });
  }
}

void goto_symext::replace_null(expr2tc &expr)
{
  if (is_nil_expr(expr)) return;
  if (is_symbol2t(expr))
  {
    if (to_symbol2t(expr).get_symbol_name() != "NULL") return;
    expr = is_intheap_type(expr) ? gen_emp() : gen_nil();
  }
  else
  {
    expr->Foreach_operand([this](expr2tc &e) {
      if (!is_nil_expr(e))
        replace_null(e);
    });
  }
}

void goto_symext::replace_pointer_airth(expr2tc &expr)
{
  if (is_nil_expr(expr)) return;

  expr->Foreach_operand([this](expr2tc &e) { replace_pointer_airth(e); });

  if (is_add2t(expr) || is_sub2t(expr))
  {
    if (!is_pointer_type(expr) && !is_intloc_type(expr)) return;

    expr2tc side_1 = is_add2t(expr) ? to_add2t(expr).side_1 : to_sub2t(expr).side_1;
    expr2tc side_2 = is_add2t(expr) ? to_add2t(expr).side_2 : to_sub2t(expr).side_2;
    
    if (is_pointer_type(side_2) || is_intloc_type(side_2))
      std::swap(side_1, side_2);
    
    if (is_pointer_type(side_2) || is_intloc_type(side_2))
    {
      log_error("Wrong pointer arithmetic");
      abort();
    }

    if (is_sub2t(expr)) side_2 = neg2tc(side_2->type, side_2);

    expr2tc locadd = locadd2tc(side_1, side_2);
    do_simplify(locadd);

    expr = locadd;
  }

  if (is_member2t(expr))
  {
    // replaced by field_of
    const member2t &member = to_member2t(expr);

    const struct_type2t &struct_type = to_struct_type(member.source_value->type);

    unsigned int idx = struct_type.get_component_number(member.member);
    unsigned int count_pad = 0;
    for (unsigned int i = 0; i < idx; i++)
      count_pad += has_prefix(struct_type.member_names[i], "anon");
    idx -= count_pad;

    expr2tc field = gen_ulong(idx);
    expr = field_of2tc(member.type, member.source_value, field);
  }
}

void goto_symext::replace_address_of(expr2tc &expr)
{
  if (is_nil_expr(expr)) return;

  expr->Foreach_operand([this](expr2tc &e) { replace_address_of(e); });

  if (is_address_of2t(expr))
    expr = location_of2tc(to_address_of2t(expr).ptr_obj);
}

void goto_symext::replace_typecast(expr2tc &expr)
{
  if (is_nil_expr(expr)) return;

  expr->Foreach_operand([this](expr2tc &e) { replace_typecast(e); });

  if (is_typecast2t(expr))
  {
    const typecast2t &typecast = to_typecast2t(expr);

    // Only replace bool typecast
    if ((is_pointer_type(typecast.from) || is_intloc_type(typecast.from))
        && is_bool_type(typecast.type))
      expr = notequal2tc(typecast.from, gen_nil());
  }
}

void goto_symext::replace_tuple(expr2tc &expr)
{
  if (is_nil_expr(expr)) return;

  expr->Foreach_operand([this](expr2tc &e) { replace_tuple(e); });

  if (is_symbol2t(expr) && is_struct_type(expr->type))
  {
    unsigned int bytes = type_byte_size(expr->type, &ns).to_uint64();
    
    // use l1 name and suffix "loc" to create base loc
    expr2tc base_loc = create_heap_region_loc(expr);

    expr->type = create_heap_region_type(expr->type, bytes, base_loc);
    to_intheap_type(expr->type).is_alloced = true;
  }
}


void goto_symext::adapt_to_slhv(expr2tc &expr)
{
  replace_null(expr);
  replace_pointer_airth(expr);
  replace_address_of(expr);
  replace_tuple(expr);
  replace_typecast(expr);
}