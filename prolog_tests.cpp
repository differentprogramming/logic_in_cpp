// metagame.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "loopyprolog.h"

Trampoline unify_tests(Search &s)
{
	LVar A, B, C, D, E, F, G;
	LVar hello("hello"), one(1), willBeHello, willBeOne, l1(L(A, "hello", B, L(one, C, hello), F));
	CapturedCont c, d, e, f, g, h, i, j, k, l;
	*c = [=](Search &s)
	{
		cout << hello << "?=" << willBeHello << endl;
		return s.identical(1, one, trampoline(d, s));
	};
	*d = [=](Search &s) {
		cout << one << "?=" << willBeOne << endl;
		s.alt(f);
		return s.identical(hello, "hello", trampoline(e, s));
	};
	*e = [=](Search &s) {
		cout << "compare with string succeeded" << endl;
		s.alt(g);
		return s.identical(F, G, trampoline(h, s));

	};
	*f = [=](Search &s) { cout << "compare with string failed" << endl; return end_search; };
	*g = [=](Search &s)
	{
		cout << "unlike compare with vars did the right thing" << endl;
		s.alt(i);
		return s.unify(l1, L("Say", D, "there", L(E, 2, "hello"), G), trampoline(j, s));
	};
	*h = [=](Search &s) { cout << "unlike compare with vars did the wrong thing" << endl; return end_search; };
	*i = [=](Search &s) { cout << "list unify failed" << A << " " << D << " " << B << " " << E << " " << C << endl; return end_search; };
	*j = [=](Search &s) { s.alt(l); return s.identical(F, G, trampoline(k, s));};
	*k = [=](Search &s) { cout << "list unify: " << A << " " << D << " " << B << " " << E << " " << C << " " << F << " " << G << endl; return end_search; };
	*l = [=](Search &s) { cout << "var unify failed" << endl; return end_search; };


	return s.unify(hello, willBeHello, trampoline(c, s));
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
			s.alt(trampoline(rest_uncounted, s, n));
			*m = n;
			//			cout << "n is " << *n << endl;
			return c;
		}
	};
	cout << rest.get()->use_count() << endl;
	return trampoline(rest, s, 0);
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
			s.alt(trampoline(rest_uncounted, s, n));
			//			cout << "m is " << *n * *n << endl;
			*m = n * n;
			return c;
		}
	};
	return trampoline(rest, s, 0);
}

Trampoline AmbTest(Search &s)
{
	CapturedVar<int> n, m;
	CapturedCont c1, c2, c3;
	UncountedCont c1_u = c1, c2_u = c2, c3_u = c3;
	combine_refs(c1, c2, c3);

	//note it can't safely use Search inside of functions that return a value
	*c1 = [=](Search &s) { return stream1(s, n, trampoline(c2_u, s)); };
	*c2 = [=](Search &s) { return stream2(s, m, trampoline(c3_u, s)); };
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
	return trampoline(c1, s);
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
	CapturedLambda(int, int) loop;
	UncountedLambda(int, int) loopu = loop;

	//	cout << "r = " << *r << endl;

	*c = [=](Search &s)
	{
		cout << "Solution: ";
		for (int y = 0;y < QUEENS;++y) cout << rowsx[y] << ' ';
		cout << endl;
		return end_search;
	};
	*loop = [=, &s](int  n, int r)
	{
		//CapturedVar<int> nu = n;
		//		UncountedLambda(Search &, int, int) loop_restu(CombineRef,loopu);


		//		*loop_restu = [=](Search &s, int n,int r) { return trampoline(loopu,s, n + 1, r ); };
		if (n <= QUEENS) {
			s.alt(trampoline(loopu, n + 1, r));
			if (!distinct_from_all(n, r)) return s.fail();
			else {
				if (r < QUEENS) return QueenRow(s, r + 1);
				else return trampoline(c, s);
			}
		}
		else return s.fail();
	};
	return trampoline(loop, 1, ru);
}

Trampoline QR2end(Search &s)
{
	cout << "Solution: ";
	for (int y = 0;y < QUEENS;++y) cout << rowsx[y] << ' ';
	cout << endl;
	return end_search;
};
Trampoline QueenRow2(Search &s, int ru);

Trampoline QR2loop(Search&s, int  n, int r)
{
	if (n <= QUEENS) {
		s.alt(trampoline(QR2loop, s, n + 1, r));
		if (!distinct_from_all(n, r)) {
			//printf("failed row %d\n", r);
			return s.fail();
		}
		else {
			if (r < QUEENS) return trampoline(QR2loop, s, 1, r + 1);
			else {
				//printf("FOUND!\n");
				return QR2end(s);
			}
		}
	}
	else return s.fail();
}


Trampoline QueenRow2(Search &s, int ru)// {int row}
{
	return trampoline(QR2loop, s, 1, ru);
}


InternedString eats("eats"), plays("plays"), with("with"), bat("bat"), cat("cat"), the("the"), IS_v("v"), IS_d("d"), IS_np("np"), IS_n("n"), IS_a("a");

//verb([eats | O], O, v(eats)).
//verb([plays with | O], O, v(plays with)).
Trampoline verb(Search &s, Trampoline c, LVar X, LVar Y, LVar Z)
{
	LVar O;

	Subclause rest;
	*rest = [=, &s]() { return s.unify(L(X, Y, Z), L(L(plays, with, DOT, O), O, L(IS_v, plays, with)), c); };
	s.alt(trampoline(rest));
	return s.unify(L(X, Y, Z), L(L(eats, DOT, O), O, L(IS_v, eats)), c);
}


//noun([bat | O], O, n(bat)).
//noun([cat | O], O, n(cat)).

Trampoline noun(Search &s, Trampoline c, LVar X, LVar Y, LVar Z)
{
	LVar O;

	Subclause rest;
	*rest = [=, &s]() { return s.unify(L(X, Y, Z), L(L(cat, DOT, O), O, L(IS_n, cat)), c); };
	s.alt(rest);
	return s.unify(L(X, Y, Z), L(L(bat, DOT, O), O, L(IS_n, bat)), c);
}

//det([the | O], O, d(the)).
//det([a | O], O, d(a)).
Trampoline det(Search &s, Trampoline c, LVar X, LVar Y, LVar Z)
{
	LVar O;

	Subclause rest;
	*rest = [=, &s]() { return s.unify(L(X, Y, Z), L(L(IS_a, DOT, O), O, L(IS_d, IS_a)), c); };
	s.alt(rest);
	return s.unify(L(X, Y, Z), L(L(the, DOT, O), O, L(IS_d, the)), c);
}
//noun_phrase(A,B,np(D,N)) :- det(A,C,D), noun(C,B,N).
Trampoline noun_phrase(Search &s, Trampoline c, LVar X, LVar Y, LVar Z)
{
	LVar A, B, C, D, N;
	Subclause r1, r2;

	*r1 = [=, &s]() { return det(s, r2, A, C, D); };
	*r2 = [=, &s]() { return noun(s, c, C, B, N); };
	return s.unify(L(X, Y, Z), L(A, B, L(IS_np, D, N)), r1);
}

//verb_phrase(A,B,vp(V,NP)):- verb(A,C,V), noun_phrase(C,B,NP).
Trampoline verb_phrase(Search &s, Trampoline c, LVar X, LVar Y, LVar Z)
{
	LVar  A, B, C, V, NP;
	Subclause r1, r2;

	*r1 = [=, &s]() { return verb(s, r2, A, C, V); };
	*r2 = [=, &s]() { return noun_phrase(s, c, C, B, NP); };
	return s.unify(L(X, Y, Z), L(A, B, L("vp", V, NP)), r1);
}
//sentence(A, B, s(NP, VP)) :-noun_phrase(A, C, NP), verb_phrase(C, B, VP).
Trampoline sentence(Search &s, Trampoline c, LVar X, LVar Y, LVar Z)
{
	LVar  A, B, C, VP, NP;
	Subclause r1, r2;

	*r1 = [=, &s]() { return noun_phrase(s, r2, A, C, NP); };
	*r2 = [=, &s]() { return verb_phrase(s, c, C, B, VP); };
	return s.unify(L(X, Y, Z), L(A, B, L("s", NP, VP)), r1);
}

Trampoline gen_sentences(Search &s)
{
	LVar T, _, S;
	Subclause display;
	*display = [=, &s]() { cout << "sentence: " << T << endl << "parse: " << S << endl; return end_search; };
	return sentence(s, display, T, _, S);
}



void prolog_tests()
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
	std::cout << A << ' ' << B << ' ' << C << ' ' << D << ' ' << E << ' ' << F << ' ' << &F.get_target() << std::endl;
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


	Search q(QueenRow2, 1);
	q();
	Search u(unify_tests);
	u();

	LVar A1;
	LVar B1("hello"), B2("hello");
	LVar C1(1.0), C2(1.0);
	LVar D1, D2;
	D1.chain(A1);D2.chain(A1);
	cout << "equals tests " << strict_equals(A1, A1) << " " << strict_equals(B1, B2) << " " << strict_equals(C1, C2) << " " << strict_equals(D1, D2) << " " << endl;
	LVar A2;
	LVar B3("There");
	LVar C3(2.0);
	LVar D3;D2.chain(A2);
	cout << "equals tests " << strict_equals(A1, A2) << " " << strict_equals(B1, B3) << " " << strict_equals(C1, C3) << " " << strict_equals(D1, D3) << " " << endl;

	DynamicPredicate<CapturedLambda(Search &, int, Trampoline, LVar)> dynamic_test;
	DynamicClause dog, cat, person;

	dog = dynamic_test.asserta([](Search &s, int cut, Trampoline c, LVar Animal) { return s.unify(Animal, "dog", c); });
	LVar Animal;
	Search animals(std::function<Trampoline(Search&, Trampoline, LVar)>(std::ref(dynamic_test)), end_search, Animal);
	cout << "should be just dog" << endl;
	while (animals()) cout << Animal << endl;
	cat = dynamic_test.assertz([](Search &s, int cut, Trampoline c, LVar Animal) { return s.unify(Animal, "cat", c); });
	cout << "should be dog, cat" << endl;
	animals.reset();
	while (animals()) cout << Animal << endl;
	person = dynamic_test.asserta([](Search &s, int cut, Trampoline c, LVar Animal) { return s.unify(Animal, "person", c); });
	cout << "should be person, dog, cat" << endl;
	animals.reset();
	while (animals()) cout << Animal << endl;

	cout << "should match dog" << endl;
	char buffer[10] = "dog";
	Animal=buffer;
	buffer[0] = 'c';
	buffer[1] = 'a';
	buffer[2] = 't';

	animals.reset();
	while (animals()) cout << Animal << endl;
	Animal = LVar();

	dynamic_test.retract(dog);
	cout << "should be person, cat" << endl;
	animals.reset();
	while (animals()) cout << Animal << endl;
	dynamic_test.retract(person);
	cout << "should be cat" << endl;
	animals.reset();
	while (animals()) cout << Animal << endl;
	dynamic_test.retract(cat);
	cout << "should be empty" << endl;
	animals.reset();
	while (animals()) cout << Animal << endl;
}


