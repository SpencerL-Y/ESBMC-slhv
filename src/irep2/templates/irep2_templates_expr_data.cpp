/**
 * @author Term SELO
 * @brief Modified by from ESBMC
 */

#include <irep2/irep2_templates_expr.h>

expr_typedefs1(constant_int, constant_int_data);
expr_typedefs1(constant_fixedbv, constant_fixedbv_data);
expr_typedefs1(constant_floatbv, constant_floatbv_data);
expr_typedefs1(constant_struct, constant_datatype_data);
expr_typedefs2(constant_union, constant_union_data);
expr_typedefs1(constant_array, constant_datatype_data);
expr_typedefs1(constant_vector, constant_datatype_data);
expr_typedefs1(constant_bool, constant_bool_data);
expr_typedefs1(constant_array_of, constant_array_of_data);
expr_typedefs2(constant_string, constant_string_data);
expr_typedefs6(symbol, symbol_data);
expr_typedefs2(nearbyint, typecast_data);
expr_typedefs2(typecast, typecast_data);
expr_typedefs2(bitcast, bitcast_data);
expr_typedefs3(if, if_data);
expr_typedefs2(equality, relation_data);
expr_typedefs2(notequal, relation_data);
expr_typedefs2(lessthan, relation_data);
expr_typedefs2(greaterthan, relation_data);
expr_typedefs2(lessthanequal, relation_data);
expr_typedefs2(greaterthanequal, relation_data);
expr_typedefs2(same_object, same_object_data);
expr_typedefs3(byte_extract, byte_extract_data);
expr_typedefs4(byte_update, byte_update_data);
expr_typedefs3(with, with_data);
expr_typedefs2(member, member_data);
expr_typedefs2(index, index_data);
expr_typedefs2(overflow_cast, overflow_cast_data);
expr_typedefs3(dynamic_object, dynamic_object_data);
expr_typedefs2(dereference, dereference_data);
expr_typedefs5(sideeffect, sideeffect_data);
expr_typedefs1(code_block, code_block_data);
expr_typedefs2(code_assign, code_assign_data);
expr_typedefs2(code_init, code_assign_data);
expr_typedefs1(code_decl, code_decl_data);
expr_typedefs1(code_dead, code_decl_data);
expr_typedefs1(code_printf, code_printf_data);
expr_typedefs1(code_expression, code_expression_data);
expr_typedefs1(code_return, code_expression_data);
expr_typedefs_empty(code_skip, expr2t);
expr_typedefs1(code_free, code_expression_data);
expr_typedefs1(code_goto, code_goto_data);
expr_typedefs3(object_descriptor, object_desc_data);
expr_typedefs3(code_function_call, code_funccall_data);
expr_typedefs2(code_comma, code_comma_data);
expr_typedefs1(code_asm, code_asm_data);
expr_typedefs1(code_cpp_del_array, code_expression_data);
expr_typedefs1(code_cpp_delete, code_expression_data);
expr_typedefs1(code_cpp_catch, code_cpp_catch_data);
expr_typedefs2(code_cpp_throw, code_cpp_throw_data);
expr_typedefs2(code_cpp_throw_decl, code_cpp_throw_decl_data);
expr_typedefs1(code_cpp_throw_decl_end, code_cpp_throw_decl_data);
expr_typedefs3(extract, extract_data);


expr_typedefs2(constant_intloc, constant_intloc_data);
expr_typedefs1(constant_intheap, constant_intheap_data);
expr_typedefs1(constant_heap_region, constant_datatype_data);
expr_typedefs1(disjh, disjh_data);
expr_typedefs2(points_to, points_to_data);
expr_typedefs1(uplus, uplus_data);
expr_typedefs2(locadd, locadd_data);
expr_typedefs2(heap_region, heap_region_data);