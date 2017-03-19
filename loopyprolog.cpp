#include "stdafx.h"
#include "loopyprolog.h"

void intrusive_ptr_add_ref(SimpleRefCount *p)
{
	p->_inc();
}
void intrusive_ptr_release(SimpleRefCount *p)
{
	SimpleRefCount * d = p->_dec();
	if (d) delete d;
}

void intrusive_ptr_add_ref(CombinableRefCount *p)
{
	p->_inc();
}
void intrusive_ptr_release(CombinableRefCount *p)
{
	CombinableRefCount * d = p->_dec();
	if (d) delete d;
	//	delete(p->_dec());
}


#ifdef OWN_MEMORY_MANAGEMENT
intptr_t CombinableRefCount::blocksize = intptr_t((sizeof(CombinableRefCount) + sizeof(CombinableRefCount) - 1)&~((sizeof(CombinableRefCount) + sizeof(CombinableRefCount) - 1) >> 1));
FreeList *CombinableRefCount::free_list = nullptr;
#endif



Trampoline::Trampoline(const CapturedVar< std::function< Trampoline() > > &c) :intrusive_ptr(new Trampoline0<CapturedVar< std::function< Trampoline() > > >(c))
{

}

Trampoline end_search = new NullTrampoline();

char * const TypeNames[] = { "nil","variable","double","string","var","list","custom","extra data type 1","extra data type 2","extra data type 3","extra data type 4" };

#ifdef OWN_MEMORY_MANAGEMENT
intptr_t LVar::blocksize = intptr_t((sizeof(LVar) + sizeof(LVar) - 1)&~((sizeof(LVar) + sizeof(LVar) - 1) >> 1));
FreeList *LVar::free_list = nullptr;
#endif

#ifdef OWN_MEMORY_MANAGEMENT
intptr_t LogicalVariant::blocksize = intptr_t((sizeof(LogicalVariant) + sizeof(LogicalVariant) - 1)&~((sizeof(LogicalVariant) + sizeof(LogicalVariant) - 1) >> 1));
FreeList *LogicalVariant::free_list = nullptr;
#endif


#ifdef OWN_MEMORY_MANAGEMENT
intptr_t LCons::blocksize = intptr_t((sizeof(LCons) + sizeof(LCons) - 1)&~((sizeof(LCons) + sizeof(LCons) - 1) >> 1));
FreeList *LCons::free_list = nullptr;
#endif

const char *LCons::open_paren = "( ";
const char *LCons::close_paren = " )";
const char *LCons::display_dot = " | ";
const char *LCons::display_nil = "()";


ostream & operator<<(ostream & os, const LVar &v)
//ostream & operator<<(ostream & os, const LogicalVariant &v)
{
	switch (v.type()) {
	case LV_UNINSTANCIATED:
		os << "Var" << &v;
		break;
	case LV_LVAR:
#ifdef DISPLAY_LVAR_LINKS
		os << "->(" << &v->value << ')' << v->value;
#else
		os << v->value;
#endif
		break;
	case LV_NIL:
		os << LCons::display_nil;
		break;
	case LV_LIST:
		//boost::get<intrusive_ptr<LCons> >(cdr.get_target()->value)->_out_rest(os);
		/*
		__tostring=function (self)
		if nullp(self) then return display_nil
		elseif nullp(logical_get(self[2])) then return open_paren .. tostring(logical_get(self[1])) .. close_paren
		elseif listp(logical_get(self[2])) then return open_paren .. tostring(logical_get(self[1])) .. logical_get(self[2]):rest_tostring()
		else return open_paren .. tostring(logical_get(self[1])) .. display_dot .. tostring(self[2]) ..close_paren
		end
		end
		*/
	{
		intrusive_ptr<LCons> l = boost::get<intrusive_ptr<LCons> >(v->value);
		if (l->cdr.nullp()) os << LCons::open_paren << l->car.get_target() << LCons::close_paren;
		else if (l->cdr.listp()) {
			os << LCons::open_paren << l->car.get_target();
			return boost::get<intrusive_ptr<LCons> >(l->cdr.get_target()->value)->_out_rest(os);
		}
		else os << LCons::open_paren << l->car.get_target() << LCons::display_dot << l->cdr.get_target() << LCons::close_paren;
	}
	break;
	default:
#ifdef DISPLAY_LVAR_LINKS
		os << "[" << &v->value << ']' << v->value;
#else
		os << v->value;
#endif
	}
	return os;
}

bool strict_equals(LVar &a, LVar &b)
{
	return  boost::apply_visitor(are_strict_equals(), a->value, b->value);
}

Trampoline _restore_unified(Search &s, LVar a_save, LValue restore_a)
{
	a_save->value = restore_a;
	return s.fail();
}

bool _unify(Search &s, LVar &a, LVar&b)
{
	if (strict_equals(a, b)) return true;
	LVar a_target = a.get_target();
	LVar b_target = b.get_target();
	if (strict_equals(a_target, b_target)) return true; //test strict equals on uninstanciated {}{}{}
	if (a_target.uninstanciatedp() && b_target.uninstanciatedp())
	{
		LValue restore_a = a_target.get()->value;
		a_target.chain(b_target);
		s.save_undo(trampoline(_restore_unified, s, a_target, restore_a));
		return true;
	}
	else if (a_target.uninstanciatedp()) {
		LValue restore_a = a_target.get()->value;
		a_target->value = b_target.get()->value;
		s.save_undo(trampoline(_restore_unified, s, a_target, restore_a));
		return true;
	}
	else if (b_target.uninstanciatedp()) {
		LValue restore_b = b_target.get()->value;
		b_target->value = a_target.get()->value;
		s.save_undo(trampoline(_restore_unified, s, b_target, restore_b));
		return true;
	}
	if (a_target.pairp() && b_target.pairp())
	{
		if (!_unify(s, a_target.car(), b_target.car())) return false;
		return _unify(s, a_target.cdr(), b_target.cdr());
	}
	return false;
}

bool _identical(LVar &a, LVar&b)
{
	if (strict_equals(a, b)) return true;
	LVar a_target = a.get_target();
	LVar b_target = b.get_target();
	if (strict_equals(a_target, b_target)) return true; //test strict equals on uninstanciated {}{}{}
	if (a_target.pairp() && b_target.pairp())
	{
		if (!_identical(a_target.car(), b_target.car())) return false;
		return _identical(a_target.cdr(), b_target.cdr());
	}
	return false;
}

intrusive_ptr<LCons> LVar::as_LCons() {
	return boost::get<intrusive_ptr<LCons> >(get_target()->value);
}
double LVar::as_double() {
	return boost::get<double>(get_target()->value);
}
InternedString LVar::as_IString() {
	return boost::get<InternedString>(get_target()->value);
}
intrusive_ptr<LogicalData> LVar::as_LogicalValue() {
	return boost::get<intrusive_ptr<LogicalData> >(get_target()->value);
}
LVar LVar::car() { return as_LCons()->car.get_target(); }
LVar LVar::cdr() { return as_LCons()->cdr.get_target(); }

void LVar::set_car(LVar &t) { return as_LCons()->car.get_target() = t; }
void LVar::set_cdr(LVar &t) { return as_LCons()->cdr.get_target() = t; }

LVar& LVar::get_target()
{
	LVar *t = this;
	//	GetAddress get_address;
	while ((*t).type() == LV_LVAR) t = &boost::get<LVar>((*t)->value);
	return *t;
}

LogicalVariant NilVariant(NIL);

LVType LVar::type() const
{
	LVType t = (LVType)(*this)->value.which();
	if (t == LV_CUSTOM) return boost::get<intrusive_ptr<LogicalData>&>((*this)->value)->class_type;
	return t;
}
