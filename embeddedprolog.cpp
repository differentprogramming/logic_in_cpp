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
#include <stdint.h>

//#define OWN_MEMORY_MANAGEMENT

using std::ostream;
using std::cout;
using std::endl;
using std::string;
using boost::any_cast;
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

//Much to my surprise Search in C++ is only 1/3 more lines than the lua version. 
class Search;
//Note: the search parameter only seems necessary in the final continuation that 
//reports when the search has failed, the rest of the time it could be captured... but 
//it's simpler to pass than capture especially since other values are captured by value
//and this one would have to be captured by reference. 
//The other times it has to be passed are in direct calls not continuations
typedef std::function<void(Search &)> Continuation;
//typedef std::initializer_list<boost::any> Params;
//typedef std::function<void(Search&, Params)> TailWithParams;

template<typename T>
class CapturedVar;

#ifdef OWN_MEMORY_MANAGEMENT
struct FreeList
{
	FreeList * next;
};
const int FREE_LIST_BLOCK_SIZE = 32768;
template<typename T> void *allocate_from_freelist()
{
	if (T::free_list == nullptr) {
		void *block = malloc(FREE_LIST_BLOCK_SIZE);
		intptr_t align;
		if (T::blocksize > 16) align = 16;
		else align = T::blocksize;
		intptr_t offset = ((reinterpret_cast<intptr_t>(block) + align - 1)& ~(align - 1)) - reinterpret_cast<intptr_t>(block);
		FreeList* p = nullptr;
		for (intptr_t i = offset;i <= FREE_LIST_BLOCK_SIZE - T::blocksize; i += T::blocksize) {
			FreeList* const f = reinterpret_cast<FreeList *>(i + reinterpret_cast<intptr_t>(block));
			f->next = p;
			p = f;
		}
		T::free_list = p;
	}
	FreeList* r = T::free_list;
	T::free_list = r->next;
	return static_cast<void *>(r);
}
template <typename T>
void free_to_freelist(void *v)
{
	if (v == nullptr) return;
	FreeList* r = static_cast<FreeList*>(v);
	r->next = T::free_list;
	T::free_list = r;
}
#endif

#define DECLARE_MEM_MANAGEMENT(name)			\
static intptr_t blocksize;						\
static FreeList *free_list;						\
void * operator new (size_t size)				\
{												\
	assert(size == sizeof(name));				\
	return allocate_from_freelist<name>();		\
}												\
void * operator new (size_t, void *place)		\
{												\
	return place;								\
}												\
void operator delete (void *, void *) {}		\
												\
void operator delete (void * mem)				\
{												\
	free_to_freelist<name>(mem);				\
}


#define DEFINE_MEM_MANAGEMENT(name)	\
intptr_t name::blocksize = intptr_t(((sizeof(name) + 15)>>4)<<4); \
FreeList *name::free_list = nullptr;

#define DEFINE_MEM_MANAGEMENT_T(name,...)	\
template <__VA_ARGS__>\
intptr_t name::blocksize = intptr_t(((sizeof(name) + 15)>>4)<<4); \
template <__VA_ARGS__>\
FreeList *name::free_list = nullptr;

//very simple not thread safe class compatible with intrusive_ptr
//the point of this class is that you can define both counted and eternal/singleton objects
//and the built in class doesn't have that
class SimpleRefCount
{
protected:
	static const int SINGLETON = -1000000;
public:
	int _ref;
	int use_count() const { return _ref; }
	void _inc() { if (_ref != SINGLETON) ++_ref; }
	SimpleRefCount * _dec() {
		if (_ref != SINGLETON) if (0 == --_ref) return this;
		return nullptr;
	}
	SimpleRefCount() :_ref(0) {}
	SimpleRefCount(int initial) :_ref(initial) {}
	virtual ~SimpleRefCount()
	{}

};


void intrusive_ptr_add_ref(SimpleRefCount *p)
{
	p->_inc();
}
void intrusive_ptr_release(SimpleRefCount *p)
{
	SimpleRefCount * d = p->_dec();
	if (d) delete d;
}


class TrampolineLetter :public SimpleRefCount
{
public:
	TrampolineLetter() {}
	TrampolineLetter(int i) :SimpleRefCount(i) {}
	virtual ~TrampolineLetter() {}
	virtual intrusive_ptr<TrampolineLetter> execute() = 0;
	virtual bool isNull() const { return false; }
	virtual void _for_retargetting(void *n) { }
};


template <typename T>
class Trampoline0 : public TrampolineLetter
{
	T fn;
public:
	Trampoline0(const T &f) :fn(f) {  }
	virtual intrusive_ptr<TrampolineLetter> execute() { return fn(); }
};

class NullTrampoline : public TrampolineLetter
{
public:
	NullTrampoline() :TrampolineLetter(SimpleRefCount::SINGLETON) { }
	virtual bool isNull() const { return true; }
	virtual intrusive_ptr<TrampolineLetter> execute() { return this; }
};

template <typename T, typename P1>
class Trampoline1 : public TrampolineLetter
{
	T fn;
	P1 p1;
public:
	Trampoline1(const T &f, const P1 &_p1) :fn(f), p1(_p1) {}
	virtual intrusive_ptr<TrampolineLetter> execute() { return fn(p1); }
	virtual void _for_retargetting(void *n) { *static_cast<T **>(n) = &fn; }
};

template <typename T, typename P1, typename P2>
class Trampoline2 : public TrampolineLetter
{
	T fn;
	P1 p1;
	P2 p2;
public:
	Trampoline2(const T &f, const P1 &_p1, const P2 &_p2) :fn(f), p1(_p1), p2(_p2) {}
	virtual intrusive_ptr<TrampolineLetter> execute() { return fn(p1, p2); }
	virtual void _for_retargetting(void *n) { *static_cast<T **>(n) = &fn; }
};

template <typename T, typename P1, typename P2, typename P3>
class Trampoline3 : public TrampolineLetter
{
	T fn;
	P1 p1;
	P2 p2;
	P3 p3;
public:
	Trampoline3(const T &f, const P1 &_p1, const P2 &_p2, const P3 &_p3) :fn(f), p1(_p1), p2(_p2), p3(_p3) {}
	virtual intrusive_ptr<TrampolineLetter> execute() { return fn(p1, p2, p3); }
	virtual void _for_retargetting(void *n) { *static_cast<T **>(n) = &fn; }
};

template <typename T, typename P1, typename P2, typename P3, typename P4>
class Trampoline4 : public TrampolineLetter
{
	T fn;
	P1 p1;
	P2 p2;
	P3 p3;
	P4 p4;
public:
	Trampoline4(const T &f, const P1 &_p1, const P2 &_p2, const P3 &_p3, const P4 &_p4) :fn(f), p1(_p1), p2(_p2), p3(_p3), p4(_p4) {}
	virtual intrusive_ptr<TrampolineLetter> execute() { return fn(p1, p2, p3, p4); }
	virtual void _for_retargetting(void *n) { *static_cast<T **>(n) = &fn; }
};

template <typename T, typename P1, typename P2, typename P3, typename P4, typename P5>
class Trampoline5 : public TrampolineLetter
{
	T fn;
	P1 p1;
	P2 p2;
	P3 p3;
	P4 p4;
	P5 p5;
public:
	Trampoline5(const T &f, const P1 &_p1, const P2 &_p2, const P3 &_p3, const P4 &_p4, const P5 &_p5) :fn(f), p1(_p1), p2(_p2), p3(_p3), p4(_p4), p5(_p5) {}
	virtual intrusive_ptr<TrampolineLetter> execute() { return fn(p1, p2, p3, p4, p5); }
	virtual void _for_retargetting(void *n) { *static_cast<T **>(n) = &fn; }
};
template <typename T, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>
class Trampoline6 : public TrampolineLetter
{
	T fn;
	P1 p1;
	P2 p2;
	P3 p3;
	P4 p4;
	P5 p5;
	P6 p6;
public:
	Trampoline6(const T &f, const P1 &_p1, const P2 &_p2, const P3 &_p3, const P4 &_p4, const P5 &_p5, const P6 &_p6) :fn(f), p1(_p1), p2(_p2), p3(_p3), p4(_p4), p5(_p5), p6(_p6) {}
	virtual intrusive_ptr<TrampolineLetter> execute() { return fn(p1, p2, p3, p4, p5, p6); }
	virtual void _for_retargetting(void *n) { *static_cast<T **>(n) = &fn; }
};

template <typename T, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>
class Trampoline7 : public TrampolineLetter
{
	T fn;
	P1 p1;
	P2 p2;
	P3 p3;
	P4 p4;
	P5 p5;
	P6 p6;
	P7 p7;
public:
	Trampoline7(const T &f, const P1 &_p1, const P2 &_p2, const P3 &_p3, const P4 &_p4, const P5 &_p5, const P6 &_p6, const P7 &_p7) :fn(f), p1(_p1), p2(_p2), p3(_p3), p4(_p4), p5(_p5), p6(_p6), p7(_p7) {}
	virtual intrusive_ptr<TrampolineLetter> execute() { return fn(p1, p2, p3, p4, p5, p6, p7); }
	virtual void _for_retargetting(void *n) { *static_cast<T **>(n) = &fn; }
};

template <typename T, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8>
class Trampoline8 : public TrampolineLetter
{
	T fn;
	P1 p1;
	P2 p2;
	P3 p3;
	P4 p4;
	P5 p5;
	P6 p6;
	P7 p7;
	P8 p8;
public:
	Trampoline8(const T &f, const P1 &_p1, const P2 &_p2, const P3 &_p3, const P4 &_p4, const P5 &_p5, const P6 &_p6, const P7 &_p7, const P8 &_p8) :fn(f), p1(_p1), p2(_p2), p3(_p3), p4(_p4), p5(_p5), p6(_p6), p7(_p7), p8(_p8) {}
	virtual intrusive_ptr<TrampolineLetter> execute() { return fn(p1, p2, p3, p4, p5, p6, p7, p8); }
	virtual void _for_retargetting(void *n) { *static_cast<T **>(n) = &fn; }
};

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
	CombinableRefCount() :_ref(0), _next(nullptr), _forward(nullptr) {}
	virtual ~CombinableRefCount()
	{
		if (_next) delete _next;
	}
	template<typename T>
	void add_ref(T &one_more)
	{
		if (_forward == nullptr) {
			_forward = new CombinableRefCount;
			_forward->_ref = _ref;
			_forward->_next = this;
		}
		CombinableRefCount *o = one_more.get();
		_forward->_ref += o->_ref;
		o->_forward = _forward;
		o->_next = _forward->_next;
		_forward->_next = o;


	}
#ifdef OWN_MEMORY_MANAGEMENT
	static intptr_t blocksize;
	static FreeList *free_list;
	void * operator new (size_t size)
	{
		assert(size == sizeof(CombinableRefCount));
		return allocate_from_freelist<CombinableRefCount>();
	}
	void * operator new (size_t, void *place)
	{
		return place;
	}
	void operator delete (void *, void *) {}

	void operator delete (void * mem)
	{
		free_to_freelist<CombinableRefCount>(mem);
	}
#endif
};

#ifdef OWN_MEMORY_MANAGEMENT
intptr_t CombinableRefCount::blocksize = intptr_t((sizeof(CombinableRefCount) + sizeof(CombinableRefCount) - 1)&~((sizeof(CombinableRefCount) + sizeof(CombinableRefCount) - 1) >> 1));
FreeList *CombinableRefCount::free_list = nullptr;
#endif

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
	typedef T type;
	T value;
	CapturedVarLetter(const T& a) :value(a) {}
	CapturedVarLetter() {}
	T& operator *() { return value; }
	T* operator ->() { return &value; }
#ifdef OWN_MEMORY_MANAGEMENT
	static intptr_t blocksize;
	static FreeList *free_list;
	void * operator new (size_t size)
	{
		assert(size == sizeof(CapturedVarLetter<T>));
		return allocate_from_freelist<CapturedVarLetter<T>>();
	}
	void * operator new (size_t, void *place)
	{
		return place;
	}
	void operator delete (void *, void *) {}

	void operator delete (void * mem)
	{
		free_to_freelist<CapturedVarLetter<T>>(mem);
	}
#endif
};

#ifdef OWN_MEMORY_MANAGEMENT
template <typename T>
intptr_t CapturedVarLetter<T>::blocksize = intptr_t((sizeof(CapturedVarLetter<T>) + sizeof(CapturedVarLetter<T>) - 1)&~((sizeof(CapturedVarLetter<T>) + sizeof(CapturedVarLetter<T>) - 1) >> 1));
template <typename T>
FreeList *CapturedVarLetter<T>::free_list = nullptr;
#endif
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

enum CombineRefType { CombineRef };

template<typename T>
class CapturedVar : public intrusive_ptr< CapturedVarLetter<T> >
{
public:
	CapturedVar(const CapturedVarLetter<T>& v) :intrusive_ptr(&const_cast<CapturedVarLetter<T>&>(v))
	{

	}
	CapturedVar(const T& v) :intrusive_ptr(new CapturedVarLetter<T>(v)) {}

	template<typename U>
	CapturedVar(CombineRefType, const CapturedVar<U> &c) : intrusive_ptr(new CapturedVarLetter<T>()) { c.get()->add_ref(*this); }
	template<typename U>
	CapturedVar(CombineRefType, const UncountedVar<U> &c) : intrusive_ptr(new CapturedVarLetter<T>()) { c.get()->add_ref(*this); }


	CapturedVar() :intrusive_ptr(new CapturedVarLetter<T>()) {}

	CapturedVar(const CapturedVar<T> &o) :intrusive_ptr(static_cast<const intrusive_ptr< CapturedVarLetter<T> > &>(o)) {}
	CapturedVar(const UncountedVar<T> &o);
	void clear()
	{
		*static_cast<intrusive_ptr< CapturedVarLetter<T> > *>(this) = nullptr;
	}

	auto operator()() { return (this->operator *())(); }
	template <typename P1>
	auto operator()(P1 &&p1) { return (this->operator *())(p1); }
	template <typename P1, typename P2>
	auto operator()(P1 &&p1, P2 &&p2) { return (this->operator *())(p1, p2); }
	template <typename P1, typename P2, typename P3>
	auto operator()(P1 &&p1, P2 &&p2, P3 &&p3) { return (this->operator *())(p1, p2, p3); }
	template <typename P1, typename P2, typename P3, typename P4>
	auto operator()(P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4) { return (this->operator *())(p1, p2, p3, p4); }
	template <typename P1, typename P2, typename P3, typename P4, typename P5>
	auto operator()(P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4, P5 &&p5) { return (this->operator *())(p1, p2, p3, p4, p5); }
	template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>
	auto operator()(P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4, P5 &&p5, P6 &&p6) { return (this->operator *())(p1, p2, p3, p4, p5, p6); }
	template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>
	auto operator()(P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4, P5 &&p5, P6 &&p6, P7 &&p7) { return (this->operator *())(p1, p2, p3, p4, p5, p6, p7); }
	template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8>
	auto operator()(P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4, P5 &&p5, P6 &&p6, P7 &&p7, P8 &&p8) { return (this->operator *())(p1, p2, p3, p4, p5, p6, p7, p8); }

	T * operator->() { return &get()->value; }
	T * operator->() const { return &get()->value; }
	T& operator *() { return get()->value; }
	T& operator *() const { return get()->value; }
};


//typedef CapturedVarLetter<Continuation> *UncountedCont;
template <typename T>
class UncountedVar
{
public:
	CapturedVarLetter<T> * value;
	UncountedVar(const CapturedVar<T> &c) :value(c.get()) {}
	template<typename U>
	UncountedVar(CombineRefType, const CapturedVar<U> &c) : value(new CapturedVarLetter<T>()) { c.get()->add_ref(*this); }
	template<typename U>
	UncountedVar(CombineRefType, const UncountedVar<U> &c) : value(new CapturedVarLetter<T>()) { c.get()->add_ref(*this); }

	CapturedVarLetter<T> * get() const {
		return const_cast<CapturedVarLetter<T> *>(value);
	}

	T * operator->() { return &get()->value; }
	T * operator->() const { return &get()->value; }
	T& operator *() { return value->value; }
	T& operator *() const { return const_cast<T&>(value->value); }

	auto operator()() { return (this->operator *())(); }
	template <typename P1>
	auto operator()(P1 &&p1) { return (this->operator *())(p1); }
	template <typename P1, typename P2>
	auto operator()(P1 &&p1, P2 &&p2) { return (this->operator *())(p1, p2); }
	template <typename P1, typename P2, typename P3>
	auto operator()(P1 &&p1, P2 &&p2, P3 &&p3) { return (this->operator *())(p1, p2, p3); }
	template <typename P1, typename P2, typename P3, typename P4>
	auto operator()(P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4) { return (this->operator *())(p1, p2, p3, p4); }
	template <typename P1, typename P2, typename P3, typename P4, typename P5>
	auto operator()(P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4, P5 &&p5) { return (this->operator *())(p1, p2, p3, p4, p5); }
	template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>
	auto operator()(P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4, P5 &&p5, P6 &&p6) { return (this->operator *())(p1, p2, p3, p4, p5, p6); }
	template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>
	auto operator()(P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4, P5 &&p5, P6 &&p6, P7 &&p7) { return (this->operator *())(p1, p2, p3, p4, p5, p6, p7); }
	template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8>
	auto operator()(P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4, P5 &&p5, P6 &&p6, P7 &&p7, P8 &&p8) { return (this->operator *())(p1, p2, p3, p4, p5, p6, p7, p8); }
};

template<typename T>
CapturedVar<T>::CapturedVar(const UncountedVar<T> &o) :intrusive_ptr(o.value) {}


template<typename T, typename ... Types>
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


template <typename T>
auto make_counted(T && o) {
	return o;
}
template <typename T>
CapturedVar<T> make_counted(const UncountedVar<T> &o) {
	return *o.get();
}

template <typename T>
CapturedVar<T> make_counted(UncountedVar<T> &o) {
	return *o.get();
}

std::reference_wrapper<Search> make_counted(Search &o) {
	return std::ref(o);
}

template <typename T, typename P1>
inline TrampolineLetter* new_trampoline(T &&f)
{
	return new Trampoline0<T>(f);
}

template <typename T, typename P1>
inline TrampolineLetter* new_trampoline(T &&f, P1 &&p1)
{
	return new Trampoline1<T, P1>(f, p1);
}
template <typename T, typename P1, typename P2>
inline TrampolineLetter * new_trampoline(T &&f, P1 &&p1, P2 &&p2)
{
	return new Trampoline2<T, P1, P2>(f, p1, p2);
}
template <typename T, typename P1, typename P2, typename P3>
inline TrampolineLetter * new_trampoline(T &&f, P1 &&p1, P2 &&p2, P3 &&p3)
{
	return new Trampoline3<T, P1, P2, P3>(f, p1, p2, p3);
}
template <typename T, typename P1, typename P2, typename P3, typename P4>
inline TrampolineLetter * new_trampoline(T &&f, P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4)
{
	return new Trampoline4<T, P1, P2, P3, P4>(f, p1, p2, p3, p4);
}
template <typename T, typename P1, typename P2, typename P3, typename P4, typename P5>
inline TrampolineLetter * new_trampoline(T &&f, P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4, P5 &&p5)
{
	return new Trampoline5<T, P1, P2, P3, P4, P5>(f, p1, p2, p3, p4, p5);
}
template <typename T, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>
inline TrampolineLetter * new_trampoline(T &&f, P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4, P5 &&p5, P6 &&p6)
{
	return new Trampoline6<T, P1, P2, P3, P4, P5, P6>(f, p1, p2, p3, p4, p5, p6);
}
template <typename T, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>
inline TrampolineLetter * new_trampoline(T &&f, P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4, P5 &&p5, P6 &&p6, P7 &&p7)
{
	return new Trampoline7<T, P1, P2, P3, P4, P5, P6, P7>(f, p1, p2, p3, p4, p5, p6, p7);
}
template <typename T, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8>
inline TrampolineLetter * new_trampoline(T &&f, P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4, P5 &&p5, P6 &&p6, P7 &&p7, P8 &&p8)
{
	return new Trampoline8<T, P1, P2, P3, P4, P5, P6, P7, P8>(f, p1, p2, p3, p4, p5, p6, p7, p8);
}


template <typename T>
intrusive_ptr<TrampolineLetter> trampoline(T &&f)
{
	return new_trampoline(make_counted(f));
}
template <typename T, typename P1>
intrusive_ptr<TrampolineLetter> trampoline(T &&f, P1 &&p1)
{
	return new_trampoline(make_counted(f), make_counted(p1));
}
template <typename T, typename P1, typename P2>
intrusive_ptr<TrampolineLetter> trampoline(T &&f, P1 &&p1, P2 &&p2)
{
	return new_trampoline(make_counted(f), make_counted(p1), make_counted(p2));
}
template <typename T, typename P1, typename P2, typename P3>
intrusive_ptr<TrampolineLetter>  trampoline(T &&f, P1 &&p1, P2 &&p2, P3 &&p3)
{
	return new_trampoline(make_counted(f), make_counted(p1), make_counted(p2), make_counted(p3));
}
template <typename T, typename P1, typename P2, typename P3, typename P4>
intrusive_ptr<TrampolineLetter>  trampoline(T &&f, P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4)
{
	return new_trampoline(make_counted(f), make_counted(p1), make_counted(p2), make_counted(p3), make_counted(p4));
}
template <typename T, typename P1, typename P2, typename P3, typename P4, typename P5>
intrusive_ptr<TrampolineLetter>  trampoline(T &&f, P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4, P5 &&p5)
{
	return new_trampoline(make_counted(f), make_counted(p1), make_counted(p2), make_counted(p3), make_counted(p4), make_counted(p5));
}
template <typename T, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>
intrusive_ptr<TrampolineLetter>  trampoline(T &&f, P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4, P5 &&p5, P6 &&p6)
{
	return new_trampoline(make_counted(f), make_counted(p1), make_counted(p2), make_counted(p3), make_counted(p4), make_counted(p5), make_counted(p6));
}
template <typename T, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>
intrusive_ptr<TrampolineLetter>  trampoline(T &&f, P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4, P5 &&p5, P6 &&p6, P7 &&p7)
{
	return new_trampoline(make_counted(f), make_counted(p1), make_counted(p2), make_counted(p3), make_counted(p4), make_counted(p5), make_counted(p6), make_counted(p7));
}
template <typename T, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8>
intrusive_ptr<TrampolineLetter>  trampoline(T &&f, P1 &&p1, P2 &&p2, P3 &&p3, P4 &&p4, P5 &&p5, P6 &&p6, P7 &&p7, P8 &&p8)
{
	return new_trampoline(make_counted(f), make_counted(p1), make_counted(p2), make_counted(p3), make_counted(p4), make_counted(p5), make_counted(p6), make_counted(p7), make_counted(p8));
}

typedef intrusive_ptr<TrampolineLetter> Trampoline;

Trampoline end_search = new NullTrampoline();


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
	void set_car(LVar &t);
	void set_cdr(LVar &t);

//	LV_NIL,LV_UNINSTANCIATED,LV_DOUBLE,LV_STRING,LV_LVAR,LV_LIST,LV_CUSTOM,LV_DATA1,LV_DATA2,LV_DATA3,LV_DATA4
	LVType type() const;
	bool uninstanciatedp() { return type() == LV_UNINSTANCIATED; }
	bool doublep() { return type() == LV_DOUBLE; }
	bool stringp() { return type() == LV_STRING; }
	bool lvarp() { return type() == LV_LVAR; }
	void operator = (const LVar &o);


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
//	bool operator ==(LVar &);
#ifdef OWN_MEMORY_MANAGEMENT
	static intptr_t blocksize;
	static FreeList *free_list;
	void * operator new (size_t size)
	{
		assert(size == sizeof(LVar));
		return allocate_from_freelist<LVar>();
	}
	void * operator new (size_t , void *place)
	{
		return place;
	}
	void operator delete (void *, void *) {}

	void operator delete (void * mem)
	{
		free_to_freelist<LVar>(mem);
	}
#endif
};

#ifdef OWN_MEMORY_MANAGEMENT
intptr_t LVar::blocksize = intptr_t((sizeof(LVar) + sizeof(LVar) - 1)&~((sizeof(LVar) + sizeof(LVar) - 1) >> 1));
FreeList *LVar::free_list = nullptr;
#endif


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
#ifdef OWN_MEMORY_MANAGEMENT
	static intptr_t blocksize;
	static FreeList *free_list;
	void * operator new (size_t size)
	{
		assert(size == sizeof(LogicalVariant));
		return allocate_from_freelist<LogicalVariant>();
	}
	void * operator new (size_t, void *place)
	{
		return place;
	}
	void operator delete (void *, void *) {}
	void operator delete (void * mem)
	{
		free_to_freelist<LogicalVariant>(mem);
	}
#endif
};

#ifdef OWN_MEMORY_MANAGEMENT
intptr_t LogicalVariant::blocksize = intptr_t((sizeof(LogicalVariant) + sizeof(LogicalVariant) - 1)&~((sizeof(LogicalVariant) + sizeof(LogicalVariant) - 1) >> 1));
FreeList *LogicalVariant::free_list = nullptr;
#endif

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
inline void LVar::operator = (const LVar &o) { (*this)->value = o->value; }

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

//typedef CapturedVar<LVar> CLVar;
//typedef UncountedVar<LVar> ULVar;


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
#ifdef OWN_MEMORY_MANAGEMENT
	static intptr_t blocksize;
	static FreeList *free_list;
	void * operator new (size_t size)
	{
		assert(size == sizeof(LCons));
		return allocate_from_freelist<LCons>();
	}
	void * operator new (size_t, void *place)
	{
		return place;
	}
	void operator delete (void *, void *) {}

	void operator delete (void * mem)
	{
		free_to_freelist<LCons>(mem);
	}
#endif
};

#ifdef OWN_MEMORY_MANAGEMENT
intptr_t LCons::blocksize = intptr_t((sizeof(LCons) + sizeof(LCons) - 1)&~((sizeof(LCons) + sizeof(LCons) - 1) >> 1));
FreeList *LCons::free_list = nullptr;
#endif

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

void LVar::set_car(LVar &t) { return as_LCons()->car.get_target()=t; }
void LVar::set_cdr(LVar &t) { return as_LCons()->cdr.get_target() = t; }


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


//typedef std::vector<boost::any> AnyValues;

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

//emulate lua's == 
//the types that are primitive in lua are compared by value
//the other types are compared by address
//used for a quick equality test in unify
class are_strict_equals
	: public boost::static_visitor<bool>
{
public:

	template <typename T, typename U>
	bool operator()(const T &, const U &) const
	{
		return false; // cannot compare different types
	}

	template <typename T>
	bool operator()(const T & lhs, const T & rhs) const
	{
		return lhs == rhs;
	}
	template <>
	bool operator()(const UninstanciatedType & lhs, const UninstanciatedType & rhs) const
	{
		return &lhs == &rhs;
	}
	template <>
	bool operator()(const LVar & lhs, const LVar & rhs) const
	{
		return &lhs->value == &rhs->value;
	}
	template <>
	bool operator()(const intrusive_ptr<LCons> & lhs, const intrusive_ptr<LCons> & rhs) const
	{
		return & (*lhs) == & (*rhs);
	}
	template <>
	bool operator()(const intrusive_ptr<LogicalData> & lhs, const intrusive_ptr<LogicalData> & rhs) const
	{
		return &(*lhs) == &(*rhs);
	}
};

bool strict_equals(LVar &a, LVar &b)
{
	return  boost::apply_visitor(are_strict_equals(), a->value, b->value);
}

bool _unify(Search &s, LVar &a, LVar&b);
bool _identical(LVar &a, LVar&b);

#define CapturedLambda(...) CapturedVar<std::function<Trampoline (__VA_ARGS__) >>
#define UncountedLambda(...) UncountedVar<std::function<Trampoline (__VA_ARGS__) >>

typedef CapturedLambda(Search &) CapturedCont;
typedef UncountedLambda(Search &) UncountedCont;


class Search
{
	enum AmbTag { AMB_UNDO, AMB_ALT };
	struct AmbRecord {
		AmbTag tag;
		Trampoline cont;
		AmbRecord(AmbTag t, Trampoline c) :tag(t), cont(c) {}

	};
	std::vector<AmbRecord> amblist;

	Trampoline captured_fail;

	static Trampoline fail_fn(Search &s)
	{
		s.failed = true;
		s.save_undo(s.captured_fail);
		return end_search;
	}

	void new_amblist()
	{
		failed = false;
		started = false;
		amblist.clear();
		amblist.push_back(AmbRecord( AMB_UNDO,captured_fail));
	}
	bool started;
	Trampoline cont;
	bool failed;
	Trampoline initial;
public:

	std::map<const char *,boost::any> results;

	bool running() { return !failed;  }
	void save_undo(const Trampoline &c) { amblist.push_back(AmbRecord(AMB_UNDO,c)); }
	void alt(Trampoline (*c)(Search &)) { amblist.push_back(AmbRecord(AMB_ALT, trampoline(c, *this))); }
	void alt(const CapturedCont &c){
		amblist.push_back(AmbRecord(AMB_ALT, trampoline(c,*this)));
	}
	void alt(const UncountedCont &c) {
		amblist.push_back(AmbRecord(AMB_ALT, trampoline(c, *this)));
	}
	void alt(const Trampoline &c) {
		amblist.push_back(AmbRecord(AMB_ALT, c));
	}

	//Note this can throw a CleanStackException so only call in tail position
	//you can use the fail() function defined below the class as a continuation
	//instead
	Trampoline fail() {
		Trampoline c = amblist.back().cont;
		amblist.pop_back();
		return c; //tail call
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
	template <typename T, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>
	Search(T &&f, P1 && p1, P2 && p2, P3 && p3, P4 && p4, P5 && p5, P6 && p6, P7 && p7) :initial(trampoline(f, *this, p1, p2, p3, p4, p5, p6, p7)), captured_fail(trampoline(fail_fn, *this))
	{
		new_amblist();
	}

	template <typename T, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>
	Search(T &&f, P1 && p1, P2 && p2, P3 && p3, P4 && p4, P5 && p5, P6 && p6) :initial(trampoline(f, *this, p1, p2, p3, p4, p5, p6)), captured_fail(trampoline(fail_fn, *this))
	{
		new_amblist();
	}

	template <typename T, typename P1, typename P2, typename P3, typename P4, typename P5>
	Search(T &&f, P1 && p1, P2 && p2, P3 && p3, P4 && p4, P5 && p5) :initial(trampoline(f, *this, p1, p2, p3, p4, p5)), captured_fail(trampoline(fail_fn, *this))
	{
		new_amblist();
	}

	template <typename T, typename P1, typename P2, typename P3, typename P4>
	Search(T &&f, P1 && p1, P2 && p2, P3 && p3, P4 && p4) :initial(trampoline(f, *this, p1, p2, p3, p4)), captured_fail(trampoline(fail_fn, *this))
	{
		new_amblist();
	}

	template <typename T, typename P1, typename P2, typename P3>
	Search(T &&f, P1 && p1, P2 && p2, P3 && p3) :initial(trampoline(f, *this, p1, p2, p3)), captured_fail(trampoline(fail_fn, *this))
	{
		new_amblist();
	}

	template <typename T, typename P1, typename P2>
	Search(T &&f, P1 && p1, P2 && p2) :initial(trampoline(f, *this, p1, p2)), captured_fail(trampoline(fail_fn, *this))
	{
		new_amblist();
	}

	template <typename T, typename P1>
	Search(T &&f, P1 && p1) :initial(trampoline(f, *this,p1)), captured_fail(trampoline(fail_fn, *this))
	{
		new_amblist();
	}

	template <typename T>
	Search(T &&f ):initial(trampoline(f, *this)), captured_fail(trampoline(fail_fn, *this))
	{
		new_amblist();
	}
	Search(const Trampoline i) :initial(i), captured_fail (trampoline( fail_fn, *this))
	{
		new_amblist();
	}
	~Search() {}
	bool operator() ()
	{
		Trampoline c;
		if (!started) {
			started = true;
			failed = false;
			c=initial;
		}
		else c=fail();
		do { c = c->execute(); } while (!c->isNull());
		return !failed;
	}
	Trampoline  unify(LVar a, LVar b, Trampoline c)
	{
		if (!_unify(*this, a, b)) return fail();
		return c;
	}
	Trampoline identical(LVar a, LVar b, Trampoline c)
	{
		if (!_identical(a, b)) return fail();
		return c;
	}
	Trampoline not_identical(LVar a, LVar b, Trampoline c)
	{
		if (_identical(a, b)) return fail();
		return c;
	}
};

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
		a_target->value= b_target.get()->value;
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
		if (!_identical( a_target.car(), b_target.car())) return false;
		return _identical( a_target.cdr(), b_target.cdr());
	}
	return false;
}

//rolling my own so that I can be sure that iterators are preserved across deletes.
enum ClauseRootType { ClauseRoot };
enum InsertHeadType { ClauseHead };
enum InsertTailType { ClauseTail };


//while it's not strictly necessary, this is properly garbage collected so the programmer
//is freed from designing around lifetime issues.  If you delete a DynamicPredicate while
//its clauses are running, nothing will break;
struct DynamicClauseBase : public SimpleRefCount
{
	intrusive_ptr<DynamicClauseBase> next; 
	DynamicClauseBase* prev;

	virtual bool is_root() const { return true; }
	bool empty() const { return next == this; }
	DynamicClauseBase(ClauseRootType) : SimpleRefCount(SimpleRefCount::SINGLETON)
	{
		cout << "creating root " << this << endl;
		next = prev = this; 
	}
	DynamicClauseBase(InsertHeadType, DynamicClauseBase *root) : next(root->next), prev(root) 
	{ 
		cout << "creating at head " << this << endl;
		root->next = this; next->prev = this;
	}
	DynamicClauseBase(InsertTailType, DynamicClauseBase *root) : next(root), prev(root->prev) 
	{ 
		cout << "creating at tail " << this << endl;
		root->prev = this; prev->next = this;
	}

	void unlink() 
	{ 
		if (next != nullptr) next->prev = prev; 
		if (prev != nullptr) prev->next = next;
		next = nullptr;
		prev = nullptr;
	}
	virtual ~DynamicClauseBase() 
	{ 
		cout << "deleting " << this << endl;
		unlink();  
	}

};
template <typename T>
struct DynamicClauseT :public DynamicClauseBase
{
	T value;

	virtual bool is_root() const { return false; }

	DynamicClauseT(InsertHeadType i, DynamicClauseBase &root, T f) : DynamicClauseBase(i, &root), value(f) { }
	DynamicClauseT(InsertTailType i, DynamicClauseBase &root, T f) : DynamicClauseBase(i, &root), value(f) { }
};

typedef intrusive_ptr<DynamicClauseBase> DynamicClause;

//typedef std::function<void(Search&,)> DynamicCont;

template <typename T> struct gimme;

template <template <typename> typename TT, typename R, typename...Args>
struct gimme<TT<R(Args...)>> {
	using type = R(Args...);
};


// T should be CapturedLambda(function...)
template <typename T>
class DynamicPredicate {
protected:
	DynamicClauseBase root;

	static Trampoline next_clause(Search &s, Trampoline last_template, Trampoline final_continuation, intrusive_ptr<DynamicClauseT<T>> last_ptr, T *stored_fn)
	{
		if (last_ptr->next->is_root()) return final_continuation;
		last_ptr = static_cast<DynamicClauseT<T> *>(last_ptr->next.get());
		*stored_fn = last_ptr.value;
		s.alt(next_clause, last_template, final_continuation, last_ptr, stored_fn);
		return last_template;
	}

	Trampoline _apply(Search&s, Trampoline initial, Trampoline final_continuation)
	{
		intrusive_ptr<DynamicClauseT<T>> first = static_cast<DynamicClauseT<T>*>(root->next);
		T * stored_fn=nullptr;
		initial->_for_retargetting(&stored_fn);
		assert(stored_fn != nullptr);
		s.alt(template(next_clause<T>, s, initial, final_continuation, first, stored_fn));
		return initial;
	}
public:
	DynamicPredicate() :root(ClauseRoot) {}
	~DynamicPredicate() { 
		root.next->prev = nullptr;
		root.next = nullptr; //break chain		
	}
	

	DynamicClause asserta(T f)
	{
		return new DynamicClauseT<T>(ClauseHead, root, f);
	}
	DynamicClause asserta(typename T::element_type f)
	{
		return asserta(T(f));
	}
	DynamicClause asserta(typename gimme<typename T::element_type::type>::type f)
	{
		return asserta(T(f));
	}
	DynamicClause assertz(T f)
	{
		return new DynamicClauseT<T>(ClauseTail, root, f);
	}
	DynamicClause assertz(typename T::element_type f)
	{
		return assertz(T(f));
	}
	DynamicClause assertz(typename gimme<typename T::element_type::type>::type f)
	{
		return assertz(T(f));
	}

	void retract(DynamicClause c)
	{
		c->unlink();
	}
	void retract_all()
	{
		root->next->prev = nullptr;
		root->next = &root; //break chain	
		root->prev = &root;
	}
	
	Trampoline operator () (Search &s,Trampoline c)
	{
		if (root.empty()) return s.fail();
		int snip = s.snip_start();
		return _appy(s, template(root->next->value, s, c, snip),c);
	}
	template <typename P1>
	Trampoline operator () (Search &s, Trampoline c, P1 && p1)
	{
		if (root.empty()) return s.fail();
		int snip = s.snip_start();
		return _appy(s, template(root->next->value, s, c, snip, p1), c);
	}
	template <typename P1, typename P2>
	Trampoline operator () (Search &s, Trampoline c, P1 && p1, P2 && p2)
	{
		if (root.empty()) return s.fail();
		int snip = s.snip_start();
		return _appy(s, template(root->next->value, s, c, snip, p1,p2), c);
	}
	template <typename P1, typename P2, typename P3>
	Trampoline operator () (Search &s, Trampoline c, P1 && p1, P2 && p2, P3 && p3)
	{
		if (root.empty()) return s.fail();
		int snip = s.snip_start();
		return _appy(s, template(root->next->value, s, c, snip, p1, p2,p3), c);
	}
	template <typename P1, typename P2, typename P3, typename P4>
	Trampoline operator () (Search &s, Trampoline c, P1 && p1, P2 && p2, P3 && p3, P4 && p4)
	{
		if (root.empty()) return s.fail();
		int snip = s.snip_start();
		return _appy(s, template(root->next->value, s, c, snip, p1, p2, p3,p4), c);
	}
	template <typename P1, typename P2, typename P3, typename P4, typename P5>
	Trampoline operator () (Search &s, Trampoline c, P1 && p1, P2 && p2, P3 && p3, P4 && p4, P5 && p5)
	{
		if (root.empty()) return s.fail();
		int snip = s.snip_start();
		return _appy(s, template(root->next->value, s, c, snip, p1, p2, p3, p4,p5), c);
	}

};


Trampoline unify_tests(Search &s)
{
	LVar A, B, C,D,E,F,G;
	LVar hello("hello"), one(1), willBeHello, willBeOne,l1(L(A,"hello",B,L(one,C,hello),F));
	CapturedCont c,d,e,f,g,h,i,j,k,l;
	*c = [=](Search &s)
	{ 
		cout << hello <<"?="<< willBeHello << endl;
		return s.identical(1, one, trampoline(d,s));
	};
	*d = [=](Search &s) { 
		cout << one << "?=" << willBeOne << endl;
		s.alt(f);
		return s.identical(hello, "hello", trampoline(e,s));
	};
	*e = [=](Search &s) { 
		cout << "compare with string succeeded" << endl;
		s.alt(g);
		return s.identical(F, G, trampoline(h,s));

	};
	*f = [=](Search &s) { cout << "compare with string failed" << endl; return end_search; };
	*g = [=](Search &s) 
	{ 
		cout << "unlike compare with vars did the right thing" << endl;
		s.alt(i);
		return s.unify(l1, L("Say", D, "there", L(E, 2, "hello"), G),trampoline(j,s));
	};
	*h = [=](Search &s) { cout << "unlike compare with vars did the wrong thing" << endl; return end_search; };
	*i = [=](Search &s) { cout << "list unify failed" << A << " " << D << " " << B << " " << E << " " << C << endl; return end_search; };
	*j = [=](Search &s) { s.alt(l); return s.identical(F,G,trampoline(k,s));};
	*k = [=](Search &s) { cout << "list unify: " <<A<<" "<<D<<" "<<B<<" "<<E<<" "<<C<<" "<<F<<" "<<G<< endl; return end_search; };
	*l = [=](Search &s) { cout << "var unify failed" << endl; return end_search; };


	return s.unify(hello, willBeHello,trampoline(c,s));
}
//oops, the return value could be nixed by stack clean exception
//but it worked when I made it always throw... {}{}{} WHY DOES IT WORK?
//OH it works because it doesn't use the search until AFTER it returns the value
Trampoline stream1(Search &s, CapturedVar<int> m, Trampoline c)
{
	CapturedLambda(Search &, int) rest;
	UncountedLambda(Search &, int) rest_uncounted = rest;


	*rest = [=](Search &s, int n)
	{ 
		n = n + 1;
		if (n == 10) {
			return s.fail();
		}
		else {
			s.alt(trampoline(rest_uncounted,s,n));
			*m = n;
//			cout << "n is " << *n << endl;
			return c;
		}
	};
	cout << rest.get()->use_count() << endl;
	return trampoline(rest,s,0);
}


//Note it's probably cheaper to pass a CapturedCont than a Continuation
Trampoline stream2(Search &s, CapturedVar<int> m, Trampoline c)
{
	CapturedLambda(Search &, int) rest;
	UncountedLambda(Search &, int) rest_uncounted = rest;

	*rest = [=](Search &s, int n)
	{
		n += 1;
		if (n == 4) {
			return s.fail();
		}
		else {
			s.alt(trampoline(rest_uncounted,s,n));
//			cout << "m is " << *n * *n << endl;
			*m = n * n;
			return c;
		}
	};
	return trampoline(rest,s,0);
}

Trampoline AmbTest(Search &s)
{
	CapturedVar<int> n, m;
	CapturedCont c1, c2, c3;
	UncountedCont c1_u = c1, c2_u = c2, c3_u = c3;
	combine_refs(c1, c2, c3);

	//note it can't safely use Search inside of functions that return a value
	*c1 = [=](Search &s) { return stream1(s, n, trampoline(c2_u, s)); };
	*c2 = [=](Search &s) { return stream2(s,m,trampoline(c3_u, s)); };
	*c3 = [=](Search &s)
	{
		if (*n != *m) return s.fail();
		else {
			s.results.insert_or_assign("n", *n);
			s.results.insert_or_assign("m", *m);
			return end_search;
		}
	};
	cout << c1.get()->use_count() << endl;
	cout << c2.get()->use_count() << endl;
	cout << c3.get()->use_count() << endl;
	return trampoline(c1,s);
}

#define OUT_OS_TYPE(TYPE) if (v.type() == typeid(TYPE)) { os << any_cast<TYPE>(v); } else
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

#define QUEENS 20
int rowsx[QUEENS];
bool distinct(int x1, int y1, int x2, int y2)
{
	return x1 != x2 && y1 != y2 && x1 + y1 != x2 + y2 && x1 - y1 != x2 - y2;
}
bool distinct_from_row(int x, int y, int r)
{
	return distinct(x, y, rowsx[r - 1], r);
}

bool distinct_from_all(int x, int y)
{
	for (int i = 1; i <= y - 1;++i) {
		if (!distinct_from_row(x, y, i)) return false;
	}
	rowsx[y - 1] = x;
	return true;
}
/*
local function queen_row(C,row)
local function loop(C,n)
local function loop_rest()
return loop(C,n+1)
end
if n<=queens then
amb_next(loop_rest)
--        print ('try',n,row)
if not distinct_from_all(n,row) then return amb() end
if row<queens then return queen_row(C,row+1) end
return C()
end
return amb()
end
return loop(C,1)
end
*/
Trampoline QueenRow(Search &s, int ru)// {int row}
{
	//CapturedVar<int> r = ru;
	CapturedCont c;
	CapturedLambda(Search &, int,int) loop;
	UncountedLambda(Search &,int,int) loopu = loop;

//	cout << "r = " << *r << endl;

	*c = [=](Search &s) 
	{
		cout << "Solution: ";
		for (int y = 0;y < QUEENS;++y) cout << rowsx[y] << ' ';
		cout << endl;
		return end_search;
	};
	*loop = [=](Search &s, int  n, int r)
	{
		//CapturedVar<int> nu = n;
//		UncountedLambda(Search &, int, int) loop_restu(CombineRef,loopu);

		
//		*loop_restu = [=](Search &s, int n,int r) { return trampoline(loopu,s, n + 1, r ); };
		if (n <= QUEENS) {
			s.alt(trampoline(loopu,s,n+1,r));
			if (!distinct_from_all(n, r)) return s.fail();
			else {
				if (r < QUEENS) return trampoline(QueenRow,s, r + 1 );
				else return trampoline(c, s);
			}
		}else return s.fail();
	};
	return trampoline(loop, s, 1 , ru);
}

//verb([eats | O], O, v(eats)).
//verb([plays with | O], O, v(plays with)).
Trampoline verb(Search &s, Trampoline c, LVar X, LVar Y, LVar Z)
{
	LVar O;

	CapturedCont rest;
	*rest = [=](Search &s) { return s.unify(L(X, Y, Z), L(L("plays","with", DOT, O), O, L("v", "plays", "with")), c); };
	s.alt(rest);
	return s.unify(L(X, Y, Z), L(L("eats", DOT, O), O, L("v", "eats")), c);
}


//noun([bat | O], O, n(bat)).
//noun([cat | O], O, n(cat)).

Trampoline noun(Search &s, Trampoline c, LVar X, LVar Y, LVar Z)
{
	LVar O;

	CapturedCont rest;
	*rest = [=](Search &s) { return s.unify(L(X, Y, Z), L(L("cat", DOT, O), O, L("n","cat")),c); };
	s.alt(rest);
	return s.unify(L(X, Y, Z), L(L("bat", DOT, O), O, L("n", "bat")),c);
}

//det([the | O], O, d(the)).
//det([a | O], O, d(a)).
Trampoline det(Search &s, Trampoline c, LVar X, LVar Y, LVar Z)
{
	LVar O;

	CapturedCont rest;
	*rest = [=](Search &s) { return s.unify(L(X, Y, Z), L(L("a", DOT, O), O, L("d", "a")), c); };
	s.alt(rest);
	return s.unify(L(X, Y, Z), L(L("the", DOT, O), O, L("d", "the")), c);
}
//noun_phrase(A,B,np(D,N)) :- det(A,C,D), noun(C,B,N).
Trampoline noun_phrase(Search &s, Trampoline c, LVar X, LVar Y, LVar Z)
{
	LVar A, B, C, D, N;
	CapturedCont r1, r2;

	*r1 = [=](Search &s) { return trampoline(det, s, trampoline(r2,s),A,C,D ); };
	*r2 = [=](Search &s) { return trampoline(noun, s, c,C,B,N ); };
	return s.unify(L(X, Y, Z), L(A, B, L("np", D, N)), trampoline(r1,s));
}

//verb_phrase(A,B,vp(V,NP)):- verb(A,C,V), noun_phrase(C,B,NP).
Trampoline verb_phrase(Search &s, Trampoline c, LVar X, LVar Y, LVar Z)
{
	LVar  A,B,C,V,NP;
	CapturedCont r1,r2;

	*r1 = [=](Search &s) { return trampoline(verb, s, trampoline(r2,s),A,C,V); };
	*r2 = [=](Search &s) { return trampoline(noun_phrase, s, c,C,B,NP); };
	return s.unify(L(X,Y,Z),L(A,B,L("vp",V,NP)), trampoline(r1,s) );
}
//sentence(A, B, s(NP, VP)) :-noun_phrase(A, C, NP), verb_phrase(C, B, VP).
Trampoline sentence(Search &s, Trampoline c, LVar X, LVar Y, LVar Z)
{
	LVar  A, B, C, VP, NP;
	CapturedCont r1, r2;

	*r1 = [=](Search &s) { return trampoline(noun_phrase, s, trampoline(r2,s),A,C,NP ); };
	*r2 = [=](Search &s) { return trampoline(verb_phrase, s, c,C,B,VP ); };
	return s.unify(L(X, Y, Z), L(A, B, L("s", NP, VP)), trampoline(r1,s));
}

Trampoline gen_sentences(Search &s)
{
	LVar T, _, S;
	CapturedCont display;
	*display = [=](Search &s) { cout << "sentence: " << T << endl << "parse: " << S << endl; return end_search; };
	return trampoline(sentence, s, trampoline(display,s),T,_,S );
}



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

	Search g(gen_sentences);
	while (g());


	Search q(QueenRow,  1 );
	q();
	Search u(unify_tests);
	u();

	LVar A1;
	LVar B1("hello"), B2("hello");
	LVar C1(1.0), C2(1.0);
	LVar D1, D2;
	D1.chain(A1);D2.chain(A1);
	cout << "equals tests " << strict_equals(A1,A1 ) << " " << strict_equals(B1,B2 ) << " " << strict_equals(C1,C2 ) << " " << strict_equals(D1,D2 ) << " " << endl;
	LVar A2;
	LVar B3("There");
	LVar C3(2.0);
	LVar D3;D2.chain(A2);
	cout << "equals tests " << strict_equals(A1, A2) << " " << strict_equals(B1, B3) << " " << strict_equals(C1, C3) << " " << strict_equals(D1, D3) << " " << endl;
	
	DynamicPredicate<CapturedLambda(Search &, Trampoline, int, LVar)> dynamic_test;
	DynamicClause dog, cat, person;

	dog = dynamic_test.asserta([](Search &s, Trampoline c, int cut, LVar animal) { return c; });

}
