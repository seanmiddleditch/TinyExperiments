// This code is released under the terms of the "CC0" license.  Full terms and conditions
// can be found at: http://creativecommons.org/publicdomain/zero/1.0/

#include <iostream>
#include <type_traits>

// for tests
#define ASSERT_EQ(expected, actual) \
	do \
	{ \
		std::cout << "EXPECT: " #actual " == " #expected << std::endl; \
		auto actual_v = (actual); \
		auto expected_v = (expected); \
		if (actual_v == expected_v) \
		{ \
			std::cout << "   GOT: " << actual_v << " == " << expected_v << ": OK" << std::endl; \
		} \
		else \
		{ \
			std::cout << "   GOT: " << actual_v << " != " << expected_v << ": FAIL!" << std::endl; \
			std::terminate(); \
		} \
	} while(false)

// this is a simple delegate that only supports functors with signature int(int),
// but never ever allocates memory.  it uses a fixed-size buffer internally to be
// able to support functors (and lambdas) of varying reasonable sizes to be stored
// without copies.
// goal is to find a reasonable size for this buffer that fits everything which
// Real Code(tm) needs without wasting excess memory.  huge benefit for games where
// memory allocation is bad.
// naturally, this can all be achieved with std::function<> and a custom allocator,
// but what fun is that?  ... actually that's totally the smarter option for "normal"
// C++11 apps.  games typically replace half the STL anyway though, and a smaller
// specialized delegate type will be faster to compile than std::function too.
struct delegate
{
	// maximimum size of functors that can be stored in the delegate.
	// size is not (completely) random.  chosen so that max_size + sizeof(void*)
	// is evenly divisible by sizeof(double) on both 32-bit and 64-bit platforms.
	// any odd scalar >= 3 is fine here.
	// note that with a real implementation, this could be a template parameter
	// with a default value, so you could control the size on a per-use basis.
	// not doing that here as for demo purposes that's overkill.
	static const size_t max_size = sizeof(void*) * 3;

	// virtual base class for our binding, used to wrap up operations that must
	// be performed on underlying types.  used for type erasure of those types.
	// our use is technically a total hack, but works on all "real" systems we
	// care about.  in particular, we use the this pointer and an offset to find
	// the object we're operating on, since we don't use the this pointer for
	// anything else, and it's going to be passed into this methods anyway.
	struct binding_base
	{
		virtual int invoke(int) = 0;
		virtual void copy_construct(const void* source) = 0;
		virtual void move_construct(void* source) = 0;
		virtual void destruct() = 0;
	};

	union
	{
		// force alignment of our struct to that of double, which is the
		// strictest aligned type in standard C++.  not enough for SIMD
		// types, but that's a larger problem in general.
		double alignme;

		// buffer in which we will store functors copied into the delegate.
		// this must be big enough to store any functor you want to assign
		// to the delegate.  if you get errors about functors being too
		// big, you either need to increase this buffer size, or make your
		// functors smaller (usually the latter).
		char buffer[max_size];
	};

	// this is not actually a pointer to the binding class.  this actually
	// _contains_ the binding class.  the idea is that the binding class is
	// just a vtable pointer, which is just a pointer, and so can fit in a
	// void*.  plus, copying the void* means copying the vtable pointer,
	// which you can't normally do.  of course this is all completely
	// implementation defined behavior, as there is no guarantee that
	// virtual methods are even implemented with a virtual table of any
	// kind... but for all our target compilers, this works.  i really
	// wish there was a more well-defined way to do this, though.
	void* binding;

	// default constructor (most useful comment in the sample code)
	delegate() : binding(nullptr) {}

	// magic delegate constructor that can actually create all the necessary
	// data given the right binding vtable.  this shoudl ideally be private,
	// but we're not keeping secrets from clients in this demo code.
	template <typename Functor>
	delegate (void* binding_ptr, Functor&& functor) : binding(binding_ptr)
	{
		// determine whether we're moving or copying, based on whether we have
		// a real rvalue reference or not.  remember, Functor&& in our
		// signature doesn't mean rvalue reference, because the type is a
		// templated type.  nah, C++ isn't confusing at all.
		if (std::is_rvalue_reference<decltype(functor)>::value)
			reinterpret_cast<binding_base*>(&binding)->move_construct(&functor);
		else
			reinterpret_cast<binding_base*>(&binding)->copy_construct(&functor);
	}

	// copy constructor, which needs to invoke our binding to ensure that
	// functors are copied correctly.
	delegate(const delegate& rhs) : binding(rhs.binding)
	{
		if (binding != nullptr)
			reinterpret_cast<binding_base*>(&binding)->copy_construct(rhs.buffer);
	}

	// move constructor, which needs to invoke our binding to ensure that
	// functors are moved correctly.
	delegate(delegate&& rhs) : binding(rhs.binding)
	{
		if (binding != nullptr)
			reinterpret_cast<binding_base*>(&binding)->move_construct(rhs.buffer);
	}

	// destructor must be sure to invoke the destruct of the stored functor,
	// since it might contain a std::unique_ptr or std::vector or something
	// else that must be destructed properly.
	~delegate()
	{
		if (binding != nullptr)
			reinterpret_cast<binding_base*>(&binding)->destruct();
	}

	// copy assignment operator, which destroys our old functor if present
	// and constructs a new one.  we do that since we can only use copy
	// assignment on the functor objects if they're of identical type, which
	// is rather unlikely (in this demo, at least); maybe it's worthwhile
	// to test if the bindings are equal (meaning the same functor type),
	// but I lean away from extra dynamic branches in general.
	delegate& operator=(const delegate& rhs)
	{
		if (this != &rhs)
		{
			// destroy current copy
			if (binding != nullptr)
				reinterpret_cast<binding_base*>(&binding)->destruct();

			// get the new vtable so we can operate on the incoming type
			// all proper like
			binding = rhs.binding;

			// copy incoming type, assuming we're not being assigned to
			// the empty delegate.
			if (binding != nullptr)
				reinterpret_cast<binding_base*>(&binding)->copy_construct(rhs.buffer);
		}

		return *this;
	}

	// move assignment operator.  note that we destruct the old functor
	// we have, since we don't support bound assignment operators.
	delegate& operator=(delegate&& rhs)
	{
		if (this != &rhs)
		{
			// destroy current copy
			if (binding != nullptr)
				reinterpret_cast<binding_base*>(&binding)->destruct();

			// get the new vtable so we can operate on the incoming type
			// all proper like
			binding = rhs.binding;

			// copy incoming type, assuming we're not being assigned to
			// the empty delegate.  destroy the moved-from delegate
			// so it can clearly have its destructor called with no
			// side-effects.  obviously would be better to support
			// real move semantics here.
			if (binding != nullptr)
				reinterpret_cast<binding_base*>(&binding)->move_construct(rhs.buffer);
		}

		return *this;
	}

	// binds a functor to a delegate.  note that while this is set up to
	// support move semantics, those don't actually work on lambdas.
	template <typename Functor>
	static delegate make(Functor&& functor)
	{
		// checks to ensure that we're not trying to store an incompatible
		// functor.  we have a fixed size for our buffer, and we don't support
		// over-strict alignment.
		static_assert(sizeof(functor) <= max_size, "Functor is too large for delegate; too many capture variables in lamba expression");
		static_assert(std::alignment_of<Functor>::value <= std::alignment_of<Functor>::value, "Functor alignment is too strict for delegate");

		static_assert(std::is_destructible<Functor>::value, "Functor is not destructible; use make_ref instead if possible");
		static_assert(std::is_copy_constructible<Functor>::value, "Functor is not copy constructible; use make_ref instead if possible");

		// instantiate our magic binding class so that we can convert it
		// into a void*.  we're basically casting its vtable pointer to
		// a void*.  total hack.
		void* binding;
		static_assert(sizeof(binding_value<Functor>) == sizeof(binding), "Size of binding is not equal to size of pointer; compiler incompatible with fixed-size delegates");
		new (&binding) binding_value<Functor>();

		return delegate(binding, std::forward<Functor>(functor));
	}

	// binds a functor to a delegate, but as a reference/pointer.  this
	// does not copy the functor.  this of course is a waste of space in
	// our buffer, but sometimes you might need a single delegate instance
	// that can store either a reference or a copy.
	template <typename Functor>
	static delegate make_ref(Functor&& functor)
	{
		// instantiate our magic binding class so that we can convert it
		// into a void*.  we're basically casting its vtable pointer to
		// a void*.  total hack.
		void* binding;
		static_assert(sizeof(binding_reference<Functor>) == sizeof(binding), "Size of binding is not equal to size of pointer; compiler incompatible with fixed-size delegates");
		new (&binding) binding_reference<Functor>();

		return delegate(binding, &functor);
	}

	// public way to check if the delegate is empty (cannot be called) or
	// or not.
	bool empty() const { return binding == nullptr; }

	// if Visual Studio had full C++11, this would be a good operator to have.
	// bool conversion operators without explicit conversion support are just
	// a bad idea in my experience, though (that's why they added explicit
	// conversion operators; I mean, not my experience specifically, but
	// the world's shared C++ experience), so I'm avoiding it.
	// explicit operator bool() const { return binding == nullptr; }

	// invoke our delegate.  does not check if the delegate is not bound.
	// if you support exceptions, throw one, otherwise you should probably
	// assert in debug builds at the very least.
	// never call is empty() returns true.
	int operator()(int x)
	{
		return reinterpret_cast<binding_base*>(&binding)->invoke(x);
	}

	// implementation of our bindings for standard functors/lambdas (copied
	// into the delegate by value).
	template <typename T>
	struct binding_value : public binding_base
	{
		virtual int invoke(int p)
		{
			return (*reinterpret_cast<T*>(reinterpret_cast<char*>(this) - delegate::max_size))(p);
		}

		virtual void copy_construct(const void* source)
		{
			new (reinterpret_cast<char*>(this) - delegate::max_size) T(*reinterpret_cast<const T*>(source));
		}

		virtual void move_construct(void* source)
		{
			new (reinterpret_cast<char*>(this) - delegate::max_size) T(std::move(*reinterpret_cast<T*>(source)));
		}

		virtual void destruct()
		{
			reinterpret_cast<T*>(reinterpret_cast<char*>(this) - delegate::max_size)->~T();
		}
	};

	// implementation of our bindings for functors/lambdas bound by reference.
	// mostly no-ops or simple copies of pointer values.
	template <typename T>
	struct binding_reference : public binding_base
	{
		virtual int invoke(int p)
		{
			return (**reinterpret_cast<T**>(reinterpret_cast<char*>(this) - delegate::max_size))(p);
		}

		virtual void copy_construct(const void* source)
		{
			*reinterpret_cast<T**>(reinterpret_cast<char*>(this) - delegate::max_size) = *reinterpret_cast<T* const*>(source);
		}

		virtual void move_construct(void* source)
		{
			*reinterpret_cast<T**>(reinterpret_cast<char*>(this) - delegate::max_size) = *reinterpret_cast<T* const*>(source);
		}

		virtual void destruct()
		{
			// no-op
		}
	};
};

// this struct is intentionally designed to be too big to fit
// inside the buffer of the delegate.
struct toobig
{
	char huge_buffer[delegate::max_size + 1];
};

// stats for side-effects class
struct unit_stats
{
	int constructed;
	int copied;
	int destructed;

	unit_stats() : constructed(0), copied(0), destructed(0) {}
};

// this struct has side effects on construction, copy-construction,
// move-construction, copy-assignment, move-assignment, and destruction.
// it serves to test that all the correct methods get called when
// copying functors into a delegate.
// leaving the move variants in even though they never get called
// for lambdas.
// values and reset() are for unit testing.
struct side_effects
{
	unit_stats& stats;

	side_effects(unit_stats& stats) : stats(stats) { ++stats.constructed; }
	~side_effects() { ++stats.destructed; }
	side_effects(const side_effects& rhs) : stats(rhs.stats) { ++stats.copied; }
	side_effects(side_effects&& rhs) : stats(rhs.stats) { ++stats.copied; }
};

// a regular old-style functor wrapper for testing stuff
struct Functor
{
	side_effects fx;

	Functor(unit_stats& stats) : fx(stats) {}
	Functor(const Functor& rhs) : fx(rhs.fx) {}
	Functor(Functor&& rhs) : fx(std::move(rhs.fx)) {}

	int operator()(int x) { return x; }
};

// bunch of tests.  should be self-explanatory.
void test1()
{
	auto d1 = delegate::make([](int x){ return x * x; });
	auto d2 = delegate::make([](int x){ return x + 2 * x; });

	ASSERT_EQ(25, d1(5));
	ASSERT_EQ(15, d2(5));
}

void test2()
{
	int x1 = 8;
	int x2 = 12;

	auto d3 = delegate::make([=](int x){ return x * x1 + x2; });

	ASSERT_EQ(52, d3(5));
}

void test3()
{
	int x1 = 8;
	int x2 = 12;

	auto d3 = delegate::make([&](int x){ return x * x1 + x2; });

	ASSERT_EQ(52, d3(5));

	x1 = 3;
	x2 = 7;

	ASSERT_EQ(22, d3(5));
}

void test4()
{
	toobig big;

	// FAILS TO COMPILE: toobig make the lambda unable to fit inside delegate
	//auto d4 = delegate::make([big](int x){ return x; });

	auto d5 = delegate::make_ref([big](int x){ return x; });

	ASSERT_EQ(5, d5(5));
}

void test5()
{
	unit_stats stats;

	{
		side_effects fx(stats);;

		ASSERT_EQ(1, stats.constructed);

		auto d6 = delegate::make([fx](int x){ return x; });

		ASSERT_EQ(2, stats.copied);
		ASSERT_EQ(1, stats.destructed);

		ASSERT_EQ(5, d6(5));
	}

	ASSERT_EQ(3, stats.destructed);
}

void test6()
{
	unit_stats stats;

	{
		side_effects fx(stats);

		ASSERT_EQ(1, stats.constructed);

		auto d1 = delegate::make_ref([fx](int x){ return x; });

		ASSERT_EQ(1, stats.copied);
		ASSERT_EQ(1, stats.destructed);

		ASSERT_EQ(5, d1(5));

		d1 = delegate::make([&fx](int x){ return x + 2 * x; });

		ASSERT_EQ(1, stats.copied);
		ASSERT_EQ(1, stats.destructed);

		ASSERT_EQ(15, d1(5));
	}

	ASSERT_EQ(2, stats.destructed);
}

void test7()
{
	unit_stats stats;

	{
		auto d8 = delegate::make(Functor(stats));

		ASSERT_EQ(1, stats.constructed);
		ASSERT_EQ(1, stats.copied);
		ASSERT_EQ(1, stats.destructed);

		ASSERT_EQ(5, d8(5));
	}

	ASSERT_EQ(2, stats.destructed);
}

void test8()
{
	unit_stats stats;

	{
		side_effects fx(stats);

		ASSERT_EQ(1, stats.constructed);
		
		auto d1 = delegate::make([fx](int x){ return x + 2 * x; });

		ASSERT_EQ(2, stats.copied);
		ASSERT_EQ(1, stats.destructed);

		ASSERT_EQ(15, d1(5));

		d1 = delegate::make_ref([fx](int x){ return x; });

		ASSERT_EQ(3, stats.destructed);

		ASSERT_EQ(5, d1(5));
	}

	ASSERT_EQ(4, stats.destructed);
}

void(*tests[])() = {
	&test1,
	&test2,
	&test3,
	&test4,
	&test5,
	&test6,
	&test7,
	&test8,
	nullptr
};

int main()
{
	for (int i = 0; tests[i] != nullptr; ++i)
	{
		std::cout << "*** test " << (i + 1) << " ***" << std::endl;
		tests[i]();
	}

	// wait for key press
	char tmp;
	std::cin.read(&tmp, 1);
	return 0;
}
