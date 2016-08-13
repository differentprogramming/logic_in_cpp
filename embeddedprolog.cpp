// embeddedprolog.cpp : Defines the entry point for the console application.
//
#include "stdafx.h"
#include <stdexcept>
#include <utility>
#include <boost/any.hpp>
#include <vector>
#include <boost/regex.hpp>
#include <boost/variant.hpp>
#include <boost/variant/recursive_variant.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/flyweight.hpp>
#include <iostream>
#include <string>
#include <functional>
#include <map>

using std::ostream;
using std::cout;
using std::endl;
using std::string;

using boost::intrusive_ref_counter; 
using boost::intrusive_ptr;
using std::function;

class LogicalVariant;
class LCons;
class LVar;

enum UninstanciatedType { UNINSTANCIATED };
enum NilType { NIL };

inline ostream & operator<<(ostream & os, const NilType & )
{
	os << "Nil";
	return os;
}

inline ostream & operator<<(ostream & os, const UninstanciatedType &)
{
	os << "Uninstanciated";
	return os;
}

//for testing with .which()
enum LVType {
	LV_NIL,LV_UNINSTANCIATED,LV_DOUBLE,LV_STRING,LV_LVAR,LV_LIST,LV_CUSTOM,LV_DATA1,LV_DATA2,LV_DATA3,LV_DATA4
};

char * const TypeNames[] = { "nil","variable","double","string","var","list","custom","extra data type 1","extra data type 2","extra data type 3","extra data type 4" };

//to allow custom logical types
class LogicalData :public intrusive_ref_counter<LogicalData, boost::thread_unsafe_counter >
{
public:
	LVType class_type;
	void *data;
	LogicalData(LVType t, void *d) :class_type(t),data(d) {}
};


typedef boost::flyweight<string> InternedString;


/*
Note: difference from Lua version
boost::variant treats its values as value types, it's not possible to get a reference to the stored value, only a copy
so when you instanciate an LVar it's not the LVar that can be shared, it's the LVariant inside it. You can't instanciate by 
setting anLVar = aValue you have to do it as anLVar->value = aValue!
Anyway the point of LVar in the C++ version is to allow reference counting and simple initialization.
All the extra levels of indirection are just ways of dealing with C++ limitations consider an LVar to just be a variable and 
the LVariant/LValue to be the logical variable inside it.
*/

typedef boost::variant <
	NilType
	, UninstanciatedType
	, double
	, InternedString
	, LVar
	, intrusive_ptr<LCons>
	, intrusive_ptr<LogicalData>
> LValue;

//Note, in this program LVars are never to hold NULL 
//rather they can hold a LogicalVariant with the type LV_NIL
//The usual initialization is to be:  LVar aVariable(LInit()) 
//that initializes the variable to UNINSTANCIATED
class LVar : public boost::intrusive_ptr<LogicalVariant>
{
public:
	LVar();
	LVar(NilType);
	LVar(UninstanciatedType);
	LVar(const char * c);
	LVar(double d);
	//copy constructor, not chaining
	LVar(const LVar &v);
	LVar(LogicalVariant *v);
	LVar(LValue v);
	LVar(InternedString s);
	LVar(LCons *);
	LVar(intrusive_ptr<LCons>&);
	void chain(LVar&o);
	LVar& get_target();
	LVType target_type();
	bool nullp() { return target_type() == LV_NIL; }
	bool listp() { LVType t = target_type(); return t == LV_NIL || t==LV_LIST; }
	bool pairp() { return target_type() == LV_LIST; }

//	LV_NIL,LV_UNINSTANCIATED,LV_DOUBLE,LV_STRING,LV_LVAR,LV_LIST,LV_CUSTOM,LV_DATA1,LV_DATA2,LV_DATA3,LV_DATA4
	LVType type() const;
	bool uninstanciatedp() { return type() == LV_UNINSTANCIATED; }
	bool doublep() { return type() == LV_DOUBLE; }
	bool stringp() { return type() == LV_STRING; }
	bool lvarp() { return type() == LV_LVAR; }

	bool ground()  { 
		LVType t = type();
		if (t == LV_LIST) return car().ground() && cdr().ground();
		return t != LV_UNINSTANCIATED; 
	}
	intrusive_ptr<LCons> as_LCons();
	double as_double();
	InternedString as_IString();
	intrusive_ptr<LogicalData> as_LogicalValue();
	LVar car();
	LVar cdr();
};



class LogicalVariant:public intrusive_ref_counter<LogicalVariant, boost::thread_unsafe_counter>
{
public:
	LogicalVariant() :value(UNINSTANCIATED) {}
	LogicalVariant(LValue v) :value(v) {}
	LogicalVariant(LogicalVariant &v) :value(v.value) {}
	LogicalVariant(LVar  &v) :value(v) {}
	LogicalVariant(LCons *c) :value(intrusive_ptr<LCons>(c)) {}
	LogicalVariant(boost::intrusive_ptr<LCons>&c) :value(c) {}

	LValue value;
};

inline LVar::LVar() : intrusive_ptr<LogicalVariant>(new LogicalVariant(UNINSTANCIATED)) { }
inline LVar::LVar(NilType) : intrusive_ptr<LogicalVariant>(new LogicalVariant(NIL)) { }
inline LVar::LVar(LValue v) : intrusive_ptr<LogicalVariant>(new LogicalVariant(v)) { }
inline LVar::LVar(UninstanciatedType) : intrusive_ptr<LogicalVariant>(new LogicalVariant(UNINSTANCIATED)) { }
inline LVar::LVar(const char * c): intrusive_ptr<LogicalVariant>(new LogicalVariant(InternedString(c))){}
inline LVar::LVar(double d): intrusive_ptr<LogicalVariant>(new LogicalVariant(d)){}
//Note it's not safe to make this chain because it's used as a copy constructor
inline LVar::LVar(const LVar &v) : intrusive_ptr<LogicalVariant>(v) {}
inline LVar::LVar(LogicalVariant *v) :intrusive_ptr<LogicalVariant>(v) {}
inline LVar::LVar(InternedString s) : intrusive_ptr<LogicalVariant>(new LogicalVariant(s)) {}
inline void LVar::chain(LVar &o) { (*this)->value = o; }
inline LVType LVar::target_type() { return get_target().type(); }
LVType LVar::type() const
{
	LVType t = (LVType)(*this)->value.which();
	if (t == LV_CUSTOM) return boost::get<intrusive_ptr<LogicalData>&>((*this)->value)->class_type;
	return t;
}


struct GetAddress : public boost::static_visitor<>
{
	void *address;
	template <typename T>
	void operator()(T &t) const { address = &t; }
};

;
LVar& LVar::get_target()
{
	LVar *t = this;
//	GetAddress get_address;
	while ((*t).type() == LV_LVAR) t = &boost::get<LVar>((*t)->value);
	return *t;
}

inline LogicalVariant * LInit()
{
	return new LogicalVariant();
}

//ostream & operator<<(ostream & os, const LogicalVariant &v);
ostream & operator<<(ostream & os, const LVar &v);



struct DotHolder
{
	LVar cdr;
};

class LCons :public intrusive_ref_counter<LCons, boost::thread_unsafe_counter>
{

public:
	static const char *open_paren; 
	static const char *close_paren;
	static const char *display_dot;
	static const char *display_nil;
	ostream & _out_rest(ostream & os)
	{
/*
if (nullp(logical_get(self[2]))) then return ' ' .. tostring(logical_get(self[1])) .. close_paren
elseif (listp(logical_get(self[2]))) then return ' ' .. tostring(logical_get(self[1])) .. logical_get(self[2]):rest_tostring()
else return ' ' .. tostring(logical_get(self[1])) .. display_dot .. tostring(self[2]) ..close_paren
end
*/
		if (cdr.nullp()) os << " " << car.get_target() << close_paren;
		else if (cdr.listp()) {
			os << " "
				<< car.get_target();
			return cdr.as_LCons()->_out_rest(os);
		}
		else os << " " << car.get_target() << display_dot << cdr.get_target() << close_paren;
		return os;
	}
	//allocating a new LogicalVariant for NIL allows the cons to be mutable
	//Maybe we'd prefer immutable
	LCons(LValue first):car(new LogicalVariant(first)), cdr(new LogicalVariant(NIL)) {}
	LCons(LValue first, LValue rest) :car(new LogicalVariant(first)), cdr(new LogicalVariant(rest)) {}
	//LCons(LVar& first, DotHolder rest) :car(first), cdr(rest.cdr) {}
	LCons(LVar && first, DotHolder && rest) :car(first), cdr(rest.cdr.car()) {}
	LCons(LVar& first) :car(first), cdr(new LogicalVariant(NIL)) {}
	LCons(LVar& first, LVar& rest) :car(first), cdr(rest) {}
	LCons(LValue first, LVar& rest) :car(new LogicalVariant(first)), cdr(rest) {}
	LCons(LVar& first, LValue rest) :car(first), cdr(new LogicalVariant(rest)) {}
	
	//Note there is an extra level of indirection here so that there doesn't have to be
	//some complicated plumbing for the garbage collection.  And remember LVars should never be NULL
	LVar car;
	LVar cdr;
};

inline LVar::LVar(LCons *c):intrusive_ptr<LogicalVariant>(new LogicalVariant(c)) {}
inline LVar::LVar(intrusive_ptr<LCons> &c) :intrusive_ptr<LogicalVariant>(new LogicalVariant(c)) {}
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
		os << "->(" << &v->value << ')' << v->value;
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
		os << "[" << &v->value << ']' << v->value;
	}
	return os;
}


typedef std::vector<boost::any> AnyValues;

enum DotType { DOT };
LogicalVariant NilVariant(NIL);
template<typename ... TYPES>
LVar L()
{
	return NIL; 
}

template<typename T, typename ... TYPES>
LVar L(T a, TYPES ... rest)
{
	return LVar(intrusive_ptr<LCons>(new LCons(LVar(a), L(rest ...))));
}

template<typename ... TYPES>
DotHolder L(DotType, TYPES ... rest)
{
	return DotHolder{ L(rest ...) };
}

//Much to my surprise Search in C++ is only 1/3 more lines than the lua version. 
class Search;
//Note: the search parameter only seems necessary in the final continuation that 
//reports when the search has failed, the rest of the time it could be captured... but 
//it's simpler to pass than capture especially since other values are captured by value
//and this one would have to be captured by reference. 
//The other times it has to be passed are in direct calls not continuations
typedef std::function<void(Search &)> Continuation;

template<typename T>
class CapturedVar;


/* CombinableRefCount is a replacement for intrusive_ref_counter<_, boost::thread_unsafe_counter >
 * With the difference that you can combine a bunches of them to share a reference counter.
 * The point of that is to handle the case where a bunch of objects create a cycle of references.
 * Note it is assumed that the organization of this cycle is immutable.
 * Then by combining the counts, a reference to one object is considered a reference to all for the 
 * sake of refernce counting.  Only when there are no external links to all of the objects will they
 * be collected.
 * Note it is assumed that the cycle of references either is special cased to not cause the counter
 * to increment, or that all of those increments have been manually decremented out.
 * The class is hard to understand because CombinableRefCount is used in two separate ways by the
 * algorithm and it's just punning that the same code works for both.
 * The main way is that CombinableRefCount is subclassed.  These subclasses can be used just like 
 * subclasses of boost::intrusive_ref_counter.
 * However, if combine_refs is called on a list of CombinableRefCount* (or on a list of CapturedVar<T> holding
 * CapturedVarLetter<T> derived from  CombinableRefCount) then a single CombinableRefCount is allocated
 * to hold the combined reference count for all those objects.  Note that this CombinableRefCount is 
 * just the raw type, not a subclass.
 * For the first kind, the subclassed version, _forward points at the shared count if there is one
 * and _next makes a list to facilitate deleting the whole set at once.
 * For the second kind, the shared count, _next points to the head of the list of shared objects
 * and _forward isn't used.
 */
class CombinableRefCount
{

public:
	int _ref;
	CombinableRefCount *_next;//in union head , otherwise next member of union
	CombinableRefCount *_forward;//in union end of list, otherwise not used


	int use_count() const { if (_forward) return _forward->_ref; else return _ref; }
	void _inc() { if (_forward) ++_forward->_ref; else ++_ref; }
	CombinableRefCount * _dec() {
		if (_forward) {
			if (0 == --_forward->_ref) return _forward;
		}
		else {
			if (0 == --_ref) return this;
		}
		return nullptr;
	}
	CombinableRefCount() :_ref(0), _next(nullptr), _forward(nullptr)  {}
	virtual ~CombinableRefCount()
	{
		if (_next) delete _next;
	}
};

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

CombinableRefCount * combine_refs()
{
	return new CombinableRefCount;
}

template<typename ... Types>
CombinableRefCount * combine_refs(CombinableRefCount *first, Types ... rest)
{
	CombinableRefCount *u = combine_refs(rest...);
	first->_forward = u;
	u->_ref += first->_ref;
	first->_next = u->_next;
	u->_next = first;
	return u;
}



template <typename T>
class CapturedVarLetter :public CombinableRefCount
{
public:
	T value;
	CapturedVarLetter(const T& a) :value(a) {}
	CapturedVarLetter() {}
	T& operator *() { return value; }
	T* operator ->() { return &value; }
};

/* CapturedVar has two uses
 * it can be used inside of lambdas to give the variable capture semantics of other language ie:
 * 1) variables are captured by reference but
 * 2) the lifetime of captured variables is controlled by garbage collection - even if the original variable goes out of scope
 * the variable exists as long as there is a lambda that references it.
 * 3) note that this garbage collection is not thread safe - I decided that speed is more important, do not share lambdas that hold
 * CapturedVar across threads
 *
 * The other use (for CapturedCont) is to speed up copying std::function objects that would otherwise require copying a heap object
 * on each copy.  Incrementing and decrementing the refernce counter is faster than calling new and delete.
 *
 * A weirdness with CapturedVar<T> where T is a std function type such as CapturedCont is that while lambdas can be stored in 
 * std::function<> types you can't match the type of a lambda to automatically convert to std::function as a result I couldn't give
 * CapturedCont a shortcut assignment such as "foo=[=](){...};", instead you have to use * to expose the std::function inside and assign 
 * to that.  Ie it's "CapturedCont foo; *foo=[=](){...};"  The difference is the "*" 
 * Avoid assigning to CapturedVar<T> without the *.
 * 
 * Note, there's a bug/complication with the use of reference counters here.  
 * Lambdas that are held in CapturedConts that can form cycles of ownership will never be collected.
 * In normal programs that would rarely come up, but I'm afraid that in logic style programming it will come up quite often, so
 * there's a memory leak unless a somewhat complicated fix is used.
 * The fix is:
 * Any CapturedCont that's part of a cycle (even a self reference) needs a shadow ptr variable thus:
 * UncountedCont foo_uncounted = foo;
 * and inside the lambdas use foo_uncounted everywhere you would have used foo. foo_uncounted can convert to CapturedCont as needed, say to pass
 * as a parameter.  
 * 
 * For all cycles of more than one CapturedCont you have to call combine_refs on the set like so:
 * CapturedCont foo,bar,baz;
 * UncountedCont foo_uncounted = foo,bar_uncounted=bar,baz_uncounted=baz;
 * combine_refs(foo,bar,baz);
 * and inside of the lambdas that foo,bar and baz are set to use foo_uncounted, bar_uncounted and baz_uncounted instead of foo,bar&baz
 * Having done that incantation, the problem of circular references of CapturedConts is solved.
 *
 * What combine_refs does combine the reference count of the objects it refers to so that for the sake of garbage collection the
 * whole set is managed as a single object. A reference to one is a reference to all.
 */

template <typename T>
class UncountedVar;


template<typename T>
class CapturedVar : public intrusive_ptr< CapturedVarLetter<T> >
{
public:
	CapturedVar(const CapturedVarLetter<T>& v) :intrusive_ptr(&const_cast<CapturedVarLetter<T>&>(v)) 
	{
	
	}
	CapturedVar(const T& v) :intrusive_ptr(new CapturedVarLetter<T>(v)) {}
	CapturedVar() :intrusive_ptr(new CapturedVarLetter<T>()) {}

	CapturedVar(const CapturedVar<T> &o) :intrusive_ptr(static_cast<const intrusive_ptr< CapturedVarLetter<T> > &>(o)) {}
	CapturedVar(const UncountedVar<T> &o);

	CapturedVarLetter<T> & internal() {
		return *get();
	}

	T& operator *() { return get()->value; }
	T& operator *() const { return get()->value; }
};


typedef CapturedVar<Continuation> CapturedCont;
//typedef CapturedVarLetter<Continuation> *UncountedCont;
template <typename T>
class UncountedVar
{
public:
	CapturedVarLetter<T> * value;
	class UncountedVar(const CapturedVar<T> &c) :value(c.get()) {}
	CapturedVarLetter<T> & operator *() const { return *value; }
	CapturedVarLetter<T> * operator->() const { return value;  }
};

typedef UncountedVar<Continuation> UncountedCont;

template<typename T>
CapturedVar<T>::CapturedVar(const UncountedVar<T> &o) :intrusive_ptr(o.value) {}


template<typename T,typename ... Types>
CombinableRefCount * combine_refs(const CapturedVar<T> &_first, Types ... rest)
{
	CombinableRefCount *first = _first.get();
	CombinableRefCount *u = combine_refs(rest...);
	first->_forward = u;
	u->_ref += first->_ref;
	first->_next = u->_next;
	u->_next = first;
	return u;
}


struct CleanStackException
{
};

const int STACK_SAVER_DEPTH = 30;


class Search
{
	enum AmbTag { AMB_UNDO, AMB_ALT };
	struct AmbRecord {
		AmbTag tag;
		CapturedCont cont;
	};
	std::vector<AmbRecord> amblist;

	static const CapturedCont captured_fail;

	static void fail_fn(Search &s)
	{
		s.failed = true;
		s.save_undo(captured_fail);
	}

	void new_amblist()
	{
		failed = false;
		started = false;
		amblist.erase(amblist.begin(), amblist.end());
		amblist.push_back(AmbRecord{ AMB_UNDO,captured_fail});
	}
	bool started;
	int stack_saver;
	CapturedCont cont;
	bool failed;
	//used by friend function fail() ie, lets you use fail as a continuation 
	CapturedCont _for_fail()
	{
		CapturedCont c = amblist.back().cont;
		amblist.pop_back();
		return c;
	}
	void start() { tail(initial); }
public:
	CapturedCont initial;
	std::map<const char *,boost::any> results;
	friend void fail(Search &s);
	bool running() { return !failed;  }
	void save_undo(const CapturedCont &c) { amblist.push_back(AmbRecord{AMB_UNDO,c}); }
	//save_undo is only called from unify so I don't think we need this conversion
	void save_undo(const UncountedCont &c) { amblist.push_back(AmbRecord{ AMB_UNDO,CapturedCont(c) }); }
	void alt(const CapturedCont &c) { amblist.push_back(AmbRecord{ AMB_ALT,c }); }
	void alt(const UncountedCont &c) { amblist.push_back(AmbRecord{ AMB_ALT,CapturedCont(c) }); }

	void tail(const CapturedCont &c)
	{
		if (--stack_saver < 1) {
			cont = c;
			throw(CleanStackException());
		}
		(*c)(*this);
	}
	void tail(const Continuation &c)
	{
		if (--stack_saver < 1) {
			cont = c;
			throw(CleanStackException());
		}
		c(*this);
	}
	void tail(const UncountedCont &c)
	{
		if (--stack_saver < 1) {
			cont = *c;
			throw(CleanStackException());
		}
		(c->value)(*this);

	}

	//Note this can throw a CleanStackException so only call in tail position
	//you can use the fail() function defined below the class as a continuation
	//instead
	void fail() {
		CapturedCont c = amblist.back().cont;
		amblist.pop_back();
		tail(c); //tail call
	}
	int snip_start() { return (int)amblist.size()-1; }
	void snip(int pos) {
		std::vector<AmbRecord> temp;
		for (int i = (int)amblist.size() - 1; i > pos;--i) {
			if (amblist.back().tag == AMB_UNDO) {
				temp.push_back(amblist.back());
			}
			amblist.pop_back();
		}
		while (!temp.empty()) {
			amblist.push_back(temp.back());
			temp.pop_back();
		}
	}

	void reset()
	{
		new_amblist();
	}
	Search(Continuation i) :initial(i)
	{
		new_amblist();
	}
	
	~Search() {}
	bool operator() ()
	{
		bool retry = false;
		stack_saver = STACK_SAVER_DEPTH;
		try {
			if (!started) {
				started = true;
				failed = false;
				start();
			}
			else fail();
		}
		catch (CleanStackException &) {
			stack_saver = STACK_SAVER_DEPTH;
			retry = true;
		}
		while (retry) {
			retry = false;
			try {
				(*cont)(*this);
			}
			catch (CleanStackException &) {
				stack_saver = STACK_SAVER_DEPTH;
				retry = true;
			}
		}
		return !failed;
	}
};
//allows you to use failure as a continuation
void fail(Search &s)
{
	s.tail(s._for_fail());
}



const CapturedCont Search::captured_fail = (Continuation)fail_fn;


//oops, the return value could be nixed by stack clean exception
//but it worked when I made it always throw... {}{}{} WHY DOES IT WORK?
//OH it works because it doesn't use the search until AFTER it returns the value
CapturedCont stream1(CapturedVar<int> m, CapturedCont c)
{
	CapturedVar<int> n(0);
	CapturedCont rest;
	UncountedCont rest_uncounted = rest;


	*rest = [=](Search &s)
	{ 
		*n = *n + 1;
		if (*n == 10) {
			s.fail();
		}
		else {
			s.alt(rest_uncounted);
//			cout << "n is " << *n << endl;
			*m = *n;
			s.tail(c);
		}
	};
	cout << rest->use_count() << endl;
	return rest;
}


//Note it's probably cheaper to pass a CapturedCont than a Continuation
CapturedCont stream2(CapturedVar<int> m, CapturedCont c)
{
	CapturedVar<int> n(0);
	CapturedCont rest;
	UncountedCont rest_uncounted = rest;

	*rest = [=](Search &s)
	{
		*n += 1;
		if (*n == 4) {
			s.fail();
		}
		else {
			s.alt(rest_uncounted);
//			cout << "m is " << *n * *n << endl;
			*m = *n * *n;
			s.tail(c);
		}
	};
	return rest;
}

void AmbTest(Search &s)
{
	CapturedVar<int> n, m;
	CapturedCont c1, c2, c3;
	UncountedCont c1_u = c1, c2_u = c2, c3_u = c3;
	combine_refs(c1, c2, c3);

	//note it can't safely use Search inside of functions that return a value
	*c1 = [=](Search &s) { s.tail(stream1(n,c2_u)); };
	*c2 = [=](Search &s) { s.tail(stream2(m,c3_u)); };
	*c3 = [=](Search &s)
	{
		if (*n != *m) s.fail();
		else {
			s.results.insert_or_assign("n", *n);
			s.results.insert_or_assign("m", *m);
		}
	};
	cout << c1->use_count() << endl;
	cout << c2->use_count() << endl;
	cout << c3->use_count() << endl;
	s.tail(c1);
}

#define OUT_OS_TYPE(TYPE) if (v.type() == typeid(TYPE)) { os << boost::any_cast<TYPE>(v); } else
inline std::ostream & operator<<(std::ostream & os, const boost::any &v)
{
	OUT_OS_TYPE(int)
	OUT_OS_TYPE(double)
	OUT_OS_TYPE(std::string)
	OUT_OS_TYPE(const char *)
	OUT_OS_TYPE(LVar)
//	OUT_OS_TYPE(LogicalVariant)
//	OUT_OS_TYPE(LValue)
	{
		os << "[unhandled type]";
	}
	return os;
}
#undef OUT_OS_TYPE

char Hello[] = "Hello";
int main()
{
	LVar A(NIL);
	LVar B(UNINSTANCIATED);
	LVar C("hello");
	LVar D;
	LVar E; // note that would share the value not chain E(D);
	LVar F; // F(E);
	F.chain(E);
	E.chain(D);
	F.get_target() = C.get_target();

	std::cout << (LValue(InternedString("Hello")) == LValue(InternedString(Hello))) << std::endl;
	std::cout << (LValue(InternedString("Hello")) == LValue(55)) << std::endl;
	std::cout << (LValue(55) == LValue(55)) << std::endl;
	std::cout << TypeNames[D.type()] << std::endl;
	std::cout << A <<' '<< B << ' ' << C << ' ' << D<<' '<<E<<' '<<F<<' '<<&F.get_target() << std::endl;
	LVar M = L(1, DOT, 2);
	std::cout << L("hello", 1, "Laurie") << L(1, L(2, 3), 4) << M << std::endl;

	Search s(AmbTest);
	while (s()) {
		std::cout << "n = " << s.results["n"] << " m = " << s.results["m"] << std::endl;
	}
	s.reset();
	cout << "run a second time" << endl;
	while (s()) {
		std::cout << "n = " << s.results["n"] << " m = " << s.results["m"] << std::endl;
	}

	
	char temp[100];
	std::cin >> temp;
}
