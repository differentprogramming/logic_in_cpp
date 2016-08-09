// embeddedprolog.cpp : Defines the entry point for the console application.
//
#include "stdafx.h"
#include <stdexcept>
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
	LVar(char * const c);
	LVar(double d);
	//copy constructor, not chaining
	LVar(const LVar &v);
	LVar(LogicalVariant *v);
	LVar(InternedString s);
	LVar(LCons *);
	LVar(intrusive_ptr<LCons>&);
	void chain(LVar&o);
	LValue& get_target();
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
	LVType type() const 
	{
		LVType t = (LVType)value.which(); 
		if (t == LV_CUSTOM) return boost::get<intrusive_ptr<LogicalData>&>(value)->class_type;
		return t;
	}
	bool ground() const { return type() != LV_UNINSTANCIATED; }
};

inline LVar::LVar() : intrusive_ptr<LogicalVariant>(new LogicalVariant(UNINSTANCIATED)) { }
inline LVar::LVar(NilType) : intrusive_ptr<LogicalVariant>(new LogicalVariant(NIL)) { }
inline LVar::LVar(UninstanciatedType) : intrusive_ptr<LogicalVariant>(new LogicalVariant(UNINSTANCIATED)) { }
inline LVar::LVar(char * const c): intrusive_ptr<LogicalVariant>(new LogicalVariant(InternedString(c))){}
inline LVar::LVar(double d): intrusive_ptr<LogicalVariant>(new LogicalVariant(d)){}
//Note it's not safe to make this chain because it's used as a copy constructor
inline LVar::LVar(const LVar &v) : intrusive_ptr<LogicalVariant>(v) {}
inline LVar::LVar(LogicalVariant *v) :intrusive_ptr<LogicalVariant>(v) {}
inline LVar::LVar(InternedString s) : intrusive_ptr<LogicalVariant>(new LogicalVariant(s)) {}
inline void LVar::chain(LVar &o) { (*this)->value = o; }


struct GetAddress : public boost::static_visitor<>
{
	void *address;
	template <typename T>
	void operator()(T &t) const { address = &t; }
};


LValue& LVar::get_target()
{
	LVar *t = this;
	GetAddress get_address;
	while ((*t)->type() == LV_LVAR) t = &boost::get<LVar>((*t)->value);
	return (*t)->value;
}

inline ostream & operator<<(ostream & os, const LogicalVariant &v)
{
	switch (v.type()) {
	case LV_UNINSTANCIATED:
		os << "Var" << &v;
		break;
	case LV_LVAR:
		os <<"->("<< &v.value <<')'<<v.value;
		break;
	default:
		os << "[" << &v.value << ']' << v.value;
	}
	return os;
}


inline LogicalVariant * LInit()
{
	return new LogicalVariant();
}

inline ostream & operator<<(ostream & os, const LVar &v)
{
	os << *v;
	return os;
}


class LCons :public intrusive_ref_counter<LCons, boost::thread_unsafe_counter>
{
public:
	LCons(LValue first):car(new LogicalVariant(first)), cdr(new LogicalVariant(NIL)) {}
	LCons(LValue first, LValue rest) :car(new LogicalVariant(first)), cdr(new LogicalVariant(rest)) {}
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
inline LVar::LVar(intrusive_ptr<LCons> &c):intrusive_ptr<LogicalVariant>(new LogicalVariant(c)) {}

typedef std::vector<boost::any> AnyValues;

//Much to my surprise Search in C++ is only 1/3 more lines than the lua version. 
class Search;
//Note: the search parameter only seems necessary in the final continuation that 
//reports when the search has failed, the rest of the time it could be captured... but 
//it's simpler to pass than capture especially since other values are captured by value
//and this one would have to be captured by reference. 
//The other times it has to be passed are in direct calls not continuations
typedef std::function<void(Search &)> Continuation;

struct clean_stack_exception
{
};

const int STACK_SAVER_DEPTH = 30;

class Search
{
	enum AmbTag { AMB_UNDO, AMB_ALT };
	struct AmbRecord {
		AmbTag tag;
		Continuation cont;
	};
	std::vector<AmbRecord> amblist;
	static void fail_fn(Search &s) 
	{
		s.failed = true;
		s.save_undo(fail_fn);
	}
	void new_amblist()
	{
		failed = false;
		started = false;
		amblist.erase(amblist.begin(), amblist.end());
		amblist.push_back(AmbRecord{ AMB_UNDO,(Continuation)fail_fn});
	}
	bool started;
	int stack_saver;
	Continuation cont;
	bool failed;
	//used by friend function fail() ie, lets you use fail as a continuation 
	Continuation _for_fail()
	{
		Continuation c = amblist.back().cont;
		amblist.pop_back();
		return c;
	}
	void start() { tail(initial); }
public:
	Continuation initial;
	std::map<const char *,boost::any> results;
	friend void fail(Search &s);
	bool running() { return !failed;  }
	void save_undo(Continuation c) { amblist.push_back(AmbRecord{AMB_UNDO,c}); }
	void alt(Continuation c) { amblist.push_back(AmbRecord{ AMB_ALT,c }); }

	void tail(Continuation c) 
	{
		if (--stack_saver < 1) {
			cont = c;
//			cout << "throwing!" << endl;
			throw(clean_stack_exception());
		}
		c(*this);
	}

	//Note this can throw a clean_stack_exception so only call in tail position
	//you can use the fail() function defined below the class as a continuation
	//instead
	void fail() {
		Continuation c = amblist.back().cont;
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
//			cout << "try!" << endl;
			if (!started) {
				started = true;
				failed = false;
				start();
			}
			else fail();
//			cout << "end try!" << endl;
		}
		catch (clean_stack_exception &) {
//			cout << "caught!" << endl;
			stack_saver = STACK_SAVER_DEPTH;
			retry = true;
		}
		while (retry) {
			retry = false;
			try {
//				cout << "try!" << endl;
				cont(*this);
//				cout << "end try!" << endl;
			}
			catch (clean_stack_exception &) {
//				cout << "caught!" << endl;
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


template <typename T>
class CapturedVarLetter :public intrusive_ref_counter<CapturedVarLetter<T>, boost::thread_unsafe_counter >
{
public:
	T value;
	CapturedVarLetter(const T& a) :value(a) {}
	CapturedVarLetter() {}
};

template<typename T>
class CapturedVar : public intrusive_ptr< CapturedVarLetter<T> >
{
public:
	CapturedVar(const T& v) :intrusive_ptr(new CapturedVarLetter<T>(v)) {}
	CapturedVar() :intrusive_ptr(new CapturedVarLetter<T>()) {}

	CapturedVar(const CapturedVar<T> &o):intrusive_ptr(static_cast<const intrusive_ptr< CapturedVarLetter<T> > &>(o)) {}

	T& operator *() { return (*static_cast<intrusive_ptr< CapturedVarLetter<T> >* >(this))->value; }
	T& operator *() const { return (*static_cast<const intrusive_ptr< CapturedVarLetter<T> > *>(this))->value; }
};

typedef CapturedVar<Continuation> CapturedCont;

//oops, the return value could be nixed by stack clean exception
//but it worked when I made it always throw... {}{}{} WHY DOES IT WORK?
//OH it works because it doesn't use the search until AFTER it returns the value
Continuation stream1(CapturedVar<int> m, CapturedCont c)
{
	CapturedVar<int> n(0);
	CapturedCont rest;
	*rest = [=](Search &s)
	{ 
		*n = *n + 1;
		if (*n == 10) {
			s.fail();
		}
		else {
			s.alt(*rest);
//			cout << "n is " << *n << endl;
			*m = *n;
			s.tail(*c);
		}
	};
	return *rest;
}


//Note it's probably cheaper to pass a CapturedCont than a Continuation
Continuation stream2(CapturedVar<int> m, CapturedCont c)
{
	CapturedVar<int> n(0);
	CapturedCont rest;

	*rest = [=](Search &s)
	{
		*n += 1;
		if (*n == 4) {
			s.fail();
		}
		else {
			s.alt(*rest);
//			cout << "m is " << *n * *n << endl;
			*m = *n * *n;
			s.tail(*c);
		}
	};
	return *rest;
}

void AmbTest(Search &s)
{
	CapturedVar<int> n, m;
	CapturedCont c1, c2, c3;
	//note it can't safely use Search inside of functions that return a value
	*c1 = [=](Search &s) { s.tail(stream1(n,c2)); };
	*c2 = [=](Search &s) { s.tail(stream2(m,c3)); };
	*c3 = [=](Search &s)
	{
		if (*n != *m) s.fail();
		else {
			s.results.insert_or_assign("n", *n);
			s.results.insert_or_assign("m", *m);
		}
	};
	s.tail(*c1);
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
	std::cout << TypeNames[D->type()] << std::endl;
	std::cout << A <<' '<< B << ' ' << C << ' ' << D<<' '<<E<<' '<<F<<' '<<&F.get_target() << std::endl;

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
